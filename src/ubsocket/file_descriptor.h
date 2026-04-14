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
#include <sys/eventfd.h>
#include <pthread.h>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <mutex>

#include "rpc_adpt_vlog.h"
#include "socket_adapter.h"
#include "polling_epoll.h"
#include "cli_message.h"
#include "net_common.h"
#include "ub_lock_ops.h"

#define RPC_ADPT_FD_MAX      (8192)

class SocketEpollMapper {
public:
    explicit SocketEpollMapper(int fd) : m_fd(fd) {}

    ~SocketEpollMapper() {}

    void Add(int epoll_fd)
    {
        m_epoll_set.insert(epoll_fd);
    }

    void Del(int epoll_fd)
    {
        m_epoll_set.erase(epoll_fd);
    }

    void Clear();
private:
    int m_fd;
    std::unordered_set<int> m_epoll_set;
};

extern std::unordered_map<int, SocketEpollMapper *> g_socket_epoll_mappers;

extern u_rw_lock_t* g_socket_epoll_lock;

SocketEpollMapper* GetSocketEpollMapper(int socket_fd);

bool CreateSocketEpollMapper(int socket_fd, SocketEpollMapper*& mapper);

void CleanSocketEpollMapper(int socket_fd);

template <typename FdType>
class Fd{
    public:
    Fd(int fd) : m_fd(fd) {}

    virtual ~Fd() {}

    ALWAYS_INLINE static int GlobalFdInit()
    {
        m_rwlock = g_rw_lock_ops.create();
        return m_rwlock != nullptr ? 0 : -1;
    }

    /* This is an interface used for the data plane, and the caller can ensure that no
     * concurrency issues will arise, thus eliminating the need for locking. */

     /* This is locked version of the query interface, primarily used in scenarios where
      * concurrent access cannot be guaranteed, such as when querying statistical information.*/
    ALWAYS_INLINE static u_rw_lock_t *GetRWLock()
    {
        return m_rwlock;
    }

    ALWAYS_INLINE static FdType *GetFdObj(int fd)
    {
        if(fd<0 || fd>=RPC_ADPT_FD_MAX){
            return nullptr;
        }
        return m_fd_obj_map[fd];
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
            ScopedUbWriteLocker lock(m_rwlock);
            old_obj = m_fd_obj_map[fd];
            m_fd_obj_map[fd] = new_obj;
        }
        if(old_obj !=nullptr){
            delete old_obj;
        }
    }

    ALWAYS_INLINE static void ReleaseAllFdObj(void)
    {
        ScopedUbWriteLocker lock(m_rwlock);
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
    static u_rw_lock_t* m_rwlock;
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
        char errnoBuf[NET_STR_ERROR_BUF_SIZE] = {0};
        while (total != 0){
            if(IsTimeout(start, timeout_ms)){
                errno = ETIMEDOUT;
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
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to send socket message: %s, sent = %u.\n",
                    NetCommon::NN_GetStrError(errno, errnoBuf, NET_STR_ERROR_BUF_SIZE), sent);
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
        char errnoBuf[NET_STR_ERROR_BUF_SIZE] = {0};
        while (total != 0){
            if(IsTimeout(start, timeout_ms)){
                errno = ETIMEDOUT;
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

            if (received <= 0 || errno != 0){
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,  "Failed to receive socket message: %s, received: %u, fd: %d.\n",
                    NetCommon::NN_GetStrError(errno, errnoBuf, NET_STR_ERROR_BUF_SIZE), received, fd);
                return received;
            } else {
                RPC_ADPT_VLOG_DEBUG(
                    "Receive socket message successful, fd: %d, received: %u, total: %d\n",
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
    virtual void GetSocketCLIData(Statistics::CLISocketData *data) = 0;

    virtual PollingErrCode IsShmReadable(uint32_t event)
    {
        return PollingErrCode::OK;
    }

    virtual PollingErrCode IsShmWriteable(uint32_t event)
    {
        return PollingErrCode::OK;
    }
};

enum class FdType : uint32_t {
    SOCKET_FD = 0,
    EVENT_FD = 1,
    SHARE_JFR_FD = 2,
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
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Origin epoll control add failed, epfd: %d, fd: %d\n", epoll_fd,
                              m_fd);
            return ret;
        }

        RPC_ADPT_VLOG_DEBUG("Origin epoll control add successful, epfd: %d, fd: %d\n", epoll_fd, m_fd);
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
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Origin epoll control modify failed, epfd: %d, fd: %d\n", epoll_fd,
                              m_fd);
            return ret;
        }

        RPC_ADPT_VLOG_DEBUG("Origin epoll control modify successful, epfd: %d, fd: %d, old events: %u,"
                            " new events: %u\n",
                            epoll_fd, m_fd, m_event.events, event->events);
        m_event = *event;

        if (use_polling) {
            PollingEpoll::GetInstance().EpollCtl(epoll_fd, EPOLL_CTL_MOD, m_fd, &tmp_event);
        }

        return 0;
    }

    virtual int DelEpollEvent(int epoll_fd, bool use_polling = false)
    {
        int ret = OsAPiMgr::GetOriginApi()->epoll_ctl(epoll_fd, EPOLL_CTL_DEL, m_fd, nullptr);
        if (ret != 0) {
            if (errno != EBADF) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Origin epoll control delete failed, "
                    "epfd: %d, fd: %d, errno: %d\n", epoll_fd, m_fd, errno);
                return ret;
            }
            RPC_ADPT_VLOG_WARN("Origin epoll control error for bad file descriptor, "
                "epfd: %d, fd: %d\n", epoll_fd, m_fd);
        }

        RPC_ADPT_VLOG_DEBUG("Origin epoll control delete successful, epfd: %d, fd: %d\n", epoll_fd, m_fd);
        m_add_epoll_event = false;

        if (use_polling) {
            PollingEpoll::GetInstance().EpollCtl(epoll_fd, EPOLL_CTL_DEL, m_fd, nullptr);
        }

        return 0;
    }

    virtual ALWAYS_INLINE int ProcessEpollEvent(struct epoll_event *input_event, struct epoll_event *output_event,
                                                int maxevents, bool use_polling = false)
    {
        output_event->events = input_event->events;
        output_event->data = m_event.data;
        return 1;
    }

    ALWAYS_INLINE int ProcessEpollEvent(struct epoll_event *output_event)
    {
        output_event->events = m_event.events;
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

    FdType GetFdType()
    {
        return m_fd_type;
    }

protected:
    int m_fd;
    struct epoll_event m_event;
    bool m_add_epoll_event = false;
    FdType m_fd_type = FdType::SOCKET_FD;
};

