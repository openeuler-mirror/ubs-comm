/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UBSOCKET_EVENT_H
#define UBS_COMM_UBSOCKET_EVENT_H

#include "ubsocket_def.h"

#ifdef __cplusplus
extern "C" {
#endif

int UB_API_WRAP(epoll_create)(int size);
int UB_API_WRAP(epoll_create1)(int flags);
int UB_API_WRAP(epoll_ctl)(int epfd, int op, int fd, struct epoll_event *event);
int UB_API_WRAP(epoll_wait)(int epfd, struct epoll_event *events, int maxevents, int timeout);
int UB_API_WRAP(epoll_pwait)(int epfd, struct epoll_event *events, int maxevents, int timeout, const sigset_t *sigmask);

#ifdef __cplusplus
}
#endif

#endif //UBS_COMM_UBSOCKET_EVENT_H
