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
#include "ub_worker.h"
#include "net_common.h"

namespace ock {
namespace hcom {
constexpr uint64_t MAX_OP_TIME_US = NN_NO500000; // 500 ms
uint64_t g_connection_count = 0;

std::map<std::string, uint32_t> g_eid_port_map;

NResult NetDriverUBWithOob::DoInitialize()
{
    if (mWorkers.empty()) {
        NN_LOG_ERROR("Failed to do initialize in Driver " << mName << ", as mWorkers is empty");
    }

    for (auto worker : mWorkers) {
        worker->RegisterPostedHandler(std::bind(&NetDriverUBWithOob::SendFinished, this, std::placeholders::_1));
        worker->RegisterNewRequestHandler(std::bind(&NetDriverUBWithOob::NewRequest, this, std::placeholders::_1));
        worker->RegisterOneSideDoneHandler(std::bind(&NetDriverUBWithOob::OneSideDone, this, std::placeholders::_1));
        if (mIdleHandler != nullptr) {
            worker->RegisterIdleHandler(mIdleHandler);
        }
    }

    NResult result = NN_OK;
    // create oob
    if (mStartOobSvr) {
        if (mOptions.oobType != NET_OOB_UB) {
            // get route list
            for (auto &lOpt : mOobListenOptions) {
                std::string bondingEid = lOpt.Ip();
                if (bondingEid.empty() || !NetFunc::NN_IsUrmaEid(bondingEid)) {
                    continue;
                }
                NN_LOG_DEBUG("BondingEid: " << bondingEid);
                std::string localPrimaryEid;
                std::string remotePrimaryEid;
                NResult ret = NetFunc::NN_GetPrimaryEid(bondingEid, bondingEid, localPrimaryEid, remotePrimaryEid);
                if (ret != NN_OK) {
                    lOpt.Ip(bondingEid);
                    continue;
                }
                NN_LOG_DEBUG("Local primary eid: " << localPrimaryEid << ", Remote primary eid: " << remotePrimaryEid);
                lOpt.Ip(localPrimaryEid);
            }
 	             
            result = CreateListeners(mOptions.enableMultiRail);
        } else {
            result = CreateUrmaListeners(mPublicJetty);
        }
        if (result != NN_OK) {
            NN_LOG_ERROR("Failed to create listeners");
            return NN_ERROR;
        }
    }
    mEndPoints.reserve(NN_NO1024);

    return NN_OK;
}

void NetDriverUBWithOob::DoUnInitialize()
{
    if (mStarted) {
        NN_LOG_WARN("Unable to uninitialize ub driver " << mName << " which is not stopped");
        return;
    }

    if (!mOobServers.empty()) {
        mOobServers.clear();
    }
}

NResult NetDriverUBWithOob::DoStart()
{
    NResult result = NN_OK;
    if (mStartOobSvr) {
        if (mOptions.oobType != NET_OOB_UB) {
            if (mNewEndPointHandler == nullptr) {
                NN_LOG_ERROR("Failed to do start in Driver " << mName << ", as newEndPointerHandler is null");
                return NN_INVALID_PARAM;
            }

            /* set cb for listeners */
            for (auto &oobServer : mOobServers) {
                oobServer->SetNewConnCB(std::bind(&NetDriverUBWithOob::NewConnectionCB, this, std::placeholders::_1));
                oobServer->SetNewConnCbThreadNum(mOptions.oobConnHandleThreadCount);
                oobServer->SetNewConnCbQueueCap(mOptions.oobConnHandleQueueCap);
            }

            result = StartListeners();
            if (result != NN_OK) {
                NN_LOG_ERROR("Failed to start listeners for driver " << mName << ", result " << result);
                return result;
            }
        } else {
            mPublicJetty->SetNewConnCB(
                std::bind(&NetDriverUBWithOob::PublicJettyNewConnectionCB, this, std::placeholders::_1));
            result = mPublicJetty->StartPublicJetty();
            if (result != NN_OK) {
                NN_LOG_ERROR("Failed to start public jetty for driver " << mName << ", result " << result);
                return result;
            }
        }
    }

    mHeartBeat = new (std::nothrow) NetHeartbeat(this, mOptions.heartBeatIdleTime, mOptions.heartBeatProbeInterval);
    if (mHeartBeat == nullptr) {
        NN_LOG_ERROR("Failed to do start in Driver " << mName << ", as new heartbeat failed");
        return NN_ERROR;
    }

    result = mHeartBeat->Start();
    if (result != NN_OK) {
        StopListeners();
        return result;
    }

    mNeedStopEvent = false;
    std::thread tmpEventThread(&NetDriverUBWithOob::RunInUbEventThread, this);
    mUBEventThread = std::move(tmpEventThread);

    while (!mEventStarted.load()) {
        usleep(NN_NO10);
    }

    return NN_OK;
}

void NetDriverUBWithOob::DoStop()
{
    if (mHeartBeat != nullptr) {
        mHeartBeat->Stop();
        delete mHeartBeat;
        mHeartBeat = nullptr;
    }

    mNeedStopEvent = true;
    if (mUBEventThread.native_handle()) {
        mUBEventThread.join();
    }
    if (mPublicJetty != nullptr) {
        mPublicJetty->Stop();
    }
    StopListeners();
}

NResult NetDriverUBWithOob::MultiRailNewConnection(OOBTCPConnection &conn)
{
    return NewConnectionCB(conn);
}

void NetDriverUBWithOob::DestroyEpByPortNum(int portNum)
{
    static thread_local std::vector<UBSHcomNetEndpointPtr> endPointsCopy;
    endPointsCopy.reserve(NN_NO8192);
    endPointsCopy.clear();
    {
        std::lock_guard<std::mutex> locker(mEndPointsMutex);
        for (auto iter = mEndPoints.begin(); iter != mEndPoints.end();) {
            auto asyncEp = iter->second.ToChild<NetUBAsyncEndpoint>();
            if (asyncEp != nullptr && asyncEp->GetQp()->GetPortNum() == portNum) {
                endPointsCopy.emplace_back(iter->second);
                iter = mEndPoints.erase(iter);
            } else {
                ++iter;
            }
        }
    }

    for (auto &endPoint : endPointsCopy) {
        NN_LOG_WARN("Detect port down event, handle Ep id " << endPoint->Id() << " of driver " << mName);
        ProcessEpError(reinterpret_cast<uintptr_t>(endPoint.Get()));
    }

    NN_LOG_INFO("Destroyed all endpoints count " << endPointsCopy.size() << " by port down of driver " << mName);
    endPointsCopy.clear();
}

void NetDriverUBWithOob::HandlePortDown(int portNum)
{
    for (auto &worker : mWorkers) {
        if (worker->PortNum() == portNum) {
            worker->Stop();
        }
    }

    DestroyEpByPortNum(portNum);
}

void NetDriverUBWithOob::HandlePortActive(int portNum)
{
    for (auto &worker : mWorkers) {
        if (worker->PortNum() == portNum) {
            worker->Start();
        }
    }
}

void NetDriverUBWithOob::DestroyEpInWorker(UBWorker *worker)
{
    static thread_local std::vector<UBSHcomNetEndpointPtr> endPointsCopy;
    endPointsCopy.reserve(NN_NO8192);
    endPointsCopy.clear();
    {
        std::lock_guard<std::mutex> locker(mEndPointsMutex);
        for (auto iter = mEndPoints.begin(); iter != mEndPoints.end();) {
            auto asyncEp = iter->second.ToChild<NetUBAsyncEndpoint>();
            if (asyncEp != nullptr && asyncEp->mWorker == worker) {
                endPointsCopy.emplace_back(iter->second);
                iter = mEndPoints.erase(iter);
            } else {
                ++iter;
            }
        }
    }

    for (auto &endPoint : endPointsCopy) {
        NN_LOG_WARN("Detect CQ incorrect event, handle Ep id " << endPoint->Id() << " of driver " << mName);
        ProcessEpError(reinterpret_cast<uintptr_t>(endPoint.Get()));
    }

    NN_LOG_INFO("Destroyed all endpoints count " << endPointsCopy.size() << " in UB worker " << worker->DetailName() <<
        " of driver " << mName);
    endPointsCopy.clear();
}

void NetDriverUBWithOob::HandleCqEvent(urma_async_event_t *event)
{
    /* when sync mode connecting, there is no worker */
    if (NN_UNLIKELY(event->element.jfc == nullptr || event->element.jfc->jfc_cfg.user_ctx == 0)) {
        NN_LOG_ERROR("CQ error for CQ of driver " << mName);
        return;
    }

    auto worker = reinterpret_cast<UBWorker *>(event->element.jfc->jfc_cfg.user_ctx);
    NN_LOG_ERROR("CQ error for CQ in UB worker " << worker->DetailName() << " of driver " << mName);
    if (worker->Stop() != UB_OK) {
        NN_LOG_ERROR("Handle Cq event stop error in UB worker " << worker->DetailName() << " of driver " << mName);
        return;
    }

    DestroyEpInWorker(worker);
    if (worker->ReInitializeCQ() != UB_OK) {
        NN_LOG_ERROR("Handle Cq event ReInitializeCQ error in UB worker " << worker->DetailName() << " of driver " <<
            mName);
        return;
    }
    if (worker->Start() != UB_OK) {
        NN_LOG_ERROR("Handle Cq event start error in UB worker " << worker->DetailName() << " of driver " << mName);
        return;
    }
}

static inline std::string QpDetailInfo(void *qpContext)
{
    auto qp = reinterpret_cast<UBJetty *>(qpContext);
    std::ostringstream oss;
    oss << "[Qp name:" << qp->GetName() << ", id:" << qp->GetId() << "]";
    return oss.str();
}

void NetDriverUBWithOob::HandleAsyncEvent(urma_async_event_t *event)
{
    switch (event->event_type) {
        case URMA_EVENT_JFC_ERR:
            HandleCqEvent(event);
            NN_LOG_ERROR("jfc error of driver " << mName);
            return;
        case URMA_EVENT_JFS_ERR:
            NN_LOG_ERROR("jfs error of driver " << mName);
            return;
        case URMA_EVENT_JFR_ERR:
            NN_LOG_ERROR("jfr error of driver " << mName);
            return;
        case URMA_EVENT_JFR_LIMIT:
            NN_LOG_ERROR("jfr limit of driver " << mName);
            return;
        case URMA_EVENT_JETTY_ERR:
            NN_LOG_ERROR("jetty error of driver " << mName);
            return;
        case URMA_EVENT_JETTY_LIMIT:
            NN_LOG_ERROR("jetty limit of driver " << mName);
            return;
        case URMA_EVENT_JETTY_GRP_ERR:
            NN_LOG_ERROR("jetty grp error of driver " << mName);
            return;
        case URMA_EVENT_PORT_ACTIVE:
            NN_LOG_ERROR("port active of driver " << mName);
            HandlePortActive(event->element.port_id);
            return;
        case URMA_EVENT_PORT_DOWN:
            NN_LOG_ERROR("port down of driver " << mName);
            HandlePortDown(event->element.port_id);
            return;
        case URMA_EVENT_DEV_FATAL:
            NN_LOG_ERROR("dev fatal of driver " << mName);
            return;
        case URMA_EVENT_EID_CHANGE:
            NN_LOG_ERROR("eid change of driver " << mName);
            return;
        case URMA_EVENT_ELR_ERR:
            NN_LOG_ERROR("elr error of driver " << mName);
            return;
        case URMA_EVENT_ELR_DONE:
            NN_LOG_ERROR("elr done of driver " << mName);
            return;
        default:
            NN_LOG_ERROR("Unknown event " << event->event_type << " of driver " << mName);
    }
}

void NetDriverUBWithOob::RunInUbEventThread()
{
    mEventStarted.store(true);
    NN_LOG_INFO("Ub event monitor thread for driver " << mName << " started");

    /* set thread name */
    pthread_setname_np(pthread_self(), ("UBEvent" + std::to_string(mIndex)).c_str());

    /* set nonblock */
    urma_context_t *urmaContext = mContext->GetContext();
    if (urmaContext == nullptr) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to get urma context for driver " << mName << ", error " <<
            NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        mEventStarted.store(false);
        return;
    }
    int flags = fcntl(urmaContext->async_fd, F_GETFL);
    int ret = fcntl(urmaContext->async_fd, F_SETFL, (static_cast<uint32_t>(flags)) | O_NONBLOCK);
    if (ret < 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to change event fd of ub context for driver " << mName << ", error " <<
            NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        mEventStarted.store(false);
        return;
    }

