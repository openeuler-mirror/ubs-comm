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
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <fcntl.h>
#include <sys/poll.h>

#include "net_monotonic.h"
#include "net_oob_ssl.h"
#include "net_ub_endpoint.h"
#include "ub_mr_fixed_buf.h"
#include "ub_worker.h"
#include "net_ub_driver_oob.h"
#include "net_oob_secure.h"
#include "ub_urma_wrapper_public_jetty.h"

namespace ock {
namespace hcom {
class TestNetDriverUBPublicJetty : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);

    std::string name = "driver-public-jetty";
    NetDriverUBWithOob *driver = nullptr;
    UBPublicJetty *jetty = nullptr;
    UBJetty *qp = nullptr;
};

void TestNetDriverUBPublicJetty::SetUp()
{
    driver = new (std::nothrow) NetDriverUBWithOob(name, false, UBC);
    ASSERT_NE(driver, nullptr);
    jetty = new (std::nothrow) UBPublicJetty(name, 0, nullptr, nullptr);
    ASSERT_NE(jetty, nullptr);
    qp = new (std::nothrow) UBJetty(name, 0, nullptr, nullptr);
    ASSERT_NE(qp, nullptr);
    qp->StoreExchangeInfo(new UBJettyExchangeInfo);
}

void TestNetDriverUBPublicJetty::TearDown()
{
    if (driver != nullptr) {
        delete driver;
        driver = nullptr;
    }
    if (jetty != nullptr) {
        delete jetty;
        jetty = nullptr;
    }
    if (qp != nullptr) {
        delete qp;
        qp = nullptr;
    }
    GlobalMockObject::verify();
}

TEST_F(TestNetDriverUBPublicJetty, CreateUrmaListeners)
{
    UBPublicJetty *publicJetty = nullptr;
    EXPECT_EQ(driver->CreateUrmaListeners(publicJetty), NN_INVALID_PARAM);

    UBSHcomNetOobListenerOptions opt{};
    opt.port = NN_NO4;
    driver->mOobListenOptions.emplace_back(opt);
    MOCKER_CPP(&NetDriverUBWithOob::CreatePublicJetty)
        .stubs()
        .with(outBound(jetty), any())
        .will(returnValue(1))
        .then(returnValue(0));
    MOCKER_CPP(&NetWorkerLB::AddWorkerGroups).stubs().will(returnValue(1)).then(returnValue(0));
    EXPECT_EQ(driver->CreateUrmaListeners(publicJetty), NN_ERROR);
    EXPECT_EQ(driver->CreateUrmaListeners(publicJetty), NN_ERROR);
    EXPECT_EQ(driver->CreateUrmaListeners(publicJetty), NN_OK);
}

TEST_F(TestNetDriverUBPublicJetty, CreateUrmaListenersFailed)
{
    UBPublicJetty *publicJetty = nullptr;
    UBSHcomNetOobListenerOptions opt{};
    opt.port = NN_NO2;
    driver->mOobListenOptions.emplace_back(opt);
    EXPECT_EQ(driver->CreateUrmaListeners(publicJetty), NN_ERROR);
}

TEST_F(TestNetDriverUBPublicJetty, ConnectByPublicJetty)
{
    std::string oobIp("1.2.3.4");
    std::string payload("hello");
    UBSHcomNetEndpointPtr outEp = nullptr;

    MOCKER_CPP(&NetDriverUBWithOob::ClientCheckState).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&NetDriverUBWithOob::ConnectSyncEpByPublicJetty).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUBWithOob::ConnectASyncEpByPublicJetty).stubs().will(returnValue(0));

    EXPECT_EQ(driver->ConnectByPublicJetty(oobIp, NN_NO4, payload, outEp, 0, 0, 0), NN_ERROR);
    EXPECT_EQ(driver->ConnectByPublicJetty(oobIp, NN_NO4, payload, outEp, NET_EP_SELF_POLLING, 0, 0), 0);
    EXPECT_EQ(driver->ConnectByPublicJetty(oobIp, NN_NO4, payload, outEp, 0, 0, 0), 0);
}

TEST_F(TestNetDriverUBPublicJetty, ConnectByPublicJettyFail)
{
    std::string oobIp("1.2.3.4");
    std::string payload("hello");
    UBSHcomNetEndpointPtr outEp = nullptr;
    MOCKER_CPP(&NetDriverUBWithOob::ClientCheckState).stubs().will(returnValue(1)).then(returnValue(0));
    EXPECT_EQ(driver->ConnectByPublicJetty(oobIp, NN_NO2, payload, outEp, 0, 0, 0), NN_ERROR);
}

TEST_F(TestNetDriverUBPublicJetty, ClientCheckState)
{
    std::string payload("hello");

    driver->mInited.store(false);
    EXPECT_EQ(driver->ClientCheckState(payload), NN_NOT_INITIALIZED);

    driver->mInited.store(true);
    driver->mStarted = false;
    EXPECT_EQ(driver->ClientCheckState(payload), NN_ERROR);

    driver->mStarted = true;
    std::string payload2(NN_NO2048, 'a');
    EXPECT_EQ(driver->ClientCheckState(payload2), NN_INVALID_PARAM);
    EXPECT_EQ(driver->ClientCheckState(payload), NN_OK);
}

TEST_F(TestNetDriverUBPublicJetty, CreatePublicJetty)
{
    int err = 1;
    UBPublicJetty *publicJetty = nullptr;
    MOCKER_CPP(&UBJfc::Initialize).stubs().will(returnValue(err)).then(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::InitializePublicJetty).stubs().will(returnValue(err)).then(returnValue(0));

    EXPECT_EQ(driver->CreatePublicJetty(publicJetty, 0), err);
    EXPECT_EQ(driver->CreatePublicJetty(publicJetty, 0), NN_ERROR);
    EXPECT_EQ(driver->CreatePublicJetty(publicJetty, 0), NN_OK);

    if (publicJetty != nullptr) {
        delete publicJetty;
        publicJetty = nullptr;
    }
}

TEST_F(TestNetDriverUBPublicJetty, PublicJettyConnect1)
{
    std::string oobIp("1.2.3.4");
    UBPublicJetty *clientPublicJetty = nullptr;

    MOCKER_CPP(&NetDriverUBWithOob::CreatePublicJetty)
        .stubs()
        .will(returnValue(1));

    EXPECT_EQ(driver->PublicJettyConnect(oobIp, 1, clientPublicJetty), NN_ERROR);
}

TEST_F(TestNetDriverUBPublicJetty, PublicJettyConnect2)
{
    std::string oobIp("1.2.3.4");
    UBPublicJetty *clientPublicJetty = nullptr;
    
    UBPublicJetty* tmp = new (std::nothrow) UBPublicJetty(name, 0, nullptr, nullptr);
    MOCKER_CPP(&NetDriverUBWithOob::CreatePublicJetty)
        .stubs()
        .with(outBound(tmp), any())
        .will(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::StartPublicJetty).stubs().will(returnValue(1));

    EXPECT_EQ(driver->PublicJettyConnect(oobIp, 1, clientPublicJetty), NN_ERROR);
}

TEST_F(TestNetDriverUBPublicJetty, PublicJettyConnect3)
{
    std::string oobIp("1.2.3.4");
    UBPublicJetty *clientPublicJetty = nullptr;

    UBPublicJetty* tmp = new (std::nothrow) UBPublicJetty(name, 0, nullptr, nullptr);
    MOCKER_CPP(&NetDriverUBWithOob::CreatePublicJetty)
        .stubs()
        .with(outBound(tmp), any())
        .will(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::StartPublicJetty).stubs().will(returnValue(0));
    MOCKER_CPP(HcomUrma::StrToEid).stubs().will(returnValue(1));

    EXPECT_EQ(driver->PublicJettyConnect(oobIp, 1, clientPublicJetty), NN_ERROR);
}

