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
//#ifdef RDMA_BUILD_ENABLED
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include "hcom.h"
#include "net_common.h"
#include "rdma_composed_endpoint.h"
#include "net_rdma_driver_oob.h"
#include "net_security_rand.h"
#include "rdma_validation.h"
#include "net_rdma_async_endpoint.h"

namespace ock {
namespace hcom {

UBSHcomNetTransHeader mockAsyncMrBuf{};
class TestNetRdmaAsyncEndpoint : public testing::Test {
public:
    TestNetRdmaAsyncEndpoint();
    virtual void SetUp(void);
    virtual void TearDown(void);

    std::string name;
    std::string ip;
    uint16_t port;
    NetDriverRDMAWithOob *mDriver = nullptr;
    RDMAContext *ctx = nullptr;
    RDMAWorker *mWorker = nullptr;
    RDMACq *cq = nullptr;
    RDMAQp *qp = nullptr;
    UBSHcomNetWorkerIndex mWorkerIndex;
    RDMAAsyncEndPoint *ep = nullptr;
    UBSHcomNetTransRequest request;
    UBSHcomNetTransSglRequest sglRequest;
    UBSHcomNetTransSgeIov *iov = nullptr;
    NetAsyncEndpoint *NEP = nullptr;
    RDMAMemoryRegionFixedBuffer *Mr = nullptr;
    NetHeartbeat *mHeartBeat = nullptr;
};

TestNetRdmaAsyncEndpoint::TestNetRdmaAsyncEndpoint() {}

void TestNetRdmaAsyncEndpoint::SetUp()
{
    bool useDevX = true;
    RDMAGId gid;
    ctx = new (std::nothrow) RDMAContext(name, useDevX, gid);
    ASSERT_NE(ctx, nullptr);

    bool startOobSvr = true;
    UBSHcomNetDriverProtocol protocol = RDMA;
    mDriver = new (std::nothrow) NetDriverRDMAWithOob(name, startOobSvr, protocol);
    mDriver->mStarted = true;
    Mr = mDriver->mDriverSendMR = new (std::nothrow) RDMAMemoryRegionFixedBuffer(name, ctx, 1, 1);
    ASSERT_NE(mDriver, nullptr);

    NetMemPoolFixedOptions memOptions{};
    memOptions.minBlkSize = sizeof(RDMAOpContextInfo);

    NetMemPoolFixedPtr memPool = new (std::nothrow) NetMemPoolFixed(name, memOptions);
    ASSERT_EQ(memPool->Initialize(), NN_OK);

    NetMemPoolFixedPtr sglMemPool = new (std::nothrow) NetMemPoolFixed(name, memOptions);
    ASSERT_EQ(sglMemPool->Initialize(), NN_OK);

    RDMAWorkerOptions options;
    mWorker = new (std::nothrow) RDMAWorker(name, ctx, options, memPool, sglMemPool);
    mWorker->mOpCtxInfoPool.Initialize(mWorker->mOpCtxMemPool);
    mWorker->mSglCtxInfoPool.Initialize(mWorker->mSglCtxMemPool);
    ASSERT_NE(mWorker, nullptr);

    cq = new (std::nothrow) RDMACq(name, ctx, false, 0);
    ASSERT_NE(cq, nullptr);

    uint32_t mid = 0;
    QpOptions qpOptions;
    qp = new (std::nothrow) RDMAQp(name, mid, ctx, cq, qpOptions);
    qp->UpContext1((uintptr_t)mWorker);
    ASSERT_NE(qp, nullptr);

    uint64_t id = 0;
    ep = new (std::nothrow) RDMAAsyncEndPoint(name, mWorker, qp);
    ASSERT_NE(ep, nullptr);

    NEP = new (std::nothrow) NetAsyncEndpoint(id, ep, mDriver, mWorkerIndex);
    NEP->mState.Set(NEP_ESTABLISHED);
    NEP->mAllowedSize = NN_NO128;
    NEP->mSegSize = NN_NO128;

    mHeartBeat = new (std::nothrow) NetHeartbeat(mDriver, NN_NO60, NN_NO2);

    request.lAddress = reinterpret_cast<uintptr_t>(&mWorkerIndex);
    request.size = 1;

    iov = new (std::nothrow) UBSHcomNetTransSgeIov();
    sglRequest = UBSHcomNetTransSglRequest(iov, 1, 1);
}

void TestNetRdmaAsyncEndpoint::TearDown()
{
    GlobalMockObject::verify();
    if (Mr != nullptr) {
        delete Mr;
        Mr = nullptr;
    }
    if (NEP != nullptr) {
        delete NEP;
        NEP = nullptr;
    }
    if (ctx != nullptr) {
        delete ctx;
        ctx = nullptr;
    }
    if (iov != nullptr) {
        delete iov;
        iov = nullptr;
    }
}

static bool MockGetFreeBuffer(uintptr_t &mrBufAddress)
{
    mrBufAddress = reinterpret_cast<uintptr_t>(&mockAsyncMrBuf);
    return true;
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaPostSendSeqFailed)
{
    name = "NetAsyncEndpointRdmaPostSend";
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(returnValue(false));
    int ret = NEP->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_GET_BUFF_FAILED));
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaPostSendSeq)
{
    name = "NetAsyncEndpointRdmaPostSend";
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer)
        .stubs()
        .will(invoke(MockGetFreeBuffer));

    MOCKER_CPP(&RDMAWorker::PostSend)
        .stubs()
        .will(returnValue(1))
        .then(returnValue(0))
        .then(returnValue(static_cast<int>(RR_QP_POST_SEND_FAILED)));

    MOCKER_CPP(&AesGcm128::Encrypt,
               bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false));

    NEP->mIsNeedEncrypt = 0;
    int ret = NEP->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));

    NEP->mIsNeedEncrypt = 1;
    ret = NEP->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_ENCRYPT_FAILED));
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaPostSendSeqTwo)
{
    name = "NetAsyncEndpointRdmaPostSend";
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&RDMAWorker::PostSend).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));

    NEP->mIsNeedEncrypt = 0;
    int ret = NEP->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = NEP->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaPostSendInfoFailed)
{
    name = "NetAsyncEndpointRdmaPostSend";
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(returnValue(false));
    UBSHcomNetTransOpInfo OpInfo{};
    int ret = NEP->PostSend(0, request, OpInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_GET_BUFF_FAILED));
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaPostSendOpInfo)
{
    name = "NetAsyncEndpointRdmaPostSend";
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));

    MOCKER_CPP(&RDMAWorker::PostSend)
        .stubs()
        .will(returnValue(1))
        .then(returnValue(0))
        .then(returnValue(static_cast<int>(RR_QP_POST_SEND_FAILED)));

    MOCKER_CPP(&AesGcm128::Encrypt,
               bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false));

    UBSHcomNetTransOpInfo OpInfo{};
    NEP->mIsNeedEncrypt = 0;
    int ret = NEP->PostSend(0, request, OpInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));

    NEP->mIsNeedEncrypt = 1;
    ret = NEP->PostSend(0, request, OpInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_ENCRYPT_FAILED));
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaPostSendOpInfoTwo)
{
    name = "NetAsyncEndpointRdmaPostSend";
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&RDMAWorker::PostSend).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));

    UBSHcomNetTransOpInfo OpInfo{};
    NEP->mIsNeedEncrypt = 0;
    int ret = NEP->PostSend(0, request, OpInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = NEP->PostSend(0, request, OpInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaPostSendOpInfoWithHeaderRaw)
{
    NEP->mIsNeedEncrypt = 0;

    UBSHcomNetTransOpInfo OpInfo{};
    UBSHcomExtHeaderType extHeaderType;
    UBSHcomFragmentHeader extHeader{};
    int ret;

    extHeaderType = UBSHcomExtHeaderType::RAW;
    ret = NEP->PostSend(0, request, OpInfo, extHeaderType, &extHeader, sizeof(extHeader));
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaPostSendOpInfoWithHeaderNull)
{
    NEP->mIsNeedEncrypt = 0;

    UBSHcomNetTransOpInfo OpInfo{};
    UBSHcomExtHeaderType extHeaderType;
    UBSHcomFragmentHeader extHeader{};
    int ret;

    extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ret = NEP->PostSend(0, request, OpInfo, extHeaderType, nullptr, sizeof(extHeader));
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaPostSendOpInfoWithHeaderValidateFailed)
{
    NEP->mState.Set(NEP_BROKEN);
    UBSHcomNetTransOpInfo OpInfo{};
    UBSHcomExtHeaderType extHeaderType;
    UBSHcomFragmentHeader extHeader{};
    int ret;

    extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ret = NEP->PostSend(0, request, OpInfo, extHeaderType, &extHeader, sizeof(extHeader));
    EXPECT_EQ(ret, NN_EP_NOT_ESTABLISHED);
    NEP->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaPostSendOpInfoWithHeaderBuffer)
{
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(returnValue(false));

    NEP->mIsNeedEncrypt = 0;

    UBSHcomNetTransOpInfo OpInfo{};
    UBSHcomExtHeaderType extHeaderType;
    UBSHcomFragmentHeader extHeader{};
    int ret;

    extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ret = NEP->PostSend(0, request, OpInfo, extHeaderType, &extHeader, sizeof(extHeader));
    EXPECT_EQ(ret, NN_GET_BUFF_FAILED);
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaPostSendOpInfoWithHeaderMemcpy)
{
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0)).then(returnValue(1));

    NEP->mIsNeedEncrypt = 1;

    UBSHcomNetTransOpInfo OpInfo{};
    UBSHcomExtHeaderType extHeaderType;
    UBSHcomFragmentHeader extHeader{};
    int ret;

    extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ret = NEP->PostSend(0, request, OpInfo, extHeaderType, &extHeader, sizeof(extHeader));
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    extHeaderType = UBSHcomExtHeaderType::FRAGMENT;
    ret = NEP->PostSend(0, request, OpInfo, extHeaderType, &extHeader, sizeof(extHeader));
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaPostSendOpInfoWithHeaderWorkerSend)
{
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&RDMAWorker::PostSend)
        .stubs()
        .will(returnValue(static_cast<RResult>(NN_OK)))
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

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaPostSendRawSgl)
{
    name = "NetAsyncEndpointRdmaPostSendRawSgl";
    MOCKER_CPP(&RDMAWorker::PostSendSgl).stubs().will(returnValue(1)).then(returnValue(0));

    MOCKER_CPP(&NetDriverRDMAWithOob::ValidateMemoryRegion,
               NResult(NetDriverRDMAWithOob::*)(uint64_t, uintptr_t, uint64_t))
        .stubs()
        .will(returnValue(0));

    MOCKER_CPP(&AesGcm128::EstimatedEncryptLen).stubs().will(returnValue(static_cast<size_t>(0)));
    NEP->mIsNeedEncrypt = 0;
    int ret = NEP->PostSendRawSgl(sglRequest, 1);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = NEP->PostSendRawSgl(sglRequest, 1);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaPostSendRawSglFail)
{
    MOCKER_CPP(&NetDriverRDMAWithOob::ValidateMemoryRegion,
               NResult(NetDriverRDMAWithOob::*)(uint64_t, uintptr_t, uint64_t))
        .stubs()
        .will(returnValue(0));

    MOCKER_CPP(&AesGcm128::EstimatedEncryptLen).stubs().will(returnValue(static_cast<size_t>(0)));
    NEP->mIsNeedEncrypt = true;
    int ret = NEP->PostSendRawSgl(sglRequest, 1);
    EXPECT_EQ(ret, static_cast<int>(NN_ENCRYPT_FAILED));
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaSetEpOption)
{
    UBSHcomEpOptions epOptions;
    int ret = NEP->SetEpOption(epOptions);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaEstimatedEncryptLen)
{
    int ret = NEP->EstimatedEncryptLen(0);
    EXPECT_EQ(ret, 0);
    NEP->mIsNeedEncrypt = 0;
    ret = NEP->EstimatedEncryptLen(1);
    EXPECT_EQ(ret, 0);
}
TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaEstimatedEncryptLenTwo)
{
    NEP->mIsNeedEncrypt = 1;
    MOCKER_CPP(&AesGcm128::EstimatedEncryptLen).stubs().will(returnValue(1));
    int ret = NEP->EstimatedEncryptLen(1);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaEncrypt)
{
    uint64_t cipherLen = 0;
    MOCKER_CPP(&AesGcm128::Encrypt,
               bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false));
    int ret = NEP->Encrypt(reinterpret_cast<void *>(0), 0, reinterpret_cast<void *>(0), cipherLen);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));

    NEP->mIsNeedEncrypt = 0;
    ret = NEP->Encrypt(reinterpret_cast<void *>(0), 0, reinterpret_cast<void *>(0), cipherLen);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaEncryptTwo)
{
    uint64_t cipherLen = 0;
    MOCKER_CPP(&AesGcm128::Encrypt,
               bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(true));

    NEP->mIsNeedEncrypt = 1;
    int ret = NEP->Encrypt(reinterpret_cast<void *>(0), 0, reinterpret_cast<void *>(0), cipherLen);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaEstimatedDecryptLen)
{
    NEP->mIsNeedEncrypt = 0;
    int ret = NEP->EstimatedDecryptLen(0);
    EXPECT_EQ(ret, 0);

    NEP->mIsNeedEncrypt = 1;
    MOCKER_CPP(&AesGcm128::GetRawLen).stubs().will(returnValue(1));
    ret = NEP->EstimatedDecryptLen(0);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaDecrypt)
{
    uint64_t rawLen = 0;
    MOCKER_CPP(&AesGcm128::Decrypt, bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false));
    int ret = NEP->Decrypt(reinterpret_cast<void *>(0), 0, reinterpret_cast<void *>(0), rawLen);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));

    NEP->mIsNeedEncrypt = 0;
    ret = NEP->Decrypt(reinterpret_cast<void *>(0), 0, reinterpret_cast<void *>(0), rawLen);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaDecryptTwo)
{
    uint64_t rawLen = 0;
    MOCKER_CPP(&AesGcm128::Decrypt, bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(true));

    NEP->mIsNeedEncrypt = 1;
    int ret = NEP->Decrypt(reinterpret_cast<void *>(0), 0, reinterpret_cast<void *>(0), rawLen);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaGetRemoteUdsIdInfo)
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

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaGetRemoteUdsIdInfoTwo)
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

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaGetPeerIpPort)
{
    if (NEP->mEp->mQP != nullptr) {
        delete NEP->mEp->mQP;
        NEP->mEp->mQP = nullptr;
    }
    int ret = NEP->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, false);
    if (NEP->mEp != nullptr) {
        delete NEP->mEp;
        NEP->mEp = nullptr;
    }
    ret = NEP->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, false);
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaGetPeerIpPortTwo)
{
    NEP->mEp->mQP->mPeerIpPort = "0.0.0.0";
    int ret = NEP->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, false);
    NEP->mEp->mQP->mPeerIpPort = "0.0.0.0:sss";
    ret = NEP->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, false);
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaGetPeerIpPortThree)
{
    NEP->mEp->mQP->mPeerIpPort = "0.0.0.0:0";
    int ret = NEP->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, false);
    NEP->mEp->mQP->mPeerIpPort = "0.0.0.0:16";
    ret = NEP->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, true);
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaGetSendQueueSize)
{
    name = "NetAsyncEndpointRdmaPostSend";
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&RDMAQp::PostSend)
            .stubs()
            .will(returnValue(static_cast<NResult>(NN_OK)));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));

    NEP->mIsNeedEncrypt = 0;
    int ret = NEP->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
    EXPECT_EQ(NEP->GetSendQueueCount(), 1);

    ret = NEP->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
    EXPECT_EQ(NEP->GetSendQueueCount(), 2);
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaPostSendSglInlineOne)
{
    name = "NetAsyncEndpointRdmaPostSendSglInlineOne";
    NEP->mIsNeedEncrypt = true;
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));

    MOCKER_CPP(&AesGcm128::Encrypt,
               bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false));

    MOCKER_CPP(&RDMAQp::PostSend)
            .stubs()
            .will(returnValue(static_cast<NResult>(NN_OK)));

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));

    UBSHcomNetTransOpInfo opInfo{};
    int ret = NEP->PostSendSglInline(0, request, opInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_ENCRYPT_FAILED));
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaPostSendSglInlineTwo)
{
    name = "NetAsyncEndpointRdmaPostSendSglInlineTwo";
    NEP->mIsNeedEncrypt = true;
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));

    MOCKER_CPP(&AesGcm128::Encrypt,
               bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(true));

    MOCKER_CPP(&RDMAQp::PostSend)
            .stubs()
            .will(returnValue(static_cast<NResult>(NN_OK)));

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));

    UBSHcomNetTransOpInfo opInfo{};
    int ret = NEP->PostSendSglInline(0, request, opInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}


TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaPostSendSglInlineThree)
{
    name = "NetAsyncEndpointRdmaPostSendSglInlineThree";
    NEP->mIsNeedEncrypt = false;
    MOCKER_CPP(&RDMAWorker::PostSendSglInline)
        .stubs()
        .will(returnValue(0));

    UBSHcomNetTransOpInfo opInfo{};
    int ret = NEP->PostSendSglInline(0, request, opInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaPostSendSglInlineFour)
{
    name = "NetAsyncEndpointRdmaPostSendSglInlineFour";
    NEP->mIsNeedEncrypt = false;
    MOCKER_CPP(&RDMAWorker::PostSendSglInline)
        .stubs()
        .will(returnValue(1));

    UBSHcomNetTransOpInfo opInfo{};
    int ret = NEP->PostSendSglInline(0, request, opInfo);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaPostSendSglInlineFive)
{
    name = "NetAsyncEndpointRdmaPostSendSglInlineFive";
    NEP->mIsNeedEncrypt = false;
    MOCKER_CPP(&RDMAWorker::PostSendSglInline)
        .stubs()
        .will(returnValue(static_cast<RResult>(RR_QP_POST_SEND_WR_FULL)))
        .then(returnValue(0));

    UBSHcomNetTransOpInfo opInfo{};
    int ret = NEP->PostSendSglInline(0, request, opInfo);
    EXPECT_EQ(ret, 0);
}

TEST_F(TestNetRdmaAsyncEndpoint, NetAsyncEndpointRdmaPostSendSglInlineValidateFail)
{
    NEP->mState.Set(NEP_BROKEN);
    NEP->mIsNeedEncrypt = false;
    UBSHcomNetTransOpInfo opInfo{};
    int ret = NEP->PostSendSglInline(0, request, opInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));
    NEP->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestNetRdmaAsyncEndpoint, QpInitializeFail)
{
    int ret;

    MOCKER_CPP(&RDMAQp::CreateIbvQp).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&RDMAQp::CreateQpMr).stubs().will(returnValue(1));
    MOCKER_CPP(HcomIbv::DestroyQp).stubs().will(returnValue(0));

    ret = qp->Initialize();
    EXPECT_NE(ret, 0);

    ret = qp->Initialize();
    EXPECT_NE(ret, 0);
}

