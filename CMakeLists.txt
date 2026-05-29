cmake_minimum_required(VERSION 3.14.1)
project(hcom CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_SKIP_BUILD_RPATH ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE release)
endif()
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
add_compile_options(-D_GLIBCXX_USE_CXX11_ABI=0)
# Reset the prefix path if user doesn't provide one.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set_property(CACHE CMAKE_INSTALL_PREFIX PROPERTY VALUE ${PROJECT_BINARY_DIR})
endif()
add_subdirectory(src)

option(BUILD_TESTS "Build unit tests" OFF)
if (BUILD_TESTS)
    enable_testing()
    add_definitions(-DBUILD_TESTS)

    # GTest can be found via find_package.
    #   find_package(GTest CONFIG REQUIRED)
    #   target_link_libraries(foo GTest::gtest_main)  # or GTest::gtest w/o the main function
    if(NOT EXISTS ${TEST_TOOL_INSTALL_PATH})
        message(FATAL_ERROR "Specify -DTEST_TOOL_INSTALL_PATH=/path/to/dir in cmake command args.")
    endif()
    list(APPEND CMAKE_PREFIX_PATH ${TEST_TOOL_INSTALL_PATH}/googletest/lib64/cmake)
    add_subdirectory(test)
endif ()
message(STATUS "BUILD_TESTS: ${BUILD_TESTS}")
