# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2020-2025. All rights reserved.

cmake_minimum_required(VERSION 3.12.1)
project(umq
  VERSION 0.0.1
)

option(USE_URMA_STUB "CI environment open stub: " OFF)
message(STATUS "USE_URMA_STUB: ${USE_URMA_STUB}")

if(USE_URMA_STUB)
    message(STATUS "using USE_URMA_STUB: ${USE_URMA_STUB}")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/perftest")
        get_directory_property(include_dirs INCLUDE_DIRECTORIES)
        list(REMOVE_ITEM include_dirs "${CMAKE_CURRENT_SOURCE_DIR}/tools/perftest")
        set_directory_properties(PROPERTIES INCLUDE_DIRECTORIES "${include_dirs}")
    endif()
    include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/stub/urma)
else()
    message(STATUS "not use USE_URMA_STUB: ${USE_URMA_STUB}")
endif()

set(CMAKE_FLAGS_PUBILC " -Wall -Werror -Wfloat-equal -Wtrampolines -g -rdynamic -Wl,-z,noexecstack,-z,relro,-z,now \
-fno-strict-aliasing -fstack-protector-strong -fPIE -pie -fPIC -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
-Wno-type-limits -fno-common")

set(CMAKE_BUILD_TYPE "Release")
set(CMAKE_C_FLAGS_RELEASE "-O2 -D_FORTIFY_SOURCE=2")

set(CMAKE_FLAGS_ASAN "-fsanitize=address -fsanitize-recover=address -fsanitize=leak -fno-omit-frame-pointer")
set(CMAKE_FLAGS_TSAN "-fsanitize=thread -fsanitize-recover=address -fno-omit-frame-pointer")
set(CMAKE_FLAGS_COVERAGE "-fprofile-arcs -ftest-coverage")
set(CMAKE_FLAGS_FUZZ "${CMAKE_FLAGS_ASAN} ${CMAKE_FLAGS_COVERAGE} -fdump-rtl-expand")

if(NOT OPENSSL_ROOT_DIR)
    set(OPENSSL_ROOT_DIR /usr/local/opt/openssl CACHE PATH "Custom OpenSSL install prefix")
endif()

find_package(OpenSSL REQUIRED)

message(STATUS "Found OpenSSL:")
message(STATUS "  Include: ${OPENSSL_INCLUDE_DIR}")
message(STATUS "  Libraries: ${OPENSSL_LIBRARIES}")

# FUZZ ENABLE
if("${FUZZ}" STREQUAL "enable")
    set(CMAKE_FLAGS_PUBILC "${CMAKE_FLAGS_PUBILC} ${CMAKE_FLAGS_FUZZ}")
    set(CMAKE_BUILD_TYPE "Debug")
else()
    # ASAN
    if("${ASAN}" STREQUAL "enable")
        set(CMAKE_FLAGS_PUBILC "${CMAKE_FLAGS_PUBILC} ${CMAKE_FLAGS_ASAN}")
        set(CMAKE_BUILD_TYPE "Debug")
        add_compile_definitions(URPC_ASAN)
    endif()
    # TSAN
    if("${TSAN}" STREQUAL "enable")
        set(CMAKE_FLAGS_PUBILC "${CMAKE_FLAGS_PUBILC} ${CMAKE_FLAGS_TSAN}")
        set(CMAKE_BUILD_TYPE "Debug")
        add_compile_definitions(URPC_TSAN)
    endif()
    # CODE COVERAGE
    if("${CODE_COVERAGE}" STREQUAL "enable")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-inline -O0")
        set(CMAKE_FLAGS_PUBILC "${CMAKE_FLAGS_PUBILC} ${CMAKE_FLAGS_COVERAGE}")
        set(CMAKE_FLAGS_PUBILC "${CMAKE_FLAGS_PUBILC} -fno-stack-protector -g1")
        string(REPLACE "-fstack-protector-strong" "" CMAKE_FLAGS_PUBILC "${CMAKE_FLAGS_PUBILC}")
        add_compile_definitions(URPC_CODE_COVERAGE)
        set(CMAKE_BUILD_TYPE "Debug")
    endif()
endif()

# RELEASE DISABLE
if("${RELEASE_ENABLE}" STREQUAL "enable")
    message(STATUS "RELEASE_ENABLE enable!")
else()
    set(RELEASE_ENABLE "disable")
    message(STATUS "RELEASE_ENABLE disable!")
endif()

if("${STRIP}" STREQUAL "enable")
    set(CMAKE_FLAGS_PUBILC "${CMAKE_FLAGS_PUBILC} -s")
endif()

add_compile_definitions(URPC_VERSION="${PROJECT_VERSION}")
message("Using build version ${URPC_VERSION}")

add_compile_definitions(URPC_RELEASE_VERSION="${PROJECT_VERSION_MAJOR}")
message("Using build release version ${URPC_RELEASE_VERSION}")

if (NOT BUILD_DATE)
    string(TIMESTAMP BUILD_DATE "%Y-%m-%d-%H:%M:%S")
endif ()
add_compile_definitions(URPC_BUILD_DATE="${BUILD_DATE}")
message("Using build timestamp ${BUILD_DATE}")

set(CMAKE_FLAGS_ARM64 " -march=armv8-a+crc -DUB_ARCH_ARM64")
set(CMAKE_FLAGS_x86_64 " -msse4.2 -DUB_ARCH_X86_64")
if("${CMAKE_HOST_SYSTEM_PROCESSOR}" STREQUAL "aarch64")
    set(CMAKE_C_FLAGS  "${CMAKE_FLAGS_PUBILC} ${CMAKE_FLAGS_ARM64} -std=gnu11")
    set(CMAKE_CXX_FLAGS  "${CMAKE_FLAGS_PUBILC} ${CMAKE_FLAGS_ARM64} -std=c++11")
else()
    set(CMAKE_C_FLAGS  "${CMAKE_FLAGS_PUBILC} ${CMAKE_FLAGS_x86_64} -std=gnu11")
    set(CMAKE_CXX_FLAGS  "${CMAKE_FLAGS_PUBILC} ${CMAKE_FLAGS_x86_64} -std=c++11")
endif()

set(CMAKE_SKIP_RPATH TRUE)

get_filename_component(PROJECT_ROOT_DIR ${CMAKE_SOURCE_DIR}/../../.. ABSOLUTE)
set(URMA_INCLUDE_DIR ${PROJECT_ROOT_DIR}/dist/hcom_3rdparty/umdk/urma/include)
message(STATUS "UMQ: URMA_INCLUDE_DIR = ${URMA_INCLUDE_DIR}")

if(EXISTS "${URMA_INCLUDE_DIR}/uvs_api.h")
    message(STATUS "uvs_api.h has found.")
else()
    message(WARNING "uvs_api.h cannot found.")
endif()

include_directories(${URMA_INCLUDE_DIR})

add_subdirectory(config)
add_subdirectory(include)
add_subdirectory(tools)
add_subdirectory(src)
add_subdirectory(util)
add_subdirectory(examples)
