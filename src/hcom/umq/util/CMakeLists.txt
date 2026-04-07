# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2020-2025. All rights reserved.

include_directories(${CMAKE_CURRENT_LIST_DIR}/../include/framework)

add_compile_definitions(_GNU_SOURCE)

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} COMMON_UTIL_SRC)
add_library(common_util OBJECT ${COMMON_UTIL_SRC})
target_link_libraries(common_util
        PRIVATE
            OpenSSL::SSL
            OpenSSL::Crypto
            pthread
)
set_property(TARGET common_util PROPERTY C_STANDARD 11)
target_include_directories(common_util
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_SOURCE_DIR}/urma/lib/urma/core/include
        ${CMAKE_SOURCE_DIR}/urma/common/include
    PRIVATE
        # ub_get_clock.h lives under umq/tools/perftest; use a relative path so it works
        # whether umq is built standalone or as a subdirectory of another project.
        ${CMAKE_CURRENT_LIST_DIR}/../tools/perftest
        ${OPENSSL_INCLUDE_DIR}
)