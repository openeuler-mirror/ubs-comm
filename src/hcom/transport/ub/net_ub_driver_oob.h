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

#ifndef HCOM_NET_UB_DRIVER_OOB_H
#define HCOM_NET_UB_DRIVER_OOB_H
#ifdef UB_BUILD_ENABLED

#include <unordered_map>

#include "net_oob.h"
#include "net_ub_driver.h"
#include "net_util.h"
#include "net_heartbeat.h"
#include "ub_common.h"
#include "ub_mr_fixed_buf.h"
#include "ub_urma_wrapper_public_jetty.h"

namespace ock {
namespace hcom {
#define PUBLIC_JETTY_NUM_MIN (4)
#define PUBLIC_JETTY_NUM_MAX (1023)

class NetDriverUBWithOob : public NetDriverUB {
public:
    NetDriverUBWithOob(const std::string &name, bool startOobSvr, UBSHcomNetDriverProtocol protocol)
        : NetDriverUB(name, startOobSvr, protocol)
    {
        OBJ_GC_INCREASE(NetDriverUBWithOob);
    }

    ~NetDriverUBWithOob() override
    {
        OBJ_GC_DECREASE(NetDriverUBWithOob);
        if (mPublicJetty != nullptr) {
            mPublicJetty->DecreaseRef();
        }
    }

    NResult Connect(const std::string &payload, UBSHcomNetEndpointPtr &ep, uint32_t flags, uint8_t serverGrpNo = 0,
        uint8_t clientGrpNo = 0) override;
    NResult Connect(const std::string &oobIp, uint16_t oobPort, const std::string &payload,
        UBSHcomNetEndpointPtr &outEp, uint32_t flags, uint8_t serverGrpNo = 0, uint8_t clientGrpNo = 0,
        uint64_t ctx = 0) override;
    NResult Connect(const std::string &serverUrl, const std::string &payload, UBSHcomNetEndpointPtr &ep, uint32_t flags,
        uint8_t serverGrpNo = 0, uint8_t clientGrpNo = 0, uint64_t ctx = 0) override;
    NResult MultiRailNewConnection(OOBTCPConnection &mConn);
    uint16_t GetHbIdleTime()
    {
        if (mHeartBeat == nullptr) {
            NN_LOG_ERROR("mHeartBeat is nullpttr");
            return 0;
        }
        return mHeartBeat->GetHbIdleTime();
    }

protected:
    int NewConnectionCB(OOBTCPConnection &mConn);
    int NewRequest(UBOpContextInfo *ctx);
    NResult NewRequestOnEncryption(UBOpContextInfo *ctx, UBSHcomNetMessage &msg, bool &messageReady,
        UBSHcomNetRequestContext &netCtx);
    int SendFinished(UBOpContextInfo *ctx);
    int OneSideDone(UBOpContextInfo *ctx);

    NResult DoInitialize() override;
    void DoUnInitialize() override;

    NResult DoStart() override;
    void DoStop() override;

    void RunInUbEventThread();
    int SendFinishedCB(UBOpContextInfo *ctx);
    int OneSideDoneCB(UBOpContextInfo *ctx);
    int SendRawSglFinishedCB(UBOpContextInfo *ctx, UBSHcomNetRequestContext &netCtx);
    int RWSglOneSideDoneCB(UBOpContextInfo *ctx, UBSHcomNetRequestContext &netCtx);
    int SendSglInlineFinishedCB(UBOpContextInfo *ctx, UBSHcomNetRequestContext &requestCtx, UBWorker *worker);
    int SendAndSendRawFinishedCB(UBOpContextInfo *ctx, UBSHcomNetRequestContext &requestCtx, UBWorker *worker);

    void ProcessEpError(uintptr_t ep);
    void ProcessQPError(UBOpContextInfo *ctx);
    void ProcessErrorNewRequest(UBOpContextInfo *ctx);
    void ProcessErrorSendFinished(UBOpContextInfo *ctx);
    void ProcessErrorOneSideDone(UBOpContextInfo *ctx);
    void ProcessTwoSideHeartbeat(UBOpContextInfo *ctx, UBSHcomNetRequestContext &netCtx);

    NResult ConnectSyncEp(const std::string &oobIp, uint16_t oobPort, const std::string &payload,
        UBSHcomNetEndpointPtr &outEp, uint32_t flags, uint8_t serverGrpNo, uint64_t ctx);

