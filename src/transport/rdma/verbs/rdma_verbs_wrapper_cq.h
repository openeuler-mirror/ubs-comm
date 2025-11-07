/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef HCOM_RDMA_VERBS_WRAPPER_CQ_H
#define HCOM_RDMA_VERBS_WRAPPER_CQ_H
#ifdef RDMA_BUILD_ENABLED

#include "rdma_verbs_wrapper_ctx.h"

namespace ock {
namespace hcom {

struct RDMACqPollResult {
    uint64_t context = 0;
    uint32_t dataSize = 0;
    enum ibv_wc_status status = IBV_WC_SUCCESS;
};

class RDMACq {
public:
    RDMACq(const std::string &name, RDMAContext *ctx, bool createCompletionChannel = false, uintptr_t work = 0)
        : mName(name), mCreateCompletionChannel(createCompletionChannel), mWork(work), mRDMAContext(ctx)
    {
        if (mRDMAContext != nullptr) {
            mRDMAContext->IncreaseRef();
        }

        OBJ_GC_INCREASE(RDMACq);
    }

    ~RDMACq()
    {
        UnInitialize();
        OBJ_GC_DECREASE(RDMACq);
    }

    inline void SetCQCount(uint32_t value)
    {
        mCQCount = (value < NN_NO1024) ? NN_NO1024 : value;
    }

    inline uint32_t GetCQCount()
    {
        return mCQCount;
    }

    RResult Initialize();
    RResult UnInitialize();

    RResult ProgressV(struct ibv_wc *wc, int &countInOut);
    RResult EventProgressV(struct ibv_wc *wc, int &countInOut, int32_t timeoutInMs = NN_NO500);

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    RResult CreatePollingCq();
    RResult CreateEventCq();
    std::string mName;
    uint32_t mCQCount = CQ_COUNT;
    bool mCreateCompletionChannel = false;
    uintptr_t mWork = 0;
    RDMAContext *mRDMAContext = nullptr;
    ibv_cq *mCompletionQueue = nullptr;
    ibv_comp_channel *mCompletionChannel = nullptr;

    DEFINE_RDMA_REF_COUNT_VARIABLE;

    friend class RDMAQp;
};
}
}
#endif
#endif // HCOM_RDMA_VERBS_WRAPPER_CQ_H