#pragma once

#include <arc/util/Config.hpp>
#ifndef ARC_FEATURE_TIME
ARC_FATAL_NO_FEATURE(time)
#else

#include <arc/future/Pollable.hpp>
#include <arc/future/Future.hpp>
#include <arc/runtime/Runtime.hpp>
#include <arc/util/Result.hpp>

#include <asp/time/Instant.hpp>
#include <cstdint>

namespace arc {

struct TimedOut {};
template <typename T>
using TimeoutResult = Result<T, TimedOut>;

template <
    IsPollable Fut,
    typename FutOut = FutureTraits<std::decay_t<Fut>>::Output,
    bool IsVoid = std::is_void_v<FutOut>,
    typename Output = TimeoutResult<std::conditional_t<IsVoid, std::monostate, FutOut>>
>
struct ARC_NODISCARD Timeout : Pollable<Timeout<Fut>, Output> {
    explicit Timeout(Fut fut, asp::Instant expiry) noexcept
        : m_future(std::move(fut)), m_expiry(expiry) {}

    ~Timeout() {
        if (m_id != 0) {
            auto rt = m_runtime.upgrade();
            if (rt && !rt->isShuttingDown()) {
                rt->timeDriver().removeEntry(m_expiry, m_id);
            }
        }
    }

    Timeout(Timeout&& other) noexcept :
        m_future(std::move(other.m_future)),
        m_expiry(other.m_expiry),
        m_id(other.m_id)
    {
        other.m_id = 0;
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

    std::optional<Output> poll(Context& cx) {
        auto now = asp::time::Instant::now();
        auto& td = cx.runtime()->timeDriver();

        if (now >= m_expiry) {
            // timeout occurred, so the future is now cancelled
            m_id = 0;
            return Err(TimedOut{});
        }

        // poll the future, if completed cancel timer and return ready
        if (m_future.m_vtable->m_poll(&m_future, cx)) {
            if (m_id != 0) {
                td.removeEntry(m_expiry, m_id);
                m_id = 0;
            }

            if constexpr (IsVoid) {
                return Ok(std::monostate{});
            } else {
                return Ok(std::move(m_future.m_vtable->template getOutput<FutOut>(&m_future, cx)));
            }
        }

        // register timer if we aren't already registered
        if (m_id == 0) {
            td.addEntry(m_expiry, cx.cloneWaker());
            m_runtime = cx.runtime()->weakFromThis();
        }

        return std::nullopt;
    }

private:
    Fut m_future;
    asp::time::Instant m_expiry;
    asp::WeakPtr<Runtime> m_runtime;
    uint64_t m_id = 0;
};

auto timeoutAt(asp::time::Instant expiry, Awaitable auto fut) noexcept {
    return Timeout{std::move(fut), expiry};
}

auto timeout(asp::time::Duration dur, Awaitable auto fut) noexcept {
    return timeoutAt(asp::Instant::now() + dur, std::move(fut));
}

}

#endif
