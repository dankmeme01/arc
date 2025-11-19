#include <arc/sync/Semaphore.hpp>

using enum std::memory_order;

namespace arc {

using AcquireAwaiter = Semaphore::AcquireAwaiter;

static void notify(Waker& waker, AcquireAwaiter* waiter) {
    waiter->m_waitState.store(AcquireAwaiter::State::Notified, release);
    waker.wake(); // consume waker
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
            notify(waiter->waker, waiter->awaiter);
        } else {
            m_permits.fetch_add(1, ::relaxed);
        }
    }
}

bool AcquireAwaiter::pollImpl() {
    // try to acquire immediately
    if (m_sem.tryAcquire()) {
        m_waitState.store(State::Notified, ::release);
        return true;
    }

    switch (m_waitState.load(::acquire)) {
        case State::Init: {
            // register the waker
            m_sem.m_waiters.add(*ctx().m_waker, this);
            m_waitState.store(State::Waiting, ::release);

            // it's not impossible that a permit was released between our tryAcquire and registering, so try again
            if (m_sem.tryAcquire()) {
                m_sem.m_waiters.remove(this);
                m_waitState.store(State::Notified, ::release);
                return true;
            }

            return false;
        } break;

        case State::Waiting: {
            return false;
        } break;

        case State::Notified: {
            return true;
        } break;

        default: std::unreachable();
    }
}

AcquireAwaiter::AcquireAwaiter(AcquireAwaiter&& other) noexcept : m_sem(other.m_sem) {
    *this = std::move(other);
}

AcquireAwaiter& AcquireAwaiter::operator=(AcquireAwaiter&& other) noexcept {
    if (&m_sem != &other.m_sem) {
        std::abort();
    }

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
        m_waitState.store(State::Init, ::release);
    }

}

}