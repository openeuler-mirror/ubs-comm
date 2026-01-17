cmake_minimum_required(VERSION 3.12.1)
project(ubsocket)

option(UBSOCKET_BUILD_TESTS "Build tests" OFF)
option(UBSOCKET_USE_SYSTEM_GTEST "Whether to use gtest on system" OFF)
option(UBSOCKET_ENABLE_COVERAGE "Enable code coverage" OFF)

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
  add_compile_definitions(UBSOCKET_TEST_MODE)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/urpc_util.c
    ${CMAKE_CURRENT_SOURCE_DIR}/util_vlog.c
)

# brpc adaptor
add_subdirectory(brpc)

## Coverage
if(UBSOCKET_ENABLE_COVERAGE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(STATUS "Building with coverage support")
    target_compile_options(ubsocket PUBLIC --coverage)
    target_link_options(ubsocket PUBLIC --coverage)
  else()
    message(FATAL_ERROR "Coverage is only supported for GCC and Clang")
  endif()

  find_program(LCOV_PATH lcov)
  find_program(GENHTML_PATH genhtml)
  if(LCOV_PATH AND GENHTML_PATH)
    add_custom_target(coverage
      # 1. Zero counters
      COMMAND ${LCOV_PATH} --directory . --zerocounters

      # 2. Run tests
      COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure

      # 3. Capture coverage
      COMMAND ${LCOV_PATH} --directory . --capture --output-file coverage.info --rc branch_coverage=1 --ignore-errors inconsistent,mismatch

      # 4. Filter coverage (remove system headers, dependencies, and build artifacts)
      COMMAND ${LCOV_PATH} --remove coverage.info '/usr/*' '*/_deps/*' '*/build/*' --output-file coverage_filtered.info --rc branch_coverage=1 --ignore-errors inconsistent,mismatch,unused,range

      # 5. Generate HTML report
      COMMAND ${GENHTML_PATH} coverage_filtered.info --output-directory coverage_report --rc branch_coverage=1 --ignore-errors inconsistent,mismatch,unused

      # 6. Print success message
      COMMAND ${CMAKE_COMMAND} -E echo "Coverage report generated in: ${CMAKE_BINARY_DIR}/coverage_report/index.html"

      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMENT "Generating code coverage report..."
    )
  else()
    message(WARNING "lcov or genhtml not found, Coverage target will not be available.")
  endif()
endif()