TEST_F(TestNetRdmaAsyncEndpoint, TestCreateFailed)
{
    auto ret = RDMAAsyncEndPoint::Create("", nullptr, ep);
    EXPECT_EQ(ret, static_cast<int>(RR_PARAM_INVALID));

    MOCKER_CPP(RDMAWorker::CreateQP).stubs().will(returnValue(static_cast<int>(RR_PARAM_INVALID)));
    ret = RDMAAsyncEndPoint::Create("name", mWorker, ep);
    EXPECT_EQ(ret, static_cast<int>(RR_PARAM_INVALID));

    QpOptions option {};
    RDMASyncEndpoint *syncEp = nullptr;
    ret = RDMASyncEndpoint::Create("name", nullptr, EVENT_POLLING, 0, option, syncEp);
    EXPECT_EQ(ret, static_cast<int>(RR_PARAM_INVALID));
}

TEST_F(TestNetRdmaAsyncEndpoint, TestRDMAEndpointFunction)
{
    RDMAAsyncEndPoint asyncEp {"name", nullptr, nullptr};
    RDMAQpExchangeInfo info {};
    EXPECT_EQ(asyncEp.GetExchangeInfo(info), static_cast<int>(RR_QP_NOT_INITIALIZED));
    EXPECT_EQ(asyncEp.ChangeToReady(info), static_cast<int>(RR_EP_NOT_INITIALIZED));

    asyncEp.mQP = qp;

    MOCKER_CPP(RDMAQp::ReturnBuffer).stubs().will(returnValue(false));
    EXPECT_NO_FATAL_FAILURE(asyncEp.ReturnBuffer(0));
    asyncEp.mQP = nullptr;

    EXPECT_EQ(asyncEp.Initialize(), static_cast<int>(RR_EP_NOT_INITIALIZED));
    asyncEp.mQP = qp;
    MOCKER_CPP(RDMAQp::Initialize).stubs().will(returnValue(static_cast<int>(RR_PARAM_INVALID)));
    EXPECT_EQ(asyncEp.Initialize(), static_cast<int>(RR_PARAM_INVALID));
    asyncEp.mQP = nullptr;
}

