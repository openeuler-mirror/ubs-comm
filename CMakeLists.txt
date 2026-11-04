cmake_minimum_required(VERSION 3.14.1)
project(UBS_COMM CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_SKIP_BUILD_RPATH ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE release)
endif()
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

# Reset the prefix path if user doesn't provide one.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set_property(CACHE CMAKE_INSTALL_PREFIX PROPERTY VALUE ${PROJECT_BINARY_DIR})
endif()
add_subdirectory(src)

option(BUILD_TESTS "Build unit tests" OFF)
if (BUILD_TESTS)
    if (TEST_TOOL_INSTALL_PATH)
        list(INSERT CMAKE_PREFIX_PATH 0 "${TEST_TOOL_INSTALL_PATH}/googletest")
    endif()
    find_package(GTest REQUIRED)
    enable_testing()
    add_definitions(-DBUILD_TESTS)

    add_subdirectory(test)
endif ()
message(STATUS "BUILD_TESTS: ${BUILD_TESTS}")