    urma_async_event_t event{};
    while (!mNeedStopEvent) {
        struct pollfd fd {};
        int timeoutMs = NN_NO100;
        fd.fd = urmaContext->async_fd;
        fd.events = POLLIN;
        fd.revents = 0;
        do {
            ret = poll(&fd, 1, timeoutMs);
            if (ret > 0) {
                break;
            } else if (ret < 0 && errno != EINTR) {
                char buf[NET_STR_ERROR_BUF_SIZE] = {0};
                NN_LOG_ERROR("Failed to poll event fd of ub context for driver " << mName << ", error " <<
                    NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
                break;
            }
            // rc == 0
        } while (!mNeedStopEvent);

        if (mNeedStopEvent) {
            break;
        }
        ret = HcomUrma::GetAsyncEvent(urmaContext, &event);
        if (ret != 0) {
            /* nothing happen when nonblock mode */
            continue;
        }

        /* when fatal event happened, need stop worker first, then call ep broken to prevent race condition
           with poll cq thread */
        HandleAsyncEvent(&event);

        /* ack the event, otherwise destroy cq will block */
        HcomUrma::AckAsyncEvent(&event);
    }
    NN_LOG_INFO("UB event monitor thread for driver " << mName << " exiting");
    mEventStarted.store(false);
}

int NetDriverUBWithOob::ChooseRoutes(std::string peerEid, uvs_path_set_t &uvsPathSet)
{
    uint8_t chipId = 1; // 固定0
    uint8_t dieId = 1; // 固定0
    std::set<uint8_t> ports;
    for (int pathIdx = 0; pathIdx < uvsPathSet.path_count; ++pathIdx) {
        if (uvsPathSet.paths[pathIdx].src_port.chip_id == chipId
            && uvsPathSet.paths[pathIdx].src_port.die_id == dieId) {
            ports.insert(uvsPathSet.paths[pathIdx].src_port.port_idx);
        }
    }
    // 主备信息留存
    // RR轮询--port，参考ubsocket
    std::vector<uint8_t> portVec(ports.begin(), ports.end());
    uint32_t portIdx = 0;
    if (g_eid_port_map.find(peerEid) == g_eid_port_map.end()) {
        g_eid_port_map[peerEid] = 0;
    } else {
        portIdx = g_eid_port_map[peerEid];
    }

    if (portVec.size() == 0) {
        NN_LOG_ERROR("No available ports for peerEid " << peerEid);
        return NN_ERROR;
    }

    mainRouteSrcPortEid = portVec[portIdx];
    portIdx = (portIdx + 1) % portVec.size();
    backRouteSrcPortEid = portVec[portIdx];
    g_eid_port_map[peerEid] = portIdx;
    return NN_OK;
}

int NetDriverUBWithOob::NewConnectionCB(OOBTCPConnection &conn)
{
    if (NN_UNLIKELY(OOBSecureProcess::SecProcessInOOBServer(mSecInfoProvider, mSecInfoValidator, conn, mName,
        mOptions.secType)) != NN_OK) {
        return NN_OOB_SEC_PROCESS_ERROR;
    }

    int ret = 0;

    if (!mOptions.enableMultiRail && mOptions.activateBackup) {
        std::string peerEid;
        uint32_t peerEidLen = 0;

        ret = conn.Receive(&peerEidLen, sizeof(peerEidLen));
        if (ret != NN_OK) {
            NN_LOG_ERROR("Failed to receive peer eid length in Driver " << mName);
            return ret;
        }

        peerEid.resize(peerEidLen);
        if (peerEidLen > 0) {
            ret = conn.Receive(&peerEid[0], peerEidLen);
            if (ret != NN_OK) {
                NN_LOG_ERROR("Failed to receive peer eid in Driver " << mName);
                return ret;
            }
        }

        std::string localEid = mEid;
        uint32_t localEidLen = static_cast<uint32_t>(localEid.size());

        ret = conn.Send(&localEidLen, sizeof(localEidLen));
        if (ret != NN_OK) {
            NN_LOG_ERROR("Failed to send local eid length in Driver " << mName);
            return ret;
        }
        if (localEidLen > 0) {
            auto localEidPtr = reinterpret_cast<void *>(const_cast<char *>(localEid.data()));
            ret = conn.Send(localEidPtr, localEidLen);
            if (ret != NN_OK) {
                NN_LOG_ERROR("Failed to send local eid in Driver " << mName);
                return ret;
            }
        }

        uvs_path_set_t uvsPathSet;
        ret = NetFunc::GetTopoInfo(localEid, peerEid, uvsPathSet);
        if (ret != NN_OK) {
            NN_LOG_ERROR("Failed to get topo info in Driver " << mName << ", localEid " << localEid << ", peerEid " <<
                peerEid << ", ret " << ret);
            return ret;
        }
        // CLOS  topo
        NN_LOG_INFO("path set topo type : " << uvsPathSet.topo_type);
        if (uvsPathSet.topo_type == UVS_TOPO_TYPE_CLOS) {
            g_is_activate_backup = true;
            // 开启主备
            ret = ChooseRoutes(peerEid, uvsPathSet);
            if (ret != NN_OK) {
                NN_LOG_ERROR("Failed to choose routes , ret " << ret);
                return ret;
            }
        }
    }

    // receive server worker grpno
    auto startRecvWG = NetMonotonic::TimeUs();
    ConnectHeader header{};
    void *grpnobuf = &header;
    if ((ret = conn.Receive(grpnobuf, sizeof(ConnectHeader))) != 0) {
        NN_LOG_ERROR("Failed to receive specified server worker grpno from client " << mName << ", ret " << ret);
        return NN_ERROR;
    }

    ConnRespWithUId respWithUId{ OK, 0 };
    ret = OOBSecureProcess::SecCheckConnectionHeader(header, mOptions, mEnableTls, Protocol(), mMajorVersion,
        mMinorVersion, respWithUId);
    if (ret != NN_OK) {
        conn.Send(&respWithUId, sizeof(ConnRespWithUId));
        return NN_ERROR;
    }

    auto endRecvWG = NetMonotonic::TimeUs();
    auto recvWGtime = endRecvWG - startRecvWG;
    if (NN_UNLIKELY(recvWGtime > MAX_OP_TIME_US)) {
        NN_LOG_WARN("Receive group num time is too long :" << recvWGtime << " us.");
    }

    /* choose worker */
    NetWorkerLBPtr lb = nullptr;
    if (mOptions.enableMultiRail) {
        lb = mServerLb;
    } else {
        lb = conn.LoadBalancer();
    }
    NN_ASSERT_LOG_RETURN(lb.Get() != nullptr, NN_ERROR)
    uint16_t wkrIdx = 0;
    if (NN_UNLIKELY(!lb->ChooseWorker(header.groupIndex, conn.GetIpAndPort(), wkrIdx)) ||
        wkrIdx >= mWorkers.size()) {
        NN_LOG_ERROR("Failed to find worker fit grpno " << header.groupIndex << " in " << mName << " , ret " <<
            ret);
        ConnRespWithUId respWithUId{ WORKER_GRPNO_MISMATCH, 0 };
        conn.Send(&respWithUId, sizeof(ConnRespWithUId));
        return NN_ERROR;
    }

    NN_LOG_TRACE_INFO("Worker " << wkrIdx << " is chosen in driver " << mName);
    auto worker = mWorkers[wkrIdx];
    NN_ASSERT_LOG_RETURN(worker != nullptr, NN_ERROR);

    if (!worker->IsWorkStarted()) {
        NN_LOG_ERROR("Failed to connect worker group no " << header.groupIndex << " in " << mName);
        ConnRespWithUId respWithUId{ WORKER_NOT_STARTED, 0 };
        conn.Send(&respWithUId, sizeof(ConnRespWithUId));
        return NN_ERROR;
    }

    // create qp
    auto startCreateQp = NetMonotonic::TimeUs();
    UBJetty *qp = nullptr;
    if ((ret = worker->CreateQP(qp)) != 0) {
        NN_LOG_ERROR("Failed to create qp for new connection in Driver " << mName << " , ret " << ret);
        ConnRespWithUId respWithUId{ SERVER_INTERNAL_ERROR, 0 };
        conn.Send(&respWithUId, sizeof(ConnRespWithUId));
        return NN_ERROR;
    }
    qp->SetName(mName);
    NetLocalAutoDecreasePtr<UBJetty> qpAutoDecPtr(qp);
    uint32_t token = GenerateSecureRandomUint32();
    UBJettyExchangeInfo info{};
    info.token = token;
    if ((ret = qp->Initialize(mOptions.mrSendReceiveSegCount, 0, token)) != 0) {
        NN_LOG_ERROR("Failed to initialize qp for new connection in Driver " << mName << " , ret " << ret);
        ConnRespWithUId respWithUId{ SERVER_INTERNAL_ERROR, 0 };
        conn.Send(&respWithUId, sizeof(ConnRespWithUId));
        return NN_ERROR;
    }
    std::string ipPort = conn.GetIpAndPort();
    qp->SetPeerIpAndPort(ipPort);

    g_connection_count++;
    auto id = NetUuid::GenerateUuid();
    NN_LOG_TRACE_INFO("new ep id will be set as " << id << " in driver " << mName);
    respWithUId.connResp = OK;
    respWithUId.epId = id;
    conn.Send(&respWithUId, sizeof(ConnRespWithUId));
    auto endCreateQp = NetMonotonic::TimeUs();
    auto createQpTime = endCreateQp - startCreateQp;
    if (NN_UNLIKELY(createQpTime > MAX_OP_TIME_US)) {
        NN_LOG_WARN("Create qp time is too long :" << createQpTime << " us.");
    }

    // exchange info
    NN_LOG_TRACE_INFO("Get and send exchange info of ep");
    auto startExchInfo = NetMonotonic::TimeUs();
    auto prePostCount = mOptions.prePostReceiveSizePerQP;
    if (mHeartBeat != nullptr) {
        if ((ret = qp->CreateHBMemoryRegion(NN_NO128, qp->mHBLocalMr)) != NN_OK) {
            NN_LOG_ERROR("Failed to create mr for local HB, ret: " << ret);
            return ret;
        }
        if ((ret = qp->CreateHBMemoryRegion(NN_NO128, qp->mHBRemoteMr)) != NN_OK) {
            NN_LOG_ERROR("Failed to create mr for remote HB, ret: " << ret);
            qp->DestroyHBMemoryRegion(qp->mHBLocalMr);
            return ret;
        }
        qp->GetRemoteHbInfo(info);
    }
    info.receiveSegSize = mOptions.mrSendReceiveSegSize;
    info.receiveSegCount = mOptions.prePostReceiveSizePerQP;
    info.maxSendWr = mOptions.qpSendQueueSize;
    info.maxReceiveWr = mOptions.qpReceiveQueueSize;
    if (((ret = qp->FillExchangeInfo(info)) != 0)) {
        NN_LOG_ERROR("Failed to get ep exchange info in Driver " << mName << ", ret " << ret);
        return NN_ERROR;
    }
    if (((ret = conn.Send(&info, sizeof(UBJettyExchangeInfo))) != 0)) {
        NN_LOG_ERROR("Failed to send ep exchange info in Driver " << mName << ", ret " << ret);
        return NN_ERROR;
    }
    NN_LOG_TRACE_INFO("Send exchange info success in Server " << mName);
    NN_LOG_TRACE_INFO("local ep ex info lid " << info.lid << ", qpn " << info.qpn << ", gid interface " <<
        info.gid.global.interface_id);

    std::unique_ptr<UBJettyExchangeInfo> peerExInfo(new (std::nothrow) UBJettyExchangeInfo);
    if (!peerExInfo) {
        NN_LOG_ERROR("Failed to alloc UBJettyExchangeInfo in Driver " << mName);
        return NN_MALLOC_FAILED;
    }

    if ((ret = conn.Receive(peerExInfo.get(), sizeof(UBJettyExchangeInfo))) != 0) {
        NN_LOG_ERROR("Failed to receive ep exchange info in Driver " << mName << ", ret " << ret);
        return NN_ERROR;
    }
    NN_LOG_TRACE_INFO("Recv exchange info success in Server " << mName);
    qp->StoreExchangeInfo(peerExInfo.release());

    //  receive payload length
    uint32_t payloadLen = 0;
    auto tmpPayloadLen = reinterpret_cast<void *>(&payloadLen);
    if ((ret = conn.Receive(tmpPayloadLen, sizeof(uint32_t))) != 0) {
        NN_LOG_ERROR("Failed to receive connection payload length in Driver " << mName << ", ret " << ret);
        return NN_ERROR;
    }

    if (payloadLen == 0 || payloadLen > NN_NO1024) {
        NN_LOG_ERROR("Invalid payload length " << payloadLen << ", it should be 1 ~ 1024");
        return NN_ERROR;
    }

    //  receive payload
    std::string payload;
    if (payloadLen > 0) {
        auto payloadChars = new (std::nothrow) char[payloadLen + NN_NO1];
        if (payloadChars == nullptr) {
            NN_LOG_ERROR("Failed to new payload char array in Driver " << mName << ", probably out of memory");
            return NN_NEW_OBJECT_FAILED;
        }
        NetLocalAutoFreePtr<char> autoFreePayChars(payloadChars, true);

        void *tmpChars = static_cast<void *>(payloadChars);
        if ((ret = conn.Receive(tmpChars, payloadLen)) != 0) {
            NN_LOG_ERROR("Failed to receive connection payload in Driver " << mName << ", ret " << ret);
            return NN_ERROR;
        }

        payloadChars[payloadLen] = '\0';
        payload = std::string(payloadChars, payloadLen);
    }

    NN_LOG_TRACE_INFO("Remote qp ex info lid " << info.lid << ", qpn " << info.qpn << ", gid interface " <<
        info.gid.global.interface_id << ", pre-post-receive-count " << info.receiveSegCount);
    if ((ret = qp->ChangeToReady(qp->GetExchangeInfo())) != 0) {
        NN_LOG_ERROR("Failed to change qp to ready in Driver " << mName << ", ret " << ret);
        return ret;
    }

    auto *mrSegs = new (std::nothrow) uintptr_t[prePostCount];
    if (mrSegs == nullptr) {
        NN_LOG_ERROR("Failed to create mr address array in Driver " << mName << ", probably out of memory");
        return NN_NEW_OBJECT_FAILED;
    }

    NetLocalAutoFreePtr<uintptr_t> segAutoDelete(mrSegs, true);

    if (!qp->GetFreeBufferN(mrSegs, prePostCount)) {
        NN_LOG_ERROR("Failed to get free mr from pool, mr is not enough");
        return NN_MALLOC_FAILED;
    }

    uint16_t i = 0;
    for (; i < prePostCount; i++) {
        if ((ret = worker->PostReceive(qp, mrSegs[i], mOptions.mrSendReceiveSegSize,
            reinterpret_cast<urma_target_seg_t *>(qp->GetMemorySeg()))) != 0) {
            ClearJettyResource(qp);
            return ret;
        }
    }

    for (; i < prePostCount; i++) {
        qp->ReturnBuffer(mrSegs[i]);
    }
    auto endExchInfo = NetMonotonic::TimeUs();
    auto exchInfoTime = endExchInfo - startExchInfo;
    if (NN_UNLIKELY(exchInfoTime > MAX_OP_TIME_US)) {
        NN_LOG_WARN("Exchange info time too long :" << exchInfoTime << " us.");
    }

    // create endpoint
    auto startCreateEp = NetMonotonic::TimeUs();
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetUBAsyncEndpoint(id, qp, this, worker);
    if (ep.Get() == nullptr) {
        NN_LOG_ERROR("Failed to create UBSHcomNetEndpoint in Driver " << mName << ", probably out of memory");
        ClearJettyResource(qp);
        return NN_NEW_OBJECT_FAILED;
    }

    if (mOptions.oobType == NET_OOB_UDS) {
        struct ucred remoteIds {};
        socklen_t len = sizeof(struct ucred);
        if (NN_UNLIKELY(getsockopt(conn.GetFd(), SOL_SOCKET, SO_PEERCRED, &remoteIds, &len) != 0)) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to get uds ids in driver " << mName << " errno:" << errno << " error:" <<
                NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
            return NN_GET_UDS_ID_INFO_FAILED;
        }
        ep->RemoteUdsIdInfo(remoteIds.pid, remoteIds.uid, remoteIds.gid);
    }