TEST_F(TestNetDriverUBPublicJetty, PublicJettyConnect4)
{
    std::string oobIp("1.2.3.4");
    UBPublicJetty *clientPublicJetty = nullptr;

    UBPublicJetty* tmp = new (std::nothrow) UBPublicJetty(name, 0, nullptr, nullptr);
    MOCKER_CPP(&NetDriverUBWithOob::CreatePublicJetty)
        .stubs()
        .with(outBound(tmp), any())
        .will(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::StartPublicJetty).stubs().will(returnValue(0));
    MOCKER_CPP(HcomUrma::StrToEid).stubs().will(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::ImportPublicJetty).stubs().will(returnValue(1));

    EXPECT_EQ(driver->PublicJettyConnect(oobIp, 1, clientPublicJetty), NN_ERROR);
}

TEST_F(TestNetDriverUBPublicJetty, PublicJettyConnect5)
{
    std::string oobIp("1.2.3.4");
    UBPublicJetty *clientPublicJetty = nullptr;

    UBPublicJetty* tmp = new (std::nothrow) UBPublicJetty(name, 0, nullptr, nullptr);
    MOCKER_CPP(&NetDriverUBWithOob::CreatePublicJetty)
        .stubs()
        .with(outBound(tmp), any())
        .will(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::StartPublicJetty).stubs().will(returnValue(0));
    MOCKER_CPP(HcomUrma::StrToEid).stubs().will(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::ImportPublicJetty).stubs().will(returnValue(0));

    EXPECT_EQ(driver->PublicJettyConnect(oobIp, 1, clientPublicJetty), NN_OK);
    delete tmp;
}

TEST_F(TestNetDriverUBPublicJetty, ClientSelectWorker)
{
    NetWorkerLB *lb = new NetWorkerLB(name, NET_ROUND_ROBIN, 1);
    NetMemPoolFixed *memPool = nullptr;
    NetMemPoolFixed *sglMemPool = nullptr;
    UBWorkerOptions workerOptions{};
    UBWorker *worker = new UBWorker(name, 0, workerOptions, memPool, sglMemPool);
    UBWorker *outWorker = nullptr;

    driver->mClientLb = lb;
    driver->mWorkers.emplace_back(worker);
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs().will(returnValue(false)).then(returnValue(true));

    EXPECT_EQ(driver->ClientSelectWorker(outWorker, 0, 0), NN_ERROR);
    EXPECT_EQ(driver->ClientSelectWorker(outWorker, 0, 0), NN_OK);
    if (lb != nullptr) {
        delete lb;
        lb = nullptr;
    }
    if (worker != nullptr) {
        delete worker;
        worker = nullptr;
    }
}

TEST_F(TestNetDriverUBPublicJetty, ClientSendConnReq)
{
    std::string payload("hello");

    MOCKER_CPP(&NetDriverUBWithOob::FillExchMsg).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::SendByPublicJetty).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::PollingCompletion).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::GetJettyId).stubs().will(returnValue(1));

    EXPECT_EQ(driver->ClientSendConnReq(payload, 0, 0, nullptr, qp, nullptr), UB_PARAM_INVALID);
    EXPECT_EQ(driver->ClientSendConnReq(payload, 0, 0, jetty, qp, nullptr), NN_ERROR);
    EXPECT_EQ(driver->ClientSendConnReq(payload, 0, 0, jetty, qp, nullptr), NN_ERROR);
    EXPECT_EQ(driver->ClientSendConnReq(payload, 0, 0, jetty, qp, nullptr), NN_ERROR);
    EXPECT_EQ(driver->ClientSendConnReq(payload, 0, 0, jetty, qp, nullptr), NN_OK);
}

TEST_F(TestNetDriverUBPublicJetty, CheckServerACK)
{
    JettyConnResp exchangeMsg{};
    exchangeMsg.connResp = MAGIC_MISMATCH;
    EXPECT_EQ(driver->CheckServerACK(exchangeMsg), NN_CONNECT_REFUSED);
    exchangeMsg.connResp = WORKER_GRPNO_MISMATCH;
    EXPECT_EQ(driver->CheckServerACK(exchangeMsg), NN_CONNECT_REFUSED);
    exchangeMsg.connResp = PROTOCOL_MISMATCH;
    EXPECT_EQ(driver->CheckServerACK(exchangeMsg), NN_CONNECT_PROTOCOL_MISMATCH);
    exchangeMsg.connResp = SERVER_INTERNAL_ERROR;
    EXPECT_EQ(driver->CheckServerACK(exchangeMsg), NN_ERROR);
    exchangeMsg.connResp = VERSION_MISMATCH;
    EXPECT_EQ(driver->CheckServerACK(exchangeMsg), NN_CONNECT_REFUSED);
    exchangeMsg.connResp = TLS_VERSION_MISMATCH;
    EXPECT_EQ(driver->CheckServerACK(exchangeMsg), NN_CONNECT_REFUSED);
    exchangeMsg.connResp = OK;
    EXPECT_EQ(driver->CheckServerACK(exchangeMsg), NN_OK);
    exchangeMsg.connResp = OK_PROTOCOL_TCP;
    EXPECT_EQ(driver->CheckServerACK(exchangeMsg), NN_ERROR);
}

TEST_F(TestNetDriverUBPublicJetty, ClientEstablishConnOnReply)
{
    UBJettyExchangeInfo info{};
    MOCKER_CPP(&UBPublicJetty::Receive).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&NetDriverUBWithOob::CheckServerACK).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::SetBondingInfo).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::ImportPublicJetty).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBJetty::ChangeToReady).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::SendByPublicJetty).stubs().will(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::PollingCompletion).stubs().will(returnValue(0));

    EXPECT_EQ(driver->ClientEstablishConnOnReply(nullptr, qp, info), UB_PARAM_INVALID);
    EXPECT_EQ(driver->ClientEstablishConnOnReply(jetty, qp, info), NN_ERROR);
    EXPECT_EQ(driver->ClientEstablishConnOnReply(jetty, qp, info), NN_ERROR);
    EXPECT_EQ(driver->ClientEstablishConnOnReply(jetty, qp, info), NN_ERROR);
    EXPECT_EQ(driver->ClientEstablishConnOnReply(jetty, qp, info), NN_ERROR);
    EXPECT_EQ(driver->ClientEstablishConnOnReply(jetty, qp, info), NN_ERROR);
    EXPECT_EQ(driver->ClientEstablishConnOnReply(jetty, qp, info), NN_OK);

    MOCKER_CPP(&operator new, void *(*) (size_t, const std::nothrow_t &))
        .stubs()
        .will(returnValue(static_cast<void *>(nullptr)));
    EXPECT_EQ(driver->ClientEstablishConnOnReply(jetty, qp, info), NN_MALLOC_FAILED);
}

TEST_F(TestNetDriverUBPublicJetty, ClientCreateJetty1)
{
    UBJetty *outQp = nullptr;
    NetMemPoolFixed *memPool = nullptr;
    NetMemPoolFixed *sglMemPool = nullptr;
    UBWorkerOptions workerOptions{};

    EXPECT_EQ(driver->ClientCreateJetty(outQp, nullptr), NN_PARAM_INVALID);
}

