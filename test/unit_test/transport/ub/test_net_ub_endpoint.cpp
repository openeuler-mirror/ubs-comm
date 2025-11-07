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

#include "hcom.h"
#include "ub_common.h"
#include "ub_worker.h"
#include "net_ub_driver_oob.h"
#include "net_ub_endpoint.h"

#include "net_monotonic.h"
#include "net_security_alg.h"
#include "hcom_utils.h"
#include "ub_urma_wrapper_jetty.h"

namespace ock {
namespace hcom {

class TestNetUBAsyncEndpoint : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);

    std::string name;
    NetDriverUBWithOob *mDriver = nullptr;
    UBContext *ctx = nullptr;
    UBWorker *mWorker = nullptr;
    UBJfc *cq = nullptr;
    UBJetty *qp = nullptr;
    UBSHcomNetWorkerIndex mWorkerIndex;
    UBSHcomNetTransRequest request;
    UBSHcomNetTransSglRequest sglRequest;
    UBSHcomNetTransSgeIov *iov = nullptr;
    NetUBAsyncEndpoint *NEP = nullptr;
    UBMemoryRegionFixedBuffer *Mr = nullptr;
    NetHeartbeat *mHeartBeat = nullptr;
};

void TestNetUBAsyncEndpoint::SetUp()
{
    UBEId eid{};
    UBContext::Create("test_net_ub_endpoint", eid, ctx);
    ASSERT_NE(ctx, nullptr);

    bool startOobSvr = true;
    UBSHcomNetDriverProtocol protocol = UBC;
    mDriver = new (std::nothrow) NetDriverUBWithOob(name, startOobSvr, protocol);
    mDriver->mStarted = true;
    Mr = mDriver->mDriverSendMR = new (std::nothrow) UBMemoryRegionFixedBuffer(name, ctx, 1, 1, 1);
    ASSERT_NE(mDriver, nullptr);

    UBWorkerOptions options;
    NetMemPoolFixedPtr memPool;
    NetMemPoolFixedPtr sglMemPool;
    mWorker = new (std::nothrow) UBWorker(name, ctx, options, memPool, sglMemPool);
    ASSERT_NE(mWorker, nullptr);

    cq = new (std::nothrow) UBJfc(name, ctx, false, 0);
    ASSERT_NE(cq, nullptr);

    JettyOptions jettyOptions;
    qp = new (std::nothrow) UBJetty(name, 0, ctx, cq, jettyOptions);
    ASSERT_NE(qp, nullptr);

    qp->StoreExchangeInfo(new UBJettyExchangeInfo);

    NEP = new (std::nothrow) NetUBAsyncEndpoint(0, qp, mDriver, mWorker);
    ASSERT_NE(NEP, nullptr);
    NEP->mState.Set(NEP_ESTABLISHED);
    NEP->mAllowedSize = NN_NO128;
    NEP->mSegSize = NN_NO128;

    mHeartBeat = new (std::nothrow) NetHeartbeat(mDriver, NN_NO60, NN_NO2);

    request.lAddress = reinterpret_cast<uintptr_t>(&mWorkerIndex);
    request.rAddress = reinterpret_cast<uintptr_t>(&mWorkerIndex);
    request.size = 1;

    iov = new (std::nothrow) UBSHcomNetTransSgeIov();
    ASSERT_NE(iov, nullptr);
    iov->lAddress = reinterpret_cast<uintptr_t>(&mWorkerIndex);
    iov->rAddress = reinterpret_cast<uintptr_t>(&mWorkerIndex);
    iov->size = 1;
    sglRequest = UBSHcomNetTransSglRequest(iov, 1, 1);
}

void TestNetUBAsyncEndpoint::TearDown()
{
    qp->mSendJfc = nullptr;
    qp->mRecvJfc = nullptr;
    qp->mUrmaJetty = nullptr;
    qp->mJettyMr = nullptr;
    cq->mUBContext = nullptr;
    cq->mUrmaJfc = nullptr;
    if (cq != nullptr) {
        delete cq;
        cq = nullptr;
    }
    if (Mr != nullptr) {
        delete Mr;
        Mr = nullptr;
    }
    if (NEP != nullptr) {
        delete NEP;
        NEP = nullptr;
    }
    if (iov != nullptr) {
        delete iov;
        iov = nullptr;
    }
    GlobalMockObject::verify();
}
static UBSHcomNetTransHeader mockMrBuf{};
static bool MockGetFreeBuffer(uintptr_t &mrBufAddress)
{
    mrBufAddress = reinterpret_cast<uintptr_t>(&mockMrBuf);
    return true;
}

static NResult FakePollingCompletion(UBOpContextInfo *&ctx, int32_t timeout, uint32_t &immData)
{
    static char buf[128];
    static UBOpContextInfo info;
    info.dataSize = 1;
    info.mrMemAddr = reinterpret_cast<uintptr_t>(buf); // used by NetUBSyncEndpoint::ReceiveRawHandle

    ctx = &info;
    return NN_OK;
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointPostSendSeqFailed)
{
    name = "NetUBAsyncEndpointPostSendSeqFailed";
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool(UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(returnValue(false));
    int ret = NEP->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_GET_BUFF_FAILED));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointPostSendSeq)
{
    name = "NetUBAsyncEndpointPostSend";
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool(UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));

    MOCKER_CPP(&UBWorker::PostSend)
        .stubs()
        .will(returnValue(1))
        .then(returnValue(0))
        .then(returnValue(static_cast<int>(RR_QP_POST_SEND_FAILED)));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));
    MOCKER_CPP(&AesGcm128::Encrypt).stubs().will(returnValue(false));

    NEP->mIsNeedEncrypt = 0;
    int ret = NEP->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    NEP->mIsNeedEncrypt = 1;
    ret = NEP->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_ENCRYPT_FAILED));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointPostSendSeqTwo)
{
    name = "NetUBAsyncEndpointPostSend";
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool(UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&UBWorker::PostSend)
        .stubs()
        .will(returnValue(static_cast<int>(UB_QP_POST_SEND_WR_FULL)))
        .then(returnValue(1))
        .then(returnValue(0));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));

    NEP->mIsNeedEncrypt = 0;
    int ret = NEP->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = NEP->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointPostRead)
{
    name = "NetUBAsyncEndpointPostRead";
    MOCKER_CPP(&UBWorker::PostRead, UResult(UBWorker::*)(UBJetty *, const UBSendReadWriteRequest &))
        .stubs()
        .will(returnValue(1))
        .then(returnValue(0));

    int ret = 0;
    uint32_t key = sglRequest.iov[0].lKey;
    mDriver->mMapTseg.emplace(key, nullptr);

    ret = NEP->PostRead(request);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = NEP->PostRead(request);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointPostWrite)
{
    name = "NetUBAsyncEndpointPostWrite";
    MOCKER_CPP(&UBWorker::PostWrite,
               UResult(UBWorker::*)(UBJetty *, const UBSendReadWriteRequest &, UBOpContextInfo::OpType))
        .stubs()
        .will(returnValue(1))
        .then(returnValue(0));
    int ret = 0;
    uint32_t key = sglRequest.iov[0].lKey;
    mDriver->mMapTseg.emplace(key, nullptr);

    ret = NEP->PostWrite(request);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = NEP->PostWrite(request);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetUBAsyncEndpoint, PostSendSglInlineEncrypt)
{
    name = "NetUBAsyncEndpointPostSendSglInline";
    NEP->mIsNeedEncrypt = true;
    
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool(UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&UBWorker::PostSend)
        .stubs()
        .will(returnValue(0));
    MOCKER_CPP(&AesGcm128::Encrypt).stubs().will(returnValue(true));

    UBSHcomNetTransOpInfo OpInfo{};
    auto ret = NEP->PostSendSglInline(0, request, OpInfo);
    EXPECT_EQ(ret, NN_OK);
}

