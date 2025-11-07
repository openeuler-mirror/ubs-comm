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

#ifndef HCOM_UB_URMA_WRAPPER_JFC_H
#define HCOM_UB_URMA_WRAPPER_JFC_H
#ifdef UB_BUILD_ENABLED

#include "ub_common.h"
#include "ub_urma_wrapper_ctx.h"

namespace ock {
namespace hcom {

class UBJfc {
public:
    UBJfc(const std::string &name, UBContext *ctx, bool createCompletionChannel = false, uintptr_t work = 0)
        : mName(name), mCreateCompletionChannel(createCompletionChannel), mWork(work), mUBContext(ctx)
    {
        if (mUBContext != nullptr) {
            mUBContext->IncreaseRef();
        }

        OBJ_GC_INCREASE(UBJfc);
    }

    ~UBJfc()
    {
        UnInitialize();
        OBJ_GC_DECREASE(UBJfc);
    }

    inline void SetJfcCount(uint32_t value)
    {
        mJfcCount = (value < NN_NO1024) ? NN_NO1024 : value;
    }

    inline uint32_t GetCQCount()
    {
        return mJfcCount;
    }

    UResult Initialize();
    UResult UnInitialize();

    UResult ProgressV(urma_cr_t *cr, uint32_t &countInOut);
    UResult EventProgressV(urma_cr_t *cr, uint32_t &countInOut, int32_t timeoutInMs = NN_NO500);

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    UResult CreatePollingCq();
    UResult CreateEventCq();
    std::string mName;
    uint32_t mJfcCount = JFC_COUNT;
    bool mCreateCompletionChannel = false;
    uintptr_t mWork = 0;
    UBContext *mUBContext = nullptr;
    urma_jfc_t *mUrmaJfc = nullptr;
    urma_jfce_t *mUrmaJfcEvent = nullptr;

    DEFINE_RDMA_REF_COUNT_VARIABLE;

    friend class UBJetty;
    friend class UBPublicJetty;
};
}
}
#endif
#endif // HCOM_UB_URMA_WRAPPER_JFC_H