#include <arc/util/Random.hpp>
#include <arc/util/Assert.hpp>
#include <fmt/core.h>
#include <random>
#include <stdexcept>

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

}