TEST_F(TestNetUBAsyncEndpoint, PostSendSglInlineNotUB)
{
    name = "NetUBAsyncEndpointPostSendSglInline";
    NEP->mIsNeedEncrypt = false;
    NEP->mJetty->mUBContext->protocol = UBSHcomNetDriverProtocol::RDMA;

    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool(UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&UBWorker::PostSend)
        .stubs()
        .will(returnValue(0));
    MOCKER_CPP(&AesGcm128::Encrypt).stubs().will(returnValue(true));

    UBSHcomNetTransOpInfo OpInfo{};
    auto ret = NEP->PostSendSglInline(0, request, OpInfo);
    EXPECT_EQ(ret, 0);
}

TEST_F(TestNetUBAsyncEndpoint, PostSendSglInlineUBSuccess)
{
    name = "NetUBAsyncEndpointPostSendSglInline";
    NEP->mIsNeedEncrypt = false;
    NEP->mJetty->mUBContext->protocol = UBSHcomNetDriverProtocol::UBC;

    MOCKER_CPP(&UBWorker::PostSendSglInline)
        .stubs()
        .will(returnValue(0));

    UBSHcomNetTransOpInfo OpInfo{};
    auto ret = NEP->PostSendSglInline(0, request, OpInfo);
    EXPECT_EQ(ret, 0);
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointPostSendInfoFailed)
{
    name = "NetUBAsyncEndpointPostSend";
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool(UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(returnValue(false));
    UBSHcomNetTransOpInfo OpInfo{};
    int ret = NEP->PostSend(0, request, OpInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_GET_BUFF_FAILED));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointPostSendOpInfo)
{
    name = "NetUBAsyncEndpointPostSend";
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool(UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));

    MOCKER_CPP(&UBWorker::PostSend)
        .stubs()
        .will(returnValue(1))
        .then(returnValue(0))
        .then(returnValue(static_cast<int>(RR_QP_POST_SEND_FAILED)));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));
    MOCKER_CPP(&AesGcm128::Encrypt).stubs().will(returnValue(false));

    UBSHcomNetTransOpInfo OpInfo{};
    NEP->mIsNeedEncrypt = 0;
    int ret = NEP->PostSend(0, request, OpInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    NEP->mIsNeedEncrypt = 1;
    ret = NEP->PostSend(0, request, OpInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_ENCRYPT_FAILED));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointPostSendOpInfoTwo)
{
    name = "NetUBAsyncEndpointPostSend";
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool(UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&UBWorker::PostSend)
        .stubs()
        .will(returnValue(static_cast<int>(UB_QP_POST_SEND_WR_FULL)))
        .then(returnValue(1))
        .then(returnValue(0));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));

    UBSHcomNetTransOpInfo OpInfo{};
    NEP->mIsNeedEncrypt = 0;
    int ret = NEP->PostSend(0, request, OpInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = NEP->PostSend(0, request, OpInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointPostSendOpInfoWithHeaderRaw)
{
    NEP->mIsNeedEncrypt = 0;

    UBSHcomNetTransOpInfo OpInfo{};
    UBSHcomExtHeaderType extHeaderType;
    UBSHcomFragmentHeader extHeader;
    int ret;

    extHeaderType = UBSHcomExtHeaderType::RAW;
    ret = NEP->PostSend(0, request, OpInfo, extHeaderType, &extHeader, sizeof(extHeader));
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointPostSendOpInfoWithHeaderNull)
{
    NEP->mIsNeedEncrypt = 0;

    UBSHcomNetTransOpInfo OpInfo{};
    UBSHcomExtHeaderType extHeaderType;
    UBSHcomFragmentHeader extHeader;
    int ret;

    extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ret = NEP->PostSend(0, request, OpInfo, extHeaderType, nullptr, sizeof(extHeader));
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointPostSendOpInfoWithHeaderBuffer)
{
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer).stubs().will(returnValue(false));

    NEP->mIsNeedEncrypt = 0;

    UBSHcomNetTransOpInfo OpInfo{};
    UBSHcomExtHeaderType extHeaderType;
    UBSHcomFragmentHeader extHeader;
    int ret;

    extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ret = NEP->PostSend(0, request, OpInfo, extHeaderType, &extHeader, sizeof(extHeader));
    EXPECT_EQ(ret, NN_GET_BUFF_FAILED);
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointPostSendOpInfoWithHeaderMemcpy)
{
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer).stubs().will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0)).then(returnValue(1));

    NEP->mIsNeedEncrypt = 0;

    UBSHcomNetTransOpInfo OpInfo{};
    UBSHcomExtHeaderType extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    UBSHcomFragmentHeader extHeader{};
    int ret;

    extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ret = NEP->PostSend(0, request, OpInfo, extHeaderType, &extHeader, sizeof(extHeader));
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ret = NEP->PostSend(0, request, OpInfo, extHeaderType, &extHeader, sizeof(extHeader));
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointPostSendOpInfoWithHeaderWorkerSend)
{
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer).stubs().will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&UBWorker::PostSend)
            .stubs()
            .will(returnValue(static_cast<int>(UB_QP_POST_SEND_WR_FULL)))
            .then(returnValue(static_cast<RResult>(NN_OK)))
            .then(returnValue(static_cast<RResult>(RR_QP_POST_SEND_FAILED)));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));

    NEP->mIsNeedEncrypt = 0;

    UBSHcomNetTransOpInfo OpInfo{};
    UBSHcomExtHeaderType extHeaderType;
    UBSHcomFragmentHeader extHeader{};
    int ret;

    extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ret = NEP->PostSend(0, request, OpInfo, extHeaderType, &extHeader, sizeof(extHeader));
    EXPECT_EQ(ret, NN_OK);

    extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ret = NEP->PostSend(0, request, OpInfo, extHeaderType, &extHeader, sizeof(extHeader));
    EXPECT_EQ(ret, RR_QP_POST_SEND_FAILED);
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointPostSendAll)
{
    name = "NetUBAsyncEndpointPostSendAll";

    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool(UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));

    MOCKER_CPP(&NetDriverUB::ValidateMemoryRegion, NResult(NetDriverUB::*)(uint64_t, uintptr_t, uint64_t)).stubs()
        .will(returnValue(0));

    MOCKER_CPP(&UBWorker::PostSend, NResult(UBWorker::*)(UBJetty *, const UBSendReadWriteRequest &,
        urma_target_seg_t *, uint32_t)).stubs().will(returnValue(0));

    MOCKER_CPP(&UBJetty::GetUpContext1, uintptr_t(UBJetty::*)() const).stubs()
        .will(returnValue(reinterpret_cast<uintptr_t>(mWorker)));

    int ret = NEP->PostSendRaw(request, 1);
    EXPECT_EQ(ret, static_cast<int>(UB_OK));

    MOCKER_CPP(&UBWorker::PostSendSgl, NResult(UBWorker::*)(UBJetty *, const UBSHcomNetTransSglRequest &,
        const UBSHcomNetTransRequest &, uint32_t, bool)).stubs().will(returnValue(0));

    ret = NEP->PostSendRawSgl(sglRequest, 1);
    EXPECT_EQ(ret, static_cast<int>(UB_PARAM_INVALID));

    uint32_t key = sglRequest.iov[0].lKey;
    mDriver->mMapTseg.emplace(key, nullptr);

    ret = NEP->PostSendRawSgl(sglRequest, 1);
    EXPECT_EQ(ret, static_cast<int>(UB_OK));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointPostSendRawAllTwo)
{
    MOCKER_CPP(&UBJetty::GetUpContext1, uintptr_t(UBJetty::*)() const).stubs()
        .will(returnValue(reinterpret_cast<uintptr_t>(mWorker)));

    MOCKER_CPP(&UBWorker::PostOneSideSgl)
            .stubs()
            .will(returnValue(static_cast<int>(UB_QP_POST_SEND_WR_FULL)))
            .then(returnValue(1))
            .then(returnValue(0))
            .then(returnValue(static_cast<int>(UB_QP_POST_SEND_WR_FULL)))
            .then(returnValue(1))
            .then(returnValue(0));

    int ret = NEP->PostRead(sglRequest);
    EXPECT_EQ(ret, static_cast<int>(UB_PARAM_INVALID));

    uint32_t key = sglRequest.iov[0].lKey;
    mDriver->mMapTseg.emplace(key, nullptr);

    ret = NEP->PostRead(sglRequest);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = NEP->PostRead(sglRequest);
    EXPECT_EQ(ret, static_cast<int>(UB_OK));

    ret = NEP->PostWrite(sglRequest);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = NEP->PostWrite(sglRequest);
    EXPECT_EQ(ret, static_cast<int>(UB_OK));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointPostSendRawGetBufferErr)
{
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool(UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(returnValue(false));
    int ret = NEP->PostSendRaw(request, 1);
    EXPECT_EQ(ret, static_cast<int>(NN_GET_BUFF_FAILED));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointPostSendRawCopyErr)
{
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool(UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));
    MOCKER_CPP(&AesGcm128::Encrypt,
               bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false));

    NEP->mIsNeedEncrypt = false;
    int ret = NEP->PostSendRaw(request, 1);
    EXPECT_EQ(ret, static_cast<int>(UB_PARAM_INVALID));

    NEP->mIsNeedEncrypt = true;
    ret = NEP->PostSendRaw(request, 1);
    EXPECT_EQ(ret, static_cast<int>(NN_ENCRYPT_FAILED));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointPostSendRaw)
{
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool(UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&UBWorker::PostSend)
        .stubs()
        .will(returnValue(static_cast<int>(UB_QP_POST_SEND_WR_FULL)))
        .then(returnValue(1))
        .then(returnValue(0));
    
    NEP->mIsNeedEncrypt = false;
    int ret = NEP->PostSendRaw(request, 1);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = NEP->PostSendRaw(request, 1);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointPostSendRawSgl)
{
    MOCKER_CPP(&UBWorker::PostSendSgl)
        .stubs()
        .will(returnValue(static_cast<int>(RR_QP_POST_SEND_WR_FULL)))
        .then(returnValue(1))
        .then(returnValue(0));
    int ret = NEP->PostSendRawSgl(sglRequest, 1);
    EXPECT_EQ(ret, static_cast<int>(UB_PARAM_INVALID));
    
    uint32_t key = sglRequest.iov[0].lKey;
    mDriver->mMapTseg.emplace(key, nullptr);
    NEP->mIsNeedEncrypt = false;
    ret = NEP->PostSendRawSgl(sglRequest, 1);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = NEP->PostSendRawSgl(sglRequest, 1);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointSetEpOption)
{
    UBSHcomEpOptions epOptions;
    int ret = NEP->SetEpOption(epOptions);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointGetSendQueueCount)
{
    MOCKER_CPP(&UBJetty::GetSendQueueSize).stubs().will(returnValue(1));
    int ret = NEP->GetSendQueueCount();
    EXPECT_EQ(ret, 1);
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointPeerIpAndPort)
{
    NEP->mJetty->mPeerIpPort = "1.2.3.4";
    std::string ret = NEP->PeerIpAndPort();
    EXPECT_EQ(ret, "1.2.3.4");

    NEP->mJetty = nullptr;
    ret = NEP->PeerIpAndPort();
    EXPECT_EQ(ret, CONST_EMPTY_STRING);
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointUdsName)
{
    std::string ret = NEP->UdsName();
    EXPECT_EQ(ret, CONST_EMPTY_STRING);
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointCheckTargetHbTime)
{
    uint64_t currTime = 1;
    NEP->mTargetHbTime = 0;
    bool ret = NEP->checkTargetHbTime(currTime);
    EXPECT_EQ(ret, true);

    NEP->mTargetHbTime = 1;
    ret = NEP->checkTargetHbTime(currTime);
    EXPECT_EQ(ret, false);
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointInvalidOperation)
{
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    int ret = NEP->WaitCompletion(timeout);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_OPERATION));
    ret = NEP->Receive(timeout, ctx);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_OPERATION));
    ret = NEP->ReceiveRaw(timeout, ctx);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_OPERATION));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointHbRecordCount)
{
    EXPECT_NO_FATAL_FAILURE(NEP->HbRecordCount());
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointHbCheckStateNormal)
{
    NEP->mHbCount = 1;
    NEP->mHbLastCount = 0;
    bool ret = NEP->HbCheckStateNormal();
    EXPECT_EQ(ret, true);

    NEP->mHbLastCount = 1;
    ret = NEP->HbCheckStateNormal();
    EXPECT_EQ(ret, false);
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointSetRemoteHbInfo)
{
    uintptr_t address = 0;
    uint32_t key = 0;
    uint64_t size = 0;
    EXPECT_NO_FATAL_FAILURE(NEP->SetRemoteHbInfo(address, key, size));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointSetHbBrokenEp)
{
    EXPECT_NO_FATAL_FAILURE(NEP->SetHbBrokenEp());
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointHbBrokenEp)
{
    NEP->mHbBrokenEp = false;
    bool ret = NEP->HbBrokenEp();
    EXPECT_EQ(ret, false);
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointEnableEncrypt)
{
    UBSHcomNetDriverOptions options{};
    EXPECT_NO_FATAL_FAILURE(NEP->EnableEncrypt(options));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointSetSecrets)
{
    NetSecrets secrets;
    EXPECT_NO_FATAL_FAILURE(NEP->SetSecrets(secrets));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointEstimatedEncryptLen)
{
    int ret = NEP->EstimatedEncryptLen(0);
    EXPECT_EQ(ret, 0);
    NEP->mIsNeedEncrypt = 0;
    ret = NEP->EstimatedEncryptLen(1);
    EXPECT_EQ(ret, 0);
}
TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointEstimatedEncryptLenTwo)
{
    NEP->mIsNeedEncrypt = 1;
    MOCKER_CPP(&AesGcm128::EstimatedEncryptLen).stubs().will(returnValue(1));
    int ret = NEP->EstimatedEncryptLen(1);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointEncrypt)
{
    uint64_t cipherLen = 0;
    MOCKER_CPP(&AesGcm128::Encrypt).stubs().will(returnValue(false));
    int ret = NEP->Encrypt(reinterpret_cast<void *>(0), 0, reinterpret_cast<void *>(0), cipherLen);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));

    NEP->mIsNeedEncrypt = 0;
    ret = NEP->Encrypt(reinterpret_cast<void *>(0), 0, reinterpret_cast<void *>(0), cipherLen);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointEncryptTwo)
{
    uint64_t cipherLen = 0;
    MOCKER_CPP(&AesGcm128::Encrypt).stubs().will(returnValue(true));

    NEP->mIsNeedEncrypt = 1;
    int ret = NEP->Encrypt(reinterpret_cast<void *>(0), 0, reinterpret_cast<void *>(0), cipherLen);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointEstimatedDecryptLen)
{
    NEP->mIsNeedEncrypt = 0;
    int ret = NEP->EstimatedDecryptLen(0);
    EXPECT_EQ(ret, 0);

    NEP->mIsNeedEncrypt = 1;
    MOCKER_CPP(&AesGcm128::GetRawLen).stubs().will(returnValue(1));
    ret = NEP->EstimatedDecryptLen(0);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointDecrypt)
{
    uint64_t rawLen = 0;
    MOCKER_CPP(&AesGcm128::Decrypt).stubs().will(returnValue(false));
    int ret = NEP->Decrypt(reinterpret_cast<void *>(0), 0, reinterpret_cast<void *>(0), rawLen);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));

    NEP->mIsNeedEncrypt = 0;
    ret = NEP->Decrypt(reinterpret_cast<void *>(0), 0, reinterpret_cast<void *>(0), rawLen);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointDecryptTwo)
{
    uint64_t rawLen = 0;
    MOCKER_CPP(&AesGcm128::Decrypt).stubs().will(returnValue(true));

    NEP->mIsNeedEncrypt = 1;
    int ret = NEP->Decrypt(reinterpret_cast<void *>(0), 0, reinterpret_cast<void *>(0), rawLen);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointGetRemoteUdsIdInfo)
{
    UBSHcomNetUdsIdInfo verbsIdInfo;
    NEP->mState.Set(NEP_NEW);
    int ret = NEP->GetRemoteUdsIdInfo(verbsIdInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));

    NEP->mState.Set(NEP_ESTABLISHED);
    NEP->mDriver->mStartOobSvr = false;
    ret = NEP->GetRemoteUdsIdInfo(verbsIdInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_UDS_ID_INFO_NOT_SUPPORT));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointGetRemoteUdsIdInfoTwo)
{
    UBSHcomNetUdsIdInfo verbsIdInfo;

    NEP->mState.Set(NEP_ESTABLISHED);
    NEP->mDriver->mStartOobSvr = true;
    NEP->mDriver->mOptions.oobType = NET_OOB_TCP;
    int ret = NEP->GetRemoteUdsIdInfo(verbsIdInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_UDS_ID_INFO_NOT_SUPPORT));

    NEP->mDriver->mOptions.oobType = NET_OOB_UDS;
    ret = NEP->GetRemoteUdsIdInfo(verbsIdInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointGetPeerIpPortErr)
{
    std::string ip;
    uint16_t port;
    bool ret = NEP->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, false);

    NEP->mJetty = nullptr;
    ret = NEP->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, false);
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointGetPeerIpPort)
{
    std::string ip;
    uint16_t port;
    NEP->mJetty->mPeerIpPort = "1.2.3.4";
    bool ret = NEP->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, false);
}

