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
#include "hcom.h"
#include "net_common.h"
#include "net_sock_driver_oob.h"
#include "net_security_rand.h"
#include "sock_validation.h"
#include "net_sock_sync_endpoint.h"

namespace ock {
namespace hcom {
class TestNetSockSyncEndpoint : public testing::Test {
public:
    TestNetSockSyncEndpoint();
    virtual void SetUp(void);
    virtual void TearDown(void);

    std::string name;
    std::string ip;
    uint16_t port;
    NetDriverSockWithOOB *mDriver = nullptr;
    SockWorker *mWorker = nullptr;
    Sock *sock = nullptr;
    UBSHcomNetWorkerIndex mWorkerIndex;
    NetSyncEndpointSock *ep = nullptr;
    UBSHcomNetTransRequest request;
    UBSHcomNetTransSglRequest sglRequest;
    UBSHcomNetTransSgeIov *iov = nullptr;
};

TestNetSockSyncEndpoint::TestNetSockSyncEndpoint() {}

void TestNetSockSyncEndpoint::SetUp()
{
    bool startOobSvr = true;
    UBSHcomNetDriverProtocol protocol = TCP;
    mDriver = new (std::nothrow) NetDriverSockWithOOB(name, startOobSvr, protocol, SOCK_TCP);
    mDriver->mStarted = true;

    SockWorkerOptions options;
    NetMemPoolFixedPtr memPool;
    NetMemPoolFixedPtr sglMemPool;
    NetMemPoolFixedPtr headerReqMemPool;
    UBSHcomNetWorkerIndex index;
    mWorker = new (std::nothrow) SockWorker(SOCK_TCP, name, index, memPool, sglMemPool, headerReqMemPool, options);
    ASSERT_NE(mWorker, nullptr);

    uint32_t sockId = NN_NO100;
    uint32_t mid = 0;
    SockOptions sockOptions;
    sock = new (std::nothrow) Sock(SOCK_TCP, name, sockId, -1, sockOptions);
    ASSERT_NE(sock, nullptr);

    ep = new (std::nothrow) NetSyncEndpointSock(sockId, sock, mDriver, index);
    ASSERT_NE(ep, nullptr);

    ep->mState.Set(NEP_ESTABLISHED);
    ep->mAllowedSize = NN_NO128;
    ep->mSegSize = NN_NO128;

    request.lAddress = reinterpret_cast<uintptr_t>(&mWorkerIndex);
    request.size = 1;
    iov = new (std::nothrow) UBSHcomNetTransSgeIov();
    sglRequest = UBSHcomNetTransSglRequest(iov, 1, 1);
}

void TestNetSockSyncEndpoint::TearDown()
{
    if (iov != nullptr) {
        delete iov;
        iov = nullptr;
    }
    if (ep != nullptr) {
        delete ep;
        ep = nullptr;
    }
    if (mWorker != nullptr) {
        delete mWorker;
        mWorker = nullptr;
    }

    GlobalMockObject::verify();
}

UBSHcomNetTransHeader mockHeader;
TEST_F(TestNetSockSyncEndpoint, SyncSetEpOptionTimeoutErr)
{
    UBSHcomEpOptions options{};
    options.sendTimeout = NN_NO2;
    ep->mDefaultTimeout = NN_NO1;
    NResult ret = ep->SetEpOption(options);
    EXPECT_EQ(ret, NN_ERROR);
    ep->mDefaultTimeout = -1;
}

TEST_F(TestNetSockSyncEndpoint, SyncSetEpOptionSetTimeoutErr)
{
    UBSHcomEpOptions options{};
    MOCKER_CPP(&Sock::SetBlockingSendTimeout).stubs().will(returnValue(1));
    NResult ret = ep->SetEpOption(options);
    EXPECT_EQ(ret, NN_ERROR);
}

TEST_F(TestNetSockSyncEndpoint, SyncPostSendSeqValidateStateFail)
{
    ep->mState.Set(NEP_BROKEN);
    int ret = ep->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));
    ep->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestNetSockSyncEndpoint, SyncPostSendSeqValidateBuffFail)
{
    request.upCtxSize = sizeof(SockOpContextInfo::upCtx) + 1;
    int ret = ep->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
    request.upCtxSize = 0;
}

