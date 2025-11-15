#pragma once

#include <asp/time/Instant.hpp>
#include <coroutine>

namespace arc {

struct Sleep {
    asp::time::Instant m_expiry;

    explicit Sleep(asp::time::Instant expiry) : m_expiry(expiry) {}

    bool await_ready() const noexcept;
    void await_suspend(std::coroutine_handle<> h) noexcept;
    void await_resume() noexcept;
};

Sleep sleepFor(asp::time::Duration duration);
Sleep sleepUntil(asp::time::Instant expiry);

}
