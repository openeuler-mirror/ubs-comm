/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *Description: Provide the utility for umq buffer, iov, etc
 *Author:
 *Create: 2025-07-31
 *Note:
 *History: 2025-07-31
*/

#ifndef FILE_DESCRIPTOR_H
#define FILE_DESCRIPTOR_H

#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <sstream>
#include <unordered_map>
#include <chrono>

#include "rpc_adpt_vlog.h"
#include "socket_adapter.h"
#include "polling_epoll.h"

#define RPC_ADPT_FD_MAX      (8192)

class ScopedReadLock{
    public:
    ScopedReadLock(pthread_rwlock_t &lock) : m_rwlock(lock)
    {
        (void)pthread_rwlock_rdlock(&m_rwlock);
    }

    ~ScopedReadLock()
    {
        (void)pthread_rwlock_unlock(&m_rwlock);
    }

    private:
    pthread_rwlock_t &m_rwlock;
};

class ScopedWriteLock{
    public:
    ScopedWriteLock(pthread_rwlock_t &lock) : m_rwlock(lock)
    {
        (void)pthread_rwlock_wrlock(&m_rwlock);
    }

    ~ScopedWriteLock()
    {
        (void)pthread_rwlock_unlock(&m_rwlock);
    }

    private:
    pthread_rwlock_t &m_rwlock;
};

template <typename FdType>
class Fd{
    public:
    Fd(int fd) : m_fd(fd) {}

    virtual ~Fd() {}

    /* This is an interface used for the data plane, and the caller can ensure that no
     * concurrency issues will arise, thus eliminating the need for locking. */
     ALWAYS_INLINE static FdType *GetFdObj(int fd)
     {
        if(fd<0 || fd>=RPC_ADPT_FD_MAX){
            return nullptr;
        }

        return m_fd_obj_map[fd];
     }

     /* This is locked version of the query interface, primarily used in scenarios where
      * concurrent access cannot be guaranteed, such as when querying statistical information.*/
    ALWAYS_INLINE static pthread_rwlock_t &GetRWLock()
    {
        return m_rwlock;
    }
    
    ALWAYS_INLINE static FdType **GetFdObjMap()
    {
        return m_fd_obj_map;
    }

    ALWAYS_INLINE static void OverrideFdObj(int fd, FdType *new_obj)
    {
        if(fd < 0 || fd >= RPC_ADPT_FD_MAX){
            return;
        }

        FdType *old_obj;
        {
            ScopedWriteLock lock(m_rwlock);
            old_obj = m_fd_obj_map[fd];
            m_fd_obj_map[fd] = new_obj;
        }
        if(old_obj !=nullptr){
            delete old_obj;
        }
    }

    ALWAYS_INLINE static void ReleaseAllFdObj(void)
    {
        ScopedWriteLock lock(m_rwlock);
        for(uint32_t i = 0; i < RPC_ADPT_FD_MAX; ++i){
            FdType *obj = m_fd_obj_map[i];
            m_fd_obj_map[i] = nullptr;
            if(obj !=nullptr){
                delete obj;
            }
        }
    }

    ALWAYS_INLINE int GetFd(void)
    {
        return m_fd;
    }

    protected:
    int m_fd;

    private:
    static pthread_rwlock_t m_rwlock;
    static FdType *m_fd_obj_map[RPC_ADPT_FD_MAX];
};

template <typename TimePoint>
static ALWAYS_INLINE bool IsTimeout(TimePoint &start, uint32_t timeout_ms)
{
    TimePoint end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end-start);
    return duration.count()>timeout_ms;
}

class SocketFd : public Fd<SocketFd> {
    public:
    SocketFd(int fd) : Fd<SocketFd>(fd) {}

    virtual ~SocketFd() {}

    static const uint32_t FLUSH_SOCKET_MSG_BUFFER_LEN = 1024;

    static ALWAYS_INLINE bool IsBlocking(int fd) {
        const int flags = OsAPiMgr::GetOriginApi()->fcntl(fd, F_GETFL, 0);
        return flags >= 0 && !(flags & O_NONBLOCK);
    }

