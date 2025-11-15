#pragma once

#include "Runtime.hpp"

namespace arc {

struct Semaphore {
    explicit Semaphore(size_t permits);

    struct AcquireAwaiter {
        Semaphore& m_sem;

        bool await_ready() noexcept;
        void await_suspend(std::coroutine_handle<> h) noexcept;
        void await_resume() noexcept;
    };

    AcquireAwaiter acquire() noexcept;
    bool tryAcquire() noexcept;
    void release() noexcept;
    void release(size_t n) noexcept;

private:
    template <typename T>
    friend struct Mutex;

    size_t m_permits;
    std::mutex m_mtx;
    std::deque<std::coroutine_handle<>> m_waiters;
    Runtime* m_runtime = nullptr;

    void onAcquire(std::coroutine_handle<> h) noexcept;
};

}
