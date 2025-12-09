#include <arc/util/Random.hpp>
#include <arc/util/Assert.hpp>
#include <arc/util/Trace.hpp>
#include <fmt/core.h>
#include <random>
#include <stdexcept>
#include <iostream>

namespace arc {


std::array<uint64_t, 3> _getRandomSeed() {
    std::random_device rd;
    std::array<uint64_t, 3> seed;

    for (auto& s : seed) {
        auto a = rd(), b = rd();
        s = (uint64_t(a) << 32) ^ uint64_t(b);
    }

    return seed;
}

[[noreturn]] void _assertionFail(std::string_view what, std::string_view why, std::string_view file, int line) {
    throw std::runtime_error(fmt::format("Assertion failed ({}) at {}:{}: {}", what, file, line, why));
}

static std23::move_only_function<void(std::string, LogLevel)> g_logFunction;

void doLogMessage(std::string message, LogLevel level) {
    if (g_logFunction) {
        g_logFunction(std::move(message), level);
    } else {
        switch (level) {
            case LogLevel::Trace: {
                fmt::println("{}", message);
            } break;

            case LogLevel::Warn: {
                fmt::println(std::cerr, "{}", message);
            } break;

            case LogLevel::Error: {
                fmt::println(std::cerr, "{}", message);
            } break;
        }
    }
}

void setLogFunction(std23::move_only_function<void(std::string, LogLevel)> func) {
    g_logFunction = std::move(func);
}

}