#include <arc/Util.hpp>
#include <random>

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

}