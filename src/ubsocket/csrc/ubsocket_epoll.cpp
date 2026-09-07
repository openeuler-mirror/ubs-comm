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
#include "common/ubsocket_common_includes.h"
#include "core/ubsocket_event_epoll.h"
#include "include/ubsocket.h"
#include "under_api/dl_libc_api.h"

using namespace ock::ubs;

UBS_API int UB_API_WRAP(epoll_create)(int size)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE || !GlobalSetting::UBS_INITED) {
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
    if (GlobalSetting::UBS_NATIVE_TCP_MODE || !GlobalSetting::UBS_INITED) {
        return LibcApi::epoll_ctl(epfd, op, fd, event);
    }

    EventPollPtr eventPoll = ArraySet<EventPoll>::GetInstance().GetItem(epfd);
    if (UNLIKELY(eventPoll == nullptr)) {
        /* Fallback to native epoll for fds not created via UB (e.g. created
         * during ubsocket_init before UBS_INITED was set, or by libc/runtime).
         * Returning -1 here would break the caller's epoll loop. */
        return LibcApi::epoll_ctl(epfd, op, fd, event);
    }

    return eventPoll->EpollCtl(op, fd, event);
}

UBS_API int UB_API_WRAP(epoll_wait)(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE || !GlobalSetting::UBS_INITED) {
        return LibcApi::epoll_wait(epfd, events, maxevents, timeout);
    }

    EventPollPtr eventPoll = ArraySet<EventPoll>::GetInstance().GetItem(epfd);
    if (UNLIKELY(eventPoll == nullptr)) {
        /* Fallback to native epoll_wait for fds not tracked by UB. */
        return LibcApi::epoll_wait(epfd, events, maxevents, timeout);
    }
    return eventPoll->EpollWait(events, maxevents, timeout);
}

UBS_API int UB_API_WRAP(epoll_create1)(int flags)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE || !GlobalSetting::UBS_INITED) {
        return LibcApi::epoll_create1(flags);
    }

    int epollFd = LibcApi::epoll_create1(flags);
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

UBS_API int UB_API_WRAP(epoll_pwait)(int epfd, struct epoll_event *events, int maxevents, int timeout,
                                     const sigset_t *sigmask)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE || !GlobalSetting::UBS_INITED) {
        return LibcApi::epoll_wait(epfd, events, maxevents, timeout);
    }

    EventPollPtr eventPoll = ArraySet<EventPoll>::GetInstance().GetItem(epfd);
    if (UNLIKELY(eventPoll == nullptr)) {
        /* Fallback to native epoll_wait for fds not tracked by UB. */
        return LibcApi::epoll_wait(epfd, events, maxevents, timeout);
    }
    return eventPoll->EpollWait(events, maxevents, timeout);
}
