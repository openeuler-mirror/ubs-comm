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
#include <csignal>

#include "urma_poller_agg.h"

namespace ock {
namespace ubs {
namespace urma {

Result UrmaDedicatedJfcAggPoller::Start(const UrmaAppPollerStartOptions &options) noexcept
{
    if (jfce_ != nullptr) {
        UBS_VLOG_INFO("Already started");
        return UBS_OK;
    }

    // TODO

    return UBS_OK;
}

void UrmaDedicatedJfcAggPoller::Stop() noexcept
{
    if (jfce_ == nullptr) {
        UBS_VLOG_INFO("Not started");
        return;
    }

    jfc_vector_.clear();

    jfce_ = nullptr;
}

UrmaJfcePtr UrmaDedicatedJfcAggPoller::Jfce() const noexcept
{
    return jfce_;
}

Result UrmaDedicatedJfcAggPoller::SelectJfc(UrmaJfcPtr &jfc) noexcept
{
    if (UNLIKELY(jfce_ == nullptr)) {
        UBS_VLOG_INFO("Not started");
        return UBS_ERROR;
    }

    // TODO

    return UBS_OK;
}

/****************************************************************/
UrmaJfcePtr UrmaSharedJfcAggPollerWithThread::Jfce() const noexcept
{
    return jfce_;
}

Result UrmaSharedJfcAggPollerWithThread::SelectJfc(UrmaJfcPtr &jfc) noexcept
{
    if (UNLIKELY(jfce_ == nullptr)) {
        UBS_VLOG_INFO("Not started");
        return UBS_ERROR;
    }

    // TODO

    return UBS_OK;
}

Result UrmaSharedJfcAggPollerWithThread::Start(const UrmaAppPollerStartOptions &options) noexcept
{
    if (started_.load()) {
        UBS_VLOG_DEBUG("Already started");
        return UBS_OK;
    }

    Result result = UBS_OK;
    if (run_in_thread_) {
        result = StartWithThread();
    } else {
        result = StartWithoutThread();
    }

    if (result != UBS_OK) {
        UBS_VLOG_ERR("Failed to start agg poller, result(%d)", result);
        return result;
    }

    UBS_VLOG_TRACE("agg poller started");
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
    if (run_in_thread_) {
        // TODO 后续改为jfr消息触发
        struct sigaction sa = {0};
        sa.sa_handler = [](int) {
        };
        sa.sa_flags = 0;
        sigaction(SIGUSR1, &sa, NULL);

        pthread_kill(thread_.native_handle(), SIGUSR1);

        stopping_ = true;
        if (thread_.joinable()) {
            thread_.join();
        }
    }
}

Result UrmaSharedJfcAggPollerWithThread::StartWithThread() noexcept
{
    if (cqe_handler_ == nullptr) {
        UBS_VLOG_ERR("Failed to start urma agg poller %d as cqe_handler is nullptr", index_);
        return UBS_ERROR;
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

Result UrmaSharedJfcAggPollerWithThread::StartWithoutThread() noexcept
{
    started_.store(true);
    return UBS_OK;
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
    // TODO
}

void UrmaSharedJfcAggPollerWithThread::EventPoll() noexcept
{
    // TODO
}

/* for agg poller set */
Result UrmaAggPollerSet::VerifyOptions() noexcept
{
    if (options_.poller_count_per_context > 16L) {
        UBS_VLOG_ERR("Option poller_count_per_context(%d) is valid, which should be <=16",
                     options_.poller_count_per_context);
        return UBS_INVALID_PARAM;
    }

    if (!options_.run_in_thread) {
        options_.create_dedicated_jfc = true;
        UBS_VLOG_DEBUG("Set option the create_dedicated_jfc to %d", options_.create_dedicated_jfc);
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
        /* case1: for async poller, which is running in thread */
        if (options_.run_in_thread) {
            /* create jfc */
            for (uint16_t i = 0; i < options_.poller_count_per_context; i++) {
                urma_jfc_cfg_t jfc_cfg = ctx->CreateJfcCfg(options_.jfc_depth);
                UrmaJfcPtr jfc;
                if (ctx->CreateJfc(jfc_cfg, options_.polling_type, jfc) != UBS_OK) {
                    UBS_VLOG_ERR("Failed to create UrmaJfc with dev(%s) eid index(%d)", iter.first.first.c_str(),
                                 iter.first.second);
                    UnInitInner();
                    return UBS_ERROR;
                }

                jfc_vector.emplace_back(jfc);
            }

            /* create poller */
            tmp_poller = new (std::nothrow) UrmaSharedJfcAggPollerWithThread();
            if (UNLIKELY(tmp_poller == nullptr)) {
                UBS_VLOG_ERR("Failed to create agg poller for dev(%s) eid index(%d)", iter.first.first.c_str(),
                             iter.first.second);
                UnInitInner();
                return UBS_MALLOC_FAILED;
            }

            /* start poller */
            UrmaAppPollerStartOptions start_options;
            start_options.runInThread = true;
            start_options.jfce = nullptr;
            start_options.handler = options_.cqe_handler;
            start_options.index = index_creator_++;
            start_options.jfcVector.swap(jfc_vector);
            start_options.busyPoll = options_.polling_type == BUSY_POLLING;

            auto result = tmp_poller->Start(start_options);
            if (UNLIKELY(result != UBS_OK)) {
                UBS_VLOG_ERR("Failed to start agg poller for dev(%s) eid index(%d)", iter.first.first.c_str(),
                             iter.first.second);
                UnInitInner();
                return UBS_ERROR;
            }
        } else {
            /*case2: for sync poller */
            /* create jfce */
            UrmaJfcePtr jfce;
            auto result = ctx->CreateJfce(jfce);
            if (UNLIKELY(result != UBS_OK)) {
                UBS_VLOG_ERR("Failed to create jfce for dev(%s) eid index(%d)", iter.first.first.c_str(),
                             iter.first.second);
                UnInitInner();
                return UBS_ERROR;
            }

            /* create poller */
            tmp_poller = new (std::nothrow) UrmaDedicatedJfcAggPoller();
            if (UNLIKELY(tmp_poller == nullptr)) {
                UBS_VLOG_ERR("Failed to create agg poller for dev(%s) eid index(%d)", iter.first.first.c_str(),
                             iter.first.second);
                UnInitInner();
                return UBS_MALLOC_FAILED;
            }

            /* start poller */
            UrmaAppPollerStartOptions start_options;
            start_options.jfce = jfce;
            start_options.handler = options_.cqe_handler;
            start_options.index = index_creator_++;

            result = tmp_poller->Start(start_options);
            if (UNLIKELY(result != UBS_OK)) {
                UBS_VLOG_ERR("Failed to start agg poller for dev(%s) eid index(%d)", iter.first.first.c_str(),
                             iter.first.second);
                UnInitInner();
                return UBS_ERROR;
            }
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