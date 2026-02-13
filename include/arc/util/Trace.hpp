#pragma once

#include <fmt/core.h>
#include <fmt/std.h>
#include <asp/time/Instant.hpp>
#include <arc/util/Function.hpp>

#include <utility>

#ifdef ARC_ENABLE_TRACE
# define ARC_TRACE ::arc::trace
#else
# define ARC_TRACE(...) do {} while(0)
#endif

namespace arc {

static auto epoch = asp::time::Instant::now();

enum LogLevel {
    Trace,
    Warn,
    Error,
};

void doLogMessage(std::string message, LogLevel level);
void setLogFunction(arc::MoveOnlyFunction<void(std::string, LogLevel)> func);

template <class... Args>
void trace(fmt::format_string<Args...> fmt, Args&&... args) {
#ifdef ARC_ENABLE_TRACE
    auto elapsed = epoch.elapsed();
    auto message = fmt::format("[TRACE] [{:.4f}] {}", elapsed.seconds<float>(), fmt::format(fmt, std::forward<Args>(args)...));
    doLogMessage(std::move(message), LogLevel::Trace);
#endif
}

template <class... Args>
void printWarn(fmt::format_string<Args...> fmt, Args&&... args) {
    auto message = fmt::format("[WARN] {}", fmt::format(fmt, std::forward<Args>(args)...));
    doLogMessage(std::move(message), LogLevel::Warn);
}

template <class... Args>
void printError(fmt::format_string<Args...> fmt, Args&&... args) {
    auto message = fmt::format("[ERROR] {}", fmt::format(fmt, std::forward<Args>(args)...));
    doLogMessage(std::move(message), LogLevel::Error);
}

inline std::string_view levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "UNKNOWN";
}

}