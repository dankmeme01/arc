#pragma once

#include <fmt/core.h>
#include <fmt/std.h>
#include <asp/time/Instant.hpp>
#include <utility>

namespace arc {

static auto epoch = asp::time::Instant::now();

template <class... Args>
void trace(fmt::format_string<Args...> fmt, Args&&... args) {
    auto elapsed = epoch.elapsed();

    fmt::println("[TRACE] [{:.4f}] {}", elapsed.seconds<float>(), fmt::format(fmt, std::forward<Args>(args)...));
}

}