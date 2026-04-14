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
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <dirent.h>
#include <iostream>
#include "net_sock_driver_oob.h"
#include "net_sock_async_endpoint.h"
#include "net_oob_secure.h"

namespace ock {
namespace hcom {
constexpr uint16_t K_MOCK_OOB_PORT = 9981;

class TestNetSockDriverOob : public testing::Test {
public:
    std::string name;
    NetDriverSockWithOOB *mDriver = nullptr;
    Sock *sock = nullptr;
    UBSHcomNetDriverOptions option{};

    TestNetSockDriverOob();
    virtual void SetUp(void);
    virtual void TearDown(void);
};

TestNetSockDriverOob::TestNetSockDriverOob() {}

void TestNetSockDriverOob::SetUp()
{
    bool startOobSvr = true;
    UBSHcomNetDriverProtocol protocol = TCP;
    mDriver = new (std::nothrow) NetDriverSockWithOOB(name, startOobSvr, protocol, SOCK_TCP);
    mDriver->mStarted = true;

    SockOptions sockOptions;
    sock = new (std::nothrow) Sock(SOCK_TCP, name, NN_NO100, -1, sockOptions);
    ASSERT_NE(sock, nullptr);
}

void TestNetSockDriverOob::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(TestNetSockDriverOob, InitializeNetOutLoggerInstanceErr)
{
    mDriver->mInited = false;
    UBSHcomNetOutLogger *logger = nullptr;
    MOCKER_CPP(&UBSHcomNetDriverOptions::ValidateCommonOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverSockWithOOB::ValidateOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&UBSHcomNetOutLogger::Instance).stubs().will(returnValue(logger));
    EXPECT_EQ(mDriver->Initialize(option), NN_NOT_INITIALIZED);
}

TEST_F(TestNetSockDriverOob, ValidateOptionsOobTypeErr)
{
    mDriver->mOptions.oobType = NET_OOB_UB;
    EXPECT_EQ(mDriver->ValidateOptionsOobType(), NN_INVALID_PARAM);
}

TEST_F(TestNetSockDriverOob, InitializeLoadOpensslErr)
{
    mDriver->mInited = false;
    MOCKER_CPP(&UBSHcomNetDriverOptions::ValidateCommonOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverSockWithOOB::ValidateOptions).stubs().will(returnValue(0));
    option.enableTls = true;
    MOCKER_CPP(HcomSsl::Load).stubs().will(returnValue(1));
    EXPECT_EQ(mDriver->Initialize(option), NN_NOT_INITIALIZED);
    option.enableTls = false;
}

TEST_F(TestNetSockDriverOob, InitializeCreateWorkerResourceErr)
{
    mDriver->mInited = false;
    MOCKER_CPP(&UBSHcomNetDriverOptions::ValidateCommonOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverSockWithOOB::ValidateOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverSockWithOOB::CreateWorkerResource).stubs().will(returnValue(1));
    MOCKER_CPP(&NetDriverSockWithOOB::UnInitializeInner).stubs().will(ignoreReturnValue());
    EXPECT_EQ(mDriver->Initialize(option), 1);
}

TEST_F(TestNetSockDriverOob, InitializeCreateClientLBErr)
{
    mDriver->mInited = false;
    MOCKER_CPP(&UBSHcomNetDriverOptions::ValidateCommonOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverSockWithOOB::ValidateOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverSockWithOOB::CreateWorkerResource).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverSockWithOOB::CreateWorkers).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverSockWithOOB::CreateClientLB).stubs().will(returnValue(1));
    MOCKER_CPP(&NetDriverSockWithOOB::UnInitializeInner).stubs().will(ignoreReturnValue());
    EXPECT_EQ(mDriver->Initialize(option), 1);
}

TEST_F(TestNetSockDriverOob, InitializeCreateListenersErr)
{
    mDriver->mInited = false;
    mDriver->mStartOobSvr = true;
    MOCKER_CPP(&UBSHcomNetDriverOptions::ValidateCommonOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverSockWithOOB::ValidateOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverSockWithOOB::CreateWorkerResource).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverSockWithOOB::CreateWorkers).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverSockWithOOB::CreateClientLB).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverSockWithOOB::CreateListeners).stubs().will(returnValue(1));
    MOCKER_CPP(&NetDriverSockWithOOB::UnInitializeInner).stubs().will(ignoreReturnValue());
    EXPECT_EQ(mDriver->Initialize(option), 1);
}

