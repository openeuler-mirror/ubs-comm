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
#ifndef UBS_COMM_URMA_POLLER_H
#define UBS_COMM_URMA_POLLER_H

#include "urma_event_dispatch.h"
#include "urma_socket_types.h"
#include "urma_wrapper.h"

namespace ock {
namespace ubs {
namespace urma {

struct UrmaPollerOptions {
    uint16_t poller_count = 2;
    uint16_t jfc_depth = 16000;
    UrmaJfcPollingType polling_type = EVENT_POLLING;
    UrmaCqeHandler cqe_handler = nullptr;
    UrmaContextPtr context_;

    friend std::ostream &operator<<(std::ostream &os, const UrmaPollerOptions &o)
    {
        os << "UrmaPollerOptions [" << o.poller_count << ", " << o.jfc_depth << ", jfc_depth: " << o.jfc_depth
           << ", polling type: " << (o.polling_type == EVENT_POLLING ? "event" : "busy") << "]";
        return os;
    }
};

class UrmaPoller : public Referable {
public:
    UrmaPoller(uint16_t index, const UrmaCqeHandler &h, const UrmaJfcPtr &jfc) noexcept
        : jfc_(jfc),
          cqe_handler_(h),
          index_(index),
          handle_mode_caps_(UrmaHandleModeCapsNow())
    {
    }

    ~UrmaPoller() override
    {
        Stop();
    }

    const UrmaJfcPtr &GetJFC() const noexcept
    {
        return jfc_;
    }

    /*
     * DISPATCHER_THREAD_POOL 模式(无独立 poller 线程)下, 由 UrmaEventPoll 在 epoll_wait
     * 的 JFCE 事件处理中调用: 内联 ack + 批 poll + 经 cqe_handler_ 派发到各 socket。
     * @param ackFirst 是否先 Ack(1) 一次中断事件(inline JFCE 路径传 true)
     */
    Result PollAndDispatch(bool ackFirst = false) noexcept;

private:
    /**
     * @brief Start the thread to do polling
     * @return 0 if successful
     */
    Result Start() noexcept;

    /**
     * @brief Stop the thread of polling
     */
    void Stop() noexcept;

    /**
     * @brief thread function
     */
    void RunInThread();

    void EventPoll() noexcept;
    void BusyPoll() noexcept;
    Result DrainCqes(bool ackFirst) noexcept;

private:
    UrmaJfcPtr jfc_{nullptr};             /* one poller contains one urma poller */
    const UrmaCqeHandler cqe_handler_;    /* callback function of  */
    const uint16_t index_;                /* index of the poller */
    UrmaHandleModeCaps handle_mode_caps_; /* ctor snapshot of the handle-mode capabilities */
    std::atomic<bool> started_{false};    /* started sign */
    bool stopping_{false};                /* stop sign to stop poller thread */
    std::thread thread_;                  /* thread to call urma poll or epoll_wait */

    friend class UrmaPollerSet;
};
using UrmaPollerPtr = Ref<UrmaPoller>;

class UrmaPollerSet : public Referable {
public:
    static UrmaPollerSet &Instance() noexcept
    {
        static UrmaPollerSet instance;
        return instance;
    }

public:
    Result Init(const UrmaPollerOptions &options) noexcept;
    void UnInit() noexcept;

    Result SelectPoller(UrmaJfcPtr &jfc) noexcept;
    const std::vector<UrmaPollerPtr> &GetAllPollers() const noexcept
    {
        return pollers_;
    }

private:
    void UnInitInner() noexcept;
    Result CreateUrmaPollers() noexcept;
    Result VerifyOptions(const UrmaPollerOptions &options) noexcept;

private:
    std::vector<UrmaPollerPtr> pollers_;        /* poller vector */
    std::mutex mutex_;                          /* for init */
    bool inited_{false};                        /* init flag */
    std::atomic<uint16_t> rr_select_index_{0};  /* round-robin index for poller choosing */
    UrmaPollerOptions options_;                 /* options  */
    static std::atomic<uint16_t> INDEX_CREATOR; /* auto increase index */
};
using UrmaPollerSetPtr = Ref<UrmaPollerSet>;
} // namespace urma
} // namespace ubs
} // namespace ock

#endif //UBS_COMM_URMA_POLLER_H