TEST_F(TestNetUBAsyncEndpoint, NetUBAsyncEndpointClose)
{
    EXPECT_NO_FATAL_FAILURE(NEP->Close());
}

class TestNetUBSyncEndpoint : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);

    std::string name;
    NetDriverUBWithOob *mDriver = nullptr;
    UBContext *ctx = nullptr;
    UBJfc *cq = nullptr;
    UBJetty *qp = nullptr;
    UBSHcomNetWorkerIndex mWorkerIndex;
    UBSHcomNetTransSglRequest sglRequest;
    UBSHcomNetTransRequest request;
    UBSHcomNetTransSgeIov *iov = nullptr;
    NetUBSyncEndpoint *NEP = nullptr;
    UBMemoryRegionFixedBuffer *Mr = nullptr;
};

void TestNetUBSyncEndpoint::SetUp()
{
    UBEId gid;
    ctx = new (std::nothrow) UBContext(name, gid);
    ASSERT_NE(ctx, nullptr);

    bool startOobSvr = true;
    UBSHcomNetDriverProtocol protocol = UBC;
    mDriver = new (std::nothrow) NetDriverUBWithOob(name, startOobSvr, protocol);
    mDriver->mStarted = true;
    Mr = mDriver->mDriverSendMR = new (std::nothrow) UBMemoryRegionFixedBuffer(name, ctx, 1, 1, 1);
    ASSERT_NE(mDriver, nullptr);

    cq = new (std::nothrow) UBJfc(name, ctx, false, 0);
    ASSERT_NE(cq, nullptr);

    JettyOptions jettyOptions;
    qp = new (std::nothrow) UBJetty(name, 0, ctx, cq, jettyOptions);
    ASSERT_NE(qp, nullptr);

    NEP = new (std::nothrow) NetUBSyncEndpoint(0, qp, cq, 0, mDriver, mWorkerIndex);
    NEP->mState.Set(NEP_ESTABLISHED);
    NEP->mAllowedSize = NN_NO128;
    NEP->mSegSize = NN_NO128;

    request.lAddress = reinterpret_cast<uintptr_t>(&mWorkerIndex);
    request.rAddress = reinterpret_cast<uintptr_t>(&mWorkerIndex);
    request.size = 1;

    iov = new (std::nothrow) UBSHcomNetTransSgeIov();
    iov->lAddress = reinterpret_cast<uintptr_t>(&mWorkerIndex);
    iov->rAddress = reinterpret_cast<uintptr_t>(&mWorkerIndex);
    iov->size = 1;
    sglRequest = UBSHcomNetTransSglRequest(iov, 1, 1);
}

