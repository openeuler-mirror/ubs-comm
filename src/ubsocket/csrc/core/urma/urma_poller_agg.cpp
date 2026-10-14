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
#include <cerrno>
#include <cstdlib>

#include "urma_poller_agg.h"
#include "urma_setting.h"

namespace ock {
namespace ubs {
namespace urma {
namespace {
/* the wakeup message carries no payload; a 1-byte sge is enough to satisfy the verbs */
constexpr uint32_t WAKEUP_MSG_LEN = 1;
constexpr uint32_t WAKEUP_BUF_SIZE = 64;
constexpr uint32_t WAKEUP_BUF_ALIGN = 4096;
constexpr uint32_t WAKEUP_QUEUE_DEPTH = 16;
} // namespace

Result UrmaDedicatedJfcAggPoller::Start(const UrmaAppPollerStartOptions &options) noexcept
{
    if (jfce_ != nullptr) {
        UBS_VLOG_INFO("Already started");
        return UBS_OK;
    }
    jfce_ = options.jfce;
    handler_ = options.handler;
    index_ = options.index;
    jfc_vector_.assign(options.jfcVector.begin(), options.jfcVector.end());
    UBS_VLOG_TRACE("agg poller %d started", index_);
    return UBS_OK;
}

void UrmaDedicatedJfcAggPoller::Stop() noexcept
{
    if (jfce_ == nullptr) {
        UBS_VLOG_INFO("Not started");
        return;
    }
    stopping_ = true;
    jfc_vector_.clear();
    jfce_ = nullptr;
}

UrmaJfcePtr UrmaDedicatedJfcAggPoller::Jfce() const noexcept
{
    return jfce_;
}

Result UrmaDedicatedJfcAggPoller::SelectJfc(UrmaJfcPtr &jfc) noexcept
{
    if (UNLIKELY(jfce_ == nullptr) || UNLIKELY(jfc_vector_.empty())) {
        UBS_VLOG_INFO("Not started");
        return UBS_ERROR;
    }
    if (jfc_vector_.size() == 1) {
        jfc = jfc_vector_[0];
        return UBS_OK;
    }

    uint32_t seq = seq_.fetch_add(1);
    jfc = jfc_vector_[seq % jfc_vector_.size()];
    return UBS_OK;
}

Result UrmaDedicatedJfcAggPoller::poll() noexcept
{
    if (jfc_vector_.empty() || handler_ == nullptr) {
        UBS_VLOG_ERR("poller(%d) invalid to poll and dispatch.", index_);
        return UBS_ERROR;
    }

    constexpr int32_t POLL_BATCH_SIZE = 32;
    urma_cr_t crs[POLL_BATCH_SIZE]{};
    bool first_time = true;
    uint32_t jfc_cnt = jfc_vector_.size();
    uint32_t try_count[jfc_cnt];
    memset(try_count, 0, sizeof(try_count));
    uint32_t try_max = 1;
    while (!stopping_) {
        bool finish = true;
        for (uint32_t i = 0; i < jfc_cnt; ++i) {
            if (try_count[i] >= try_max) {
                continue;
            }
            auto cnt = jfc_vector_[i]->Poll(POLL_BATCH_SIZE, crs);
            if (UNLIKELY(cnt == 0)) {
                jfc_vector_[i]->Rearm(false);
                try_count[i]++;
                continue;
            }
            if (UNLIKELY(cnt < 0)) {
                UBS_VLOG_ERR("poll jfc failed return: %d", cnt);
                return UBS_ERROR;
            }
            finish = false;

            auto result = handler_(0, cnt, crs);
#ifdef UBS_LOG_TRACE_ENABLED
            UBS_VLOG_TRACE("cqe handler processed (%d) and return %d, continue polling", cnt, result);
#else
            (void)result;
#endif
            if (first_time) {
                first_time = false;
                jfc_vector_[i]->Rearm(false);
                try_count[i] = 0;
            }
        }
        if (finish) {
            break;
        }
    }
    return UBS_OK;
}

/****************************************************************/
UrmaJfcePtr UrmaSharedJfcAggPollerWithThread::Jfce() const noexcept
{
    return jfce_;
}

Result UrmaSharedJfcAggPollerWithThread::SelectJfc(UrmaJfcPtr &jfc) noexcept
{
    if (UNLIKELY(jfce_ == nullptr) || UNLIKELY(jfc_vector_.empty())) {
        UBS_VLOG_INFO("Not started");
        return UBS_ERROR;
    }
    if (jfc_vector_.size() == 1) {
        jfc = jfc_vector_[0];
        return UBS_OK;
    }

    uint32_t seq = seq_.fetch_add(1);
    jfc = jfc_vector_[seq % jfc_vector_.size()];
    return UBS_OK;
}

Result UrmaSharedJfcAggPollerWithThread::Start(const UrmaAppPollerStartOptions &options) noexcept
{
    if (started_.load()) {
        UBS_VLOG_DEBUG("Already started");
        return UBS_OK;
    }

    jfce_ = options.jfce;
    cqe_handler_ = options.handler;
    index_ = options.index;
    busy_poll_ = options.busyPoll;
    jfc_vector_.assign(options.jfcVector.begin(), options.jfcVector.end());

    if (cqe_handler_ == nullptr) {
        UBS_VLOG_ERR("Failed to start urma agg poller %d as cqe_handler is nullptr", index_);
        return UBS_ERROR;
    }

    /* busy poll exits on its own by re-checking stopping_, so the loopback wakeup jetty is
     * only needed for the blocking event-poll path. */
    if (!busy_poll_) {
        auto wakeup_result = SetupWakeupJetty();
        if (UNLIKELY(wakeup_result != UBS_OK)) {
            UBS_VLOG_ERR("Failed to start urma agg poller %d as wakeup jetty setup failed", index_);
            return wakeup_result;
        }
    }

    thread_ = std::thread(&UrmaSharedJfcAggPollerWithThread::RunInThread, this);

    /* wait for 5 seconds to ensure the thread is started */
    for (uint16_t i = 0; i < 5000L; i++) {
        if (started_.load()) {
            break;
        } else {
            usleep(1000L);
        }
    }

    UBS_VLOG_TRACE("agg poller %d started", index_);
    return UBS_OK;
}

void UrmaSharedJfcAggPollerWithThread::Stop() noexcept
{
    if (!started_.load()) {
        UBS_VLOG_DEBUG("Not started");
        return;
    }

    StopInner();
    started_.store(false);
}

void UrmaSharedJfcAggPollerWithThread::StopInner() noexcept
{
    /*
     * Message-triggered wakeup. Setting stopping_ alone cannot release the indefinitely blocking
     * urma_wait_jfc(-1); posting a loopback message generates a completion on the poller jfc so
     * the thread wakes up, observes stopping_ and exits. No signal is sent, which avoids the
     * harmless-but-noisy "wait jfc ioctl failed, errno=EINTR" logs from liburma.
     */
    stopping_.store(true);
    Wakeup();

    if (thread_.joinable()) {
        thread_.join();
    }

    DestroyWakeupJetty();
}

Result UrmaSharedJfcAggPollerWithThread::SetupWakeupJetty() noexcept
{
    if (UNLIKELY(jfc_vector_.empty())) {
        UBS_VLOG_ERR("Failed to setup wakeup jetty for poller(%d) as there is no jfc", index_);
        return UBS_ERROR;
    }

    auto ctx = jfc_vector_[0]->Context();
    if (UNLIKELY(ctx == nullptr)) {
        UBS_VLOG_ERR("Failed to setup wakeup jetty for poller(%d) as context is null", index_);
        return UBS_ERROR;
    }

    /* step1: allocate and register a small local buffer as the loopback message payload */
    if (UNLIKELY(posix_memalign(&wakeup_buf_, WAKEUP_BUF_ALIGN, WAKEUP_BUF_SIZE) != 0 || wakeup_buf_ == nullptr)) {
        UBS_VLOG_ERR("Failed to allocate wakeup buffer for poller(%d)", index_);
        wakeup_buf_ = nullptr;
        return UBS_ERROR;
    }
    memset(wakeup_buf_, 0, WAKEUP_BUF_SIZE);

    urma_reg_seg_flag_t seg_flag{};
    seg_flag.value = 0;
    seg_flag.bs.token_policy = URMA_TOKEN_NONE;
    seg_flag.bs.cacheable = URMA_NON_CACHEABLE;
    seg_flag.bs.access = URMA_ACCESS_READ | URMA_ACCESS_WRITE;

    urma_seg_cfg_t seg_cfg{};
    seg_cfg.va = reinterpret_cast<uint64_t>(wakeup_buf_);
    seg_cfg.len = WAKEUP_BUF_SIZE;
    seg_cfg.token_id = nullptr;
    seg_cfg.flag = seg_flag;
    seg_cfg.user_ctx = 0;
    seg_cfg.iova = 0;

    wakeup_seg_ = UrmaApi::urma_register_seg(ctx->Handle(), &seg_cfg);
    if (UNLIKELY(wakeup_seg_ == nullptr)) {
        UBS_VLOG_ERR("Failed to register wakeup segment for poller(%d), errno: %d", index_, errno);
        DestroyWakeupJetty();
        return UBS_ERROR;
    }

    /* step2: create a jfs/jfr bound to the poller jfc, then the loopback jetty */
    urma_jfs_cfg_t jfs_cfg = ctx->CreateJfsCfg(WAKEUP_QUEUE_DEPTH, UrmaSetting::UB_JETTY_TRANS_MODE);
    UrmaJfsPtr wakeup_jfs;
    if (UNLIKELY(ctx->CreateJfs(jfs_cfg, jfc_vector_[0], wakeup_jfs) != UBS_OK)) {
        UBS_VLOG_ERR("Failed to create wakeup jfs for poller(%d)", index_);
        DestroyWakeupJetty();
        return UBS_ERROR;
    }

    urma_jfr_cfg_t jfr_cfg = ctx->CreateJfrCfg(WAKEUP_QUEUE_DEPTH, UrmaSetting::UB_JETTY_TRANS_MODE, 0);
    UrmaJfrPtr wakeup_jfr;
    if (UNLIKELY(ctx->CreateJfr(jfr_cfg, jfc_vector_[0], wakeup_jfr) != UBS_OK)) {
        UBS_VLOG_ERR("Failed to create wakeup jfr for poller(%d)", index_);
        DestroyWakeupJetty();
        return UBS_ERROR;
    }

    urma_jetty_cfg_t jetty_cfg = ctx->CreateJettyCfg();
    if (UNLIKELY(ctx->CreateJetty(jetty_cfg, UrmaSetting::UB_JETTY_TP_TYPE, wakeup_jfs, wakeup_jfr, wakeup_jetty_) !=
                 UBS_OK)) {
        UBS_VLOG_ERR("Failed to create wakeup jetty for poller(%d)", index_);
        DestroyWakeupJetty();
        return UBS_ERROR;
    }

    /* step3: import the local jetty as the loopback target used by the send WR */
    auto jetty_id = wakeup_jetty_->RawJettyId();
    uint32_t token = wakeup_jetty_->Token();
    if (UNLIKELY(wakeup_jetty_->ImportRemoteJetty(jetty_id, token) != UBS_OK)) {
        UBS_VLOG_ERR("Failed to import loopback wakeup jetty for poller(%d)", index_);
        DestroyWakeupJetty();
        return UBS_ERROR;
    }

    /* step4: pre-post a recv WR so the loopback send always finds a buffer (no RNR retry) */
    wakeup_recv_sge_.addr = reinterpret_cast<uint64_t>(wakeup_buf_);
    wakeup_recv_sge_.len = WAKEUP_MSG_LEN;
    wakeup_recv_sge_.tseg = wakeup_seg_;
    wakeup_recv_sge_.user_tseg = nullptr;
    wakeup_recv_wr_.src.sge = &wakeup_recv_sge_;
    wakeup_recv_wr_.src.num_sge = 1;
    wakeup_recv_wr_.user_ctx = 0;
    wakeup_recv_wr_.next = nullptr;

    urma_jfr_wr_t *bad_recv_wr = nullptr;
    if (UNLIKELY(wakeup_jetty_->PostRecvWr(&wakeup_recv_wr_, &bad_recv_wr) != URMA_SUCCESS)) {
        UBS_VLOG_ERR("Failed to pre-post wakeup recv WR for poller(%d), errno: %d", index_, errno);
        DestroyWakeupJetty();
        return UBS_ERROR;
    }

    UBS_VLOG_TRACE("wakeup jetty for poller(%d) is ready", index_);
    return UBS_OK;
}

void UrmaSharedJfcAggPollerWithThread::Wakeup() noexcept
{
    if (UNLIKELY(wakeup_jetty_ == nullptr || wakeup_seg_ == nullptr)) {
        /* normal for busy-poll pollers, which never create a wakeup jetty */
        UBS_VLOG_TRACE("Wakeup poller(%d) skipped as wakeup jetty is not ready", index_);
        return;
    }

    urma_sge_t src_sge{};
    src_sge.addr = reinterpret_cast<uint64_t>(wakeup_buf_);
    src_sge.len = WAKEUP_MSG_LEN;
    src_sge.tseg = wakeup_seg_;
    src_sge.user_tseg = nullptr;

    /* user_ctx = 0 so the sender completion is ignored by HandleCqe; the recv completion uses
     * URMA_CR_OPC_SEND which HandleCqe does not act on. */
    urma_jfs_wr_t wr{};
    wr.opcode = URMA_OPC_SEND;
    wr.user_ctx = 0;
    wr.send.src.sge = &src_sge;
    wr.send.src.num_sge = 1;
    wr.send.imm_data = 0;
    wr.next = nullptr;
    wr.flag.bs.complete_enable = 1;
    wr.flag.bs.inline_flag = 0;

    urma_jfs_wr_t *bad_wr = nullptr;
    auto ret = wakeup_jetty_->PostSendWr(&wr, &bad_wr);
    if (UNLIKELY(ret != URMA_SUCCESS)) {
        UBS_VLOG_ERR("Failed to post wakeup message for poller(%d), ret: %d, errno: %d", index_, ret, errno);
    }
}

void UrmaSharedJfcAggPollerWithThread::DestroyWakeupJetty() noexcept
{
    /* release the jetty first: it unimports the loopback target and releases jfs/jfr */
    wakeup_jetty_ = nullptr;

    if (wakeup_seg_ != nullptr) {
        if (LIKELY(UrmaApi::IsLoaded())) {
            auto ret = UrmaApi::urma_unregister_seg(wakeup_seg_);
            if (UNLIKELY(ret != URMA_SUCCESS)) {
                UBS_VLOG_WARN("Failed to unregister wakeup segment, errno: %d", errno);
            }
        }
        wakeup_seg_ = nullptr;
    }

    if (wakeup_buf_ != nullptr) {
        free(wakeup_buf_);
        wakeup_buf_ = nullptr;
    }

    wakeup_recv_wr_ = {};
    wakeup_recv_sge_ = {};
}

void UrmaSharedJfcAggPollerWithThread::RunInThread() noexcept
{
    started_.store(true);
    UBS_VLOG_TRACE("thread for urma agg poller(%d) is started", index_);
    std::string thread_name = "ubs_apoller_" + std::to_string(index_);
    pthread_setname_np(pthread_self(), thread_name.c_str());

    if (busy_poll_) {
        BusyPoll();
    } else {
        EventPoll();
    }

    UBS_VLOG_INFO("thread for urma agg poller(%d) exiting", index_);
}

void UrmaSharedJfcAggPollerWithThread::BusyPoll() noexcept
{
    constexpr int32_t POLL_BATCH_SIZE = 32;
    urma_cr_t crs[POLL_BATCH_SIZE]{};
    while (!stopping_) {
        for (auto jfc_ : jfc_vector_) {
            try {
                int polled_count = jfc_->Poll(POLL_BATCH_SIZE, crs);
                if (polled_count > 0) { /* if polled */
                    auto result = cqe_handler_(0, polled_count, crs);
                    if (UNLIKELY(result != UBS_OK)) {
                        UBS_VLOG_INFO("cqe handler processed and return %d, continue polling", result);
                    }
                } else {
                    // TODO
                }
            } catch (...) {
                UBS_VLOG_ERR("Exception caught in UrmaPoller");
            }
        }
    }
}

void UrmaSharedJfcAggPollerWithThread::EventPoll() noexcept
{
    constexpr uint32_t MAX_EV_JFC_CNT = 64;
    uint32_t jfc_cnt = jfc_vector_.size();
    if (UNLIKELY(jfc_cnt == 0 || jfc_cnt > MAX_EV_JFC_CNT)) {
        UBS_VLOG_ERR("Invalid jfc count(%u) for agg poller(%d)", jfc_cnt, index_);
        return;
    }
    urma_jfc_t *ev_jfc[MAX_EV_JFC_CNT] = {nullptr};
    uint32_t ack[MAX_EV_JFC_CNT] = {1};
    while (!stopping_) {
        try {
            /* block indefinitely; the loopback wakeup message posted by StopInner() releases it */
            auto cnt = UrmaApi::urma_wait_jfc(jfce_->GetRawJfce(), jfc_cnt, -1, ev_jfc);
            if (stopping_) {
                break;
            }
            if (cnt < 0) {
                if (errno == EINTR) {
                    continue; /* interrupted by an unrelated signal, re-check stopping_ */
                }
                UBS_VLOG_ERR("Failed to wait jfc cnt: %d, errno: %d", cnt, errno);
                continue;
            }
            if (UNLIKELY(cnt == 0)) {
                continue;
            }
            UrmaApi::urma_ack_jfc(ev_jfc, ack, cnt);
            DrainCqes(ev_jfc, static_cast<uint32_t>(cnt));
        } catch (...) {
            UBS_VLOG_ERR("Exception caught in UrmaPoller");
        }
    }
}

Result UrmaSharedJfcAggPollerWithThread::DrainCqes(urma_jfc_t **ev_jfc, uint32_t jfc_cnt) noexcept
{
    constexpr int32_t POLL_BATCH_SIZE = 32;
    urma_cr_t crs[POLL_BATCH_SIZE]{};

    bool first_time = true;
    uint32_t try_count[jfc_cnt];
    memset(try_count, 0, sizeof(try_count));
    uint32_t try_max = 1;
    while (!stopping_) {
        bool finish = true;
        for (uint32_t i = 0; i < jfc_cnt; ++i) {
            if (UNLIKELY(ev_jfc[i] == nullptr)) {
                continue;
            }
            if (try_count[i] >= try_max) {
                continue;
            }
            auto cnt = UrmaApi::urma_poll_jfc(ev_jfc[i], POLL_BATCH_SIZE, crs);
            if (UNLIKELY(cnt == 0)) {
                UrmaApi::urma_rearm_jfc(ev_jfc[i], false);
                try_count[i]++;
                continue;
            }

            if (UNLIKELY(cnt < 0)) {
                stopping_ = true;
                UBS_VLOG_ERR("poll jfc failed return: %d", cnt);
                return UBS_ERROR;
            }
            finish = false;
            auto result = cqe_handler_(0, cnt, crs);
#ifdef UBS_LOG_TRACE_ENABLED
            UBS_VLOG_TRACE("cqe handler processed (%d) and return %d, continue polling", cnt, result);
#else
            (void)result;
#endif
            if (first_time) {
                first_time = false;
                UrmaApi::urma_rearm_jfc(ev_jfc[i], false);
            }
        }
        if (finish) {
            break;
        }
    }
    return UBS_OK;
}

/* for agg poller set */
Result UrmaAggPollerSet::VerifyOptions() noexcept
{
    if (options_.poller_count_per_context > 16L) {
        UBS_VLOG_ERR("Option poller_count_per_context(%d) is valid, which should be <=16",
                     options_.poller_count_per_context);
        return UBS_INVALID_PARAM;
    }

    if (options_.cqe_handler == nullptr) {
        UBS_VLOG_ERR("Option cqe_handler shoule NOT be null");
        return UBS_INVALID_PARAM;
    }

    if (options_.context_pool == nullptr) {
        UBS_VLOG_ERR("Option context_pool should NOT be null");
        return UBS_INVALID_PARAM;
    }

    return UBS_OK;
}

Result UrmaAggPollerSet::Init(const ock::ubs::urma::UrmaAggPollerOptions &options) noexcept
{
    UBS_VLOG_TRACE("Enter");
    std::lock_guard<std::mutex> guard(mutex_);
    if (inited_) {
        UBS_VLOG_DEBUG("Already initialized");
        return UBS_OK;
    }

    options_ = options;
    auto result = VerifyOptions();
    if (result != UBS_OK) {
        UBS_VLOG_ERR("Failed to initialize urma agg poller set as options is invalid");
        return result;
    }

    result = CreatePollers();
    if (result != UBS_OK) {
        UBS_VLOG_ERR("Failed to create urma agg poller, result(%d)", result);
        return result;
    }

    inited_ = true;
    UBS_VLOG_TRACE("Initialized");
    return UBS_OK;
}

Result UrmaAggPollerSet::CreatePollers() noexcept
{
    /* step1: pollers_ clean */
    pollers_.clear();

    /* step2: create jfce */
    auto all_contexts = options_.context_pool->Contexts();
    for (auto &iter : all_contexts) {
        auto &ctx = iter.second;
        if (ctx == nullptr) {
            UBS_VLOG_WARN("context for dev(%s) eid index(%d) is null, dont create jfce for this device",
                          iter.first.first.c_str(), iter.first.second);
            continue;
        }

        std::vector<UrmaJfcPtr> jfc_vector;
        UrmaAggPollerPtr tmp_poller;
        /* create jfce */
        UrmaJfcePtr jfce;
        if (ctx->CreateJfce(jfce) != UBS_OK) {
            UBS_VLOG_ERR("Failed to create UrmaJfce with dev(%s) eid index(%d)", iter.first.first.c_str(),
                         iter.first.second);
            UnInitInner();
            return UBS_ERROR;
        }
        /* create jfc */
        for (uint16_t i = 0; i < options_.poller_count_per_context; i++) {
            urma_jfc_cfg_t jfc_cfg = ctx->CreateJfcCfg(options_.jfc_depth);
            jfc_cfg.jfce = jfce->GetRawJfce();
            UrmaJfcPtr jfc;
            if (ctx->CreateJfc(jfc_cfg, options_.polling_type, jfc) != UBS_OK) {
                UBS_VLOG_ERR("Failed to create UrmaJfc with dev(%s) eid index(%d)", iter.first.first.c_str(),
                             iter.first.second);
                UnInitInner();
                return UBS_ERROR;
            }

            jfc_vector.emplace_back(jfc);
        }
        UrmaAppPollerStartOptions start_options;
        start_options.jfce = jfce;
        start_options.handler = options_.cqe_handler;
        start_options.index = index_creator_++;
        start_options.jfcVector.swap(jfc_vector);
        start_options.busyPoll = options_.polling_type == BUSY_POLLING;
        if (options_.run_in_thread) {
            /* case1: for async poller, which is running in thread */
            tmp_poller = new (std::nothrow) UrmaSharedJfcAggPollerWithThread();
            if (UNLIKELY(tmp_poller == nullptr)) {
                UBS_VLOG_ERR("Failed to create agg poller for dev(%s) eid index(%d)", iter.first.first.c_str(),
                             iter.first.second);
                UnInitInner();
                return UBS_MALLOC_FAILED;
            }
        } else {
            /*case2: for sync poller */
            tmp_poller = new (std::nothrow) UrmaDedicatedJfcAggPoller();
            if (UNLIKELY(tmp_poller == nullptr)) {
                UBS_VLOG_ERR("Failed to create agg poller for dev(%s) eid index(%d)", iter.first.first.c_str(),
                             iter.first.second);
                UnInitInner();
                return UBS_MALLOC_FAILED;
            }
        }
        auto result = tmp_poller->Start(start_options);
        if (UNLIKELY(result != UBS_OK)) {
            UBS_VLOG_ERR("Failed to start agg poller for dev(%s) eid index(%d)", iter.first.first.c_str(),
                         iter.first.second);
            UnInitInner();
            return UBS_ERROR;
        }
        /* emplace poller */
        pollers_.emplace(ctx->Id(), tmp_poller);
        UBS_VLOG_TRACE("Create agg poller for dev(%s) eid index(%d)", iter.first.first.c_str(), iter.first.second);
    }

    UBS_VLOG_TRACE("Init without thread finished, poller size(%d)", pollers_.size());
    return UBS_OK;
}

void UrmaAggPollerSet::UnInit() noexcept
{
    UBS_VLOG_TRACE("Enter");
    std::lock_guard<std::mutex> guard(mutex_);
    if (!inited_) {
        UBS_VLOG_DEBUG("Not initialized");
        return;
    }

    UnInitInner();

    inited_ = false;
    UBS_VLOG_TRACE("Un-initialized");
    return;
}

void UrmaAggPollerSet::UnInitInner() noexcept
{
    for (auto &item : pollers_) {
        if (item.second != nullptr) {
            item.second->Stop();
        }
    }

    pollers_.clear();
    UBS_VLOG_TRACE("un-inited, poller count(%d)", pollers_.size());
}

Result UrmaAggPollerSet::SelectJfc(const UrmaContextPtr &ctx, UrmaJfcPtr &out) noexcept
{
    if (UNLIKELY(ctx == nullptr)) {
        UBS_VLOG_ERR("Invalid param ctx, which is null");
        return UBS_INVALID_PARAM;
    }

    auto device_name = ctx->Device() != nullptr ? ctx->Device()->DeviceName() : "";

    std::lock_guard<std::mutex> guard(mutex_);
    auto iter = pollers_.find(ctx->Id());
    if (UNLIKELY(iter == pollers_.end())) {
        UBS_VLOG_ERR("Failed to select jfc, there is NO jfc created for context id(%d) dev(%s) eid index(%d)",
                     ctx->Id(), device_name.c_str(), ctx->EidInfo().eid_index);
        return UBS_ERROR;
    }

    if (UNLIKELY(iter->second == nullptr)) {
        UBS_VLOG_ERR("Failed to select jfc, there is NO poller created for context id(%d) dev(%s) eid index(%d)",
                     ctx->Id(), device_name.c_str(), ctx->EidInfo().eid_index);
        return UBS_ERROR;
    }

    return iter->second->SelectJfc(out);
}

} // namespace urma
} // namespace ubs
} // namespace ock