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
#include "hcom_service_context.h"

#include "net_monotonic.h"
#include "service_common.h"

namespace ock {
namespace hcom {

constexpr uint16_t S_TO_MS_MULTIPLIER = 1000;

UBSHcomServiceContext::UBSHcomServiceContext(const UBSHcomRequestContext &ctx, UBSHcomChannel *ch) : mCh(ch)
{
    mOpType = ctx.OpType();
    mResult = ctx.Result();
    Ep2ChanUpCtx epCtx(ctx.EndPoint()->UpCtx());
    mEpIdxInCh = epCtx.EpIdx();
    mSeqNo = ctx.Header().seqNo;

    if (ctx.Message() != nullptr) {
        mDataType = OUTER_DATA;
        mDataLen = ctx.Message()->DataLen();
        mData = ctx.Message()->Data();
    } else {
        mDataType = INVALID_DATA;
        mDataLen = 0;
        mData = nullptr;
    }

    mTimeoutTraceMs = 0;
    if (ctx.Header().timeout > 0) {
        uint64_t timoutMs = static_cast<uint64_t>(ctx.Header().timeout) * S_TO_MS_MULTIPLIER;
        // NetMonotonic::TimeMs() + timoutMs overflow needs system to run for more than 500 million years
        mTimeoutTraceMs = NetMonotonic::TimeMs() + timoutMs;
    }
    mOpCode = ctx.Header().opCode;
    mErrorCode = ctx.Header().errorCode;
}

}
}