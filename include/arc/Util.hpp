#pragma once

#include <utility>
#include <fmt/core.h>
#include <fmt/std.h>
#include <asp/time/Instant.hpp>

namespace arc {

template <typename T>
struct ManuallyDrop {
    union {
        T value;
        char _dummy;
    };

    ManuallyDrop() : value() {}

    template <typename... Args>
    ManuallyDrop(Args&&... args) : value(std::forward<Args>(args)...) {}

    ~ManuallyDrop() noexcept {}

    void drop() noexcept {
        value.~T();
    }

    T* operator->() noexcept {
        return &value;
    }

    T& operator*() noexcept {
        return value;
    }

    T& get() noexcept {
        return value;
    }
};

static auto epoch = asp::time::Instant::now();

template <class... Args>
void trace(fmt::format_string<Args...> fmt, Args&&... args) {
    auto elapsed = epoch.elapsed();

    fmt::println("[TRACE] [{:.4f}] {}", elapsed.seconds<float>(), fmt::format(fmt, std::forward<Args>(args)...));
}

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

}