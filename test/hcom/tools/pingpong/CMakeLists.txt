#
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
#
# ubs-hcom is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#      http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
#

cmake_minimum_required(VERSION 3.14.1)
project(HCOM_PINGPONG C CXX)
set(CMAKE_SKIP_RPATH TRUE)

if (NOT EXISTS ${HCOM_INCLUDE_DIR})
    set(HCOM_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../../../../dist/hcom/include")
    message(STATUS "-- HCOM_INCLUDE_DIR is empty, use default value(${HCOM_INCLUDE_DIR})")
endif()

if (NOT EXISTS ${HCOM_LIB_DIR})
    set(HCOM_LIB_DIR "${CMAKE_SOURCE_DIR}/../../../../dist/hcom/lib")
    message(STATUS "-- HCOM_LIB_DIR is empty, use default value(${HCOM_LIB_DIR})")
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

set(CXX_FLAGS
    -Wall
    -fPIE -pie
    -fstack-protector-strong
    -Wl,-z,relro,-z,now,-z,noexecstack
    -std=c++11
)
add_compile_options(${CXX_FLAGS})

set(HCOM_PINGPONG_TOOL pingpong)
add_executable(${HCOM_PINGPONG_TOOL} pingpong.cpp)
target_link_libraries(${HCOM_PINGPONG_TOOL} hcom_static pthread rt dl boundscheck)