TEST_F(TestNetSockSyncEndpoint, SyncPostSendOpInfoValidateStateFail)
{
    ep->mState.Set(NEP_BROKEN);
    UBSHcomNetTransOpInfo opInfo{};
    int ret = ep->PostSend(0, request, opInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));
    ep->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestNetSockSyncEndpoint, SyncPostSendOpInfoValidateBuffFail)
{
    request.size = 0;
    UBSHcomNetTransOpInfo opInfo{};
    int ret = ep->PostSend(0, request, opInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
    request.size = 1;
}

TEST_F(TestNetSockSyncEndpoint, SyncPostSendRawValidateStateFail)
{
    ep->mState.Set(NEP_BROKEN);
    int ret = ep->PostSendRaw(request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));
    ep->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestNetSockSyncEndpoint, SyncPostSendRawValidateBuffFail)
{
    request.size = 0;
    int ret = ep->PostSendRaw(request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
    request.size = 1;
}

TEST_F(TestNetSockSyncEndpoint, SyncPostSendRawSglValidateStateFail)
{
    ep->mState.Set(NEP_BROKEN);
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    int ret = ep->PostSendRawSgl(sglReq, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));
    ep->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestNetSockSyncEndpoint, SyncPostSendRawSglValidateBuffFail)
{
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    sglReq.upCtxSize = sizeof(SockOpContextInfo::upCtx) + 1;
    int ret = ep->PostSendRawSgl(sglReq, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
}

TEST_F(TestNetSockSyncEndpoint, SyncPostReadValidateStateFail)
{
    ep->mState.Set(NEP_BROKEN);
    int ret = ep->PostRead(request);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));
    ep->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestNetSockSyncEndpoint, SyncPostReadValidateBuffFail)
{
    request.size = 0;
    int ret = ep->PostRead(request);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
    request.size = 1;
}

TEST_F(TestNetSockSyncEndpoint, SyncPostReadOneSideValidateFail)
{
    MOCKER_CPP(&NetDriverSockWithOOB::ValidateMemoryRegion).stubs().will(returnValue(1));
    int ret = ep->PostRead(request);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_LKEY));
}

TEST_F(TestNetSockSyncEndpoint, SyncPostReadCtxInfoErr)
{
    SockOpContextInfo *ctxInfo = nullptr;
    MOCKER_CPP(&SockOpContextInfoPool::Get).stubs().will(returnValue(ctxInfo));
    MOCKER_CPP(&NetDriverSockWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));
    NResult ret = ep->PostRead(request);
    EXPECT_EQ(ret, SS_CTX_FULL);
}

TEST_F(TestNetSockSyncEndpoint, SyncPostReadCtxSglInfoErr)
{
    SockOpContextInfo *ctxInfo = new (std::nothrow) SockOpContextInfo();
    SockSglContextInfo *sglCtxInfo = nullptr;
    MOCKER_CPP(&SockOpContextInfoPool::Get).stubs().will(returnValue(ctxInfo));
    MOCKER_CPP(&SockSglContextInfoPool::Get).stubs().will(returnValue(sglCtxInfo));
    MOCKER_CPP(&SockOpContextInfoPool::Return).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&NetDriverSockWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));
    NResult ret = ep->PostRead(request);
    EXPECT_EQ(ret, SS_CTX_FULL);

    if (ctxInfo != nullptr) {
        delete ctxInfo;
        ctxInfo = nullptr;
    }
}

TEST_F(TestNetSockSyncEndpoint, SyncPostReadSglValidateStateFail)
{
    ep->mState.Set(NEP_BROKEN);
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    int ret = ep->PostRead(sglReq);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));
    ep->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestNetSockSyncEndpoint, SyncPostReadSglOneSideSglValidateFail)
{
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    sglReq.upCtxSize = sizeof(SockOpContextInfo::upCtx) + 1;
    int ret = ep->PostRead(sglReq);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
}

TEST_F(TestNetSockSyncEndpoint, SyncPostReadSglCtxInfoErr)
{
    SockOpContextInfo *ctxInfo = nullptr;
    MOCKER_CPP(&SockOpContextInfoPool::Get).stubs().will(returnValue(ctxInfo));
    MOCKER_CPP(&NetDriverSockWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));
    NResult ret = ep->PostRead(sglRequest);
    EXPECT_EQ(ret, SS_CTX_FULL);
}

