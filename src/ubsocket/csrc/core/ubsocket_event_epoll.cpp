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
#include <new>
#include <utility>

#include "common/ubsocket_common_includes.h"
#include "ubsocket_event_epoll.h"

namespace ock {
namespace ubs {

/* 注册表: socket_fd -> 该 fd 注册过的所有 epoll 归属对象。
 * map 中的 shared_ptr 是“基引用”, Get/Create 返回的拷贝是“借出引用”; Clean 只从 map 摘除基引用,
 * 对象在最后一个持有者释放后自动析构, 因此不会再出现 close/ADD 失败路径提前 delete 的 use-after-free */
std::unordered_map<int, EpollMapperPtr> g_socket_epoll_mappers{};
u_rw_lock_t *g_socket_epoll_lock = nullptr;

EpollMapperPtr GetSocketEpollMapper(int socket_fd)
{
    ReadLocker s_lock(g_socket_epoll_lock);
    auto iter = g_socket_epoll_mappers.find(socket_fd);
    if (iter == g_socket_epoll_mappers.end()) {
        return nullptr;
    }
    return iter->second; /* 拷贝 shared_ptr: 借出引用, 调用方出作用域自动归还 */
}

bool CreateSocketEpollMapper(int socket_fd, EpollMapperPtr &mapper)
{
    WriteLocker s_lock(g_socket_epoll_lock);
    auto iter = g_socket_epoll_mappers.find(socket_fd);
    if (iter != g_socket_epoll_mappers.end()) {
        mapper = iter->second;
        return false;
    }

    EpollMapperPtr created(new (std::nothrow) EpollMapper(socket_fd));
    if (created == nullptr) {
        mapper = nullptr;
        return false;
    }
    g_socket_epoll_mappers.emplace(socket_fd, created); /* 注册表持有基引用 */
    mapper = std::move(created);
    return true;
}

void CleanSocketEpollMapper(int socket_fd, const EpollMapper *expect)
{
    EpollMapperPtr mapper = nullptr;
    {
        /* find+erase 同一临界区: 只有真正 erase 掉的线程拿到摘除权, 并发调用者 find 不到直接返回,
         * 杜绝 double free; 对象不在此处析构——其它线程可能仍持有借出的 shared_ptr,
         * 由最后一个持有者负责释放, 杜绝 use-after-free。
         * expect 非空时只摘“表中仍是该对象”的条目: 并发 close 之后同 fd 号重新 ADD 出来的新对象
         * 不会被误删, 否则新连接的 epoll 注册就失去追踪 */
        WriteLocker s_lock(g_socket_epoll_lock);
        auto iter = g_socket_epoll_mappers.find(socket_fd);
        if (iter == g_socket_epoll_mappers.end()) {
            return;
        }
        if (expect != nullptr && iter->second.get() != expect) {
            return;
        }
        mapper = std::move(iter->second);
        g_socket_epoll_mappers.erase(iter);
    }
    mapper->Clear();
}

void CleanAllSocketEpollMappers()
{
    if (g_socket_epoll_lock == nullptr) {
        return;
    }

    /* 锁内只做表搬运, 析构(可能触发 N 次 ~EpollMapper)放到锁外, 避免长时间持写锁 */
    std::unordered_map<int, EpollMapperPtr> removed;
    {
        WriteLocker s_lock(g_socket_epoll_lock);
        removed.swap(g_socket_epoll_mappers);
    }
    for (auto &mapper : removed) {
        if (mapper.second != nullptr) {
            mapper.second->Clear();
        }
    }
}

void OnSocketClosed(int socket_fd)
{
    if (g_socket_epoll_lock == nullptr) {
        return; /* ubsocket 未初始化: rwlock 未创建且 LockRegistry 未加载, 纯 TCP/应用 fd close 直通 */
    }

    EpollMapperPtr mapper = nullptr;
    std::vector<int> epfds;
    {
        ReadLocker s_lock(g_socket_epoll_lock);
        auto iter = g_socket_epoll_mappers.find(socket_fd);
        if (iter == g_socket_epoll_mappers.end() || iter->second == nullptr) {
            return; /* fd 从未注册进任何 EventPoll (纯 TCP fd/eventfd 等), 零开销返回 */
        }
        mapper = iter->second; /* 借出引用: 保证快照与后续清理期间对象存活 */
        epfds = mapper->GetEpollFds();
    }
    /* 通知该 fd 注册过的每个 EventPoll 清理实例级注册表 (socket_data_ 等) */
    for (int epfd : epfds) {
        EventPollPtr poll = ArraySet<EventPoll>::GetInstance().GetItem(epfd);
        if (poll != nullptr) {
            poll->OnSocketClosed(socket_fd);
        }
    }
    CleanSocketEpollMapper(socket_fd, mapper.get());
}

} // namespace ubs
} // namespace ock