TEST_F(TestNetSockDriverOob, UnInitializeStartedErr)
{
    mDriver->mInited = false;
    mDriver->mStarted = true;
    EXPECT_NO_FATAL_FAILURE(mDriver->UnInitialize());
}

TEST_F(TestNetSockDriverOob, ValidateOptionsArrErr)
{
    MOCKER_CPP(ValidateArrayOptions).stubs().will(returnValue(false));
    EXPECT_EQ(mDriver->ValidateOptions(), NN_INVALID_PARAM);
}

TEST_F(TestNetSockDriverOob, ValidateOptionsParamErr)
{
    MOCKER_CPP(ValidateArrayOptions).stubs().will(returnValue(true));
    mDriver->mOptions.tcpSendBufSize = NN_NO10000;
    EXPECT_EQ(mDriver->ValidateOptions(), NN_INVALID_PARAM);

    mDriver->mOptions.tcpSendBufSize = NN_NO1024;
    mDriver->mOptions.tcpReceiveBufSize = NN_NO10000;
    EXPECT_EQ(mDriver->ValidateOptions(), NN_INVALID_PARAM);
}

TEST_F(TestNetSockDriverOob, ValidateOptionsErrTwo)
{
    MOCKER_CPP(ValidateArrayOptions).stubs().will(returnValue(true));
    mDriver->mSockType = SOCK_UDS;
    MOCKER_CPP(&UBSHcomNetDriver::ValidateAndParseOobPortRange).stubs().will(returnValue(1)).then(returnValue(0));
    EXPECT_EQ(mDriver->ValidateOptions(), NN_INVALID_PARAM);

    MOCKER_CPP(&UBSHcomNetDriver::ValidateOptionsOobType).stubs().will(returnValue(1));
    EXPECT_EQ(mDriver->ValidateOptions(), NN_INVALID_PARAM);
}

TEST_F(TestNetSockDriverOob, CreateWorkerResourceOpCtxMemPoolErr)
{
    MOCKER_CPP(&NetDriverSockWithOOB::CreateOpCtxMemPool).stubs().will(returnValue(1));
    EXPECT_EQ(mDriver->CreateWorkerResource(), 1);
}

TEST_F(TestNetSockDriverOob, CreateWorkerResourceSglCtxMemPoolErr)
{
    MOCKER_CPP(&NetDriverSockWithOOB::CreateOpCtxMemPool).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverSockWithOOB::CreateSglCtxMemPool).stubs().will(returnValue(1));
    EXPECT_EQ(mDriver->CreateWorkerResource(), 1);
}

TEST_F(TestNetSockDriverOob, CreateWorkerResourceHeaderReqMemPoolErr)
{
    mDriver->mOptions.tcpSendZCopy = true;
    MOCKER_CPP(&NetDriverSockWithOOB::CreateOpCtxMemPool).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverSockWithOOB::CreateSglCtxMemPool).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverSockWithOOB::CreateHeaderReqMemPool).stubs().will(returnValue(1));
    EXPECT_EQ(mDriver->CreateWorkerResource(), 1);
}

TEST_F(TestNetSockDriverOob, CreateWorkerResourceSendMrErr)
{
    mDriver->mOptions.tcpSendZCopy = false;
    MOCKER_CPP(&NetDriverSockWithOOB::CreateOpCtxMemPool).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverSockWithOOB::CreateSglCtxMemPool).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverSockWithOOB::CreateSendMr).stubs().will(returnValue(1));
    EXPECT_EQ(mDriver->CreateWorkerResource(), 1);
}

TEST_F(TestNetSockDriverOob, CreateOpCtxMemPoolErr)
{
    MOCKER_CPP(&NetMemPoolFixed::Initialize).stubs().will(returnValue(1));
    EXPECT_EQ(mDriver->CreateOpCtxMemPool(), 1);
}

TEST_F(TestNetSockDriverOob, CreateOpCtxMemPoolNullErr)
{
    NetMemPoolFixed *testOpCtxMemPool = nullptr;
    MOCKER_CPP(&NetRef<NetMemPoolFixed>::Get).stubs().will(returnValue(testOpCtxMemPool));
    EXPECT_EQ(mDriver->CreateOpCtxMemPool(), NN_INVALID_PARAM);
}

