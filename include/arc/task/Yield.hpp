#pragma once

#include <arc/future/Pollable.hpp>

namespace arc {

struct Yield : PollableBase<Yield, void> {
    bool poll();

    bool yielded = false;
};

Yield yield() noexcept;

struct Never : PollableBase<Never, void> {
    bool poll();
};

Never never() noexcept;

}