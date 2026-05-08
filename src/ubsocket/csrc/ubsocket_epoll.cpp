/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "ubsocket.h"
#include "ubsocket_common_includes.h"

UBS_API int UB_API_WRAP(epoll_create)(int size)
{
    return 0;
}

UBS_API int UB_API_WRAP(epoll_create1)(int flags)
{
    return 0;
}

UBS_API int UB_API_WRAP(epoll_ctl)(int epfd, int op, int fd, struct epoll_event *event)
{
    return 0;
}

UBS_API int UB_API_WRAP(epoll_wait)(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    return 0;
}

UBS_API int UB_API_WRAP(epoll_pwait)(int epfd, struct epoll_event *events, int maxevents, int timeout,
                                     const sigset_t *sigmask)
{
    return 0;
}