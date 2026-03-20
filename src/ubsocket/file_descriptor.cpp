/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: Provide the utility for umq buffer, iov, etc
 * Author:
 * Create: 2025-07-31
 * Note:
 * History: 2025-07-31
*/

#include "file_descriptor.h"

template <>
SocketFd *Fd<SocketFd>::m_fd_obj_map[RPC_ADPT_FD_MAX] = {0};
template <>
pthread_rwlock_t Fd<SocketFd>::m_rwlock = PTHREAD_RWLOCK_INITIALIZER;

template <>
EpollFd *Fd<EpollFd>::m_fd_obj_map[RPC_ADPT_FD_MAX] = {0};
template <>
pthread_rwlock_t Fd<EpollFd>::m_rwlock = PTHREAD_RWLOCK_INITIALIZER;

std::unordered_map<int, SocketEpollMapper *> g_socket_epoll_mappers{};

UbLazyLock<UbRWLock> g_socket_epoll_lock{};

void SocketEpollMapper::Clear()
{
    for (int epfd : m_epoll_set) {
        EpollFd *obj = Fd<EpollFd>::GetFdObj(epfd);
        if (obj == nullptr) {
            continue;
        }
        std::lock_guard<std::mutex> guard(obj->GetCtlMutex());
        obj->EpollCtlDel(m_fd, nullptr);
    }
    m_epoll_set.clear();
}

SocketEpollMapper* GetSocketEpollMapper(int socket_fd)
{
    ScopedUbReadLock s_lock(g_socket_epoll_lock.get());
    auto iter = g_socket_epoll_mappers.find(socket_fd);
    if (iter == g_socket_epoll_mappers.end()) {
        return nullptr;
    }
    return iter->second;
}

bool CreateSocketEpollMapper(int socket_fd, SocketEpollMapper*& mapper)
{
    bool result = false;
    ScopedUbWriteLock s_lock(g_socket_epoll_lock.get());
    auto iter = g_socket_epoll_mappers.find(socket_fd);
    if (iter != g_socket_epoll_mappers.end()) {
        mapper = iter->second;
    } else {
        mapper = new SocketEpollMapper(socket_fd);
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
    SocketEpollMapper* mapper = GetSocketEpollMapper(socket_fd);
    if (mapper == nullptr) {
        return;
    }
    {
        ScopedUbWriteLock s_lock(g_socket_epoll_lock.get());
        g_socket_epoll_mappers.erase(socket_fd);
    }
    mapper->Clear();
    free(mapper);
    mapper = nullptr;
}