TEST_F(TestNetDriverUBPublicJetty, ClientCreateJetty2)
{
    UBJetty *outQp = nullptr;
    NetMemPoolFixed *memPool = nullptr;
    NetMemPoolFixed *sglMemPool = nullptr;
    UBWorkerOptions workerOptions{};
    UBWorker *worker = new UBWorker(name, 0, workerOptions, memPool, sglMemPool);

    MOCKER_CPP(&UBWorker::CreateQP).stubs().will(returnValue(1));

    EXPECT_EQ(driver->ClientCreateJetty(outQp, worker), NN_ERROR);

    if (worker != nullptr) {
        delete worker;
        worker = nullptr;
    }
}

TEST_F(TestNetDriverUBPublicJetty, ClientCreateJetty3)
{
    UBJetty *outQp = nullptr;
    NetMemPoolFixed *memPool = nullptr;
    NetMemPoolFixed *sglMemPool = nullptr;
    UBWorkerOptions workerOptions{};
    UBWorker *worker = new UBWorker(name, 0, workerOptions, memPool, sglMemPool);
    UBJetty* tmp = new (std::nothrow) UBJetty(name, 0, nullptr, nullptr);

    MOCKER_CPP(&UBWorker::CreateQP).stubs().with(outBound(tmp)).will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(1));

    EXPECT_EQ(driver->ClientCreateJetty(outQp, worker), NN_ERROR);

    if (worker != nullptr) {
        delete worker;
        worker = nullptr;
    }
}

TEST_F(TestNetDriverUBPublicJetty, ClientCreateJetty4)
{
    UBJetty *outQp = nullptr;
    NetMemPoolFixed *memPool = nullptr;
    NetMemPoolFixed *sglMemPool = nullptr;
    UBWorkerOptions workerOptions{};
    UBWorker *worker = new UBWorker(name, 0, workerOptions, memPool, sglMemPool);
    UBJetty* tmp = new (std::nothrow) UBJetty(name, 0, nullptr, nullptr);

    MOCKER_CPP(&UBWorker::CreateQP).stubs().with(outBound(tmp)).will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));

    EXPECT_EQ(driver->ClientCreateJetty(outQp, worker), NN_OK);

    delete tmp;
    if (worker != nullptr) {
        delete worker;
        worker = nullptr;
    }
}

TEST_F(TestNetDriverUBPublicJetty, CheckMagicAndProtocol)
{
    JettyConnResp exchangeMsg{};
    JettyConnHeader exchangeInfo{};

    exchangeInfo.ConnectHeader.magic = NN_NO2;
    MOCKER_CPP(&UBPublicJetty::SendByPublicJetty).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUBWithOob::Protocol).stubs().will(returnValue(UBC));
    EXPECT_EQ(driver->CheckMagicAndProtocol(exchangeMsg, &exchangeInfo, jetty), NN_ERROR);

    exchangeInfo.ConnectHeader.magic = NN_NO256;
    exchangeInfo.ConnectHeader.protocol = UBC;
    EXPECT_EQ(driver->CheckMagicAndProtocol(exchangeMsg, &exchangeInfo, jetty), NN_OK);
}

TEST_F(TestNetDriverUBPublicJetty, FillExchMsg)
{
    JettyConnHeader *exchangeInfo = (JettyConnHeader *)malloc(sizeof(JettyConnHeader) + NN_NO6);
    std::string payload("hello");

    MOCKER_CPP(&UBJetty::FillExchangeInfo).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::FillBondingMsg).stubs().will(returnValue(0));
    MOCKER_CPP(memcpy_s).stubs().will(returnValue(1)).then(returnValue(0));

    EXPECT_EQ(driver->FillExchMsg(exchangeInfo, qp, payload, 0, jetty), 1);
    EXPECT_EQ(driver->FillExchMsg(exchangeInfo, qp, payload, 0, jetty), NN_ERROR);
    EXPECT_EQ(driver->FillExchMsg(exchangeInfo, qp, payload, 0, jetty), NN_OK);
    free(exchangeInfo);
}

TEST_F(TestNetDriverUBPublicJetty, FillExchMsgHeartBeat)
{
    JettyConnHeader *exchangeInfo = (JettyConnHeader *)malloc(sizeof(JettyConnHeader) + NN_NO6);
    std::string payload("hello");
    NetHeartbeat *hb = new (std::nothrow) NetHeartbeat(nullptr, 0, 0);
    driver->mHeartBeat = hb;
    UBEId eid{};
    UBContext *ctx = new (std::nothrow) UBContext("name", eid);
    qp->mUBContext = ctx;
    qp->mUBContext->protocol = UBSHcomNetDriverProtocol::UBC;

    MOCKER_CPP(&UBJetty::CreateHBMemoryRegion).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::GetRemoteHbInfo).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBJetty::FillExchangeInfo).stubs().will(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::FillBondingMsg).stubs().will(returnValue(0));
    MOCKER_CPP(memcpy_s).stubs().will(returnValue(0));
    EXPECT_EQ(driver->FillExchMsg(exchangeInfo, qp, payload, 0, jetty), NN_OK);
    free(exchangeInfo);
    if (hb != nullptr) {
        delete hb;
    }
    driver->mHeartBeat = nullptr;
}

TEST_F(TestNetDriverUBPublicJetty, FillExchMsgHeartBeatErr)
{
    JettyConnHeader *exchangeInfo = (JettyConnHeader *)malloc(sizeof(JettyConnHeader) + NN_NO6);
    std::string payload("hello");
    NetHeartbeat *hb = new (std::nothrow) NetHeartbeat(nullptr, 0, 0);
    driver->mHeartBeat = hb;
    UBEId eid{};
    UBContext *ctx = new (std::nothrow) UBContext("name", eid);
    qp->mUBContext = ctx;
    qp->mUBContext->protocol = UBSHcomNetDriverProtocol::UBC;

    MOCKER_CPP(&UBJetty::CreateHBMemoryRegion).stubs().will(returnValue(0)).then(returnValue(1));
    MOCKER_CPP(&UBPublicJetty::FillBondingMsg).stubs().will(returnValue(0));

    EXPECT_EQ(driver->FillExchMsg(exchangeInfo, qp, payload, 0, jetty), 1);
    EXPECT_EQ(driver->FillExchMsg(exchangeInfo, qp, payload, 0, jetty), 1);
    free(exchangeInfo);
    if (hb != nullptr) {
        delete hb;
    }
    driver->mHeartBeat = nullptr;
}

TEST_F(TestNetDriverUBPublicJetty, PrePostReceiveOnConnection)
{
    NetMemPoolFixed *memPool = nullptr;
    NetMemPoolFixed *sglMemPool = nullptr;
    UBWorkerOptions workerOptions{};
    UBWorker *worker = new UBWorker(name, 0, workerOptions, memPool, sglMemPool);
    urma_target_seg_t *tseg = nullptr;

    MOCKER_CPP(&UBJetty::GetFreeBufferN).stubs().will(returnValue(false)).then(returnValue(true));
    MOCKER_CPP(&UBWorker::PostReceive).stubs().will(returnValue(1));
    MOCKER_CPP(&UBJetty::ReturnBuffer).stubs().will(returnValue(true));
    MOCKER_CPP(&UBJetty::GetMemorySeg).stubs().will(returnValue(tseg));

    EXPECT_EQ(driver->PrePostReceiveOnConnection(qp, nullptr), UB_PARAM_INVALID);
    EXPECT_EQ(driver->PrePostReceiveOnConnection(qp, worker), NN_ERROR);
    EXPECT_EQ(driver->PrePostReceiveOnConnection(qp, worker), 1);
    if (worker != nullptr) {
        delete worker;
        worker = nullptr;
    }
}

