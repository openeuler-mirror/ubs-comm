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
#ifndef UBS_COMM_URMA_JETTY_POOL_H
#define UBS_COMM_URMA_JETTY_POOL_H

#include "urma_context_pool.h"
#include "urma_setting.h"
#include "urma_wrapper.h"

namespace ock {
namespace ubs {
namespace urma {

enum UrmaJettyPoolType {
    PT_SHARED = 0, /* shared jetty pool */
    PT_DEDICATED,  /* dedicated jetty pool, i.e. no pool */

    PT_COUNT
};

struct UrmaJettyPoolOptions {
    UrmaJettyPoolType pool_type = PT_DEDICATED;        /* pool type */
    uint16_t init_jetty_count = 2;                     /* init jetty count */
    uint16_t max_jetty_count = 2;                      /* max jetty count */
    const uint16_t jetty_count_cap = SIZE_4K;          /* cap of jetty */
    uint16_t jfr_count = 2;                            /* jfr count, if equals to jfr count, then no shared jfr */
    uint16_t jfs_depth = 256;                          /* queue depth of jfs */
    uint16_t jfr_depth = 256;                          /* queue depth of jfr */
    urma_transport_mode_t transport_mode = URMA_TM_RM; /* trans mode */
    urma_tp_type_t tp_type = URMA_CTP;                 /* rtp/ctp/utp */
    UrmaContextPoolPtr context_pool = nullptr;         /* urma context */
};

class UrmaJettyPool : public Referable {
public:
    UrmaJettyPool(const UrmaJettyPoolOptions &options) noexcept : options_(options)
    {
        dynamic_expand_ = options_.max_jetty_count > options_.init_jetty_count;
    }

    ~UrmaJettyPool() noexcept override
    {
        UnInit();
    }

    Result Init() noexcept;
    void UnInit() noexcept;

    Result GetJetty(const UrmaJfcPtr &jfc, uint32_t flags, UrmaJettyPtr &jetty) noexcept;
    void ReturnJetty(const UrmaJettyPtr &jetty) noexcept;

private:
    void UnInitInner() noexcept;
    Result VerifyOptions() noexcept;
    Result CreateJfrs() noexcept;
    void DestroyJfrs() noexcept;
    Result CreateJettys() noexcept;
    void DestroyJettys() noexcept;

private:
    std::mutex mutex_;
    uint32_t jetty_rr_index_ = 0;      /* next index for round-robin policy */
    bool inited_ = false;              /* inited or not */
    bool dynamic_expand_ = false;      /* dynamic expand jetty or not */
    UrmaJettyPoolOptions options_;     /* options */
    std::vector<UrmaJfrPtr> jfrs_;     /* jfr vector */
    std::vector<UrmaJettyPtr> jettys_; /* jetty vector */
};
using UrmaJettyPoolPtr = Ref<UrmaJettyPool>;

} // namespace urma
} // namespace ubs
} // namespace ock

#endif //UBS_COMM_URMA_JETTY_POOL_H
