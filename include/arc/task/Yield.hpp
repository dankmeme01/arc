#pragma once

#include <arc/future/Pollable.hpp>

namespace arc {

struct ARC_NODISCARD Yield : Pollable<Yield> {
    bool poll(Context& cx);

    bool yielded = false;
};

Yield yield() noexcept;

struct ARC_NODISCARD Never : Pollable<Never> {
    bool poll(Context& cx);
};

Never never() noexcept;

struct ARC_NODISCARD CoopYield : Pollable<CoopYield> {
    bool poll(Context& cx);

private:
    bool yielded = false;
};

/// Yields if the current task has been running for way too long without yielding.
CoopYield coopYield() noexcept;

}