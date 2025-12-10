#
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
#

cmake_minimum_required(VERSION 3.14.1)
project(HCOM_PERF_TEST C CXX)
set(CMAKE_SKIP_RPATH TRUE)

if (NOT EXISTS ${HCOM_INCLUDE_DIR})
    set(HCOM_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../../../../dist/hcom/include")
    message(INFO "-- HCOM_INCLUDE_DIR is empty, use default value(${HCOM_INCLUDE_DIR})")
endif()

if (NOT EXISTS ${HCOM_LIB_DIR})
    set(HCOM_LIB_DIR "${CMAKE_SOURCE_DIR}/../../../../dist/hcom/lib")
    message(INFO "-- HCOM_LIB_DIR is empty, use default value(${HCOM_LIB_DIR})")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE release)
endif()
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

include_directories("${HCOM_INCLUDE_DIR}")
include_directories("${CMAKE_SOURCE_DIR}/../../../../dist/hcom_3rdparty/libboundscheck/include")
include_directories("${CMAKE_SOURCE_DIR}/../../../../dist/hcom_3rdparty/umdk/urma/include")
link_directories("${HCOM_LIB_DIR}")
link_directories("${CMAKE_SOURCE_DIR}/../../../../dist/hcom_3rdparty/libboundscheck/lib")
include_directories("${CMAKE_SOURCE_DIR}")

if (${CMAKE_BUILD_TYPE} MATCHES "release")
    set(CXX_FLAGS
        -Wno-format-overflow
        -O3
        -Wno-unused-function
        -fPIE -pie
        -fstack-protector-strong
        -Wl,-z,relro,-z,now,-z,noexecstack
        -s
        )
else ()
    set(CXX_FLAGS
        -Wall
        -Wno-format-overflow
        -Wno-unused-function
        -fPIE -pie
        -fstack-protector-strong
        -Wl,-z,relro,-z,now,-z,noexecstack
        -s
        )
endif ()

file(GLOB_RECURSE PERF_TEST_SOURCES 
    "${CMAKE_SOURCE_DIR}/hcom_perf_test.cpp"
    "${CMAKE_SOURCE_DIR}/test_case/service_v2/*.cpp"
    "${CMAKE_SOURCE_DIR}/test_case/transport/*.cpp"
    "${CMAKE_SOURCE_DIR}/report/*.cpp"
    "${CMAKE_SOURCE_DIR}/common/*.cpp")

set(HCOM_PERF_TOOL hcom_perf)
add_executable(${HCOM_PERF_TOOL} ${PERF_TEST_SOURCES})
target_link_libraries(${HCOM_PERF_TOOL} hcom_static pthread rt dl)
target_link_libraries(${HCOM_PERF_TOOL} hcom_static pthread boundscheck)
