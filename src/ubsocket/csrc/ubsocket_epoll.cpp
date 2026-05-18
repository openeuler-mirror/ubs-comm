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
#include "dl_libc_api.h"
#include "ubsocket.h"
#include "ubsocket_common_includes.h"
#include "ubsocket_event_epoll.h"

using namespace ock::ubs;

UBS_API int UB_API_WRAP(epoll_create)(int size)
{
    if (!ock::ubs::GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::epoll_create(size);
    }
    
    int epollFd = LibcApi::epoll_create(size);
    if (epollFd < 0) {
        return epollFd;
    }

    ock::ubs::EventPoll *eventPoll = new ock::ubs::AsyncEventPoll(epollFd);
    if (eventPoll == nullptr) {
        UBS_VLOG_ERR("create async event poll failed, epoll fd: %d\n", epollFd);
        LibcApi::close(epollFd);
        return -1;
    }

    ArraySet<ock::ubs::EventPoll>::GetInstance().OverrideItem(epollFd, eventPoll);
    return epollFd;
}

UBS_API int UB_API_WRAP(epoll_ctl)(int epfd, int op, int fd, struct epoll_event *event)
{
    if (!ock::ubs::GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::epoll_ctl(epfd, op, fd, event);
    }
    return -1;
}

UBS_API int UB_API_WRAP(epoll_wait)(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    if (!ock::ubs::GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::epoll_wait(epfd, events, maxevents, timeout);
    }
    return -1;
}

UBS_API int UB_API_WRAP(epoll_create1)(int flags)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::epoll_create1(flags);
    }

    return 0;
}


UBS_API int UB_API_WRAP(epoll_pwait)(int epfd, struct epoll_event *events, int maxevents, int timeout,
                                     const sigset_t *sigmask)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::epoll_pwait(epfd, events, maxevents, timeout, sigmask);
    }

    return 0;
}