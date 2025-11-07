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
#include "sock_validation.h"
#include "net_sock_async_endpoint.h"

namespace ock {
namespace hcom {
class TestNetSockAsyncEndpoint : public testing::Test {
public:
    TestNetSockAsyncEndpoint();
    virtual void SetUp(void);
    virtual void TearDown(void);

    std::string name;
    std::string ip;
    uint16_t port;
    NetDriverSockWithOOB *mDriver = nullptr;
    SockWorker *mWorker = nullptr;
    Sock *sock = nullptr;
    UBSHcomNetWorkerIndex mWorkerIndex;
    NetAsyncEndpointSock *ep = nullptr;
    UBSHcomNetTransRequest request;
};

TestNetSockAsyncEndpoint::TestNetSockAsyncEndpoint() {}

void TestNetSockAsyncEndpoint::SetUp()
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

    ep = new (std::nothrow) NetAsyncEndpointSock(sockId, sock, mDriver, index);
    ASSERT_NE(ep, nullptr);

    ep->mState.Set(NEP_ESTABLISHED);
    ep->mAllowedSize = NN_NO128;
    ep->mSegSize = NN_NO128;

    request.lAddress = reinterpret_cast<uintptr_t>(&mWorkerIndex);
    request.size = 1;
}

void TestNetSockAsyncEndpoint::TearDown()
{
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

static UBSHcomNetTransHeader mockMrBuf{};
static bool MockGetFreeBuffer(uintptr_t &mrBufAddress)
{
    mrBufAddress = reinterpret_cast<uintptr_t>(&mockMrBuf);
    return true;
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPeerIpAndPortErr)
{
    ep->mSock = nullptr;
    std::string ret = ep->PeerIpAndPort();
    EXPECT_EQ(ret, CONST_EMPTY_STRING);
}

TEST_F(TestNetSockAsyncEndpoint, AsyncUdsNameErr)
{
    std::string ret = ep->UdsName();
    EXPECT_EQ(ret, CONST_EMPTY_STRING);
}

TEST_F(TestNetSockAsyncEndpoint, AsyncGetRemoteUdsIdInfo)
{
    ep->mState.Set(NEP_BROKEN);
    ep->mDriver->mStartOobSvr = true;
    ep->mDriver->mOptions.oobType = NET_OOB_UDS;
    ep->mRemoteUdsIdInfo.gid = NN_NO1024;
    ep->mRemoteUdsIdInfo.pid = NN_NO1024;
    ep->mRemoteUdsIdInfo.uid = NN_NO1024;
    UBSHcomNetUdsIdInfo sockIdInfo{};
    NResult ret = ep->GetRemoteUdsIdInfo(sockIdInfo);
    EXPECT_EQ(ret, NN_OK);

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

TEST_F(TestNetSockAsyncEndpoint, AsyncGetPeerIpPort)
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

TEST_F(TestNetSockAsyncEndpoint, AsyncSetEpOptionReSetErr)
{
    UBSHcomEpOptions options{};
    options.tcpBlockingIo = false;
    NResult ret = ep->SetEpOption(options);
    EXPECT_EQ(ret, NN_OK);
}

TEST_F(TestNetSockAsyncEndpoint, AsyncSetEpOptionTimeoutErr)
{
    UBSHcomEpOptions options{};
    options.tcpBlockingIo = true;
    options.sendTimeout = NN_NO2;
    ep->mDefaultTimeout = NN_NO1;
    NResult ret = ep->SetEpOption(options);
    EXPECT_EQ(ret, NN_ERROR);
    ep->mDefaultTimeout = -1;
}

TEST_F(TestNetSockAsyncEndpoint, AsyncSetEpOptionSetTimeoutErr)
{
    UBSHcomEpOptions options{};
    options.tcpBlockingIo = true;
    MOCKER_CPP(&Sock::SetBlockingIo, SResult(Sock::*)(UBSHcomEpOptions &)).stubs().will(returnValue(1));
    NResult ret = ep->SetEpOption(options);
    EXPECT_EQ(ret, NN_ERROR);
}

TEST_F(TestNetSockAsyncEndpoint, AsyncGetSendQueueCount)
{
    MOCKER_CPP(&Sock::GetSendQueueCount).stubs().will(returnValue(1));
    uint32_t ret = ep->GetSendQueueCount();
    EXPECT_EQ(ret, 1);
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostSendZCopy)
{
    UBSHcomNetTransOpInfo OpInfo{};
    MOCKER_CPP(&SockWorker::PostSend).stubs()
        .will(returnValue(static_cast<int>(SS_TCP_RETRY)))
        .then(returnValue(1))
        .then(returnValue(0));
    
    int ret = ep->PostSendZCopy(0, request, OpInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = ep->PostSendZCopy(0, request, OpInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostSendSeqZCopy)
{
    ep->mSendZCopy = true;
    MOCKER_CPP(&NetAsyncEndpointSock::PostSendZCopy).stubs().will(returnValue(0));
    int ret = ep->PostSend(0, request, 0);
    EXPECT_EQ(ret, 0);
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostSendSeqValidateStateFail)
{
    ep->mState.Set(NEP_BROKEN);
    int ret = ep->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));
    ep->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostSendSeqValidateBuffFail)
{
    request.upCtxSize = sizeof(SockOpContextInfo::upCtx) + 1;
    int ret = ep->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
    request.upCtxSize = 0;
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostSendSeqGetBuffErr)
{
    ep->mSendZCopy = false;
    MOCKER_CPP(&NormalMemoryRegionFixedBuffer::GetFreeBuffer, bool(NormalMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs().will(returnValue(false));
    int ret = ep->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_GET_BUFF_FAILED));
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostSendSeqCopyErr)
{
    ep->mSendZCopy = false;
    NormalMemoryRegionFixedBuffer *Mr = mDriver->mSockDriverSendMR
        = new (std::nothrow)NormalMemoryRegionFixedBuffer(name, 1, 1);
    MOCKER_CPP(&NormalMemoryRegionFixedBuffer::GetFreeBuffer, bool(NormalMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs().will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));
    MOCKER_CPP(&NormalMemoryRegionFixedBuffer::ReturnBuffer, bool(NormalMemoryRegionFixedBuffer::*)(uintptr_t))
        .stubs().will(returnValue(true));
    int ret = ep->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));

    if (Mr != nullptr) {
        delete Mr;
        Mr = nullptr;
    }
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostSendOpInfoValidateStateFail)
{
    ep->mState.Set(NEP_BROKEN);
    UBSHcomNetTransOpInfo opInfo{};
    int ret = ep->PostSend(0, request, opInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));
    ep->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostSendOpInfoValidateBuffFail)
{
    request.size = 0;
    UBSHcomNetTransOpInfo opInfo{};
    int ret = ep->PostSend(0, request, opInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
    request.size = 1;
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostSendOpInfoZCopy)
{
    ep->mSendZCopy = true;
    UBSHcomNetTransOpInfo opInfo{};
    MOCKER_CPP(&NetAsyncEndpointSock::PostSendZCopy).stubs().will(returnValue(0));
    int ret = ep->PostSend(0, request, opInfo);
    EXPECT_EQ(ret, 0);
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostSendOpInfoGetBuffErr)
{
    ep->mSendZCopy = false;
    UBSHcomNetTransOpInfo opInfo{};
    MOCKER_CPP(&NormalMemoryRegionFixedBuffer::GetFreeBuffer, bool(NormalMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs().will(returnValue(false));
    int ret = ep->PostSend(0, request, opInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_GET_BUFF_FAILED));
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostSendOpInfoCopyErr)
{
    ep->mSendZCopy = false;
    UBSHcomNetTransOpInfo opInfo{};
    NormalMemoryRegionFixedBuffer *Mr = mDriver->mSockDriverSendMR
        = new (std::nothrow)NormalMemoryRegionFixedBuffer(name, 1, 1);
    MOCKER_CPP(&NormalMemoryRegionFixedBuffer::GetFreeBuffer, bool(NormalMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs().will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));
    MOCKER_CPP(&NormalMemoryRegionFixedBuffer::ReturnBuffer, bool(NormalMemoryRegionFixedBuffer::*)(uintptr_t))
        .stubs().will(returnValue(true));
    int ret = ep->PostSend(0, request, opInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));

    if (Mr != nullptr) {
        delete Mr;
        Mr = nullptr;
    }
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostSendRawValidateStateFail)
{
    ep->mState.Set(NEP_BROKEN);
    int ret = ep->PostSendRaw(request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));
    ep->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostSendRawValidateBuffFail)
{
    request.size = 0;
    int ret = ep->PostSendRaw(request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
    request.size = 1;
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostSendRawSeqZCopy)
{
    ep->mSendZCopy = true;
    MOCKER_CPP(&NetAsyncEndpointSock::PostSendZCopy).stubs().will(returnValue(0));
    int ret = ep->PostSendRaw(request, 0);
    EXPECT_EQ(ret, 0);
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostSendRawSeqGetBuffErr)
{
    ep->mSendZCopy = false;
    MOCKER_CPP(&NormalMemoryRegionFixedBuffer::GetFreeBuffer, bool(NormalMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs().will(returnValue(false));
    int ret = ep->PostSendRaw(request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_GET_BUFF_FAILED));
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostSendRawSeqCopyErr)
{
    ep->mSendZCopy = false;
    NormalMemoryRegionFixedBuffer *Mr = mDriver->mSockDriverSendMR
        = new (std::nothrow)NormalMemoryRegionFixedBuffer(name, 1, 1);
    MOCKER_CPP(&NormalMemoryRegionFixedBuffer::GetFreeBuffer, bool(NormalMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs().will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));
    MOCKER_CPP(&NormalMemoryRegionFixedBuffer::ReturnBuffer, bool(NormalMemoryRegionFixedBuffer::*)(uintptr_t))
        .stubs().will(returnValue(true));
    int ret = ep->PostSendRaw(request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));

    if (Mr != nullptr) {
        delete Mr;
        Mr = nullptr;
    }
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostSendRawSglValidateStateFail)
{
    ep->mState.Set(NEP_BROKEN);
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    int ret = ep->PostSendRawSgl(sglReq, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));
    ep->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostSendRawSglValidateBuffFail)
{
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    sglReq.upCtxSize = sizeof(SockOpContextInfo::upCtx) + 1;
    int ret = ep->PostSendRawSgl(sglReq, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostReadValidateStateFail)
{
    ep->mState.Set(NEP_BROKEN);
    int ret = ep->PostRead(request);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));
    ep->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostReadValidateBuffFail)
{
    request.size = 0;
    int ret = ep->PostRead(request);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
    request.size = 1;
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostReadOneSideValidateFail)
{
    MOCKER_CPP(&NetDriverSockWithOOB::ValidateMemoryRegion).stubs().will(returnValue(1));
    int ret = ep->PostRead(request);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_LKEY));
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostReadSglValidateStateFail)
{
    ep->mState.Set(NEP_BROKEN);
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    int ret = ep->PostRead(sglReq);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
    ep->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostReadSglOneSideSglValidateFail)
{
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    sglReq.upCtxSize = sizeof(SockOpContextInfo::upCtx) + 1;
    int ret = ep->PostRead(sglReq);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostWriteValidateStateFail)
{
    ep->mState.Set(NEP_BROKEN);
    int ret = ep->PostWrite(request);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
    ep->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostWriteValidateBuffFail)
{
    request.size = 0;
    int ret = ep->PostWrite(request);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
    request.size = 1;
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostWriteOneSideValidateFail)
{
    MOCKER_CPP(&NetDriverSockWithOOB::ValidateMemoryRegion).stubs().will(returnValue(1));
    int ret = ep->PostWrite(request);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostWriteSglValidateStateFail)
{
    ep->mState.Set(NEP_BROKEN);
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    int ret = ep->PostWrite(sglReq);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
    ep->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestNetSockAsyncEndpoint, AsyncPostWriteSglOneSideSglValidateFail)
{
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    sglReq.upCtxSize = sizeof(SockOpContextInfo::upCtx) + 1;
    int ret = ep->PostWrite(sglReq);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
}

