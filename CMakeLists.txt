cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(arc VERSION 1.0.0)
option(ARC_DEBUG "Enable debug assertions" OFF)
option(ARC_BUILD_TESTS "Build tests" OFF)
option(ARC_BUILD_TESTER "Build tester" OFF)
option(ARC_TRACE "Enable debug tracing" OFF)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_library(arc STATIC ${SOURCES})
target_include_directories(arc PUBLIC "include")

if (ARC_DEBUG)
    target_compile_definitions(arc PUBLIC ARC_DEBUG=1)
endif()

if (ARC_TRACE)
    target_compile_definitions(arc PUBLIC ARC_TRACE=1)
endif()

include(cmake/CPM.cmake)

if (NOT TARGET asp2)
    CPMAddPackage("gh:dankmeme01/asp2#0e9f639")
endif()
if (NOT TARGET qsox)
    CPMAddPackage("gh:dankmeme01/qsox#73aeabd")
endif()
if (NOT TARGET std23::nontype_functional)
    CPMAddPackage("gh:zhihaoy/nontype_functional#bdb0987")
endif()

target_link_libraries(arc PUBLIC asp qsox std23::nontype_functional)


if (ARC_BUILD_TESTER)
    # small executable for testing

    file(GLOB_RECURSE TESTER_SOURCES CONFIGURE_DEPENDS "test/main.cpp")
    add_executable(arc_tester ${TESTER_SOURCES})
    target_link_libraries(arc_tester PRIVATE arc asp fmt std23::nontype_functional)

    # add asan
    if (NOT WIN32)
        target_compile_options(arc_tester PRIVATE -fsanitize=address -fsanitize=leak -fno-omit-frame-pointer -g)
        target_link_options(arc_tester PRIVATE -fsanitize=address -fsanitize=leak)
    endif()
endif()

if (ARC_BUILD_TESTS)
    # unit tests (gtest)

    enable_testing()

    CPMAddPackage("gh:google/googletest@1.16.0")

    add_executable(
        arc_tests
        test/Future.cpp
        test/Join.cpp
        test/mpsc.cpp
        test/Notify.cpp
        test/Runtime.cpp
        test/Semaphore.cpp
        test/Task.cpp
    )
    target_link_libraries(arc_tests PRIVATE arc gtest_main GeodeResult)

    if (NOT WIN32)
        target_compile_options(arc_tests PRIVATE -fsanitize=address -fsanitize=leak -fno-omit-frame-pointer -g)
        target_link_options(arc_tests PRIVATE -fsanitize=address -fsanitize=leak)
    endif()

    include(GoogleTest)
    gtest_discover_tests(arc_tests)
endif()
