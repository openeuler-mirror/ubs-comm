/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: Provide the utility for umq buffer, iov, etc
 * Author:
 * Create: 2025-12-10
 * Note:
 * History: 2025-12-10
*/

#include "polling_epoll.h"
#include "socket_adapter.h"
#include "umq_pro_api.h"
#include "umq_api.h"
#include "file_descriptor.h"

const uint64_t SEC_TO_NSEC = 1000000000;
const uint64_t MSEC_TO_NSEC = 1000000;

uint64_t GetCurNanoSeconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t timestamp = static_cast<uint64_t>(ts.tv_sec) * SEC_TO_NSEC + static_cast<uint64_t>(ts.tv_nsec);
    return timestamp;
}

void PollingEpoll::EpollListDestroy(EpList *epList)
{
    if (UNLIKELY(epList == nullptr)) {
        return;
    }
    EpListNode *curNode = epList->head->next;
    while (curNode != epList->tail) {
        EpListNode *next = curNode->next;
        FREE_PTR(curNode);
        curNode = next;
    }
    FREE_PTR(epList->head);
    FREE_PTR(epList->tail);
    FREE_PTR(epList);
}

EpList* PollingEpoll::EpollListInit(void)
{
    EpList *epList = (EpList *)malloc(sizeof(EpList));
    if (UNLIKELY(epList == nullptr)) {
        return nullptr;
    }
    epList->head = (EpListNode *)malloc(sizeof(EpListNode));
    epList->tail = (EpListNode *)malloc(sizeof(EpListNode));
    if (UNLIKELY(epList->head == nullptr || epList->tail == nullptr)) {
        FREE_PTR(epList->head);
        FREE_PTR(epList->tail);
        FREE_PTR(epList);
        return nullptr;
    }
    epList->head->prev = nullptr;
    epList->head->next = epList->tail;
    epList->tail->prev = epList->head;
    epList->tail->next = nullptr;
    epList->length = 0;
    return epList;
}

int PollingEpoll::PollingEpollCreate(int epfd)
{
    EpollSocket *epSocket = (EpollSocket *)malloc(sizeof(EpollSocket));
    if (UNLIKELY(epSocket == nullptr)) {
        errno = ENOMEM;
        return -1;
    }
    epSocket->sock.type = SocketType::SOCKET_TYPE_EPOLL;
    epSocket->ep.epfd = epfd;
    epSocket->ep.waitList = EpollListInit();
    epSocket->ep.readyList = EpollListInit();
    if (UNLIKELY(epSocket->ep.waitList == nullptr || epSocket->ep.readyList == nullptr)) {
        EpollListDestroy(epSocket->ep.waitList);
        epSocket->ep.waitList = nullptr;
        EpollListDestroy(epSocket->ep.readyList);
        epSocket->ep.readyList = nullptr;
        FREE_PTR(epSocket);
        errno = ENOMEM;
        return -1;
    }

    g_table[epfd] = &(epSocket->sock);
    return 0;
}

int PollingEpoll::EpollListRemove(EpList *epList, int fd)
{
    if (UNLIKELY(epList == nullptr)) {
        return -1;
    }

    EpListNode *curNode = epList->head->next;
    while (curNode != epList->tail) {
        if (curNode->epItem.fd != fd) {
            curNode = curNode->next;
            continue;
        }
        curNode->prev->next = curNode->next;
        curNode->next->prev = curNode->prev;
        FREE_PTR(curNode);
        epList->length--;
        return 0;
    }
    errno = ENOENT;
    return -1;
}

int PollingEpoll::IsExistInEpollList(EpList *epList, int fd)
{
    if (UNLIKELY(epList == nullptr)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Epoll list is null. \n");
        return -1;
    }

    EpListNode *curNode = epList->head->next;
    while (curNode != epList->tail) {
        if (curNode->epItem.fd == fd) {
            return 0;
        }
        curNode = curNode->next;
    }
    return -1;
}

void PollingEpoll::EpollListModify(EpList *epList, int fd, uint32_t epEvent)
{
    if (UNLIKELY(epList == nullptr)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Epoll list is null. \n");
        return;
    }

    EpListNode *curNode = epList->head->next;
    while (curNode != epList->tail) {
        if (curNode->epItem.fd != fd) {
            curNode = curNode->next;
            continue;
        }
        curNode->epItem.event.events |= epEvent;
        return;
    }
}

