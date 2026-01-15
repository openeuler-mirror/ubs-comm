cmake_minimum_required(VERSION 3.12.1)
project(ubsocket)

option(UBSOCKET_BUILD_TESTS "Build tests" OFF)
option(UBSOCKET_USE_SYSTEM_GTEST "Whether to use gtest on system" OFF)

# Sources for the library are specified at the end
add_library(ubsocket OBJECT "")

## Compile options
target_compile_options(ubsocket
  PRIVATE
    -Werror
  PUBLIC
    -g -fPIC
    -Wall -Wextra -Wfloat-equal -Wtrampolines -Wno-unused-parameter -Wno-missing-field-initializers -Wno-type-limits
    -fno-strict-aliasing -fstack-protector-strong -fno-common

    # arch specific
    $<IF:$<STREQUAL:${CMAKE_HOST_SYSTEM_PROCESSOR},aarch64>,-march=armv8-a+crc -DUB_ARCH_ARM64
                                                           ,-msse4.2 -DUB_ARCH_X86_64>
)

## Libraries
# Link options
target_link_options(ubsocket
  PUBLIC
    -rdynamic -Wl,-z,noexecstack,-z,relro,-z,now
)

# umq
add_subdirectory(${PROJECT_SOURCE_DIR}/3rdparty/umq)
target_link_libraries(ubsocket PUBLIC umq)

# dl
target_link_libraries(ubsocket PUBLIC ${CMAKE_DL_LIBS})

# boundscheck
add_subdirectory(${PROJECT_SOURCE_DIR}/3rdparty/boundscheck)
target_link_libraries(ubsocket PUBLIC openeuler::boundscheck)

## Test
if(UBSOCKET_BUILD_TESTS)
  # Warn on non-debug builds
  if(NOT ${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    message(STATUS "Current build type is: ${CMAKE_BUILD_TYPE}. mockcpp may fail to work if opt is enabled.")
  endif()

  include(CTest)

  if(UBSOCKET_USE_SYSTEM_GTEST)
    find_package(GTest CONFIG)
    if(NOT GTest_FOUND)
      add_subdirectory(${PROJECT_SOURCE_DIR}/3rdparty/gtest)
    endif()
  else()
    add_subdirectory(${PROJECT_SOURCE_DIR}/3rdparty/gtest)
  endif()

  add_subdirectory(${PROJECT_SOURCE_DIR}/3rdparty/mockcpp)
  add_compile_definitons(UBSOCKET_TEST_MODE)
  add_subdirectory(unit_test)
endif()

## Definitions

## Includes
target_include_directories(ubsocket PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

## Install

## Sources
target_sources(ubsocket
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/configure_settings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/file_descriptor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/polling_epoll.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rpc_adpt_vlog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/statistics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/urpc_util.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/util_vlog.cpp
)

# brpc adaptor
option(BUILD_WITH_UBS_SHM "Build UBS SHM" OFF)
if (BUILD_WITH_UBS_SHM)
    add_definitions(-DUBS_SHM_BUILD_ENABLED)
endif ()

message(STATUS "BUILD_WITH_UBS_SHM: ${BUILD_WITH_UBS_SHM}")
add_subdirectory(brpc)
