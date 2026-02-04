option(ARC_FEATURE_FULL "Enable all Arc features" ON)

set(ARC_ENABLED_FEATURES "")

macro(arc_define_feature NAME FULL_VAL DEFAULT_VAL HELP_STR)
    if (ARC_FEATURE_FULL)
        option(ARC_FEATURE_${NAME} "${HELP_STR}" ${FULL_VAL})
    else()
        option(ARC_FEATURE_${NAME} "${HELP_STR}" ${DEFAULT_VAL})
    endif()

    if (ARC_FEATURE_${NAME})
        string(TOLOWER ${NAME} LOWER_NAME)
        list(APPEND ARC_ENABLED_FEATURES ${LOWER_NAME})
    endif()
endmacro()

arc_define_feature(TIME ON ON "Enable time driver")
arc_define_feature(NET ON OFF "Enable networking support")
arc_define_feature(SIGNAL ON OFF "Enable signal handling")
arc_define_feature(IOCP ON OFF "Enable IOCP driver (windows only)")

arc_define_feature(DEBUG OFF OFF "Enable debug assertions")
arc_define_feature(TRACE OFF OFF "Enable debug tracing")

if (ARC_ENABLED_FEATURES)
    list(JOIN ARC_ENABLED_FEATURES ", " ARC_ENABLED_FEATURES)
    message(STATUS "Arc features: ${ARC_ENABLED_FEATURES}")
else()
    message(STATUS "Arc features: none (only core)")
endif()

set(ARC_OPTIONAL_SOURCES "")
set(ARC_FEATURE_DEFINES "")

if(ARC_FEATURE_TIME)
    file(GLOB_RECURSE TIME_SRC CONFIGURE_DEPENDS "src/time/*.cpp")
    list(APPEND ARC_OPTIONAL_SOURCES ${TIME_SRC} "src/runtime/TimeDriver.cpp")
    list(APPEND ARC_FEATURE_DEFINES ARC_FEATURE_TIME=1)
endif()

if(ARC_FEATURE_NET)
    file(GLOB_RECURSE NET_SRC CONFIGURE_DEPENDS "src/net/*.cpp")
    list(APPEND ARC_OPTIONAL_SOURCES ${NET_SRC} "src/runtime/IoDriver.cpp")
    list(APPEND ARC_FEATURE_DEFINES ARC_FEATURE_NET=1)
endif()

if(ARC_FEATURE_SIGNAL)
    file(GLOB_RECURSE SIGNAL_SRC CONFIGURE_DEPENDS "src/signal/*.cpp")
    list(APPEND ARC_OPTIONAL_SOURCES ${SIGNAL_SRC} "src/runtime/SignalDriver.cpp")
    list(APPEND ARC_FEATURE_DEFINES ARC_FEATURE_SIGNAL=1)
endif()

if (ARC_FEATURE_IOCP AND WIN32)
    list(APPEND ARC_OPTIONAL_SOURCES "src/runtime/IocpDriver.cpp" "src/runtime/Iocp.cpp")
    list(APPEND ARC_FEATURE_DEFINES ARC_FEATURE_IOCP=1)
endif()

if(ARC_FEATURE_DEBUG)
    list(APPEND ARC_FEATURE_DEFINES ARC_DEBUG=1)
endif()
if(ARC_FEATURE_TRACE)
    list(APPEND ARC_FEATURE_DEFINES ARC_TRACE=1)
endif()