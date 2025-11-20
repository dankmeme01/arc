#pragma once

#include <arc/future/Pollable.hpp>
#include <arc/future/Future.hpp>
#include <asp/time/Instant.hpp>
#include <arc/runtime/Runtime.hpp>
#include <cstdint>

namespace arc {

template <
    Pollable Fut,
    typename FutOut = FutureTraits<std::decay_t<Fut>>::Output,
    bool IsVoid = std::is_void_v<FutOut>,
    typename Output = std::optional<std::conditional_t<IsVoid, std::monostate, FutOut>>
>
struct Timeout : PollableBase<Timeout<Fut>, Output> {
    explicit Timeout(Fut fut, asp::time::Instant expiry)
        : m_future(std::move(fut)), m_expiry(expiry) {}

    ~Timeout() {
        if (m_id != 0) {
            ctx().runtime()->timeDriver().removeEntry(m_expiry, m_id);
        }
    }

    Timeout(Timeout&& other) noexcept {
        *this = std::move(other);
    }

    Timeout& operator=(Timeout&& other) noexcept {
        if (this != &other) {
            m_future = std::move(other.m_future);
            m_expiry = other.m_expiry;
            m_id = other.m_id;
            other.m_id = 0;
        }
        return *this;
    }

    bool poll() {
        auto now = asp::time::Instant::now();
        auto& td = ctx().runtime()->timeDriver();

        if (now >= m_expiry) {
            // timeout occurred, so the future is now cancelled
            m_id = 0;
            return true;
        }

        // poll the future, if completed cancel timer and return ready
        if (m_future.poll()) {
            m_completed = true;
            if (m_id != 0) {
                td.removeEntry(m_expiry, m_id);
                m_id = 0;
            }
            return true;
        }

        // register timer if we aren't already registered
        if (m_id == 0) {
            td.addEntry(m_expiry, ctx().m_waker->clone());
        }

        return false;
    }

    Output getOutput() {
        if (!m_completed) return std::nullopt;

        if constexpr (IsVoid) {
            return std::make_optional(std::monostate{});
        } else {
            return std::make_optional(std::move(m_future.getOutput()));
        }
    }

private:
    Fut m_future;
    asp::time::Instant m_expiry;
    uint64_t m_id = 0;
    bool m_completed = false;
};

auto timeoutAt(asp::time::Instant expiry, Awaitable auto fut) {
    return Timeout{std::move(fut), expiry};
}

auto timeout(asp::time::Duration dur, Awaitable auto fut) {
    return timeoutAt(asp::time::Instant::now() + dur, std::move(fut));
}

}