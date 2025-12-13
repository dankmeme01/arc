#pragma once

#include "Semaphore.hpp"
#include <arc/future/Future.hpp>
#include <type_traits>
#include <variant>
#include <optional>

namespace arc {

template <typename T>
struct Mutex;

template <typename T = void, typename Mtx = Mutex<T>>
struct MutexGuard {
    Mtx* m_mtx = nullptr;

    ~MutexGuard() {
        if (m_mtx) m_mtx->m_sema.release();
    }

    T& operator*() noexcept {
        return m_mtx->m_value;
    }

    T* operator->() noexcept {
        return &m_mtx->m_value;
    }

    MutexGuard(Mtx* mtx) : m_mtx(mtx) {}
    MutexGuard(const MutexGuard&) = delete;
    MutexGuard& operator=(const MutexGuard&) = delete;
    MutexGuard(MutexGuard&& other) : m_mtx(std::exchange(other.m_mtx, nullptr)) {}
    MutexGuard& operator=(MutexGuard&& other) {
        if (this != &other) {
            m_mtx = std::exchange(other.m_mtx, nullptr);
        }
        return *this;
    }
};

template <typename RawT = void>
struct Mutex {
    using T = std::conditional_t<std::is_void_v<RawT>, std::monostate, RawT>;
    using Guard = MutexGuard<T, Mutex<RawT>>;

    explicit Mutex(T value) : m_value(std::move(value)), m_sema(1) {}
    explicit Mutex() : m_value(), m_sema(1) {}

    arc::Future<Guard> lock() noexcept {
        co_await m_sema.acquire();
        co_return Guard{this};
    }

    Guard blockingLock() noexcept {
        m_sema.acquireBlocking();
        return Guard{this};
    }

    std::optional<Guard> tryLock() noexcept {
        if (m_sema.tryAcquire()) {
            return Guard{this};
        } else {
            return std::nullopt;
        }
    }

private:
    friend struct MutexGuard<T, Mutex<RawT>>;

    T m_value;
    Semaphore m_sema;
};

}