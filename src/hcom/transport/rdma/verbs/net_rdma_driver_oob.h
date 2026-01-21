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
#ifndef OCK_NET_CLIENT_SERVER_RDMA_1234244441233_H
#define OCK_NET_CLIENT_SERVER_RDMA_1234244441233_H
#ifdef RDMA_BUILD_ENABLED
#include <unordered_map>

#include "hcom.h"

#include "net_oob.h"
#include "net_rdma_driver.h"
#include "net_util.h"
#include "net_heartbeat.h"
#include "rdma_common.h"
#include "rdma_mr_dm_buf.h"
#include "rdma_mr_fixed_buf.h"

namespace ock {
namespace hcom {
/* **************************************************************************************** */
class NetDriverRDMAWithOob : public NetDriverRDMA {
public:
    NetDriverRDMAWithOob(const std::string &name, bool startOobSvr, UBSHcomNetDriverProtocol protocol)
        : NetDriverRDMA(name, startOobSvr, protocol)
    {
        OBJ_GC_INCREASE(NetDriverRDMAWithOob);
    }

    ~NetDriverRDMAWithOob() override
    {
        OBJ_GC_DECREASE(NetDriverRDMAWithOob);
    }

    NResult Connect(const std::string &payload, UBSHcomNetEndpointPtr &ep, uint32_t flags, uint8_t serverGrpNo = 0,
        uint8_t clientGrpNo = 0) override;
    NResult Connect(const std::string &oobIp, uint16_t oobPort, const std::string &payload,
        UBSHcomNetEndpointPtr &outEp, uint32_t flags, uint8_t serverGrpNo = 0, uint8_t clientGrpNo = 0,
        uint64_t ctx = 0) override;
    NResult Connect(const std::string &serverUrl, const std::string &payload, UBSHcomNetEndpointPtr &ep, uint32_t flags,
        uint8_t serverGrpNo = 0, uint8_t clientGrpNo = 0, uint64_t ctx = 0) override;

    NResult MultiRailNewConnection(OOBTCPConnection &conn);
    uint16_t GetHbIdleTime()
    {
        if (mHeartBeat == nullptr) {
            NN_LOG_ERROR("mHeartBeat is nullpttr");
            return 0;
        }
        return mHeartBeat->GetHbIdleTime();
    }

protected:
    int NewConnectionCB(OOBTCPConnection &conn);
    int SendFinished(RDMAOpContextInfo *ctx);
    int NewRequest(RDMAOpContextInfo *ctx);
    int OneSideDone(RDMAOpContextInfo *ctx);

    NResult DoInitialize() override;
    void DoUnInitialize() override;

    NResult DoStart() override;
    void DoStop() override;

    int OneSideDoneCB(RDMAOpContextInfo *ctx);
    int SendFinishedCB(RDMAOpContextInfo *ctx);

    void ProcessEpError(uintptr_t ep);
    void ProcessQPError(RDMAOpContextInfo *ctx);
    void ProcessErrorNewRequest(RDMAOpContextInfo *ctx);
    void ProcessErrorOneSideDone(RDMAOpContextInfo *ctx);
    void ProcessErrorSendFinished(RDMAOpContextInfo *ctx);

private:
    friend class NetAsyncEndpoint;
    friend class NetSyncEndpoint;

    NResult NewReceivedRequest(RDMAOpContextInfo *ctx, UBSHcomNetRequestContext &netCtx, UBSHcomNetMessage &msg,
        RDMAWorker *worker) const;

    NResult NewReceivedRawRequest(RDMAOpContextInfo *ctx, UBSHcomNetRequestContext &netCtx, UBSHcomNetMessage &msg,
        RDMAWorker *worker, uint32_t immData) const;
    
    NResult NewReceivedRequestWithoutCopy(RDMAOpContextInfo *ctx, UBSHcomNetRequestContext &netCtx,
        UBSHcomNetMessage &msg, RDMAWorker *worker, void *dataAddress, UBSHcomNetTransHeader *header) const;
        
