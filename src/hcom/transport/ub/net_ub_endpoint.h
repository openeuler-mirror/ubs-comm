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

#ifndef HCOM_NET_UB_ENDPOINT_H
#define HCOM_NET_UB_ENDPOINT_H
#ifdef UB_BUILD_ENABLED

#include "transport/net_endpoint_impl.h"
#include "net_monotonic.h"
#include "net_security_alg.h"
#include "hcom_utils.h"
#include "ub_urma_wrapper_jetty.h"
#include "net_ub_driver_oob.h"

namespace ock {
namespace hcom {
class NetUBAsyncEndpoint : public NetEndpointImpl {
public:
    NetUBAsyncEndpoint(uint64_t id, UBJetty *qp, NetDriverUBWithOob *driver, UBWorker *worker);
    ~NetUBAsyncEndpoint() override;

    NResult SetEpOption(UBSHcomEpOptions &epOptions) override
    {
        NN_LOG_WARN("[UB AsyncEp] Empty function for now");
        return NN_OK;
    }

    const std::string &PeerIpAndPort() override
    {
        if (mJetty != nullptr) {
            return mJetty->GetPeerIpAndPort();
        }

        return CONST_EMPTY_STRING;
    }

    uint32_t GetSendQueueCount() override
    {
        return mJetty->GetSendQueueSize();
    }

    const std::string &UdsName() override
    {
        NN_LOG_WARN("[UB AsyncEp] Empty function for now");
        return CONST_EMPTY_STRING;
    }

    NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, uint32_t seqNO) override;
    NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
        const UBSHcomNetTransOpInfo &opInfo) override;

    NResult PostSendSglInline(uint16_t opCode, const UBSHcomNetTransRequest &request,
        const UBSHcomNetTransOpInfo &opInfo) override;

    NResult PostSendRaw(const UBSHcomNetTransRequest &request, uint32_t seqNO) override;
    NResult PostSendRawSgl(const UBSHcomNetTransSglRequest &request, uint32_t seqNo) override;

    NResult PostRead(const UBSHcomNetTransRequest &request) override;
    NResult PostRead(const UBSHcomNetTransSglRequest &request) override;
    NResult PostWrite(const UBSHcomNetTransRequest &request) override;
    NResult PostWrite(const UBSHcomNetTransSglRequest &request) override;
    void UpdateTargetHbTime();

    bool checkTargetHbTime(uint64_t currTime)
    {
        if (mTargetHbTime < currTime) {
            mTargetHbTime = currTime + mHeartBeatIdleTime;
            return true;
        }
        return false;
    }

    NResult WaitCompletion(int32_t timeout) override
    {
        NN_LOG_WARN("Invalid operation, wait completion is not supported by NetUBAsyncEndpoint");
        return NN_INVALID_OPERATION;
    }

    NResult Receive(int32_t timeout, UBSHcomNetResponseContext &ctx) override
    {
        NN_LOG_WARN("Invalid operation, wait completion is not supported by NetUBAsyncEndpoint");
        return NN_INVALID_OPERATION;
    }

    NResult ReceiveRaw(int32_t timeout, UBSHcomNetResponseContext &ctx) override
    {
        NN_LOG_WARN("Invalid operation, wait completion is not supported by NetUBAsyncEndpoint");
        return NN_INVALID_OPERATION;
    }

    inline bool HbCheckStateNormal()
    {
        if (mHbCount > mHbLastCount) {
            mHbLastCount = mHbCount;
            return true;
        }

        return false;
    }

    inline void HbRecordCount()
    {
        __sync_add_and_fetch(&mHbCount, 1);
    }

    inline void SetRemoteHbInfo(uintptr_t address, uint64_t key, uint64_t size)
    {
        mRemoteHbAddress = address;
        mRemoteHbKey = key;
        mHbMrSize = size;
    }

    inline UBJetty *GetQp() const
    {
        return mJetty;
    }

    NResult GetRemoteUdsIdInfo(UBSHcomNetUdsIdInfo &idInfo) override
    {
        if (!mState.Compare(NEP_ESTABLISHED)) {
            NN_LOG_ERROR("[UB AsyncEp] EP is not established");
            return NN_EP_NOT_ESTABLISHED;
        }

        if (!mDriver->mStartOobSvr) {
            NN_LOG_ERROR("[UB AsyncEp] oob server is not start");
            return NN_UDS_ID_INFO_NOT_SUPPORT;
        }

        if (mDriver->mOptions.oobType != NET_OOB_UDS) {
            NN_LOG_ERROR("[UB AsyncEp] oob type is not uds");
            return NN_UDS_ID_INFO_NOT_SUPPORT;
        }

        idInfo = mRemoteUdsIdInfo;
        return NN_OK;
    }

