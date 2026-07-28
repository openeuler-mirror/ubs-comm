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
#include "urma_jetty_pool.h"

namespace ock {
namespace ubs {
namespace urma {
Result UrmaJettyPool::VerifyOptions() noexcept
{
    /* only verify JFS and JFR queue depth */
    if (options_.jfr_depth == 0 || options_.jfr_depth > 32 * SIZE_1K) {
        UBS_VLOG_ERR("Invalid jfr queue depth %d, which be between 1~32K");
        return UBS_INVALID_PARAM;
    }

    if (options_.jfs_depth == 0 || options_.jfs_depth > 32 * SIZE_1K) {
        UBS_VLOG_ERR("Invalid jfs queue depth %d, which be between 1~32K");
        return UBS_INVALID_PARAM;
    }

    /* no need to check the following options for dedicated mode */
    if (options_.pool_type == PT_DEDICATED) {
        return UBS_OK;
    }

    if (options_.init_jetty_count <= 2) {
        UBS_VLOG_ERR("Invalid init jetty count %d, which be >=2");
        return UBS_INVALID_PARAM;
    }

    if (options_.init_jetty_count > options_.jetty_count_cap) {
        UBS_VLOG_ERR("Invalid init jetty count %d, which be <= %d", options_.jetty_count_cap);
        return UBS_INVALID_PARAM;
    }

    if (options_.max_jetty_count != options_.init_jetty_count) {
        UBS_VLOG_ERR("Invalid max jetty count %d", options_.max_jetty_count);
        return UBS_INVALID_PARAM;
    }

    if (options_.jfr_count != options_.init_jetty_count) {
        UBS_VLOG_ERR("Invalid jfr count %d", options_.jfr_count);
        return UBS_INVALID_PARAM;
    }

    return UBS_OK;
}

Result UrmaJettyPool::Init() noexcept
{
    UBS_VLOG_DEBUG("enter");

    std::lock_guard<std::mutex> lk(mutex_);
    if (inited_) {
        UBS_VLOG_DEBUG("Jrma jetty pool already initialized");
        return UBS_OK;
    }

    auto result = VerifyOptions();
    if (result != UBS_OK) {
        UBS_VLOG_ERR("Failed to initialize Urma Jetty pool, options are invalid");
        return result;
    }

    if (options_.pool_type == PT_SHARED) {
        result = CreateJfrs();
        if (result != UBS_OK) {
            UnInitInner();
            UBS_VLOG_ERR("Failed to create Urma Jetty pool, as create jfrs failed");
            return result;
        }

        result = CreateJettys();
        if (result != UBS_OK) {
            UnInitInner();
            UBS_VLOG_ERR("Failed to create Urma Jetty pool, as create jettys failed");
            return result;
        }
    }

    inited_ = true;
    UBS_VLOG_DEBUG("Urma Jetty pool is initialized");
    return UBS_OK;
}

void UrmaJettyPool::UnInitInner() noexcept
{
    DestroyJettys(); /* destroy jetty first */
    DestroyJfrs();   /* must destroy jfr after jetty, as jetty refer to jfr */
}

void UrmaJettyPool::UnInit() noexcept
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!inited_) {
        return;
    }

    UnInitInner();

    inited_ = false;
}

Result UrmaJettyPool::CreateJfrs() noexcept
{
    /* no need to create jfr for dedicated type */
    if (options_.pool_type == PT_DEDICATED) {
        return UBS_OK;
    }

    UBS_VLOG_ERR("Not implemented");
    return UBS_ERROR;
}

void UrmaJettyPool::DestroyJfrs() noexcept
{
    if (jettys_.empty()) {
        jfrs_.clear();
    }
}

Result UrmaJettyPool::CreateJettys() noexcept
{
    if (options_.pool_type == PT_DEDICATED) {
        return UBS_OK;
    }

    UBS_VLOG_ERR("Not implemented");
    return UBS_ERROR;
}

void UrmaJettyPool::DestroyJettys() noexcept
{
    jettys_.clear();
}

Result UrmaJettyPool::GetJetty(const UrmaJfcPtr &jfc, uint32_t flags, UrmaJettyPtr &jetty) noexcept
{
    (void)flags;

    std::lock_guard<std::mutex> lk(mutex_);
    if (UNLIKELY(!inited_)) {
        UBS_VLOG_ERR("Not initialized");
        return UBS_ERROR;
    }

    if (options_.pool_type == PT_DEDICATED) {
        if (UNLIKELY(jfc == nullptr)) {
            UBS_VLOG_ERR("Invalid param, jfc is nullptr");
            return UBS_INVALID_PARAM;
        }

        if (UNLIKELY(jfc->Context() == nullptr)) {
            UBS_VLOG_ERR("Invalid param, urma context in jfc is nullptr");
            return UBS_INVALID_PARAM;
        }

        auto context = jfc->Context();
        UrmaJfsPtr jfs;
        urma_jfs_cfg_t jfs_cfg = context->CreateJfsCfg(options_.jfs_depth, options_.transport_mode);
        if (context->CreateJfs(jfs_cfg, jfc, jfs) != UBS_OK) {
            UBS_VLOG_ERR("Create URMA JFS failed");
            return UBS_ERROR;
        }

        urma_jfr_cfg_t jfr_cfg =
            context->CreateJfrCfg(options_.jfr_depth, options_.transport_mode, Func::SecureRandUInt32());
        UrmaJfrPtr jfr;
        if (context->CreateJfr(jfr_cfg, jfc, jfr) != UBS_OK) {
            UBS_VLOG_ERR("Create URMA JFR failed\n");
            return UBS_ERROR;
        }

        urma_jetty_cfg_t jetty_cfg = context->CreateJettyCfg();
        if (context->CreateJetty(jetty_cfg, options_.tp_type, jfs, jfr, jetty) != UBS_OK) {
            UBS_VLOG_ERR("Create URMA Jetty failed\n");
            return UBS_ERROR;
        }

        return UBS_OK;
    }

    // TODO
    UBS_VLOG_ERR("Not implemented");
    return UBS_ERROR;
}

void UrmaJettyPool::ReturnJetty(const UrmaJettyPtr &jetty) noexcept
{
    // TODO
}
} // namespace urma
} // namespace ubs
} // namespace ock