TEST_F(TestNetRdmaAsyncEndpoint, TestRDMASizeValidateFail)
{
    request.size = NN_NO2;
    uint32_t allowedSize = NN_NO1;
    AesGcm128 mAes;
    EXPECT_EQ(SizeValidate(request, allowedSize, false, mAes), NN_TWO_SIDE_MESSAGE_TOO_LARGE);
}

TEST_F(TestNetRdmaAsyncEndpoint, TestRDMAPostSendValidationFail)
{
    UBSHcomNetAtomicState<UBSHcomNetEndPointState> state{NEP_ESTABLISHED};
    request.size = NN_NO2;
    uint32_t allowedSize = NN_NO1;
    AesGcm128 mAes;
    EXPECT_EQ(PostSendValidation(state, 1, mDriver, 1, request, allowedSize, false, mAes),
        NN_TWO_SIDE_MESSAGE_TOO_LARGE);

    allowedSize = NN_NO3;
    EXPECT_EQ(PostSendValidation(state, 1, mDriver, MAX_OPCODE + 1, request, allowedSize, false, mAes),
        NN_INVALID_OPCODE);
}

TEST_F(TestNetRdmaAsyncEndpoint, TestRDMAPostSendRawValidationFail)
{
    UBSHcomNetAtomicState<UBSHcomNetEndPointState> state{NEP_BROKEN};
    uint32_t allowedSize = NN_NO1;
    AesGcm128 mAes;
    EXPECT_EQ(PostSendRawValidation(state, 1, mDriver, 1, request, allowedSize, false, mAes),
        NN_EP_NOT_ESTABLISHED);

    state.Set(NEP_ESTABLISHED);
    request.size = NN_NO2;
    allowedSize = NN_NO1;
    EXPECT_EQ(PostSendRawValidation(state, 1, mDriver, 1, request, allowedSize, false, mAes),
        NN_TWO_SIDE_MESSAGE_TOO_LARGE);

    allowedSize = NN_NO3;
    EXPECT_EQ(PostSendRawValidation(state, 1, mDriver, 0, request, allowedSize, false, mAes),
        NN_PARAM_INVALID);
}

