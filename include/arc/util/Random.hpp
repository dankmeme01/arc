#pragma once

#include <array>
#include <stdint.h>

namespace arc {

// Implementation of RomuTrio PRNG
// https://www.romu-random.org/

std::array<uint64_t, 3> _getRandomSeed();

#define ARC_ROTL(d,lrot) ((d<<(lrot)) | (d>>(8*sizeof(d)-(lrot))))
inline uint64_t fastRand() {
    static thread_local std::array<uint64_t, 3> state = _getRandomSeed();

    uint64_t xp = state[0];
    uint64_t yp = state[1];
    uint64_t zp = state[2];
    state[0] = 15241094284759029579u * zp;
    state[1] = yp - xp;
    state[1] = ARC_ROTL(state[1], 12);
    state[2] = zp - yp;
    state[2] = ARC_ROTL(state[2], 44);
    return xp;
}
#undef ARC_ROTL

inline uint64_t fastRandNonzero() {
    uint64_t x;
    do {
        x = fastRand();
    } while (x == 0);
    return x;
}

constexpr inline uint64_t fnv1aHash(char const* str) {
    uint64_t hash = 0xcbf29ce484222325;
    while (*str) {
        hash ^= *str++;
        hash *= 0x100000001b3;
    }
    return hash;
}

#define ARC_STRINGIFY_IMPL(x) #x
#define ARC_STRINGIFY(x) ARC_STRINGIFY_IMPL(x)
#define ARC_EXPAND(x) x
#define ARC_CONCAT(a, b) a##b

#define ARC_RANDOM_NUMBER() ([]() constexpr { \
    constexpr char data[] = __FILE__ ":" ARC_STRINGIFY(__LINE__) ":" ARC_STRINGIFY(__COUNTER__); \
    return ::arc::fnv1aHash(data); \
}())

}