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

SerResult UBSHcomServiceContext::CopyData(void *data, uint32_t dataLen)
{
    if (NN_UNLIKELY(mData != nullptr)) {
        free(mData);
        mData = nullptr;
    }
    mData = malloc(dataLen);
    if (NN_UNLIKELY(mData == nullptr)) {
        NN_LOG_ERROR("Invalid msg size " << dataLen << ", alloc failed");
        return SER_NEW_OBJECT_FAILED;
    }
    if (NN_UNLIKELY(memcpy_s(mData, dataLen, data, dataLen) != SER_OK)) {
        free(mData);
        mData = nullptr;
        NN_LOG_ERROR("Failed to copy data");
        return SER_ERROR;
    }

    mDataType = MEM_POOL_DATA;
    return SER_OK;
}

SerResult UBSHcomServiceContext::Clone(UBSHcomServiceContext &newOne, const UBSHcomServiceContext &oldOne,
                                       bool copyData)
{
    newOne = oldOne;

    if (copyData && oldOne.mDataLen > NN_NO0) {
        newOne.mDataLen = oldOne.mDataLen;
        return newOne.CopyData(oldOne.mData, oldOne.mDataLen);
    } else {
        newOne.mDataType = INVALID_DATA;
        newOne.mDataLen = NN_NO0;
        newOne.mData = nullptr;
    }
    return SER_OK;
}
} // namespace hcom
} // namespace ock