void TestNetUBSyncEndpoint::TearDown()
{
    GlobalMockObject::verify();
    qp->mSendJfc = nullptr;
    qp->mRecvJfc = nullptr;
    qp->mUrmaJetty = nullptr;
    qp->mJettyMr = nullptr;
    cq->mUBContext = nullptr;
    cq->mUrmaJfc = nullptr;

    if (cq != nullptr) {
        delete cq;
        cq = nullptr;
    }
    if (Mr != nullptr) {
        delete Mr;
        Mr = nullptr;
    }
    if (NEP != nullptr) {
        delete NEP;
        NEP = nullptr;
    }
    if (iov != nullptr) {
        delete iov;
        iov = nullptr;
    }
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostSendSeqFailed)
{
    name = "NetUBSyncEndpointPostSendSeqFailed";
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool(UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(returnValue(false));
    int ret = NEP->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_GET_BUFF_FAILED));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostSendSeq)
{
    name = "NetUBSyncEndpointPostSend";
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool(UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));

    MOCKER_CPP(&UBWorker::PostSend)
        .stubs()
        .will(returnValue(1))
        .then(returnValue(0))
        .then(returnValue(static_cast<int>(RR_QP_POST_SEND_FAILED)));

    MOCKER_CPP(&AesGcm128::Encrypt).stubs().will(returnValue(false));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));
    NEP->mIsNeedEncrypt = 0;
    int ret = NEP->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(UB_QP_NOT_INITIALIZED));

    NEP->mIsNeedEncrypt = 1;
    ret = NEP->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_ENCRYPT_FAILED));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostSendSeqTwo)
{
    name = "NetUBSyncEndpointPostSend";
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool(UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&NetUBSyncEndpoint::InnerPostSend)
        .stubs()
        .will(returnValue(static_cast<int>(UB_QP_POST_SEND_WR_FULL)))
        .then(returnValue(1))
        .then(returnValue(0));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));

    NEP->mIsNeedEncrypt = 0;
    int ret = NEP->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = NEP->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostRead)
{
    name = "NetUBSyncEndpointPostRead";
    MOCKER_CPP(&NetUBSyncEndpoint::InnerPostRead, NResult(NetUBSyncEndpoint::*)(const UBSendReadWriteRequest &)).stubs()
        .will(returnValue(static_cast<int>(UB_QP_POST_SEND_WR_FULL)))
        .then(returnValue(1))
        .then(returnValue(0));

    int ret = NEP->PostRead(request);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = NEP->PostRead(request);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostWrite)
{
    name = "NetUBSyncEndpointPostWrite";
    MOCKER_CPP(&NetUBSyncEndpoint::InnerPostWrite, NResult(NetUBSyncEndpoint::*)(const UBSendReadWriteRequest &))
        .stubs()
        .will(returnValue(static_cast<int>(UB_QP_POST_SEND_WR_FULL)))
        .then(returnValue(1))
        .then(returnValue(0));

    int ret = NEP->PostWrite(request);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = NEP->PostWrite(request);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostSendInfoFailed)
{
    name = "NetUBSyncEndpointPostSend";
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool(UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(returnValue(false));
    UBSHcomNetTransOpInfo OpInfo{};
    int ret = NEP->PostSend(0, request, OpInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_GET_BUFF_FAILED));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostSendOpInfo)
{
    name = "NetUBSyncEndpointPostSend";
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool(UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));

    MOCKER_CPP(&UBWorker::PostSend)
        .stubs()
        .will(returnValue(1))
        .then(returnValue(0))
        .then(returnValue(static_cast<int>(RR_QP_POST_SEND_FAILED)));

    MOCKER_CPP(&AesGcm128::Encrypt).stubs().will(returnValue(false));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));
    UBSHcomNetTransOpInfo OpInfo{};
    NEP->mIsNeedEncrypt = 0;
    int ret = NEP->PostSend(0, request, OpInfo);
    EXPECT_EQ(ret, static_cast<int>(UB_QP_NOT_INITIALIZED));

    NEP->mIsNeedEncrypt = 1;
    ret = NEP->PostSend(0, request, OpInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_ENCRYPT_FAILED));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostSendOpInfoTwo)
{
    name = "NetUBSyncEndpointPostSend";
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool(UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&NetUBSyncEndpoint::InnerPostSend)
        .stubs()
        .will(returnValue(static_cast<int>(UB_QP_POST_SEND_WR_FULL)))
        .then(returnValue(1))
        .then(returnValue(0));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));

    UBSHcomNetTransOpInfo OpInfo{};
    NEP->mIsNeedEncrypt = 0;
    int ret = NEP->PostSend(0, request, OpInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = NEP->PostSend(0, request, OpInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostSendOpInfoWithHeaderRaw)
{
    NEP->mIsNeedEncrypt = 0;

    UBSHcomNetTransOpInfo opInfo{};
    UBSHcomExtHeaderType extHeaderType;
    UBSHcomFragmentHeader extHeader{};
    int ret;

    extHeaderType = UBSHcomExtHeaderType::RAW;
    ret = NEP->PostSend(0, request, opInfo, extHeaderType, &extHeader, sizeof(extHeaderType));
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostSendOpInfoWithHeaderNull)
{
    NEP->mIsNeedEncrypt = 0;

    UBSHcomNetTransOpInfo opInfo{};
    UBSHcomExtHeaderType extHeaderType;
    UBSHcomFragmentHeader extHeader{};
    int ret;

    extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ret = NEP->PostSend(0, request, opInfo, extHeaderType, nullptr, sizeof(extHeaderType));
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostSendOpInfoWithHeaderBuffer)
{
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer).stubs().will(returnValue(false));

    NEP->mIsNeedEncrypt = 0;

    UBSHcomNetTransOpInfo opInfo{};
    UBSHcomExtHeaderType extHeaderType;
    UBSHcomFragmentHeader extHeader{};
    int ret;

    extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ret = NEP->PostSend(0, request, opInfo, extHeaderType, &extHeader, sizeof(extHeaderType));
    EXPECT_EQ(ret, NN_GET_BUFF_FAILED);
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostSendOpInfoWithHeaderMemory)
{
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer).stubs().will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0)).then(returnValue(1));

    NEP->mIsNeedEncrypt = 0;

    UBSHcomNetTransOpInfo opInfo{};
    UBSHcomExtHeaderType extHeaderType;
    UBSHcomFragmentHeader extHeader{};
    int ret;

    extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ret = NEP->PostSend(0, request, opInfo, extHeaderType, &extHeader, sizeof(extHeader));
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ret = NEP->PostSend(0, request, opInfo, extHeaderType, &extHeader, sizeof(extHeader));
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostSendOpInfoWithHeaderWorkerSend)
{
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer).stubs().will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&NetUBSyncEndpoint::InnerPostSend)
        .stubs()
        .will(returnValue(static_cast<int>(UB_QP_POST_SEND_WR_FULL)))
        .then(returnValue(static_cast<RResult>(NN_OK)))
        .then(returnValue(static_cast<RResult>(RR_QP_POST_SEND_FAILED)));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));

    NEP->mIsNeedEncrypt = 0;

    UBSHcomNetTransOpInfo OpInfo{};
    UBSHcomExtHeaderType extHeaderType;
    UBSHcomFragmentHeader extHeader{};
    int ret;

    extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ret = NEP->PostSend(0, request, OpInfo, extHeaderType, &extHeader, sizeof(extHeader));
    EXPECT_EQ(ret, NN_OK);

    extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ret = NEP->PostSend(0, request, OpInfo, extHeaderType, &extHeader, sizeof(extHeader));
    EXPECT_EQ(ret, RR_QP_POST_SEND_FAILED);
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostSendAll)
{
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer)
            .stubs()
            .will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&UBJetty::PostSend)
            .stubs()
            .will(returnValue(static_cast<UResult>(UB_OK)));
    MOCKER_CPP(&UBJetty::PostSendSgl)
            .stubs()
            .will(returnValue(static_cast<UResult>(UB_OK)));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));

    int ret = NEP->PostSendRaw(request, 1);
    EXPECT_EQ(ret, static_cast<int>(UB_OK));

    ret = NEP->PostSendRawSgl(sglRequest, 1);
    EXPECT_EQ(ret, static_cast<int>(UB_PARAM_INVALID));

    uint32_t key = sglRequest.iov[0].lKey;
    mDriver->mMapTseg.emplace(key, nullptr);

    ret = NEP->PostSendRawSgl(sglRequest, 1);
    EXPECT_EQ(ret, static_cast<int>(UB_OK));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostSendRawAllTwo)
{
    MOCKER_CPP(&NetUBSyncEndpoint::PostOneSideSgl)
            .stubs()
            .will(returnValue(static_cast<int>(UB_QP_POST_SEND_WR_FULL)))
            .then(returnValue(1))
            .then(returnValue(0))
            .then(returnValue(static_cast<int>(UB_QP_POST_SEND_WR_FULL)))
            .then(returnValue(1))
            .then(returnValue(0));

    int ret = NEP->PostRead(sglRequest);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = NEP->PostRead(sglRequest);
    EXPECT_EQ(ret, static_cast<int>(UB_OK));

    ret = NEP->PostWrite(sglRequest);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = NEP->PostWrite(sglRequest);
    EXPECT_EQ(ret, static_cast<int>(UB_OK));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostSendRawGetBufferErr)
{
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool(UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(returnValue(false));
    int ret = NEP->PostSendRaw(request, 1);
    EXPECT_EQ(ret, static_cast<int>(UB_MEMORY_ALLOCATE_FAILED));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostSendRawCopyErr)
{
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool(UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));
    MOCKER_CPP(&AesGcm128::Encrypt,
               bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false));

    NEP->mIsNeedEncrypt = false;
    int ret = NEP->PostSendRaw(request, 1);
    EXPECT_EQ(ret, static_cast<int>(UB_PARAM_INVALID));

    NEP->mIsNeedEncrypt = true;
    ret = NEP->PostSendRaw(request, 1);
    EXPECT_EQ(ret, static_cast<int>(NN_ENCRYPT_FAILED));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostSendRaw)
{
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool(UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&NetUBSyncEndpoint::InnerPostSend)
        .stubs()
        .will(returnValue(static_cast<int>(UB_QP_POST_SEND_WR_FULL)))
        .then(returnValue(1))
        .then(returnValue(0));
    
    NEP->mIsNeedEncrypt = false;
    int ret = NEP->PostSendRaw(request, 1);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = NEP->PostSendRaw(request, 1);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointInnerPostSendSglNullErr)
{
    NEP->mJetty = nullptr;
    UBSendReadWriteRequest tlsReq;
    UBSendSglRWRequest sglRWRequest;
    int ret = NEP->InnerPostSendSgl(sglRWRequest, tlsReq, 0);
    EXPECT_EQ(ret, static_cast<int>(UB_PARAM_INVALID));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointInnerPostSendCopyErr)
{
    UBSendReadWriteRequest tlsReq;
    UBSendSglRWRequest sglRWRequest;
    sglRWRequest.upCtxSize = 1;
    MOCKER_CPP(&memcpy_s).stubs()
        .will(returnValue(0))
        .then(returnValue(1));

    int ret = NEP->InnerPostSendSgl(sglRWRequest, tlsReq, 0);
    EXPECT_EQ(ret, static_cast<int>(UB_PARAM_INVALID));

    ret = NEP->InnerPostSendSgl(sglRWRequest, tlsReq, 0);
    EXPECT_EQ(ret, static_cast<int>(UB_PARAM_INVALID));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostSendRawSgl)
{
    MOCKER_CPP(&NetUBSyncEndpoint::InnerPostSendSgl)
        .stubs()
        .will(returnValue(static_cast<int>(RR_QP_POST_SEND_WR_FULL)))
        .then(returnValue(1))
        .then(returnValue(0));
    
    NEP->mIsNeedEncrypt = false;
    int ret = NEP->PostSendRawSgl(sglRequest, 1);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = NEP->PostSendRawSgl(sglRequest, 1);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointWaitCompletionErr)
{
    MOCKER_CPP(&NetUBSyncEndpoint::PollingCompletion).stubs().will(returnValue(1));
    int ret = NEP->WaitCompletion(0);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointReceiveRaw)
{
    MOCKER_CPP(&NetUBSyncEndpoint::PollingCompletion)
            .stubs()
            .will(invoke(FakePollingCompletion));
    MOCKER_CPP(&NetUBSyncEndpoint::RePostReceive)
            .stubs()
            .will(returnValue(static_cast<NResult>(UB_OK)));

    UBSHcomNetResponseContext resCtx;
    int ret = NEP->ReceiveRaw(0, resCtx);
    EXPECT_EQ(ret, static_cast<int>(UB_OK));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPollingCompletionCqNull)
{
    UBOpContextInfo *ctx = nullptr;
    NEP->mJfc = nullptr;
    uint32_t immData = 0;

    int ret = NEP->PollingCompletion(ctx, 0, immData);
    EXPECT_EQ(ret, static_cast<int>(UB_EP_NOT_INITIALIZED));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostReceiveJettyNull)
{
    NEP->mJetty = nullptr;
    int ret = NEP->PostReceive(0, 0, nullptr);
    EXPECT_EQ(ret, static_cast<int>(UB_PARAM_INVALID));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostReceiveDequeueErr)
{
    MOCKER_CPP(&NetObjPool<UBOpContextInfo>::Dequeue)
        .stubs()
        .will(returnValue(false));
    int ret = NEP->PostReceive(0, 0, nullptr);
    EXPECT_EQ(ret, static_cast<int>(UB_PARAM_INVALID));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointRePostReceive)
{
    UBOpContextInfo *ctx = nullptr;
    int ret = NEP->RePostReceive(ctx);
    EXPECT_EQ(ret, static_cast<int>(UB_PARAM_INVALID));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointCreateResourcesParamErr)
{
    UBPollingMode pollMode = UB_EVENT_POLLING;
    JettyOptions options{};
    int ret = NEP->CreateResources(name, nullptr, pollMode, options, qp, cq);
    EXPECT_EQ(ret, static_cast<int>(UB_PARAM_INVALID));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointCreateResources)
{
    UBPollingMode pollMode = UB_EVENT_POLLING;
    JettyOptions options{};
    name = "test";
    int ret = NEP->CreateResources(name, ctx, pollMode, options, qp, cq);
    EXPECT_EQ(ret, static_cast<int>(UB_OK));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointInnerPostReadJettyNull)
{
    NEP->mJetty = nullptr;
    UBSendReadWriteRequest rwReq{};
    int ret = NEP->InnerPostRead(rwReq);
    EXPECT_EQ(ret, static_cast<int>(UB_PARAM_INVALID));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointInnerPostWriteJettyNull)
{
    NEP->mJetty = nullptr;
    UBSendReadWriteRequest rwReq{};
    int ret = NEP->InnerPostWrite(rwReq);
    EXPECT_EQ(ret, static_cast<int>(UB_PARAM_INVALID));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointCreateOneSideCtxParamErr)
{
    UBSgeCtxInfo sgeInfo{};
    uint64_t ctxArr[NET_SGE_MAX_IOV];
    int ret = NEP->CreateOneSideCtx(sgeInfo, nullptr, 0, ctxArr, true);
    EXPECT_EQ(ret, static_cast<int>(UB_PARAM_INVALID));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointCreateOneSideCtx)
{
    UBSgeCtxInfo sgeInfo{};
    uint64_t ctxArr[NET_SGE_MAX_IOV];
    int ret = NEP->CreateOneSideCtx(sgeInfo, iov, 1, ctxArr, true);
    EXPECT_EQ(ret, static_cast<int>(UB_OK));

    NEP->mJetty->DecreaseRef();
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostOneSideSglParamErr)
{
    sglRequest.upCtxSize = 1;
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0)).then(returnValue(1));

    int ret = NEP->PostOneSideSgl(sglRequest);
    EXPECT_EQ(ret, static_cast<int>(UB_PARAM_INVALID));

    ret = NEP->PostOneSideSgl(sglRequest);
    EXPECT_EQ(ret, static_cast<int>(UB_PARAM_INVALID));

    NEP->mJetty = nullptr;
    ret = NEP->PostOneSideSgl(sglRequest);
    EXPECT_EQ(ret, static_cast<int>(UB_PARAM_INVALID));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPostOneSideSglCreateOneSideCtxErr)
{
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));
    MOCKER_CPP(&NetUBSyncEndpoint::CreateOneSideCtx)
        .stubs()
        .will(returnValue(1));
    int ret = NEP->PostOneSideSgl(sglRequest);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointSetEpOption)
{
    UBSHcomEpOptions epOptions;
    int ret = NEP->SetEpOption(epOptions);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointGetSendQueueCount)
{
    int ret = NEP->GetSendQueueCount();
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointPeerIpAndPort)
{
    NEP->mJetty->mPeerIpPort = "1.2.3.4";
    std::string ret = NEP->PeerIpAndPort();
    EXPECT_EQ(ret, "1.2.3.4");

    NEP->mJetty = nullptr;
    ret = NEP->PeerIpAndPort();
    EXPECT_EQ(ret, CONST_EMPTY_STRING);
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointUdsName)
{
    std::string ret = NEP->UdsName();
    EXPECT_EQ(ret, CONST_EMPTY_STRING);
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointEnableEncrypt)
{
    UBSHcomNetDriverOptions options{};
    EXPECT_NO_FATAL_FAILURE(NEP->EnableEncrypt(options));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointSetSecrets)
{
    NetSecrets secrets;
    EXPECT_NO_FATAL_FAILURE(NEP->SetSecrets(secrets));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointEstimatedEncryptLen)
{
    int ret = NEP->EstimatedEncryptLen(0);
    EXPECT_EQ(ret, 0);
    NEP->mIsNeedEncrypt = 0;
    ret = NEP->EstimatedEncryptLen(1);
    EXPECT_EQ(ret, 0);
}
TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointEstimatedEncryptLenTwo)
{
    NEP->mIsNeedEncrypt = 1;
    MOCKER_CPP(&AesGcm128::EstimatedEncryptLen).stubs().will(returnValue(1));
    int ret = NEP->EstimatedEncryptLen(1);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointEncrypt)
{
    uint64_t cipherLen = 0;
    MOCKER_CPP(&AesGcm128::Encrypt).stubs().will(returnValue(false));
    int ret = NEP->Encrypt(reinterpret_cast<void *>(0), 0, reinterpret_cast<void *>(0), cipherLen);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));

    NEP->mIsNeedEncrypt = 0;
    ret = NEP->Encrypt(reinterpret_cast<void *>(0), 0, reinterpret_cast<void *>(0), cipherLen);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointEncryptTwo)
{
    uint64_t cipherLen = 0;
    MOCKER_CPP(&AesGcm128::Encrypt).stubs().will(returnValue(true));

    NEP->mIsNeedEncrypt = 1;
    int ret = NEP->Encrypt(reinterpret_cast<void *>(0), 0, reinterpret_cast<void *>(0), cipherLen);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointEstimatedDecryptLen)
{
    NEP->mIsNeedEncrypt = 0;
    int ret = NEP->EstimatedDecryptLen(0);
    EXPECT_EQ(ret, 0);

    NEP->mIsNeedEncrypt = 1;
    MOCKER_CPP(&AesGcm128::GetRawLen).stubs().will(returnValue(1));
    ret = NEP->EstimatedDecryptLen(0);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointDecrypt)
{
    uint64_t rawLen = 0;
    MOCKER_CPP(&AesGcm128::Decrypt).stubs().will(returnValue(false));
    int ret = NEP->Decrypt(reinterpret_cast<void *>(0), 0, reinterpret_cast<void *>(0), rawLen);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));

    NEP->mIsNeedEncrypt = 0;
    ret = NEP->Decrypt(reinterpret_cast<void *>(0), 0, reinterpret_cast<void *>(0), rawLen);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointDecryptTwo)
{
    uint64_t rawLen = 0;
    MOCKER_CPP(&AesGcm128::Decrypt).stubs().will(returnValue(true));

    NEP->mIsNeedEncrypt = 1;
    int ret = NEP->Decrypt(reinterpret_cast<void *>(0), 0, reinterpret_cast<void *>(0), rawLen);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointGetRemoteUdsIdInfo)
{
    UBSHcomNetUdsIdInfo verbsIdInfo;
    NEP->mState.Set(NEP_NEW);
    int ret = NEP->GetRemoteUdsIdInfo(verbsIdInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));

    NEP->mState.Set(NEP_ESTABLISHED);
    NEP->mDriver->mStartOobSvr = false;
    ret = NEP->GetRemoteUdsIdInfo(verbsIdInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_UDS_ID_INFO_NOT_SUPPORT));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointGetRemoteUdsIdInfoTwo)
{
    UBSHcomNetUdsIdInfo verbsIdInfo;

    NEP->mState.Set(NEP_ESTABLISHED);
    NEP->mDriver->mStartOobSvr = true;
    NEP->mDriver->mOptions.oobType = NET_OOB_TCP;
    int ret = NEP->GetRemoteUdsIdInfo(verbsIdInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_UDS_ID_INFO_NOT_SUPPORT));

    NEP->mDriver->mOptions.oobType = NET_OOB_UDS;
    ret = NEP->GetRemoteUdsIdInfo(verbsIdInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointGetPeerIpPortErr)
{
    std::string ip;
    uint16_t port;
    bool ret = NEP->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, false);

    NEP->mJetty = nullptr;
    ret = NEP->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, false);
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointGetPeerIpPort)
{
    std::string ip;
    uint16_t port;
    NEP->mJetty->mPeerIpPort = "1.2.3.4";
    bool ret = NEP->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, false);
}