TEST_F(TestNetDriverUBPublicJetty, ServerSelectWorker)
{
    NetMemPoolFixed *memPool = nullptr;
    NetMemPoolFixed *sglMemPool = nullptr;
    UBWorkerOptions workerOptions{};
    UBWorker *worker = new UBWorker(name, 0, workerOptions, memPool, sglMemPool);
    UBWorker *outWorker = nullptr;
    JettyConnResp exchangeMsg{};

    driver->mPublicJetty = jetty;
    driver->mWorkers.emplace_back(worker);
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs().will(returnValue(false)).then(returnValue(true));
    MOCKER_CPP(&UBPublicJetty::SendByPublicJetty).stubs().will(returnValue(0));
    MOCKER_CPP(&UBWorker::IsWorkStarted).stubs().will(returnValue(false)).then(returnValue(true));
    NetWorkerLBPtr lbOne = nullptr;
    NetWorkerLBPtr lb = new NetWorkerLB(name, NET_ROUND_ROBIN, 1);
    MOCKER_CPP(&UBPublicJetty::LoadBalancer).stubs().will(returnValue(lbOne)).then(returnValue(lb));

    EXPECT_EQ(driver->ServerSelectWorker(outWorker, exchangeMsg, 0, jetty), NN_ERROR);
    jetty->mWorkerLb = lb;
    lb->IncreaseRef();
    EXPECT_EQ(driver->ServerSelectWorker(outWorker, exchangeMsg, 0, jetty), NN_ERROR);
    lb->IncreaseRef();
    EXPECT_EQ(driver->ServerSelectWorker(outWorker, exchangeMsg, 0, jetty), NN_ERROR);
    lb->IncreaseRef();
    EXPECT_EQ(driver->ServerSelectWorker(outWorker, exchangeMsg, 0, jetty), NN_OK);

    driver->mPublicJetty = nullptr;
    if (worker != nullptr) {
        delete worker;
        worker = nullptr;
    }
}

TEST_F(TestNetDriverUBPublicJetty, ServerCreateJetty1)
{
    UBJetty *outQp = nullptr;
    NetMemPoolFixed *memPool = nullptr;
    NetMemPoolFixed *sglMemPool = nullptr;
    UBWorkerOptions workerOptions{};
    UBWorker *worker = new UBWorker(name, 0, workerOptions, memPool, sglMemPool);
    JettyConnResp exchangeMsg{};
    JettyConnHeader info{};

    MOCKER_CPP(&UBWorker::CreateQP).stubs().will(returnValue(1));
    MOCKER_CPP(&UBPublicJetty::SendByPublicJetty).stubs().will(returnValue(0));
    EXPECT_EQ(driver->ServerCreateJetty(outQp, worker, exchangeMsg, &info, jetty), NN_ERROR);

    if (worker != nullptr) {
        delete worker;
        worker = nullptr;
    }
}

TEST_F(TestNetDriverUBPublicJetty, ServerCreateJetty2)
{
    UBJetty *outQp = nullptr;
    NetMemPoolFixed *memPool = nullptr;
    NetMemPoolFixed *sglMemPool = nullptr;
    UBWorkerOptions workerOptions{};
    UBWorker *worker = new UBWorker(name, 0, workerOptions, memPool, sglMemPool);
    JettyConnResp exchangeMsg{};
    JettyConnHeader info{};

    UBJetty* tmp = new (std::nothrow) UBJetty(name, 0, nullptr, nullptr);
    MOCKER_CPP(&UBWorker::CreateQP).stubs().with(outBound(tmp)).will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(1));
    MOCKER_CPP(&UBPublicJetty::SendByPublicJetty).stubs().will(returnValue(0));

    EXPECT_EQ(driver->ServerCreateJetty(outQp, worker, exchangeMsg, &info, jetty), NN_ERROR);
    if (worker != nullptr) {
        delete worker;
        worker = nullptr;
    }
}

TEST_F(TestNetDriverUBPublicJetty, ServerCreateJetty3)
{
    UBJetty *outQp = nullptr;
    NetMemPoolFixed *memPool = nullptr;
    NetMemPoolFixed *sglMemPool = nullptr;
    UBWorkerOptions workerOptions{};
    UBWorker *worker = new UBWorker(name, 0, workerOptions, memPool, sglMemPool);
    JettyConnResp exchangeMsg{};
    JettyConnHeader info{};

    UBJetty* tmp = new (std::nothrow) UBJetty(name, 0, nullptr, nullptr);
    MOCKER_CPP(&UBWorker::CreateQP).stubs().with(outBound(tmp)).will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::SendByPublicJetty).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::ChangeToReady).stubs().will(returnValue(1));

    EXPECT_EQ(driver->ServerCreateJetty(outQp, worker, exchangeMsg, &info, jetty), 1);
    if (worker != nullptr) {
        delete worker;
        worker = nullptr;
    }
}

TEST_F(TestNetDriverUBPublicJetty, ServerCreateJetty4)
{
    UBJetty *outQp = nullptr;
    NetMemPoolFixed *memPool = nullptr;
    NetMemPoolFixed *sglMemPool = nullptr;
    UBWorkerOptions workerOptions{};
    UBWorker *worker = new UBWorker(name, 0, workerOptions, memPool, sglMemPool);
    JettyConnResp exchangeMsg{};
    JettyConnHeader info{};

    UBJetty* tmp = new (std::nothrow) UBJetty(name, 0, nullptr, nullptr);
    MOCKER_CPP(&UBWorker::CreateQP).stubs().with(outBound(tmp)).will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::SendByPublicJetty).stubs().will(returnValue(0));

    MOCKER_CPP(&operator new, void *(*) (size_t, const std::nothrow_t &))
        .stubs()
        .will(returnValue(static_cast<void *>(nullptr)));
    EXPECT_EQ(driver->ServerCreateJetty(outQp, worker, exchangeMsg, &info, jetty), NN_MALLOC_FAILED);

    if (worker != nullptr) {
        delete worker;
        worker = nullptr;
    }
}

TEST_F(TestNetDriverUBPublicJetty, ServerCreateJetty5)
{
    UBJetty *outQp = nullptr;
    NetMemPoolFixed *memPool = nullptr;
    NetMemPoolFixed *sglMemPool = nullptr;
    UBWorkerOptions workerOptions{};
    UBWorker *worker = new UBWorker(name, 0, workerOptions, memPool, sglMemPool);
    JettyConnResp exchangeMsg{};
    JettyConnHeader info{};

    UBJetty* tmp = new (std::nothrow) UBJetty(name, 0, nullptr, nullptr);
    MOCKER_CPP(&UBWorker::CreateQP).stubs().with(outBound(tmp)).will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::SendByPublicJetty).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::ChangeToReady).stubs().will(returnValue(0));

    EXPECT_EQ(driver->ServerCreateJetty(outQp, worker, exchangeMsg, &info, jetty), NN_OK);
    delete tmp;
    if (worker != nullptr) {
        delete worker;
        worker = nullptr;
    }
}

TEST_F(TestNetDriverUBPublicJetty, ServerReplyMsg)
{
    JettyConnResp exchangeMsg{};
    std::string name = "test-public-jetty";
    UBEId eid{};
    UBContext *ctx = new (std::nothrow) UBContext(name, eid);
    ASSERT_NE(ctx, nullptr);
    jetty->mUBContext = ctx;
    uint32_t jettyId = 0;

    MOCKER_CPP(&UBJetty::FillExchangeInfo).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::SendByPublicJetty).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::PollingCompletion).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::GetJettyId).stubs().will(returnValue(jettyId));
    MOCKER_CPP(&UBPublicJetty::FillBondingMsg).stubs().will(returnValue(0));
    MOCKER_CPP(&UBDeviceHelper::UnInitialize).stubs().will(ignoreReturnValue());

    EXPECT_EQ(driver->ServerReplyMsg(nullptr, exchangeMsg, nullptr), UB_PARAM_INVALID);
    EXPECT_EQ(driver->ServerReplyMsg(qp, exchangeMsg, jetty), 1);
    EXPECT_EQ(driver->ServerReplyMsg(qp, exchangeMsg, jetty), NN_ERROR);
    EXPECT_EQ(driver->ServerReplyMsg(qp, exchangeMsg, jetty), NN_ERROR);
    EXPECT_EQ(driver->ServerReplyMsg(qp, exchangeMsg, jetty), NN_OK);
    jetty->mUBContext = nullptr;
    delete ctx;
}

