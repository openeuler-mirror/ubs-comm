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

#include "net_heartbeat.h"
#ifdef RDMA_BUILD_ENABLED
#include "net_rdma_async_endpoint.h"
#endif

#ifdef UB_BUILD_ENABLED
#include "net_ub_endpoint.h"
#include "ub_worker.h"
#endif

namespace ock {
namespace hcom {
NetHeartbeat::NetHeartbeat(UBSHcomNetDriver *driver, uint16_t heartBeatIdleTime, uint16_t heartBeatProbeInterval)
    : mDriver(driver),
      mHeartBeatIdleTime(heartBeatIdleTime),
      mHeartBeatProbeInterval(heartBeatProbeInterval * NN_NO1000000)
{
    if (mDriver != nullptr) {
        mDriver->IncreaseRef();
    }

    if (mHeartBeatProbeInterval == 0) {
        // If the user sets mHeartBeatProbeInterval to 0, change it to 5000 instead(5ms).
        mHeartBeatProbeInterval = 5000;
    }
}
NetHeartbeat::~NetHeartbeat()
{
    if (mDriver != nullptr) {
        mDriver->DecreaseRef();
        mDriver = nullptr;
    }
}

NResult NetHeartbeat::Start()
{
    if (mDriver == nullptr) {
        NN_LOG_ERROR("Failed to start because driver is null");
        return NN_INVALID_PARAM;
    }
    NResult result = NN_OK;
    if ((result = mDriver->CreateMemoryRegion(NN_NO64 * NN_NO1024, mHBLocalOpMr)) != NN_OK) {
        NN_LOG_ERROR("Failed to create mr for local HB in driver " << mDriver->Name() << ", result " << result);
        return result;
    }

    if ((result = mDriver->CreateMemoryRegion(NN_NO64 * NN_NO1024, mHBRemoteOpMr)) != NN_OK) {
        NN_LOG_ERROR("Failed to create mr for remote HB in driver " << mDriver->Name() << ", result " << result);
        mDriver->DestroyMemoryRegion(mHBLocalOpMr);
        return result;
    }

    mNeedStopHb = false;
    std::thread tmpThread(&NetHeartbeat::RunInHbThread, this);
    mHbThread = std::move(tmpThread);

    while (!mHBStarted.load()) {
        usleep(NN_NO10);
    }
    return NN_OK;
}

void NetHeartbeat::Stop()
{
    mNeedStopHb = true;
    if (mHbThread.native_handle()) {
        mHbThread.join();
    }

    if (mDriver == nullptr) {
        return;
    }

    if (mHBLocalOpMr != nullptr) {
        mDriver->DestroyMemoryRegion(mHBLocalOpMr);
        mHBLocalOpMr.Set(nullptr);
    }

    if (mHBRemoteOpMr != nullptr) {
        mDriver->DestroyMemoryRegion(mHBRemoteOpMr);
        mHBRemoteOpMr.Set(nullptr);
    }
}

void NetHeartbeat::RunInHbThread()
{
    mHBStarted.store(true);
    NN_LOG_INFO("Heartbeat thread for driver " << mDriver->Name() << ", HCOMHb" << std::to_string(mDriver->GetId()) <<
        " started, idle time " << mHeartBeatIdleTime);

    /* set thread name */
    pthread_setname_np(pthread_self(), ("HCOMHb" + std::to_string(mDriver->GetId())).c_str());

    mTarSec = NetMonotonic::TimeSec() + mHeartBeatIdleTime;
    while (!mNeedStopHb) {
        mCurrentSec = NetMonotonic::TimeSec();
        while (mCurrentSec > mTarSec) {
            mTarSec = mCurrentSec + mHeartBeatProbeInterval / NN_NO1000000;
            DetectHbState();
        }
        usleep(mHeartBeatProbeInterval);
    }
    NN_LOG_INFO("Heartbeat thread for driver " << mDriver->Name() << ", HCOMHb" << std::to_string(mDriver->GetId()) <<
        " exiting");
    mHBStarted.store(false);
}

void NetHeartbeat::DetectHbState()
{
    if (mHBLocalOpMr.Get() == nullptr) {
        NN_LOG_ERROR("Failed to heart beat detection as related memory region is null in driver " << mDriver->Name());
        return;
    }

    UBSHcomNetTransRequest request = {};
    request.lAddress = GetNextLocalOpHBAddress();
    request.lKey = GetLocalOpHBKey();
    request.size = GetLocalOpHBMrSize();

    static thread_local std::unordered_map<uint64_t, UBSHcomNetEndpointPtr> endPointsCopy;
    endPointsCopy.reserve(NN_NO8192);
    endPointsCopy.clear();
    {
        std::lock_guard<std::mutex> locker(mDriver->mEndPointsMutex);
        for (auto &endPoint : mDriver->mEndPoints) {
            auto ep = endPoint.second.Get();
            if (ep != nullptr && ep->IsNeedSendHb()) {
                endPointsCopy.emplace(endPoint.first, endPoint.second);
            }
        }
    }

    for (auto &endPoint : endPointsCopy) {
        DetectSingleEpHbState(request, endPoint.second.Get());
    }

    endPointsCopy.clear();
}

NResult NetHeartbeat::SendTwoSideHeartBeat(UBSHcomNetEndpoint *endPoint)
{
    NResult result = NN_OK;
    char data;
    UBSHcomNetTransRequest req((void *)(&data), sizeof(data), 0);
    if (NN_UNLIKELY((result = endPoint->PostSend(HB_SEND_OP, req, 0)) != NN_OK)) {
        NN_LOG_ERROR("Endpoint " << endPoint->mId << " failed to post send request, result " << result);
        return result;
    }
    return NN_OK;
}

template <typename T, typename T1, typename T2>
NResult NetHeartbeat::SendHeartBeat(T *ep, T1 *driver, UBSHcomNetTransRequest &request, T2 opType)
{
    if (NN_UNLIKELY(!ep->mState.Compare(NEP_ESTABLISHED))) {
        NN_LOG_ERROR("Endpoint " << ep->mId << " is not established, state is " <<
            UBSHcomNEPStateToString(ep->mState.Get()));
        return NN_EP_NOT_ESTABLISHED;
    }

    if (NN_UNLIKELY(ep->GetQp() == nullptr)) {
        NN_LOG_ERROR("Endpoint " << ep->mId << " invalid endpoint");
        return NN_ERROR;
    }
#ifdef UB_BUILD_ENABLED
    if (driver->Protocol() == UBSHcomNetDriverProtocol::UBC) {
        auto ubcEp = dynamic_cast<NetUBAsyncEndpoint *>(ep);
        if (NN_UNLIKELY(ubcEp == nullptr)) {
            NN_LOG_ERROR("Invalid operation to dynamic cast");
            return NN_ERROR;
        }
        auto jetty = ubcEp->GetQp();
        if (jetty->mHBLocalMr == nullptr) {
            NN_LOG_WARN("Endpoint " << ep->mId << " HB mr freed already");
            return NN_ERROR;
        }
        request.lAddress = jetty->GetNextLocalHBAddress();
        request.lKey = jetty->GetLocalHBKey();
        request.srcSeg = jetty->mHBLocalMr->GetMemorySeg();
    }
#endif
    request.rAddress = ep->mRemoteHbAddress;
    request.upCtxSize = 0;
    request.rKey = ep->mRemoteHbKey;

    if (driver->ValidateMemoryRegion(request.lKey, request.lAddress, request.size) != NN_OK) {
        NN_LOG_ERROR("Endpoint " << ep->mId << " Invalid MemoryRegion or lkey");
        return NN_INVALID_LKEY;
    }
    auto worker = ep->GetWorker();
    if (NN_UNLIKELY(worker == nullptr)) {
        NN_LOG_ERROR("Endpoint " << ep->mId << " failed to get worker from group in PostWrite ");
        return NN_ERROR;
    }

    NResult result = NN_OK;
    if (NN_UNLIKELY((result = worker->PostWrite(ep->GetQp(), request, opType)) != NN_OK)) {
        NN_LOG_ERROR("Endpoint " << ep->mId << " failed to post write request, result " << result);
        return result;
    }
    return result;
}

template <typename T, typename T1, typename T2>
void NetHeartbeat::DetectSingleEpHbState(T *ep, T1 *driver, UBSHcomNetTransRequest &request, T2 opType)
{
    if (NN_UNLIKELY(ep == nullptr || driver == nullptr)) {
        NN_LOG_WARN("Invalid operation to dynamic cast");
        return;
    }

    NResult result = NN_OK;
    /* check if reach ep target hb time */
    if (!ep->checkTargetHbTime(mCurrentSec)) {
        return;
    }
    if (ep->HbCheckStateNormal()) {
        result = SendHeartBeat(ep, driver, request, opType);
        if (result == NN_OK) {
            return;
        }
        NN_LOG_WARN("Detect Ep id " << ep->Id() << " cannot send Hb, result " << result);
    }
    if (ep->HbBrokenEp()) {
        /* delay handle broken ep to prevent race condition with work polling cq thread */
        NN_LOG_WARN("Detect Ep id " << ep->Id() << " Hb state abnormal, call broken handle");
        driver->ProcessEpError(reinterpret_cast<uintptr_t>(ep));
    } else {
        /* set hb broken ep when detected first time */
        NN_LOG_WARN("Detect Ep id " << ep->Id() << " Hb state abnormal, set qp err and wait next probe to handle");
        ep->State().Set(NEP_BROKEN);
        ep->SetHbBrokenEp();
        if (NN_UNLIKELY(ep->GetQp() == nullptr)) {
            NN_LOG_ERROR("Endpoint " << ep->Id() << " failed to get qp");
            return;
        }
        ep->GetQp()->Stop();
    }
}

template <typename T>
void NetHeartbeat::DetectSingleEpHbState(NetUBAsyncEndpoint *ep, NetDriverUBWithOob *driver,
    UBSHcomNetTransRequest &request, T opType)
{
    if (NN_UNLIKELY(ep == nullptr || driver == nullptr)) {
        NN_LOG_WARN("Invalid operation to dynamic cast");
        return;
    }

    /* check if reach ep target hb time */
    if (!ep->checkTargetHbTime(mCurrentSec)) {
        return;
    }

    // EP 上的心跳包可能会因对端机器重启而产生超时事件。心跳事件任何非SUCCESS的状态码都将认为心跳异常从而断链。另外需
    // 要注意的是，心跳没有在一个周期内完成也判定为异常，这种情况可能发生于EP上存在大量的用户数据包，心跳包位于用户数
    // 据包后。因在处理用户数据包时就耗费大量时间，在轮到心跳包处理时可能已过了一个心跳周期。
    // \see NetDriverUBWithOob::OneSideDoneCB
    if (ep->HbCheckStateNormal()) {
        NResult result = SendHeartBeat(ep, driver, request, opType);
        if (result != NN_OK) {
            NN_LOG_WARN("Detect Ep id " << ep->Id() << " cannot send Hb, result " << result);
            driver->ProcessEpError(reinterpret_cast<uintptr_t>(ep));
        }
    } else {
        driver->ProcessEpError(reinterpret_cast<uintptr_t>(ep));
    }
}

template <typename T, typename T1> void NetHeartbeat::DetectSingleEpHbState(T *ep, T1 *driver)
{
    if (NN_UNLIKELY(ep == nullptr || driver == nullptr)) {
        NN_LOG_WARN("Invalid operation to dynamic cast");
        return;
    }

    NResult result = NN_OK;
    if (ep->HbCheckStateNormal()) {
        result = SendTwoSideHeartBeat(ep);
        if (result == NN_OK) {
            return;
        }
        NN_LOG_WARN("Detect Ep id " << ep->Id() << " cannot send Hb, result " << result);
    }
    if (ep->HbBrokenEp()) {
        /* delay handle broken ep to prevent race condition with work polling cq thread */
        NN_LOG_WARN("Detect Ep id " << ep->Id() << " Hb state abnormal, call broken handle");
        driver->ProcessEpError(reinterpret_cast<uintptr_t>(ep));
    } else {
        /* set hb broken ep when detected first time */
        NN_LOG_WARN("Detect Ep id " << ep->Id() << " Hb state abnormal, set qp err and wait next probe to handle");
        if (ep->State().Compare(NEP_ESTABLISHED)) {
            ep->State().Set(NEP_BROKEN);
        }
        ep->SetHbBrokenEp();
        // free resources in ProcessEpError
    }
}

void NetHeartbeat::DetectSingleEpHbState(UBSHcomNetTransRequest &request, UBSHcomNetEndpoint *endPoint)
{
    if (NN_UNLIKELY(endPoint == nullptr || mDriver == nullptr)) {
        NN_LOG_ERROR("Endpoint or driver is null");
        return;
    }

    switch (mDriver->Protocol()) {
#ifdef RDMA_BUILD_ENABLED
        case UBSHcomNetDriverProtocol::RDMA:
            return DetectSingleEpHbState(dynamic_cast<NetAsyncEndpoint *>(endPoint),
                dynamic_cast<NetDriverRDMAWithOob *>(mDriver), request, RDMAOpContextInfo::HB_WRITE);
#endif

#ifdef UB_BUILD_ENABLED
        case UBSHcomNetDriverProtocol::UBC:
            return DetectSingleEpHbState(dynamic_cast<NetUBAsyncEndpoint *>(endPoint),
                dynamic_cast<NetDriverUBWithOob *>(mDriver), request, UBOpContextInfo::HB_WRITE);
#endif

        default:
            NN_LOG_ERROR("Invalid protocol " << UBSHcomNetDriverProtocolToString(mDriver->Protocol()) <<
                " to send heartbeat");
            return;
    }
    return;
}
}
}
