# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.

add_library(urma_stub STATIC urma_stub.c)
target_include_directories(urma_stub PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_library(urma ALIAS urma_stub)
add_library(urma_common ALIAS urma_stub)
add_library(tpsa ALIAS urma_stub)