TEST_F(TestNetUBSyncEndpoint, NetUBSyncEndpointClose)
{
    EXPECT_NO_FATAL_FAILURE(NEP->Close());
}

TEST_F(TestNetUBSyncEndpoint, SyncReceiveFailWithErrorOpType)
{
    // param init
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    UBOpContextInfo opCtx{};
    opCtx.opType = UBOpContextInfo::SEND;
    NEP->mDelayHandleReceiveCtx = &opCtx;

    MOCKER_CPP(&NetUBSyncEndpoint::RePostReceive)
    .stubs()
    .will(returnValue(0));

    NResult ret = NEP->Receive(timeout, ctx);
    EXPECT_EQ(ret, NN_ERROR);
}

TEST_F(TestNetUBSyncEndpoint, SyncReceiveFailWithNullptr)
{
    // param init
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    UBOpContextInfo opCtx{};
    opCtx.opType = UBOpContextInfo::RECEIVE;
    NEP->mDelayHandleReceiveCtx = nullptr;

    MOCKER_CPP(&NetUBSyncEndpoint::PollingCompletion)
        .stubs()
        .will(returnValue(1));
    int ret = NEP->Receive(timeout, ctx);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));
}

TEST_F(TestNetUBSyncEndpoint, SyncReceiveCopyErr)
{
    // param init
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    UBOpContextInfo opCtx{};
    opCtx.opType = UBOpContextInfo::RECEIVE;
    opCtx.dataSize = NN_NO1024;
    UBSHcomNetTransHeader header{};
    header.seqNo = 0;
    header.dataLength = NN_NO1024;
    opCtx.mrMemAddr = reinterpret_cast<uintptr_t>(&header);
    NEP->mDelayHandleReceiveCtx = &opCtx;

    MOCKER_CPP(NetFunc::ValidateHeaderWithDataSize)
        .stubs()
        .will(returnValue(0));
    
    MOCKER_CPP(&AesGcm128::GetRawLen)
        .stubs()
        .will(returnValue(1));
    
    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed)
        .stubs()
        .will(returnValue(false));

    MOCKER_CPP(&NetUBSyncEndpoint::RePostReceive)
        .stubs()
        .will(returnValue(0));

    NResult ret = NEP->Receive(timeout, ctx);
    EXPECT_EQ(ret, NN_MALLOC_FAILED);

    NEP->mIsNeedEncrypt = true;
    ret = NEP->Receive(timeout, ctx);
    EXPECT_EQ(ret, UB_CQ_NOT_INITIALIZED);
}

