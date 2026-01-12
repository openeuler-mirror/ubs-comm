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

#ifdef UB_BUILD_ENABLED
#include <fcntl.h>
#include <sys/poll.h>

#include "net_monotonic.h"
#include "net_oob_ssl.h"
#include "net_ub_endpoint.h"
#include "net_ub_driver_oob.h"
#include "net_oob_secure.h"
#include "ub_mr_fixed_buf.h"
#include "ub_urma_wrapper_public_jetty.h"
#include "ub_worker.h"

namespace ock {
namespace hcom {

NResult NetDriverUBWithOob::CreateUrmaListeners(UBPublicJetty *&publicJetty)
{
    if (mOobListenOptions.empty()) {
        NN_LOG_ERROR("No listen info is set for oob type " << UBSHcomNetDriverOobTypeToString(mOptions.oobType) <<
            " in driver " << mName);
        return NN_INVALID_PARAM;
    }
    for (auto &lOpt : mOobListenOptions) {
        uint32_t jettyId = lOpt.port;
        if (jettyId < PUBLIC_JETTY_NUM_MIN || jettyId > PUBLIC_JETTY_NUM_MAX) {
            NN_LOG_ERROR("Invalid public jetty id " << jettyId << " should in 4~1023");
            return NN_ERROR;
        }
        if ((CreatePublicJetty(mPublicJetty, jettyId, true)) != NN_OK) {
            NN_LOG_ERROR("Failed to create oob public jetty");
            return NN_ERROR;
        }
        mPublicJetty->IncreaseRef();
        auto twc = lOpt.targetWorkerCount == 0 ? UINT16_MAX : lOpt.targetWorkerCount;
        NetWorkerLBPtr lb = new (std::nothrow) NetWorkerLB(mName, mOptions.lbPolicy, twc);
        if (NN_UNLIKELY(lb == nullptr)) {
            NN_LOG_ERROR("Failed to new oob load balancer in driver " << mName);
            return NN_NEW_OBJECT_FAILED;
        }
        mPublicJetty->SetWorkerLb(lb.Get());

        /* add worker groups to lb */
        if (NN_UNLIKELY(lb->AddWorkerGroups(mWorkerGroups) != NN_OK)) {
            NN_LOG_ERROR("Failed to added worker groups into load balancer in driver " << mName);
            return NN_ERROR;
        }
    }

    return NN_OK;
}

NResult NetDriverUBWithOob::ConnectByPublicJetty(const std::string &oobIp, uint16_t oobPort, const std::string &payload,
    UBSHcomNetEndpointPtr &outEp, uint32_t flags, uint8_t serverGrpNo, uint8_t clientGrpNo, uint64_t ctx)
{
    if (ClientCheckState(payload) != 0) {
        NN_LOG_ERROR("Failed to connect as driver not start or payload oversize");
        return NN_ERROR;
    }
    if (oobPort < PUBLIC_JETTY_NUM_MIN || oobPort > PUBLIC_JETTY_NUM_MAX) {
        NN_LOG_ERROR("Invalid public jetty id " << oobPort << " should in 4~1023");
        return NN_ERROR;
    }
    if ((flags & NET_EP_SELF_POLLING) || (flags & NET_EP_EVENT_POLLING)) {
        return ConnectSyncEpByPublicJetty(oobIp, oobPort, payload, outEp, flags, serverGrpNo, clientGrpNo, ctx);
    }

    return ConnectASyncEpByPublicJetty(oobIp, oobPort, payload, outEp, flags, serverGrpNo, clientGrpNo, ctx);
}

NResult NetDriverUBWithOob::ConnectASyncEpByPublicJetty(const std::string &oobIp, uint16_t oobPort,
    const std::string &payload, UBSHcomNetEndpointPtr &outEp, uint32_t flags, uint8_t serverGrpNo, uint8_t clientGrpNo,
    uint64_t ctx)
{
    UBPublicJetty *clientPublicJetty = nullptr;
    if (PublicJettyConnect(oobIp, oobPort, clientPublicJetty) != 0) {
        NN_LOG_ERROR("Failed to connect to server public jetty");
        if (clientPublicJetty != nullptr) {
            delete clientPublicJetty;
            clientPublicJetty = nullptr;
        }
        return NN_ERROR;
    }
    NetLocalAutoDecreasePtr<UBPublicJetty> publicJettyAutoDecPtr(clientPublicJetty);
    UBPublicJetty *clientControlJetty = nullptr;
    if (CreatePublicJetty(clientControlJetty, 0, false) != NN_OK) {
        NN_LOG_ERROR("Failed to create control jetty in client");
        return NN_ERROR;
    }
    NetLocalAutoDecreasePtr<UBPublicJetty> clientControlJettyAutoDecPtr(clientControlJetty);
    if (clientControlJetty->StartPublicJetty() != NN_OK) {
        NN_LOG_ERROR("Failed to start control jetty in client");
        return NN_ERROR;
    }
    // choose worker
    auto id = NetUuid::GenerateUuid();
    UBWorker *worker = nullptr;
    if (ClientSelectWorker(worker, clientGrpNo, id) != 0) {
        NN_LOG_ERROR("Failed to select worker in connection");
        return NN_ERROR;
    }
    // create rc jetty
    UBJetty *qp = nullptr;
    uint32_t token = GenerateSecureRandomUint32();
    if (ClientCreateJetty(qp, worker, token) != 0) {
        NN_LOG_ERROR("Failed to create jetty in client");
        if (qp != nullptr) {
            delete qp;
            qp = nullptr;
        }
        return NN_ERROR;
    }
    NetLocalAutoDecreasePtr<UBJetty> qpAutoDecPtr(qp);
    // fill exchange info
    if (ClientSendConnReq(payload, id, serverGrpNo, clientPublicJetty, qp, clientControlJetty, token) != 0) {
        NN_LOG_ERROR("Failed to send connect request to server");
        return NN_ERROR;
    }
    // recv exchange info from server
    UBJettyExchangeInfo info{};
    if (ClientEstablishConnOnReply(clientControlJetty, qp, info) != 0) {
        NN_LOG_ERROR("Failed to establish connection on ack in client");
        return NN_ERROR;
    }
    if (PrePostReceiveOnConnection(qp, worker) != 0) {
        NN_LOG_ERROR("Failed to pre postrecv in public client connections");
        ClearJettyResource(qp);
        return NN_ERROR;
    }
    /* Create endpoint */
    if (ClientCreateEp(outEp, id, qp, worker, info, clientControlJetty) != 0) {
        NN_LOG_ERROR("Failed to create ep in public client connection");
        ClearJettyResource(qp);
        return NN_ERROR;
    }

    return NN_OK;
}

NResult NetDriverUBWithOob::ClientCheckState(const std::string &payload)
{
    if (NN_UNLIKELY(!mInited.load())) {
        NN_LOG_ERROR("Driver " << mName << " is not initialized");
        return NN_NOT_INITIALIZED;
    }

    if (NN_UNLIKELY(!mStarted)) {
        NN_LOG_ERROR("Failed to connect on driver " << mName << " as it is not started");
        return NN_ERROR;
    }

    if (payload.size() >= NN_NO1024) {
        NN_LOG_ERROR("Failed to connect to server via payload size " << payload.size() <<
            " over limit size " << NN_NO1024);
        return NN_INVALID_PARAM;
    }
    return NN_OK;
}

NResult NetDriverUBWithOob::CreatePublicJetty(UBPublicJetty *&publicJetty, uint32_t id, bool isServer)
{
    NResult result = NN_OK;
    auto tmpJfc = new (std::nothrow) UBJfc(mName, mContext, false);
    if (tmpJfc == nullptr) {
        NN_LOG_ERROR("Failed to create jfc in public jetty");
        return NN_ERROR;
    }
    result = tmpJfc->Initialize();
    if (result != UB_OK) {
        NN_LOG_ERROR("Jfc initialize failed in create public jetty " << result);
        delete(tmpJfc);
        return result;
    }
    publicJetty = new (std::nothrow) UBPublicJetty(mName, id, mContext, tmpJfc, isServer);
    if (publicJetty == nullptr) {
        NN_LOG_ERROR("Failed to create public jetty");
        delete(tmpJfc);
        return NN_ERROR;
    }
    if ((publicJetty->InitializePublicJetty(id)) != NN_OK) {
        NN_LOG_ERROR("Failed to initialize public jetty");
        delete(publicJetty);
        publicJetty = nullptr;
        return NN_ERROR;
    }
    return NN_OK;
}

NResult NetDriverUBWithOob::PublicJettyConnect(const std::string &oobIp, uint16_t oobPort,
    UBPublicJetty *&clientPublicJetty)
{
    urma_eid_t remoteEid{};
    if (CreatePublicJetty(clientPublicJetty, 0, false) != NN_OK) {
        NN_LOG_ERROR("Failed to create public jetty in client");
        goto ERROR_FREE;
    }

    if (clientPublicJetty->StartPublicJetty() != NN_OK) {
        NN_LOG_ERROR("Failed to start public jetty in client");
        goto ERROR_FREE;
    }

    if (HcomUrma::StrToEid(oobIp.c_str(), &remoteEid) != 0) {
        NN_LOG_ERROR("Failed to convert to eid as eid illegal");
        goto ERROR_FREE;
    }

    if (clientPublicJetty->ImportPublicJetty(remoteEid, oobPort) != 0) {
        goto ERROR_FREE;
    }
    return NN_OK;

ERROR_FREE:
    if (clientPublicJetty != nullptr) {
        delete clientPublicJetty;
        clientPublicJetty = nullptr;
    }
    return NN_ERROR;
}

NResult NetDriverUBWithOob::ClientSelectWorker(UBWorker *&worker, uint8_t clientGrpNo, uint64_t id)
{
    uint16_t workerIndex = 0;
    if (NN_UNLIKELY(!mClientLb->ChooseWorker(clientGrpNo, std::to_string(id), workerIndex)) ||
        workerIndex >= mWorkers.size()) {
        NN_LOG_ERROR("Failed to choose worker during connect in driver " << mName);
        return NN_ERROR;
    }

    NN_ASSERT_LOG_RETURN(workerIndex < mWorkers.size(), NN_ERROR)
    worker = mWorkers[workerIndex];
    NN_ASSERT_LOG_RETURN(worker != nullptr, NN_ERROR)

    return NN_OK;
}

NResult NetDriverUBWithOob::ClientSendConnReq(const std::string payload, uint64_t id, uint8_t serverGrpNo,
    UBPublicJetty *clientPublicJetty, UBJetty *qp, UBPublicJetty *clientControlJetty, uint32_t token)
{
    if (NN_UNLIKELY(clientPublicJetty == nullptr)) {
        NN_LOG_ERROR("Failed to send connection request as clientPublicJetty is nullptr");
        return UB_PARAM_INVALID;
    }
    uint32_t msgSize = sizeof(JettyConnHeader) - 1024 + payload.size() + 1;
    JettyConnHeader exchangeInfo;
    exchangeInfo.epId = id;
    exchangeInfo.info.token = token;
    if (FillExchMsg(&exchangeInfo, qp, payload, serverGrpNo, clientControlJetty) != 0) {
        NN_LOG_ERROR("Failed to fill exchange message in client public jetty");
        return NN_ERROR;
    }
    NN_LOG_INFO("Client send exchangeInfo clientControlJettyId = " << exchangeInfo.controlJettyId << " jettyId = "
        << exchangeInfo.info.jettyId.id);
    // send to server
    if (clientPublicJetty->SendByPublicJetty(&exchangeInfo, msgSize) != 0) {
        NN_LOG_ERROR("Failed to send data to server public jetty");
        return NN_ERROR;
    }
    if (clientPublicJetty->PollingCompletion() != 0) {
        NN_LOG_ERROR("Failed to poll completion in client public jetty");
        return NN_ERROR;
    }
    return NN_OK;
}

NResult NetDriverUBWithOob::CheckServerACK(JettyConnResp &exchangeMsg)
{
    auto serverAcks = exchangeMsg.connResp;
    switch (serverAcks) {
        case MAGIC_MISMATCH:
            NN_LOG_ERROR("Failed to pass server magic validation " << mName << ", result " << serverAcks);
            return NN_CONNECT_REFUSED;
        case WORKER_GRPNO_MISMATCH:
        case WORKER_NOT_STARTED:
            NN_LOG_ERROR("Failed to choose worker or not started " << mName << ", result " << serverAcks);
            return NN_CONNECT_REFUSED;
        case PROTOCOL_MISMATCH:
            NN_LOG_ERROR("Failed to pass server protocol validation " << mName << ", result " << serverAcks);
            return NN_CONNECT_PROTOCOL_MISMATCH;
        case SERVER_INTERNAL_ERROR:
            NN_LOG_ERROR("Server error happened, connection refused " << mName << ", result " << serverAcks);
            return NN_ERROR;
        case VERSION_MISMATCH:
            NN_LOG_ERROR("Failed to pass server version validation " << mName << ", result " << serverAcks);
            return NN_CONNECT_REFUSED;
        case TLS_VERSION_MISMATCH:
            NN_LOG_ERROR("Failed to pass server tls version validation " << mName << ", result " << serverAcks);
            return NN_CONNECT_REFUSED;
        case OK:
            break;
        default:
            NN_LOG_ERROR("Server error happened, connection refused " << mName << ", result " << serverAcks);
            return NN_ERROR;
    }
    return NN_OK;
}

NResult NetDriverUBWithOob::ClientEstablishConnOnReply(UBPublicJetty *clientControlJetty, UBJetty *qp,
    UBJettyExchangeInfo &info)
{
    if (NN_UNLIKELY(qp == nullptr || clientControlJetty == nullptr)) {
        NN_LOG_ERROR("Failed to establish connection on reply as qp or clientControlJetty is nullptr");
        return UB_PARAM_INVALID;
    }
    JettyConnResp exchangeMsg{};
    if (clientControlJetty->Receive(&exchangeMsg, sizeof(JettyConnResp)) != 0) {
        NN_LOG_ERROR("Failed to receive exchange message");
        return NN_ERROR;
    }
    NN_LOG_INFO("Client recv exchangeMsg serverControlJetty id = " << exchangeMsg.serverCtrlJettyId << " jettyId = "
        << exchangeMsg.info.jettyId.id);
    if (CheckServerACK(exchangeMsg) != 0) {
        NN_LOG_ERROR("Failed to check server ack in client public jetty");
        return NN_ERROR;
    }
    if (clientControlJetty->ImportPublicJetty(exchangeMsg.serverCtrlEid, exchangeMsg.serverCtrlJettyId) != 0) {
        NN_LOG_ERROR("Failed to import client jetty in public server");
        return NN_ERROR;
    }
    info = exchangeMsg.info;
    std::unique_ptr<UBJettyExchangeInfo> peerExInfo(new (std::nothrow) UBJettyExchangeInfo(info));
    if (!peerExInfo) {
        NN_LOG_ERROR("Failed to alloc UBJettyExchangeInfo in Driver " << mName);
        return NN_MALLOC_FAILED;
    }
    qp->StoreExchangeInfo(peerExInfo.release());

    // import and bind rc jetty
    if (qp->ChangeToReady(info) != 0) {
        NN_LOG_ERROR("Failed to change qp to ready in Driver " << mName);
        int8_t clientAck = -1;
        clientControlJetty->SendByPublicJetty(&clientAck, sizeof(int8_t));
        clientControlJetty->PollingCompletion();
        return NN_ERROR;
    }
    return NN_OK;
}

NResult NetDriverUBWithOob::ClientCreateJetty(UBJetty *&qp, UBWorker *worker, uint32_t token)
{
    if (NN_UNLIKELY(worker == nullptr)) {
        NN_LOG_ERROR("Failed to create jetty in client as worker is nullptr");
        return NN_PARAM_INVALID;
    }
    int result = 0;
    if ((result = worker->CreateQP(qp)) != 0) {
        NN_LOG_ERROR("Failed to create qp for new connection in Driver " << mName << " , result " << result);
        goto ERROR_FREE;
    }
    qp->SetName(mName);
    if ((result = qp->Initialize(mOptions.mrSendReceiveSegCount, 0, token)) != 0) {
        NN_LOG_ERROR("Failed to initialize qp for new connection in Driver " << mName << " , result " << result);
        goto ERROR_FREE;
    }
    return NN_OK;

ERROR_FREE:
    if (qp != nullptr) {
        delete qp;
        qp = nullptr;
    }
    return NN_ERROR;
}

NResult NetDriverUBWithOob::ClientCreateEp(UBSHcomNetEndpointPtr &outEp, uint64_t id, UBJetty *qp, UBWorker *worker,
    UBJettyExchangeInfo &info, UBPublicJetty *clientControlJetty)
{
    if (NN_UNLIKELY(qp == nullptr || worker == nullptr)) {
        NN_LOG_ERROR("Failed to create ep in client as qp or worker is nullptr");
        return NN_PARAM_INVALID;
    }
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetUBAsyncEndpoint(id, qp, this, worker);
    if (ep.Get() == nullptr) {
        NN_LOG_ERROR("Failed to create UBSHcomNetEndpoint in Driver " << mName << ", probably out of memory");
        return NN_NEW_OBJECT_FAILED;
    }
    ep.ToChild<NetUBAsyncEndpoint>()->SetRemoteHbInfo(info.hbAddress, info.hbKey, info.hbMrSize);
    int8_t clientAck = 1;
    NN_LOG_INFO("clientControlJetty send clientAck jetty id: " << clientControlJetty->GetJettyId());
    if (clientControlJetty->SendByPublicJetty(&clientAck, sizeof(int8_t)) != 0) {
        NN_LOG_ERROR("Failed to send ready signal in public client jetty id: " << clientControlJetty->GetJettyId());
        return NN_ERROR;
    }
    if (clientControlJetty->PollingCompletion() != 0) {
        NN_LOG_ERROR("Failed to poll completion in clientControlJetty jetty id: " << clientControlJetty->GetJettyId());
        return NN_ERROR;
    }
    int8_t serverAck = -1;
    if (clientControlJetty->Receive(&serverAck, sizeof(int8_t)) != 0) {
        NN_LOG_ERROR("Failed to receive serverAck signal from server jetty id: " << clientControlJetty->GetJettyId());
        return NN_ERROR;
    }
    if (serverAck != 1) {
        NN_LOG_ERROR("Failed to check serverAck signal from server jetty id: " << clientControlJetty->GetJettyId());
        return NN_ERROR;
    }

    // \see NetDriverUBWithOob::NewConnectionCB
    qp->SetUpContext(reinterpret_cast<uintptr_t>(ep.Get()));
    ep->State().Set(NEP_ESTABLISHED);
    {
        std::lock_guard<std::mutex> locker(mEndPointsMutex);
        mEndPoints.emplace(ep->Id(), ep);
    }
    NN_LOG_INFO("New connection established via public jetty, async ep id " << ep->Id() << ", jetty id: "
                << qp->QpNum() << ", worker info " << worker->DetailName());
    outEp = ep;
    reinterpret_cast<NetUBAsyncEndpoint *>(ep.Get())->GetQp()->SetUpId(ep->Id());
    return NN_OK;
}


NResult NetDriverUBWithOob::ServerEstablishCtrlConn(JettyConnHeader *exchangeInfo, UBPublicJetty *serverControlJetty)
{
    if (NN_UNLIKELY(exchangeInfo == nullptr || serverControlJetty == nullptr)) {
        NN_LOG_ERROR("Failed to establish control connection as exchangeInfo or serverControlJetty is nullptr");
        return NN_PARAM_INVALID;
    }
    NN_LOG_INFO("Server recv exchangeInfo clientControlJettyId = " << exchangeInfo->controlJettyId << " jettyId = "
        << exchangeInfo->info.jettyId.id);
    urma_eid_t remoteEid = exchangeInfo->info.eid;
    if (serverControlJetty->StartPublicJetty() != NN_OK) {
        NN_LOG_ERROR("Failed to start public jetty in client");
        return NN_ERROR;
    }
    if (serverControlJetty->ImportPublicJetty(remoteEid, exchangeInfo->controlJettyId) != 0) {
        NN_LOG_ERROR("Failed to import client jetty in public server");
        return NN_ERROR;
    }
    return NN_OK;
}

NResult NetDriverUBWithOob::PublicJettyNewConnectionCB(UBOpContextInfo *ctx)
{
    auto exchangeInfo = reinterpret_cast<JettyConnHeader *>(ctx->mrMemAddr);
    NN_ASSERT_LOG_RETURN(exchangeInfo != nullptr, NN_ERROR)
    // connect to client public jetty
    UBPublicJetty *serverControlJetty = nullptr;
    if (CreatePublicJetty(serverControlJetty, 0, false) != NN_OK) {
        NN_LOG_ERROR("Failed to create public jetty in client");
        return NN_ERROR;
    }
    NetLocalAutoDecreasePtr<UBPublicJetty> serverControlJettyAutoDecPtr(serverControlJetty);
    // check connect info
    JettyConnResp exchangeMsg{};
    exchangeMsg.msgType = UrmaConnectMsgType::EXCHANGE_MSG;
    exchangeMsg.connResp = ConnectResp::OK;
    if (ServerEstablishCtrlConn(exchangeInfo, serverControlJetty) != NN_OK) {
        NN_LOG_ERROR("Failed to establish control connection in server");
        return NN_ERROR;
    }
    if (CheckMagicAndProtocol(exchangeMsg, exchangeInfo, serverControlJetty) != 0) {
        NN_LOG_ERROR("Failed to check magic number or protocol");
        return NN_ERROR;
    }
    // choose worker
    UBWorker *worker = nullptr;
    if (ServerSelectWorker(worker, exchangeMsg, exchangeInfo->ConnectHeader.groupIndex, serverControlJetty) != 0) {
        NN_LOG_ERROR("Failed to select in public server");
        return NN_ERROR;
    }
    // Create RC Jetty
    UBJetty *qp = nullptr;
    uint32_t token = GenerateSecureRandomUint32();
    if (ServerCreateJetty(qp, worker, exchangeMsg, exchangeInfo, serverControlJetty, token) != 0) {
        NN_LOG_ERROR("Failed to create jetty in new connection callback");
        if (qp != nullptr) {
            delete qp;
            qp = nullptr;
        }
        return NN_ERROR;
    }
    NetLocalAutoDecreasePtr<UBJetty> qpAutoDecPtr(qp);
    // send exchange info back to client
    if (ServerReplyMsg(qp, exchangeMsg, serverControlJetty, token) != 0) {
        NN_LOG_ERROR("Failed to reply message to client");
        return NN_ERROR;
    }
    if (PrePostReceiveOnConnection(qp, worker) != 0) {
        NN_LOG_ERROR("Failed to pre postrecv in public server connection cb");
        ClearJettyResource(qp);
        return NN_ERROR;
    }
    // Create endpoint
    if (ServerCreateEp(exchangeInfo->info, qp, worker, exchangeInfo, serverControlJetty) != 0) {
        NN_LOG_ERROR("Failed to create ep in public server connection cb");
        ClearJettyResource(qp);
        return NN_ERROR;
    }

    return NN_OK;
}

void NetDriverUBWithOob::ClearJettyResource(UBJetty *qp)
{
    if (qp == nullptr) {
        NN_LOG_WARN("Failed to clear jetty resource as jetty is nullptr");
        return;
    }

    // 建链失败时 EP 会先于 jetty 析构，此种情况下需要保证在触发 FLUSH_ERR_DONE 时 jetty 无法索引到 已析构的 EP，清理工
    // 作全权由本函数 ClearJettyResource 负责。
    qp->SetUpContext(0);
    qp->Stop();

    UBOpContextInfo *it = nullptr;
    UBOpContextInfo *next = nullptr;
    qp->GetCtxPosted(it);
    while (it != nullptr) {
        next = it->next;
        if (it->opType != UBOpContextInfo::OpType::RECEIVE) {
            NN_LOG_ERROR("Failed to clear jetty resource as invalid type");
        }
        ProcessErrorNewRequest(it);

        // 至此，it指向的内存可能会归还给 mempool，再修改it指向的内存可能会引起并发冲突
        it = next;
    }
    return;
}

NResult NetDriverUBWithOob::ServerCreateEp(UBJettyExchangeInfo &info, UBJetty *qp, UBWorker *worker,
    JettyConnHeader *exchangeInfo, UBPublicJetty *serverControlJetty)
{
    if (NN_UNLIKELY(qp == nullptr || worker == nullptr)) {
        NN_LOG_ERROR("Failed to create ep in server as qp or worker is nullptr");
        return NN_PARAM_INVALID;
    }

    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetUBAsyncEndpoint(exchangeInfo->epId, qp, this, worker);
    if (ep.Get() == nullptr) {
        NN_LOG_ERROR("Failed to create UBSHcomNetEndpoint in Driver " << mName << ", probably out of memory");
        return NN_NEW_OBJECT_FAILED;
    }

    ep.ToChild<NetUBAsyncEndpoint>()->SetRemoteHbInfo(info.hbAddress, info.hbKey, info.hbMrSize);
    ep->mDevIndex = mDevIndex;
    ep->mPeerDevIndex = mPeerDevIndex;
    ep->mBandWidth = mBandWidth;

    std::string payload;
    auto payloadLen = exchangeInfo->payloadLen;
    if ((payloadLen == 0) | (payloadLen >= NN_NO1024)) {
        NN_LOG_ERROR("Failed to create ep in server as exchangeInfo payloadLen " << payloadLen << " is invalid");
        return NN_PARAM_INVALID;
    }
    if (payloadLen > 0) {
        exchangeInfo->payload[payloadLen] = '\0';
        payload = std::string(exchangeInfo->payload, payloadLen);
    }
    struct in_addr ipAddr{};
    char ipStr[INET_ADDRSTRLEN]{};
    ipAddr.s_addr = exchangeInfo->info.eid.in4.addr;
    if (inet_ntop(AF_INET, &ipAddr, ipStr, INET_ADDRSTRLEN) == NULL) {
        NN_LOG_ERROR("Failed to convert ip num to string");
        return NN_ERROR;
    }
    auto listenJettyId = exchangeInfo->controlJettyId;
    std::string eidAndPort = std::string(ipStr) + ":" + std::to_string(listenJettyId);
    ep->StoreConnInfo(exchangeInfo->info.eid.in4.addr, listenJettyId, exchangeInfo->ConnectHeader.version, payload);
    
    // client                         server
    //    -----------client ack---------->
    //                                  NewEndpointHandler()
    //    <----------server ack-----------
    // 客户端 EP创建完毕
    int8_t clientAck = -1;
    if (serverControlJetty->Receive(&clientAck, sizeof(int8_t)) != 0) {
        NN_LOG_ERROR("Failed to receive clientAck signal from client jetty id: " << serverControlJetty->GetJettyId());
        return NN_ERROR;
    }
    if (clientAck != 1) {
        NN_LOG_ERROR("Failed to check clientAck signal from client jetty id: " << serverControlJetty->GetJettyId());
        return NN_ERROR;
    }

    NResult result = NN_OK;
    NN_LOG_INFO("ServerControlJetty send serverAck jetty id: " << serverControlJetty->GetJettyId());
    if (NN_UNLIKELY(mNewEndPointHandler(eidAndPort, ep, payload) != UB_OK)) {
        NN_LOG_ERROR("Called new end point handler failed jetty id: " << serverControlJetty->GetJettyId());
        result = NN_ERROR;
    }

    // \see NetDriverUBWithOob::NewConnectionCB
    qp->SetUpContext(reinterpret_cast<uintptr_t>(ep.Get()));
    qp->SetUpId(ep->Id());
    ep->State().Set(NEP_ESTABLISHED);

    // serverAck 同步信令发送后客户端可能会立即发包，会在UBWorker中被动触发事件。如果 jetty 无法通过 UpContext() 索引
    // 到ep， 此 ep上产生的事件无法被 UBWorker 转发至回调。因此发送 serverAck 同步信令必须位于 qp->SetUpContext(...)之
    // 后。
    // \see NetDriverUBWithOob::NewConnectionCB
    int8_t serverAck = (result == NN_OK) ? 1 : 0;
    if (serverControlJetty->SendByPublicJetty(&serverAck, sizeof(int8_t)) != 0) {
        NN_LOG_ERROR("Failed to send serverAck signal in public server jetty id: " << serverControlJetty->GetJettyId());
        return NN_ERROR;
    }
    if (serverControlJetty->PollingCompletion() != 0) {
        NN_LOG_ERROR("Failed to poll completion in client serverControlJetty jetty id: " <<
            serverControlJetty->GetJettyId());
        return NN_ERROR;
    }
    NN_LOG_INFO("serverControlJetty end ServerHandshake jetty id: " << serverControlJetty->GetJettyId());

    // EP 被安全创建完毕
    {
        std::lock_guard<std::mutex> locker(mEndPointsMutex);
        mEndPoints.emplace(ep->Id(), ep);
    }

    NN_LOG_INFO("New connection build via public jetty, ep id " << ep->Id() << ", jetty id: " << qp->QpNum()
                << ", worker info " << worker->DetailName());
    return NN_OK;
}

NResult NetDriverUBWithOob::CheckMagicAndProtocol(JettyConnResp &exchangeMsg, JettyConnHeader *exchangeInfo,
    UBPublicJetty *serverControlJetty)
{
    auto header = exchangeInfo->ConnectHeader;
    if (header.magic != mOptions.magic) {
        NN_LOG_ERROR("Failed to match magic number from client, connection refused header.magic = " << header.magic <<
            ", mOptions.magic = " << mOptions.magic);
        exchangeMsg.connResp = MAGIC_MISMATCH;
        serverControlJetty->SendByPublicJetty(&exchangeMsg, sizeof(JettyConnResp));
        return NN_ERROR;
    }
    if (header.protocol != Protocol()) {
        NN_LOG_ERROR("Failed to match protocol " << Protocol() << " vs " << header.protocol << " connection refused");
        exchangeMsg.connResp = PROTOCOL_MISMATCH;
        serverControlJetty->SendByPublicJetty(&exchangeMsg, sizeof(JettyConnResp));
        return NN_ERROR;
    }
    return NN_OK;
}

NResult NetDriverUBWithOob::FillExchMsg(JettyConnHeader *exchangeInfo, UBJetty *qp,
    const std::string &payload, uint8_t serverGrpNo, UBPublicJetty *clientControlJetty)
{
    int result = 0;
    exchangeInfo->msgType = CONNECT_REQ;
    exchangeInfo->controlJettyId = clientControlJetty->GetJettyId();
    exchangeInfo->SetConnHeader(mOptions.magic, mOptions.version, serverGrpNo, Protocol(), mMajorVersion, mMinorVersion,
        mOptions.tlsVersion);

    exchangeInfo->info.maxSendWr = mOptions.qpSendQueueSize;
    exchangeInfo->info.maxReceiveWr = mOptions.qpReceiveQueueSize;
    exchangeInfo->info.receiveSegSize = mOptions.mrSendReceiveSegSize;
    exchangeInfo->info.receiveSegCount = mOptions.prePostReceiveSizePerQP;
    if (mHeartBeat != nullptr) {
        if ((result = qp->CreateHBMemoryRegion(NN_NO128, qp->mHBLocalMr)) != NN_OK) {
            NN_LOG_ERROR("Failed to create mr for local HB, result: " << result);
            return result;
        }
        if ((result = qp->CreateHBMemoryRegion(NN_NO128, qp->mHBRemoteMr)) != NN_OK) {
            NN_LOG_ERROR("Failed to create mr for remote HB, result: " << result);
            qp->DestroyHBMemoryRegion(qp->mHBLocalMr);
            return result;
        }
        qp->GetRemoteHbInfo(exchangeInfo->info);
        exchangeInfo->info.isNeedSendHb = true;
    }
    if ((result = qp->FillExchangeInfo(exchangeInfo->info)) != 0) {
        NN_LOG_ERROR("Failed to get or send ep exchange info in Driver " << mName << ", result: " << result);
        return result;
    }
    if (payload.size() >= NN_NO1024) {
        NN_LOG_ERROR("Failed to copy data as payload is too long " << payload.size());
        return NN_ERROR;
    }
    if (NN_UNLIKELY(memcpy_s(exchangeInfo->payload, NN_NO1024, payload.c_str(), payload.size())
        != NN_OK)) {
        NN_LOG_ERROR("Failed to copy data");
        return NN_ERROR;
    }
    exchangeInfo->payloadLen = payload.size();
    exchangeInfo->payload[exchangeInfo->payloadLen] = '\0';
    return NN_OK;
}

NResult NetDriverUBWithOob::PrePostReceiveOnConnection(UBJetty *qp, UBWorker *worker)
{
    int result = 0;
    if (NN_UNLIKELY(qp == nullptr || worker == nullptr)) {
        NN_LOG_ERROR("Failed to pre postrecv as qp is nullptr");
        return UB_PARAM_INVALID;
    }
    auto prePostCount = mOptions.prePostReceiveSizePerQP;
    auto *mrSegs = new (std::nothrow) uintptr_t[prePostCount];
    if (mrSegs == nullptr) {
        NN_LOG_ERROR("Failed to create mr address array in Driver " << mName << ", probably out of memory");
        return NN_NEW_OBJECT_FAILED;
    }

    NetLocalAutoFreePtr<uintptr_t> segAutoDelete(mrSegs, true);

    if (!qp->GetFreeBufferN(mrSegs, prePostCount)) {
        NN_LOG_ERROR("Failed to get free mr from pool");
        return NN_ERROR;
    }

    uint16_t i = 0;
    for (; i < prePostCount; i++) {
        if ((result = worker->PostReceive(qp, mrSegs[i], mOptions.mrSendReceiveSegSize,
            reinterpret_cast<urma_target_seg_t *>(qp->GetMemorySeg()))) != 0) {
            break;
        }
    }

    for (; i < prePostCount; i++) {
        qp->ReturnBuffer(mrSegs[i]);
    }

    return result;
}

NResult NetDriverUBWithOob::ServerSelectWorker(UBWorker *&worker, JettyConnResp &exchangeMsg,
    uint8_t groupIndex, UBPublicJetty *serverControlJetty)
{
    uint16_t workerIndex = 0;
    NetWorkerLBPtr lb = mPublicJetty->LoadBalancer();
    NN_ASSERT_LOG_RETURN(lb.Get() != nullptr, NN_ERROR)
    if (NN_UNLIKELY(!lb->ChooseWorker(groupIndex, mOobIp, workerIndex)) ||
        workerIndex >= mWorkers.size()) {
        exchangeMsg.connResp = WORKER_GRPNO_MISMATCH;
        serverControlJetty->SendByPublicJetty(&exchangeMsg, sizeof(JettyConnResp));
        return NN_ERROR;
    }
    worker = mWorkers[workerIndex];
    NN_ASSERT_LOG_RETURN(worker != nullptr, NN_ERROR)
    if (!worker->IsWorkStarted()) {
        NN_LOG_ERROR("Failed to connect worker group no " << groupIndex << " in " << mName);
        exchangeMsg.connResp = WORKER_NOT_STARTED;
        serverControlJetty->SendByPublicJetty(&exchangeMsg, sizeof(JettyConnResp));
        return NN_ERROR;
    }
    return NN_OK;
}

NResult NetDriverUBWithOob::ServerCreateJetty(UBJetty *&qp, UBWorker *worker, JettyConnResp &exchangeMsg,
    JettyConnHeader *exchangeInfo, UBPublicJetty *serverControlJetty, uint32_t token)
{
    int result = 0;
    uint64_t epId = exchangeInfo->epId;
    if ((result = worker->CreateQP(qp)) != 0) {
        NN_LOG_ERROR("Failed to create qp for new connection in Driver " << mName << " , result " << result);
        exchangeMsg.connResp = SERVER_INTERNAL_ERROR;
        serverControlJetty->SendByPublicJetty(&exchangeMsg, sizeof(JettyConnResp));
        return NN_ERROR;
    }
    qp->SetName(mName);
    if ((result = qp->Initialize(mOptions.mrSendReceiveSegCount, 0, token)) != 0) {
        NN_LOG_ERROR("Failed to initialize qp for new connection in Driver " << mName << " , result " << result);
        exchangeMsg.connResp = SERVER_INTERNAL_ERROR;
        serverControlJetty->SendByPublicJetty(&exchangeMsg, sizeof(JettyConnResp));
        delete qp;
        qp = nullptr;
        return NN_ERROR;
    }
    UBJettyExchangeInfo info = exchangeInfo->info;
    std::unique_ptr<UBJettyExchangeInfo> peerExInfo(new (std::nothrow) UBJettyExchangeInfo(info));
    if (!peerExInfo) {
        NN_LOG_ERROR("Failed to alloc UBJettyExchangeInfo in Driver " << mName);
        delete qp;
        qp = nullptr;
        return NN_MALLOC_FAILED;
    }
    qp->StoreExchangeInfo(peerExInfo.release());

    if ((result = qp->ChangeToReady(info)) != 0) {
        NN_LOG_ERROR("Failed to change qp to ready in Driver " << mName << ", result " << result);
        exchangeMsg.connResp = SERVER_INTERNAL_ERROR;
        serverControlJetty->SendByPublicJetty(&exchangeMsg, sizeof(JettyConnResp));
        delete qp;
        qp = nullptr;
        return result;
    }
    return NN_OK;
}

NResult NetDriverUBWithOob::ServerReplyMsg(UBJetty *qp, JettyConnResp &exchangeMsg, UBPublicJetty *serverControlJetty,
    uint32_t token)
{
    if (NN_UNLIKELY(qp == nullptr)) {
        NN_LOG_ERROR("Failed to reply message as qp is nullptr");
        return UB_PARAM_INVALID;
    }
    int result = 0;
    exchangeMsg.info.maxSendWr = mOptions.qpSendQueueSize;
    exchangeMsg.info.maxReceiveWr = mOptions.qpReceiveQueueSize;
    exchangeMsg.info.receiveSegSize = mOptions.mrSendReceiveSegSize;
    exchangeMsg.info.receiveSegCount = mOptions.prePostReceiveSizePerQP;
    exchangeMsg.serverCtrlJettyId = serverControlJetty->GetJettyId();
    exchangeMsg.serverCtrlEid = serverControlJetty->GetEid();
    exchangeMsg.info.token = token;
    if (mHeartBeat != nullptr) {
        if ((result = qp->CreateHBMemoryRegion(NN_NO128, qp->mHBLocalMr)) != NN_OK) {
            NN_LOG_ERROR("Failed to create mr for local HB in server, result " << result);
            return result;
        }

        if ((result = qp->CreateHBMemoryRegion(NN_NO128, qp->mHBRemoteMr)) != NN_OK) {
            NN_LOG_ERROR("Failed to create mr for remote HB, result " << result);
            qp->DestroyHBMemoryRegion(qp->mHBLocalMr);
            return result;
        }

        qp->GetRemoteHbInfo(exchangeMsg.info);
        exchangeMsg.info.isNeedSendHb = true;
    }

    if ((result = qp->FillExchangeInfo(exchangeMsg.info)) != 0) {
        NN_LOG_ERROR("Failed to get or send ep exchange info in Driver " << mName << ", result " << result);
        return result;
    }
    NN_LOG_INFO("Server send exchangeMsg serverControlJetty = " << exchangeMsg.serverCtrlJettyId << " jettyId = "
        << exchangeMsg.info.jettyId.id);
    if (serverControlJetty->SendByPublicJetty(&exchangeMsg, sizeof(JettyConnResp)) != 0) {
        NN_LOG_ERROR("Failed to send data in public server");
        return NN_ERROR;
    }
    if (serverControlJetty->PollingCompletion() != 0) {
        NN_LOG_ERROR("Failed to poll completion in server serverControlJetty");
        return NN_ERROR;
    }
    return NN_OK;
}

NResult NetDriverUBWithOob::CreateSyncEp(UBJetty *qp, UBJfc *cq, uint64_t id, UBSHcomNetEndpointPtr &outEp,
                                         UBPublicJetty *clientControlJetty)
{
    auto prePostCount = mOptions.prePostReceiveSizePerQP;
    static UBSHcomNetWorkerIndex workerIndex;
    workerIndex.driverIdx = mIndex;
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetUBSyncEndpoint(id, qp, cq, prePostCount + NN_NO4, this,
        workerIndex);
    if (ep.Get() == nullptr) {
        NN_LOG_ERROR("Failed to create UB sync endpoint in Driver " << mName << ", probably out of memory");
        return NN_NEW_OBJECT_FAILED;
    }
    NN_LOG_INFO("Create sync ep success, ep id: " << ep->Id() << ", with jetty id: " << qp->QpNum());

    if (reinterpret_cast<NetUBSyncEndpoint *>(ep.Get())->mCtxPool.Initialize() != UB_OK) {
        NN_LOG_ERROR("Fail to initialize mCtxPool");
    }
    if (PrePostReceiveOnSyncEp(ep, prePostCount, qp) != 0) {
        NN_LOG_ERROR("Failed to pre post recv in client sync ep");
        return NN_ERROR;
    }

    int8_t clientAck = 1;
    NN_LOG_INFO("clientControlJetty send clientAck jetty id: " << clientControlJetty->GetJettyId());
    if (clientControlJetty->SendByPublicJetty(&clientAck, sizeof(clientAck)) != 0) {
        NN_LOG_ERROR("Failed to send ready signal in public client jetty id: " << clientControlJetty->GetJettyId());
        return NN_ERROR;
    }
    if (clientControlJetty->PollingCompletion() != 0) {
        NN_LOG_ERROR("Failed to poll completion in clientControlJetty jetty id: " << clientControlJetty->GetJettyId());
        return NN_ERROR;
    }

    int8_t serverAck = 1;
    if (clientControlJetty->Receive(&serverAck, sizeof(serverAck)) != 0) {
        NN_LOG_ERROR("Failed to receive serverAck signal from server jetty id: " << clientControlJetty->GetJettyId());
        return NN_ERROR;
    }
    if (serverAck != 1) {
        NN_LOG_ERROR("Failed to check serverAck signal from server jetty id: " << clientControlJetty->GetJettyId());
        return NN_ERROR;
    }

    ClientSyncEpSetInfo(ep, qp, outEp);
    return NN_OK;
}

NResult NetDriverUBWithOob::ConnectSyncEpByPublicJetty(const std::string &oobIp, uint16_t oobPort,
    const std::string &payload, UBSHcomNetEndpointPtr &outEp, uint32_t flags, uint8_t serverGrpNo, uint8_t clientGrpNo,
    uint64_t ctx)
{
    // create public jetty and connect
    UBPublicJetty *clientPublicJetty = nullptr;
    if (PublicJettyConnect(oobIp, oobPort, clientPublicJetty) != 0) {
        NN_LOG_ERROR("Failed to connect to server public jetty");
        return NN_ERROR;
    }
    NetLocalAutoDecreasePtr<UBPublicJetty> publicJettyAutoDecPtr(clientPublicJetty);
    UBPublicJetty *clientControlJetty = nullptr;
    if (CreatePublicJetty(clientControlJetty, 0, false) != NN_OK) {
        NN_LOG_ERROR("Failed to create public jetty in client");
        return NN_ERROR;
    }
    NetLocalAutoDecreasePtr<UBPublicJetty> clientControlJettyAutoDecPtr(clientControlJetty);
    if (clientControlJetty->StartPublicJetty() != NN_OK) {
        NN_LOG_ERROR("Failed to start public jetty in client");
        return NN_ERROR;
    }
    UBPollingMode pollMode = ((flags & NET_EP_EVENT_POLLING)) ? UB_EVENT_POLLING : UB_BUSY_POLLING;
    UBJetty *qp = nullptr;
    UBJfc *cq = nullptr;
    uint32_t token = GenerateSecureRandomUint32();
    if (ClientSyncEpCreateJetty(qp, cq, pollMode, token) != 0) {
        NN_LOG_ERROR("Failed to create jetty in client sycn ep");
        return NN_ERROR;
    }
    NetLocalAutoDecreasePtr<UBJetty> qpAutoDecPtr(qp);
    NetLocalAutoDecreasePtr<UBJfc> cqAutoDecPtr(cq);
    // fill exchange info
    auto id = NetUuid::GenerateUuid();
    if (ClientSendConnReq(payload, id, serverGrpNo, clientPublicJetty, qp, clientControlJetty, token) != 0) {
        NN_LOG_ERROR("Failed to send connect request to server");
        return NN_ERROR;
    }
    // recv exchange info from server
    UBJettyExchangeInfo info{};
    if (ClientEstablishConnOnReply(clientControlJetty, qp, info) != 0) {
        NN_LOG_ERROR("Failed to establish connection on ack in client");
        return NN_ERROR;
    }
    if (CreateSyncEp(qp, cq, id, outEp, clientControlJetty) != 0) {
        NN_LOG_ERROR("Failed to create sync ep in client");
        return NN_ERROR;
    }

    return NN_OK;
}

void NetDriverUBWithOob::ClientSyncEpSetInfo(UBSHcomNetEndpointPtr ep, UBJetty *qp, UBSHcomNetEndpointPtr &outEp)
{
    auto id = ep->Id();

    // SyncEp 不会在 UBWorker 中处理事件，为保持一致性与 AsyncEp 采用一样顺序。
    // \see NetDriverUBWithOob::NewConnectionCB
    qp->SetUpContext(reinterpret_cast<uintptr_t>(ep.Get()));
    ep->State().Set(NEP_ESTABLISHED);
    {
        std::lock_guard<std::mutex> locker(mEndPointsMutex);
        mEndPoints.emplace(id, ep);
    }
    outEp = ep;
    reinterpret_cast<NetUBSyncEndpoint *>(ep.Get())->mJetty->SetUpId(id);
    NN_LOG_INFO("New connection established via public jetty, sync ep id " << id);
}

NResult NetDriverUBWithOob::PrePostReceiveOnSyncEp(UBSHcomNetEndpointPtr ep, uint16_t prePostCount, UBJetty *qp)
{
    int result = 0;
    auto *mrSegs = new (std::nothrow) uintptr_t[prePostCount];
    if (mrSegs == nullptr) {
        NN_LOG_ERROR("Failed to create mr address array in driver " << mName << ", probably out of memory");
        return NN_NEW_OBJECT_FAILED;
    }
    NetLocalAutoFreePtr<uintptr_t> segAutoDelete(mrSegs, true);
    if (!qp->GetFreeBufferN(mrSegs, prePostCount)) {
        NN_LOG_ERROR("Failed to get free mr from pool, result " << result);
        return NN_ERROR;
    }
    int i = 0;
    for (; i < prePostCount; i++) {
        if (result = reinterpret_cast<NetUBSyncEndpoint *>(ep.Get())->PostReceive(mrSegs[i],
            mOptions.mrSendReceiveSegSize, reinterpret_cast<urma_target_seg_t *>(qp->GetMemorySeg())) != 0) {
                break;
        }
    }
    for (; i < prePostCount; i++) {
        qp->ReturnBuffer(mrSegs[i]);
    }
    return result;
}

NResult NetDriverUBWithOob::ClientSyncEpCreateJetty(UBJetty *&qp, UBJfc *&cq, UBPollingMode pollMode, uint32_t token)
{
    int result = 0;
    JettyOptions qpOptions(mOptions.qpSendQueueSize, mOptions.qpReceiveQueueSize, mOptions.mrSendReceiveSegSize,
        mOptions.prePostReceiveSizePerQP, mOptions.slave, mOptions.ubcMode);
    if ((result = NetUBSyncEndpoint::CreateResources(mName, mContext, pollMode, qpOptions, qp, cq)) != 0) {
        NN_LOG_ERROR("Failed to create qp and cq, result " << result);
        return result;
    }
    qp->SetName(mName);
    if (cq->Initialize() != 0) {
        NN_LOG_ERROR("Failed to initialize cq for new connection in Driver " << mName);
        delete cq;
        delete qp;
        return NN_ERROR;
    }

    if (qp->Initialize(mOptions.mrSendReceiveSegCount, 0, token) != 0) {
        NN_LOG_ERROR("Failed to initialize qp for new connection in Driver " << mName);
        cq->UnInitialize();
        delete cq;
        delete qp;
        return NN_ERROR;
    }
    return NN_OK;
}
}
}
#endif