    static ALWAYS_INLINE int SetNonBlocking(int fd){
        const int flags = OsAPiMgr::GetOriginApi()->fcntl(fd, F_GETFL, 0);
        if(flags < 0){
            return flags;
        }

        if(flags & O_NONBLOCK){
            return 0;
        }

        return OsAPiMgr::GetOriginApi()->fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    static ALWAYS_INLINE int SetBlocking(int fd){
        const int flags = OsAPiMgr::GetOriginApi()->fcntl(fd, F_GETFL, 0);
        if(flags < 0){
            return flags;
        }

        if(flags & O_NONBLOCK){
            return OsAPiMgr::GetOriginApi()->fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
        }

        return 0;    
    }

    static ALWAYS_INLINE ssize_t SendSocketData(int fd, const void *buf,size_t size, uint32_t timeout_ms)
    {
        errno = 0;
        char *cur = (char *)(uintptr_t)buf;
        ssize_t sent = 0;
        size_t total = size;
        auto start = std::chrono::high_resolution_clock::now();
        while (total != 0){
            if(IsTimeout(start, timeout_ms)){
                return sent;
            }

            sent = OsAPiMgr::GetOriginApi()->send(fd, cur, total, MSG_NOSIGNAL);
            if(errno == EAGAIN){
                // reset errno to 0
                errno = 0;
                if(sent > 0){
                    total -= sent;
                    cur += sent;
                }

                continue;
            }

            if(sent<=0 || errno != 0){
                RPC_ADPT_VLOG_ERR("Failed to send socket message: %s, sent = %u.\n",
                    strerror(errno), sent);
                return sent;
            }else {
                RPC_ADPT_VLOG_DEBUG("Send socket message successful, fd: %d, sent = %u, total: %d\n",
                    fd, sent, size);
            }
            total -= sent;
            cur += sent;
        }

        return size;
    }

    static ALWAYS_INLINE ssize_t RecvSocketData(int fd, const void *buf,size_t size, uint32_t timeout_ms)
    {
        // reset errno to 0
        errno = 0;
        char *cur = (char *)(uintptr_t)buf;
        ssize_t received = 0;
        size_t total = size;
        auto start = std::chrono::high_resolution_clock::now();
        while (total != 0){
            if(IsTimeout(start, timeout_ms)){
                return received;
            }

            received = OsAPiMgr::GetOriginApi()->recv(fd, cur, total, MSG_NOSIGNAL);
            if(errno == EAGAIN){
                // reset errno to 0
                errno = 0;
                if(received > 0){
                    total -= received;
                    cur += received;
                }

                continue;
            }

            if(received<=0 || errno != 0){
               RPC_ADPT_VLOG_ERR("Failed to receive socket message: %s, received: %u, fd: %d.\n",
                   strerror(errno), received, fd);
                return received;
            }else {
                RPC_ADPT_VLOG_DEBUG("Receive socket message successful, fd: %d, received: %u, total: %d\n",
                    fd, received, size);
            }
            total -= received;
            cur += received;
        }

        return size;
    }

    static ALWAYS_INLINE void FlushSocketMsg(int fd)
    {
        //reset errno to 0
        errno = 0;
        char tmp_buf[FLUSH_SOCKET_MSG_BUFFER_LEN];
        ssize_t received = 0;
        do{
            received = OsAPiMgr::GetOriginApi()->recv(fd, tmp_buf, FLUSH_SOCKET_MSG_BUFFER_LEN, MSG_NOSIGNAL);
            if (errno == EAGAIN || errno == EINTR){
                // reset errno to 0
                errno = 0;
                continue;
            }

            if(received < 0 || errno != 0){
                return;
            }
        }while (received > 0);
    }

    virtual void OutputStats(std::ostringstream &oss) = 0;
};

class EpollEvent {
    public:
    EpollEvent(int fd, struct epoll_event *event) : m_fd(fd), m_event(*event) {}

