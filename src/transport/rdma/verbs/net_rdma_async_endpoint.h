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
#ifndef OCK_HCOM_NET_ASYNC_ENDPOINT_RDMA_H
#define OCK_HCOM_NET_ASYNC_ENDPOINT_RDMA_H
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
class NetAsyncEndpoint : public NetEndpointImpl {
public:
    NetAsyncEndpoint(uint64_t id, RDMAAsyncEndPoint *ep, NetDriverRDMAWithOob *driver,
        const UBSHcomNetWorkerIndex &workerIndex);
    ~NetAsyncEndpoint() override;

    NResult SetEpOption(UBSHcomEpOptions &epOptions) override
    {
        NN_LOG_WARN("Empty function for now");
        return NN_OK;
    }

    const std::string &PeerIpAndPort() override
    {
        if (mEp != nullptr) {
            return mEp->PeerIpAndPort();
        }

        return CONST_EMPTY_STRING;
    }

    uint32_t GetSendQueueCount() override;

    const std::string &UdsName() override
    {
        NN_LOG_WARN("Empty function for now");
        return CONST_EMPTY_STRING;
    }

    NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, uint32_t seqNO) override;
    NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
        const UBSHcomNetTransOpInfo &opInfo) override;

    NResult PostSendSglInline(uint16_t opCode, const UBSHcomNetTransRequest &request,
        const UBSHcomNetTransOpInfo &opInfo) override;

    /*
     * @brief raw data to peer without opcode
     */
    NResult PostSendRaw(const UBSHcomNetTransRequest &request, uint32_t seqNO) override;
    NResult PostSendRawSgl(const UBSHcomNetTransSglRequest &request, uint32_t seqNo) override;

    NResult PostRead(const UBSHcomNetTransSglRequest &request) override;
    NResult PostRead(const UBSHcomNetTransRequest &request) override;
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
        NN_LOG_WARN("Invalid operation, wait completion is not supported by NetAsyncEndpoint");
        return NN_INVALID_OPERATION;
    }

    NResult Receive(int32_t timeout, UBSHcomNetResponseContext &ctx) override
    {
        NN_LOG_WARN("Invalid operation, wait completion is not supported by NetAsyncEndpoint");
        return NN_INVALID_OPERATION;
    }

    NResult ReceiveRaw(int32_t timeout, UBSHcomNetResponseContext &ctx) override
    {
        NN_LOG_WARN("Invalid operation, wait completion is not supported by NetAsyncEndpoint");
        return NN_INVALID_OPERATION;
    }

    inline void HbRecordCount()
    {
        __sync_add_and_fetch(&mHbCount, 1);
    }

    inline bool HbCheckStateNormal()
    {
        if (mHbCount > mHbLastCount) {
            mHbLastCount = mHbCount;
            return true;
        }

        return false;
    }

    inline void SetRemoteHbInfo(uintptr_t address, uint32_t key, uint64_t size)
    {
        mRemoteHbAddress = address;
        mRemoteHbKey = key;
        mHbMrSize = size;
    }

    inline void SetHbBrokenEp()
    {
        mHbBrokenEp = true;
    }

    inline bool HbBrokenEp() const
    {
        return mHbBrokenEp;
    }

    inline RDMAAsyncEndPoint *GetRdmaEp()
    {
        return mEp;
    }

    NResult GetRemoteUdsIdInfo(UBSHcomNetUdsIdInfo &verbsIdInfo) override
    {
        if (!mState.Compare(NEP_ESTABLISHED)) {
            NN_LOG_ERROR("[RDMA AsyncEp] EP is not established");
            return NN_EP_NOT_ESTABLISHED;
        }

        if (!mDriver->mStartOobSvr) {
            NN_LOG_ERROR("[RDMA AsyncEp] oob server is not start");
            return NN_UDS_ID_INFO_NOT_SUPPORT;
        }

        if (mDriver->mOptions.oobType != NET_OOB_UDS) {
            NN_LOG_ERROR("[RDMA AsyncEp] oob type is not uds");
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

        auto ipAndPort = mEp->PeerIpAndPort();
        if (NN_UNLIKELY(ipAndPort.empty())) {
            NN_LOG_ERROR("[RDMA AsyncEp] ip and port of peer is empty");
            return false;
        }

        std::vector<std::string> ipPortVec;
        NetFunc::NN_SplitStr(ipAndPort, ":", ipPortVec);
        if (NN_UNLIKELY(ipPortVec.size() != NN_NO2)) {
            NN_LOG_ERROR("[RDMA AsyncEp] ip and port of peer is invalid");
            return false;
        }

        try {
            port = std::stoi(ipPortVec[1]);
        } catch (...) {
            NN_LOG_ERROR("[RDMA AsyncEp] port of peer is invalid");
            return false;
        }
        if (port == 0) {
            NN_LOG_ERROR("[RDMA AsyncEp] oob type is uds, does not have peer ip and port msg");
            return false;
        }
        ip = ipPortVec[0];

        return true;
    }

    void Close() override
    {
        NN_LOG_INFO("Close ep id " << mId << " by user");
        auto qp = GetRdmaEp()->Qp();
        qp->Stop();
    }

protected:
    // extHeader 可以认为是服务层头部，它可以是 UBSHcomFragmentHeader 也可以是通用的服
    // 务器头部（暂未实现）。
    // Q：服务层的头部也可以看做是 request 的一部分，为什么不把它放进 request 中？
    // A：request 是用户直接传递进来的，而 extHeader 可能是服务层自己生成的，它
    // 们两个内存不连续，强行令它们归一在 request 中需要另外额外的 memcpy. 比较
    // 好的方式是通过 iov 的方式发送。
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

    bool inline NeedRetry(RResult &result)
    {
        if (!State().Compare(NEP_ESTABLISHED)) {
            result = NN_EP_NOT_ESTABLISHED;
            return false;
        }

        if (result == RR_QP_POST_SEND_WR_FULL || result == RR_QP_ONE_SIDE_WR_FULL || result == RR_QP_CTX_FULL) {
            return true;
        }

        return false;
    }

    inline RDMAQp *GetQp() const
    {
        if (NN_UNLIKELY(mEp == nullptr)) {
            return nullptr;
        }
        return mEp->Qp();
    }

    inline RDMAWorker *GetWorker() const
    {
        return reinterpret_cast<RDMAWorker *>(mEp->Qp()->UpContext1());
    }

    RDMAAsyncEndPoint *mEp = nullptr;
    NetDriverRDMAWithOob *mDriver = nullptr;

    bool mHbBrokenEp = false;
    uint64_t mHbCount = 1;
    uint64_t mHbLastCount = 0;
    uintptr_t mRemoteHbAddress = 0;
    uint32_t mRemoteHbKey = 0;
    uint64_t mHbMrSize = 0;
    uint64_t mTargetHbTime = 0;
    uint16_t mHeartBeatIdleTime = NN_NO60;

    friend class NetDriverRDMAWithOob;
    friend class NetHeartbeat;
};
}
}

#endif
#endif // OCK_HCOM_NET_ASYNC_ENDPOINT_RDMA_H