class DelEventFdEpollEvent : public ::EpollEvent {
public:
    DelEventFdEpollEvent(int event_fd, struct epoll_event *event) : EpollEvent(event_fd, event)
    {
        m_fd_type = FdType::EVENT_FD;
    }

    virtual ~DelEventFdEpollEvent()
    {
        if (m_fd >= 0) {
            OsAPiMgr::GetOriginApi()->close(m_fd);
            m_fd = -1;
        }
    }

    int AddEpollEvent(int epoll_fd)
    {
        struct epoll_event tmp_event;
        tmp_event.events = EPOLLIN;
        tmp_event.data.ptr = (void *)(uintptr_t)this;

        int ret = OsAPiMgr::GetOriginApi()->epoll_ctl(epoll_fd, EPOLL_CTL_ADD, m_fd, &tmp_event);
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Add delete event fd failed, epfd: %d, delete event fd: %d\n",
                              epoll_fd, m_fd);
            return ret;
        }

        RPC_ADPT_VLOG_DEBUG("Add delete event fd successful, epfd: %d, delete event fd: %d\n", epoll_fd, m_fd);
        m_add_epoll_event = true;

        return 0;
    }

    void WakeUp()
    {
        uint64_t notification = 1;
        if (eventfd_write(m_fd, notification) < 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Wakeup delete event fd: %d failed.\n", m_fd);
        }
    }

    ALWAYS_INLINE int ProcessEpollEvent(struct epoll_event *input_event, struct epoll_event *output_event,
                                        int maxevents, bool use_polling = false)
    {
        uint64_t cnt;
        if (eventfd_read(m_fd, &cnt) == -1) {
            RPC_ADPT_VLOG_WARN("read delete event fd %d failed, errno %d\n", m_fd, errno);
        }

        return 0;
    }
};

class EpollFd : public Fd<EpollFd> {
    public:
    EpollFd(int epoll_fd) : Fd<EpollFd>(epoll_fd)
    {
        m_mutex = g_external_lock_ops.create(LT_EXCLUSIVE);
        m_ctl_mutex = g_external_lock_ops.create(LT_EXCLUSIVE);
        m_del_event_mutex = g_external_lock_ops.create(LT_EXCLUSIVE);

        CreateAndAddDelEventFd();
    }