TEST_F(TestNetDriverUBPublicJetty, ServerReplyMsgHeartBeat)
{
    JettyConnResp exchangeMsg{};
    std::string name = "test-public-jetty";
    UBEId eid{};
    UBContext *ctx = new (std::nothrow) UBContext(name, eid);
    ASSERT_NE(ctx, nullptr);
    jetty->mUBContext = ctx;
    uint32_t jettyId = 0;
    NetHeartbeat *hb = new (std::nothrow) NetHeartbeat(nullptr, 0, 0);
    ASSERT_NE(hb, nullptr);
    driver->mHeartBeat = hb;

    UBEId eid2{};
    UBContext *ctx2 = new (std::nothrow) UBContext("name", eid2);
    qp->mUBContext = ctx2;
    qp->mUBContext->protocol = UBSHcomNetDriverProtocol::UBC;

    MOCKER_CPP(&UBJetty::CreateHBMemoryRegion).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::GetRemoteHbInfo).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBPublicJetty::GetJettyId).stubs().will(returnValue(jettyId));
    MOCKER_CPP(&UBPublicJetty::FillBondingMsg).stubs().will(returnValue(0));
    MOCKER_CPP(&UBDeviceHelper::UnInitialize).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBJetty::FillExchangeInfo).stubs().will(returnValue(1));

    EXPECT_EQ(driver->ServerReplyMsg(qp, exchangeMsg, jetty), 1);

    if (hb != nullptr) {
        delete hb;
    }
    driver->mHeartBeat = nullptr;
    jetty->mUBContext = nullptr;
    delete ctx;
}

TEST_F(TestNetDriverUBPublicJetty, ServerReplyMsgHeartBeatErr)
{
    JettyConnResp exchangeMsg{};
    std::string name = "test-public-jetty";
    UBEId eid{};
    UBContext *ctx = new (std::nothrow) UBContext(name, eid);
    ASSERT_NE(ctx, nullptr);
    jetty->mUBContext = ctx;
    uint32_t jettyId = 0;
    NetHeartbeat *hb = new (std::nothrow) NetHeartbeat(nullptr, 0, 0);
    ASSERT_NE(hb, nullptr);
    driver->mHeartBeat = hb;
    UBEId eid2{};
    UBContext *ctx2 = new (std::nothrow) UBContext("name", eid2);
    qp->mUBContext = ctx2;
    qp->mUBContext->protocol = UBSHcomNetDriverProtocol::UBC;

    MOCKER_CPP(&UBJetty::CreateHBMemoryRegion).stubs().will(returnValue(0)).then(returnValue(1));
    MOCKER_CPP(&UBJetty::GetRemoteHbInfo).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBPublicJetty::GetJettyId).stubs().will(returnValue(jettyId));
    MOCKER_CPP(&UBPublicJetty::FillBondingMsg).stubs().will(returnValue(0));
    MOCKER_CPP(&UBDeviceHelper::UnInitialize).stubs().will(ignoreReturnValue());

    EXPECT_EQ(driver->ServerReplyMsg(qp, exchangeMsg, jetty), 1);
    EXPECT_EQ(driver->ServerReplyMsg(qp, exchangeMsg, jetty), 1);

    if (hb != nullptr) {
        delete hb;
    }
    driver->mHeartBeat = nullptr;
    jetty->mUBContext = nullptr;
    delete ctx;
}

TEST_F(TestNetDriverUBPublicJetty, ConnectSyncEpByPublicJetty)
{
    std::string oobIp("1.2.3.4");
    std::string payload("hello");
    UBSHcomNetEndpointPtr outEp = nullptr;

    MOCKER_CPP(&NetDriverUBWithOob::PublicJettyConnect).stubs().will(returnValue(1)).then(returnValue(0));

    MOCKER_CPP(&NetDriverUBWithOob::ClientSyncEpCreateJetty)
        .stubs()
        .with(outBound(qp), any(), any())
        .will(returnValue(1))
        .then(returnValue(0));
    MOCKER_CPP(&NetDriverUBWithOob::ClientSendConnReq).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&NetDriverUBWithOob::ClientEstablishConnOnReply).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&NetDriverUBWithOob::PrePostReceiveOnSyncEp).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&NetDriverUBWithOob::ClientSyncEpSetInfo).stubs().will(ignoreReturnValue());

    qp->IncreaseRef();
    EXPECT_EQ(driver->ConnectSyncEpByPublicJetty(oobIp, 1, payload, outEp, 0, 0, 0), NN_ERROR);
    EXPECT_EQ(driver->ConnectSyncEpByPublicJetty(oobIp, 1, payload, outEp, 0, 0, 0), NN_ERROR);
    EXPECT_EQ(driver->ConnectSyncEpByPublicJetty(oobIp, 1, payload, outEp, 0, 0, 0), NN_ERROR);
    EXPECT_EQ(driver->ConnectSyncEpByPublicJetty(oobIp, 1, payload, outEp, 0, 0, 0), NN_ERROR);
    driver->IncreaseRef();
    EXPECT_EQ(driver->ConnectSyncEpByPublicJetty(oobIp, 1, payload, outEp, 0, 0, 0), NN_ERROR);
    driver->IncreaseRef();
    EXPECT_EQ(driver->ConnectSyncEpByPublicJetty(oobIp, 1, payload, outEp, 0, 0, 0), NN_ERROR);
}

TEST_F(TestNetDriverUBPublicJetty, ClientSyncEpSetInfo)
{
    UBSHcomNetWorkerIndex workerIndex{};
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetUBSyncEndpoint(0, nullptr, nullptr, NN_NO4, nullptr, workerIndex);
    reinterpret_cast<NetUBSyncEndpoint *>(ep.Get())->mJetty = qp;
    UBSHcomNetEndpointPtr outEp = nullptr;

    EXPECT_NO_FATAL_FAILURE(driver->ClientSyncEpSetInfo(ep, qp, outEp));
    reinterpret_cast<NetUBSyncEndpoint *>(ep.Get())->mJetty = nullptr;
}

TEST_F(TestNetDriverUBPublicJetty, PrePostReceiveOnSyncEp)
{
    urma_target_seg_t *tseg = nullptr;
    UBSHcomNetWorkerIndex workerIndex{};
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetUBSyncEndpoint(0, nullptr, nullptr, NN_NO4, nullptr, workerIndex);

    MOCKER_CPP(&UBJetty::GetFreeBufferN).stubs().will(returnValue(false)).then(returnValue(true));
    MOCKER_CPP(&UBJetty::ReturnBuffer).stubs().will(returnValue(true));
    MOCKER_CPP(&UBJetty::GetMemorySeg).stubs().will(returnValue(tseg));
    MOCKER_CPP(&NetUBSyncEndpoint::PostReceive).stubs().will(returnValue(1));

    EXPECT_EQ(driver->PrePostReceiveOnSyncEp(ep, NN_NO4, qp), NN_ERROR);
    EXPECT_EQ(driver->PrePostReceiveOnSyncEp(ep, NN_NO4, qp), 1);
}

