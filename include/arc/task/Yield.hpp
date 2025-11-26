#pragma once

#include <arc/future/Pollable.hpp>

namespace arc {

struct Yield : PollableBase<Yield> {
    bool poll();

    bool yielded = false;
};

Yield yield() noexcept;

struct Never : PollableBase<Never> {
    bool poll();
};

Never never() noexcept;

}