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
#ifndef OCK_HCOM_NET_SOCK_SYNC_ENDPOINT_H
#define OCK_HCOM_NET_SOCK_SYNC_ENDPOINT_H

#include "transport/net_endpoint_impl.h"
#include "net_monotonic.h"
#include "net_security_alg.h"
#include "net_sock_common.h"
#include "sock_common.h"

namespace ock {
namespace hcom {
using SockOpContextInfoPool = OpContextInfoPool<SockOpContextInfo>;
using SockSglContextInfoPool = OpContextInfoPool<SockSglContextInfo>;
class NetSyncEndpointSock : public NetEndpointImpl {
public:
    NetSyncEndpointSock(uint64_t id, Sock *sock, NetDriverSockWithOOB *driver,
        const UBSHcomNetWorkerIndex &workerIndex);
    ~NetSyncEndpointSock() override;

    NResult SetEpOption(UBSHcomEpOptions &epOptions) override;

    uint32_t GetSendQueueCount() override
    {
        NN_LOG_WARN("[Sock SyncEp] Invalid function, sync strategy does not have queue.");
        return 0;
    }

    const std::string &PeerIpAndPort() override
    {
        if (NN_LIKELY(mSock != nullptr)) {
            return mSock->PeerIpPort();
        }

        return CONST_EMPTY_STRING;
    }

    const std::string &UdsName() override
    {
        NN_LOG_WARN("[Sock SyncEp] Empty function for now");
        return CONST_EMPTY_STRING;
    }

    NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, uint32_t seqNo) override;

    NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
        const UBSHcomNetTransOpInfo &opInfo) override;

    NResult PostSendRaw(const UBSHcomNetTransRequest &request, uint32_t seqNo) override;

    NResult PostSendRawSgl(const UBSHcomNetTransSglRequest &request, uint32_t seqNo) override;

    NResult PostRead(const UBSHcomNetTransSglRequest &request) override;

    NResult PostRead(const UBSHcomNetTransRequest &request) override;

    NResult PostWrite(const UBSHcomNetTransSglRequest &request) override;

    NResult PostWrite(const UBSHcomNetTransRequest &request) override;

    NResult WaitCompletion(int32_t timeout) override;

    NResult ReceiveRaw(int32_t timeout, UBSHcomNetResponseContext &ctx) override;

    NResult Receive(int32_t timeout, UBSHcomNetResponseContext &ctx) override;

    NResult GetRemoteUdsIdInfo(UBSHcomNetUdsIdInfo &sockIdInfo) override
    {
        // 用户可能在建链回调中使用该函数，此时ep状态并未设置成NEP_ESTABLISHED
        if (!mState.Compare(NEP_ESTABLISHED)) {
            NN_LOG_WARN("[Sock SyncEp] EP status is " << mState.Get() <<
                " now, use ep after the connection established.");
        }

        if (!mDriver->mStartOobSvr) {
            NN_LOG_ERROR("[Sock SyncEp] oob server is not start");
            return NN_UDS_ID_INFO_NOT_SUPPORT;
        }

        if (mDriver->mOptions.oobType != NET_OOB_UDS) {
            NN_LOG_ERROR("[Sock SyncEp] oob type is not uds");
            return NN_UDS_ID_INFO_NOT_SUPPORT;
        }
        // 通过mRemoteUdsIdInfo值判断是否可以返回给用户
        if (mRemoteUdsIdInfo.gid == 0 && mRemoteUdsIdInfo.pid == 0 && mRemoteUdsIdInfo.uid == 0) {
            NN_LOG_ERROR("[Sock SyncEp] RemoteUdsIdInfo has not been set.");
            return NN_ERROR;
        }
        sockIdInfo = mRemoteUdsIdInfo;
        return NN_OK;
    }