TEST_F(TestNetUBSyncEndpoint, SyncReceiveMemCopyHeaderErr)
{
    // param init
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    UBOpContextInfo opCtx{};
    opCtx.opType = UBOpContextInfo::RECEIVE;
    opCtx.dataSize = NN_NO1024;
    UBSHcomNetTransHeader header{};
    header.seqNo = 0;
    header.dataLength = NN_NO1024;
    opCtx.mrMemAddr = reinterpret_cast<uintptr_t>(&header);
    NEP->mDelayHandleReceiveCtx = &opCtx;

    MOCKER_CPP(NetFunc::ValidateHeaderWithDataSize)
        .stubs()
        .will(returnValue(0));
    
    MOCKER_CPP(&AesGcm128::GetRawLen)
        .stubs()
        .will(returnValue(1));
    
    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed)
        .stubs()
        .will(returnValue(true));
    
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0)).then(returnValue(1));

    MOCKER_CPP(&NetUBSyncEndpoint::RePostReceive)
        .stubs()
        .will(returnValue(0));

    NEP->mIsNeedEncrypt = false;
    NResult ret = NEP->Receive(timeout, ctx);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestNetUBSyncEndpoint, SyncReceiveMemCopyAddressErr)
{
    // param init
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    UBOpContextInfo opCtx{};
    opCtx.opType = UBOpContextInfo::RECEIVE;
    opCtx.dataSize = NN_NO1024;
    UBSHcomNetTransHeader header{};
    header.seqNo = 0;
    header.dataLength = NN_NO1024;
    opCtx.mrMemAddr = reinterpret_cast<uintptr_t>(&header);
    NEP->mDelayHandleReceiveCtx = &opCtx;

    MOCKER_CPP(NetFunc::ValidateHeaderWithDataSize)
        .stubs()
        .will(returnValue(0));
    
    MOCKER_CPP(&AesGcm128::GetRawLen)
        .stubs()
        .will(returnValue(1));
    
    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed)
        .stubs()
        .will(returnValue(true));
    
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));

    MOCKER_CPP(&NetUBSyncEndpoint::RePostReceive)
        .stubs()
        .will(returnValue(0));

    NEP->mIsNeedEncrypt = false;
    NResult ret = NEP->Receive(timeout, ctx);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestNetUBSyncEndpoint, SyncReceiveDecryptErr)
{
    // param init
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    UBOpContextInfo opCtx{};
    opCtx.opType = UBOpContextInfo::RECEIVE;
    opCtx.dataSize = NN_NO1024;
    UBSHcomNetTransHeader header{};
    header.seqNo = 0;
    header.dataLength = NN_NO1024;
    opCtx.mrMemAddr = reinterpret_cast<uintptr_t>(&header);
    NEP->mDelayHandleReceiveCtx = &opCtx;

    MOCKER_CPP(NetFunc::ValidateHeaderWithDataSize)
        .stubs()
        .will(returnValue(0));
    
    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed)
        .stubs()
        .will(returnValue(true));
    
    MOCKER_CPP(&AesGcm128::Decrypt, bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false));

    MOCKER_CPP(&NetUBSyncEndpoint::RePostReceive)
        .stubs()
        .will(returnValue(0));

    NEP->mIsNeedEncrypt = true;
    NResult ret = NEP->Receive(timeout, ctx);
    EXPECT_EQ(ret, NN_DECRYPT_FAILED);
}

