#pragma once

#include <arc/future/Pollable.hpp>
#include <asp/time/Instant.hpp>
#include <cstdint>

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
    explicit Interval(asp::time::Duration period);
    ~Interval();
    Interval(Interval&& other) noexcept;
    Interval& operator=(Interval&& other) noexcept;

    struct Awaiter : PollableBase<Awaiter> {
        explicit Awaiter(Interval* interval) : m_interval(interval) {}
        Awaiter(Awaiter&&) noexcept = default;
        Awaiter& operator=(Awaiter&&) noexcept = default;

        bool poll();
    private:
        Interval* m_interval;
    };

    void setMissedTickBehavior(MissedTickBehavior behavior);

    /// Returns an awaiter that completes when the next tick occurs.
    /// Note: the behavior is undefined if the Interval is destroyed before the awaiter completes,
    /// or if multiple awaiters are polled concurrently.
    Awaiter tick() noexcept;

private:
    // The next wake time
    asp::time::Instant m_current;
    asp::time::Duration m_period;
    MissedTickBehavior m_mtBehavior = MissedTickBehavior::Burst;
    uint64_t m_id = 0;

    bool doPoll();
};

Interval interval(asp::time::Duration period);

}