TEST_F(TestNetDriverUBPublicJetty, ClientSyncEpCreateJettyFail)
{
    UBJetty *outQp = nullptr;
    UBJfc *outCq = nullptr;
    UBJfc *cq = new (std::nothrow) UBJfc(name, nullptr);
    ASSERT_NE(cq, nullptr);
    UBJetty *qpIn = new (std::nothrow) UBJetty(name, 0, nullptr, nullptr);
    ASSERT_NE(qpIn, nullptr);
    MOCKER_CPP(&NetUBSyncEndpoint::CreateResources)
        .stubs()
        .with(any(), any(), any(), any(), outBound(qpIn), outBound(cq))
        .will(returnValue(1));
    EXPECT_EQ(driver->ClientSyncEpCreateJetty(outQp, outCq, UB_BUSY_POLLING), 1);
    delete cq;
    delete qpIn;
}

TEST_F(TestNetDriverUBPublicJetty, ClientSyncEpCreateJettyFailTwo)
{
    UBJetty *outQp = nullptr;
    UBJfc *outCq = nullptr;
    UBJfc *cq = new (std::nothrow) UBJfc(name, nullptr);
    ASSERT_NE(cq, nullptr);
    UBJetty *qpIn = new (std::nothrow) UBJetty(name, 0, nullptr, nullptr);
    ASSERT_NE(qpIn, nullptr);
    MOCKER_CPP(&NetUBSyncEndpoint::CreateResources)
        .stubs()
        .with(any(), any(), any(), any(), outBound(qpIn), outBound(cq))
        .will(returnValue(0));
    MOCKER_CPP(&UBJfc::Initialize).stubs().will(returnValue(1));
    EXPECT_EQ(driver->ClientSyncEpCreateJetty(outQp, outCq, UB_BUSY_POLLING), NN_ERROR);
}

TEST_F(TestNetDriverUBPublicJetty, ClientSyncEpCreateJettyFailThree)
{
    UBJetty *outQp = nullptr;
    UBJfc *outCq = nullptr;
    UBJfc *cq = new (std::nothrow) UBJfc(name, nullptr);
    ASSERT_NE(cq, nullptr);
    UBJetty *qpIn = new (std::nothrow) UBJetty(name, 0, nullptr, nullptr);
    ASSERT_NE(qpIn, nullptr);
    MOCKER_CPP(&NetUBSyncEndpoint::CreateResources)
        .stubs()
        .with(any(), any(), any(), any(), outBound(qpIn), outBound(cq))
        .will(returnValue(0));
    MOCKER_CPP(&UBJfc::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(1));
    MOCKER_CPP(&UBJetty::UnInitialize).stubs().will(returnValue(0));
    EXPECT_EQ(driver->ClientSyncEpCreateJetty(outQp, outCq, UB_BUSY_POLLING), NN_ERROR);
}

TEST_F(TestNetDriverUBPublicJetty, ClientSyncEpCreateJettySuccess)
{
    UBJetty *outQp = nullptr;
    UBJfc *outCq = nullptr;
    UBJfc *cq = new (std::nothrow) UBJfc(name, nullptr);
    ASSERT_NE(cq, nullptr);
    UBJetty *qpIn = new (std::nothrow) UBJetty(name, 0, nullptr, nullptr);
    ASSERT_NE(qpIn, nullptr);
    MOCKER_CPP(&NetUBSyncEndpoint::CreateResources)
        .stubs()
        .with(any(), any(), any(), any(), outBound(qpIn), outBound(cq))
        .will(returnValue(0));
    MOCKER_CPP(&UBJfc::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    EXPECT_EQ(driver->ClientSyncEpCreateJetty(outQp, outCq, UB_BUSY_POLLING), NN_OK);
    delete cq;
    delete qpIn;
}

TEST_F(TestNetDriverUBPublicJetty, ClearJettyResource)
{
    UBOpContextInfo ctxInfo{};
    qp->mCtxPosted.next = &ctxInfo;
    MOCKER_CPP(&NetDriverUBWithOob::ProcessErrorNewRequest).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBJetty::Stop).stubs().will(returnValue(0));
    EXPECT_NO_FATAL_FAILURE(driver->ClearJettyResource(qp));
}

TEST_F(TestNetDriverUBPublicJetty, ClientCreateEpRecvFail)
{
    UBSHcomNetEndpointPtr outEp = nullptr;
    NetMemPoolFixed *memPool = nullptr;
    NetMemPoolFixed *sglMemPool = nullptr;
    UBWorkerOptions workerOptions{};
    UBWorker *worker = new UBWorker(name, 0, workerOptions, memPool, sglMemPool);
    ASSERT_NE(worker, nullptr);
    worker->IncreaseRef();
    UBJettyExchangeInfo info{};

    MOCKER_CPP(&UBPublicJetty::Receive).stubs().will(returnValue(1));
    worker->IncreaseRef();
    qp->IncreaseRef();
    driver->IncreaseRef();
    // auto free by std::unique_ptr
    UBJettyExchangeInfo *exchInfo = new UBJettyExchangeInfo();
    qp->StoreExchangeInfo(exchInfo);
    EXPECT_EQ(driver->ClientCreateEp(outEp, 0, qp, worker, info, jetty), NN_ERROR);
    if (worker != nullptr) {
        delete worker;
    }
}

TEST_F(TestNetDriverUBPublicJetty, ClientCreateEpSendFail)
{
    UBSHcomNetEndpointPtr outEp = nullptr;
    NetMemPoolFixed *memPool = nullptr;
    NetMemPoolFixed *sglMemPool = nullptr;
    UBWorkerOptions workerOptions{};
    UBWorker *worker = new UBWorker(name, 0, workerOptions, memPool, sglMemPool);
    ASSERT_NE(worker, nullptr);
    worker->IncreaseRef();
    UBJettyExchangeInfo info{};

    MOCKER_CPP(&UBPublicJetty::SendByPublicJetty).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::PollingCompletion).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::Receive).stubs().will(returnValue(1)).then(returnValue(0));
    worker->IncreaseRef();
    qp->IncreaseRef();
    driver->IncreaseRef();
    // auto free by std::unique_ptr
    UBJettyExchangeInfo *exchInfo = new UBJettyExchangeInfo();
    qp->StoreExchangeInfo(exchInfo);
    EXPECT_EQ(driver->ClientCreateEp(outEp, 0, qp, worker, info, jetty), NN_ERROR);

    worker->IncreaseRef();
    qp->IncreaseRef();
    driver->IncreaseRef();
    // auto free by std::unique_ptr
    exchInfo = new UBJettyExchangeInfo();
    qp->StoreExchangeInfo(exchInfo);
    EXPECT_EQ(driver->ClientCreateEp(outEp, 0, qp, worker, info, jetty), NN_ERROR);

    worker->IncreaseRef();
    qp->IncreaseRef();
    driver->IncreaseRef();
    // auto free by std::unique_ptr
    exchInfo = new UBJettyExchangeInfo();
    qp->StoreExchangeInfo(exchInfo);
    EXPECT_EQ(driver->ClientCreateEp(outEp, 0, qp, worker, info, jetty), NN_ERROR);

    worker->IncreaseRef();
    qp->IncreaseRef();
    driver->IncreaseRef();
    // auto free by std::unique_ptr
    exchInfo = new UBJettyExchangeInfo();
    qp->StoreExchangeInfo(exchInfo);
    EXPECT_EQ(driver->ClientCreateEp(outEp, 0, qp, worker, info, jetty), NN_ERROR);
    if (worker != nullptr) {
        delete worker;
    }
}

