#pragma once
#include <string_view>
#include <utility>

// Assert macro
#ifndef ARC_ASSERT
# define ARC_ASSERT(condition, ...) \
    ARC__ASSERT2(condition, "" __VA_ARGS__)

# define ARC__ASSERT2(condition, msg) \
    do { \
        if (!(condition)) [[unlikely]] { \
            ::arc::_assertionFail(#condition, msg, __FILE__, __LINE__); \
        } \
    } while (false)
#endif

#ifndef ARC_DEBUG_ASSERT
# if defined ARC_DEBUG
#  define ARC_DEBUG_ASSERT ARC_ASSERT
# else
#  define ARC_DEBUG_ASSERT(...) (void)0
# endif
#endif

#ifndef ARC_UNREACHABLE
# define ARC_UNREACHABLE(msg) ARC_ASSERT(false, msg); std::unreachable()
#endif

namespace arc {

[[noreturn]] void _assertionFail(std::string_view what, std::string_view why, std::string_view file, int line);

}
