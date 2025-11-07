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
#include "shm_channel.h"

namespace ock {
namespace hcom {
/* get channel fd queue size */
uint32_t ShmChannel::gQueueSizeCap = GetQueueCap();

HResult ShmChannel::Initialize()
{
    /* create data channel */
    ShmDataChannelOptions opt(mId, mSendDCBuckSize, mSendDCBuckCount, true);
    ShmDataChannelPtr dc = new (std::nothrow) ShmDataChannel(mName, opt, &mState);
    if (NN_UNLIKELY(dc == nullptr)) {
        NN_LOG_ERROR("Failed to new ShmDataChannel " << mName << ", probably out of memory");
        return SH_NEW_OBJECT_FAILED;
    }

    /* initialize channel */
    auto result = dc->Initialize();
    if (NN_UNLIKELY(result != SH_OK)) {
        NN_LOG_ERROR("Failed to init ShmDataChannel " << mName << ", result " << result);
        return result;
    }

    dc->IncreaseRef();
    mDataChannel = dc.Get();
    NN_LOG_INFO("shm channel " << mName << "," << mId << " initialized ");
    return SH_OK;
}

void ShmChannel::UnInitialize()
{
    mState.CAS(CH_NEW, CH_BROKEN);

    if (mDataChannel != nullptr) {
        mDataChannel->DecreaseRef();
        mDataChannel = nullptr;
    }

    if (mPeerDataChannel != nullptr) {
        mPeerDataChannel->DecreaseRef();
        mPeerDataChannel = nullptr;
    }

    if (mPeerEventQueue != nullptr) {
        mPeerEventQueue->DecreaseRef();
        mPeerEventQueue = nullptr;
    }
    NetFunc::NN_SafeCloseFd(mFd);
}

HResult ShmChannel::ValidateExchangeInfo(const ShmConnExchangeInfo &info)
{
    if (NN_UNLIKELY(info.qCapacity == 0 || info.qCapacity > NN_NO8192 || info.queueFd <= 0)) {
        NN_LOG_ERROR("Failed to change ShmChannel" << mName << ":" << mId <<
            " to ready as invalid queue capacity or fd from peer");
        return SH_PARAM_INVALID;
    }

    if (NN_UNLIKELY(info.GetQueueName().empty())) {
        NN_LOG_ERROR("Failed to change ShmChannel" << mName << ":" << mId <<
            " to ready as invalid queue name from peer");
        return SH_PARAM_INVALID;
    }

    if (NN_UNLIKELY(info.dcBuckCount == 0 || info.dcBuckSize == 0 || info.dcBuckCount > NN_NO65535 ||
        info.dcBuckSize > NET_SGE_MAX_SIZE)) {
        NN_LOG_ERROR("Failed to change ShmChannel" << mName << ":" << mId <<
            " to ready as invalid buck size or count from peer");
        return SH_PARAM_INVALID;
    }

    if (NN_UNLIKELY(info.GetDCName().empty())) {
        NN_LOG_ERROR("Failed to change ShmChannel" << mName << ":" << mId <<
            " to ready as invalid data channel name from peer");
        return SH_PARAM_INVALID;
    }

    if (NN_UNLIKELY(info.channelId == 0 || info.channelAddress == 0 || info.channelFd <= 0)) {
        NN_LOG_ERROR("Failed to change ShmChannel" << mName << ":" << mId <<
            " to ready as invalid data channel id, address or fd from peer");
        return SH_PARAM_INVALID;
    }

    return SH_OK;
}

HResult ShmChannel::ChangeToReady(const ShmConnExchangeInfo &info)
{
    NN_LOG_INFO("Try to change shm channel " << mName << ":" << mId << " to ready with ex info " << info.ToString());
    HResult result = SH_OK;
    if (NN_UNLIKELY((result = ValidateExchangeInfo(info)) != SH_OK)) {
        return result;
    }

    /* new eq handle for send msg to peer event queue of worker */
    ShmHandlePtr peerEqHandle = new (std::nothrow)
        ShmHandle(mName, info.GetQueueName(), mId, ShmEventQueue::MemSize(info.qCapacity), info.queueFd, false);
    if (NN_UNLIKELY(peerEqHandle.Get() == nullptr)) {
        NN_LOG_ERROR("Failed to new shmHandle in ShmChannel " << mName << ", probably out of memory");
        return SH_NEW_OBJECT_FAILED;
    }

    /* initialize event queue handle without ownership */
    if (NN_UNLIKELY((result = peerEqHandle->Initialize()) != SH_OK)) {
        NN_LOG_ERROR("Failed to change ShmChannel " << mName << ":" << mId << " to ready as result " << result);
        return result;
    }

    /* new event queue object */
    ShmEventQueuePtr queue = new (std::nothrow) ShmEventQueue(mName, info.qCapacity, peerEqHandle);
    if (NN_UNLIKELY(queue.Get() == nullptr)) {
        NN_LOG_ERROR("Failed to new event queue in ShmChannel " << mName << ", probably out of memory");
        return SH_NEW_OBJECT_FAILED;
    }

    /* initialize event queue */
    if (NN_UNLIKELY((result = queue->Initialize()) != SH_OK)) {
        NN_LOG_ERROR("Failed to change ShmChannel " << mName << ":" << mId << " to ready as result " << result);
        return result;
    }

    /* create peer data channel */
    ShmDataChannelOptions opt(info.channelId, info.dcBuckSize, info.dcBuckCount, info.channelFd, false);
    (void)opt.SetFileName(info.GetDCName());
    ShmDataChannelPtr peerDC = new (std::nothrow) ShmDataChannel(mName + ":peer", opt, &mState);
    if (NN_UNLIKELY(peerDC.Get() == nullptr)) {
        NN_LOG_ERROR("Failed to new data channel of peer in ShmChannel " << mName << ", probably out of memory");
        return SH_NEW_OBJECT_FAILED;
    }

    /* initialize dc */
    if (NN_UNLIKELY((result = peerDC->Initialize()) != SH_OK)) {
        NN_LOG_ERROR("Failed to change ShmChannel " << mName << ":" << mId << " to ready as result " << result);
        return result;
    }

    mPeerEventQueue = queue.Get();
    mPeerEventQueue->IncreaseRef();

    mPeerDataChannel = peerDC.Get();
    mPeerDataChannel->IncreaseRef();

    mPeerChId = info.channelId;

    mPeerEventPooling = info.mode == ShmPollingMode::SHM_EVENT_POLLING;

    mPeerChAddress = info.channelAddress;

    return SH_OK;
}
}
}