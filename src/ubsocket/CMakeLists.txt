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

set(UBSOCKET_BASE_DIR ${PROJECT_SOURCE_DIR}/ubsocket)
set(UBSOCKET_CSRC_DIR ${PROJECT_SOURCE_DIR}/ubsocket/csrc)
set(UBSOCKET_INSTALL_DIR ${PROJECT_SOURCE_DIR}/output/ubsocket)

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
    add_link_options(-s)
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

include_directories(
        ${UBSOCKET_BASE_DIR}/include
        ${UBSOCKET_BASE_DIR}/csrc/common
        ${UBSOCKET_BASE_DIR}/csrc/core
        ${UBSOCKET_BASE_DIR}/csrc/core/umq
        ${UBSOCKET_BASE_DIR}/csrc/iobuf
        ${UBSOCKET_BASE_DIR}/csrc/profiling
        ${UBSOCKET_BASE_DIR}/csrc/under_api
        ${UBSOCKET_BASE_DIR}/../hcom
        ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
)

add_subdirectory(csrc)
add_subdirectory(tools/golden)