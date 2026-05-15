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
#include <cerrno>

#include "ubsocket_event_epoll.h"

namespace ock {
namespace ubs {

std::unordered_map<int, EpollMapper *> g_socket_epoll_mappers{};
u_rw_lock_t *g_socket_epoll_lock = nullptr;

EpollMapper *GetSocketEpollMapper(int socket_fd)
{
    ReadLocker s_lock(g_socket_epoll_lock);
    auto iter = g_socket_epoll_mappers.find(socket_fd);
    if (iter == g_socket_epoll_mappers.end()) {
        return nullptr;
    }
    return iter->second;
}

bool CreateSocketEpollMapper(int socket_fd, EpollMapper *&mapper)
{
    bool result = false;
    WriteLocker s_lock(g_socket_epoll_lock);
    auto iter = g_socket_epoll_mappers.find(socket_fd);
    if (iter != g_socket_epoll_mappers.end()) {
        mapper = iter->second;
    } else {
        mapper = new (std::nothrow) EpollMapper(socket_fd);
        if (mapper == nullptr) {
            return false;
        }
        g_socket_epoll_mappers[socket_fd] = mapper;
        result = true;
    }
    return result;
}

void CleanSocketEpollMapper(int socket_fd)
{
    EpollMapper *mapper = GetSocketEpollMapper(socket_fd);
    if (mapper == nullptr) {
        return;
    }
    {
        WriteLocker s_lock(g_socket_epoll_lock);
        g_socket_epoll_mappers.erase(socket_fd);
    }
    mapper->Clear();
    free(mapper);
    mapper = nullptr;
}

int EpollRunner::Start()
{
    mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
    if (mutex_ == nullptr) {
        UBS_VLOG_ERR("async_epoll g_external_lock_ops.create(LT_EXCLUSIVE) failed.");
        return -1;
    }

    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        UBS_VLOG_ERR("async_epoll epoll_create1() failed : %d : %s\n", errno, strerror(errno));
        LockRegistry::LOCK_OPS.destroy(mutex_);
        mutex_ = nullptr;
        return -1;
    }

    // 此 exit_efd，仅用于表示退出，停止线程，释放资源
    exit_efd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (exit_efd_ < 0) {
        UBS_VLOG_ERR("async_epoll eventfd() failed : %d : %s\n", errno, strerror(errno));
        close(epoll_fd_);
        epoll_fd_ = -1;
        LockRegistry::LOCK_OPS.destroy(mutex_);
        mutex_ = nullptr;
        return -1;
    }

#ifdef ENABLED
    DaemonEventData event_data{};
    struct epoll_event event{};
    event.events = EPOLLIN | EPOLLET;
    event_data.event_data.type = DAEMON_EVENT_TYPE_STOP;
    event_data.event_data.data = exit_efd_;
    event.data.u64 = event_data.u64;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, exit_efd_, &event) == -1) {
        UBS_VLOG_ERR("async_epoll epoll_ctl(ADD) failed : %d : %s\n", errno, strerror(errno));
        close(exit_efd_);
        close(epoll_fd_);
        exit_efd_ = -1;
        epoll_fd_ = -1;
        LockRegistry::LOCK_OPS.destroy(m_mutex);
        mutex_ = nullptr;
        return -1;
    }

    wait_thread_ = std::thread([this]() { RunnerThreadRun(); });
#endif
    return 0;
}

void EpollRunner::Stop()
{
    if (exit_efd_ < 0) {
        return;
    }

    // 通过向exit_efd_写入数据，唤醒后台线程退出流程
    if (eventfd_write(exit_efd_, 1) < 0) {
        UBS_VLOG_ERR("async_epoll eventfd_write() failed : %d : %s\n", errno, strerror(errno));
        return;
    }
#ifdef ENABLED
    // 正常情况下 joinable()为真，如果不可join，可能是线程异常退出
    if (!m_wait_thread.joinable()) {
        UBS_VLOG_ERR("async_epoll wait thread is not joinable()\n");
        return;
    }

    m_wait_thread.join();
    close(exit_efd_);
    close(epoll_fd_);
    exit_efd_ = -1;
    epoll_fd_ = -1;
    LockRegistry::LOCK_OPS.destroy(mutex_);
    mutex_ = nullptr;
#endif
}

void EpollRunner::RunInThread() noexcept
{
    UBS_VLOG_INFO("async_epoll epoll_wait_async_daemon thread started.\n");
    pthread_setname_np(pthread_self(), "ubs_poller");
#ifdef ENABLED
    bool stopped = false;

    std::vector<struct epoll_event> events;
    events.resize(MAX_EPOLL_WAIT_COUNT);
    while (LIKELY(!stopped)) {
        auto count = epoll_wait(epoll_fd_, events.data(), MAX_EPOLL_WAIT_COUNT, 10000);
        if (UNLIKELY(count < 0)) {
            if (errno == EINTR) {
                continue;
            }
            UBS_VLOG_ERR("async_epoll epoll_wait() failed: %d : %s\n", errno, strerror(errno));
            break;
        }

        for (auto i = 0; i < count; i++) {
            auto event_data = (DaemonEventData *)&events[i].data;
            if (UNLIKELY(event_data->event_data.type == DAEMON_EVENT_TYPE_STOP)) {
                stopped = true;
                RPC_ADPT_VLOG_INFO("async_epoll notify exit fd received, exit now\n");
                break;
            }

            ProcessOneEvent(events[i]);
        }
    }
    UBS_VLOG_INFO("async_epoll epoll_wait_async_daemon thread exit.\n");
#endif
}