    ~EpollFd()
    {
        for (std::pair<int, EpollEvent *> kv : m_epoll_event_map) {
            delete kv.second;
        }
        g_external_lock_ops.destroy(m_ctl_mutex);
        g_external_lock_ops.destroy(m_mutex);
        g_external_lock_ops.destroy(m_del_event_mutex);

        if (m_del_event_fd_epoll_event != nullptr) {
            delete m_del_event_fd_epoll_event;
            m_del_event_fd_epoll_event = nullptr;
        }

        ScopedUbExclusiveLocker sLock(m_del_event_mutex);
        for (std::pair<int, EpollEvent *> kv : m_delete_event_map) {
            delete kv.second;
        }
        m_delete_event_map.clear();
    }

    virtual ALWAYS_INLINE int EpollCtlAdd(int fd, struct epoll_event *event, bool use_polling = false)
    {
        if (event == nullptr) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Invalid argument, epoll event is NULL\n");
            errno = EINVAL;
            return -1;
        }
        ScopedUbExclusiveLocker sLock(m_mutex);
        if (m_epoll_event_map.count(fd) > 0) {
            RPC_ADPT_VLOG_WARN("Origin epoll control add duplicated, epfd: %d, fd: %d\n", m_fd, fd);
            EpollEvent *epoll_event = m_epoll_event_map[fd];
            if (epoll_event->DelEpollEvent(m_fd) != 0)
            {
                RPC_ADPT_VLOG_WARN("Deleting duplicated fd: %d from epfd: %d is unsuccessful\n", fd, m_fd);
            }
            delete epoll_event;
            m_epoll_event_map.erase(fd);
        }

