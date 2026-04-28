/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: Provide the utility for umq buffer, iov, etc
 * Author:
 * Create: 2026-04-20
 * Note:
 * History: 2026-04-20
 */
#ifndef FILE_DESCRIPTOR_ASYNC_H
#define FILE_DESCRIPTOR_ASYNC_H

#include <pthread.h>
#include <cstdint>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <vector>
#include <unordered_map>
#include "brpc_spsc_ring_queue.h"
#include "file_descriptor.h"

namespace Brpc::async {

static constexpr auto MAX_READABLE_FD_COUNT = 0x10000U;

/*
 * 通过用户可见的socket fd信息，一次查找全部可用的信息
 * (1) socket_fd_object: 封装的socket fd对象
 * (2) main_umq: 主umq
 * (3) share_jfr_fd: 共享jfr模式，使用的公共jfr对应的fd
 * (4) tx_interrupt_fd: 当收到来自umq的流控update信息时，会通过此fd通知可写状态
 * (5) rx_interrupt_fd: 本用于表示写读通过fd，在收到此fd的事件时，表示对方申请流控，需要调用rearm
 * (6) rd_event_fd: 包装的socket fd对象中的一个event fd, 用于通知此socket可读
 */
struct SocketConnectInfo {
    SocketFd *socket_fd_object{ nullptr };
    uint64_t main_umq{ 0 };
    int share_jfr_fd{ -1 };
    int tx_interrupt_fd{ -1 };
    int rx_interrupt_fd{ -1 };
    int rd_event_fd{ -1 };
};

/*
 *    (a)           (b)                                  (d)              (e)
 *  socket_fd     tx_fd                     ┌──────── share_jfr_fd       rx_fd
 *      │            │                      │              │               │
 *      │            │                      │              │               │
 *      │            │                      │              │               │
 *      │            │                      │              │               │
 *      │            │                      │              │               │
 *      │            │                      │              │               │
 * ┌────┼────────────┼─────┐                │           ┌──┼───────────────┼──┐
 * │     AsyncEpoolFd      │                │           │    DaemonEpollFd    │
 * └───────┬───────────────┘                │           └──────┬──────────────┘
 *         │                                │                  │
 *         │                                │                  │
 *         │                                │                  │
 *         │                    (g)         │                  │
 *    readable_event_fd  <──────────────────┘              stop_event_fd
 *        (c)                                                 (f)
 *
 * - 前台 epoll fd，对上提供epoll操作，关联的有三种event
 *   (1) socket_fd(a)，上层每调用一次epoll_add，就添加一个，这个在ubsocket场景，首次写的OUT有用
 *   (2) tx_fd(b)，上层调用epoll_add或epoll_mod时，events带OUT时才注册，用于表示此socket可写
 *   (3) readable_event_fd，与上层注册的socket个数无关，总共一个用于通知有socket可读
 * - 后台 epoll fd，在后台有一个专门线程运行，关联的有三种event
 *   (1) share_jfr_fd(d) 与上层注册的socket个数无关，总共一个（多EID时会有多个）用于表示有数据可读，后台线程将umq_buf添加到socket_obj
 *       中后，生成对应的events，放到队列中，由前台wait线程返回给上层
 *   (2) rx_fd(e)，用于处理回复流控信息，由后台线程自行处理，不需前台线程参与
 *   (3) stop_event_fd(f)，在整个功能退出时，用于通知后台epoll wait退出并停止后台线程
 * - 前后台的关联事件
 *   后台通过readable_event_fd通知前台，除此eventfd之外，还有一个队列，用于存放可读的events(g)
 */
class EpollDaemon {
public:
    static EpollDaemon &GetInstance() noexcept;
    ~EpollDaemon()
    {
        Stop();
    }

public:
    EpollDaemon(const EpollDaemon &) = delete;

    EpollDaemon(EpollDaemon &&) = delete;

    EpollDaemon &operator = (const EpollDaemon &) = delete;

    EpollDaemon &operator = (EpollDaemon &&) = delete;

    int Start() noexcept;

    void Stop() noexcept;

    int AddEvent(::EpollFd &epoll_fd, int socket_fd, const struct epoll_event &event,
        SocketConnectInfo &connect_info) noexcept;

    int RemoveEvent(::EpollFd &epoll_fd, int socket_fd) noexcept;

private:
    EpollDaemon() noexcept;

    void DaemonThreadRun() noexcept;

    void ProcessOneEvent(const struct epoll_event &event) noexcept;

    void ProcessShareJfrEvent(const struct epoll_event &event, uint64_t main_umq) noexcept;

    int ProcessMainUmqRearm(uint64_t main_umq) noexcept;

