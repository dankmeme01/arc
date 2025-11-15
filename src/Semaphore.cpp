#include <arc/Semaphore.hpp>

using AcquireAwaiter = arc::Semaphore::AcquireAwaiter;

namespace arc {

Semaphore::Semaphore(size_t permits) : m_permits(permits) {}

bool AcquireAwaiter::await_ready() noexcept {
    return m_sem.tryAcquire();
}

void AcquireAwaiter::await_suspend(std::coroutine_handle<> h) noexcept {
    m_sem.onAcquire(h);
}

void AcquireAwaiter::await_resume() noexcept {}

AcquireAwaiter Semaphore::acquire() noexcept {
    return AcquireAwaiter{*this};
}

bool Semaphore::tryAcquire() noexcept {
    std::lock_guard lock(m_mtx);

    if (m_permits > 0) {
        m_permits--;
        return true;
    }

    return false;
}

void Semaphore::onAcquire(std::coroutine_handle<> h) noexcept {
    std::lock_guard lock(m_mtx);

    if (m_permits > 0) {
        m_permits--;
        g_runtime->enqueue(h);
    } else {
        m_runtime = g_runtime;
        m_waiters.push_back(h);
    }
}

void Semaphore::release(size_t n) noexcept {
    std::lock_guard lock(m_mtx);

    for (size_t i = 0; i < n; i++) {
        if (!m_waiters.empty()) {
            auto next = m_waiters.front();
            m_waiters.pop_front();
            m_runtime->enqueue(next);
        } else {
            m_permits++;
        }
    }
}

void Semaphore::release() noexcept {
    return this->release(1);
}

}