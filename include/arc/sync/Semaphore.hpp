#pragma once

#include <arc/future/Pollable.hpp>
#include <arc/task/WaitList.hpp>
#include <arc/runtime/Runtime.hpp>
#include <asp/sync/Mutex.hpp>
#include <cstddef>

namespace arc {

struct Semaphore {
    explicit Semaphore(size_t permits);

    struct ARC_NODISCARD AcquireAwaiter : PollableBase<AcquireAwaiter> {
        explicit AcquireAwaiter(Semaphore& sem, size_t permits) : m_sem(sem), m_requested(permits) {}

        bool poll();
        AcquireAwaiter(AcquireAwaiter&&) noexcept;
        AcquireAwaiter& operator=(AcquireAwaiter&&) noexcept = delete;
        ~AcquireAwaiter();

        size_t remaining() const;

    private:
        friend struct Semaphore;
        Semaphore& m_sem;
        bool m_registered = false;
        size_t m_acquired = 0;
        size_t m_requested;
        asp::SpinLock<> m_lock;
    };

    AcquireAwaiter acquire(size_t permits = 1) noexcept;
    void acquireBlocking(size_t permits = 1) noexcept;
    bool tryAcquire(size_t permits = 1) noexcept;
    void release() noexcept;
    void release(size_t n) noexcept;

    size_t permits() const noexcept;

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

    /// Acquires from 0 to n permits, returning the acquired amount
    size_t tryAcquireOrRegister(size_t maxp, AcquireAwaiter* awaiter);
    bool assignPermitsTo(size_t& remaining, AcquireAwaiter* waiter);
};

}