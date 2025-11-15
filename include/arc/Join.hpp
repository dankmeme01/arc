#pragma once

#include "Task.hpp"

namespace arc {

template <typename... Ts>
arc::Task<> joinAll(Ts&&... tasks) {
    (co_await tasks, ...);
}


}