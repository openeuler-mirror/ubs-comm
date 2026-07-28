#
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
# ubs-comm is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#      http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
#
file(GLOB ALLOCATOR_TOOL_SRCS ${UBSOCKET_BASE_DIR}/tools/allocator/*.cpp)

message(STATUS "ubsocket: allocator_tool = ${ALLOCATOR_TOOL_SRCS}")

add_executable(allocator_tool ${ALLOCATOR_TOOL_SRCS})
target_compile_options(allocator_tool PUBLIC -g -O0)
target_link_libraries(allocator_tool PUBLIC ubsocket_shared)
