#pragma once

#ifdef _MSC_VER
# define ARC_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
# define ARC_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

#define ARC_FATAL_NO_FEATURE(feat) \
    static_assert(false, \
        "\n\nError: an Arc header was included that requires the feature: " #feat "\n" \
        "This feature is disabled, and you must enable it in CMakeLists.txt\n" \
        "See https://github.com/dankmeme01/arc for more information on enabling features\n\n" \
    );