    NResult SendRequestFinishedCB(RDMAOpContextInfo *ctx, UBSHcomNetRequestContext &netCtx, RDMAWorker *worker);
    NResult SendRawSglFinishedCB(RDMAOpContextInfo *ctx, UBSHcomNetRequestContext &netCtx, RDMAWorker *worker);
    NResult SendSglInlineFinishedCB(RDMAOpContextInfo *ctx, UBSHcomNetRequestContext &netCtx, RDMAWorker *worker);

    NResult Connect(const OOBTCPClientPtr &client, const std::string &payload, UBSHcomNetEndpointPtr &outEp,
       uint8_t serverGrpNo, uint8_t clientGrpNo, uint64_t ctx);
    NResult ConnectSyncEp(const OOBTCPClientPtr &client, const std::string &payload, UBSHcomNetEndpointPtr &outEp,
        uint32_t flags, uint8_t serverGrpNo, uint64_t ctx);

    void DestroyEpInWorker(RDMAWorker *worker);
    void DestroyEpByPortNum(int portNum);
    void HandleCqEvent(struct ibv_async_event *event);
    void HandlePortDown(int portNum);
    void HandlePortActive(int portNum);
    void HandleAsyncEvent(struct ibv_async_event *event);
    void RunInRdmaEventThread();
    inline bool ValidateRequestContext(RDMAOpContextInfo *ctx)
    {
        if (NN_UNLIKELY(ctx == nullptr || ctx->qp == nullptr || ctx->qp->UpContext1() == 0 ||
            ctx->qp->UpContext() == 0)) {
            NN_LOG_ERROR("Ctx or QP or Worker is null of RequestReceived in Driver " << mName << "");
            return false;
        }
        return true;
    }
    __always_inline void ProcessErrorContext(RDMAOpContextInfo *&nextOpCtx, RDMAOpContextInfo *&remainingOpCtx,
        UBSHcomNetEndpoint *epPtr)
    {
        nextOpCtx = remainingOpCtx->next;
        if (remainingOpCtx->opResultType != RDMAOpContextInfo::INVALID_MAGIC) {
            remainingOpCtx->opResultType = epPtr->State().Compare(NEP_BROKEN) ? RDMAOpContextInfo::ERR_EP_BROKEN :
                                                                                RDMAOpContextInfo::ERR_EP_CLOSE;
            switch (remainingOpCtx->opType) {
                case (RDMAOpContextInfo::OpType::SEND):
                case (RDMAOpContextInfo::OpType::SEND_RAW):
                case (RDMAOpContextInfo::OpType::SEND_RAW_NO_CP):
                case (RDMAOpContextInfo::OpType::SEND_RAW_SGL):
                    ProcessErrorSendFinished(remainingOpCtx);
                    break;
                case (RDMAOpContextInfo::OpType::RECEIVE):
                    ProcessErrorNewRequest(remainingOpCtx);
                    break;
                case (RDMAOpContextInfo::OpType::WRITE):
                case (RDMAOpContextInfo::OpType::SGL_WRITE):
                case (RDMAOpContextInfo::OpType::HB_WRITE):
                case (RDMAOpContextInfo::OpType::READ):
                case (RDMAOpContextInfo::OpType::SGL_READ):
                    ProcessErrorOneSideDone(remainingOpCtx);
                    break;
                default:
                    NN_LOG_ERROR("Poll cq invalid OpType " << remainingOpCtx->opType);
                    break;
            }
        }
        remainingOpCtx->qpNum = 0xffffffff;
        remainingOpCtx->opResultType = RDMAOpContextInfo::INVALID_MAGIC;
        remainingOpCtx = nextOpCtx;
    }
    bool mNeedStopEvent = false;
    std::thread mRdmaEventThread;
    std::atomic<bool> mEventStarted { false };
    NetHeartbeat *mHeartBeat = nullptr;
    friend class NetHeartbeat;
};
}
}
#endif
#endif // _OCK_NET_CLIENT_SERVER_RDMA_1234244441233_H