#pragma once

#include <asp/time/Instant.hpp>
#include <coroutine>

namespace arc {

enum class MissedTickBehavior {
    /// Catch up on missed ticks. For example, if the interval is 1 second and work takes 1.5 seconds,
    /// the next tick fires immediately, and the following tick will be 0.5 seconds later.
    Burst,
    /// Skip missed ticks. For example, if the interval is 1 second and work between ticks takes 1.5 seconds,
    /// the next tick will be 0.5 seconds after work is finished.
    Skip,
};

struct Interval {
    asp::time::Instant m_current;
    asp::time::Duration m_period;
    MissedTickBehavior m_mtBehavior = MissedTickBehavior::Burst;

    explicit Interval(asp::time::Duration period);

    bool await_ready() const noexcept;
    void await_suspend(std::coroutine_handle<> h) noexcept;
    void await_resume() noexcept;

    void setMissedTickBehavior(MissedTickBehavior behavior);
};

Interval interval(asp::time::Duration period);

}