TEST_F(TestNetRdmaAsyncEndpoint, TestRDMAReadWriteValidationFail)
{
    UBSHcomNetAtomicState<UBSHcomNetEndPointState> state{NEP_BROKEN};
    EXPECT_EQ(ReadWriteValidation(state, 1, mDriver, request), NN_EP_NOT_ESTABLISHED);

    state.Set(NEP_ESTABLISHED);
    request.size = NET_SGE_MAX_SIZE + 1;
    EXPECT_EQ(ReadWriteValidation(state, 1, mDriver, request), NN_PARAM_INVALID);
}

TEST_F(TestNetRdmaAsyncEndpoint, TestRDMASglValidationFail)
{
    size_t totalSize = 0;
    sglRequest.iov[0].size = NET_SGE_MAX_SIZE + 1;
    EXPECT_EQ(SglValidation(sglRequest, totalSize, mDriver), NN_PARAM_INVALID);
}

TEST_F(TestNetRdmaAsyncEndpoint, TestRDMAPostSendSglValidationFail)
{
    size_t totalSize = 0;
    UBSHcomNetAtomicState<UBSHcomNetEndPointState> state{NEP_BROKEN};
    uint32_t allowedSize = NN_NO1;
    AesGcm128 mAes;
    EXPECT_EQ(PostSendSglValidation(state, 1, mDriver, 1, sglRequest, allowedSize, totalSize, false, mAes),
        NN_EP_NOT_ESTABLISHED);

    state.Set(NEP_ESTABLISHED);
    EXPECT_EQ(PostSendSglValidation(state, 1, mDriver, 0, sglRequest, allowedSize, totalSize, false, mAes),
        NN_PARAM_INVALID);

    allowedSize = 0;
    MOCKER_CPP(&NetDriverRDMAWithOob::ValidateMemoryRegion,
               NResult(NetDriverRDMAWithOob::*)(uint64_t, uintptr_t, uint64_t))
        .stubs()
        .will(returnValue(0));
    MOCKER_CPP(&AesGcm128::EstimatedEncryptLen).stubs().will(returnValue(static_cast<size_t>(1)));
    EXPECT_EQ(PostSendSglValidation(state, 1, mDriver, 1, sglRequest, allowedSize, totalSize, true, mAes),
        NN_TWO_SIDE_MESSAGE_TOO_LARGE);
}

