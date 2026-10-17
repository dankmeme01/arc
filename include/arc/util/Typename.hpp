#pragma once
#include <string_view>

namespace arc {

/// Gets the name of a type in a constexpr fashion. The output is dependant on the compiler, so it should be only used for debug purposes.
/// For example, `getTypename<std::string>` may return:
/// * `std::basic_string<char>` (Clang)
/// * `class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> >` (MSVC)
/// * Anything else, due to compiler updates
template <typename T>
constexpr std::string_view getTypename() {
    // This is very implementation defined, and may break with compiler updates
    // Example outputs of compilers' output with T = std::string
    // Clang 21: std::pair<const char *, size_t> arc::getTypename() [T = std::basic_string<char>]
    // Msvc:     struct std::pair<char const *,unsigned __int64> __cdecl arc::getTypename<class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> >>(void)
#if defined __clang__ || defined __GNUC__
    std::string_view name = __PRETTY_FUNCTION__;
    std::string_view prefix = "T = ";
    std::string_view suffix = "]";

#elif defined(_MSC_VER)
    std::string_view name = __FUNCSIG__;
    std::string_view prefix = "getTypename<";
    std::string_view suffix = ">(void)";
#else
# error "Unsupported compiler"
#endif

    auto start = name.find(prefix) + prefix.size();
    auto end = name.rfind(suffix);
    return name.substr(start, end - start);
}

}