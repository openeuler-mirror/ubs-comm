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

#include "urma_socket_types.h"
#include "urma_wrapper.h"

namespace ock {
namespace ubs {
namespace urma {

/**
 * Urma cqe handler,
 * @param flags            [in] optional flags
 * @param cr_count         [in] completion count
 * @param cr               [in] completion array ptr
 */
using UrmaCqeHandler = std::function<Result(uint32_t flags, int cr_count, urma_cr_t *cr)>;

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
          index_(index)
    {
    }

    ~UrmaPoller() override
    {
        Stop();
    }

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

private:
    UrmaJfcPtr jfc_{nullptr};          /* one poller contains one urma poller */
    const UrmaCqeHandler cqe_handler_; /* callback function of  */
    const uint16_t index_;             /* index of the poller */
    std::atomic<bool> started_{false}; /* started sign */
    bool stopping_{false};             /* stop sign to stop poller thread */
    std::thread thread_;               /* thread to call urma poll or epoll_wait */

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