TEST_F(TestNetSockDriverOob, CreateSglCtxMemPoolErr)
{
    MOCKER_CPP(&NetMemPoolFixed::Initialize).stubs().will(returnValue(1));
    EXPECT_EQ(mDriver->CreateSglCtxMemPool(), 1);
}

TEST_F(TestNetSockDriverOob, CreateSglCtxMemPoolNullErr)
{
    NetMemPoolFixed *testSglCtxMemPool = nullptr;
    MOCKER_CPP(&NetRef<NetMemPoolFixed>::Get).stubs().will(returnValue(testSglCtxMemPool));
    EXPECT_EQ(mDriver->CreateSglCtxMemPool(), NN_INVALID_PARAM);
}

TEST_F(TestNetSockDriverOob, CreateHeaderReqMemPoolErr)
{
    MOCKER_CPP(&NetMemPoolFixed::Initialize).stubs().will(returnValue(1));
    EXPECT_EQ(mDriver->CreateHeaderReqMemPool(), 1);
}

TEST_F(TestNetSockDriverOob, CreateHeaderReqMemPoolNullErr)
{
    NetMemPoolFixed *testHeaderReqMemPool = nullptr;
    MOCKER_CPP(&NetRef<NetMemPoolFixed>::Get).stubs().will(returnValue(testHeaderReqMemPool));
    EXPECT_EQ(mDriver->CreateHeaderReqMemPool(), NN_INVALID_PARAM);
}

TEST_F(TestNetSockDriverOob, CreateHeaderReqMemPoolSuccess)
{
    MOCKER_CPP(&NetMemPoolFixed::Initialize).stubs().will(returnValue(0));
    EXPECT_EQ(mDriver->CreateHeaderReqMemPool(), 0);
    mDriver->mHeaderReqMemPool.Set(nullptr);
}

TEST_F(TestNetSockDriverOob, CreateSendMrErr)
{
    MOCKER_CPP(&NormalMemoryRegionFixedBuffer::Create).stubs().will(returnValue(1));
    EXPECT_EQ(mDriver->CreateSendMr(), 1);
}

TEST_F(TestNetSockDriverOob, CreateMemoryRegionErr)
{
    UBSHcomNetMemoryRegionPtr mr = nullptr;
    mDriver->mInited = true;
    MOCKER_CPP(NormalMemoryRegion::Create,
        NResult(const std::string &, uint64_t, NormalMemoryRegion *&))
        .stubs().will(returnValue(1));
    EXPECT_EQ(mDriver->CreateMemoryRegion(NN_NO8, mr), 1);
}

TEST_F(TestNetSockDriverOob, CreateMemoryRegionInitErr)
{
    UBSHcomNetMemoryRegionPtr mr = nullptr;
    mDriver->mInited = true;
    auto tmpBuf = new (std::nothrow) NormalMemoryRegion(name, false, 0, NN_NO8);
    MOCKER(NormalMemoryRegion::Create, NResult(const std::string &, uint64_t, NormalMemoryRegion *&)).stubs()
        .with(any(), any(), outBound(tmpBuf))
        .will(returnValue(0));
    MOCKER_CPP_VIRTUAL(*tmpBuf, &NormalMemoryRegion::Initialize).stubs().will(returnValue(1));
    EXPECT_EQ(mDriver->CreateMemoryRegion(NN_NO8, mr), 1);
}