    static std::unordered_set<::SocketFd *> SiftSocketEventsWithUmqBuffers(umq_buf_t **buf, int count) noexcept;

    static int GetSocketConnectInfo(int socket_fd, SocketConnectInfo &info) noexcept;

private:
    int m_epoll_fd;
    int m_notify_fd;
    uint32_t m_event_num{ 0 };
    std::unordered_map<int, uint64_t> m_jfr_main_umq;
    std::vector<struct epoll_event> m_events;
    std::thread m_wait_thread;
    u_external_mutex_t *m_mutex;
};

struct EpollSocketInOutFds {
    int epoll_in_fd{ -1 };
    int epoll_out_fd{ -1 };
};

enum EpollEventType {
    EPOLL_EVENT_RAW_SOCKET = 0,
    EPOLL_EVENT_UB_SOCKET_IN,
    EPOLL_EVENT_UB_SOCKET_OUT,
    EPOLL_EVENT_BUTT
};

struct EpollEventData {
    EpollEventType event_type;
    int socket_fd;
    struct epoll_event event;
    EpollEventData *next{ nullptr };

    EpollEventData(EpollEventType type, int socket, const struct epoll_event &evt) noexcept
        : event_type{ type }, socket_fd{ socket }, event{ evt }
    {}
};

class EpollFdAsync : public ::EpollFd {
public:
    /*
     * SPSCRingQueue需要使用cache line对齐的申请，标准的new无法进行对齐，需要重载new和delete
     */
    static void *operator new(std::size_t size)
    {
        return ::aligned_alloc(alignof(EpollFdAsync), size);
    }

    static void operator delete(void *ptr)
    {
        free(ptr);
    }

    explicit EpollFdAsync(int epoll_fd) noexcept : ::EpollFd{ epoll_fd } {}

    ~EpollFdAsync() override;

    int EpollCtlAdd(int socket_fd, struct epoll_event *event, bool use_polling) override;

    int EpollCtlMod(int socket_fd, struct epoll_event *event, bool use_polling) override;

    int EpollCtlDel(int socket_fd, struct epoll_event *event, bool use_polling) override;

    int EpollWait(struct epoll_event *events, int max_events, int timeout, bool use_polling) override;

    int AddSocketReadableEvent(int socket_fd, epoll_data_t data) noexcept;

    int SetSocketsReadable() noexcept;

private:
    int ArrangeWakeupEvents(struct epoll_event *events, int input_count, int max_events) noexcept;
    void ReleaseRemovedEventsData() noexcept;
    int EnsureReadableEventFdReady() noexcept;
    int AddPureSocketEvent(int socket_fd, struct epoll_event *event) noexcept;
    int DelPureSocketEvent(int socket_fd) noexcept;
    int AddSocketOutEvent(int socket_fd, int event_fd, struct epoll_event *event) noexcept;
    int DelSocketOutEvent(int socket_fd, int event_fd) noexcept;
    static int GetEpollSocketInOutFds(int socket_fd, EpollSocketInOutFds &fds) noexcept;
    ALWAYS_INLINE bool IsSocketEventDataExist(int fd) noexcept
    {
        ScopedUbExclusiveLocker sLock(m_mutex);
        return m_socket_data.find(fd) != m_socket_data.end();
    }

    ALWAYS_INLINE bool RemoveSocketEventData(int fd) noexcept
    {
        ScopedUbExclusiveLocker sLock(m_mutex);
        auto pos = m_socket_data.find(fd);
        if (UNLIKELY(pos == m_socket_data.end())) {
            return false;
        }
        auto removed = pos->second;
        m_socket_data.erase(pos);
        if (removed != nullptr) {
            removed->next = m_removed_head;
            m_removed_head = removed;
        }
        return true;
    }

    ALWAYS_INLINE bool InsertSocketEventData(int fd, EpollEventData *data) noexcept
    {
        ScopedUbExclusiveLocker sLock(m_mutex);
        auto pos = m_socket_data.find(fd);
        if (UNLIKELY(pos != m_socket_data.end())) {
            return false;
        }
        m_socket_data.emplace(fd, data);
        return true;
    }

private:
    int m_socket_readable_event_fd{ -1 };
    EpollEventData m_readable_event_data{ EPOLL_EVENT_UB_SOCKET_IN, -1, epoll_event{} };
    std::unordered_map<int, EpollEventData *> m_socket_data;
    EpollEventData *m_removed_head{ nullptr }; // 待删除的event data列表，用wait唤醒时统一释放
    SPSCRingQueue<struct epoll_event> m_readable_sockets_queue{ MAX_READABLE_FD_COUNT };
};
}

#endif // FILE_DESCRIPTOR_ASYNC_H