int AsyncEventPoll::InitSocketReadableFd()
{
#ifdef ENABLED
    auto fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (UNLIKELY(fd < 0)) {
        UBS_VLOG_ERR("async_epoll create event fd for epoll readable failed: %d : %s\n", errno, strerror(errno));
        return -1;
    }

    struct epoll_event event{};
    event.events = EPOLLIN | EPOLLET;
    event.data.ptr = &m_readable_event_data;
    m_readable_event_data.socket_fd = fd;
    auto ret = epoll_ctl(m_fd, EPOLL_CTL_ADD, fd, &event);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("async_epoll epoll_ctl add for epoll readable failed: %d : %s\n", errno, strerror(errno));
        close(fd);
        return -1;
    }
#endif
    return 0;
}

int AsyncEventPoll::EpollCtl(int op, const Socket *const socket, struct epoll_event *event)
{
    int ret = -1;
#ifdef ENABLED
    bool mapper_create = false;
    EpollMapper *mapper = nullptr;
    Locker sLock(ctl_mutex_);
    if (op == EPOLL_CTL_ADD) {
        mapper_create = CreateSocketEpollMapper(fd, mapper);
    } else {
        // TODO：补充其他实现
        // mapper = GetSocketEpollMapper(fd);
    }
    switch (op) {
        case EPOLL_CTL_ADD:
            ret = EpollCtlAdd(socket, event);
            if (ret == 0 && mapper != nullptr) {
                mapper->Add(m_fd);
            } else if (mapper_create) {
                WriteLocker s_lock(g_socket_epoll_lock);
                g_socket_epoll_mappers.erase(fd);
                free(mapper);
                mapper = nullptr;
            }
            break;
        // TODO：补充其他实现
        default:
            UBS_VLOG_ERR("Invalid op code(%d), epfd: %d, fd: %d\n", op, m_fd, fd);
            errno = EINVAL;
    }
#endif
    return ret;
}

int AsyncEventPoll::EpollWait(const Socket *const socket, struct epoll_event *events, int maxevents, int timeout)
{
    return 0;
}

int AsyncEventPoll::EpollCtlAdd(const Socket *const socket, struct epoll_event *event)
{
#ifdef ENABLED
    if (UNLIKELY(event == nullptr)) {
        UBS_VLOG_ERR("async_epoll AddEvent invalid args fd:%d, event:%p\n", socket_fd, event);
        errno = EINVAL;
        return -1;
    }

    if (UNLIKELY((event->events & EPOLLET) == 0)) {
        UBS_VLOG_ERR("async_epoll AddEvent must be edge-triggered notification.\n");
        errno = EINVAL;
        return -1;
    }

    // 监听原生socket的事件
    if (UNLIKELY(AddRawSocketEvent(socket, event) != 0)) {
        UBS_VLOG_ERR("async_epoll epoll ctl add raw socket: %d failed\n", socket_fd);
        return -1;
    }

    // 添加runner监听的事件，epoll_runner由socket持有，umq的socket持有umq的runner，rdma的socket持有rdma的runner...
    // socket->GetEpollRunner()->AddEpollEvent(socket);

    // 添加对应协议的tx事件，epoll_ops由socket持有，umq的socket持有umq的ops，rdma的socket持有rdma的ops...
    if ((event->events & EPOLLOUT) == EPOLLOUT) {
        ret = socket->GetEpollOps()->AddTxEvent(socket, epoll_fd_);
        if (ret < 0) {
            DelRawSocketEvent(socket);
            UBS_VLOG_ERR("async_epoll epoll_ctl(ADD:%d) failed: %d : %s\n", socket_fd, errno, strerror(errno));
            return -1;
        }
    }
    return 0;
}

// TODO：此方法之前会用一个map记录socket_fd是否已经添加到该epoll_fd中了
//       重构后去除了这个逻辑，依赖原生epoll_ctl重复添加时候报错，需验证该逻辑正确性
int AsyncEventPoll::AddRawSocketEvent(const Socket *const socket, struct epoll_event *event)
{
    struct epoll_event pure_event{};
    auto event_data = new (std::nothrow) EpollEvent(EPOLL_EVENT_RAW_SOCKET, socket.GetRawFD(), *event);
    if (UNLIKELY(event_data == nullptr)) {
        UBS_VLOG_ERR("async_epoll add out event for socket fd: %d alloc failed.\n", socket_fd);
        return -1;
    }

    pure_event.events = event->events;
    pure_event.data.ptr = event_data;
    auto ret = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, socket.GetRawFD(), &pure_event);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("async_epoll add pure event for socket fd: %d failed: %d : %s\n", socket.GetRawFD(), errno,
                     strerror(errno));
        delete event_data;
        return -1;
    }
#endif
    return 0;
}

int AsyncEventPoll::DelRawSocketEvent(const Socket *const socket)
{
    return 0;
}

int AsyncEventPoll::EpollCtlMod(const Socket *const socket, struct epoll_event *event)
{
    return 0;
}

int AsyncEventPoll::EpollCtlDel(const Socket *const socket, struct epoll_event *event)
{
    return 0;
}

} // namespace ubs
} // namespace ock