    virtual ~EpollEvent() {}

    virtual int AddEpollEvent(int epoll_fd, bool use_polling = false)
    {
        struct epoll_event tmp_event;
        tmp_event.events = m_event.events;
        tmp_event.data.ptr = (void *)(uintptr_t)this;

        int ret = OsAPiMgr::GetOriginApi()->epoll_ctl(epoll_fd, EPOLL_CTL_ADD, m_fd, &tmp_event);
        if(ret != 0){
            RPC_ADPT_VLOG_ERR("Origin epoll control add failed, epfd: %d, fd: %d\n",epoll_fd, m_fd);
            return ret;
        }

        RPC_ADPT_VLOG_DEBUG("Origin epoll control add successful, epfd: %d, fd: %d\n",epoll_fd, m_fd);
        m_add_epoll_event = true;

        if (use_polling) {
            PollingEpoll::GetInstance().EpollCtl(epoll_fd, EPOLL_CTL_ADD, m_fd, &tmp_event);
        }

        return 0;
    }

    virtual int ModEpollEvent(int epoll_fd, struct epoll_event *event, bool use_polling = false)
    {
        struct epoll_event tmp_event;
        tmp_event.events = event->events;
        tmp_event.data.ptr = (void *)(uintptr_t)this;

        int ret = OsAPiMgr::GetOriginApi()->epoll_ctl(epoll_fd, EPOLL_CTL_MOD, m_fd, &tmp_event);
        if(ret != 0){
            RPC_ADPT_VLOG_ERR("Origin epoll control modify failed, epfd: %d, fd: %d\n",epoll_fd, m_fd);
            return ret;
        }

        RPC_ADPT_VLOG_DEBUG("Origin epoll control modify successful, epfd: %d, fd: %d, old events: %u,"
            " new events: %u\n",epoll_fd, m_fd, m_event.events, event->events);
        m_event = *event;

        if (use_polling) {
            PollingEpoll::GetInstance().EpollCtl(epoll_fd, EPOLL_CTL_MOD, m_fd, &tmp_event);
        }

        return 0;
    }

    virtual int DelEpollEvent(int epoll_fd, bool use_polling = false)
    {
        int ret = OsAPiMgr::GetOriginApi()->epoll_ctl(epoll_fd, EPOLL_CTL_DEL, m_fd, nullptr);
        if(ret != 0){
            RPC_ADPT_VLOG_ERR("Origin epoll control delete failed, epfd: %d, fd: %d\n",epoll_fd, m_fd);
            return ret;
        }

        RPC_ADPT_VLOG_DEBUG("Origin epoll control delete successful, epfd: %d, fd: %d\n",epoll_fd, m_fd);
        m_add_epoll_event = false;

        if (use_polling) {
            PollingEpoll::GetInstance().EpollCtl(epoll_fd, EPOLL_CTL_DEL, m_fd, nullptr);
        }

        return 0;
    }

    virtual ALWAYS_INLINE int ProcessEpollEvent(struct epoll_event *input_event, struct epoll_event *output_event,
                                                bool use_polling = false)
    {
        output_event->events = input_event->events;
        output_event->data = m_event.data;
        return 1;
    }

    bool IsAddEpollEvent()
    {
        return m_add_epoll_event;
    }

    int GetFd()
    {
        return m_fd;
    }

    uint32_t GetEvents()
    {
        return m_event.events;
    }

    void *GetData()
    {
        return m_event.data.ptr;
    }

protected:
    int m_fd;
    struct epoll_event m_event;
    bool m_add_epoll_event = false;
};

class EpollFd : public Fd<EpollFd> {
    public:
    EpollFd(int epoll_fd) : Fd<EpollFd>(epoll_fd) {}

    ~EpollFd()
    {
        for (std::pair<int, EpollEvent *> kv : m_epoll_event_map) {
            delete kv.second;
        }
    }

