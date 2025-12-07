#pragma once
#include <utility>
#include <stdint.h>
#include <string_view>

namespace arc {

// had to be copied from util::debug because it includes this file
template <typename T>
constexpr std::pair<const char*, size_t> getTypename() {
#ifdef __clang__
    constexpr auto pfx = sizeof("std::pair<const char *, size_t> arc::getTypename() [T = ") - 1;
    constexpr auto sfx = sizeof("]") - 1;
    constexpr auto function = __PRETTY_FUNCTION__;
    constexpr auto len = sizeof(__PRETTY_FUNCTION__) - pfx - sfx - 1;
    return {function + pfx, len};

    // for debugging, comment above line and uncomment this one to see full function sig
    // return {function, sizeof(__PRETTY_FUNCTION__) - 1};
#else
    static_assert(false, "well well well");
#endif
}

struct PollableMetadata {
    std::string_view typeName;
    bool isFuture = false;

    template <typename T, bool Future = false>
    static const PollableMetadata* create() {
        constexpr auto name = getTypename<T>();
        static const PollableMetadata meta{std::string_view{name.first, name.second}, Future };

        return &meta;
    }
};

}