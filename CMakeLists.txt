cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(arc VERSION 1.0.0)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_library(arc STATIC ${SOURCES})
target_include_directories(arc PUBLIC "include")

include(cmake/CPM.cmake)

CPMAddPackage("gh:dankmeme01/asp2#15da600")
CPMAddPackage("gh:zhihaoy/nontype_functional#bdb0987")
target_link_libraries(arc PUBLIC asp std23::nontype_functional)


# testing

file(GLOB_RECURSE TESTER_SOURCES CONFIGURE_DEPENDS "test/*.cpp")
add_executable(arc_tester ${TESTER_SOURCES})
target_link_libraries(arc_tester PRIVATE arc asp fmt std23::nontype_functional)