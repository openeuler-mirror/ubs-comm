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
#include "core/ubsocket_socket_set.h"
#include "common/ubsocket_common_includes.h"
#include "core/ubsocket_event_epoll.h"
#include "include/ubsocket.h"
#include "under_api/dl_libc_api.h"

using namespace ock::ubs;

UBS_API int UB_API_WRAP(epoll_create)(int size)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::epoll_create(size);
    }

    int epollFd = LibcApi::epoll_create(size);
    if (epollFd < 0) {
        return epollFd;
    }

    EventPoll *eventPoll = new AsyncEventPoll(epollFd);
    if (UNLIKELY(eventPoll == nullptr)) {
        UBS_VLOG_ERR("create async event poll failed, epoll fd: %d\n", epollFd);
        LibcApi::close(epollFd);
        return -1;
    }

    ArraySet<EventPoll>::GetInstance().OverrideItem(epollFd, eventPoll);
    return epollFd;
}

UBS_API int UB_API_WRAP(epoll_ctl)(int epfd, int op, int fd, struct epoll_event *event)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::epoll_ctl(epfd, op, fd, event);
    }
    
    EventPoll *eventPoll = ArraySet<EventPoll>::GetInstance().GetItem(epfd);
    if (UNLIKELY(eventPoll == nullptr)) {
        UBS_VLOG_ERR("event poll can not been find, epoll fd: %d\n", epfd);
        return -1;
    }
    // TODO：取socket，待ubsocket_sock.cpp中的实现完成后参考
    SocketPtr socketPtr = SocketSet::Instance().GetSocket(fd);
    if (socketPtr == nullptr) {
        return LibcApi::epoll_ctl(epfd, op, fd, event);
    }
    return eventPoll->EpollCtl(op, socketPtr, event);
}

UBS_API int UB_API_WRAP(epoll_wait)(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
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