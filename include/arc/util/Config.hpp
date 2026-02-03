#pragma once

#ifdef _MSC_VER
# define ARC_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
# define ARC_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif
