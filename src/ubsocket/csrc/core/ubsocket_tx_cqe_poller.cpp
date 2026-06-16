/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "ubsocket_tx_cqe_poller.h"

#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "common/ubsocket_lock.h"
#include "common/ubsocket_logger.h"
#include "common/ubsocket_scope_exit.h"
#include "ubsocket_data_tx.h"
#include "ubsocket_socket.h"

namespace ock {
namespace ubs {
TxCqePoller::~TxCqePoller()
{
    Stop();
}

int TxCqePoller::Start()
{
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return 0;
    }
    auto started_restorer = MakeScopeExit([this]() { started_ = false; });

    mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
    if (mutex_ == nullptr) {
        UBS_VLOG_ERR("TxCqePoller mutex create failed.\n");
        return -1;
    }
    auto mutex_destroyer = MakeScopeExit([this]() {
        LockRegistry::LOCK_OPS.destroy(mutex_);
        mutex_ = nullptr;
    });

    epoll_fd_ = LibcApi::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        UBS_VLOG_ERR("TxCqePoller epoll_create1 failed: %d.\n", errno);
        return -1;
    }
    auto epfd_closer = MakeScopeExit([this]() {
        LibcApi::close(epoll_fd_);
        epoll_fd_ = -1;
    });

    timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timer_fd_ < 0) {
        UBS_VLOG_ERR("TxCqePoller timerfd_create failed: %d.\n", errno);
        return -1;
    }
    auto timer_destroyer = MakeScopeExit([this]() {
        LibcApi::close(timer_fd_);
        timer_fd_ = -1;
    });

    struct epoll_event ev = {.events = EPOLLIN, .data = {.fd = timer_fd_}};
    if (LibcApi::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, timer_fd_, &ev) < 0) {
        UBS_VLOG_ERR("TxCqePoller epoll_ctl add timerfd failed: %d.\n", errno);
        return -1;
    }

    // 设置定时器周期，例如 100ms
    struct itimerspec interval;
    interval.it_value.tv_sec = 0;
    interval.it_value.tv_nsec = 100'000'000;
    interval.it_interval.tv_sec = 0;
    interval.it_interval.tv_nsec = 100'000'000;
    if (timerfd_settime(timer_fd_, 0, &interval, nullptr) < 0) {
        UBS_VLOG_ERR("TxCqePoller timerfd_settime failed: %d.\n", errno);
        return -1;
    }

    stopped_ = false;
    poll_thread_ = std::thread([this]() { RunInThread(); });

    timer_destroyer.Deactivate();
    epfd_closer.Deactivate();
    mutex_destroyer.Deactivate();
    started_restorer.Deactivate();
    return 0;
}

void TxCqePoller::Stop()
{
    bool expected = true;
    if (!started_.compare_exchange_strong(expected, false)) {
        return;
    }

    stopped_ = true;

    // 直接关闭 epoll_fd_，这会唤醒 epoll_wait 并让它返回 EBADF
    if (epoll_fd_ >= 0) {
        LibcApi::close(epoll_fd_);
        epoll_fd_ = -1;
    }

    if (poll_thread_.joinable()) {
        poll_thread_.join();
    }

    {
        Locker lock(mutex_);
        sockets_.clear();
    }

    if (timer_fd_ >= 0) {
        LibcApi::close(timer_fd_);
        timer_fd_ = -1;
    }

    if (mutex_ != nullptr) {
        LockRegistry::LOCK_OPS.destroy(mutex_);
        mutex_ = nullptr;
    }
}

void TxCqePoller::AddSocket(const SocketPtr &sock)
{
    if (sock == nullptr) {
        return;
    }

    Locker sLock(mutex_);
    for (const auto &item : sockets_) {
        if (item == sock) {
            return;
        }
    }
    sockets_.push_back(sock);
}

void TxCqePoller::DelSocket(const SocketPtr &sock)
{
    if (sock == nullptr) {
        return;
    }

    Locker sLock(mutex_);
    for (auto it = sockets_.begin(); it != sockets_.end(); ++it) {
        if (*it == sock) {
            sockets_.erase(it);
            break;
        }
    }
}

void TxCqePoller::RunInThread() noexcept
{
    pthread_setname_np(pthread_self(), "ubs_tx_poller");

    const std::size_t MaxEvents = 32;
    struct epoll_event events[MaxEvents];

    while (!stopped_.load(std::memory_order_relaxed)) {
        // epoll_fd_ 可能会被 Stop() 关闭
        const int epfd = epoll_fd_;
        if (epfd < 0) {
            break;
        }

        int nfds = LibcApi::epoll_wait(epfd, events, MaxEvents, -1);
        if (nfds < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        // TODO 后面可能还是得监听 tx interrupt fd 以快速响应错误 CQE
        bool need_poll = false;
        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == timer_fd_) {
                uint64_t expirations = 0;
                ssize_t s = LibcApi::read(timer_fd_, &expirations, sizeof(expirations));
                (void)s;
                need_poll = true;
            }
        }

        if (stopped_.load(std::memory_order_relaxed)) {
            break;
        }

        if (need_poll) {
            std::vector<SocketPtr> activeSockets;
            {
                Locker sLock(mutex_);
                activeSockets = std::move(sockets_);
            }

            for (const auto &sock : activeSockets) {
                if (stopped_.load(std::memory_order_relaxed)) {
                    break;
                }
                if (sock != nullptr) {
                    // TODO dynamic_cast -> static_cast
                    auto sockBase = RefDynamicCast<SocketBase>(sock);
                    if (sockBase != nullptr) {
                        DataTxOps *txOps = sockBase->GetTx()->GetTxOps();
                        if (txOps != nullptr) {
                            txOps->PollTx(sock);
                        }
                    }
                }
            }
        }
    }
}

} // namespace ubs
} // namespace ock
