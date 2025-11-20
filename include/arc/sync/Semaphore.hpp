#pragma once

#include <arc/future/Pollable.hpp>
#include <arc/task/WaitList.hpp>
#include <arc/runtime/Runtime.hpp>
#include <asp/sync/Mutex.hpp>
#include <cstddef>

namespace arc {

struct Semaphore {
    explicit Semaphore(size_t permits);

    struct AcquireAwaiter : PollableBase<AcquireAwaiter, void> {
        enum class State : uint8_t {
            Init,
            Waiting,
            Notified,
        };

        Semaphore& m_sem;
        std::atomic<State> m_waitState{State::Init};
        std::atomic<size_t> m_remaining;

        explicit AcquireAwaiter(Semaphore& sem, size_t permits) : m_sem(sem), m_remaining(permits) {}

        bool poll();
        AcquireAwaiter(AcquireAwaiter&&) noexcept;
        AcquireAwaiter& operator=(AcquireAwaiter&&) noexcept;
        ~AcquireAwaiter();

    private:
        void reset();
        bool tryAcquireSafe();
    };

    AcquireAwaiter acquire(size_t permits = 1) noexcept;
    bool tryAcquire(size_t permits = 1) noexcept;
    void release() noexcept;
    void release(size_t n) noexcept;

private:
    template <typename T>
    friend struct Mutex;

    struct Waiter {
        Waker waker;
        AcquireAwaiter* awaiter;
    };

    std::atomic<size_t> m_permits;
    asp::Mutex<WaitList<AcquireAwaiter>> m_waiters;
    Runtime* m_runtime = nullptr;
};

}