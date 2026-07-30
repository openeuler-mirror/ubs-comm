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

#include "urma_poller.h"
#include "core/ubsocket_atomic_fd_set.h"
#include "core/ubsocket_socket.h"
#include "urma_socket.h"

namespace ock {
namespace ubs {
namespace urma {
std::atomic<uint16_t> UrmaPollerSet::INDEX_CREATOR{0};

Result UrmaPoller::Start() noexcept
{
    UBS_VLOG_DEBUG("Starting");
    if (started_.load()) {
        return UBS_OK;
    }

    if (jfc_ == nullptr) {
        UBS_VLOG_ERR("Failed to start urma poller %d as jfc is nullptr", index_);
        return UBS_ERROR;
    }

    if (cqe_handler_ == nullptr) {
        UBS_VLOG_ERR("Failed to start urma poller %d as cqe_handler is nullptr", index_);
        return UBS_ERROR;
    }

    thread_ = std::thread(&UrmaPoller::RunInThread, this);

    /* wait for 5 seconds to ensure the thread is started */
    for (uint16_t i = 0; i < 5000L; i++) {
        if (started_.load()) {
            break;
        } else {
            usleep(1000L);
        }
    }

    UBS_VLOG_DEBUG("poller %d started", index_);
    return UBS_OK;
}

void UrmaPoller::Stop() noexcept
{
    UBS_VLOG_DEBUG("Stopping");
    stopping_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }

