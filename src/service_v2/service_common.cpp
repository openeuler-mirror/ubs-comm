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

#include "service_common.h"

#include <cstdint>
#include "securec.h"
#include "hcom_def.h"
#include "net_common.h"
#include "hcom_service_def.h"
namespace ock {
namespace hcom {

Callback* HcomServiceGlobalObject::gEmptyCallback = nullptr;
bool HcomServiceGlobalObject::gInited = false;

SerResult SerConnInfo::Serialize(SerConnInfo &connInfo, const std::string &payload, std::string &out)
{
    std::string strInfo;
    if (!connInfo.ToString(strInfo)) {
        NN_LOG_ERROR("Failed to generate connect info to string");
        return SER_ERROR;
    }

    out.clear();
    out.reserve(strInfo.size() + payload.size());
    out.append(strInfo);
    out.append(payload);
    return SER_OK;
}

SerResult SerConnInfo::Deserialize(const std::string &payload, SerConnInfo &connInfo, std::string &userPayload)
{
    if (NN_UNLIKELY(!HexStringToBuff(payload, sizeof(SerConnInfo), &connInfo))) {
        NN_LOG_ERROR("Failed to parse connection info");
        return SER_INVALID_PARAM;
    }

    if (NN_UNLIKELY(!connInfo.Validate())) {
        NN_LOG_ERROR("Failed to validate connection info");
        return SER_INVALID_PARAM;
    }

    uint32_t connInfoStrSize = sizeof(SerConnInfo) * 2;
    userPayload.clear();
    userPayload.append(payload.substr(connInfoStrSize, payload.size() - connInfoStrSize));

    return NN_OK;
}

SerResult HcomServiceGlobalObject::Initialize()
{
    if (gInited) {
        return SER_OK;
    }

    gEmptyCallback = NewPermanentCallback([](UBSHcomServiceContext &context) {}, std::placeholders::_1);
    if (NN_UNLIKELY(gEmptyCallback == nullptr)) {
        NN_LOG_ERROR("Build empty callback failed");
        return SER_NEW_OBJECT_FAILED;
    }

    gInited = true;
    return SER_OK;
}

void HcomServiceGlobalObject::UnInitialize()
{
    if (!gInited) {
        return;
    }
    if (gEmptyCallback != nullptr) {
        delete gEmptyCallback;
        gEmptyCallback = nullptr;
    }
    gInited = false;
}

void HcomServiceGlobalObject::BuildTimeOutCtx(UBSHcomServiceContext &ctx)
{
    ctx.mCh.Set(nullptr);
    ctx.mResult = SER_TIMEOUT;
    ctx.mEpIdxInCh = 0;
    ctx.mSeqNo = 0;
    ctx.mDataType = UBSHcomServiceContext::INVALID_DATA;
    ctx.mDataLen = 0;
    ctx.mData = nullptr;
    ctx.mOpType = UBSHcomRequestContext::NN_INVALID_OP_TYPE;
    ctx.mOpCode = NN_NO1024;
}

void HcomServiceGlobalObject::BuildBrokenCtx(UBSHcomServiceContext &ctx)
{
    ctx.mCh.Set(nullptr);
    ctx.mResult = SER_NOT_ESTABLISHED;
    ctx.mEpIdxInCh = 0;
    ctx.mSeqNo = 0;
    ctx.mDataType = UBSHcomServiceContext::INVALID_DATA;
    ctx.mDataLen = 0;
    ctx.mData = nullptr;
    ctx.mOpType = UBSHcomRequestContext::NN_INVALID_OP_TYPE;
    ctx.mOpCode = NN_NO1024;
}

bool HcomConnectingEpInfo::AllEPBroken(uint16_t index)
{
    std::lock_guard<std::mutex> lockerEp(mLock);

    if (NN_UNLIKELY(index >= mEpVector.size()) || NN_UNLIKELY(index >= CHANNEL_EP_MAX_NUM)) {
        NN_LOG_ERROR("Invalid ep index " << index << ", ep size is " << mEpVector.size());
        return false;
    }

    mEpState[index].Set(NEP_BROKEN);

    for (uint64_t i = 0; i < mEpVector.size(); i++) {
        if (NN_UNLIKELY(mEpVector[i] == nullptr)) {
            continue;
        }
        if (!mEpState[i].Compare(NEP_BROKEN)) {
            NN_LOG_WARN("Failed to check all ep state broken, ep id " << mEpVector[i]->Id());
            return false;
        }
    }

    auto ret = mConnState.CAS(ConnectingEpState::NEW_EP, ConnectingEpState::EP_BROKEN);
    if (NN_UNLIKELY(!ret)) {
        NN_LOG_ERROR("Failed to validate ep state by generate channel, state " <<
            static_cast<uint8_t>(mConnState.Get()));
    }

    return ret;
}

bool HcomConnectingEpInfo::Compare(const SerConnInfo &info) const
{
    if (NN_UNLIKELY(mConnInfo.version != info.version)) {
        NN_LOG_ERROR("New connect version " << info.version << " is different from stored version " << info.version);
        return false;
    }

    if (NN_UNLIKELY(mConnInfo.channelId != info.channelId)) {
        NN_LOG_ERROR("New connect channelId " << info.channelId << " is different from stored channelId " <<
            info.channelId);
        return false;
    }

    if (NN_UNLIKELY(mConnInfo.policy != info.policy)) {
        NN_LOG_ERROR("New connect policy " << static_cast<uint8_t>(mConnInfo.policy)
            << " different from stored policy " << static_cast<uint8_t>(info.policy));
        return false;
    }

    if (NN_UNLIKELY(info.index != mEpVector.size())) {
        NN_LOG_ERROR("Failed to validate sequence, connect index " << info.index << " , already ep size " <<
            mEpVector.size());
        return false;
    }

    if (NN_UNLIKELY(mConnInfo.options.linkCount != info.options.linkCount)) {
        NN_LOG_ERROR("New connect linkCount " << mConnInfo.options.linkCount <<" is different from stored connect "
            << info.options.linkCount);
        return false;
    }

    if (NN_UNLIKELY(mConnInfo.options.cbType != info.options.cbType)) {
        NN_LOG_ERROR("New connect cbType " << static_cast<uint8_t>(mConnInfo.options.cbType) <<
            "is different from stored connect " << static_cast<uint8_t>(info.options.cbType));
        return false;
    }

    if (NN_UNLIKELY(mConnInfo.options.clientGroupId != info.options.clientGroupId)) {
        NN_LOG_ERROR("New connect clientGroupId " << static_cast<uint8_t>(mConnInfo.options.clientGroupId)
            << "is different from stored connect " << static_cast<uint8_t>(info.options.clientGroupId));
        return false;
    }

    if (NN_UNLIKELY(mConnInfo.options.serverGroupId != info.options.serverGroupId)) {
        NN_LOG_ERROR("New connect serverGroupId " << static_cast<uint8_t>(mConnInfo.options.serverGroupId)
            << "is different from stored connect " << static_cast<uint8_t>(info.options.serverGroupId));
        return false;
    }
    return true;
}

uint64_t HcomConnectTimestamp::GetRemoteTimestamp(int16_t timeOutSecond) const
{
    if (timeOutSecond <= 0) {
        return 0;
    }
     // V2 rndv用户在recv的时候就判断超时，所以需要用deltaTimUs/2 去计算remote的超时时间
    uint64_t remoteCurTime = NetMonotonic::TimeUs() - localTimeUs + remoteTimeUs - (deltaTimeUs / NN_NO2);
    return remoteCurTime + timeOutSecond * NN_NO1000000;
}

bool HcomServiceRndvMessage::IsTimeout() const
{
    if (timestamp == 0) {
        return false;
    }
    return NetMonotonic::TimeUs() >= timestamp;
}
}
}