    NResult CreatePublicJetty(UBPublicJetty *&publicJetty, uint32_t id, bool isServer = false);
    NResult CreateUrmaListeners(UBPublicJetty *&publicJetty);
    NResult PublicJettyNewConnectionCB(UBOpContextInfo *ctx);
    NResult ConnectByPublicJetty(const std::string &oobIp, uint16_t oobPort, const std::string &payload,
        UBSHcomNetEndpointPtr &outEp, uint32_t flags, uint8_t serverGrpNo = 0, uint8_t clientGrpNo = 0,
        uint64_t ctx = 0);
    NResult ConnectSyncEpByPublicJetty(const std::string &oobIp, uint16_t oobPort, const std::string &payload,
        UBSHcomNetEndpointPtr &outEp, uint32_t flags, uint8_t serverGrpNo = 0, uint8_t clientGrpNo = 0,
        uint64_t ctx = 0);
    NResult ConnectASyncEpByPublicJetty(const std::string &oobIp, uint16_t oobPort, const std::string &payload,
        UBSHcomNetEndpointPtr &outEp, uint32_t flags, uint8_t serverGrpNo = 0, uint8_t clientGrpNo = 0,
        uint64_t ctx = 0);
    NResult CheckServerACK(JettyConnResp &exchangeMsg);
    NResult PrePostReceiveOnConnection(UBJetty *qp, UBWorker *worker);
    NResult FillExchMsg(JettyConnHeader *exchangeInfo, UBJetty *qp, const std::string &payload,
        uint8_t serverGrpNo, UBPublicJetty *clientPublicJetty);
    NResult ServerCreateEp(UBJettyExchangeInfo &info, UBJetty *qp, UBWorker *worker, JettyConnHeader *exchangeInfo,
        UBPublicJetty *serverControlJetty);
    NResult ServerReplyMsg(UBJetty *qp, JettyConnResp &exchangeMsg, UBPublicJetty *serverControlJetty,
        uint32_t token = 0);
    NResult ServerCreateJetty(UBJetty *&qp, UBWorker *worker, JettyConnResp &exchangeMsg, JettyConnHeader *exchangeInfo,
        UBPublicJetty *serverControlJetty, uint32_t token = 0);
    NResult ServerSelectWorker(UBWorker *&worker, JettyConnResp &exchangeMsg, uint8_t groupIndex,
        UBPublicJetty *serverControlJetty);
    NResult CheckMagicAndProtocol(JettyConnResp &exchangeMsg, JettyConnHeader *exchangeInfo,
        UBPublicJetty *serverControlJetty);
    NResult ClientCheckState(const std::string &payload);
    NResult PublicJettyConnect(const std::string &oobIp, uint16_t oobPort, UBPublicJetty *&clientPublicJetty);
    NResult ClientSelectWorker(UBWorker *&worker, uint8_t clientGrpNo, uint64_t id);
    NResult ClientCreateJetty(UBJetty *&qp, UBWorker *worker, uint32_t token = 0);
    NResult ClientSendConnReq(const std::string payload, uint64_t id, uint8_t serverGrpNo,
        UBPublicJetty *clientPublicJetty, UBJetty *qp, UBPublicJetty *clientControlJetty, uint32_t token = 0);
    NResult ClientEstablishConnOnReply(UBPublicJetty *clientControlJetty, UBJetty *qp, UBJettyExchangeInfo &info);
    NResult ClientCreateEp(UBSHcomNetEndpointPtr &outEp, uint64_t id, UBJetty *qp, UBWorker *worker,
        UBJettyExchangeInfo &info, UBPublicJetty *clientControlJetty);
    NResult ClientSyncEpCreateJetty(UBJetty *&qp, UBJfc *&cq, UBPollingMode pollMode, uint32_t token = 0);
    NResult PrePostReceiveOnSyncEp(UBSHcomNetEndpointPtr ep, uint16_t prePostCount, UBJetty *qp);
    void ClientSyncEpSetInfo(UBSHcomNetEndpointPtr ep, UBJetty *qp, UBSHcomNetEndpointPtr &outEp);
    NResult ServerEstablishCtrlConn(JettyConnHeader *exchangeInfo, UBPublicJetty *serverControlJetty);
    NResult CreateSyncEp(UBJetty *qp, UBJfc *cq, uint64_t id, UBSHcomNetEndpointPtr &outEp,
        UBPublicJetty *clientControlJetty);

private:
    friend class NetUBAsyncEndpoint;
    friend class NetUBSyncEndpoint;
    friend class UBJetty;

    NResult NewReceivedRequest(UBOpContextInfo *ctx, UBSHcomNetRequestContext &netCtx, UBSHcomNetMessage &msg,
        UBWorker *worker) const;

    NResult NewReceivedRawRequest(UBOpContextInfo *ctx, UBSHcomNetRequestContext &netCtx, UBSHcomNetMessage &msg,
        UBWorker *worker, uint32_t immData) const;
    
    NResult NewReceivedRequestWithoutCopy(UBOpContextInfo *ctx, UBSHcomNetRequestContext &netCtx,
        UBSHcomNetMessage &msg, UBWorker *worker, void *dataAddress, UBSHcomNetTransHeader *header) const;

    void DestroyEpInWorker(UBWorker *worker);
    void DestroyEpByPortNum(int portNum);
    void HandleCqEvent(urma_async_event_t *event);
    void HandlePortDown(int portNum);
    void HandlePortActive(int portNum);
    void HandleAsyncEvent(urma_async_event_t *event);
    void RunInUBEventThread();
    void ClearJettyResource(UBJetty *qp);
    inline bool ValidateRequestContext(UBOpContextInfo *ctx)
    {
        if (NN_UNLIKELY(ctx == nullptr || ctx->ubJetty == nullptr || ctx->ubJetty->GetUpContext() == 0 ||
            ctx->ubJetty->GetUpContext1() == 0)) {
            NN_LOG_ERROR("Ctx or QP or Worker or ep is null of RequestReceived in Driver " << mName << "");
            return false;
        }
        return true;
    }
    bool mNeedStopEvent = false;
    std::thread mUBEventThread;
    std::atomic<bool> mEventStarted{ false };
    NetHeartbeat *mHeartBeat = nullptr;
    UBPublicJetty *mPublicJetty = nullptr;
    friend class NetHeartbeat;
    friend class UBWorker;
};
}
}

#endif
#endif // HCOM_NET_UB_DRIVER_OOB_H
