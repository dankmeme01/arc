#pragma once

#include <arc/future/Pollable.hpp>
#include <arc/task/WaitList.hpp>
#include <arc/runtime/Runtime.hpp>
#include <asp/sync/Mutex.hpp>
#include <cstddef>

namespace arc {

struct Latch {
    explicit Latch(size_t count) : m_count(count) {}

    struct ARC_NODISCARD Awaiter : Pollable<Awaiter> {
        explicit Awaiter(Latch& latch) : m_latch(latch) {}

        bool poll(Context& cx);
        Awaiter(Awaiter&&) noexcept;
        Awaiter& operator=(Awaiter&&) noexcept = delete;
        ~Awaiter();

    private:
        friend struct Latch;
        Latch& m_latch;

        enum {
            None,
            Registered,
            Completed
        } m_state = None;
    };

    Awaiter wait() noexcept;
    void countDown(size_t n = 1);

private:
    friend struct Awaiter;
    asp::Mutex<WaitList<Awaiter>> m_waiters;
    std::atomic<size_t> m_count;

    void release();
};

}