TEST_F(TestNetSockDriverOob, CreateMemoryRegionRegisterErr)
{
    UBSHcomNetMemoryRegionPtr mr = nullptr;
    mDriver->mInited = true;
    auto tmpBuf = new (std::nothrow) NormalMemoryRegion(name, false, 0, NN_NO8);
    MOCKER(NormalMemoryRegion::Create, NResult(const std::string &, uint64_t, NormalMemoryRegion *&)).stubs()
        .with(any(), any(), outBound(tmpBuf))
        .will(returnValue(0));
    MOCKER_CPP_VIRTUAL(*tmpBuf, &NormalMemoryRegion::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(MemoryRegionChecker::Register).stubs().will(returnValue(1));
    EXPECT_EQ(mDriver->CreateMemoryRegion(NN_NO8, mr), 1);
}

TEST_F(TestNetSockDriverOob, CreateMemoryRegionAddressErr)
{
    UBSHcomNetMemoryRegionPtr mr = nullptr;
    uintptr_t address = 1;
    mDriver->mInited = true;
    MOCKER_CPP(NormalMemoryRegion::Create,
        NResult(const std::string &, uintptr_t, uint64_t, NormalMemoryRegion *&))
        .stubs().will(returnValue(1));
    EXPECT_EQ(mDriver->CreateMemoryRegion(address, NN_NO8, mr), 1);
}

TEST_F(TestNetSockDriverOob, CreateMemoryRegionAddressInitErr)
{
    UBSHcomNetMemoryRegionPtr mr = nullptr;
    uintptr_t address = 1;
    mDriver->mInited = true;
    auto tmpBuf = new (std::nothrow) NormalMemoryRegion(name, true, address, NN_NO8);
    MOCKER(NormalMemoryRegion::Create, NResult(const std::string &, uintptr_t, uint64_t, NormalMemoryRegion *&)).stubs()
        .with(any(), any(), any(), outBound(tmpBuf))
        .will(returnValue(0));
    MOCKER_CPP_VIRTUAL(*tmpBuf, &NormalMemoryRegion::Initialize).stubs().will(returnValue(1));
    EXPECT_EQ(mDriver->CreateMemoryRegion(address, NN_NO8, mr), 1);
}

TEST_F(TestNetSockDriverOob, CreateMemoryRegionAddressRegisterErr)
{
    UBSHcomNetMemoryRegionPtr mr = nullptr;
    uintptr_t address = 1;
    mDriver->mInited = true;
    auto tmpBuf = new (std::nothrow) NormalMemoryRegion(name, true, address, NN_NO8);
    MOCKER(NormalMemoryRegion::Create, NResult(const std::string &, uintptr_t, uint64_t, NormalMemoryRegion *&)).stubs()
        .with(any(), any(), any(), outBound(tmpBuf))
        .will(returnValue(0));
    MOCKER_CPP_VIRTUAL(*tmpBuf, &NormalMemoryRegion::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(MemoryRegionChecker::Register).stubs().will(returnValue(1));
    EXPECT_EQ(mDriver->CreateMemoryRegion(address, NN_NO8, mr), 1);
}

TEST_F(TestNetSockDriverOob, CreateMemoryRegionMemidErr)
{
    UBSHcomNetMemoryRegionPtr mr = nullptr;
    unsigned long memid = 1;
    EXPECT_EQ(mDriver->CreateMemoryRegion(0, mr, memid), NN_ERROR);
}

TEST_F(TestNetSockDriverOob, MultiRailNewConnectionErr)
{
    OOBTCPConnection conn(-1);
    EXPECT_EQ(mDriver->MultiRailNewConnection(conn), NN_ERROR);
}

TEST_F(TestNetSockDriverOob, HandleSockRealConnectWithDupId)
{
    SockOpContextInfo ctx {};
    ctx.sock = sock;
    sock->IncreaseRef();

    UBSHcomNetWorkerIndex index;
    NetAsyncEndpointSock *ep = new (std::nothrow) NetAsyncEndpointSock(sock->mId, sock, mDriver, index);
    ASSERT_NE(ep, nullptr);
    mDriver->mEndPoints.emplace(sock->mId, ep);
    ep->IncreaseRef();

    auto ret = mDriver->HandleSockRealConnect(ctx);
    EXPECT_EQ(ret, NN_ERROR);

    mDriver->mEndPoints.erase(sock->mId);
    ep->DecreaseRef();
    sock->DecreaseRef();
}

TEST_F(TestNetSockDriverOob, HandleSockRealConnectWithOverPayload)
{
    SockOpContextInfo ctx {};
    ctx.sock = sock;
    sock->IncreaseRef();
    UBSHcomNetTransHeader mockHeader {};
    mockHeader.dataLength = NN_NO2048;
    ctx.header = &mockHeader;
    mDriver->mOptions.magic = 0;
    mDriver->mEnableTls = false;

    SockWorkerOptions options;
    NetMemPoolFixedPtr memPool;
    NetMemPoolFixedPtr sglMemPool;
    NetMemPoolFixedPtr headerReqMemPool;
    UBSHcomNetWorkerIndex index;
    SockWorker *worker =
        new (std::nothrow) SockWorker(SOCK_TCP, name, index, memPool, sglMemPool, headerReqMemPool, options);
    ASSERT_NE(worker, nullptr);
    ctx.sock->UpContext1(reinterpret_cast<uint64_t>(worker));
    worker->IncreaseRef();

    auto ret = mDriver->HandleSockRealConnect(ctx);
    EXPECT_EQ(ret, NN_EP_CLOSE);

    sock->DecreaseRef();
    worker->DecreaseRef();
}

TEST_F(TestNetSockDriverOob, HandleSockErrorWithErrUpCtx)
{
    SockWorkerOptions options;
    NetMemPoolFixedPtr memPool;
    NetMemPoolFixedPtr sglMemPool;
    NetMemPoolFixedPtr headerReqMemPool;
    UBSHcomNetWorkerIndex index;
    SockWorker *worker =
            new (std::nothrow) SockWorker(SOCK_TCP, name, index, memPool, sglMemPool, headerReqMemPool, options);
    ASSERT_NE(worker, nullptr);
    sock->UpContext1(reinterpret_cast<uint64_t>(worker));
    worker->IncreaseRef();

    auto ret = mDriver->HandleSockError(sock);
    EXPECT_EQ(ret, NN_ERROR);
    worker->DecreaseRef();
}


TEST_F(TestNetSockDriverOob, HandleReqPostedWithNullCtx)
{
    auto ret = mDriver->HandleReqPosted(nullptr);
    EXPECT_EQ(ret, NN_ERROR);
}

TEST_F(TestNetSockDriverOob, HandleNewOobConnSecProcessFail)
{
    OOBTCPConnection conn(-1);
    conn.SetIpAndPort("127.0.0.1", K_MOCK_OOB_PORT);
    conn.ListenPort(K_MOCK_OOB_PORT);

    MOCKER_CPP(OOBSecureProcess::SecProcessInOOBServer,
        NResult(const UBSHcomNetDriverEndpointSecInfoProvider &, const UBSHcomNetDriverEndpointSecInfoValidator &,
        OOBTCPConnection &, const std::string &, UBSHcomNetDriverSecType)).stubs().will(returnValue(1));

    auto ret = mDriver->HandleNewOobConn(conn);
    EXPECT_EQ(ret, NN_OOB_SEC_PROCESS_ERROR);
}

TEST_F(TestNetSockDriverOob, HandleNewOobConnCompareEpNumFail)
{
    OOBTCPConnection conn(-1);
    conn.SetIpAndPort("127.0.0.1", K_MOCK_OOB_PORT);
    conn.ListenPort(K_MOCK_OOB_PORT);

    MOCKER_CPP(OOBSecureProcess::SecProcessInOOBServer,
        NResult(const UBSHcomNetDriverEndpointSecInfoProvider &, const UBSHcomNetDriverEndpointSecInfoValidator &,
        OOBTCPConnection &, const std::string &, UBSHcomNetDriverSecType)).stubs().will(returnValue(0));
    MOCKER_CPP(OOBSecureProcess::SecProcessCompareEpNum,
        NResult(uint32_t, uint32_t, const std::string &, const std::vector<NetOOBServer *> &))
        .stubs()
        .will(returnValue(1));

    auto ret = mDriver->HandleNewOobConn(conn);
    EXPECT_EQ(ret, NN_OOB_SEC_PROCESS_ERROR);
}

TEST_F(TestNetSockDriverOob, OneSideDoneWithNullUpContext1)
{
    SockOpContextInfo ctx {};
    ctx.sock = sock;
    sock->UpContext(1);
    sock->UpContext1(0);

    auto ret = mDriver->OneSideDone(&ctx);
    EXPECT_EQ(ret, NN_ERROR);
}

TEST_F(TestNetSockDriverOob, HandleNewOobConnWithNullLb)
{
    OOBTCPConnection conn(-1);
    conn.SetIpAndPort("127.0.0.1", K_MOCK_OOB_PORT);
    conn.ListenPort(K_MOCK_OOB_PORT);

    OOBTCPConnection *tcpConn = &conn;
    MOCKER_CPP_VIRTUAL(*tcpConn, &OOBTCPConnection::Receive).stubs().will(returnValue(static_cast<int>(NN_OK)));
    MOCKER_CPP(OOBSecureProcess::SecProcessInOOBServer,
        NResult(const UBSHcomNetDriverEndpointSecInfoProvider &, const UBSHcomNetDriverEndpointSecInfoValidator &,
        OOBTCPConnection &, const std::string &, UBSHcomNetDriverSecType)).stubs().will(returnValue(0));
    MOCKER_CPP(OOBSecureProcess::SecProcessCompareEpNum,
        NResult(uint32_t, uint32_t, const std::string &, const std::vector<NetOOBServer *> &))
        .stubs()
        .will(returnValue(0));
    MOCKER_CPP(OOBSecureProcess::SecCheckConnectionHeader,
        NResult(const ConnectHeader &, const UBSHcomNetDriverOptions &, const bool &, const UBSHcomNetDriverProtocol &,
        const uint32_t &, const uint32_t &, ConnRespWithUId &)).stubs().will(returnValue(0));

    auto ret = mDriver->HandleNewOobConn(conn);
    EXPECT_EQ(ret, NN_ERROR);
}
TEST_F(TestNetSockDriverOob, HandleNewOobConnWithNullWorker)
{
    OOBTCPConnection conn(-1);
    conn.SetIpAndPort("127.0.0.1", K_MOCK_OOB_PORT);
    conn.ListenPort(K_MOCK_OOB_PORT);

    NetWorkerLBPtr lb = new (std::nothrow) NetWorkerLB("lb", NET_ROUND_ROBIN, UINT16_MAX);
    ASSERT_NE(lb.Get(), nullptr);
    std::vector<std::pair<uint16_t, uint16_t>> groups = {{0, 1}};
    ASSERT_EQ(lb->AddWorkerGroups(groups), NN_OK);
    conn.LoadBalancer(lb);

    mDriver->mWorkers.clear();
    mDriver->mWorkers.push_back(nullptr);

    OOBTCPConnection *tcpConn = &conn;
    MOCKER_CPP_VIRTUAL(*tcpConn, &OOBTCPConnection::Receive).stubs().will(returnValue(static_cast<int>(NN_OK)));
    MOCKER_CPP_VIRTUAL(*tcpConn, &OOBTCPConnection::Send).stubs().will(returnValue(static_cast<int>(NN_OK)));
    MOCKER_CPP(OOBSecureProcess::SecProcessInOOBServer,
        NResult(const UBSHcomNetDriverEndpointSecInfoProvider &, const UBSHcomNetDriverEndpointSecInfoValidator &,
        OOBTCPConnection &, const std::string &, UBSHcomNetDriverSecType)).stubs().will(returnValue(0));
    MOCKER_CPP(OOBSecureProcess::SecProcessCompareEpNum,
        NResult(uint32_t, uint32_t, const std::string &, const std::vector<NetOOBServer *> &))
        .stubs()
        .will(returnValue(0));
    MOCKER_CPP(OOBSecureProcess::SecCheckConnectionHeader,
        NResult(const ConnectHeader &, const UBSHcomNetDriverOptions &, const bool &, const UBSHcomNetDriverProtocol &,
        const uint32_t &, const uint32_t &, ConnRespWithUId &)).stubs().will(returnValue(0));

    auto ret = mDriver->HandleNewOobConn(conn);
    EXPECT_EQ(ret, NN_ERROR);
}

TEST_F(TestNetSockDriverOob, HandleReqPostedWithNullUpContext)
{
    SockOpContextInfo ctx {};
    ctx.sock = sock;
    sock->UpContext(0);

    auto ret = mDriver->HandleReqPosted(&ctx);
    EXPECT_EQ(ret, NN_ERROR);
}

TEST_F(TestNetSockDriverOob, OneSideDoneWithNullUpContext)
{
    SockOpContextInfo ctx {};
    ctx.sock = sock;
    sock->UpContext(0);

    auto ret = mDriver->OneSideDone(&ctx);
    EXPECT_EQ(ret, NN_ERROR);
}
TEST_F(TestNetSockDriverOob, GetConnRespOther)
{
    SockType t = SOCK_UDS_TCP;
    EXPECT_EQ(mDriver->GetConnResp(t), OK);
}
}
}
