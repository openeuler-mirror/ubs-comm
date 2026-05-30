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
file(GLOB GOLDEN_SRCS ${UBSOCKET_BASE_DIR}/tools/golden/*.cpp)

message(STATUS "ubsocket: golden = ${GOLDEN_SRCS}")

add_executable(golden ${GOLDEN_SRCS}
        golden_cmd_show.cpp
        golden_cmd_show.h)
target_compile_options(golden PUBLIC -g -O0)
target_link_libraries(golden PUBLIC ubsocket_shared z)