TEST_F(TestNetSockSyncEndpoint, SyncPostReadSglCtxSglInfoErr)
{
    SockOpContextInfo *ctxInfo = new (std::nothrow) SockOpContextInfo();
    SockSglContextInfo *sglCtxInfo = nullptr;
    MOCKER_CPP(&SockOpContextInfoPool::Get).stubs().will(returnValue(ctxInfo));
    MOCKER_CPP(&SockSglContextInfoPool::Get).stubs().will(returnValue(sglCtxInfo));
    MOCKER_CPP(&SockOpContextInfoPool::Return).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&NetDriverSockWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));
    NResult ret = ep->PostRead(sglRequest);
    EXPECT_EQ(ret, SS_CTX_FULL);

    if (ctxInfo != nullptr) {
        delete ctxInfo;
        ctxInfo = nullptr;
    }
}

TEST_F(TestNetSockSyncEndpoint, SyncPostWriteValidateStateFail)
{
    ep->mState.Set(NEP_BROKEN);
    int ret = ep->PostWrite(request);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));
    ep->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestNetSockSyncEndpoint, SyncPostWriteValidateBuffFail)
{
    request.size = 0;
    int ret = ep->PostWrite(request);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
    request.size = 1;
}

TEST_F(TestNetSockSyncEndpoint, SyncPostWriteOneSideValidateFail)
{
    MOCKER_CPP(&NetDriverSockWithOOB::ValidateMemoryRegion).stubs().will(returnValue(1));
    int ret = ep->PostWrite(request);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_LKEY));
}

TEST_F(TestNetSockSyncEndpoint, SyncPostWriteCtxInfoErr)
{
    SockOpContextInfo *ctxInfo = nullptr;
    MOCKER_CPP(&SockOpContextInfoPool::Get).stubs().will(returnValue(ctxInfo));
    MOCKER_CPP(&NetDriverSockWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));
    NResult ret = ep->PostWrite(request);
    EXPECT_EQ(ret, SS_CTX_FULL);
}

TEST_F(TestNetSockSyncEndpoint, SyncPostWriteCtxSglInfoErr)
{
    SockOpContextInfo *ctxInfo = new (std::nothrow) SockOpContextInfo();
    SockSglContextInfo *sglCtxInfo = nullptr;
    MOCKER_CPP(&SockOpContextInfoPool::Get).stubs().will(returnValue(ctxInfo));
    MOCKER_CPP(&SockSglContextInfoPool::Get).stubs().will(returnValue(sglCtxInfo));
    MOCKER_CPP(&SockOpContextInfoPool::Return).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&NetDriverSockWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));
    NResult ret = ep->PostWrite(request);
    EXPECT_EQ(ret, SS_CTX_FULL);

    if (ctxInfo != nullptr) {
        delete ctxInfo;
        ctxInfo = nullptr;
    }
}

TEST_F(TestNetSockSyncEndpoint, SyncPostWriteSglValidateStateFail)
{
    ep->mState.Set(NEP_BROKEN);
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    int ret = ep->PostWrite(sglReq);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));
    ep->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestNetSockSyncEndpoint, SyncPostWriteSglOneSideSglValidateFail)
{
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    sglReq.upCtxSize = sizeof(SockOpContextInfo::upCtx) + 1;
    int ret = ep->PostWrite(sglReq);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
}

TEST_F(TestNetSockSyncEndpoint, SyncPostWriteSglCtxInfoErr)
{
    SockOpContextInfo *ctxInfo = nullptr;
    MOCKER_CPP(&SockOpContextInfoPool::Get).stubs().will(returnValue(ctxInfo));
    MOCKER_CPP(&NetDriverSockWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));
    NResult ret = ep->PostWrite(sglRequest);
    EXPECT_EQ(ret, SS_PARAM_INVALID);
}

TEST_F(TestNetSockSyncEndpoint, SyncPostWriteSglCtxSglInfoErr)
{
    SockOpContextInfo *ctxInfo = new (std::nothrow) SockOpContextInfo();
    SockSglContextInfo *sglCtxInfo = nullptr;
    MOCKER_CPP(&SockOpContextInfoPool::Get).stubs().will(returnValue(ctxInfo));
    MOCKER_CPP(&SockSglContextInfoPool::Get).stubs().will(returnValue(sglCtxInfo));
    MOCKER_CPP(&SockOpContextInfoPool::Return).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&NetDriverSockWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));
    NResult ret = ep->PostWrite(sglRequest);
    EXPECT_EQ(ret, SS_PARAM_INVALID);

    if (ctxInfo != nullptr) {
        delete ctxInfo;
        ctxInfo = nullptr;
    }
}

