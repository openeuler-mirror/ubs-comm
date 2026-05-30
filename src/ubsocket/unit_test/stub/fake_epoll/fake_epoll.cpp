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
#include "fake_epoll.h"

#include <dlfcn.h>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace ock {
namespace ubs {
namespace test {

int FakeEpollCtl::next_fd_ = FAKE_FD_BASE;

struct FakeEpollState {
    std::mutex mtx;
    std::unordered_set<int> epoll_fds;
    std::unordered_set<int> event_fds;
    std::unordered_set<int> all_fake_fds;
    std::unordered_map<int, std::unordered_map<int, struct epoll_event>> registered_events;
    std::vector<struct epoll_event> next_wait_events;
    int next_wait_ret = 0;
    int next_eventfd_ret = -1;
    int next_epoll_create_ret = -1;
};

static FakeEpollState g_state;

FakeEpollState &GetFakeEpollState()
{
    return g_state;
}

void FakeEpollCtl::Reset()
{
    std::lock_guard<std::mutex> lock(g_state.mtx);
    g_state.epoll_fds.clear();
    g_state.event_fds.clear();
    g_state.all_fake_fds.clear();
    g_state.registered_events.clear();
    g_state.next_wait_events.clear();
    g_state.next_wait_ret = 0;
    g_state.next_eventfd_ret = -1;
    g_state.next_epoll_create_ret = -1;
    next_fd_ = FAKE_FD_BASE;
}

void FakeEpollCtl::SetNextEpollWaitEvents(const std::vector<struct epoll_event> &events)
{
    std::lock_guard<std::mutex> lock(g_state.mtx);
    g_state.next_wait_events = events;
    g_state.next_wait_ret = static_cast<int>(events.size());
}

void FakeEpollCtl::SetNextEpollWaitReturn(int ret)
{
    std::lock_guard<std::mutex> lock(g_state.mtx);
    g_state.next_wait_ret = ret;
}

void FakeEpollCtl::SetNextEventfdReturn(int fd)
{
    std::lock_guard<std::mutex> lock(g_state.mtx);
    g_state.next_eventfd_ret = fd;
}

void FakeEpollCtl::SetNextEpollCreateReturn(int fd)
{
    std::lock_guard<std::mutex> lock(g_state.mtx);
    g_state.next_epoll_create_ret = fd;
}

bool FakeEpollCtl::IsFakeFd(int fd)
{
    std::lock_guard<std::mutex> lock(g_state.mtx);
    return g_state.all_fake_fds.count(fd) > 0;
}

int FakeEpollCtl::AllocFakeFd()
{
    std::lock_guard<std::mutex> lock(g_state.mtx);
    int fd = next_fd_++;
    g_state.all_fake_fds.insert(fd);
    return fd;
}

void FakeEpollCtl::ReleaseFakeFd(int fd)
{
    std::lock_guard<std::mutex> lock(g_state.mtx);
    g_state.all_fake_fds.erase(fd);
}

int FakeEpollCtl::GetFdCount()
{
    std::lock_guard<std::mutex> lock(g_state.mtx);
    return static_cast<int>(g_state.all_fake_fds.size());
}

} // namespace test
} // namespace ubs
} // namespace ock

using FakeEpollState = ock::ubs::test::FakeEpollState;

FakeEpollState &S()
{
    return ock::ubs::test::GetFakeEpollState();
}

