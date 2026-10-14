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

struct UrmaAppPollerStartOptions {
    UrmaJfcePtr jfce;
    UrmaCqeHandler handler;
    uint16_t index = 0;
    bool busyPoll = false;
    std::vector<UrmaJfcPtr> jfcVector;
};

class UrmaAggPoller : public Referable {
public:
    ~UrmaAggPoller() override = default;

    /**
     * @brief Start or initialize the poller with/without thread
     *
     * @param options      [in] options to be started with
     *
     * @return 0 if successful
     */
    virtual Result Start(const UrmaAppPollerStartOptions &options) noexcept = 0;

    /*
     * @brief Stop or un-initialize the poller
     */
    virtual void Stop() noexcept = 0;

    /**
     * @brief Get the jfce object
     *
     * @return jfce object
     */
    virtual UrmaJfcePtr Jfce() const noexcept = 0;

    /**
     * @brief Select an existing jfc or create a new one
     *
     * @param jfc          [in] jfc selected or created
     * @return 0 if successful
     */
    virtual Result SelectJfc(UrmaJfcPtr &jfc) noexcept = 0;

    virtual Result poll() noexcept = 0;
};
using UrmaAggPollerPtr = Ref<UrmaAggPoller>;

class UrmaDedicatedJfcAggPoller : public UrmaAggPoller {
public:
    ~UrmaDedicatedJfcAggPoller() override
    {
        Stop();
    }

    Result Start(const UrmaAppPollerStartOptions &options) noexcept override;

    void Stop() noexcept override;

    UrmaJfcePtr Jfce() const noexcept override;

    Result SelectJfc(UrmaJfcPtr &jfc) noexcept override;

    Result poll() noexcept override;

private:
    UrmaJfcePtr jfce_;
    UrmaCqeHandler handler_;
    uint16_t index_ = 0;
    std::vector<UrmaJfcPtr> jfc_vector_;
    bool stopping_{false};
    std::atomic<uint64_t> seq_{0};
};

class UrmaSharedJfcAggPollerWithThread : public UrmaAggPoller {
public:
    ~UrmaSharedJfcAggPollerWithThread() override
    {
        Stop();
    }

    Result Start(const UrmaAppPollerStartOptions &options) noexcept override;

    void Stop() noexcept override;

    UrmaJfcePtr Jfce() const noexcept override;

    Result SelectJfc(UrmaJfcPtr &jfc) noexcept override;

    Result poll() noexcept override
    {
        return UBS_OK;
    }

private:
    void StopInner() noexcept;

    void RunInThread() noexcept;
    void EventPoll() noexcept;
    Result DrainCqes(urma_jfc_t **ev_jfc, uint32_t jfc_cnt) noexcept;
    void BusyPoll() noexcept;

    /*
     * Message-triggered wakeup: a dedicated loopback jetty (jfs/jfr bound to the poller jfc) is
     * created on Start. StopInner() posts a local message through it so a completion event lands
     * on the poller jfc and releases the blocking urma_wait_jfc, replacing the former
     * signal-based interrupt (SIGUSR1 via pthread_kill).
     */
    Result SetupWakeupJetty() noexcept;
    void Wakeup() noexcept;
    void DestroyWakeupJetty() noexcept;

private:
    UrmaJfcePtr jfce_;                   /* jfce ref */
    UrmaCqeHandler cqe_handler_;         /* callback function of  */
    uint16_t index_;                     /* index of the poller */
    bool busy_poll_ = false;             /* is busy poll or not */
    std::atomic<bool> started_{false};   /* started sign */
    std::atomic<bool> stopping_{false};  /* stop sign to stop poller thread */
    std::thread thread_;                 /* thread to call urma poll or epoll_wait */
    std::vector<UrmaJfcPtr> jfc_vector_; /* one poller contains one urma poller */
    std::atomic<uint64_t> seq_{0};

    /* loopback wakeup resources */
    UrmaJettyPtr wakeup_jetty_;              /* loopback jetty bound to the poller jfc */
    urma_target_seg_t *wakeup_seg_{nullptr}; /* registered segment for the wakeup buffer */
    void *wakeup_buf_{nullptr};              /* registered buffer used as message payload */
    urma_jfr_wr_t wakeup_recv_wr_{};         /* pre-posted recv WR consumed by the wakeup send */
    urma_sge_t wakeup_recv_sge_{};           /* backing sge of the pre-posted recv WR */
};

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

    const std::map<uint32_t, UrmaAggPollerPtr> &GetAllPollers() const noexcept
    {
        return pollers_;
    }

private:
    Result CreatePollers() noexcept;
    Result VerifyOptions() noexcept;
    void UnInitInner() noexcept;

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
