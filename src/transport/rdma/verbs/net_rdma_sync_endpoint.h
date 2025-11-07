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
#ifndef OCK_HCOM_NET_SYNC_ENDPOINT_RDMA_H
#define OCK_HCOM_NET_SYNC_ENDPOINT_RDMA_H
#ifdef RDMA_BUILD_ENABLED

#include "hcom.h"
#include "transport/net_endpoint_impl.h"
#include "rdma_composed_endpoint.h"
#include "net_monotonic.h"
#include "net_rdma_driver_oob.h"
#include "net_security_alg.h"
#include "hcom_utils.h"

namespace ock {
namespace hcom {
class NetSyncEndpoint : public NetEndpointImpl {
public:
    NetSyncEndpoint(uint64_t id, RDMASyncEndpoint *ep, NetDriverRDMAWithOob *driver,
        const UBSHcomNetWorkerIndex &workerIndex);
    ~NetSyncEndpoint() override;

    NResult SetEpOption(UBSHcomEpOptions &epOptions) override
    {
        NN_LOG_WARN("[RDMA SyncEp] Empty function for now");
        return NN_OK;
    }

    uint32_t GetSendQueueCount() override
    {
        NN_LOG_WARN("[RDMA SyncEp] Empty function for now");
        return 0;
    }

    inline void PollingMode(RDMAPollingMode m)
    {
        mPollingMode = m;
    }

    const std::string &PeerIpAndPort() override
    {
        if (mEp != nullptr) {
            return mEp->PeerIpAndPort();
        }

        return CONST_EMPTY_STRING;
    }

    const std::string &UdsName() override
    {
        NN_LOG_WARN("[RDMA SyncEp] Empty function for now");
        return CONST_EMPTY_STRING;
    }

    NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, uint32_t seqNO) override;
    NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
        const UBSHcomNetTransOpInfo &opInfo) override;

    NResult PostSendRaw(const UBSHcomNetTransRequest &request, uint32_t seqNO) override;
    NResult PostSendRawSgl(const UBSHcomNetTransSglRequest &request, uint32_t seqNo = 0) override;

    NResult WaitCompletion(int32_t timeout) override;
    NResult PostRead(const UBSHcomNetTransRequest &request) override;
    NResult PostRead(const UBSHcomNetTransSglRequest &request) override;
    NResult PostWrite(const UBSHcomNetTransRequest &request) override;
    NResult PostWrite(const UBSHcomNetTransSglRequest &request) override;

    NResult Receive(int32_t timeout, UBSHcomNetResponseContext &ctx) override;
    NResult ReceiveRaw(int32_t timeout, UBSHcomNetResponseContext &ctx) override;

    inline RDMASyncEndpoint *GetRdmaEp()
    {
        return mEp;
    }

    NResult GetRemoteUdsIdInfo(UBSHcomNetUdsIdInfo &verbsIdInfo) override
    {
        if (!mState.Compare(NEP_ESTABLISHED)) {
            NN_LOG_ERROR("[RDMA SyncEp] EP is not established");
            return NN_EP_NOT_ESTABLISHED;
        }

        if (!mDriver->mStartOobSvr) {
            NN_LOG_ERROR("[RDMA SyncEp] oob server is not start");
            return NN_UDS_ID_INFO_NOT_SUPPORT;
        }

        if (mDriver->mOptions.oobType != NET_OOB_UDS) {
            NN_LOG_ERROR("[RDMA SyncEp] oob type is not uds");
            return NN_UDS_ID_INFO_NOT_SUPPORT;
        }

        verbsIdInfo = mRemoteUdsIdInfo;
        return NN_OK;
    }

    bool GetPeerIpPort(std::string &ip, uint16_t &port) override
    {
        if (NN_UNLIKELY(mEp == nullptr)) {
            return false;
        }

        auto ipPort = mEp->PeerIpAndPort();
        if (NN_UNLIKELY(ipPort.empty())) {
            NN_LOG_ERROR("[RDMA] ip and port of peer is empty");
            return false;
        }

        std::vector<std::string> ipPortVec;
        NetFunc::NN_SplitStr(ipPort, ":", ipPortVec);
        if (NN_UNLIKELY(ipPortVec.size() != NN_NO2)) {
            NN_LOG_ERROR("[RDMA] ip and port of peer is invalid");
            return false;
        }

        try {
            port = std::stoi(ipPortVec[1]);
        } catch (...) {
            NN_LOG_ERROR("[RDMA] port of peer is invalid");
            return false;
        }
        if (port == 0) {
            NN_LOG_ERROR("[RDMA] oob type is uds, does not have peer ip and port msg");
            return false;
        }
        ip = ipPortVec[0];

        return true;
    }

    void Close() override
    {
        auto qp = GetRdmaEp()->Qp();
        qp->Stop();
    }

protected:
    NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, const UBSHcomNetTransOpInfo &opInfo,
                     const UBSHcomExtHeaderType extHeaderType, const void *extHeader, uint32_t extHeaderSize) override;

private:
    static inline bool NeedRetry(RResult result)
    {
        if (result == RR_QP_POST_SEND_WR_FULL || result == RR_QP_ONE_SIDE_WR_FULL || result == RR_QP_CTX_FULL) {
            return true;
        }

        return false;
    }

    inline uint64_t GetFinishTime()
    {
        if (mDefaultTimeout > 0) {
            return NetMonotonic::TimeNs() + static_cast<uint64_t>(mDefaultTimeout) * 1000000000UL;
        } else if (mDefaultTimeout < 0) {
            return UINT64_MAX;
        }

        return 0;
    }

    RDMASyncEndpoint *mEp = nullptr;
    NetDriverRDMAWithOob *mDriver = nullptr;
    RDMAPollingMode mPollingMode = RDMAPollingMode::BUSY_POLLING;
    uint32_t mLastSendSeqNo = 0;
    RDMAOpContextInfo::OpType mDemandPollingOpType = RDMAOpContextInfo::SEND;
    UBSHcomNetResponseContext mRespCtx;
    UBSHcomNetMessage mRespMessage;
    RDMAOpContextInfo *mDelayHandleReceiveCtx = nullptr;

    friend class NetDriverRDMAWithOob;
};
}
}

#endif
#endif // OCK_HCOM_NET_SYNC_ENDPOINT_RDMA_H