    bool GetPeerIpPort(std::string &ip, uint16_t &port) override
    {
        if (NN_UNLIKELY(mJetty == nullptr)) {
            return false;
        }

        auto ipPort = mJetty->GetPeerIpAndPort();
        if (NN_UNLIKELY(ipPort.empty())) {
            NN_LOG_ERROR("[UB AsyncEp] ip and port of peer is empty");
            return false;
        }

        std::vector<std::string> ipPortVec;
        NetFunc::NN_SplitStr(ipPort, ":", ipPortVec);
        if (NN_UNLIKELY(ipPortVec.size() != NN_NO2)) {
            NN_LOG_ERROR("[UB AsyncEp] ip and port of peer is invalid");
            return false;
        }

        try {
            port = std::stoi(ipPortVec[1]);
        } catch (...) {
            NN_LOG_ERROR("[UB AsyncEp] port of peer is invalid");
            return false;
        }
        if (port == 0) {
            NN_LOG_ERROR("[UB AsyncEp] oob type is uds, does not have peer ip and port msg");
            return false;
        }
        ip = ipPortVec[0];

        return true;
    }

    void Close() override
    {
        NN_LOG_INFO("[UB AsyncEp] Close ep id " << mId << " by user");
        mJetty->Stop();
    }

protected:
    NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, const UBSHcomNetTransOpInfo &opInfo,
                     const UBSHcomExtHeaderType extHeaderType, const void *extHeader, uint32_t extHeaderSize) override;

private:
    uint64_t inline GetFinishTime()
    {
        if (mDefaultTimeout > 0) {
            return NetMonotonic::TimeNs() + static_cast<uint64_t>(mDefaultTimeout) * 1000000000UL;
        } else if (mDefaultTimeout < 0) {
            return UINT64_MAX;
        }

        return 0;
    }

    bool inline NeedRetry(NResult &result)
    {
        if (!State().Compare(NEP_ESTABLISHED)) {
            result = NN_EP_NOT_ESTABLISHED;
            return false;
        }

        if (result == UB_QP_POST_SEND_WR_FULL || result == UB_QP_ONE_SIDE_WR_FULL || result == UB_QP_CTX_FULL) {
            return true;
        }

        return false;
    }

    inline UBWorker *GetWorker() const
    {
        return mWorker;
    }

    NetDriverUBWithOob *GetDriver() const
    {
        return mDriver;
    }

    UBJetty *mJetty = nullptr;
    UBWorker *mWorker = nullptr;
    NetDriverUBWithOob *mDriver = nullptr;

    uint64_t mHbCount = 1;
    uint64_t mHbLastCount = 0;
    uintptr_t mRemoteHbAddress = 0;
    uint64_t mRemoteHbKey = 0;
    uint64_t mHbMrSize = 0;
    uint32_t mDmSize = 0;
    uint64_t mTargetHbTime = 0;
    uint16_t mHeartBeatIdleTime = NN_NO60;
    uint32_t mSendRawAllowedSize = 0;

    friend class NetDriverUBWithOob;
    friend class NetHeartbeat;
    friend class UBJetty;
    friend class UBWorker; // 依赖GetDriver
};

/* *********************************************************************************** */
class NetUBSyncEndpoint : public NetEndpointImpl {
public:
    NetUBSyncEndpoint(uint64_t id, UBJetty *qp, UBJfc *cq, uint32_t ubOpCtxPoolSize, NetDriverUBWithOob *driver,
        const UBSHcomNetWorkerIndex &workerIndex);
    ~NetUBSyncEndpoint() override;

    NResult SetEpOption(UBSHcomEpOptions &epOptions) override
    {
        NN_LOG_WARN("[UB SyncEp] Empty function for now");
        return NN_OK;
    }

    uint32_t GetSendQueueCount() override
    {
        NN_LOG_WARN("[UB SyncEp] Empty function for now");
        return NN_OK;
    }

    inline void PollingMode(UBPollingMode m)
    {
        mPollingMode = m;
    }

    const std::string &PeerIpAndPort() override
    {
        if (mJetty != nullptr) {
            return mJetty->GetPeerIpAndPort();
        }

        return CONST_EMPTY_STRING;
    }

    const std::string &UdsName() override
    {
        NN_LOG_WARN("[UB SyncEp] Empty function for now");
        return CONST_EMPTY_STRING;
    }

    NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, uint32_t seqNO) override;
    NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
        const UBSHcomNetTransOpInfo &opInfo) override;

    NResult PostSendRaw(const UBSHcomNetTransRequest &request, uint32_t seqNo) override;
    NResult PostSendRawSgl(const UBSHcomNetTransSglRequest &request, uint32_t seqNo = 0) override;

    NResult PostRead(const UBSHcomNetTransRequest &request) override;
    NResult PostRead(const UBSHcomNetTransSglRequest &request) override;
    NResult PostWrite(const UBSHcomNetTransRequest &request) override;
    NResult PostWrite(const UBSHcomNetTransSglRequest &request) override;
    NResult WaitCompletion(int32_t timeout) override;

    NResult Receive(int32_t timeout, UBSHcomNetResponseContext &ctx) override;
    NResult ReceiveRaw(int32_t timeout, UBSHcomNetResponseContext &ctx) override;
    void ReceiveRawHandle(UBOpContextInfo *opCtx, uint32_t immData, NResult &result);

    NResult InnerPostSend(const UBSendReadWriteRequest &req, urma_target_seg_t *localSeg, uint32_t immData = 0);
    NResult InnerPostSendSgl(const UBSendSglRWRequest &req, const UBSendReadWriteRequest &tlsReq, uint32_t immData);
    NResult InnerPostRead(const UBSendReadWriteRequest &req);
    NResult InnerPostWrite(const UBSendReadWriteRequest &req);
    UResult PostOneSideSgl(const UBSendSglRWRequest &req, bool isRead = true);
    UResult CreateOneSideCtx(const UBSgeCtxInfo &sgeInfo, const UBSHcomNetTransSgeIov *iov, uint32_t iovCount,
        uint64_t (&ctxArr)[NET_SGE_MAX_IOV], bool isRead);

    NResult PollingCompletion(UBOpContextInfo *&ctx, int32_t timeout, uint32_t &immData);
    NResult PostReceive(uintptr_t bufAddress, uint32_t bufSize, urma_target_seg_t *localSeg);
    NResult RePostReceive(UBOpContextInfo *ctx);
    static NResult CreateResources(const std::string &name, UBContext *ctx, UBPollingMode pollMode,
        const JettyOptions &options, UBJetty *&qp, UBJfc *&cq);

    inline UBJetty *GetQp() const
    {
        return mJetty;
    }

    NResult GetRemoteUdsIdInfo(UBSHcomNetUdsIdInfo &idInfo) override
    {
        if (!mState.Compare(NEP_ESTABLISHED)) {
            NN_LOG_ERROR("[UB SyncEp] EP is not established");
            return NN_EP_NOT_ESTABLISHED;
        }

        if (!mDriver->mStartOobSvr) {
            NN_LOG_ERROR("[UB SyncEp] oob server is not start");
            return NN_UDS_ID_INFO_NOT_SUPPORT;
        }

        if (mDriver->mOptions.oobType != NET_OOB_UDS) {
            NN_LOG_ERROR("[UB SyncEp] oob type is not uds");
            return NN_UDS_ID_INFO_NOT_SUPPORT;
        }

        idInfo = mRemoteUdsIdInfo;
        return NN_OK;
    }

    bool GetPeerIpPort(std::string &ip, uint16_t &port) override
    {
        if (NN_UNLIKELY(mJetty == nullptr)) {
            return false;
        }

        auto ipPort = mJetty->GetPeerIpAndPort();
        if (NN_UNLIKELY(ipPort.empty())) {
            NN_LOG_ERROR("ip and port of peer is empty");
            return false;
        }

        std::vector<std::string> ipPortVec;
        NetFunc::NN_SplitStr(ipPort, ":", ipPortVec);
        if (NN_UNLIKELY(ipPortVec.size() != NN_NO2)) {
            NN_LOG_ERROR("ip and port of peer is invalid");
            return false;
        }

        try {
            port = std::stoi(ipPortVec[1]);
        } catch (...) {
            NN_LOG_ERROR("port of peer is invalid");
            return false;
        }
        if (port == 0) {
            NN_LOG_ERROR("oob type is uds, does not have peer ip and port msg");
            return false;
        }
        ip = ipPortVec[0];

        return true;
    }

    void Close() override
    {
        mJetty->Stop();
    }

protected:
    NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, const UBSHcomNetTransOpInfo &opInfo,
                     const UBSHcomExtHeaderType extHeaderType, const void *extHeader, uint32_t extHeaderSize) override;

private:
    inline uint64_t GetFinishTime()
    {
        if (mDefaultTimeout > 0) {
            return NetMonotonic::TimeNs() + static_cast<uint64_t>(mDefaultTimeout) * 1000000000UL;
        } else if (mDefaultTimeout < 0) {
            return UINT64_MAX;
        }

        return 0;
    }

    static inline bool NeedRetry(NResult result)
    {
        if (result == UB_QP_POST_SEND_WR_FULL || result == UB_QP_ONE_SIDE_WR_FULL || result == UB_QP_CTX_FULL) {
            return true;
        }

        return false;
    }

    UBJetty *mJetty = nullptr;
    UBJfc *mJfc = nullptr;
    NetObjPool<UBOpContextInfo> mCtxPool;

    NetDriverUBWithOob *mDriver = nullptr;
    UBPollingMode mPollingMode = UBPollingMode::UB_BUSY_POLLING;
    uint32_t mLastSendSeqNo = 0;
    UBOpContextInfo::OpType mDemandPollingOpType = UBOpContextInfo::SEND;
    UBSHcomNetResponseContext mRespCtx;
    UBSHcomNetMessage mRespMessage;
    UBOpContextInfo *mDelayHandleReceiveCtx = nullptr;
    uint32_t mDelayHandleReceiveImmData = 0;
    uint32_t mDmSize = 0;
    uint32_t mSendRawAllowedSize = 0;

    friend class NetDriverUBWithOob;
};
}
}


#endif
#endif // HCOM_NET_UB_ENDPOINT_H
