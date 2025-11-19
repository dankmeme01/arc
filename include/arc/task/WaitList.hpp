#pragma once

#include "Waker.hpp"
#include <deque>
#include <mutex>
#include <optional>

namespace arc {

template <typename T>
struct WaitList {
    struct Waiter {
        Waker waker;
        T* awaiter;
    };

    void add(const Waker& waker, T* awaiter) noexcept {
        std::lock_guard lock(m_mtx);
        m_waiters.push_back({
            waker.clone(),
            awaiter,
        });
    }

    void remove(T* awaiter) noexcept {
        std::lock_guard lock(m_mtx);

        for (auto it = m_waiters.begin(); it != m_waiters.end(); ++it) {
            if (it->awaiter == awaiter) {
                m_waiters.erase(it);
                return;
            }
        }
    }

    void swapData(T* old, T* newAddr) noexcept {
        std::lock_guard lock(m_mtx);

        for (auto& waiter : m_waiters) {
            if (waiter.awaiter == old) {
                waiter.awaiter = newAddr;
                return;
            }
        }
    }

    std::optional<Waiter> takeFirst() {
        std::lock_guard lock(m_mtx);
        if (m_waiters.empty()) {
            return std::nullopt;
        }

        auto waiter = std::move(m_waiters.front());
        m_waiters.pop_front();
        return waiter;
    }

    /// Calls the given function on all waiters and clears the list.
    template <typename Func>
    void forAll(Func&& func) {
        std::lock_guard lock(m_mtx);
        for (auto& waiter : m_waiters) {
            func(waiter.waker, waiter.awaiter);
        }
        m_waiters.clear();
    }

private:
    std::mutex m_mtx;
    std::deque<Waiter> m_waiters;
};

}