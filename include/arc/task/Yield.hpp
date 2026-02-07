#pragma once

#include <arc/future/Pollable.hpp>

namespace arc {

struct ARC_NODISCARD Yield : Pollable<Yield> {
    bool poll(Context& cx) noexcept {
        if (!yielded) {
            yielded = true;
            cx.wake();
            return false;
        } else {
            return true;
        }
    }

private:
    bool yielded = false;
};

inline Yield yield() noexcept {
    return Yield{};
}

struct ARC_NODISCARD Never : Pollable<Never> {
    bool poll(Context& cx) noexcept {
        return false;
    }
};

inline Never never() noexcept {
    return Never{};
}

struct ARC_NODISCARD CoopYield : Pollable<CoopYield> {
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