PollingErrCode PollingEpoll::EpollListInsert(EpList *epList, EpItem epItem)
{
    if (UNLIKELY(epList == nullptr)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Epoll list is null. \n");
        return PollingErrCode::ERR;
    }
    EpListNode *newNode = (EpListNode *)malloc(sizeof(EpListNode));
    if (UNLIKELY(newNode == nullptr)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Insert epoll list allocate memory failed. \n");
        errno = ENOMEM;
        return PollingErrCode::ERR;
    }

    newNode->epItem = epItem;
    newNode->prev = epList->tail->prev;
    newNode->next = epList->tail;
    epList->tail->prev->next = newNode;
    epList->tail->prev = newNode;
    epList->length++;
    return PollingErrCode::OK;
}

PollingErrCode PollingEpoll::AddEventIntoRdList(EpList *readyList, EpItem epItem, uint32_t epEvent)
{
    if (IsExistInEpollList(readyList, epItem.fd) == 0) {
        EpollListModify(readyList, epItem.fd, epEvent);
        return PollingErrCode::OK;
    }

    struct epoll_event event;
    event.events = epEvent;
    event.data = epItem.event.data;

    EpItem retEpItem = {epItem.fd, event};
    PollingErrCode rc = EpollListInsert(readyList, retEpItem);
    if (UNLIKELY(rc != PollingErrCode::OK)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to insert event into readyList. fd=%d. \n", epItem.fd);
        return rc;
    }
    return PollingErrCode::OK;
}

PollingErrCode PollingEpoll::UmqPoll(uint64_t umqHandle)
{
    uint32_t pollBatchMax = 32;
    umq_buf_t **buf = new umq_buf_t *[pollBatchMax];
    int poll_num = umq_poll(umqHandle, UMQ_IO_RX, buf, pollBatchMax);
    if (poll_num < 0) {
        delete[] buf;
        return PollingErrCode::ERR;
    }

    if (poll_num == 0) {
        delete[] buf;
        return PollingErrCode::NOT_READY;
    }

    AddQbuf(umqHandle, buf, poll_num);
    return PollingErrCode::OK;
}

PollingErrCode PollingEpoll::IsUmqReadable(int fd)
{
    Socket *socket = g_table[fd];
    if (socket == nullptr || socket->umqHandle == 0) {
        return PollingErrCode::ERR;
    }

    return UmqPoll(socket->umqHandle);
}

PollingErrCode PollingEpoll::IsUmqWriteable()
{
    return PollingErrCode::ERR;
}

PollingErrCode PollingEpoll::EpInEventProcess(EventPoll *eventPoll, EpItem epItem)
{
#ifdef UBS_SHM_BUILD_ENABLED
    SocketFd *obj = Fd<SocketFd>::GetFdObj(epItem.fd);
    PollingErrCode rc = PollingErrCode::OK;
    if (obj != nullptr) {
        while (1) {
            rc = obj->IsShmReadable(epItem.event.events);
            if (rc == PollingErrCode::OK) {
                break;
            }
            if (rc == PollingErrCode::NOT_READY) {
                RPC_ADPT_VLOG_WARN("Polling stop, rc %d.\n", static_cast<int>(rc));
                break;
            }
        }
    }
#else
    PollingErrCode rc = IsUmqReadable(epItem.fd);
#endif
    if (rc != PollingErrCode::OK) {
        return rc;
    }

    return AddEventIntoRdList(eventPoll->readyList, epItem, EPOLLIN);
}

PollingErrCode PollingEpoll::EpOutEventProcess(EventPoll *eventPoll, EpItem epItem)
{
#ifdef UBS_SHM_BUILD_ENABLED
    SocketFd *obj = Fd<SocketFd>::GetFdObj(epItem.fd);
    PollingErrCode rc = PollingErrCode::OK;
    if (obj != nullptr) {
        rc = obj->IsShmWriteable(epItem.event.events);
        RPC_ADPT_VLOG_WARN(
            "[DEBUG] EPOUT event, writeable %d, epfd=%d, fd=%d.\n", rc, eventPoll->epfd, epItem.fd);
    }
#else
    PollingErrCode rc = IsUmqWriteable();
#endif
    if (rc != PollingErrCode::OK) {
        return rc;
    }

    return AddEventIntoRdList(eventPoll->readyList, epItem, EPOLLOUT);
}

void PollingEpoll::EpollProcess(EventPoll *eventPoll)
{
    EpListNode *curNode = eventPoll->waitList->head->next;
    EpListNode *nextNode = nullptr;
    for (; curNode != eventPoll->waitList->tail; curNode = nextNode) {
        nextNode = curNode->next;
        int fd = curNode->epItem.fd;
        Socket *socket = g_table[fd];
        if (socket == nullptr) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Invalid fd in epoll wait list, epfd=%d, fd=%d.\n", eventPoll->epfd,
                              fd);
            EpollListRemove(eventPoll->waitList, fd);
            continue;
        }

        if (socket->type != SocketType::SOCKET_TYPE_TCP_SERVER && socket->type != SocketType::SOCKET_TYPE_TCP_CLIENT) {
            continue;
        }

        if (curNode->epItem.event.events & EPOLLIN) {
            if (EpInEventProcess(eventPoll, curNode->epItem) != PollingErrCode::OK) {
                EpollListRemove(eventPoll->waitList, fd);
                continue;
            }
        }
        if (curNode->epItem.event.events & EPOLLOUT) {
            if (EpOutEventProcess(eventPoll, curNode->epItem) != PollingErrCode::OK) {
                EpollListRemove(eventPoll->waitList, fd);
                continue;
            }
        }
    }
}

