#pragma once

#include <fmt/core.h>
#include <fmt/std.h>
#include <asp/time/Instant.hpp>
#include <std23/move_only_function.h>

#include <utility>

namespace arc {

static auto epoch = asp::time::Instant::now();

enum LogLevel {
    Trace,
    Error,
};

void doLogMessage(std::string message, LogLevel level);
void setLogFunction(std23::move_only_function<void(std::string, LogLevel)> func);

template <class... Args>
void trace(fmt::format_string<Args...> fmt, Args&&... args) {
#ifdef ARC_TRACE
    auto elapsed = epoch.elapsed();
    auto message = fmt::format("[TRACE] [{:.4f}] {}", elapsed.seconds<float>(), fmt::format(fmt, std::forward<Args>(args)...));
    doLogMessage(std::move(message), LogLevel::Trace);
#endif
}

template <class... Args>
void printError(fmt::format_string<Args...> fmt, Args&&... args) {
    auto message = fmt::format("[ERROR] {}", fmt::format(fmt, std::forward<Args>(args)...));
    doLogMessage(std::move(message), LogLevel::Error);
}

}