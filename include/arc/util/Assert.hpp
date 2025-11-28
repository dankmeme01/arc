#pragma once
#include <string_view>

namespace arc {

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
# define ARC_DEBUG_ASSERT(...) (void)0
#endif

[[noreturn]] void _assertionFail(std::string_view what, std::string_view why, std::string_view file, int line);


}