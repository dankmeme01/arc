#pragma once
#include <utility>
#include <stdint.h>
#include <string_view>

namespace arc {
/// Gets the name of a type in a constexpr fashion
template <typename T>
constexpr std::pair<const char*, size_t> getTypename() {
    // This is very implementation defined, and may break with compiler updates
    // Example outputs of compilers' output with T = std::string
    // Clang 21: std::pair<const char *, size_t> arc::getTypename() [T = std::basic_string<char>]
    // Msvc:     struct std::pair<char const *,unsigned __int64> __cdecl arc::getTypename<class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> >>(void)
#ifdef __clang__
    constexpr auto function = __PRETTY_FUNCTION__;
    constexpr auto funclen = sizeof(__PRETTY_FUNCTION__);
    constexpr char pfx[] = "std::pair<const char *, size_t> arc::getTypename() [T = ";
    constexpr char sfx[] = "]";

    // for debugging, comment above line and uncomment this one to see full function sig
#elif defined(_MSC_VER)
    constexpr decltype(auto) function = __FUNCSIG__;
    constexpr auto funclen = sizeof(function) - 1;
    constexpr char pfx[] = "struct std::pair<char const *,unsigned __int64> __cdecl arc::getTypename<";
    constexpr char sfx[] = ">(void)";
#else
# error "Unsupported compiler"
#endif

    constexpr size_t pfxlen = sizeof(pfx) - 1;
    constexpr size_t sfxlen = sizeof(sfx) - 1;

    constexpr auto len = funclen - pfxlen - sfxlen - 1;
    return {function + pfxlen, len};
}

inline int _unused_typename_test() {
    constexpr auto name = getTypename<int>();
    static_assert(std::string_view{name.first, name.second} == "int");

    // more complex tests commented out because output differs by compiler
    // constexpr auto name2 = getTypename<std::string>();
    // static_assert(std::string_view{name2.first, name2.second} == "std::basic_string<char>");

    return 0;
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