void PollingEpoll::EpollEventProcess(EventPoll *eventPoll, struct epoll_event *events, int maxevents, int *rdCnt)
{
    EpollProcess(eventPoll);
    uint32_t readyNum = eventPoll->readyList->length;
    readyNum = readyNum > static_cast<uint32_t>(maxevents) ? static_cast<uint32_t>(maxevents) : readyNum;
    EpListNode *curNode = eventPoll->readyList->head->next;
    for (uint32_t i = 0; i < readyNum && curNode != eventPoll->readyList->tail; ++i, ++(*rdCnt)) {
        events[i] = curNode->epItem.event;
        EpListNode *delNode = curNode;
        curNode = curNode->next;
        EpollListRemove(eventPoll->readyList, delNode->epItem.fd);
    }

    if (*rdCnt == maxevents) {
        return;
    }
    int rc = OsAPiMgr::GetOriginApi()->epoll_wait(eventPoll->epfd, events + *rdCnt, maxevents - *rdCnt, 0);
    if (UNLIKELY(rc == -1)) {
        *rdCnt = -1;
        return;
    }
    *rdCnt += rc;
}

int PollingEpoll::PollingEpollWait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    EpollSocket *epSocket = GET_EPOLL_SOCKET(g_table[epfd]);
    EventPoll *eventPoll = &epSocket->ep;

    int rdCnt = 0;
    if (timeout == -1) {
        while (1) {
            EpollEventProcess(eventPoll, events, maxevents, &rdCnt);
            if (rdCnt != 0) {
                return rdCnt;
            }
        }
    } else {
        uint64_t timeoutStamp = GetCurNanoSeconds() + static_cast<uint64_t>(timeout) * MSEC_TO_NSEC;
        do {
            EpollEventProcess(eventPoll, events, maxevents, &rdCnt);
        } while (rdCnt == 0 && timeoutStamp > GetCurNanoSeconds());
    }
    return rdCnt;
}

int PollingEpoll::SocketCreate(Socket **out, int fd, SocketType type, uint64_t umqHandle)
{
    if (UNLIKELY(out == nullptr)) {
        return -1;
    }

    Socket *sock = (Socket*)calloc(1, sizeof(Socket));
    if (UNLIKELY(sock == nullptr)) {
        return -1;
    }

    sock->umqHandle = umqHandle;
    sock->type = type;
    sock->fd = fd;
    *out = sock;
    return 0;
}

int PollingEpoll::EpollListReplace(EpList *epList, EpItem epItem)
{
    if (UNLIKELY(epList == nullptr)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Epoll list is null. \n");
        return -1;
    }

    EpListNode *curNode = epList->head->next;
    while (curNode != epList->tail) {
        if (curNode->epItem.fd != epItem.fd) {
            curNode = curNode->next;
            continue;
        }
        curNode->epItem = epItem;
        return 0;
    }
    errno = ENOENT;
    return -1;
}

int PollingEpoll::EpollCtl(int epfd, int op, int fd, struct epoll_event *event)
{
    EpollSocket *epSocket = GET_EPOLL_SOCKET(g_table[epfd]);
    EventPoll *eventPoll = &epSocket->ep;
    switch (op) {
        case EPOLL_CTL_ADD: {
            EpItem epItem = {.fd = fd, .event = *event};
            if (IsExistInEpollList(eventPoll->waitList, epItem.fd) == 0) {
                errno = EEXIST;
                return -1;
            }
            PollingErrCode rc = EpollListInsert(eventPoll->waitList, epItem);
            if (UNLIKELY(rc != PollingErrCode::OK)) {
                return -1;
            }
            return 0;
        }
        case EPOLL_CTL_DEL: {
            return EpollListRemove(eventPoll->waitList, fd);
        }
        case EPOLL_CTL_MOD: {
            EpItem epItem = {.fd = fd, .event = *event};
            int rc = EpollListReplace(eventPoll->waitList, epItem);
            if (UNLIKELY(rc != 0)) {
                return rc;
            }
            return 0;
        }
        default:
            errno = EINVAL;
            return -1;
    }
    return 0;
}