    ep->StoreConnInfo(NetFunc::GetIpByFd(conn.GetFd()), conn.ListenPort(), header.version, payload);
    ep.ToChild<NetUBAsyncEndpoint>()->SetRemoteHbInfo(qp->GetExchangeInfo().hbAddress, qp->GetExchangeInfo().hbKey,
        qp->GetExchangeInfo().hbMrSize);
    if (mEnableTls) {
        auto childEp = ep.ToChild<NetUBAsyncEndpoint>();
        if (NN_UNLIKELY(childEp == nullptr)) {
            NN_LOG_ERROR("To child Failed");
            return NN_ERROR;
        }
        auto tmp = dynamic_cast<OOBSSLConnection *>(&conn);
        if (NN_UNLIKELY(tmp == nullptr)) {
            NN_LOG_ERROR("dynamic cast error");
            return NN_OOB_SEC_PROCESS_ERROR;
        }
        childEp->EnableEncrypt(mOptions);
        childEp->SetSecrets(tmp->Secret());
    }
    ep->mDevIndex = mDevIndex;
    ep->mPeerDevIndex = mPeerDevIndex;
    ep->mBandWidth = mBandWidth;

    ret = mNewEndPointHandler(conn.GetIpAndPort(), ep, payload);
    if (NN_UNLIKELY(ret != UB_OK)) {
        NN_LOG_ERROR("Called new end point handler failed, ret " << ret);
        ClearJettyResource(qp);
        return NN_ERROR;
    }

    // 在EP创建失败时，保证 jetty 不会索引到无效EP，必须位于set ep to ESTABLISHED 上方。因历史原因，用户可能会在
    // NewEndpointHandler 中发送信息，而如果状态为ESTABLISHED但是Jetty无法索引到EP，在UBWorker poll到对于jetty
    // 上发生事件时可能无法找到源 EP.
    // \see NetDriverUBWithOob::ProcessPollingResult
    // \see NetServiceDefaultImp::ServiceRequestPosted
    // \see HcomServiceImp::ServiceReuqestPosted
    qp->SetUpContext(reinterpret_cast<uintptr_t>(ep.Get()));
    qp->SetUpId(ep->Id());
    ep.ToChild<NetUBAsyncEndpoint>()->State().Set(NEP_ESTABLISHED);

    // ready 同步信令发送后客户端可能会立即发包，会在 UBWorker 中被动触发事件。如果 jetty 无法通过 UpContext() 索引到
    // ep，此 ep 上产生的事件无法被 UBWorker 转发至回调。因此发送 ready 同步信令必须位于 qp->SetUpContext(...)之后。
    int8_t ready = (ret == UB_OK) ? 1 : 0;
    if ((ret = conn.Send(&ready, sizeof(int8_t))) != UB_OK) {
        NN_LOG_ERROR("Failed to send ready signal to client, ret " << ret);
        ClearJettyResource(qp);
        return NN_ERROR;
    }

    // EP被安全创建完毕
    {
        std::lock_guard<std::mutex> locker(mEndPointsMutex);
        mEndPoints.emplace(id, ep);
    }

    auto endCreateEp = NetMonotonic::TimeUs();
    auto createEpTime = endCreateEp - startCreateEp;
    if (NN_UNLIKELY(createEpTime > MAX_OP_TIME_US)) {
        NN_LOG_WARN("Create endpoint time too long :" << createEpTime << " us.");
    }