int MockNewEndPoint(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEP, const std::string &payload)
{
    if (payload == "test-handshake") {
        return 1;
    }
    return 0;
}

TEST_F(TestNetDriverUBPublicJetty, ServerCreateEpSendFail)
{
    UBJettyExchangeInfo info = qp->GetExchangeInfo();
    JettyConnHeader exchangeInfo{};
    exchangeInfo.payloadLen = 1;
    NetMemPoolFixed *memPool = nullptr;
    NetMemPoolFixed *sglMemPool = nullptr;
    UBWorkerOptions workerOptions{};
    UBWorker *worker = new UBWorker(name, 0, workerOptions, memPool, sglMemPool);
    ASSERT_NE(worker, nullptr);

    driver->RegisterNewEPHandler(
        std::bind(&MockNewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    MOCKER_CPP(&UBPublicJetty::SendByPublicJetty).stubs().will(returnValue(1));
    MOCKER_CPP(&NetDriverUBWithOob::ServerHandshake).stubs().will(returnValue(0));
    EXPECT_EQ(driver->ServerCreateEp(info, qp, nullptr, &exchangeInfo, jetty), NN_PARAM_INVALID);
    worker->IncreaseRef();
    qp->IncreaseRef();
    driver->IncreaseRef();
    EXPECT_EQ(driver->ServerCreateEp(info, qp, worker, &exchangeInfo, jetty), 0);
    if (worker != nullptr) {
        delete worker;
    }
}

TEST_F(TestNetDriverUBPublicJetty, ServerCreateEpSuccess)
{
    UBJettyExchangeInfo info = qp->GetExchangeInfo();
    JettyConnHeader exchangeInfo{};
    exchangeInfo.payloadLen = 1;
    NetMemPoolFixed *memPool = nullptr;
    NetMemPoolFixed *sglMemPool = nullptr;
    UBWorkerOptions workerOptions{};
    UBWorker *worker = new UBWorker(name, 0, workerOptions, memPool, sglMemPool);
    ASSERT_NE(worker, nullptr);

    driver->RegisterNewEPHandler(
        std::bind(&MockNewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    MOCKER_CPP(&UBPublicJetty::SendByPublicJetty).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUBWithOob::ServerHandshake).stubs().will(returnValue(0));
    worker->IncreaseRef();
    qp->IncreaseRef();
    driver->IncreaseRef();
    EXPECT_EQ(driver->ServerCreateEp(info, qp, worker, &exchangeInfo, jetty), NN_OK);
    if (worker != nullptr) {
        delete worker;
    }
}

TEST_F(TestNetDriverUBPublicJetty, ServerEstablishCtrlConn)
{
    JettyConnHeader exchangeInfo{};
    MOCKER_CPP(&UBPublicJetty::StartPublicJetty).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::SetBondingInfo).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::ImportPublicJetty).stubs().will(returnValue(1)).then(returnValue(0));

    EXPECT_EQ(driver->ServerEstablishCtrlConn(nullptr, jetty), NN_PARAM_INVALID);
    EXPECT_EQ(driver->ServerEstablishCtrlConn(&exchangeInfo, jetty), NN_ERROR);
    EXPECT_EQ(driver->ServerEstablishCtrlConn(&exchangeInfo, jetty), NN_ERROR);
    EXPECT_EQ(driver->ServerEstablishCtrlConn(&exchangeInfo, jetty), NN_ERROR);
    EXPECT_EQ(driver->ServerEstablishCtrlConn(&exchangeInfo, jetty), NN_OK);
}

TEST_F(TestNetDriverUBPublicJetty, ServerHandshakeAckFail)
{
    std::string payload = "hello";
    std::string eidAndPort = "4245:4944:0000:0000:0000:0000:0100:0000";

    UBSHcomNetWorkerIndex workerIndex{};
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetUBSyncEndpoint(0, nullptr, nullptr, NN_NO4, nullptr, workerIndex);
    driver->RegisterNewEPHandler(
        std::bind(&MockNewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    MOCKER_CPP(&UBPublicJetty::Receive).stubs().will(returnValue(1)).then(returnValue(0));

    EXPECT_EQ(driver->ServerHandshake(ep, payload, eidAndPort, jetty), NN_ERROR);
    EXPECT_EQ(driver->ServerHandshake(ep, payload, eidAndPort, jetty), NN_ERROR);
}

template<int8_t Value> UResult MockReceive(void *buf, uint32_t size)
{
    int8_t *data = reinterpret_cast<int8_t *>(buf);
    *data = Value;
    return UB_OK;
}

TEST_F(TestNetDriverUBPublicJetty, ServerHandshake)
{
    std::string payload = "hello";
    std::string test = "test-handshake";
    std::string eidAndPort = "4245:4944:0000:0000:0000:0000:0100:0000";

    UBSHcomNetWorkerIndex workerIndex{};
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetUBSyncEndpoint(0, nullptr, nullptr, NN_NO4, nullptr, workerIndex);
    driver->RegisterNewEPHandler(
        std::bind(&MockNewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    MOCKER_CPP(&UBPublicJetty::Receive).stubs().will(invoke(MockReceive<1>));
    MOCKER_CPP(&UBPublicJetty::SendByPublicJetty).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::PollingCompletion).stubs().will(returnValue(1)).then(returnValue(0));

    EXPECT_EQ(driver->ServerHandshake(ep, payload, eidAndPort, jetty), NN_ERROR);
    EXPECT_EQ(driver->ServerHandshake(ep, payload, eidAndPort, jetty), NN_ERROR);
    EXPECT_EQ(driver->ServerHandshake(ep, payload, eidAndPort, jetty), NN_OK);
    EXPECT_EQ(driver->ServerHandshake(ep, test, eidAndPort, jetty), NN_ERROR);
}

template<typename T> void *NewExceptFor(size_t sz, const std::nothrow_t &)
{
    if (sz == sizeof(T)) {
        return nullptr;
    }

    return std::malloc(sz);
}

TEST_F(TestNetDriverUBPublicJetty, CreateSyncEpMallocFail)
{
    UBSHcomNetEndpointPtr outEp = nullptr;
    UBJetty *tmpJetty = new (std::nothrow) UBJetty("name", 0, nullptr, nullptr);
    ASSERT_NE(tmpJetty, nullptr);
    tmpJetty->IncreaseRef();

    MOCKER_CPP(&operator new, void *(*)(size_t, const std::nothrow_t &))
            .stubs()
            .will(invoke(NewExceptFor<NetUBSyncEndpoint>));

    EXPECT_EQ(driver->CreateSyncEp(tmpJetty, nullptr, 0, outEp, nullptr), NN_NEW_OBJECT_FAILED);
    if (tmpJetty != nullptr) {
        delete tmpJetty;
    }
}

TEST_F(TestNetDriverUBPublicJetty, CreateSyncEpFail)
{
    UBSHcomNetEndpointPtr outEp = nullptr;
    UBJetty *tmpJetty = new (std::nothrow) UBJetty("name", 0, nullptr, nullptr);
    ASSERT_NE(tmpJetty, nullptr);
    tmpJetty->IncreaseRef();

    UBPublicJetty *pubJetty = new (std::nothrow) UBPublicJetty("pubJetty", 0x1122, nullptr, nullptr);
    ASSERT_NE(pubJetty, nullptr);
    pubJetty->IncreaseRef();

    MOCKER_CPP(&NetDriverUBWithOob::PrePostReceiveOnSyncEp)
            .stubs()
            .will(returnValue(static_cast<NResult>(NN_ERROR)))
            .then(returnValue(static_cast<NResult>(NN_OK)));
    MOCKER_CPP(&UBPublicJetty::SendByPublicJetty)
            .stubs()
            .will(returnValue(static_cast<NResult>(UB_PARAM_INVALID)))
            .then(returnValue(static_cast<NResult>(NN_OK)));
    MOCKER_CPP(&UBPublicJetty::PollingCompletion)
            .stubs()
            .will(returnValue(static_cast<NResult>(UB_CQ_EVENT_GET_TIMOUT)))
            .then(returnValue(static_cast<NResult>(NN_OK)));
    MOCKER_CPP(&UBPublicJetty::Receive).stubs()
        .will(returnValue(static_cast<NResult>(NN_ERROR)))
        .then(returnValue(static_cast<NResult>(NN_OK)));

    // PrePostReceiveOnSyncEp 失败
    driver->IncreaseRef();
    EXPECT_EQ(driver->CreateSyncEp(tmpJetty, nullptr, 0, outEp, pubJetty), NN_ERROR);

    // SendByPublicJetty 失败
    driver->IncreaseRef();
    EXPECT_EQ(driver->CreateSyncEp(tmpJetty, nullptr, 0, outEp, pubJetty), NN_ERROR);

    // PollingCompletion 失败
    driver->IncreaseRef();
    EXPECT_EQ(driver->CreateSyncEp(tmpJetty, nullptr, 0, outEp, pubJetty), NN_ERROR);

    // Receive 失败
    driver->IncreaseRef();
    EXPECT_EQ(driver->CreateSyncEp(tmpJetty, nullptr, 0, outEp, pubJetty), NN_ERROR);

    // Receive 成功，因为默认 serverAck = 1, 所以正常
    driver->IncreaseRef();
    EXPECT_EQ(driver->CreateSyncEp(tmpJetty, nullptr, 0, outEp, pubJetty), NN_OK);

    delete pubJetty;
    delete tmpJetty;
}

TEST_F(TestNetDriverUBPublicJetty, CreateSyncEpCheckServerAckFail)
{
    UBSHcomNetEndpointPtr outEp = nullptr;
    UBJetty *tmpJetty = new (std::nothrow) UBJetty("name", 0, nullptr, nullptr);
    ASSERT_NE(tmpJetty, nullptr);
    tmpJetty->IncreaseRef();

    UBPublicJetty *pubJetty = new (std::nothrow) UBPublicJetty("pubJetty", 0x1122, nullptr, nullptr);
    ASSERT_NE(pubJetty, nullptr);
    pubJetty->IncreaseRef();

    MOCKER_CPP(&NetDriverUBWithOob::PrePostReceiveOnSyncEp).stubs().will(returnValue(static_cast<NResult>(NN_OK)));
    MOCKER_CPP(&UBPublicJetty::SendByPublicJetty).stubs().will(returnValue(static_cast<NResult>(NN_OK)));
    MOCKER_CPP(&UBPublicJetty::PollingCompletion).stubs().will(returnValue(static_cast<NResult>(NN_OK)));
    MOCKER_CPP(&UBPublicJetty::Receive).stubs().will(invoke(&MockReceive<-1>));

    // Receive 成功，但是 serverAck = -1
    driver->IncreaseRef();
    EXPECT_EQ(driver->CreateSyncEp(tmpJetty, nullptr, 0, outEp, pubJetty), NN_ERROR);

    delete pubJetty;
    delete tmpJetty;
}

TEST_F(TestNetDriverUBPublicJetty, ConnectASyncEpByPublicJetty)
{
    std::string oobIp("1.2.3.4");
    std::string payload("hello");
    UBSHcomNetEndpointPtr outEp = nullptr;
    MOCKER_CPP(&NetDriverUBWithOob::PublicJettyConnect).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&NetDriverUBWithOob::CreatePublicJetty).stubs().with(outBound(jetty), any(), any())
        .will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::StartPublicJetty).stubs().then(returnValue(1));

    EXPECT_EQ(driver->ConnectASyncEpByPublicJetty(oobIp, 1, payload, outEp, 0, 0, 0), NN_ERROR);
    jetty->IncreaseRef();
    EXPECT_EQ(driver->ConnectASyncEpByPublicJetty(oobIp, 1, payload, outEp, 0, 0, 0), NN_ERROR);
    jetty->IncreaseRef();
    EXPECT_EQ(driver->ConnectASyncEpByPublicJetty(oobIp, 1, payload, outEp, 0, 0, 0), NN_ERROR);
}

TEST_F(TestNetDriverUBPublicJetty, PublicJettyNewConnectionCB)
{
    UBOpContextInfo ctx{};
    JettyConnHeader exchangeInfo{};

    driver->mPublicJetty = jetty;
    ctx.mrMemAddr = 0;
    EXPECT_EQ(driver->PublicJettyNewConnectionCB(&ctx), NN_ERROR);

    ctx.mrMemAddr = reinterpret_cast<uintptr_t>(&exchangeInfo);
    MOCKER_CPP(&NetDriverUBWithOob::CreatePublicJetty).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&NetDriverUBWithOob::ServerEstablishCtrlConn).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&NetDriverUBWithOob::CheckMagicAndProtocol).stubs().will(returnValue(1));

    EXPECT_EQ(driver->PublicJettyNewConnectionCB(&ctx), NN_ERROR);
    EXPECT_EQ(driver->PublicJettyNewConnectionCB(&ctx), NN_ERROR);
    EXPECT_EQ(driver->PublicJettyNewConnectionCB(&ctx), NN_ERROR);
    driver->mPublicJetty = nullptr;
}