        EpollEvent *epoll_event = nullptr;
        try {
            epoll_event = new EpollEvent(fd, event);
        } catch (std::exception& e){
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "%s\n", e.what());
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
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Invalid argument, epoll event is NULL\n");
            errno = EINVAL;
            return -1;
        }
        ScopedUbExclusiveLocker sLock(m_mutex);
        if (m_epoll_event_map.count(fd) == 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Origin epoll control modify not exist, epfd: %d, fd: %d\n", m_fd,
                              fd);
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
        ScopedUbExclusiveLocker sLock(m_mutex);
        auto iter = m_epoll_event_map.find(fd);
        if (iter == m_epoll_event_map.end()) {
            RPC_ADPT_VLOG_DEBUG("Origin epoll control delete not exist, epfd: %d, fd: %d\n", m_fd, fd);
            return 0;
        }

        EpollEvent *epoll_event = iter->second;
        if (epoll_event->DelEpollEvent(m_fd, use_polling) != 0) {
            return -1;
        }

        m_epoll_event_map.erase(fd);
        // 1. Add EpollEvent to m_delete_event_map.
        // 2. epoll_wait is triggered through the event fd, where the actual deletion operation is performed.
        // This is done to resolve the timing issue when accessing EpollEvent during the epoll_wait.
        AddDelEpollEvent(fd, epoll_event);
        WakeUpDelEventFd();

        return 0;
    }

    virtual ALWAYS_INLINE int EpollCtl(int op, int fd, struct epoll_event *event, bool use_polling = false)
    {
        int ret = -1;
        bool mapper_create = false;
        SocketEpollMapper* mapper = nullptr;
        ScopedUbExclusiveLocker sLock(m_ctl_mutex);
        if (op == EPOLL_CTL_ADD) {
            mapper_create = CreateSocketEpollMapper(fd, mapper);
        } else {
            mapper = GetSocketEpollMapper(fd);
        }
        switch (op){
            case EPOLL_CTL_ADD:
                ret = EpollCtlAdd(fd, event, use_polling);
                if (ret == 0 && mapper != nullptr) {
                    mapper->Add(m_fd);
                } else if (mapper_create) {
                    ScopedUbWriteLocker s_lock(g_socket_epoll_lock);
                    g_socket_epoll_mappers.erase(fd);
                    free(mapper);
                    mapper = nullptr;
                }
                break;
            case EPOLL_CTL_MOD:
                ret = EpollCtlMod(fd, event, use_polling);
                break;
            case EPOLL_CTL_DEL:
                ret = EpollCtlDel(fd, event, use_polling);
                if (ret == 0 && mapper != nullptr) {
                    mapper->Del(m_fd);
                }
                break; 
            default:
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Invalid op code(%d), epfd: %d, fd: %d\n", op, m_fd, fd);
                errno = EINVAL; 
        }
        return ret;
    }

    virtual ALWAYS_INLINE int EpollWait(struct epoll_event *events, int maxevents, int timeout,
                                        bool use_polling = false)
    {
        if (maxevents <= 0) {
            return -1;
        }

        std::vector<struct epoll_event> events_vec(maxevents);
        struct epoll_event* events_ptr = events_vec.data();

        int ev_num = use_polling ? PollingEpoll::GetInstance().PollingEpollWait(m_fd, events_ptr, maxevents, timeout) :
                                   OsAPiMgr::GetOriginApi()->epoll_wait(m_fd, events_ptr, maxevents, timeout);
        if (ev_num == -1) {
            return ev_num;
        }

        std::map<EpollEvent*, int> share_jfr_events;
        int output_idx = 0;
        bool clear_flag = false;
        for(int i = 0; i < ev_num; i++){
            EpollEvent *epoll_event = (EpollEvent *)events_ptr[i].data.ptr;
            if (epoll_event->GetFdType() == FdType::SHARE_JFR_FD) {
                share_jfr_events[epoll_event] = i;
                continue;
            }

            if (epoll_event->GetFdType() == FdType::SOCKET_FD) {
                // If socket fd, and has been removed from epoll, skip
                if (!epoll_event->IsAddEpollEvent()) {
                    continue;
                }

                output_idx += epoll_event->ProcessEpollEvent(events_ptr + i, events + output_idx, maxevents,
                                                             use_polling);
            } else if (epoll_event->GetFdType() == FdType::EVENT_FD) {
                // If event fd, the EpollEvent needs to be deleted.
                // The deletion is first marked, and the actual deletion is performed after the loop ends.
                epoll_event->ProcessEpollEvent(events_ptr + i, events + output_idx, maxevents, use_polling);
                clear_flag = true;
            }
        }

        if (maxevents - output_idx > 0) {
            int share_jfr_num = static_cast<int>(share_jfr_events.size());
            int poll_batch = std::max(1, (maxevents - output_idx) / share_jfr_num);
            for (const auto &pair : share_jfr_events) {
                if (!pair.first->IsAddEpollEvent()) {
                    continue;
                }

                output_idx += pair.first->ProcessEpollEvent(events_ptr + pair.second, events + output_idx, poll_batch,
                                                            use_polling);
            }
        }

        if (clear_flag) {
            ClearDelEpollEvent();
        }

        return output_idx;
    }

    EpollEvent *Find(int fd)
    {
        ScopedUbExclusiveLocker sLock(m_mutex);
        auto iter = m_epoll_event_map.find(fd);
        if (iter == m_epoll_event_map.end()) {
            return nullptr;
        }
        return dynamic_cast<EpollEvent *>(iter->second);
    }

    u_external_mutex_t* GetCtlMutex()
    {
        return m_ctl_mutex;
    }

    protected:
    std::unordered_map<int, EpollEvent *> m_epoll_event_map;
    std::unordered_map<int, EpollEvent *> m_delete_event_map;
    u_external_mutex_t* m_mutex;
    u_external_mutex_t* m_ctl_mutex;
    u_external_mutex_t* m_del_event_mutex;
    DelEventFdEpollEvent *m_del_event_fd_epoll_event = nullptr;

    int CreateAndAddDelEventFd()
    {
        int del_event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        struct epoll_event ev;
        m_del_event_fd_epoll_event = new(std::nothrow) DelEventFdEpollEvent(del_event_fd, &ev);
        if (m_del_event_fd_epoll_event->AddEpollEvent(m_fd)) {
            delete m_del_event_fd_epoll_event;
            m_del_event_fd_epoll_event = nullptr;
            return -1;
        }

        RPC_ADPT_VLOG_DEBUG("create and add delete event fd success. \n");
        return 0;
    }

    void WakeUpDelEventFd()
    {
        if (m_del_event_fd_epoll_event == nullptr) {
            RPC_ADPT_VLOG_WARN("Wake up delete event fd failed. \n");
            return;
        }

        m_del_event_fd_epoll_event->WakeUp();
    }

    void AddDelEpollEvent(int fd, EpollEvent *epoll_event)
    {
        ScopedUbExclusiveLocker sLock(m_del_event_mutex);
        m_delete_event_map[fd] = epoll_event;
    }

    void ClearDelEpollEvent()
    {
        ScopedUbExclusiveLocker sLock(m_del_event_mutex);
        for (std::pair<int, EpollEvent *> kv : m_delete_event_map) {
            delete kv.second;
        }
        m_delete_event_map.clear();
    }
};

#endif
