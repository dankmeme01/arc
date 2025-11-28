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

void Semaphore::release() noexcept {
    this->release(1);
}

void Semaphore::release(size_t n) noexcept {
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

bool AcquireAwaiter::poll() {
    switch (m_waitState.load(::acquire)) {
        case State::Init: {
            // try to acquire immediately, relaxed because no one else knows about us yet
            if (m_sem.tryAcquire(m_remaining.load(::relaxed))) {
                m_waitState.store(State::Notified, ::relaxed);
                m_remaining.store(0, ::relaxed);
                return true;
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
    if (!m_sem.tryAcquire(remaining)) return false;

    // we got a permit, BUT we need to consider the possibility that someone else just notified us.
    auto expected = State::Waiting;
    if (m_waitState.compare_exchange_strong(expected, State::Notified, acq_rel, ::release)) {
        // no notification, so remove the waiter, but we are not ready to return yet
        m_sem.m_waiters.lock()->remove(this);

        // it's possible that someone else gave us some permits, but not enough to satisfy our request
        // check now if m_remaining still matches the acquired amount
        auto expected = remaining;
        if (!m_remaining.compare_exchange_strong(expected, 0, acq_rel, ::acquire)) {
            size_t extra = remaining - expected;
            // we got some extra permits, return them to the semaphore
            m_sem.release(extra);
            m_remaining.store(0, ::relaxed);
        }
    } else {
        // duplicate acquire, return the permits to the semaphore
        ARC_DEBUG_ASSERT(expected == State::Notified);
        m_sem.release(remaining);
    }

    return true;
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
}

}