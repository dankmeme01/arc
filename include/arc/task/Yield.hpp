#pragma once

#include <arc/future/Pollable.hpp>

namespace arc {

struct ARC_NODISCARD Yield : PollableBase<Yield> {
    bool poll();

    bool yielded = false;
};

Yield yield() noexcept;

struct ARC_NODISCARD Never : PollableBase<Never> {
    bool poll();
};

Never never() noexcept;

}