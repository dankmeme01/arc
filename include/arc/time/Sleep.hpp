#pragma once

#include <arc/future/Pollable.hpp>
#include <asp/time/Instant.hpp>
#include <cstdint>

namespace arc {

struct Sleep : PollableBase<Sleep> {
    explicit Sleep(asp::time::Instant expiry) : m_expiry(expiry) {}
    ~Sleep();

    Sleep(Sleep&& other) noexcept;
    Sleep& operator=(Sleep&& other) noexcept;

    bool poll();

private:
    asp::time::Instant m_expiry;
    uint64_t m_id = 0;
};

Sleep sleep(asp::time::Duration duration);
Sleep sleepFor(asp::time::Duration duration);
Sleep sleepUntil(asp::time::Instant expiry);

}