TEST_F(TestNetSockSyncEndpoint, SyncWaitCompletionFail)
{
    // param init
    int32_t timeout = 0;
    mockHeader.dataLength = 0;
    ep->mLastFlag = NTH_READ;
    ep->mRespCtx.mHeader = mockHeader;
    MOCKER_CPP(setsockopt).stubs().will(returnValue(0));
    MOCKER_CPP(::recv).stubs().will(returnValue(sizeof(UBSHcomNetTransHeader)));
    MOCKER_CPP(close).stubs().will(returnValue(0));

    NResult ret = ep->WaitCompletion(timeout);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestNetSockSyncEndpoint, SyncReceiveFailWithErrorDataLen)
{
    // param init
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx {};
    mockHeader.dataLength = 0;
    ep->mRespCtx.mHeader = mockHeader;
    MOCKER_CPP(setsockopt).stubs().will(returnValue(0));
    MOCKER_CPP(::recv).stubs().will(returnValue(sizeof(UBSHcomNetTransHeader)));
    MOCKER_CPP(close).stubs().will(returnValue(0));

    NResult ret = ep->Receive(timeout, ctx);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestNetSockSyncEndpoint, SyncReceiveFailWithOverDataLen)
{
    // param init
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx {};
    mockHeader.dataLength = NET_SGE_MAX_SIZE + 1;
    ep->mRespCtx.mHeader = mockHeader;
    MOCKER_CPP(setsockopt).stubs().will(returnValue(0));
    MOCKER_CPP(::recv).stubs().will(returnValue(sizeof(UBSHcomNetTransHeader)));
    MOCKER_CPP(close).stubs().will(returnValue(0));

    NResult ret = ep->Receive(timeout, ctx);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestNetSockSyncEndpoint, SyncReceiveFailWithInvalidCRC)
{
    // param init
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx {};
    mockHeader.dataLength = NN_NO1024;
    ep->mRespCtx.mHeader = mockHeader;
    MOCKER_CPP(setsockopt).stubs().will(returnValue(0));
    MOCKER_CPP(::recv).stubs().will(returnValue(sizeof(UBSHcomNetTransHeader)));
    MOCKER_CPP(close).stubs().will(returnValue(0));

    NResult ret = ep->Receive(timeout, ctx);
    EXPECT_EQ(ret, NN_VALIDATE_HEADER_CRC_INVALID);
}

TEST_F(TestNetSockSyncEndpoint, SyncReceiveFailWithInvalidSeqNo)
{
    // param init
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx {};
    mockHeader.dataLength = NN_NO1024;
    mockHeader.seqNo = 1;
    mockHeader.headerCrc = NetFunc::CalcHeaderCrc32(mockHeader);
    ep->mRespCtx.mHeader = mockHeader;
    MOCKER_CPP(setsockopt).stubs().will(returnValue(0));
    MOCKER_CPP(::recv).stubs().will(returnValue(sizeof(UBSHcomNetTransHeader)));
    MOCKER_CPP(close).stubs().will(returnValue(0));

    NResult ret = ep->Receive(timeout, ctx);
    EXPECT_EQ(ret, NN_SEQ_NO_NOT_MATCHED);
}

TEST_F(TestNetSockSyncEndpoint, SyncGetSendQueueCount)
{
    uint32_t ret = ep->GetSendQueueCount();
    EXPECT_EQ(ret, 0);
}

TEST_F(TestNetSockSyncEndpoint, SyncPeerIpAndPortErr)
{
    ep->mSock = nullptr;
    std::string ret = ep->PeerIpAndPort();
    EXPECT_EQ(ret, CONST_EMPTY_STRING);
}

TEST_F(TestNetSockSyncEndpoint, SyncUdsNameErr)
{
    std::string ret = ep->UdsName();
    EXPECT_EQ(ret, CONST_EMPTY_STRING);
}

