#
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
# ubs-comm is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#      http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
#
option(BUILD_UMQ_ADAPTER_BACKEND "build umq adapter backend" ON)
option(BUILD_UMQ_DLOPEN_BACKEND "build umq dlopen backend" OFF)
option(BUILD_URMA_DLOPEN_BACKEND "build urma dlopen backend" OFF)

if(BUILD_UMQ_ADAPTER_BACKEND AND BUILD_UMQ_DLOPEN_BACKEND)
    message(FATAL_ERROR "BUILD_UMQ_ADAPTER_BACKEND and BUILD_UMQ_DLOPEN_BACKEND cannot both be ON")
endif()

message(STATUS "ubsocket: BUILD_UMQ_ADAPTER_BACKEND = ${BUILD_UMQ_ADAPTER_BACKEND}")
message(STATUS "ubsocket: BUILD_UMQ_DLOPEN_BACKEND = ${BUILD_UMQ_DLOPEN_BACKEND}")
message(STATUS "ubsocket: BUILD_URMA_DLOPEN_BACKEND = ${BUILD_URMA_DLOPEN_BACKEND}")

set(UBSOCKET_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(UBSOCKET_CSRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/csrc)
set(UBSOCKET_INSTALL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/output/ubsocket)

message(STATUS "ubsocket: CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")
message(STATUS "ubsocket: UBSOCKET_BASE_DIR = ${UBSOCKET_BASE_DIR}")

include(cmake/config_version.cmake)
include(cmake/config_last_commit.cmake)

add_compile_options(-Wunused-variable -Wunused-value -Wcast-align)
add_compile_options(-fpermissive -Wno-error)
add_compile_options(-Wcast-qual -Winvalid-pch -Wwrite-strings -Wsign-compare -Wfloat-equal -Wextra -Wno-deprecated-declarations)

if (${CMAKE_BUILD_TYPE} MATCHES "RELEASE")
    add_compile_options(-fvisibility=hidden -fvisibility-inlines-hidden)
    add_compile_options(-fstack-protector-strong -Wstack-usage=16384 -Werror)
    add_compile_options(-Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now -fPIE -fPIC -ftrapv -s)
    add_compile_options(-D_FORTIFY_SOURCE=2 -O2)
    add_link_options(-Wl,-z,noexecstack,-z,relro,-z,now)
    set(CMAKE_SKIP_BUILD_RPATH ON)
    message(STATUS "ubsocket: added compile options for RELEASE")
elseif (${CMAKE_BUILD_TYPE} MATCHES "ASAN")
    set(SANITIZERS "address,undefined,pointer-compare,pointer-subtract,alignment")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=${SANITIZERS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=${SANITIZERS}")
    add_link_options(-fsanitize=${SANITIZERS})
else ()
    add_compile_options(-g -ggdb3 -O0 -fno-omit-frame-pointer -rdynamic -fPIC)
endif ()

message(STATUS "ubsocket: CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")

if (BUILD_URMA_DLOPEN_BACKEND)
    add_definitions(-DURMA_DLOPEN_BACKEND_ENABLED)
endif ()

if (BUILD_UMQ_DLOPEN_BACKEND)
    add_definitions(-DUMQ_DLOPEN_BACKEND_ENABLED)
endif ()

if (BUILD_UMQ_ADAPTER_BACKEND)
    add_definitions(-DUMQ_ADAPTER_BACKEND_ENABLED)

    set(UMQ_BUILD_DIR "${UBSOCKET_BASE_DIR}/../hcom/umq/build")
    set(UMQ_LIB_DIR "${UMQ_BUILD_DIR}/src")
    set(UMQ_QBUF_LIB_DIR "${UMQ_LIB_DIR}/qbuf")
    set(UMQ_IPC_LIB_DIR "${UMQ_LIB_DIR}/umq_ipc")

    if(NOT EXISTS "${UMQ_LIB_DIR}/libumq.so")
        message(STATUS "ubsocket: libumq.so not found, building umq automatically...")
        file(MAKE_DIRECTORY "${UMQ_BUILD_DIR}")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -DUSE_URMA_STUB=ON -S "${UBSOCKET_BASE_DIR}/../hcom/umq" -B "${UMQ_BUILD_DIR}"
            RESULT_VARIABLE UMQ_CMAKE_RESULT
        )
        if(NOT UMQ_CMAKE_RESULT EQUAL 0)
            message(FATAL_ERROR "ubsocket: umq cmake failed: ${UMQ_CMAKE_RESULT}")
        endif()
        execute_process(
            COMMAND ${CMAKE_COMMAND} --build "${UMQ_BUILD_DIR}" -j ${CMAKE_BUILD_PARALLEL_LEVEL}
            RESULT_VARIABLE UMQ_BUILD_RESULT
        )
        if(NOT UMQ_BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "ubsocket: umq build failed: ${UMQ_BUILD_RESULT}")
        endif()
        message(STATUS "ubsocket: umq built successfully")
    else()
        message(STATUS "ubsocket: libumq.so found at ${UMQ_LIB_DIR}")
    endif()
endif ()

include_directories(
        ${UBSOCKET_BASE_DIR}/
        ${UBSOCKET_BASE_DIR}/csrc
        ${UBSOCKET_BASE_DIR}/../hcom
        ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
)

add_subdirectory(csrc)
add_subdirectory(tools/golden)

option(UBSOCKET_BUILD_TESTS "Build tests" OFF)
if(UBSOCKET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(3rdparty/gtest)
    add_subdirectory(3rdparty/mockcpp)
    add_subdirectory(unit_test)
endif()