TEST_F(TestNetSockAsyncEndpoint, AsyncEnableSendZCopy)
{
    EXPECT_NO_FATAL_FAILURE(ep->EnableSendZCopy());
    ep->mSendZCopy = false;
}

TEST_F(TestNetSockAsyncEndpoint, AsyncGetFinishTime)
{
    ep->mDefaultTimeout = 0;
    uint64_t ret = ep->GetFinishTime();
    EXPECT_EQ(ret, 0);
}

TEST_F(TestNetSockAsyncEndpoint, StateValidationFail)
{
    int ret = StateValidation(ep->mState, 0, nullptr, nullptr);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));
}

TEST_F(TestNetSockAsyncEndpoint, TwoSideSglValidationFail)
{
    size_t totalSize = 0;
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    sglReq.iovCount = 0;
    int ret = TwoSideSglValidation(sglReq, mDriver, 1, totalSize);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
}

TEST_F(TestNetSockAsyncEndpoint, TwoSideSglValidationLkeyFail)
{
    size_t totalSize = 0;
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    MOCKER_CPP(&NetDriverSockWithOOB::ValidateMemoryRegion).stubs().will(returnValue(1));
    int ret = TwoSideSglValidation(sglReq, mDriver, 1, totalSize);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_LKEY));
}

TEST_F(TestNetSockAsyncEndpoint, OneSideValidationFail)
{
    request.upCtxSize = sizeof(SockOpContextInfo::upCtx) + 1;
    int ret = OneSideValidation(request, mDriver);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
    request.upCtxSize = 0;
}
}
}