TEST_F(TestNetDriverUBPublicJetty, ConnectSyncEpByPublicJettyFail)
{
    std::string oobIp("1.2.3.4");
    std::string payload("hello");
    UBSHcomNetEndpointPtr outEp = nullptr;

    MOCKER_CPP(&NetDriverUBWithOob::PublicJettyConnect).stubs().will(returnValue(1)).then(returnValue(0));

    MOCKER_CPP(&NetDriverUBWithOob::CreatePublicJetty).stubs().with(outBound(jetty), any(), any())
        .will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::StartPublicJetty).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&NetDriverUBWithOob::ClientEstablishConnOnReply)
            .stubs()
            .will(returnValue(NN_ERROR))
            .then(returnValue(NN_OK));
    MOCKER_CPP(&NetDriverUBWithOob::CreateSyncEp).stubs().will(returnValue(NN_ERROR)).then(returnValue(NN_OK));

    jetty->IncreaseRef();
    EXPECT_EQ(driver->ConnectSyncEpByPublicJetty(oobIp, 1, payload, outEp, 0, 0, 0), NN_ERROR);
    jetty->IncreaseRef();
    EXPECT_EQ(driver->ConnectSyncEpByPublicJetty(oobIp, 1, payload, outEp, 0, 0, 0), NN_ERROR);
    jetty->IncreaseRef();
    EXPECT_EQ(driver->ConnectSyncEpByPublicJetty(oobIp, 1, payload, outEp, 0, 0, 0), NN_ERROR);

    // ClientEstablishConnOnReply 失败
    jetty->IncreaseRef();
    EXPECT_EQ(driver->ConnectSyncEpByPublicJetty(oobIp, 1, payload, outEp, 0, 0, 0), NN_ERROR);

    // CreateSyncEp 失败
    EXPECT_EQ(driver->ConnectSyncEpByPublicJetty(oobIp, 1, payload, outEp, 0, 0, 0), NN_ERROR);
    EXPECT_EQ(driver->ConnectSyncEpByPublicJetty(oobIp, 1, payload, outEp, 0, 0, 0), NN_ERROR);
}

}
}
#endif
