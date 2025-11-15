#pragma once

#include "Semaphore.hpp"

namespace arc {

template <typename T>
struct Mutex;

template <typename T = void>
struct MutexGuard {
    Mutex<T>& m_mtx;

    ~MutexGuard() {
        m_mtx.m_sema.release();
    }

    T& operator*() noexcept {
        return m_mtx.m_value;
    }

    T* operator->() noexcept {
        return &m_mtx.m_value;
    }
};

template <typename RawT = void>
struct Mutex {
    using T = std::conditional_t<std::is_void_v<RawT>, std::monostate, RawT>;
    using Guard = MutexGuard<T>;

    explicit Mutex(T value) : m_value(std::move(value)), m_sema(1) {}
    explicit Mutex() : m_value(), m_sema(1) {}

    struct LockAwaiter {
        Mutex& m_mtx;

        bool await_ready() noexcept {
            return m_mtx.m_sema.tryAcquire();
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            m_mtx.m_sema.onAcquire(h);
        }

        Guard await_resume() noexcept {
            return Guard{m_mtx};
        }
    };

    LockAwaiter lock() noexcept {
        return LockAwaiter{*this};
    }

    std::optional<Guard> tryLock() noexcept {
        if (m_sema.tryAcquire()) {
            return Guard{*this};
        } else {
            return std::nullopt;
        }
    }

private:
    template <typename U>
    friend struct MutexGuard;

    T m_value;
    Semaphore m_sema;
};

}