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
#ifndef UBS_COMM_UBSOCKET_EPOLL_FD_H
#define UBS_COMM_UBSOCKET_EPOLL_FD_H

#include <memory>
#include <atomic>
#include <vector>

#include "common/ubsocket_leaky_singleton.h"
#include "ubsocket_core_types.h"

namespace ock {
namespace ubs {

enum EpollEventType : uint64_t {
    EPOLL_EVENT_RAW_SOCKET = 0,
    EPOLL_EVENT_UB_SOCKET_IN,
    EPOLL_EVENT_JFCE_FD,
    EPOLL_EVENT_BUTT
};

struct EpollEvent {
    EpollEventType event_type;
    int socket_fd = -1;
    struct epoll_event event {};
    EpollEvent *next{nullptr};
    std::atomic<bool> oneshot_disabled{false};  // EPOLLONESHOT: disabled after triggered, cleared on MOD re-arm.
    EpollEvent(EpollEventType type, int socket, const struct epoll_event &evt) noexcept
        : event_type{type},
          socket_fd{socket},
          event{evt}
    {
    }
};

class EpollMapper {
public:
    explicit EpollMapper(int fd) : fd_(fd)
    {
        mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
    }

    ~EpollMapper()
    {
        if (mutex_ != nullptr) {
            LockRegistry::LOCK_OPS.destroy(mutex_);
            mutex_ = nullptr;
        }
    }

    void Add(int epoll_fd)
    {
        Locker sLock(mutex_);
        epoll_set_.insert(epoll_fd);
    }

    bool Del(int epoll_fd)
    {
        Locker sLock(mutex_);
        epoll_set_.erase(epoll_fd);
        return epoll_set_.empty();
    }

    int QueryFirst()
    {
        Locker sLock(mutex_);
        if (epoll_set_.empty()) {
            return -1;
        } else {
            return *epoll_set_.begin();
        }
    }

    /* 快照该 fd 注册过的所有 epoll fd (供 close 清理钩子使用) */
    std::vector<int> GetEpollFds()
    {
        Locker sLock(mutex_);
        return std::vector<int>(epoll_set_.begin(), epoll_set_.end());
    }

    void Clear() {}

private:
    const int fd_;
    u_mutex_t *mutex_ = nullptr;
    std::unordered_set<int> epoll_set_;
};

/* 注册表(g_socket_epoll_mappers)持有的 shared_ptr 即“基引用”;
 * Get/Create 返回的拷贝即“借出引用”, 调用方无需手工 delete,
 * 最后一个持有者释放后对象自动析构 */
using EpollMapperPtr = std::shared_ptr<EpollMapper>;
extern u_rw_lock_t *g_socket_epoll_lock;
EpollMapperPtr GetSocketEpollMapper(int socket_fd);
bool CreateSocketEpollMapper(int socket_fd, EpollMapperPtr &mapper);
/**
 * @brief 从注册表摘除 fd 的注册项(只摘“基引用”, 真正析构由最后一个持有者完成)
 * @param socket_fd socket fd
 * @param expect   非空时仅当表中仍是该对象才摘除。调用方持有借出引用时应传自身指针,
 *                 避免并发 close 后同 fd 号重新 ADD 出来的新对象被误删;
 *                 传 nullptr 表示按 fd 无条件摘除(仅 close/uninit 语义下使用)
 */
void CleanSocketEpollMapper(int socket_fd, const EpollMapper *expect = nullptr);
void CleanAllSocketEpollMappers();
void OnSocketClosed(int socket_fd);

class EventPoll {
public:
    explicit EventPoll(int epoll_fd) : epoll_fd_(epoll_fd)
    {
        mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
        ctl_mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
    }

    virtual ~EventPoll()
    {
        LockRegistry::LOCK_OPS.destroy(mutex_);
        LockRegistry::LOCK_OPS.destroy(ctl_mutex_);
    }

    /**
     * @brief corresponds to native epoll_ctl interface
     * @param op EPOLL_CTL_ADD / EPOLL_CTL_MOD / EPOLL_CTL_DEL
     * @param fd socket fd added to epoll_fd
     * @param event epoll event
     * @return 0: success; -1: failed
     */
    virtual int EpollCtl(int op, int fd, struct epoll_event *event) = 0;

    /**
     * @brief corresponds to native epoll_wait interface
     * @param fd socket fd added to epoll_fd
     * @param events epoll events waited by epoll_wait
     * @param maxevents max events return
     * @param timeout timeout of epoll_wait
     * @return 0: success; -1: failed
     */
    virtual int EpollWait(struct epoll_event *events, int maxevents, int timeout) = 0;

    virtual void WakeUpEpollFd() = 0;

    /**
     * @brief socket fd 关闭通知: 清理本 poll 实例内该 fd 的用户态注册表残留
     *        (内核侧随 fd/eventfd 关闭自动注销, 此处仅清 socket_data_ 等映射)
     * @param fd closed socket fd
     */
    virtual void OnSocketClosed(int fd)
    {
        (void)fd;
    }

    ALWAYS_INLINE int GetEpollFd() const noexcept
    {
        return epoll_fd_;
    }

    /**
     * @brief Set wakeup callback for async accept (works for both AsyncEventPoll and UrmaEventPoll)
     * @param ready_event Pointer to the ready_event EpollEvent (from UbsocketWakeupEvent)
     * @param cb Callback function to process ready events
     */
    void SetWakeupCallback(EpollEvent *ready_event,
                           std::function<int(struct epoll_event *, int, std::unordered_map<int, EpollEvent *> &)> cb)
    {
        ready_event_ = ready_event;
        wakeup_callback_ = cb;
    }

    DEFINE_REF_OPERATION_FUNC;

protected:
    DECLARE_REF_COUNT_VARIABLE;

    const int epoll_fd_;
    u_mutex_t *ctl_mutex_;
    u_mutex_t *mutex_;

    // For async accept wakeup
    EpollEvent *ready_event_ = nullptr;
    std::function<int(struct epoll_event *, int, std::unordered_map<int, EpollEvent *> &)> wakeup_callback_;
};
using EventPollPtr = Ref<EventPoll>;

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_EPOLL_FD_H
