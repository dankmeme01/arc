#pragma once

#include <arc/future/Pollable.hpp>
#include <arc/task/WaitList.hpp>
#include <arc/runtime/Runtime.hpp>
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

        explicit AcquireAwaiter(Semaphore& sem) : m_sem(sem) {}

        bool pollImpl();
        AcquireAwaiter(AcquireAwaiter&&) noexcept;
        AcquireAwaiter& operator=(AcquireAwaiter&&) noexcept;
        ~AcquireAwaiter();
        void reset();
    };

    AcquireAwaiter acquire() noexcept;
    bool tryAcquire() noexcept;
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
    WaitList<AcquireAwaiter> m_waiters;
    Runtime* m_runtime = nullptr;
};

}