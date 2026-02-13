#pragma once

#include <arc/future/Pollable.hpp>

namespace arc {

struct ARC_NODISCARD Yield : NoexceptPollable<Yield> {
    bool poll(Context& cx) noexcept {
        if (!yielded) {
            yielded = true;
            cx.wake();
            return false;
        }
        return true;
    }

private:
    bool yielded = false;
};

inline Yield yield() noexcept {
    return Yield{};
}

template <typename T = std::monostate>
struct ARC_NODISCARD Never : NoexceptPollable<Never<T>, T> {
    std::optional<T> poll(Context& cx) noexcept {
        return std::nullopt;
    }
};

template <typename T = std::monostate>
inline Never<T> never() noexcept {
    return Never<T>{};
}

struct ARC_NODISCARD CoopYield : NoexceptPollable<CoopYield> {
    bool poll(Context& cx) noexcept {
        if (yielded) return true;

        if (!cx.shouldCoopYield()) {
            return true;
        }

        yielded = true;
        cx.wake();

        return false;
    }

private:
    bool yielded = false;
};

/// Yields if the current task has been running for way too long without yielding.
inline CoopYield coopYield() noexcept {
    return CoopYield{};
}

template <typename T = void>
struct ARC_NODISCARD Ready : Pollable<Ready<T>, T, std::is_void_v<T> || std::is_nothrow_move_constructible_v<T>> {
    using NonVoidOutput = std::conditional_t<std::is_void_v<T>, std::monostate, T>;
    explicit Ready(NonVoidOutput value = {}) : m_value(std::move(value)) {}

    auto poll(Context& cx) noexcept(std::is_void_v<T> || std::is_nothrow_move_constructible_v<T>) {
        if constexpr (std::is_void_v<T>) {
            return true;
        } else {
            return std::optional<NonVoidOutput>{std::move(m_value)};
        }
    }

private:
    ARC_NO_UNIQUE_ADDRESS NonVoidOutput m_value;
};

/// Immediately returns ready, with the given value (if any)
template <typename T>
inline Ready<T> ready(T value = {}) noexcept requires (!std::is_void_v<T>) {
    return Ready<T>{std::move(value)};
}

inline Ready<> ready() noexcept {
    return Ready<>{};
}

}