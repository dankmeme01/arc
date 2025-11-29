#include <arc/sync/Semaphore.hpp>
#include <arc/util/Assert.hpp>

using enum std::memory_order;

namespace arc {

using AcquireAwaiter = Semaphore::AcquireAwaiter;
using State = AcquireAwaiter::State;

static bool assignPermitsTo(size_t& remaining, AcquireAwaiter* waiter) {
    auto wrem = waiter->m_remaining.load(::acquire);

    while (true) {
        size_t toAssign = std::min(remaining, wrem);

        if (toAssign == 0) {
            // most likely they already acquired the permits on their own
            return false;
        }

        size_t now = wrem - toAssign;
        if (waiter->m_remaining.compare_exchange_weak(wrem, now, acq_rel, acquire)) {
            remaining -= toAssign;
            if (now == 0) {
                // got all permits, now try to change state. if this fails, assume got the permits in another way
                auto expected = State::Waiting;
                return waiter->m_waitState.compare_exchange_strong(expected, State::Notified, acq_rel, acquire);
            } else {
                // still need more permits
                return false;
            }
        }
    }
}

Semaphore::Semaphore(size_t permits) : m_permits(permits) {}

AcquireAwaiter Semaphore::acquire(size_t permits) noexcept {
    return AcquireAwaiter{*this, permits};
}

bool Semaphore::tryAcquire(size_t permits) noexcept {
    // attempt to acquire the permits
    size_t current = m_permits.load(::acquire);

    while (true) {
        if (current < permits) {
            return false;
        }

        if (m_permits.compare_exchange_weak(current, current - permits, ::acq_rel, ::acquire)) {
            return true;
        }
    }
}

size_t Semaphore::tryAcquireAtMost(size_t maxp) noexcept {
    size_t current = m_permits.load(::acquire);

    while (true) {
        if (current == 0) {
            return 0;
        }

        size_t toTake = std::min(current, maxp);
        if (m_permits.compare_exchange_weak(current, current - toTake, ::acq_rel, ::acquire)) {
            return toTake;
        }
    }
}

void Semaphore::release() noexcept {
    this->release(1);
}

void Semaphore::release(size_t n) noexcept {
    if (n == 0) return;

    auto waiters = m_waiters.lock();

    while (n != 0) {
        auto waiter = waiters->first();
        if (!waiter) break;

        if (assignPermitsTo(n, waiter->awaiter)) {
            // the waiter got all the permits they need, wake and remove
            waiter->waker.wake();
            waiters->takeFirst();
        }
    }

    // add the remaining permits to the semaphore
    if (n != 0) {
        m_permits.fetch_add(n, ::release);
    }
}

size_t Semaphore::permits() const noexcept {
    return m_permits.load(::acquire);
}

bool AcquireAwaiter::poll() {
    switch (m_waitState.load(::acquire)) {
        case State::Init: {
            // try to acquire immediately
            auto initial = m_remaining.load(::acquire);
            auto iAcquired = m_sem.tryAcquireAtMost(initial);

            if (iAcquired == initial) {
                m_waitState.store(State::Notified, ::relaxed);
                m_remaining.store(0, ::release);
                return true;
            } else if (iAcquired > 0) {
                m_remaining.fetch_sub(iAcquired, ::acq_rel);
            }

            // register the waker
            m_waitState.store(State::Waiting, ::release);
            m_sem.m_waiters.lock()->add(*ctx().m_waker, this);

            // it's not impossible that a permit was released between our tryAcquire and registering, so try again
            return this->tryAcquireSafe();
        } break;

        case State::Waiting: {
            return this->tryAcquireSafe();
        } break;

        case State::Notified: {
            return true;
        } break;

        default: std::unreachable();
    }
}

bool AcquireAwaiter::tryAcquireSafe() {
    auto remaining = m_remaining.load(::acquire);
    size_t iAcquired = m_sem.tryAcquireAtMost(remaining);

    if (iAcquired == 0) {
        return false;
    }

    size_t extra = 0;
    bool satisfied = false;

    if (m_remaining.compare_exchange_strong(remaining, remaining - iAcquired, acq_rel, ::acquire)) {
        satisfied = (remaining == iAcquired);
    } else {
        while (true) {
            ARC_ASSERT(iAcquired >= remaining);
            extra = iAcquired - remaining;

            if (m_remaining.compare_exchange_weak(remaining, 0, acq_rel, ::acquire)) {
                satisfied = true;
                break;
            }
        }
    }

    // we may have gotten extra permits, return them to the semaphore
    if (extra > 0) {
        m_sem.release(extra);
        extra = 0;
    }

    // set the state to Notified if we are satisfied,
    // if we aren't, really make sure no one else notified us in the meantime

    auto expected = State::Waiting;
    auto stateAfter = satisfied ? State::Notified : State::Waiting;

    if (m_waitState.compare_exchange_strong(expected, stateAfter, acq_rel, ::release)) {
        if (!satisfied) {
            // possible that we got some permits sent our way before the cas
            satisfied = m_remaining.load(::acquire) == 0;
            if (satisfied) {
                m_waitState.store(State::Notified, ::release);
            }
        }

        if (satisfied) {
            // remove the waiter
            m_sem.m_waiters.lock()->remove(this);
        }
    } else {
        // duplicate acquire, return the permits to the semaphore
        ARC_DEBUG_ASSERT(expected == State::Notified);
        auto rem = m_remaining.load(::acquire);
        m_remaining.store(0, ::relaxed);
        m_sem.release(rem);
    }

    return satisfied;
}

AcquireAwaiter::AcquireAwaiter(AcquireAwaiter&& other) noexcept : m_sem(other.m_sem) {
    *this = std::move(other);
}

AcquireAwaiter& AcquireAwaiter::operator=(AcquireAwaiter&& other) noexcept {
    ARC_ASSERT(&m_sem != &other.m_sem, "cannot move assign awaiters from different semaphores");

    this->reset();
    auto state = other.m_waitState.load(::acquire);
    other.m_waitState.store(State::Init, ::release);
    m_waitState.store(state, ::release);

    // were we waiting? re-register ourselves
    if (state == State::Waiting) {
        m_sem.m_waiters.lock()->swapData(&other, this);
    }

    return *this;
}

AcquireAwaiter::~AcquireAwaiter() {
    this->reset();
}

void AcquireAwaiter::reset() {
    auto state = m_waitState.load(::acquire);
    if (state == State::Waiting) {
        m_sem.m_waiters.lock()->remove(this);
    }

    m_waitState.store(State::Init, ::release);

    while (true) {
        auto rem = m_remaining.load(::acquire);
        if (rem == m_requested) {
            break;
        }

        if (m_remaining.compare_exchange_weak(rem, m_requested, acq_rel, ::acquire)) {
            m_sem.release(m_requested - rem);
            break;
        }
    }
}

}