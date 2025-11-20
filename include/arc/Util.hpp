#pragma once

#include <fmt/core.h>
#include <fmt/std.h>
#include <asp/time/Instant.hpp>
#include <utility>
#include <array>

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

    void drop() noexcept(noexcept(std::declval<T>().~T())) {
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

// Assert macro
#define ARC__GET_MACRO(_0, _1, _2, name, ...) name
#define ARC_ASSERT(...) ARC__GET_MACRO(_0, ##__VA_ARGS__, ARC__ASSERT2, ARC__ASSERT1, ARC__ASSERT0)(__VA_ARGS__)

#define ARC__ASSERT2(condition, msg) \
    do { \
        if (!(condition)) [[unlikely]] { \
            ::arc::_assertionFail(#condition, msg, __FILE__, __LINE__); \
        } \
    } while (false)
#define ARC__ASSERT1(condition) ARC__ASSERT2(condition, "")

#if defined ARC_DEBUG
# define ARC_DEBUG_ASSERT ARC_ASSERT
#else
# define ARC_DEBUG_ASSERT(__VA_ARGS__) (void)0
#endif

[[noreturn]] void _assertionFail(std::string_view what, std::string_view why, std::string_view file, int line);


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

}