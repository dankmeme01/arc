#include <arc/sync/Semaphore.hpp>
#include <arc/util/Assert.hpp>

using enum std::memory_order;

namespace arc {

using AcquireAwaiter = Semaphore::AcquireAwaiter;

Semaphore::Semaphore(size_t permits) : m_permits(permits) {}

AcquireAwaiter Semaphore::acquire(size_t permits) noexcept {
    return AcquireAwaiter{*this, permits};
}

void Semaphore::acquireBlocking(size_t permits) noexcept {
    auto awaiter = this->acquire(permits);

    // temporarily replace waker
    CondvarWaker cvw;
    auto newWaker = cvw.waker();
    auto* oldWaker = ctx().m_waker;
    ctx().m_waker = &newWaker;

    while (!awaiter.poll()) {
        cvw.wait();
    }

    ctx().m_waker = oldWaker;
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

size_t Semaphore::tryAcquireOrRegister(size_t maxp, AcquireAwaiter* awaiter) {
    if (maxp == 0) return 0;

    auto waiters = m_waiters.lock();

    size_t current = m_permits.load(::acquire);

    while (true) {
        if (current == 0) {
            waiters->add(ctx().cloneWaker(), awaiter);
            return 0;
        }

        size_t toTake = std::min(current, maxp);
        if (m_permits.compare_exchange_weak(current, current - toTake, ::acq_rel, ::acquire)) {
            if (toTake < maxp) {
                waiters->add(ctx().cloneWaker(), awaiter);
            }

            return toTake;
        }
    }
}

bool Semaphore::assignPermitsTo(size_t& remaining, AcquireAwaiter* waiter) {
    auto lock = waiter->m_lock.lock();

    size_t wrem = waiter->remaining();
    ARC_DEBUG_ASSERT(wrem > 0 && remaining > 0);

    size_t toAssign = std::min(remaining, wrem);
    waiter->m_acquired += toAssign;
    remaining -= toAssign;

    return waiter->m_acquired == waiter->m_requested;
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

        if (this->assignPermitsTo(n, waiter->awaiter)) {
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
    // 1. m_acquired = 0, !m_registered -> initial state
    // 2. m_acquired < m_requested, m_registered -> waiting state
    // 3. m_acquired == m_requested -> ready state

    auto guard = m_lock.lock();

    if (m_acquired == 0 && !m_registered) {
        // handle initial state, try to acquire fast and then register if failed
        m_acquired = m_sem.tryAcquireOrRegister(m_requested, this);

        if (m_acquired == m_requested) {
            // got all permits
            m_acquired = 0;
            return true;
        }

        m_registered = true;
        return false;
    } else if (m_acquired < m_requested && m_registered) {
        // handle waiting state, not much to do here other than waiting
        return false;
    } else if (m_acquired >= m_requested) {
        // completed!
        ARC_ASSERT(m_acquired == m_requested); // idk if this is possible?
        m_acquired = 0;

        return true;
    }

    std::unreachable();
}

size_t AcquireAwaiter::remaining() const {
    return m_requested - m_acquired;
}

AcquireAwaiter::AcquireAwaiter(AcquireAwaiter&& other) noexcept
    : m_sem(other.m_sem),
      m_acquired(std::exchange(other.m_acquired, 0)),
      m_requested(other.m_requested)
{
    ARC_ASSERT(!other.m_registered, "cannot move a AcquireAwaiter that already was polled");
}

AcquireAwaiter::~AcquireAwaiter() {
    if (m_registered) {
        m_sem.m_waiters.lock()->remove(this);
    }

    if (m_acquired > 0) {
        m_sem.release(m_acquired);
    }
}

}
