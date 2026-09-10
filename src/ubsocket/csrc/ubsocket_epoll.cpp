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
#include <signal.h>
#include "core/ubsocket_event_epoll.h"
#include "core/urma/urma_event_epoll.h"
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

    auto urma_poll = new (std::nothrow) urma::UrmaEventPoll(epollFd);
    if (UNLIKELY(urma_poll == nullptr || urma_poll->Init() != UBS_OK)) {
        UBS_VLOG_ERR("create async event poll failed, epoll fd: %d\n", epollFd);
        delete urma_poll;
        LibcApi::close(epollFd);
        return -1;
    }
    EventPoll *eventPoll = urma_poll;

    ArraySet<EventPoll>::GetInstance().OverrideItem(epollFd, eventPoll);
    return epollFd;
}

UBS_API int UB_API_WRAP(epoll_ctl)(int epfd, int op, int fd, struct epoll_event *event)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::epoll_ctl(epfd, op, fd, event);
    }

    EventPollPtr eventPoll = ArraySet<EventPoll>::GetInstance().GetItem(epfd);
    if (UNLIKELY(eventPoll == nullptr)) {
        UBS_VLOG_ERR("event poll can not been find, epoll fd: %d\n", epfd);
        errno = EBADF;
        return -1;
    }

    return eventPoll->EpollCtl(op, fd, event);
}

UBS_API int UB_API_WRAP(epoll_wait)(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::epoll_wait(epfd, events, maxevents, timeout);
    }

    if (UNLIKELY(maxevents <= 0)) {
        UBS_VLOG_ERR("epoll_wait maxevents(%d) invalid\n", maxevents);
        errno = EINVAL;
        return -1;
    }

    EventPollPtr eventPoll = ArraySet<EventPoll>::GetInstance().GetItem(epfd);
    if (UNLIKELY(eventPoll == nullptr)) {
        UBS_VLOG_ERR("event poll can not been find, epoll fd: %d\n", epfd);
        errno = EBADF;
        return -1;
    }
    return eventPoll->EpollWait(events, maxevents, timeout);
}

UBS_API int UB_API_WRAP(epoll_create1)(int flags)
{
    if (UNLIKELY((flags & ~EPOLL_CLOEXEC) != 0)) {
        UBS_VLOG_ERR("epoll_create1 invalid flags: 0x%x\n", flags);
        errno = EINVAL;
        return -1;
    }

    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::epoll_create1(flags);
    }

    int epollFd = LibcApi::epoll_create1(flags);
    if (epollFd < 0) {
        return epollFd;
    }

    auto urma_poll = new (std::nothrow) urma::UrmaEventPoll(epollFd);
    if (UNLIKELY(urma_poll == nullptr)) {
        UBS_VLOG_ERR("create urma event poll failed, epoll fd: %d\n", epollFd);
        LibcApi::close(epollFd);
        errno = ENOMEM;
        return -1;
    }
    if (UNLIKELY(urma_poll->Init() != UBS_OK)) {
        UBS_VLOG_ERR("init urma event poll failed, epoll fd: %d\n", epollFd);
        delete urma_poll;
        LibcApi::close(epollFd);
        return -1;
    }

    EventPoll *eventPoll = urma_poll;
    ArraySet<EventPoll>::GetInstance().OverrideItem(epollFd, eventPoll);
    return epollFd;
}

UBS_API int UB_API_WRAP(epoll_pwait)(int epfd, struct epoll_event *events, int maxevents, int timeout,
                                     const sigset_t *sigmask)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        /* Pass through kernel epoll_pwait, sigmask replaced by kernel during wait */
        return LibcApi::epoll_pwait(epfd, events, maxevents, timeout, sigmask);
    }

    sigset_t origin_mask;
    const bool mask_replaced = (sigmask != nullptr);
    if (mask_replaced) {
        /* pthread_sigmask returns the error number directly, not via errno */
        int rc = pthread_sigmask(SIG_SETMASK, sigmask, &origin_mask);
        if (rc != 0) {
            UBS_VLOG_ERR("epoll_pwait set sigmask failed, rc: %d\n", rc);
            errno = rc;
            return -1;
        }
    }

    int ret = -1;
    EventPollPtr eventPoll = ArraySet<EventPoll>::GetInstance().GetItem(epfd);
    if (UNLIKELY(eventPoll == nullptr)) {
        UBS_VLOG_ERR("event poll can not been find, epoll fd: %d\n", epfd);
        errno = EBADF;
    } else {
        ret = eventPoll->EpollWait(events, maxevents, timeout);
    }

    if (mask_replaced) {
        pthread_sigmask(SIG_SETMASK, &origin_mask, nullptr);
    }
    return ret;
}