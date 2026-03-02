# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2020-2025. All rights reserved.

install(FILES ${CMAKE_SOURCE_DIR}/config/umq.conf
        DESTINATION /etc/rsyslog.d
        PERMISSIONS OWNER_WRITE OWNER_READ)

install(FILES ${CMAKE_SOURCE_DIR}/config/umq
        DESTINATION /etc/logrotate.d
        PERMISSIONS OWNER_WRITE OWNER_READ)