    virtual ALWAYS_INLINE int EpollCtlAdd(int fd, struct epoll_event *event, bool use_polling = false)
    {
        if (event == nullptr) {
            RPC_ADPT_VLOG_ERR("Invalid argument, epoll event is NULL\n");
            errno = EINVAL;
            return -1;
        }

        if (m_epoll_event_map.count(fd) > 0) {
            RPC_ADPT_VLOG_ERR("Origin epoll control add duplicated, epfd: %d, fd: %d\n", m_fd, fd);
            errno = EEXIST;
            return -1;
        }

        EpollEvent *epoll_event = nullptr;
        try {
            epoll_event = new EpollEvent(fd, event);
        } catch (std::exception& e){
            RPC_ADPT_VLOG_ERR("%s\n", e.what());
            return -1;
        }

        if (epoll_event->AddEpollEvent(m_fd, use_polling)) {
            delete epoll_event;
            return -1;
        }

        m_epoll_event_map[fd] = epoll_event;

        return 0;
    }

    virtual ALWAYS_INLINE int EpollCtlMod(int fd, struct epoll_event *event, bool use_polling = false)
    {
        if(event == nullptr){
            RPC_ADPT_VLOG_ERR("Invalid argument, epoll event is NULL\n");
            errno = EINVAL;
            return -1;
        }

        if(m_epoll_event_map.count(fd)==0){
            RPC_ADPT_VLOG_ERR("Origin epoll control modify not exist, epfd: %d, fd: %d\n", m_fd, fd);
            return -1;
        }

        EpollEvent *epoll_event = m_epoll_event_map[fd];
        if (epoll_event->ModEpollEvent(m_fd, event, use_polling) !=0) {
            return -1;
        }

        return 0;
    }

    virtual ALWAYS_INLINE int EpollCtlDel(int fd, struct epoll_event *event, bool use_polling = false)
    {
        if (m_epoll_event_map.count(fd) == 0) {
            RPC_ADPT_VLOG_ERR("Origin epoll control delete not exist, epfd: %d, fd: %d\n", m_fd, fd);
            return -1;
        }

        EpollEvent *epoll_event = m_epoll_event_map[fd];
        if (epoll_event->DelEpollEvent(m_fd, use_polling) != 0) {
            return -1;
        }

        delete epoll_event;
        m_epoll_event_map.erase(fd);

        return 0;
    }

    virtual ALWAYS_INLINE int EpollCtl(int op, int fd, struct epoll_event *event, bool use_polling = false)
    {
        int ret = -1;
        switch (op){
            case EPOLL_CTL_ADD:
                ret = EpollCtlAdd(fd, event, use_polling);
                break;
            case EPOLL_CTL_MOD:
                ret = EpollCtlMod(fd, event, use_polling);
                break;
            case EPOLL_CTL_DEL:
                ret = EpollCtlDel(fd, event, use_polling);
                break; 
            default:
                RPC_ADPT_VLOG_ERR("Invalid op code(%d), epfd: %d, fd: %d\n", op, m_fd, fd);
                errno = EINVAL; 
        }

        return ret;
    }

    virtual ALWAYS_INLINE int EpollWait(struct epoll_event *events, int maxevents, int timeout,
                                        bool use_polling = false)
    {
        struct epoll_event events_[maxevents];
        int ev_num = use_polling ? PollingEpoll::GetInstance().PollingEpollWait(m_fd, events_, maxevents, timeout) :
                                   OsAPiMgr::GetOriginApi()->epoll_wait(m_fd, events_, maxevents, timeout);
        if (ev_num == -1) {
            return ev_num;
        }

        int output_idx = 0;
        for(int i = 0; i<ev_num;i++){
            EpollEvent *epoll_event = (EpollEvent *)events_[i].data.ptr;
            output_idx += epoll_event->ProcessEpollEvent(events_ + i, events + output_idx, use_polling);
        }

        return output_idx;
    }

    protected:
    std::unordered_map<int, EpollEvent *> m_epoll_event_map;
};

#endif