extern "C" {
int epoll_create1(int flags)
{
    auto &s = S();
    std::lock_guard<std::mutex> lock(s.mtx);
    if (s.next_epoll_create_ret >= 0) {
        int fd = s.next_epoll_create_ret;
        s.next_epoll_create_ret = -1;
        s.epoll_fds.insert(fd);
        s.all_fake_fds.insert(fd);
        return fd;
    }
    int fd = ock::ubs::test::FakeEpollCtl::next_fd_++;
    s.epoll_fds.insert(fd);
    s.all_fake_fds.insert(fd);
    return fd;
}

int epoll_create(int size)
{
    return epoll_create1(0);
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    auto &s = S();
    std::lock_guard<std::mutex> lock(s.mtx);
    if (s.epoll_fds.count(epfd) == 0) {
        errno = EBADF;
        return -1;
    }
    auto &epoll_events = s.registered_events[epfd];
    switch (op) {
        case EPOLL_CTL_ADD:
            if (epoll_events.count(fd) > 0) {
                errno = EEXIST;
                return -1;
            }
            if (event != nullptr) {
                epoll_events[fd] = *event;
            } else {
                epoll_event empty = {};
                epoll_events[fd] = empty;
            }
            return 0;
        case EPOLL_CTL_MOD:
            if (epoll_events.count(fd) == 0) {
                errno = ENOENT;
                return -1;
            }
            if (event != nullptr) {
                epoll_events[fd] = *event;
            }
            return 0;
        case EPOLL_CTL_DEL:
            if (epoll_events.count(fd) == 0) {
                errno = ENOENT;
                return -1;
            }
            epoll_events.erase(fd);
            return 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    auto &s = S();
    std::lock_guard<std::mutex> lock(s.mtx);
    if (s.epoll_fds.count(epfd) == 0) {
        errno = EBADF;
        return -1;
    }
    if (s.next_wait_ret < 0) {
        int ret = s.next_wait_ret;
        s.next_wait_ret = 0;
        s.next_wait_events.clear();
        return ret;
    }
    int count = s.next_wait_ret;
    if (count == 0 && s.next_wait_events.empty()) {
        return 0;
    }
    int actual = static_cast<int>(s.next_wait_events.size());
    if (actual > maxevents) {
        actual = maxevents;
    }
    if (actual > 0 && events != nullptr) {
        for (int i = 0; i < actual; ++i) {
            events[i] = s.next_wait_events[i];
        }
    }
    s.next_wait_events.clear();
    s.next_wait_ret = 0;
    return actual > 0 ? actual : count;
}

int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const sigset_t *sigmask)
{
    return epoll_wait(epfd, events, maxevents, timeout);
}

int eventfd(unsigned int initval, int flags)
{
    auto &s = S();
    std::lock_guard<std::mutex> lock(s.mtx);
    if (s.next_eventfd_ret >= 0) {
        int fd = s.next_eventfd_ret;
        s.next_eventfd_ret = -1;
        s.event_fds.insert(fd);
        s.all_fake_fds.insert(fd);
        return fd;
    }
    int fd = ock::ubs::test::FakeEpollCtl::next_fd_++;
    s.event_fds.insert(fd);
    s.all_fake_fds.insert(fd);
    return fd;
}

int eventfd_write(int fd, uint64_t val)
{
    auto &s = S();
    std::lock_guard<std::mutex> lock(s.mtx);
    if (s.all_fake_fds.count(fd) == 0) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

int eventfd_read(int fd, uint64_t *val)
{
    auto &s = S();
    std::lock_guard<std::mutex> lock(s.mtx);
    if (s.all_fake_fds.count(fd) == 0) {
        errno = EBADF;
        return -1;
    }
    if (val != nullptr) {
        *val = 1;
    }
    return sizeof(uint64_t);
}

int close(int fd)
{
    auto &s = S();
    {
        std::lock_guard<std::mutex> lock(s.mtx);
        if (s.all_fake_fds.count(fd) == 0) {
            using RealCloseFn = int (*)(int);
            RealCloseFn real_close = reinterpret_cast<RealCloseFn>(dlsym(RTLD_NEXT, "close"));
            return real_close(fd);
        }
        s.all_fake_fds.erase(fd);
        s.epoll_fds.erase(fd);
        s.event_fds.erase(fd);
        s.registered_events.erase(fd);
        for (auto &ep_pair : s.registered_events) {
            ep_pair.second.erase(fd);
        }
    }
    return 0;
}

} // extern "C"