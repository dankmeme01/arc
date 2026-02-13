cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(arc VERSION 1.0.0)

include(cmake/Features.cmake)
option(ARC_BUILD_TESTS "Build tests" OFF)
option(ARC_BUILD_EXAMPLES "Build examples" OFF)

file(GLOB BASE_SOURCES CONFIGURE_DEPENDS
    "src/future/*.cpp"
    "src/runtime/Runtime.cpp"
    "src/sync/*.cpp"
    "src/task/*.cpp"
    "src/Util.cpp"
)

add_library(arc STATIC ${BASE_SOURCES} ${ARC_OPTIONAL_SOURCES})
target_include_directories(arc PUBLIC "include")
target_compile_definitions(arc PUBLIC ${ARC_FEATURE_DEFINES})

target_compile_options(arc PRIVATE
    $<$<CXX_COMPILER_ID:Clang>:-Wno-invalid-offsetof>
)

if (WIN32)
    target_compile_definitions(arc PUBLIC NOMINMAX)
endif()

if (NOT COMMAND CPMAddPackage)
    include(cmake/CPM.cmake)
endif()

if (NOT TARGET asp)
    CPMAddPackage("gh:dankmeme01/asp2#2e6d1e3")
endif()
if (NOT TARGET std23::nontype_functional)
    CPMAddPackage("gh:zhihaoy/nontype_functional#bdb0987")
endif()

target_link_libraries(arc PUBLIC asp std23::nontype_functional)

# Add qsox if networking is enabled
if (ARC_FEATURE_NET)
    if (NOT TARGET qsox)
        CPMAddPackage("gh:dankmeme01/qsox#41682e5")
    endif()
    target_link_libraries(arc PUBLIC qsox)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(arc PUBLIC stdc++exp)
endif()

if (ARC_BUILD_EXAMPLES)
    add_subdirectory(examples)
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
