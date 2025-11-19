#pragma once

#include <arc/future/Pollable.hpp>
#include <asp/time/Instant.hpp>

namespace arc {

struct Sleep : PollableBase<Sleep, void> {
    asp::time::Instant m_expiry;

    explicit Sleep(asp::time::Instant expiry) : m_expiry(expiry) {}

    bool pollImpl();
};

Sleep sleep(asp::time::Duration duration);
Sleep sleepFor(asp::time::Duration duration);
Sleep sleepUntil(asp::time::Instant expiry);

}
