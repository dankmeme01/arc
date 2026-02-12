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

}