TEST_F(TestNetSockSyncEndpoint, SyncGetRemoteUdsIdInfo)
{
    ep->mState.Set(NEP_ESTABLISHED);
    ep->mDriver->mStartOobSvr = true;
    ep->mDriver->mOptions.oobType = NET_OOB_UDS;
    UBSHcomNetUdsIdInfo sockIdInfo{};
    ep->mRemoteUdsIdInfo.gid = NN_NO1024;
    ep->mRemoteUdsIdInfo.pid = NN_NO1024;
    ep->mRemoteUdsIdInfo.uid = NN_NO1024;
    NResult ret = ep->GetRemoteUdsIdInfo(sockIdInfo);
    EXPECT_EQ(ret, NN_OK);

    ep->mState.Set(NEP_BROKEN);
    ep->mRemoteUdsIdInfo.gid = 0;
    ep->mRemoteUdsIdInfo.pid = 0;
    ep->mRemoteUdsIdInfo.uid = 0;
    ret = ep->GetRemoteUdsIdInfo(sockIdInfo);
    EXPECT_EQ(ret, NN_ERROR);

    ep->mDriver->mOptions.oobType = NET_OOB_TCP;
    ret = ep->GetRemoteUdsIdInfo(sockIdInfo);
    EXPECT_EQ(ret, NN_UDS_ID_INFO_NOT_SUPPORT);

    ep->mDriver->mStartOobSvr = false;
    ret = ep->GetRemoteUdsIdInfo(sockIdInfo);
    EXPECT_EQ(ret, NN_UDS_ID_INFO_NOT_SUPPORT);
}

TEST_F(TestNetSockSyncEndpoint, SyncGetPeerIpPort)
{
    std::string ip;
    uint16_t port;
    ep->mSock->mPeerIpPort = "";
    bool ret = ep->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, false);

    ep->mSock->mPeerIpPort = "1.2.3.4";
    ret = ep->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, false);

    ep->mSock->mPeerIpPort = "1.2.3.4:test";
    ret = ep->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, false);

    ep->mSock->mPeerIpPort = "1.2.3.4:0";
    ret = ep->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, false);

    ep->mSock->mPeerIpPort = "1.2.3.4:16";
    ret = ep->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, true);

    ep->mSock = nullptr;
    ret = ep->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, false);
}

TEST_F(TestNetSockSyncEndpoint, SyncGetFinishTime)
{
    ep->mDefaultTimeout = 0;
    uint64_t ret = ep->GetFinishTime();
    EXPECT_EQ(ret, 0);
}

TEST_F(TestNetSockSyncEndpoint, GetRemoteUdsIdInfoFail2)
{
    int ret;
    UBSHcomNetUdsIdInfo sockIdInfo{};
 
    ep->mState.Set(NEP_ESTABLISHED);
    mDriver->mStartOobSvr = true;
    mDriver->mOptions.oobType = NET_OOB_TCP;
    ret = ep->GetRemoteUdsIdInfo(sockIdInfo);
    EXPECT_EQ(ret, NN_UDS_ID_INFO_NOT_SUPPORT);
}

TEST_F(TestNetSockSyncEndpoint, Connect)
{
    int ret;
    std::string payload{};
    UBSHcomNetEndpointPtr ep;
    std::string badUrl = "unknown://127.0.0.1:9981";
    std::string serverUrl = "tcp://127.0.0.1:9981";
    mDriver->mInited = true;
    mDriver->mStarted = true;
    mDriver->mWorkerGroups.emplace_back(std::make_pair(1, 1));

    MOCKER_CPP(&NetDriverSockWithOOB::Connect,
        NResult(NetDriverSockWithOOB::*)(const OOBTCPClientPtr &, const std::string &, UBSHcomNetEndpointPtr &, uint8_t,
        uint8_t, uint64_t)).stubs().will(returnValue(1));
    MOCKER_CPP(&NetDriverSockWithOOB::ConnectSyncEp).stubs().will(returnValue(0));
    ret = mDriver->Connect(badUrl, payload, ep, 0, 0, 0, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    mDriver->mEnableTls = true;
    ret = mDriver->Connect(serverUrl, payload, ep, 0, 0, 0, 0);
    EXPECT_EQ(ret, 1);

    mDriver->mEnableTls = false;
    ret = mDriver->Connect(serverUrl, payload, ep, NET_EP_SELF_POLLING, 0, 0, 0);
    EXPECT_EQ(ret, 0);
}
}
}