TEST_F(TestNetRdmaAsyncEndpoint, TestRDMAEncryptRawSglSuccess)
{
    UBSHcomNetTransRequest tlsReq {};
    uintptr_t mrBufAddress = 0;
    size_t size = 0;
    AesGcm128 mAes;
    NetSecrets mSecrets;

    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs().will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&AesGcm128::Encrypt, bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs().will(returnValue(true));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::ReturnBuffer).stubs().will(returnValue(true));

    EXPECT_EQ(EncryptRawSgl(tlsReq, mrBufAddress, size, mAes, mDriver, sglRequest, mSecrets), NN_OK);
}

TEST_F(TestNetRdmaAsyncEndpoint, TestRDMAEncryptRawSglGetBufferFail)
{
    UBSHcomNetTransRequest tlsReq {};
    uintptr_t mrBufAddress = 0;
    size_t size = 0;
    AesGcm128 mAes;
    NetSecrets mSecrets;

    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs().will(returnValue(false));

    EXPECT_EQ(EncryptRawSgl(tlsReq, mrBufAddress, size, mAes, mDriver, sglRequest, mSecrets), NN_GET_BUFF_FAILED);
}

TEST_F(TestNetRdmaAsyncEndpoint, TestRDMAEncryptRawSglMemCpyFail)
{
    UBSHcomNetTransRequest tlsReq {};
    uintptr_t mrBufAddress = 0;
    size_t size = 0;
    AesGcm128 mAes;
    NetSecrets mSecrets;

    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs().will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::ReturnBuffer).stubs().will(returnValue(true));

    EXPECT_EQ(EncryptRawSgl(tlsReq, mrBufAddress, size, mAes, mDriver, sglRequest, mSecrets), NN_INVALID_PARAM);
}

