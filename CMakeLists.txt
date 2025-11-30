cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(arc VERSION 1.0.0)
option(ARC_DEBUG "Enable debug assertions" OFF)
option(ARC_BUILD_TESTS "Build tests" OFF)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_library(arc STATIC ${SOURCES})
target_include_directories(arc PUBLIC "include")

if (ARC_DEBUG)
    target_compile_definitions(arc PUBLIC ARC_DEBUG=1)
endif()

include(cmake/CPM.cmake)

CPMAddPackage("gh:dankmeme01/asp2#15da600")
CPMAddPackage("gh:dankmeme01/qsox#93da9bf")
CPMAddPackage("gh:zhihaoy/nontype_functional#bdb0987")
target_link_libraries(arc PUBLIC asp qsox std23::nontype_functional)


if (ARC_BUILD_TESTS)
    # small executable for testing

    file(GLOB_RECURSE TESTER_SOURCES CONFIGURE_DEPENDS "test/main.cpp")
    add_executable(arc_tester ${TESTER_SOURCES})
    target_link_libraries(arc_tester PRIVATE arc asp fmt std23::nontype_functional)

    # unit tests (gtest)

    enable_testing()

    CPMAddPackage("gh:google/googletest@1.16.0")

    add_executable(
        arc_tests
        test/Semaphore.cpp
    )
    target_link_libraries(arc_tests PRIVATE arc gtest_main GeodeResult)
    include(GoogleTest)
    gtest_discover_tests(arc_tests)
endif()
