/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Description: Wake up ready event fd utility (adapted for ubs-comm_new)
 */

#ifndef UBSOCKET_WAKEUP_EVENT_H
#define UBSOCKET_WAKEUP_EVENT_H

#include <queue>
#include <unordered_map>
#include "ubsocket_core_types.h"
#include "ubsocket_event_epoll.h"

namespace ock {
namespace ubs {

/**
 * @brief Independent utility class for ready event fd wakeup
 * @note Simplified version - only provides WakeUpReadyEventFd functionality
 */
class UbsocketWakeupEvent {
public:
    UbsocketWakeupEvent();
    ~UbsocketWakeupEvent();

    /**
     * @brief Initialize ready event fd and add to epoll
     * @param epollFd The epoll fd to add the ready event fd to
     * @return 0: success; -1: failed
     */
    int Initialize(int epollFd);

    /**
     * @brief Clean up ready event fd
     */
    void CleanUp();

    /**
     * @brief Wake up the ready event fd with a socket fd
     * @param fd Socket file descriptor to be added to ready queue
     */
    void WakeUpReadyEventFd(int fd);

    /**
     * @brief Process ready epoll events (call when ready_event_ is triggered in epoll_wait)
     * @param events Output epoll events array
     * @param maxevents Maximum number of events
     * @param socket_data Reference to socket_data_ map from AsyncEventPoll
     * @return Number of events processed
     */
    int ProcessReadyEvents(struct epoll_event *events, int maxevents,
                           std::unordered_map<int, EpollEvent *> &socket_data);

    /**
     * @brief Set the listen fd for injecting EPOLLIN event after wakeup
     * @param fd The listen socket file descriptor
     */
    void SetListenFd(int fd)
    {
        listen_fd_ = fd;
    }

    /**
     * @brief Get pointer to ready_event_ (for identifying ready event in epoll_wait)
     * @return Pointer to ready_event_
     */
    EpollEvent *GetReadyEvent()
    {
        return &ready_event_;
    }

    /**
     * @brief Set accept callback for async accept wakeup
     * @param cb Callback function to trigger Accept() again
     */
    void SetAcceptCallback(std::function<void()> cb)
    {
        accept_callback_ = cb;
    }

    /**
     * @brief Get accept callback (for calling Accept() from ProcessReadyEvents)
     * @return accept_callback_
     */
    std::function<void()> GetAcceptCallback()
    {
        return accept_callback_;
    }

private:
    int epollFd_ = -1;
    int readyEventFd_ = -1;
    std::queue<int> ready_event_queue_;
    u_mutex_t *ready_event_mutex_ = nullptr;
    EpollEvent ready_event_ = {EPOLL_EVENT_UB_SOCKET_IN, -1, {}};
    int listen_fd_ = -1;                      // listen fd for injecting EPOLLIN after wakeup
    std::function<void()> accept_callback_{}; // callback to trigger Accept() again
};

} // namespace ubs
} // namespace ock

#endif // UBSOCKET_WAKEUP_EVENT_H