    started_.store(false);
    UBS_VLOG_DEBUG("Stopped");
}

void UrmaPoller::RunInThread()
{
    started_.store(true);
    UBS_VLOG_DEBUG("urma poller thread <%d> is started", index_);
    std::string thread_name = "ubs_poller_" + std::to_string(index_);
    pthread_setname_np(pthread_self(), thread_name.c_str());

    /* hold the reference in poller thread */
    UrmaJfcPtr jfc = jfc_;
    if (UNLIKELY(jfc == nullptr)) {
        UBS_VLOG_INFO("urma poller thread <%d> exiting, as jfc is nullptr", index_);
        return;
    }

    if (jfc->PollingType() == EVENT_POLLING) {
        EventPoll();
    } else {
        BusyPoll();
    }

    UBS_VLOG_INFO("urma poller thread <%d> exiting", index_);
}

void UrmaPoller::EventPoll() noexcept
{
    constexpr int32_t POLL_BATCH_SIZE = 32;
    urma_cr_t crs[POLL_BATCH_SIZE]{};
    urma_jfc_t *ev_jfc;
    while (!stopping_) {
        try {
            auto cnt = UrmaApi::urma_wait_jfc(jfc_->JfceHandle(), 1, -1, &ev_jfc);
            if (cnt < 0 || (cnt == 1 && jfc_->Handle() != ev_jfc)) {
                UBS_VLOG_ERR("Failed to wait jfc cnt: %d", cnt);
                continue;
            }

            jfc_->Ack(1);
            bool first_time = true;
            while (true) {
                cnt = jfc_->Poll(POLL_BATCH_SIZE, crs);
                if (UNLIKELY(cnt == 0)) {
                    jfc_->Rearm(false);
                    break;
                }

                if (UNLIKELY(cnt < 0)) {
                    stopping_ = true;
                    UBS_VLOG_ERR("poll jfc failed return: %d", cnt);
                    break;
                }

                auto result = cqe_handler_(0, cnt, crs);
                UBS_VLOG_TRACE("cqe handler processed (%d) and return %d, continue polling", cnt, result);
                if (first_time) {
                    first_time = false;
                    jfc_->Rearm(false);
                }
            }
        } catch (...) {
            UBS_VLOG_ERR("Exception caught in UrmaPoller");
        }
    }
}

void UrmaPoller::BusyPoll() noexcept
{
    constexpr int32_t POLL_BATCH_SIZE = 32;
    urma_cr_t crs[POLL_BATCH_SIZE]{};
    while (!stopping_) {
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

Result UrmaPollerSet::Init(const UrmaPollerOptions &options) noexcept
{
    UBS_VLOG_DEBUG("Enter");
    std::lock_guard<std::mutex> lock(mutex_);
    if (inited_) {
        UBS_VLOG_DEBUG("Already inited");
        return UBS_OK;
    }

    auto result = VerifyOptions(options);
    if (result != UBS_OK) {
        UBS_VLOG_ERR("Failed to initialize urma pollers, as options is invalid");
        return result;
    }

    result = CreateUrmaPollers();
    if (result != UBS_OK) {
        /* destroy created pollers */
        UnInitInner();
        UBS_VLOG_ERR("Failed to create urma pollers as create indivial poller failed");
        return result;
    }

    inited_ = true;
    UBS_SLOG_DEBUG("Inited with " << options);
    return UBS_OK;
}

void UrmaPollerSet::UnInit() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!inited_) {
        return;
    }

    UnInitInner();

    inited_ = false;
}

void UrmaPollerSet::UnInitInner() noexcept
{
    for (auto &poller : pollers_) {
        if (poller != nullptr) {
            poller->Stop();
        }
    }

    pollers_.clear();
    rr_select_index_.store(0, std::memory_order_relaxed);
}

Result UrmaPollerSet::SelectPoller(UrmaJfcPtr &jfc) noexcept
{
    if (UNLIKELY(!inited_)) {
        UBS_VLOG_ERR("UrmaPollerSet not inited");
        return UBS_ERROR;
    }

    auto index = rr_select_index_.fetch_add(1, std::memory_order_relaxed);
    index = index % options_.poller_count;

    if (UNLIKELY(pollers_[index] == nullptr)) {
        UBS_VLOG_ERR("Un-reachable path, poller at %d is null", index);
        return UBS_ERROR;
    }

    jfc = pollers_[index]->jfc_;

    if (UNLIKELY(jfc == nullptr)) {
        UBS_VLOG_ERR("Un-reachable path, jfc of poller at %d is null", index);
        return UBS_ERROR;
    }

    return UBS_OK;
}

Result UrmaPollerSet::CreateUrmaPollers() noexcept
{
    pollers_.reserve(options_.poller_count);

    /* create pollers */
    for (uint16_t i = 0; i < options_.poller_count; i++) {
        urma_jfc_cfg_t jfc_cfg = options_.context_->CreateJfcCfg(options_.jfc_depth);
        UrmaJfcPtr jfc;
        if (options_.context_->CreateJfc(jfc_cfg, options_.polling_type, jfc) != UBS_OK) {
            UBS_VLOG_ERR("Failed to create UrmaJfc");
            return UBS_ERROR;
        }

        auto index = INDEX_CREATOR.fetch_add(1, std::memory_order_relaxed);
        pollers_.emplace_back(MakeRef<UrmaPoller>(index, options_.cqe_handler, jfc));
    }

    for (auto &poller : pollers_) {
        if (poller->Start() != UBS_OK) {
            UBS_VLOG_ERR("Failed to start poller");
            return UBS_ERROR;
        }
    }

    return UBS_OK;
}

Result UrmaPollerSet::VerifyOptions(const UrmaPollerOptions &options) noexcept
{
    options_ = options;
    if (options_.context_ == nullptr) {
        UBS_VLOG_ERR("UrmaPollerOptions context is null");
        return UBS_ERROR;
    }

    if (options_.cqe_handler == nullptr) {
        UBS_VLOG_ERR("UrmaPollerOptions cqe_handler is null");
        return UBS_ERROR;
    }

    if (options_.jfc_depth < SIZE_4K) {
        options_.jfc_depth = SIZE_4K;
    }

    if (options_.poller_count == 0) {
        options_.poller_count = 2;
        UBS_VLOG_WARN("UrmaPollerOptions poll_count is 0, set to 2");
    }

    if (options_.poller_count > 128) {
        options_.poller_count = 128;
        UBS_VLOG_WARN("UrmaPollerOptions poll_count is larger than 128, set to 128");
    }

    return UBS_OK;
}
} // namespace urma
} // namespace ubs
} // namespace ock