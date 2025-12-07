#pragma once
#include <algorithm>
#include <stdint.h>
#include <string_view>
#include <string>

namespace arc {

template <size_t N>
struct ConstexprString {
    constexpr ConstexprString(const char (&str)[N]) {
        std::copy_n(str, N, value);
    }
    constexpr ConstexprString(const char* str) {
        std::copy_n(str, N, value);
    }
    constexpr bool operator==(const ConstexprString& other) const {
        return std::equal(value, value + N - 1, other.value);
    }
    constexpr bool operator!=(const ConstexprString& other) const {
        return !(*this == other);
    }
    constexpr operator std::string() const {
        return std::string(value, N - 1);
    }
    constexpr operator std::string_view() const {
        return std::string_view(value, N - 1);
    }
    char value[N];
};

template <>
struct ConstexprString<0> {
    constexpr ConstexprString() {}
};

}