TEST_F(TestNetRdmaAsyncEndpoint, TestRDMAEncryptRawSglGetSecondBufferFail)
{
    UBSHcomNetTransRequest tlsReq {};
    uintptr_t mrBufAddress = 0;
    size_t size = 0;
    AesGcm128 mAes;
    NetSecrets mSecrets;

    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs().will(returnValue(true)).then(returnValue(false));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::ReturnBuffer).stubs().will(returnValue(true));

    EXPECT_EQ(EncryptRawSgl(tlsReq, mrBufAddress, size, mAes, mDriver, sglRequest, mSecrets), NN_GET_BUFF_FAILED);
}

TEST_F(TestNetRdmaAsyncEndpoint, TestRDMAEncryptRawSglEncryptFail)
{
    UBSHcomNetTransRequest tlsReq {};
    uintptr_t mrBufAddress = 0;
    size_t size = 0;
    AesGcm128 mAes;
    NetSecrets mSecrets;

    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs().will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&AesGcm128::Encrypt, bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs().will(returnValue(false));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::ReturnBuffer).stubs().will(returnValue(true));

    EXPECT_EQ(EncryptRawSgl(tlsReq, mrBufAddress, size, mAes, mDriver, sglRequest, mSecrets), NN_ENCRYPT_FAILED);
}
}
}
//#endif