    bool GetPeerIpPort(std::string &ip, uint16_t &port) override
    {
        if (NN_UNLIKELY(mSock == nullptr)) {
            return false;
        }

        auto ipPort = mSock->PeerIpPort();
        if (NN_UNLIKELY(ipPort.empty())) {
            NN_LOG_ERROR("[Sock SyncEp] ip and port of peer is empty");
            return false;
        }

        std::vector<std::string> ipPortVec;
        NetFunc::NN_SplitStr(ipPort, ":", ipPortVec);
        if (NN_UNLIKELY(ipPortVec.size() != NN_NO2)) {
            NN_LOG_ERROR("[Sock SyncEp] ip and port of peer is invalid");
            return false;
        }

        try {
            port = std::stoi(ipPortVec[1]);
        } catch (...) {
            NN_LOG_ERROR("[Sock SyncEp] port of peer is invalid");
            return false;
        }

        // port will only be 0 when the connection is on uds
        if (port == 0) {
            NN_LOG_ERROR("[Sock SyncEp] oob type is uds, does not have peer ip and port msg");
            return false;
        }
        ip = ipPortVec[0];

        return true;
    }

    void Close() override
    {
        if (mState.Compare(NEP_ESTABLISHED)) {
            mState.Set(NEP_BROKEN);
        } else {
            return;
        }
        NN_LOG_INFO("Close tcp sync ep id " << mId << " by user");
        mSock->Close();
    }

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

    static bool inline NeedRetry(NResult sockResult)
    {
        if (sockResult == SS_TCP_RETRY) {
            return true;
        }

        return false;
    }

    __always_inline NResult FillReadWriteCtx(SockOpContextInfo *ctx, SockSglContextInfo *sglCtx,
        const UBSHcomNetTransRequest &request, SockOpContextInfo::SockOpType opType, UBSHcomNetTransHeader header)
    {
        ctx->sock = mSock;
        ctx->opType = opType;
        ctx->errType = SockOpContextInfo::SockErrorType::SS_NO_ERROR;
        ctx->upCtxSize = request.upCtxSize;
        if (ctx->upCtxSize > 0) {
            if (NN_UNLIKELY(memcpy_s(ctx->upCtx, NN_NO16, request.upCtxData, ctx->upCtxSize) != NN_OK)) {
                NN_LOG_ERROR("Failed to copy request to sglCtx");
                return NN_INVALID_PARAM;
            }
        }

        UBSHcomNetTransSgeIov iov(request.lAddress, request.rAddress, request.lKey, request.rKey, request.size);
        sglCtx->Clone(header, &iov, NN_NO1);
        ctx->sendCtx = sglCtx;
        return NN_OK;
    }

    __always_inline NResult FillReadWriteSglCtx(SockOpContextInfo *ctx, SockSglContextInfo *sglCtx,
        const UBSHcomNetTransSglRequest &request, SockOpContextInfo::SockOpType opType, UBSHcomNetTransHeader header)
    {
        ctx->sock = mSock;
        ctx->opType = opType;
        ctx->errType = SockOpContextInfo::SockErrorType::SS_NO_ERROR;
        ctx->upCtxSize = request.upCtxSize;
        if (ctx->upCtxSize > 0) {
            if (NN_UNLIKELY(memcpy_s(ctx->upCtx, NN_NO16, request.upCtxData, ctx->upCtxSize) != NN_OK)) {
                NN_LOG_ERROR("Failed to copy req to sglCtx");
                return NN_INVALID_PARAM;
            }
        }

        sglCtx->Clone(header, request.iov, request.iovCount);
        ctx->sendCtx = sglCtx;
        return NN_OK;
    }

    Sock *mSock = nullptr;
    NetDriverSockWithOOB *mDriver = nullptr;
    uint32_t mLastSendSeqNo = 0;
    uint16_t mLastFlag = 0;
    UBSHcomNetResponseContext mRespCtx;
    UBSHcomNetMessage mRespMessage;

    SockOpContextInfoPool mOpCtxInfoPool;
    SockSglContextInfoPool mSglCtxInfoPool;

    friend class NetDriverSockWithOOB;
};
}
}

#endif // OCK_HCOM_NET_SOCK_SYNC_ENDPOINT_H
