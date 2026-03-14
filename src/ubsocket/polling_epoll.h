/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: Provide the utility for umq buffer, iov, etc
 * Author:
 * Create: 2025-12-10
 * Note:
 * History: 2025-12-10
*/

#ifndef POLLING_EPOLL_H
#define POLLING_EPOLL_H

#include <atomic>
#include <map>
#include <mutex>
#include <pthread.h>
#include <queue>
#include <sys/epoll.h>
#include "rpc_adpt_vlog.h"
#include "umq_types.h"

struct EpItem {
    int fd;
    struct epoll_event event;
};

struct EpListNode {
    EpItem epItem;
    EpListNode *prev;
    EpListNode *next;
};

struct EpList {
    EpListNode *head;
    EpListNode *tail;
    uint32_t length;
};

struct EventPoll {
    EpList *waitList;
    EpList *readyList;
    int epfd;
};

enum class SocketType {
    SOCKET_TYPE_EPOLL = 0,
    SOCKET_TYPE_TCP,
    SOCKET_TYPE_TCP_SERVER,
    SOCKET_TYPE_TCP_CLIENT,
};

struct Socket {
    SocketType type;
    int fd;
    uint64_t umqHandle = 0;
};

struct EpollSocket {
    Socket sock;
    EventPoll ep;
};

enum class PollingErrCode {
    OK = 0,
    ERR = -1,
    NOT_READY = -2,
};

#define FREE_PTR(ptr) \
    do {              \
        if ((ptr) != NULL) { \
            free(ptr);      \
            (ptr) = NULL;   \
        }             \
    } while (0)

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

#define GET_EPOLL_SOCKET(sockPtr) \
    (EpollSocket *)((uint8_t *)(sockPtr) - __builtin_offsetof(EpollSocket, sock))

class PollingEpoll {
public:
    PollingEpoll(const PollingEpoll&) = delete;
    PollingEpoll& operator=(const PollingEpoll&) = delete;

    static PollingEpoll& GetInstance()
    {
        static PollingEpoll instance;
        return instance;
    }

    void AddSocket(int fd, Socket* socket)
    {
        std::lock_guard<std::mutex> lock(g_table_mutex);
        g_table[fd] = socket;
    }

    /// fd 可能是 socket fd 也可能是 epoll fd
    void RemoveSocket(int fd);

    int GetAndPopQbuf(uint64_t umqHandle, umq_buf_t **buf)
    {
        std::lock_guard<std::mutex> lock(g_qbuf_table_mutex);
        auto it = g_qbuf_table.find(umqHandle);
        if (it != g_qbuf_table.end() && !g_qbuf_table[umqHandle].empty()) {
            auto qbuf_pair = g_qbuf_table[umqHandle].front();
            for (int i = 0; i < qbuf_pair.first; i++) {
                buf[i] = qbuf_pair.second[i];
            }
            g_qbuf_table[umqHandle].pop();
            return qbuf_pair.first;
        }

        *buf = nullptr;
        return 0;
    }

    void EpollListDestroy(EpList *epList);

    EpList *EpollListInit(void);

    int PollingEpollCreate(int epfd);

    void PollingEpollDestroy(EpollSocket *ep)
    {
        EpollListDestroy(ep->ep.waitList);
        EpollListDestroy(ep->ep.readyList);
    }

    int EpollListRemove(EpList *epList, int fd);

    int IsExistInEpollList(EpList *epList, int fd);

    void EpollListModify(EpList *epList, int fd, uint32_t epEvent);

    PollingErrCode EpollListInsert(EpList *epList, EpItem epItem);

    PollingErrCode AddEventIntoRdList(EpList *readyList, EpItem epItem, uint32_t epEvent);

    void EpollProcess(EventPoll *eventPoll);

    void EpollEventProcess(EventPoll *eventPoll, struct epoll_event *events, int maxevents, int *rdCnt);

    int PollingEpollWait(int epfd, struct epoll_event *events, int maxevents, int timeout);

    PollingErrCode IsUmqReadable(int fd);
    PollingErrCode IsUmqWriteable();
    PollingErrCode EpInEventProcess(EventPoll *eventPoll, EpItem epItem);
    PollingErrCode EpOutEventProcess(EventPoll *eventPoll, EpItem epItem);

    int SocketCreate(Socket **out, int fd, SocketType type, uint64_t umqHandle = 0);
    int EpollListReplace(EpList *epList, EpItem epItem);
    int EpollCtl(int epfd, int op, int fd, struct epoll_event *event);

    PollingErrCode UmqPoll(uint64_t umqHandle);

private:
    PollingEpoll() = default;

    void AddQbuf(uint64_t umqHandle, umq_buf_t** qbuf, int qbufNum)
    {
        std::lock_guard<std::mutex> lock(g_qbuf_table_mutex);
        auto it = g_qbuf_table.find(umqHandle);
        if (it != g_qbuf_table.end()) {
            it->second.push(std::make_pair(qbufNum, qbuf));
            return;
        }

        std::queue<std::pair<int, umq_buf_t**>> qbufQueue;
        qbufQueue.push(std::make_pair(qbufNum, qbuf));
        g_qbuf_table[umqHandle] = qbufQueue;
    }

    mutable std::map<int, Socket*> g_table;
    mutable std::map<uint64_t, std::queue<std::pair<int, umq_buf_t**>>> g_qbuf_table;
    mutable std::mutex g_table_mutex;
    mutable std::mutex g_qbuf_table_mutex;
};

#endif
