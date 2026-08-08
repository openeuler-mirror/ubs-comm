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
#ifndef UBS_COMM_URMA_POLLER_SINGLE_H
#define UBS_COMM_URMA_POLLER_SINGLE_H

#include "urma_context_pool.h"
#include "urma_socket_types.h"
#include "urma_wrapper.h"

namespace ock {
namespace ubs {
namespace urma {
/*
 * Aggregated poller, aggregate multiple jfc(s)
 */
struct UrmaAggPollerOptions {
    bool run_in_thread = false;
    bool create_dedicated_jfc = false;
    UrmaJfcPollingType polling_type = EVENT_POLLING;
    uint16_t poller_count_per_context = 1;
    uint16_t jfc_depth = 16000;
    UrmaCqeHandler cqe_handler = nullptr;
    UrmaContextPoolPtr context_pool = nullptr;

    friend std::ostream &operator<<(std::ostream &os, const UrmaAggPollerOptions &o)
    {
        os << "UrmaPollerOptions [run_in_thread: " << o.run_in_thread
           << ", poller thread:" << o.poller_count_per_context << ", jfc_depth: " << o.jfc_depth
           << ", cqe handler: " << (o.cqe_handler != nullptr) << "]";
        return os;
    }
};

class UrmaAggPoller : public Referable {
public:
    UrmaAggPoller(const UrmaContextPtr &ctx, const UrmaJfcePtr &jfce, const UrmaCqeHandler &h, uint16_t index,
                  const bool runInThread, const std::vector<UrmaJfcPtr> &jfcVector) noexcept
        : context_(ctx),
          jfce_(jfce),
          cqe_handler_(h),
          index_(index),
          run_in_thread_(runInThread),
          jfc_vector_(jfcVector)
    {
    }

    ~UrmaAggPoller() noexcept override
    {
        Stop();
    }

    /**
     * @brief Start the aggregated poller
     * @return
     */
    Result Start() noexcept;

    /**
     * @brief Stop the aggregated poller
     */
    void Stop() noexcept;

private:
    Result StartWithThread() noexcept;
    Result StartWithoutThread() noexcept;
    void StopInner() noexcept;

private:
    const UrmaContextPtr context_;       /* context ref */
    const UrmaJfcePtr jfce_;             /* jfce ref */
    const UrmaCqeHandler cqe_handler_;   /* callback function of  */
    const uint16_t index_;               /* index of the poller */
    const bool run_in_thread_;           /* if run in thread or not */
    std::atomic<bool> started_{false};   /* started sign */
    bool stopping_{false};               /* stop sign to stop poller thread */
    std::thread thread_;                 /* thread to call urma poll or epoll_wait */
    std::vector<UrmaJfcPtr> jfc_vector_; /* one poller contains one urma poller */

    friend class UrmaAggPollerSet;
};
using UrmaAggPollerPtr = Ref<UrmaAggPoller>;

class UrmaAggPollerSet : public Referable {
public:
    ~UrmaAggPollerSet() override
    {
        UnInit();
    }

    /**
     * @brief Initialize aggregated poller in async or sync mode
     * Async mode, we start background threads
     * Sync mode, we don't start background threads
     *
     * @param options      [in] options for agg poller set
     *
     * @return 0 if successful
     */
    Result Init(const UrmaAggPollerOptions &options) noexcept;

    /**
     * @brief Un-initialize
     */
    void UnInit() noexcept;

    /**
     * @brief Select or create jfc
     *
     * @param ctx          [in] the context that jfce and jfc on which
     * @param out          [in]
     * @return
     */
    Result SelectJfc(const UrmaContextPtr &ctx, UrmaJfcPtr &out) noexcept;

private:
    Result CreatePollers() noexcept;
    Result VerifyOptions() noexcept;
    void UnInitInner() noexcept;

    Result SelectJfcWithThread(const UrmaContextPtr &ctx, UrmaJfcPtr &out) noexcept;
    Result SelectJfcWithoutThread(const UrmaContextPtr &ctx, UrmaJfcPtr &out) noexcept;

private:
    std::mutex mutex_;                             /* mutex for this class */
    bool inited_ = false;                          /* flag for initialization */
    uint16_t index_creator_ = 0;                   /* increased index for create and round-robin */
    std::map<uint32_t, UrmaAggPollerPtr> pollers_; /* context id to poller map */
    UrmaAggPollerOptions options_;                 /* create options */
};
using UrmaAggPollerSetPtr = Ref<UrmaAggPollerSet>;
} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_POLLER_SINGLE_H
