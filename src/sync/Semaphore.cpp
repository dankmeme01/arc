#include <arc/sync/Semaphore.hpp>

using enum std::memory_order;

namespace arc {

using AcquireAwaiter = Semaphore::AcquireAwaiter;
using State = AcquireAwaiter::State;

static bool notify(Waker& waker, AcquireAwaiter* waiter) {
    auto expected = State::Waiting;
    bool exchanged = waiter->m_waitState.compare_exchange_strong(expected, State::Notified, acq_rel, acquire);

    if (exchanged) {
        waker.wake();
        return true;
    } else {
        // was already notified or in init state, do nothing
        return false;
    }
}

Semaphore::Semaphore(size_t permits) : m_permits(permits) {}

AcquireAwaiter Semaphore::acquire() noexcept {
    return AcquireAwaiter{*this};
}

bool Semaphore::tryAcquire() noexcept {
    // attempt to acquire a permit
    size_t current = m_permits.load(::acquire);

    while (true) {
        if (current == 0) {
            return false;
        }

        if (m_permits.compare_exchange_weak(current, current - 1, ::acq_rel, ::acquire)) {
            return true;
        }
    }
}

void Semaphore::release() noexcept {
    this->release(1);
}

void Semaphore::release(size_t n) noexcept {
    for (size_t i = 0; i < n; i++) {
        if (auto waiter = m_waiters.takeFirst()) {
            if (!notify(waiter->waker, waiter->awaiter)) {
                // if the awaiter already got their permit, just add to the semaphore
                m_permits.fetch_add(1, ::relaxed);
            }
        } else {
            m_permits.fetch_add(1, ::relaxed);
        }
    }
}

bool AcquireAwaiter::pollImpl() {
    switch (m_waitState.load(::acquire)) {
        case State::Init: {
            // try to acquire immediately
            if (m_sem.tryAcquire()) {
                m_waitState.store(State::Notified, ::release);
                return true;
            }

            // register the waker
            m_waitState.store(State::Waiting, ::release);
            m_sem.m_waiters.add(*ctx().m_waker, this);

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
    if (!m_sem.tryAcquire()) return false;

    // we got a permit, BUT we need to consider the possibility that someone else just notified us.
    auto expected = State::Waiting;
    if (m_waitState.compare_exchange_strong(expected, State::Notified, acq_rel, ::release)) {
        // no notification, we are good to go
        m_sem.m_waiters.remove(this);
    } else {
        // duplicate acquire, return one permit to the semaphore
        ARC_DEBUG_ASSERT(expected == State::Notified);
        m_sem.release(1);
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
        m_sem.m_waiters.swapData(&other, this);
    }

    return *this;
}

AcquireAwaiter::~AcquireAwaiter() {
    this->reset();
}

void AcquireAwaiter::reset() {
    auto state = m_waitState.load(::acquire);
    if (state == State::Waiting) {
        m_sem.m_waiters.remove(this);
    }

    m_waitState.store(State::Init, ::release);
}

}