    NN_LOG_INFO("New connection from " << conn.GetIpAndPort() << " established, async ep id " << ep->Id()
    << ", jetty id: " << qp->QpNum() << ", worker info " << worker->DetailName());
    return NN_OK;
}

NResult NetDriverUBWithOob::Connect(const std::string &payload, UBSHcomNetEndpointPtr &ep, uint32_t flags,
    uint8_t serverGrpNo, uint8_t clientGrpNo)
{
    if (mOptions.oobType == NET_OOB_TCP) {
        return Connect(mOobIp, mOobPort, payload, ep, flags, serverGrpNo, clientGrpNo);
    } else if (mOptions.oobType == NET_OOB_UDS) {
        return Connect(mUdsName, 0, payload, ep, flags, serverGrpNo, clientGrpNo);
    } else if (mOptions.oobType == NET_OOB_UB) {
        return ConnectByPublicJetty(mOobIp, mOobPort, payload, ep, flags, serverGrpNo, clientGrpNo);
    }
    return NN_ERROR;
}

NResult NetDriverUBWithOob::Connect(const std::string &oobIp, uint16_t oobPort, const std::string &payload,
    UBSHcomNetEndpointPtr &outEp, uint32_t flags, uint8_t serverGrpNo, uint8_t clientGrpNo, uint64_t ctx)
{
    if (ClientCheckState(payload) != 0) {
        NN_LOG_ERROR("Failed to connect as driver not start or payload oversize");
        return NN_ERROR;
    }
    /* all kind of drivers can connect to peer to get an ep */
    if (mOptions.oobType == NET_OOB_UB) {
        return ConnectByPublicJetty(oobIp, oobPort, payload, outEp, flags, serverGrpNo, clientGrpNo, ctx);
    }

    std::string oobIpCopy = oobIp;
    if (NetFunc::NN_IsUrmaEid(oobIp)) {
        if (mEid.empty()) {
            NN_LOG_ERROR("Failed to connect as driver not start or payload oversize");
            return NN_ERROR;
        }
        NN_LOG_DEBUG("oobIp eid: " << oobIp << ", meid: " << mEid);
        std::string localPrimaryEid;
        std::string remotePrimaryEid;
        NResult ret = NetFunc::NN_GetPrimaryEid(mEid, oobIp, localPrimaryEid, remotePrimaryEid);
        NN_LOG_DEBUG("Local primary eid: " << localPrimaryEid << ", Remote primary eid: " << remotePrimaryEid);
        if (ret != NN_OK) {
            OOBTCPClient::mLocalEid = mEid;
            oobIpCopy = oobIp;
        } else {
            OOBTCPClient::mLocalEid = localPrimaryEid;
            oobIpCopy = remotePrimaryEid;
        }
    }

    if ((flags & NET_EP_SELF_POLLING) || (flags & NET_EP_EVENT_POLLING)) {
        return ConnectSyncEp(oobIpCopy, oobPort, payload, outEp, flags, serverGrpNo, ctx);
    }
    
    OOBTCPClientPtr tcpClient;
    if (mEnableTls) {
        tcpClient = new (std::nothrow)
            OOBSSLClient(mOptions.oobType, oobIpCopy, oobPort, mTlsPrivateKeyCB, mTlsCertCB, mTlsCaCallback);
        NN_ASSERT_LOG_RETURN(tcpClient.Get() != nullptr, NN_NEW_OBJECT_FAILED)
        tcpClient.ToChild<OOBSSLClient>()->SetTlsOptions(mOptions);
        tcpClient.ToChild<OOBSSLClient>()->SetPSKCallback(mPskFindSessionCb, mPskUseSessionCb);
    } else {
        tcpClient = new (std::nothrow) OOBTCPClient(mOptions.oobType, oobIpCopy, oobPort);
        NN_ASSERT_LOG_RETURN(tcpClient.Get() != nullptr, NN_NEW_OBJECT_FAILED)
    }

    /* try to connect to oob server */
    OOBTCPConnection *conn = nullptr;
    NResult result = NN_OK;
    if ((result = tcpClient->Connect(conn)) != 0) {
        NN_LOG_ERROR("Failed to connect server via oob, result " << result);
        return result;
    }

    NetLocalAutoDecreasePtr<OOBTCPConnection> autoDecPtr(conn);
    conn->SetIpAndPort(oobIpCopy, oobPort);

    if (mOptions.enableMultiRail) {
        ConnectHeader driverHeader;
        SetDriverConnHeader(driverHeader, mBandWidth, mDevIndex);
        if ((result = conn->Send(&driverHeader, sizeof(ConnectHeader))) != 0) {
            NN_LOG_ERROR("Failed to send driver info " << mName << ", Result " << result);
            return result;
        }

        ConnectHeader header{};
        void *grpnobuf = static_cast<void *>(&header);
        auto result = conn->Receive(grpnobuf, sizeof(ConnectHeader));
        if (result != 0) {
            NN_LOG_ERROR("Failed to receive specified device info for server, Result " << result);
            return result;
        }

        if (header.devIndex >= NN_NO4) {
            NN_LOG_ERROR("Invalid devIndex " << header.devIndex << " in header, which should be in 0 ~ 3");
            return NN_ERROR;
        }
        mPeerDevIndex = header.devIndex;
    }

    if (NN_UNLIKELY(OOBSecureProcess::SecProcessInOOBClient(mSecInfoProvider, mSecInfoValidator, conn, mName, ctx,
        mOptions.secType))) {
        return NN_OOB_SEC_PROCESS_ERROR;
    }
    
    if (!mOptions.enableMultiRail && mOptions.activateBackup) {
        std::string localEid = mEid;
        uint32_t localEidLen = static_cast<uint32_t>(localEid.size());

        result = conn->Send(&localEidLen, sizeof(localEidLen));
        if (result != NN_OK) {
            NN_LOG_ERROR("Failed to send local eid length in Driver " << mName);
            return result;
        }
        if (localEidLen > 0) {
            auto localEidPtr = reinterpret_cast<void *>(const_cast<char *>(localEid.data()));
            result = conn->Send(localEidPtr, localEidLen);
            if (result != NN_OK) {
                NN_LOG_ERROR("Failed to send local eid in Driver " << mName);
                return result;
            }
        }

        std::string peerEid;
        uint32_t peerEidLen = 0;

        result = conn->Receive(&peerEidLen, sizeof(peerEidLen));
        if (result != NN_OK) {
            NN_LOG_ERROR("Failed to receive peer eid length in Driver " << mName);
            return result;
        }

        peerEid.resize(peerEidLen);
        if (peerEidLen > 0) {
            result = conn->Receive(&peerEid[0], peerEidLen);
            if (result != NN_OK) {
                NN_LOG_ERROR("Failed to receive peer eid in Driver " << mName);
                return result;
            }
        }

        uvs_path_set_t uvsPathSet;
        result = NetFunc::GetTopoInfo(localEid, peerEid, uvsPathSet);
        if (result != NN_OK) {
            NN_LOG_ERROR("Failed to get topo info in Driver " << mName << ", localEid " << localEid << ", peerEid " <<
                peerEid << ", result " << result);
            return result;
        }

        NN_LOG_INFO("path set topo type : " << uvsPathSet.topo_type);
        if (uvsPathSet.topo_type == UVS_TOPO_TYPE_CLOS) {
            result = ChooseRoutes(peerEid, uvsPathSet);
            if (result != NN_OK) {
                NN_LOG_ERROR("Failed to choose routes , ret " << result);
                return result;
            }
        }
    }

    /* send connection header & grpNo */
    auto startSendGrpNo = NetMonotonic::TimeUs();
    ConnectHeader header;
    SetConnHeader(header, mOptions.magic, mOptions.version, serverGrpNo, Protocol(), mMajorVersion, mMinorVersion,
        mOptions.tlsVersion);
    header.reserve = ctx;
    if ((result = conn->Send(&header, sizeof(ConnectHeader))) != 0) {
        NN_LOG_ERROR("Failed to send server worker grpno in Driver " << mName << ", result " << result);
        return result;
    }

    /* receive connect response and peer ep id */
    ConnRespWithUId respWithUId{};
    void *ackBuf = static_cast<void *>(&respWithUId);
    if ((result = conn->Receive(ackBuf, sizeof(ConnRespWithUId))) != 0) {
        NN_LOG_ERROR("Failed receive ServerAck in Driver " << mName << ", result " << result);
        return result;
    }

    /* connect response */
    auto serverRsp = respWithUId.connResp;
    if (serverRsp == MAGIC_MISMATCH) {
        NN_LOG_ERROR("Failed to pass server magic validation " << mName << ", result " << serverRsp);
        return NN_CONNECT_REFUSED;
    }

    if (serverRsp == WORKER_GRPNO_MISMATCH || serverRsp == WORKER_NOT_STARTED) {
        NN_LOG_ERROR("Failed to choose worker or not started " << mName << ", result " << serverRsp);
        return NN_CONNECT_REFUSED;
    }

    if (serverRsp == PROTOCOL_MISMATCH) {
        NN_LOG_ERROR("Failed to pass server protocol validation " << mName << ", result " << serverRsp);
        return NN_CONNECT_PROTOCOL_MISMATCH;
    }

    if (serverRsp == SERVER_INTERNAL_ERROR) {
        NN_LOG_ERROR("Server error happened, connection refused " << mName << ", result " << serverRsp);
        return NN_ERROR;
    }

    if (serverRsp != OK) {
        NN_LOG_ERROR("Server error happened, connection refused " << mName << ", result " << serverRsp);
        return NN_ERROR;
    }
    auto endSendGrpNo = NetMonotonic::TimeUs();
    auto sendGrpNoTime = endSendGrpNo - startSendGrpNo;
    if (NN_UNLIKELY(sendGrpNoTime > MAX_OP_TIME_US)) {
        NN_LOG_WARN("Send groupNo time too long: " << sendGrpNoTime << " us.");
    }

    /* peer ep id */
    auto id = respWithUId.epId;
    NN_LOG_TRACE_INFO("new ep id will be set as " << id << " in driver " << mName);

    /* Choose worker */
    uint16_t workerIndex = 0;
    if (NN_UNLIKELY(!mClientLb->ChooseWorker(clientGrpNo, std::to_string(id), workerIndex)) ||
        workerIndex >= mWorkers.size()) {
        NN_LOG_ERROR("Failed to choose worker during connect in driver " << mName);
        return NN_ERROR;
    }

    NN_ASSERT_LOG_RETURN(workerIndex < mWorkers.size(), NN_ERROR)
    auto *worker = mWorkers[workerIndex];

    if (!worker->IsWorkStarted()) {
        NN_LOG_ERROR("Failed to connect worker group no " << clientGrpNo << " in " << mName);
        return NN_ERROR;
    }

    /* Create Qp */
    UBJetty *jetty = nullptr;
    if (worker->CreateQP(jetty) != UB_OK) {
        NN_LOG_ERROR("Fail to create jetty");
        return NN_ERROR;
    }
    jetty->SetName(mName);
    NetLocalAutoDecreasePtr<UBJetty> qpAutoDecPtr(jetty);

    uint32_t token = GenerateSecureRandomUint32();
    UBJettyExchangeInfo info{};
    info.token = token;
    if (jetty->Initialize(mOptions.mrSendReceiveSegCount, 0, token) != 0) {
        NN_LOG_ERROR("Failed to initialize jetty for new connection in Driver " << mName);
        return NN_ERROR;
    }
    /* fill and send exchange info */
    auto startExchInfo = NetMonotonic::TimeUs();
    NN_LOG_TRACE_INFO("get and send exchange info of ep");
    if (mHeartBeat != nullptr) {
        if ((result = jetty->CreateHBMemoryRegion(NN_NO128, jetty->mHBLocalMr)) != NN_OK) {
            NN_LOG_ERROR("Failed to create mr for local HB, result " << result);
            return result;
        }
        if ((result = jetty->CreateHBMemoryRegion(NN_NO128, jetty->mHBRemoteMr)) != NN_OK) {
            NN_LOG_ERROR("Failed to create mr for remote HB, result " << result);
            jetty->DestroyHBMemoryRegion(jetty->mHBLocalMr);
            return result;
        }
        jetty->GetRemoteHbInfo(info);
    }
    info.maxSendWr = mOptions.qpSendQueueSize;
    info.maxReceiveWr = mOptions.qpReceiveQueueSize;
    info.receiveSegSize = mOptions.mrSendReceiveSegSize;
    info.receiveSegCount = mOptions.prePostReceiveSizePerQP;

    if (((result = jetty->FillExchangeInfo(info)) != 0)) {
        NN_LOG_ERROR("Failed to get ep exchange info in Driver " << mName << ", result " << result);
        return result;
    }
    if (((result = conn->Send(&info, sizeof(UBJettyExchangeInfo))) != 0)) {
        NN_LOG_ERROR("Failed to send ep exchange info in Driver " << mName << ", result " << result);
        return result;
    }

    auto prePostCount = mOptions.prePostReceiveSizePerQP;

    // send payload len
    uint32_t payloadLength = payload.length();
    if ((result = conn->Send(&payloadLength, sizeof(uint32_t))) != 0) {
        NN_LOG_ERROR("Failed to send connection payload length in Driver " << mName << ", result " << result);
        return result;
    }

    // send payload
    if (payloadLength > 0) {
        auto payloadPtr = reinterpret_cast<void *>(const_cast<char *>(payload.c_str()));
        if ((result = conn->Send(payloadPtr, payloadLength)) != 0) {
            NN_LOG_ERROR("Failed to send connection payload in Driver " << mName << ", result " << result);
            return result;
        }
    }

    // receive exchange info
    std::unique_ptr<UBJettyExchangeInfo> peerExInfo(new (std::nothrow) UBJettyExchangeInfo);
    if (!peerExInfo) {
        NN_LOG_ERROR("Failed to alloc UBJettyExchangeInfo in Driver " << mName);
        return NN_MALLOC_FAILED;
    }

    if ((result = conn->Receive(peerExInfo.get(), sizeof(UBJettyExchangeInfo))) != 0) {
        NN_LOG_ERROR("Failed to receive ep exchange info in Driver " << mName << ", result " << result);
        return NN_ERROR;
    }
    jetty->StoreExchangeInfo(peerExInfo.release());

    /* change to ready */
    NN_LOG_TRACE_INFO("remote jetty ex info lid " << info.lid << ", qpn " << info.qpn << ", gid interface " <<
        info.gid.global.interface_id << ", pre-post-receive-count " << info.receiveSegCount);
    if ((result = jetty->ChangeToReady(jetty->GetExchangeInfo())) != 0) {
        NN_LOG_ERROR("Failed to change jetty to ready in Driver " << mName << ", result " << result);
        return result;
    }

    jetty->SetPeerIpAndPort(conn->GetIpAndPort());

    auto *mrSegs = new (std::nothrow) uintptr_t[prePostCount];
    if (mrSegs == nullptr) {
        NN_LOG_ERROR("Failed to create array of mr address in Driver " << mName << ", probably out of memory");
        return NN_NEW_OBJECT_FAILED;
    }

    NetLocalAutoFreePtr<uintptr_t> segAutoDelete(mrSegs, true);

    if (!jetty->GetFreeBufferN(mrSegs, prePostCount)) {
        NN_LOG_ERROR("Failed to get free mr from pool, result " << result);
        return NN_ERROR;
    }

    uint16_t i = 0;
    for (; i < prePostCount; i++) {
        if ((result = worker->PostReceive(jetty, mrSegs[i], mOptions.mrSendReceiveSegSize,
            reinterpret_cast<urma_target_seg_t *>(jetty->GetMemorySeg()))) != 0) {
            ClearJettyResource(jetty);
            return result;
        }
    }

    for (; i < prePostCount; i++) {
        jetty->ReturnBuffer(mrSegs[i]);
    }

    auto endExchInfo = NetMonotonic::TimeUs();
    auto exchInfoTime = endExchInfo - startExchInfo;
    if (NN_UNLIKELY(exchInfoTime > MAX_OP_TIME_US)) {
        NN_LOG_WARN("Exchange Info time too long: " << exchInfoTime << " us.");
    }
         
    /* Create endpoint */
    auto startCreateEp = NetMonotonic::TimeUs();
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetUBAsyncEndpoint(id, jetty, this, worker);
    if (ep.Get() == nullptr) {
        NN_LOG_ERROR("Failed to create UBSHcomNetEndpoint in Driver " << mName << ", probably out of memory");
        ClearJettyResource(jetty);
        return NN_NEW_OBJECT_FAILED;
    }

    if (mEnableTls) {
        auto childEp = ep.ToChild<NetUBAsyncEndpoint>();
        if (NN_UNLIKELY(childEp == nullptr)) {
            NN_LOG_ERROR("To child Failed");
            return NN_ERROR;
        }
        auto tmp = dynamic_cast<OOBSSLConnection *>(conn);
        if (NN_UNLIKELY(tmp == nullptr)) {
            NN_LOG_ERROR("dynamic cast error");
            return NN_OOB_SEC_PROCESS_ERROR;
        }
        childEp->EnableEncrypt(mOptions);
        childEp->SetSecrets(tmp->Secret());
    }

    ep->StoreConnInfo(NetFunc::GetIpByFd(conn->GetFd()), conn->ListenPort(), header.version, payload);
    ep.ToChild<NetUBAsyncEndpoint>()->SetRemoteHbInfo(jetty->GetExchangeInfo().hbAddress,
        jetty->GetExchangeInfo().hbKey, jetty->GetExchangeInfo().hbMrSize);

    // receive server ready signal
    int8_t ready = -1;
    result = conn->Receive(&ready, sizeof(int8_t));
    if (result != 0 || ready != 1) {
        NN_LOG_ERROR("Failed to connect to server as server not responses or return not ready, result " << result);
        ClearJettyResource(jetty);
        return NN_ERROR;
    }

    // \see NetDriverUBWithOob::NewConnectionCB
    jetty->SetUpContext(reinterpret_cast<uintptr_t>(ep.Get()));
    ep->State().Set(NEP_ESTABLISHED);
    {
        std::lock_guard<std::mutex> locker(mEndPointsMutex);
        mEndPoints.emplace(ep->Id(), ep);
    }

    NN_LOG_INFO("New connect to " << oobIp << ":" << oobPort << " established, async ep id: " << ep->Id()
    << ", jetty id: " << jetty->QpNum() << ", worker info " << worker->DetailName());
    outEp = ep;
    reinterpret_cast<NetUBAsyncEndpoint *>(ep.Get())->GetQp()->SetUpId(ep->Id());
    auto endCreateEp = NetMonotonic::TimeUs();
    auto createEpTime = endCreateEp - startCreateEp;
    if (NN_UNLIKELY(createEpTime > MAX_OP_TIME_US)) {
        NN_LOG_WARN("Create endpoint time too long: " << createEpTime << " us.");
    }
    return NN_OK;
}

NResult NetDriverUBWithOob::ConnectSyncEp(const std::string &oobIp, uint16_t oobPort, const std::string &payload,
    UBSHcomNetEndpointPtr &outEp, uint32_t flags, uint8_t serverGrpNo, uint64_t ctx)
{
    OOBTCPClientPtr client;
    if (mEnableTls) {
        client = new (std::nothrow)
            OOBSSLClient(mOptions.oobType, oobIp, oobPort, mTlsPrivateKeyCB, mTlsCertCB, mTlsCaCallback);
        NN_ASSERT_LOG_RETURN(client.Get() != nullptr, NN_NEW_OBJECT_FAILED)
        client.ToChild<OOBSSLClient>()->SetTlsOptions(mOptions);
        client.ToChild<OOBSSLClient>()->SetPSKCallback(mPskFindSessionCb, mPskUseSessionCb);
    } else {
        client = new (std::nothrow) OOBTCPClient(mOptions.oobType, oobIp, oobPort);
        NN_ASSERT_LOG_RETURN(client.Get() != nullptr, NN_NEW_OBJECT_FAILED)
    }

    /* try to connect to oob server */
    OOBTCPConnection *conn = nullptr;
    NResult result = NN_OK;
    if ((result = client->Connect(conn)) != 0) {
        NN_LOG_ERROR("Failed to connect server via oob, result " << result);
        return result;
    }

    NetLocalAutoDecreasePtr<OOBTCPConnection> autoDecPtr(conn);
    conn->SetIpAndPort(oobIp, oobPort);

    if (NN_UNLIKELY(OOBSecureProcess::SecProcessInOOBClient(mSecInfoProvider, mSecInfoValidator, conn, mName, ctx,
        mOptions.secType))) {
        return NN_OOB_SEC_PROCESS_ERROR;
    }

    UBPollingMode pollMode = ((flags & NET_EP_EVENT_POLLING)) ? UB_EVENT_POLLING : UB_BUSY_POLLING;

    auto prePostCount = mOptions.prePostReceiveSizePerQP;

    // create qp and cq
    UBJetty *qp = nullptr;
    UBJfc *cq = nullptr;
    JettyOptions qpOptions(mOptions.qpSendQueueSize, mOptions.qpReceiveQueueSize, mOptions.mrSendReceiveSegSize,
        mOptions.prePostReceiveSizePerQP, mOptions.slave, mOptions.ubcMode);
    if ((result = NetUBSyncEndpoint::CreateResources(mName, mContext, pollMode, qpOptions, qp, cq)) != 0) {
        NN_LOG_ERROR("Failed to create qp and cq, result " << result);
        return result;
    }
    qp->SetName(mName);
    NetLocalAutoDecreasePtr<UBJetty> qpAutoDecPtr(qp);
    NetLocalAutoDecreasePtr<UBJfc> cqAutoDecPtr(cq);

    if (cq->Initialize() != 0) {
        NN_LOG_ERROR("Failed to initialize cq for new connection in Driver " << mName);
        return NN_ERROR;
    }
    uint32_t token = GenerateSecureRandomUint32();
    UBJettyExchangeInfo info{};
    info.token = token;
    if (qp->Initialize(mOptions.mrSendReceiveSegCount, 0, token) != 0) {
        NN_LOG_ERROR("Failed to initialize qp for new connection in Driver " << mName);
        return NN_ERROR;
    }

    /* send connection header */
    ConnectHeader header;
    SetConnHeader(header, mOptions.magic, mOptions.version, serverGrpNo, Protocol(), mMajorVersion, mMinorVersion,
        mOptions.tlsVersion);

    if ((result = conn->Send(&header, sizeof(ConnectHeader))) != 0) {
        NN_LOG_ERROR("Failed to send server worker grpno in Driver " << mName << ", result " << result);
        return result;
    }

    /* receive connect response and peer ep id */
    ConnRespWithUId respWithUId{};
    void *ackBuf = static_cast<void *>(&respWithUId);
    if ((result = conn->Receive(ackBuf, sizeof(ConnRespWithUId))) != 0) {
        NN_LOG_ERROR("Failed receive ServerAck in Driver " << mName << ", result " << result);
        return result;
    }

    /* connect response */
    auto serverAck = respWithUId.connResp;
    if (serverAck == MAGIC_MISMATCH) {
        NN_LOG_ERROR("Failed to pass server magic validation " << mName << ",magic " << header.magic << ", result " <<
            serverAck);
        return NN_CONNECT_REFUSED;
    }

    if (serverAck == WORKER_GRPNO_MISMATCH || serverAck == WORKER_NOT_STARTED) {
        NN_LOG_ERROR("Failed to choose worker or not started " << mName << ", result " << serverAck);
        return NN_CONNECT_REFUSED;
    }

    if (serverAck == PROTOCOL_MISMATCH) {
        NN_LOG_ERROR("Failed to pass server protocol validation " << mName << ", result " << serverAck);
        return NN_CONNECT_PROTOCOL_MISMATCH;
    }

    if (serverAck == SERVER_INTERNAL_ERROR) {
        NN_LOG_ERROR("Server error happened, connection refused " << mName << ", result " << serverAck);
        return NN_ERROR;
    }

    if (serverAck != OK) {
        NN_LOG_ERROR("Server error happened, connection refused " << mName << ", result " << serverAck);
        return NN_ERROR;
    }

    /* peer ep id */
    auto id = respWithUId.epId;
    NN_LOG_TRACE_INFO("new ep id will be set as " << id << " in driver " << mName);
    // exchange info
    NN_LOG_TRACE_INFO("get and send exchange info of ep");
    if (mHeartBeat != nullptr) {
        mHeartBeat->GetRemoteHbInfo(info);
    }
    info.maxSendWr = mOptions.qpSendQueueSize;
    info.maxReceiveWr = mOptions.qpReceiveQueueSize;
    info.receiveSegSize = mOptions.mrSendReceiveSegSize;
    info.receiveSegCount = mOptions.prePostReceiveSizePerQP;
    if (((result = qp->FillExchangeInfo(info)) != 0)) {
        NN_LOG_ERROR("Failed to get ep exchange info in Driver " << mName << ", result " << result);
        return result;
    }
    if (((result = conn->Send(&info, sizeof(UBJettyExchangeInfo))) != 0)) {
        NN_LOG_ERROR("Failed to send ep exchange info in Driver " << mName << ", result " << result);
        return result;
    }

    // send payload len
    uint32_t payloadLen = payload.length();
    if ((result = conn->Send(&payloadLen, sizeof(uint32_t))) != 0) {
        NN_LOG_ERROR("Failed to send connection payload length in Driver " << mName << ", result " << result);
        return result;
    }

    // send payload
    if (payloadLen > 0) {
        auto payloadPtr = reinterpret_cast<void *>(const_cast<char *>(payload.c_str()));
        if ((result = conn->Send(payloadPtr, payloadLen)) != 0) {
            NN_LOG_ERROR("Failed to send connection payload in Driver " << mName << ", result " << result);
            return result;
        }
    }

    std::unique_ptr<UBJettyExchangeInfo> peerExInfo(new (std::nothrow) UBJettyExchangeInfo);
    if (!peerExInfo) {
        NN_LOG_ERROR("Failed to alloc UBJettyExchangeInfo in Driver " << mName);
        return NN_MALLOC_FAILED;
    }

    if ((result = conn->Receive(peerExInfo.get(), sizeof(UBJettyExchangeInfo))) != 0) {
        NN_LOG_ERROR("Failed to receive ep exchange info in Driver " << mName << ", result " << result);
        return NN_ERROR;
    }
    qp->StoreExchangeInfo(peerExInfo.release());

    NN_LOG_TRACE_INFO("remote qp ex info lid " << info.lid << ", qpn " << info.qpn << ", gid interface " <<
        info.gid.global.interface_id << ", pre-post-receive-count " << info.receiveSegCount);
    if ((result = qp->ChangeToReady(qp->GetExchangeInfo())) != 0) {
        NN_LOG_ERROR("Failed to change ep to ready in Driver " << mName << ", result " << result);
        return result;
    }

    qp->SetPeerIpAndPort(conn->GetIpAndPort());

    auto *mrSegs = new (std::nothrow) uintptr_t[prePostCount];
    if (mrSegs == nullptr) {
        NN_LOG_ERROR("Failed to create mr address array in Driver " << mName << ", probably out of memory");
        return NN_NEW_OBJECT_FAILED;
    }

    NetLocalAutoFreePtr<uintptr_t> segAutoDelete(mrSegs, true);

    if (!qp->GetFreeBufferN(mrSegs, prePostCount)) {
        NN_LOG_ERROR("Failed to get free mr from pool, result " << result);
        return NN_ERROR;
    }

    // create endpoint
    static UBSHcomNetWorkerIndex workerIndex;
    workerIndex.driverIdx = mIndex;
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetUBSyncEndpoint(id, qp, cq, prePostCount + NN_NO4, this,
        workerIndex);
    if (ep.Get() == nullptr) {
        NN_LOG_ERROR("Failed to create UBSHcomNetEndpoint in Driver " << mName << ", probably out of memory");
        // do later: handle pre post-ed mr
        return NN_NEW_OBJECT_FAILED;
    }
    NN_LOG_INFO("Create sync ep success, ep id: " << ep->Id() << ", with jetty id: " << qp->QpNum());

    if (reinterpret_cast<NetUBSyncEndpoint *>(ep.Get())->mCtxPool.Initialize() != UB_OK) {
        NN_LOG_ERROR("Fail to initialize mCtxPool");
    }

    for (int i = 0; i < prePostCount; i++) {
        result = reinterpret_cast<NetUBSyncEndpoint *>(ep.Get())->PostReceive(mrSegs[i], mOptions.mrSendReceiveSegSize,
            reinterpret_cast<urma_target_seg_t *>(qp->GetMemorySeg()));
        if (result != 0) {
            // do later if failure, qp should break at this time
            return result;
        }
    }

    if (mEnableTls) {
        auto childEp = ep.ToChild<NetUBSyncEndpoint>();
        if (NN_UNLIKELY(childEp == nullptr)) {
            NN_LOG_ERROR("To child Failed");
            return NN_ERROR;
        }
        auto tmp = dynamic_cast<OOBSSLConnection *>(conn);
        if (NN_UNLIKELY(tmp == nullptr)) {
            NN_LOG_ERROR("dynamic cast error");
            return NN_OOB_SEC_PROCESS_ERROR;
        }
        childEp->EnableEncrypt(mOptions);
        childEp->SetSecrets(tmp->Secret());
    }
    ep->StoreConnInfo(NetFunc::GetIpByFd(conn->GetFd()), conn->ListenPort(), header.version, payload);

    // receive server ready signal
    int8_t ready = -1;
    result = conn->Receive(&ready, sizeof(int8_t));
    if (result != 0 || ready != 1) {
        NN_LOG_ERROR("Failed to connect to server as server not respond or return not ready, result " << result);
        // do later: handle pre post-ed mr
        return NN_ERROR;
    }

    // SyncEP 不会在 UBWorker中处理事件，为保持一致性与AsyncEP采用一样顺序。
    // \see NetDriverUBWithOob::NewConnectionCB
    qp->SetUpContext(reinterpret_cast<uintptr_t>(ep.Get()));
    ep->State().Set(NEP_ESTABLISHED);
    {
        std::lock_guard<std::mutex> locker(mEndPointsMutex);
        mEndPoints.emplace(id, ep);
    }

    NN_LOG_INFO("New connect to " << oobIp << ":" << oobPort << " established, sync ep id " << ep->Id());
    outEp = ep;
    reinterpret_cast<NetUBSyncEndpoint *>(ep.Get())->mJetty->SetUpId(ep->Id());
    return NN_OK;
}

void NetDriverUBWithOob::ProcessErrorNewRequest(UBOpContextInfo *ctx)
{
    if (NN_UNLIKELY(ctx == nullptr || ctx->ubJetty == nullptr || ctx->ubJetty->GetUpContext1() == 0)) {
        NN_LOG_ERROR("Ctx or QP or Worker is null of NewRequest in Driver " << mName << "");
        return;
    }

    if (ctx->opType == UBOpContextInfo::RECEIVE) {
        ctx->ubJetty->ReturnBuffer(ctx->mrMemAddr);
        auto worker = reinterpret_cast<UBWorker *>(ctx->ubJetty->GetUpContext1());
        worker->ReturnOpContextInfo(ctx);
        // not receive remote data, do not call user callback
    } else {
        NN_LOG_WARN("Unreachable path");
    }
}

int NetDriverUBWithOob::SendRawSglFinishedCB(UBOpContextInfo *ctx, UBSHcomNetRequestContext &netCtx)
{
    int result = 0;

    auto worker = reinterpret_cast<UBWorker *>(ctx->ubJetty->GetUpContext1());
    auto sgeCtx = reinterpret_cast<UBSgeCtxInfo *>(ctx->upCtx);
    auto sglCtx = sgeCtx->ctx;
    result = UBOpContextInfo::GetNResult(ctx->opResultType);
    // set context
    netCtx.mEp.Set(reinterpret_cast<UBSHcomNetEndpoint *>(ctx->ubJetty->GetUpContext()));
    netCtx.mResult = sglCtx->result < result ? result : sglCtx->result;
    netCtx.mOpType = UBSHcomNetRequestContext::NN_SENT_RAW_SGL;
    netCtx.mHeader.Invalid();
    netCtx.mMessage = nullptr;
    if (NN_UNLIKELY(memcpy_s(netCtx.iov, sizeof(UBSHcomNetTransSgeIov) * NET_SGE_MAX_IOV, sglCtx->iov,
        sizeof(UBSHcomNetTransSgeIov) * sglCtx->iovCount) != UB_OK)) {
        NN_LOG_ERROR("Failed to copy req to sglCtx");
        return UB_PARAM_INVALID;
    }
    netCtx.mOriginalSglReq.iov = netCtx.iov;
    netCtx.mOriginalSglReq.iovCount = sglCtx->iovCount;
    netCtx.mOriginalSglReq.upCtxSize = sglCtx->upCtxSize;
    if (netCtx.mOriginalSglReq.upCtxSize > 0 &&
        netCtx.mOriginalSglReq.upCtxSize <= sizeof(UBSHcomNetTransSglRequest::upCtxData)) {
        if (NN_UNLIKELY(memcpy_s(netCtx.mOriginalSglReq.upCtxData, NN_NO16, sglCtx->upCtx, sglCtx->upCtxSize) !=
            UB_OK)) {
            NN_LOG_ERROR("Failed to copy req to sglCtx");
            return UB_PARAM_INVALID;
        }
    }
    worker->ReturnSglContextInfo(sglCtx);
    // called to callback
    if (NN_UNLIKELY((result = mRequestPostedHandler(netCtx)) != UB_OK)) {
        NN_LOG_ERROR("Call requestPostedHandler in Driver " << mName << " return non-zero for sgl type " <<
            ctx->opType << " done");
    }
    netCtx.mEp.Set(nullptr);

    // buffer should return when encrypt
    if (mEnableTls) {
        (void)mDriverSendMR->ReturnBuffer(ctx->mrMemAddr);
    }

    worker->ReturnOpContextInfo(ctx);

    return NN_OK;
}

void PrintSendFinishDebug(UBSHcomNetTransHeader &header, UBOpContextInfo *ctx)
{
    UBSHcomNetEndpointPtr debugEp = reinterpret_cast<UBSHcomNetEndpoint *>(ctx->ubJetty->GetUpContext());
    uint64_t epId = debugEp->Id();
    if (ctx->opType == UBOpContextInfo::SEND) {
        NN_LOG_DEBUG("[Request Send] ------ ep id = " << epId << ", headerCrc = " << header.headerCrc
            << ", opCode = " << header.opCode << ", flags = " << header.flags << ", seqNo = " << header.seqNo
            << ",timeout = " << header.timeout << ", errCode = " << header.errorCode << ", dataLength = "
            << header.dataLength << ", status = " << UBSHcomRequestStatusToString(UBSHcomNetRequestStatus::POLLED));
    } else {
        NN_LOG_DEBUG("[Request Send] ------ raw request, ep id = " << epId << "dataLength = " << ctx->dataSize <<
            ", status = " << UBSHcomRequestStatusToString(UBSHcomNetRequestStatus::POLLED));
    }
}

int NetDriverUBWithOob::SendSglInlineFinishedCB(UBOpContextInfo *ctx, UBSHcomNetRequestContext &requestCtx,
    UBWorker *worker)
{
    int result = 0;
    requestCtx.mHeader.Invalid();
    requestCtx.mResult = UBOpContextInfo::GetNResult(ctx->opResultType);
    requestCtx.mEp.Set(reinterpret_cast<UBSHcomNetEndpoint *>(ctx->ubJetty->GetUpContext()));
    requestCtx.mMessage = nullptr;
    requestCtx.mOpType = UBSHcomNetRequestContext::NN_SENT_SGL_INLINE;
    requestCtx.mOriginalReq = {};
    requestCtx.mOriginalReq.lAddress = 0;
    requestCtx.mOriginalReq.size = ctx->dataSize;
    requestCtx.mOriginalReq.upCtxSize = ctx->upCtxSize;

    if (requestCtx.mOriginalReq.upCtxSize > 0 &&
        requestCtx.mOriginalReq.upCtxSize <= sizeof(UBSendReadWriteRequest::upCtxData)) {
        if (NN_UNLIKELY(memcpy_s(requestCtx.mOriginalReq.upCtxData, ctx->upCtxSize, ctx->upCtx, ctx->upCtxSize) !=
            UB_OK)) {
            NN_LOG_ERROR("Failed to copy req to ctx");
            result = UB_PARAM_INVALID;
        }
    }
    // return context to worker, and ctx is set null, not usable anymore
    worker->ReturnOpContextInfo(ctx);
    // call to callback
    if (result == UB_OK && NN_UNLIKELY((result = mRequestPostedHandler(requestCtx)) != UB_OK)) {
        NN_LOG_ERROR("Call requestPostedHandler in Driver " << mName <<
            " return non-zero for receive message [dataSize " << requestCtx.mHeader.dataLength << "]");
    }
    requestCtx.mEp.Set(nullptr);
    return result;
}

int NetDriverUBWithOob::SendAndSendRawFinishedCB(UBOpContextInfo *ctx, UBSHcomNetRequestContext &requestCtx,
    UBWorker *worker)
{
    using NRC = UBSHcomNetRequestContext;
    int result = 0;
    if (ctx->opType == UBOpContextInfo::SEND && NN_UNLIKELY(memcpy_s(&(requestCtx.mHeader),
        sizeof(UBSHcomNetTransHeader), reinterpret_cast<UBSHcomNetTransHeader *>(ctx->mrMemAddr),
        sizeof(UBSHcomNetTransHeader)) != UB_OK)) {
        NN_LOG_ERROR("Failed to copy ctx to requestCtx");
        result = UB_ERROR;
    }

    if (ctx->opType == UBOpContextInfo::SEND_RAW) {
        requestCtx.mHeader.Invalid();
    }
    PrintSendFinishDebug(requestCtx.mHeader, ctx);
    requestCtx.mResult = UBOpContextInfo::GetNResult(ctx->opResultType);
    requestCtx.mEp.Set(reinterpret_cast<UBSHcomNetEndpoint *>(ctx->ubJetty->GetUpContext()));
    requestCtx.mMessage = nullptr;
    requestCtx.mOpType = ctx->opType == UBOpContextInfo::SEND ? NRC::NN_SENT : NRC::NN_SENT_RAW;
    requestCtx.mOriginalReq = {};
    // if PostSend implement with one side memory, the lAddress should be valued with ctx->mrMemAddr.
    requestCtx.mOriginalReq.lAddress = 0;
    requestCtx.mOriginalReq.size = ctx->dataSize;
    requestCtx.mOriginalReq.upCtxSize = ctx->upCtxSize;

    if (requestCtx.mOriginalReq.upCtxSize > 0 &&
        requestCtx.mOriginalReq.upCtxSize <= sizeof(UBSendReadWriteRequest::upCtxData) &&
        NN_UNLIKELY(memcpy_s(requestCtx.mOriginalReq.upCtxData, ctx->upCtxSize, ctx->upCtx,
        ctx->upCtxSize) != UB_OK)) {
        NN_LOG_ERROR("Failed to copy ctx to requestCtx");
        result = UB_ERROR;
    }

    if (NN_UNLIKELY(!mDriverSendMR->ReturnBuffer(ctx->mrMemAddr))) {
        NN_LOG_ERROR("Failed to return mr segment back in Driver " << mName);
    }

    // return context to worker, and ctx is set null, not usable anymore
    worker->ReturnOpContextInfo(ctx);
    if (requestCtx.mHeader.opCode == HB_SEND_OP || requestCtx.mHeader.opCode == HB_RECV_OP) {
        return UB_OK;
    }
    // call to callback
    if (result == UB_OK && NN_UNLIKELY((result = mRequestPostedHandler(requestCtx)) != UB_OK)) {
        NN_LOG_ERROR("Call requestPostedHandler in Driver " << mName
        << " return non-zero for receive message [opCode: " << requestCtx.mHeader.opCode << ", dataSize "
        << requestCtx.mHeader.dataLength << "]");
    }
    requestCtx.mEp.Set(nullptr);
    return result;
}

int NetDriverUBWithOob::SendFinishedCB(UBOpContextInfo *ctx)
{
    using NRC = UBSHcomNetRequestContext;
    int result = 0;
    static thread_local UBSHcomNetRequestContext requestCtx{};
    ctx->ubJetty->ReturnPostSendWr();
    auto worker = reinterpret_cast<UBWorker *>(ctx->ubJetty->GetUpContext1());
    if (ctx->opType == UBOpContextInfo::SEND || ctx->opType == UBOpContextInfo::SEND_RAW) {
        return SendAndSendRawFinishedCB(ctx, requestCtx, worker);
    } else if (ctx->opType == UBOpContextInfo::SEND_RAW_SGL) {
        return SendRawSglFinishedCB(ctx, requestCtx);
    } else if (ctx->opType == UBOpContextInfo::SEND_SGL_INLINE) {
        return SendSglInlineFinishedCB(ctx, requestCtx, worker);
    } else {
        NN_LOG_WARN("Unreachable path");
    }

    return result;
}

void NetDriverUBWithOob::ProcessErrorSendFinished(UBOpContextInfo *ctx)
{
    if (NN_UNLIKELY(ctx == nullptr || ctx->ubJetty == nullptr || ctx->ubJetty->GetUpContext1() == 0)) {
        NN_LOG_ERROR("Ctx or QP or Worker is null of SendFinished in Driver " << mName << "");
        return;
    }

    SendFinishedCB(ctx);
}

int NetDriverUBWithOob::RWSglOneSideDoneCB(UBOpContextInfo *ctx, UBSHcomNetRequestContext &netCtx)
{
    int result = 0;
    auto worker = reinterpret_cast<UBWorker *>(ctx->ubJetty->GetUpContext1());
    auto sgeCtx = reinterpret_cast<UBSgeCtxInfo *>(ctx->upCtx);
    auto sglContext = sgeCtx->ctx;
    result = UBOpContextInfo::GetNResult(ctx->opResultType);
    sglContext->result = sglContext->result < result ? result : sglContext->result;
    auto refCount = __sync_add_and_fetch(&(sglContext->refCount), 1);
    if (refCount != sglContext->iovCount) {
        worker->ReturnOpContextInfo(ctx);
        return NN_OK;
    }
    // set context
    netCtx.mEp.Set(reinterpret_cast<UBSHcomNetEndpoint *>(ctx->ubJetty->GetUpContext()));

    NN_LOG_DEBUG("[Request RWSglOneSideDoneCB] ------ ep id = " << netCtx.mEp->Id() << ", opType = " <<
        static_cast<uint32_t>(ctx->opType) << ", lKey = " << ctx->lKey << ", size = " << ctx->dataSize <<
        ", header opcode = " << netCtx.mHeader.opCode << ", seqNo = " << netCtx.mHeader.seqNo << ", status = " <<
        UBSHcomRequestStatusToString(UBSHcomNetRequestStatus::POLLED));

    netCtx.mResult = sglContext->result;
    netCtx.mOpType =
        ctx->opType == UBOpContextInfo::SGL_WRITE ? UBSHcomNetRequestContext::NN_SGL_WRITTEN :
        UBSHcomNetRequestContext::NN_SGL_READ;
    netCtx.mHeader.Invalid();
    netCtx.mMessage = nullptr;
    if (NN_UNLIKELY(memcpy_s(netCtx.iov, sizeof(UBSHcomNetTransSgeIov) * NET_SGE_MAX_IOV, sglContext->iov,
        sizeof(UBSHcomNetTransSgeIov) * sglContext->iovCount) != NN_OK)) {
        NN_LOG_ERROR("Failed to copy req to sglCtx");
        result = NN_INVALID_PARAM;
    }
    netCtx.mOriginalSglReq.iov = netCtx.iov;
    netCtx.mOriginalSglReq.iovCount = sglContext->iovCount;
    netCtx.mOriginalSglReq.upCtxSize = sglContext->upCtxSize;
    if (netCtx.mOriginalSglReq.upCtxSize > 0 &&
        netCtx.mOriginalSglReq.upCtxSize <= sizeof(UBSHcomNetTransSglRequest::upCtxData)) {
        if (NN_UNLIKELY(memcpy_s(netCtx.mOriginalSglReq.upCtxData, NN_NO16,
            sglContext->upCtx, sglContext->upCtxSize) != NN_OK)) {
            NN_LOG_ERROR("Failed to copy req to sglCtx");
            result = NN_INVALID_PARAM;
        }
    }
    worker->ReturnSglContextInfo(sglContext);
    // called to callback
    if (result == NN_OK && NN_UNLIKELY((result = mOneSideDoneHandler(netCtx)) != NN_OK)) {
        NN_LOG_ERROR("Call oneSideDoneHandler in Driver " << mName << " return non-zero for sgl type " << ctx->opType <<
            " done");
    }
    netCtx.mEp.Set(nullptr);
    worker->ReturnOpContextInfo(ctx);

    return result;
}

int NetDriverUBWithOob::OneSideDoneCB(UBOpContextInfo *ctx)
{
    int result = 0;
    static thread_local UBSHcomNetRequestContext netCtx{};
    auto worker = reinterpret_cast<UBWorker *>(ctx->ubJetty->GetUpContext1());
    ctx->ubJetty->ReturnOneSideWr();
    if (ctx->opType == UBOpContextInfo::WRITE || ctx->opType == UBOpContextInfo::READ) {
        // set context
        netCtx.mResult = UBOpContextInfo::GetNResult(ctx->opResultType);
        netCtx.mEp.Set(reinterpret_cast<UBSHcomNetEndpoint *>(ctx->ubJetty->GetUpContext()));
        NN_LOG_DEBUG("[Request oneSideDown] ------ ep id = " << netCtx.mEp->Id() << ", opType = " <<
            static_cast<uint32_t>(ctx->opType) << ", lKey = " << ctx->lKey << ", size = " << ctx->dataSize <<
            ", status = " << UBSHcomRequestStatusToString(UBSHcomNetRequestStatus::POLLED));
        netCtx.mOpType =
            ctx->opType == UBOpContextInfo::WRITE ? UBSHcomNetRequestContext::NN_WRITTEN :
            UBSHcomNetRequestContext::NN_READ;
        netCtx.mHeader.Invalid();
        netCtx.mMessage = nullptr;
        netCtx.mOriginalReq.lAddress = ctx->mrMemAddr;
        netCtx.mOriginalReq.lKey = ctx->lKey;
        netCtx.mOriginalReq.size = ctx->dataSize;
        netCtx.mOriginalReq.upCtxSize = ctx->upCtxSize;

        if (netCtx.mOriginalReq.upCtxSize > 0 &&
            netCtx.mOriginalReq.upCtxSize <= sizeof(UBSendReadWriteRequest::upCtxData) &&
            NN_UNLIKELY(memcpy_s(netCtx.mOriginalReq.upCtxData, ctx->upCtxSize, ctx->upCtx,
            ctx->upCtxSize) != NN_OK)) {
            NN_LOG_ERROR("Failed to copy ctx to requestCtx");
            result = NN_ERROR;
        }

        // return context to worker and ctx is not usable anymore
        worker->ReturnOpContextInfo(ctx);

        // called to callback
        if (result == NN_OK && NN_UNLIKELY((result = mOneSideDoneHandler(netCtx)) != NN_OK)) {
            NN_LOG_ERROR("Call oneSideDoneHandler in Driver " << mName << " failed.");
        }
        netCtx.mEp.Set(nullptr);
    } else if (ctx->opType == UBOpContextInfo::SGL_WRITE || ctx->opType == UBOpContextInfo::SGL_READ) {
        return RWSglOneSideDoneCB(ctx, netCtx);
    } else if (ctx->opType == UBOpContextInfo::HB_WRITE) {
        auto ep = reinterpret_cast<NetUBAsyncEndpoint *>(ctx->ubJetty->GetUpContext());
        if (ctx->opResultType == UBOpContextInfo::SUCCESS) {
            ep->HbRecordCount();
        }

        worker->ReturnOpContextInfo(ctx);
    } else {
        NN_LOG_WARN("Unreachable path");
    }

    return result;
}

void NetDriverUBWithOob::ProcessErrorOneSideDone(UBOpContextInfo *ctx)
{
    if (NN_UNLIKELY(ctx == nullptr || ctx->ubJetty == nullptr || ctx->ubJetty->GetUpContext1() == 0)) {
        NN_LOG_ERROR("Ctx or QP or Worker is null of OneSidedone in Driver " << mName << "");
        return;
    }

    OneSideDoneCB(ctx);
}

void NetDriverUBWithOob::ProcessEpError(uintptr_t ep)
{
    auto endpointPtr = reinterpret_cast<NetUBAsyncEndpoint *>(ep);

    // UBWorker 线程与心跳线程只会有一个成功
    bool process = false;
    if (NN_UNLIKELY(!endpointPtr->EPBrokenProcessed().compare_exchange_strong(process, true))) {
        NN_LOG_WARN("Ep id " << endpointPtr->Id() << " broken handled by other thread");
        return;
    }

    if (endpointPtr->State().Compare(NEP_ESTABLISHED)) {
        endpointPtr->State().Set(NEP_BROKEN);
    }

    // 这里存在大段注释，解释了一些极限情况下的异常可能性
    auto qp = endpointPtr->GetQp();
    qp->Stop();

    NN_LOG_WARN("Handle Ep state " << UBSHcomNEPStateToString(endpointPtr->State().Get()) << ", Ep id " <<
        endpointPtr->Id() << " , try call Ep broken handle");
    mEndPointBrokenHandler(endpointPtr);
}

void NetDriverUBWithOob::ProcessQPError(UBOpContextInfo *ctx)
{
    if (NN_UNLIKELY(ctx == nullptr || ctx->ubJetty == nullptr || ctx->ubJetty->GetUpContext1() == 0)) {
        NN_LOG_ERROR("Ctx or QP or Worker is null of ProcessQPError in Driver " << mName << "");
        return;
    }

    // get ep
    auto epPtr = reinterpret_cast<NetUBAsyncEndpoint *>(ctx->ubJetty->GetUpContext());
    ProcessEpError(reinterpret_cast<uintptr_t>(epPtr));
}

void NetDriverUBWithOob::ProcessTwoSideHeartbeat(UBOpContextInfo *ctx, UBSHcomNetRequestContext &netCtx)
{
    auto tmpEp = reinterpret_cast<NetUBAsyncEndpoint *>(ctx->ubJetty->GetUpContext());
    if (netCtx.mHeader.opCode == HB_SEND_OP) {
        char data;
        UBSHcomNetTransRequest req((void *)(&data), sizeof(data), 0);
        tmpEp->PostSend(HB_RECV_OP, req, 0);
        netCtx.mEp.Set(nullptr);
        return;
    }
    if (netCtx.mHeader.opCode == HB_RECV_OP) {
        tmpEp->HbRecordCount();
        netCtx.mEp.Set(nullptr);
        return;
    }
}

NResult NetDriverUBWithOob::NewRequestOnEncryption(UBOpContextInfo *ctx, UBSHcomNetMessage &msg, bool &messageReady,
    UBSHcomNetRequestContext &netCtx)
{
    if (NN_UNLIKELY(ctx == nullptr || ctx->ubJetty == nullptr || ctx->ubJetty->GetUpContext1() == 0 ||
        ctx->ubJetty->GetUpContext() == 0)) {
        NN_LOG_ERROR("Ctx or QP or Worker or ep is null of RequestReceived in Driver " << mName);
        return NN_INVALID_PARAM;
    }
    auto ubWorker = reinterpret_cast<UBWorker *>(ctx->ubJetty->GetUpContext1());
    auto qpUpContext = ctx->ubJetty->GetUpContext();
    auto *tmpHeader = reinterpret_cast<UBSHcomNetTransHeader *>(ctx->mrMemAddr);
    UBSHcomNetEndpointPtr epPtr = reinterpret_cast<UBSHcomNetEndpoint *>(qpUpContext);
    auto asyncEp = epPtr.ToChild<NetUBAsyncEndpoint>();
    if (asyncEp == nullptr) {
        NN_LOG_ERROR("Failed to get async ep");
        ubWorker->RePostReceive(ctx);
        return NN_ERROR;
    }
    if (!asyncEp->mIsNeedEncrypt) {
        NN_LOG_ERROR("Failed to validate encrypt by driver support but ep not.");
        ubWorker->RePostReceive(ctx);
        return NN_INVALID_PARAM;
    }
    size_t decryptRawLen = asyncEp->mAes.GetRawLen(tmpHeader->dataLength);
    messageReady = msg.AllocateIfNeed(decryptRawLen);
    if (NN_LIKELY(messageReady)) {
        uint32_t decryptLen = 0;
        if (!asyncEp->mAes.Decrypt(asyncEp->mSecrets, reinterpret_cast<void *>(ctx->mrMemAddr +
            sizeof(UBSHcomNetTransHeader)), tmpHeader->dataLength, msg.mBuf, decryptLen)) {
            NN_LOG_ERROR("Failed to decrypt data");
            (void)ubWorker->RePostReceive(ctx);
            return NN_DECRYPT_FAILED;
        }
        if (memcpy_s(&(netCtx.mHeader), sizeof(UBSHcomNetTransHeader), tmpHeader,
            sizeof(UBSHcomNetTransHeader)) != NN_OK) {
            NN_LOG_ERROR("Failed to memcpy to netCtx header");
            ubWorker->RePostReceive(ctx);
            return NN_ERROR;
        }
        msg.mDataLen = decryptRawLen;
    }
    return NN_OK;
}

int NetDriverUBWithOob::NewRequest(UBOpContextInfo *ctx)
{
    if (NN_UNLIKELY(!ValidateRequestContext(ctx))) {
        return NN_ERROR;
    }

    if (NN_UNLIKELY(ctx->opResultType != UBOpContextInfo::SUCCESS)) {
        ProcessQPError(ctx);
        return NN_OK;
    }

    static thread_local UBSHcomNetRequestContext netCtx{};
    auto worker = reinterpret_cast<UBWorker *>(ctx->ubJetty->GetUpContext1());
    uint32_t immData = *reinterpret_cast<uint32_t *>(ctx->upCtx);
    bool messageReady = true;
    auto qpUpContext = ctx->ubJetty->GetUpContext();
    if (ctx->opType == UBOpContextInfo::RECEIVE && immData == 0) {
        static thread_local UBSHcomNetMessage msg;
        auto *tmpHeader = reinterpret_cast<UBSHcomNetTransHeader *>(ctx->mrMemAddr);

        UBSHcomNetEndpointPtr debugEp = reinterpret_cast<UBSHcomNetEndpoint *>(qpUpContext);
        uint64_t epId = debugEp->Id();
        auto tmpAsyncEp = debugEp.ToChild<NetUBAsyncEndpoint>();
        UBSHcomNetTransHeader *header = (UBSHcomNetTransHeader *)tmpHeader;
        NN_LOG_DEBUG("[Request Recv] ------ common request, ep id = " << epId << ", headerCrc = "
        << header->headerCrc << ", opCode = " << header->opCode << ", flags=" << header->flags << ", seqNo="
        << header->seqNo << ",timeout=" << header->timeout << ", errCode=" << header->errorCode << ", dataLength="
        << header->dataLength << " dataSize = " << ctx->dataSize << ", status = " <<
        UBSHcomRequestStatusToString(UBSHcomNetRequestStatus::POLLED));

        if (NN_UNLIKELY(NetFunc::ValidateHeaderWithDataSize(*tmpHeader, ctx->dataSize) != NN_OK)) {
            NN_LOG_ERROR("Failed to validate received header " << tmpHeader->headerCrc);
            worker->RePostReceive(ctx);
            return NN_VALIDATE_HEADER_CRC_INVALID;
        }
        if (NN_LIKELY(!mOptions.enableTls)) {
            messageReady = msg.AllocateIfNeed(tmpHeader->dataLength);
            if (NN_LIKELY(messageReady) && (NN_UNLIKELY(memcpy_s(&(netCtx.mHeader), sizeof(UBSHcomNetTransHeader),
                tmpHeader, sizeof(UBSHcomNetTransHeader)) != NN_OK) || NN_UNLIKELY(memcpy_s(msg.mBuf,
                tmpHeader->dataLength, reinterpret_cast<void *>(ctx->mrMemAddr + sizeof(UBSHcomNetTransHeader)),
                tmpHeader->dataLength) != NN_OK))) {
                NN_LOG_ERROR("Failed to copy header");
                worker->RePostReceive(ctx);
                return NN_ERROR;
            }
            if (NN_LIKELY(messageReady)) {
                msg.mDataLen = tmpHeader->dataLength;
            }
        } else {
            if (NewRequestOnEncryption(ctx, msg, messageReady, netCtx) != NN_OK) {
                NN_LOG_ERROR("Failed to decrypt new request");
                return NN_ERROR;
            }
        }

        int result = 0;
        if (NN_UNLIKELY((result = worker->RePostReceive(ctx)) != 0)) {
            NN_LOG_ERROR("Failed to repost receive in Driver " << mName << ", result " << result);
        }

        if (NN_UNLIKELY(!messageReady)) {
            NN_LOG_ERROR("Failed to build UBSHcomNetRequestContext or message in Driver " << mName <<
                ", receive message [opCode: " << netCtx.mHeader.opCode << ", dataSize " << msg.mDataLen <<
                "] will be dropped");
            return NN_OK;
        }

        netCtx.mEp.Set(reinterpret_cast<UBSHcomNetEndpoint *>(qpUpContext));
        netCtx.mMessage = &msg;
        netCtx.mOpType = UBSHcomNetRequestContext::NN_RECEIVED;
        netCtx.mOriginalReq = {};
        netCtx.mHeader.dataLength = msg.mDataLen;
        netCtx.extHeaderType = tmpHeader->extHeaderType; // 指导服务层处理

        // call to callback
        if (NN_UNLIKELY((result = mReceivedRequestHandler(netCtx)) != NN_OK)) {
            NN_LOG_ERROR("Call receivedRequestHandler in Driver " << mName <<
                " return non-zero for receive message [opCode: " << netCtx.mHeader.opCode << ", dataSize " <<
                netCtx.mHeader.dataLength << "]");
        }
        netCtx.mEp.Set(nullptr);
    } else if (ctx->opType == UBOpContextInfo::RECEIVE && immData != 0) {
        static thread_local UBSHcomNetMessage msg;

        UBSHcomNetEndpointPtr debugEp = reinterpret_cast<UBSHcomNetEndpoint *>(qpUpContext);
        uint64_t epId = debugEp->Id();
        auto tmpAsyncEp = debugEp.ToChild<NetUBAsyncEndpoint>();
        NN_LOG_DEBUG("[Request Recv] ------ raw request, ep id = " << epId << ", seqNo = " << immData
        << ", dataSize = " << msg.DataLen() << ", status = " <<
        UBSHcomRequestStatusToString(UBSHcomNetRequestStatus::POLLED));

        return NewReceivedRawRequest(ctx, netCtx, msg, worker, immData);
    } else {
        NN_LOG_WARN("Unreachable path");
    }

    return NN_OK;
}

NResult NetDriverUBWithOob::NewReceivedRawRequest(UBOpContextInfo *ctx, UBSHcomNetRequestContext &netCtx,
    UBSHcomNetMessage &msg, UBWorker *worker, uint32_t immData) const
{ /* for raw message */
    bool messageReady = true;
    auto qpUpContext = ctx->ubJetty->GetUpContext();
    if (NN_LIKELY(!mOptions.enableTls)) {
        messageReady = msg.AllocateIfNeed(ctx->dataSize);
        if (NN_LIKELY(messageReady)) {
            if (NN_UNLIKELY(memcpy_s(msg.mBuf, ctx->dataSize,
                reinterpret_cast<void *>(ctx->mrMemAddr), ctx->dataSize) != NN_OK)) {
                NN_LOG_ERROR("Failed to copy ctx to msg");
                return NN_ERROR;
            }
            msg.mDataLen = ctx->dataSize;
        }
    } else {
        UBSHcomNetEndpointPtr endpointPtr = reinterpret_cast<UBSHcomNetEndpoint *>(qpUpContext);
        auto childEp = endpointPtr.ToChild<NetUBAsyncEndpoint>();
        if (childEp == nullptr) {
            NN_LOG_ERROR("Failed to get async ep");
            worker->RePostReceive(ctx);
            return NN_ERROR;
        }
        if (!childEp->mIsNeedEncrypt) {
            NN_LOG_ERROR("Failed to validate encrypt by driver support but ep not.");
            worker->RePostReceive(ctx);
            return NN_INVALID_PARAM;
        }
        size_t decryptRawLen = childEp->mAes.GetRawLen(ctx->dataSize);
        messageReady = msg.AllocateIfNeed(decryptRawLen);
        if (NN_LIKELY(messageReady)) {
            uint32_t decryptLen = 0;
            if (!childEp->mAes.Decrypt(childEp->mSecrets, reinterpret_cast<void *>(ctx->mrMemAddr), ctx->dataSize,
                msg.mBuf, decryptLen)) {
                NN_LOG_ERROR("Failed to decrypt data");
                (void)worker->RePostReceive(ctx);
                return NN_DECRYPT_FAILED;
            }
            msg.mDataLen = decryptRawLen;
        }
    }

    int ret = 0;

    // after repost the ctx cannot be used anymore
    if (NN_UNLIKELY((ret = worker->RePostReceive(ctx)) != 0)) {
        NN_LOG_ERROR("Failed to repost receive in Driver " << mName << ", ret " << ret);
    }

    if (NN_UNLIKELY(!messageReady)) {
        NN_LOG_ERROR("Failed to build UBSHcomNetRequestContext or message in Driver " << mName <<
            ", receive message [opCode: " << netCtx.mHeader.opCode << ", dataSize " << msg.mDataLen <<
            "] will be dropped");
        return NN_OK;
    }

    netCtx.mMessage = &msg;
    netCtx.mEp.Set(reinterpret_cast<UBSHcomNetEndpoint *>(qpUpContext));
    netCtx.mHeader.Invalid();
    netCtx.mHeader.dataLength = msg.mDataLen;
    netCtx.mHeader.seqNo = immData;
    netCtx.mOpType = UBSHcomNetRequestContext::NN_RECEIVED_RAW;
    netCtx.mOriginalReq = {};

    // call to callback
    if (NN_UNLIKELY((ret = mReceivedRequestHandler(netCtx)) != NN_OK)) {
        NN_LOG_ERROR("Call receivedRequestHandler in Driver " << mName <<
            " return non-zero for receive message [opCode: " << netCtx.mHeader.opCode << ", dataSize " <<
            netCtx.mHeader.dataLength << "]");
    }

    netCtx.mEp.Set(nullptr);

    return NN_OK;
}

NResult NetDriverUBWithOob::NewReceivedRequest(UBOpContextInfo *ctx, UBSHcomNetRequestContext &netCtx,
    UBSHcomNetMessage &msg, UBWorker *worker) const
{
    bool messageReady = true;
    auto *tmpHeader = reinterpret_cast<UBSHcomNetTransHeader *>(ctx->mrMemAddr);
    auto qpUpContext = ctx->ubJetty->GetUpContext();

    auto rst = NetFunc::ValidateHeaderWithDataSize(*tmpHeader, ctx->dataSize);
    if (NN_UNLIKELY(rst != NN_OK)) {
        worker->RePostReceive(ctx);
        return rst;
    }

    // 非加密场景可以免拷贝
    if (NN_LIKELY(!mOptions.enableTls)) {
        auto tmpDataAddress = reinterpret_cast<void *>(ctx->mrMemAddr + sizeof(UBSHcomNetTransHeader));
        return NewReceivedRequestWithoutCopy(ctx, netCtx, msg, worker, tmpDataAddress, tmpHeader);
    }

    UBSHcomNetEndpointPtr ep = reinterpret_cast<UBSHcomNetEndpoint *>(qpUpContext);
    auto asyncEp = ep.ToChild<NetUBAsyncEndpoint>();
    if (asyncEp == nullptr) {
        NN_LOG_ERROR("Failed to get async ep");
        worker->RePostReceive(ctx);
        return NN_ERROR;
    }
    if (!asyncEp->mIsNeedEncrypt) {
        NN_LOG_ERROR("Failed to validate encrypt by driver support but ep not.");
        worker->RePostReceive(ctx);
        return NN_INVALID_PARAM;
    }
    uint32_t decryptRawLen = asyncEp->mAes.GetRawLen(tmpHeader->dataLength);
    messageReady = msg.AllocateIfNeed(decryptRawLen);
    if (NN_LIKELY(messageReady)) {
        uint32_t decryptLen = 0;
        if (!asyncEp->mAes.Decrypt(asyncEp->mSecrets, reinterpret_cast<void *>(ctx->mrMemAddr +
            sizeof(UBSHcomNetTransHeader)), tmpHeader->dataLength, msg.mBuf, decryptLen)) {
            NN_LOG_ERROR("Failed to decrypt data");
            (void)worker->RePostReceive(ctx);
            return NN_DECRYPT_FAILED;
        }
        if (NN_UNLIKELY(memcpy_s(&(netCtx.mHeader), sizeof(UBSHcomNetTransHeader), tmpHeader,
            sizeof(UBSHcomNetTransHeader)) != UB_OK)) {
            NN_LOG_ERROR("Failed to copy header to netCtx");
            worker->RePostReceive(ctx);
            return NN_INVALID_PARAM;
        }
        msg.mDataLen = decryptRawLen;
    }

    int result = 0;
    if (NN_UNLIKELY((result = worker->RePostReceive(ctx)) != 0)) {
        NN_LOG_ERROR("Failed to repost receive in Driver " << mName << ", result " << result);
    }

    if (NN_UNLIKELY(!messageReady)) {
        NN_LOG_ERROR("Failed to build UBSHcomNetRequestContext or message in Driver " << mName <<
            ", receive message [opCode: " << netCtx.mHeader.opCode << ", dataSize " << msg.mDataLen <<
            "] will be dropped");
        return NN_OK;
    }

    netCtx.mEp.Set(reinterpret_cast<UBSHcomNetEndpoint *>(qpUpContext));
    netCtx.mMessage = &msg;
    netCtx.mOpType = UBSHcomNetRequestContext::NN_RECEIVED;
    netCtx.mOriginalReq = {};
    netCtx.mHeader.dataLength = msg.mDataLen;

    // call to callback
    if (NN_UNLIKELY((result = mReceivedRequestHandler(netCtx)) != NN_OK)) {
        NN_LOG_ERROR("Call receivedRequestHandler in Driver " << mName <<
            " return non-zero for receive message [opCode: " << netCtx.mHeader.opCode << ", dataSize " <<
            netCtx.mHeader.dataLength << "]");
    }

    netCtx.mEp.Set(nullptr);

    return NN_OK;
}

NResult NetDriverUBWithOob::NewReceivedRequestWithoutCopy(UBOpContextInfo *ctx, UBSHcomNetRequestContext &netCtx,
    UBSHcomNetMessage &msg, UBWorker *worker, void *dataAddress, UBSHcomNetTransHeader *header) const
{
    if (NN_UNLIKELY(memcpy_s(&(netCtx.mHeader), sizeof(UBSHcomNetTransHeader), header,
        sizeof(UBSHcomNetTransHeader)) != NN_OK)) {
        NN_LOG_ERROR("Failed to copy req to sglCtx");
        return NN_INVALID_PARAM;
    }
    msg.SetBuf(dataAddress, header->dataLength);
    msg.mDataLen = header->dataLength;

    netCtx.mEp.Set(reinterpret_cast<UBSHcomNetEndpoint *>(ctx->ubJetty->GetUpContext()));
    netCtx.mOpType = UBSHcomNetRequestContext::NN_RECEIVED;
    netCtx.mMessage = &msg;
    netCtx.mOriginalReq = {};
    netCtx.mHeader.dataLength = msg.mDataLen;
    netCtx.extHeaderType = header->extHeaderType; // 指导服务层处理
    int result = 0;
    // call to callback
    if (NN_UNLIKELY((result = mReceivedRequestHandler(netCtx)) != NN_OK)) {
        NN_LOG_WARN("Verbs Call receivedRequestHandler in Driver " << mName <<
            " return non-zero for receive message [opCode: " << netCtx.mHeader.opCode << ", dataSize " <<
            netCtx.mHeader.dataLength << "]");
    }
    msg.SetBuf(nullptr, 0);
    netCtx.mMessage = nullptr;
    netCtx.mEp.Set(nullptr);

    if (NN_UNLIKELY((result = worker->RePostReceive(ctx)) != 0)) {
        NN_LOG_WARN("Verbs Failed to repost receive in Driver " << mName << ", result " << result);
    }

    return NN_OK;
}

int NetDriverUBWithOob::SendFinished(UBOpContextInfo *ctx)
{
    if (NN_UNLIKELY(!ValidateRequestContext(ctx))) {
        return NN_ERROR;
    }

    if (NN_UNLIKELY(ctx->HasInternalError())) {
        ProcessQPError(ctx);
        return NN_OK;
    }

    return SendFinishedCB(ctx);
}

int NetDriverUBWithOob::OneSideDone(UBOpContextInfo *ctx)
{
    if (NN_UNLIKELY(!ValidateRequestContext(ctx))) {
        return NN_ERROR;
    }

    if (NN_UNLIKELY(ctx->HasInternalError())) {
        ProcessQPError(ctx);
        return NN_OK;
    }

    return OneSideDoneCB(ctx);
}

NResult NetDriverUBWithOob::Connect(const std::string &serverUrl, const std::string &payload, UBSHcomNetEndpointPtr &ep,
    uint32_t flags, uint8_t serverGrpNo, uint8_t clientGrpNo, uint64_t ctx)
{
    NetDriverOobType type;
    std::string ip;
    uint16_t port = 0;
    if (NN_UNLIKELY(NetFunc::NN_ValidateUrl(serverUrl) != NN_OK)) {
        NN_LOG_ERROR("Invalid url");
        return NN_PARAM_INVALID;
    }
    if (NN_UNLIKELY(ParseUrl(serverUrl, type, ip, port) != NN_OK)) {
        NN_LOG_WARN("Invalid url, url:" << serverUrl);
        return NN_INVALID_PARAM;
    }

    if (type == NetDriverOobType::NET_OOB_UB) {
        OobEidAndJettyId(ip, port);
        mOptions.oobType = NetDriverOobType::NET_OOB_UB;
    }

    return Connect(ip, port, payload, ep, flags, serverGrpNo, clientGrpNo, ctx);
}
}
}
#endif