TEST_F(TestNetUBSyncEndpoint, SyncReceiveFailWithOverDataSize)
{
    // param init
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    UBOpContextInfo opCtx{};
    opCtx.opType = UBOpContextInfo::RECEIVE;
    UBSHcomNetTransHeader header{};
    header.seqNo = 0;
    header.dataLength = NET_SGE_MAX_SIZE + NN_NO1;
    opCtx.mrMemAddr = reinterpret_cast<uintptr_t>(&header);
    NEP->mDelayHandleReceiveCtx = &opCtx;

    MOCKER_CPP(&NetUBSyncEndpoint::RePostReceive)
    .stubs()
    .will(returnValue(0));

    NResult ret = NEP->Receive(timeout, ctx);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestNetUBSyncEndpoint, SyncReceiveFailWithErrDataLen)
{
    // param init
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    UBOpContextInfo opCtx{};
    opCtx.opType = UBOpContextInfo::RECEIVE;
    opCtx.dataSize = NN_NO1024;
    UBSHcomNetTransHeader header{};
    header.seqNo = 0;
    header.dataLength = NN_NO2048;
    opCtx.mrMemAddr = reinterpret_cast<uintptr_t>(&header);
    NEP->mDelayHandleReceiveCtx = &opCtx;

    MOCKER_CPP(&NetUBSyncEndpoint::RePostReceive)
    .stubs()
    .will(returnValue(0));

    NResult ret = NEP->Receive(timeout, ctx);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestNetUBSyncEndpoint, SyncReceiveFailWithInvalidHeader)
{
    // param init
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    UBOpContextInfo opCtx{};
    opCtx.opType = UBOpContextInfo::RECEIVE;
    opCtx.dataSize = NN_NO1024;
    UBSHcomNetTransHeader header{};
    header.seqNo = 0;
    header.dataLength = NN_NO1024 - sizeof(UBSHcomNetTransHeader);
    opCtx.mrMemAddr = reinterpret_cast<uintptr_t>(&header);
    NEP->mDelayHandleReceiveCtx = &opCtx;

    MOCKER_CPP(&NetUBSyncEndpoint::RePostReceive)
    .stubs()
    .will(returnValue(0));

    NResult ret = NEP->Receive(timeout, ctx);
    EXPECT_EQ(ret, NN_VALIDATE_HEADER_CRC_INVALID);
}
}
}
#endif
