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
#include "net_rdma_driver_oob.h"
#include "net_security_rand.h"
#include "rdma_validation.h"
#include "rdma_composed_endpoint.h"
#include "net_rdma_sync_endpoint.h"

namespace ock {
namespace hcom {

class TestNetRdmaSyncEndpoint : public testing::Test {
public:
    TestNetRdmaSyncEndpoint();
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
    RDMASyncEndpoint *ep = nullptr;
    UBSHcomNetTransRequest request;
    UBSHcomNetTransSglRequest sglRequest;
    UBSHcomNetTransSgeIov *iov = nullptr;
    NetSyncEndpoint *NEP = nullptr;
    RDMAMemoryRegionFixedBuffer *Mr = nullptr;
};

TestNetRdmaSyncEndpoint::TestNetRdmaSyncEndpoint() {}

void TestNetRdmaSyncEndpoint::SetUp()
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

    RDMAWorkerOptions options;
    NetMemPoolFixedPtr memPool;
    NetMemPoolFixedPtr sglMemPool;
    mWorker = new (std::nothrow) RDMAWorker(name, ctx, options, memPool, sglMemPool);
    ASSERT_NE(mWorker, nullptr);

    cq = new (std::nothrow) RDMACq(name, ctx, false, 0);
    ASSERT_NE(cq, nullptr);

    uint32_t mid = 0;
    QpOptions qpOptions;
    qp = new (std::nothrow) RDMAQp(name, mid, ctx, cq, qpOptions);
    ASSERT_NE(qp, nullptr);

    uint32_t rdmaOpCtxPoolSize = NN_NO1;
    RDMAPollingMode pollMode = EVENT_POLLING;
    ep = new (std::nothrow) RDMASyncEndpoint(name, ctx, pollMode, cq, qp, rdmaOpCtxPoolSize);
    ASSERT_NE(ep, nullptr);

    uint64_t id = 0;
    NEP = new (std::nothrow) NetSyncEndpoint(id, ep, mDriver, mWorkerIndex);
    NEP->mState.Set(NEP_ESTABLISHED);
    NEP->mAllowedSize = NN_NO128;
    NEP->mSegSize = NN_NO128;

    request.lAddress = reinterpret_cast<uintptr_t>(&mWorkerIndex);
    request.size = 1;
    iov = new (std::nothrow) UBSHcomNetTransSgeIov();
    sglRequest = UBSHcomNetTransSglRequest(iov, 1, 1);
}

void TestNetRdmaSyncEndpoint::TearDown()
{
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
    if (ctx != nullptr) {
        delete ctx;
        ctx = nullptr;
    }
    if (mWorker != nullptr) {
        delete mWorker;
        mWorker = nullptr;
    }

    GlobalMockObject::verify();
}
UBSHcomNetTransHeader mockSyncMrBuf{};
static bool MockGetFreeBuffer(uintptr_t &mrBufAddress)
{
    mrBufAddress = reinterpret_cast<uintptr_t>(&mockSyncMrBuf);
    return true;
}

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaUdsName)
{
    std::string ret = NEP->UdsName();
    EXPECT_EQ(ret, CONST_EMPTY_STRING);
}

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaGetSendQueueCount)
{
    int ret = NEP->GetSendQueueCount();
    EXPECT_EQ(ret, 0);
}

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaPostSendSeqFailed)
{
    name = "NetSyncEndpointRdmaPostSend";
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(returnValue(false));
    int ret = NEP->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_GET_BUFF_FAILED));
}

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaPostSendSeq)
{
    name = "NetSyncEndpointRdmaPostSend";
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));

    MOCKER_CPP(&RDMASyncEndpoint::PostSend)
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

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaPostSendSeqTwo)
{
    name = "NetSyncEndpointRdmaPostSend";
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&RDMASyncEndpoint::PostSend).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));

    NEP->mIsNeedEncrypt = 0;
    int ret = NEP->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = NEP->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaPostSendInfoFailed)
{
    name = "NetSyncEndpointRdmaPostSend";
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(returnValue(false));
    UBSHcomNetTransOpInfo OpInfo{};
    int ret = NEP->PostSend(0, request, OpInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_GET_BUFF_FAILED));
}

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaPostSendOpInfo)
{
    name = "NetSyncEndpointRdmaPostSend";
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));

    MOCKER_CPP(&RDMASyncEndpoint::PostSend)
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

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaPostSendOpInfoTwo)
{
    name = "NetSyncEndpointRdmaPostSend";
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&RDMASyncEndpoint::PostSend).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));

    UBSHcomNetTransOpInfo OpInfo{};
    NEP->mIsNeedEncrypt = 0;
    int ret = NEP->PostSend(0, request, OpInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_NO1));

    ret = NEP->PostSend(0, request, OpInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaPostSendOpInfoValidateFail)
{
    NEP->mState.Set(NEP_BROKEN);
    UBSHcomNetTransOpInfo OpInfo{};
    int ret = NEP->PostSend(0, request, OpInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));
    NEP->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaPostSendOpInfoWithHeaderRaw)
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

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaPostSendOpInfoWithHeaderNull)
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

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaPostSendOpInfoWithHeaderValidateFailed)
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

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaPostSendOpInfoWithHeaderBuffer)
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

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaPostSendOpInfoWithHeaderMemcpy)
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

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaPostSendOpInfoWithHeaderEpSend)
{
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::GetFreeBuffer, bool(RDMAMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(invoke(MockGetFreeBuffer));
    MOCKER_CPP(&RDMASyncEndpoint::PostSend)
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

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaPostSendRawSgl)
{
    name = "NetSyncEndpointRdmaPostSendRawSgl";
    MOCKER_CPP(&RDMASyncEndpoint::PostSendSgl).stubs().will(returnValue(1)).then(returnValue(0));

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

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaPostSendRawSglFail)
{
    MOCKER_CPP(&NetDriverRDMAWithOob::ValidateMemoryRegion,
               NResult(NetDriverRDMAWithOob::*)(uint64_t, uintptr_t, uint64_t))
        .stubs()
        .will(returnValue(0));

    MOCKER_CPP(&AesGcm128::EstimatedEncryptLen).stubs().will(returnValue(static_cast<size_t>(0)));
    NEP->mIsNeedEncrypt = true;
    int ret = NEP->PostSendRawSgl(sglRequest, 1);
    EXPECT_EQ(ret, static_cast<int>(NN_ENCRYPT_FAILED));

    NEP->mState.Set(NEP_BROKEN);
    ret = NEP->PostSendRawSgl(sglRequest, 1);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));
    NEP->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestNetRdmaSyncEndpoint, ComposedEndpointRdmaPostSendLKeyFail)
{
    name = "ComposedEndpointRdmaPostSendLKeyFail";
    request.lKey = UINT64_MAX;
    int ret = ep->PostSend(request, 0);
    EXPECT_EQ(ret, static_cast<int>(RR_PARAM_INVALID));
}

TEST_F(TestNetRdmaSyncEndpoint, ComposedEndpointRdmaPostSendSgl)
{
    name = "ComposedEndpointRdmaPostSendSgl";
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));
    MOCKER_CPP(&AesGcm128::EstimatedEncryptLen).stubs().will(returnValue(static_cast<size_t>(0)));

    int ret = ep->PostSendSgl(sglRequest, request, 0, false);
    EXPECT_EQ(ret, static_cast<int>(RR_PARAM_INVALID));
    if (ep->mQP != nullptr) {
        delete ep->mQP;
        ep->mQP = nullptr;
    }
    ret = ep->PostSendSgl(sglRequest, request, 0, false);
    EXPECT_EQ(ret, static_cast<int>(RR_PARAM_INVALID));
}
TEST_F(TestNetRdmaSyncEndpoint, ComposedEndpointRdmaPostSendSglTwo)
{
    name = "ComposedEndpointRdmaPostSendSglTwo";
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0)).then(returnValue(1));
    sglRequest.upCtxSize = 1;
    int ret = ep->PostSendSgl(sglRequest, request, 0, false);
    EXPECT_EQ(ret, static_cast<int>(RR_PARAM_INVALID));
}

TEST_F(TestNetRdmaSyncEndpoint, ComposedEndpointRdmaPostSendSglLKeyFail)
{
    name = "ComposedEndpointRdmaPostSendSglLKeyFail";
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));
    request.lKey = UINT64_MAX;
    int ret = ep->PostSendSgl(sglRequest, request, 0, false);
    EXPECT_EQ(ret, static_cast<int>(RR_PARAM_INVALID));
}

TEST_F(TestNetRdmaSyncEndpoint, ComposedEndpointRdmaPostSendSglThree)
{
    name = "ComposedEndpointRdmaPostSendSglThree";
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));
    sglRequest.upCtxSize = 0;
    MOCKER_CPP(&RDMAQp::PostSend).stubs().will(returnValue(1));
    MOCKER_CPP(&RDMAQp::PostSendSgl).stubs().will(returnValue(0));
    int ret = ep->PostSendSgl(sglRequest, request, 0, true);
    EXPECT_EQ(ret, 1);
    ret = ep->PostSendSgl(sglRequest, request, 0, false);
    EXPECT_EQ(ret, 0);
}
TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaSetEpOption)
{
    UBSHcomEpOptions epOptions;
    int ret = NEP->SetEpOption(epOptions);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaEstimatedEncryptLen)
{
    int ret = NEP->EstimatedEncryptLen(0);
    EXPECT_EQ(ret, 0);
    NEP->mIsNeedEncrypt = 0;
    ret = NEP->EstimatedEncryptLen(1);
    EXPECT_EQ(ret, 0);
}
TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaEstimatedEncryptLenTwo)
{
    NEP->mIsNeedEncrypt = 1;
    MOCKER_CPP(&AesGcm128::EstimatedEncryptLen).stubs().will(returnValue(1));
    int ret = NEP->EstimatedEncryptLen(1);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaEncrypt)
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

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaEncryptTwo)
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

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaEstimatedDecryptLen)
{
    NEP->mIsNeedEncrypt = 0;
    int ret = NEP->EstimatedDecryptLen(0);
    EXPECT_EQ(ret, 0);

    NEP->mIsNeedEncrypt = 1;
    MOCKER_CPP(&AesGcm128::GetRawLen).stubs().will(returnValue(1));
    ret = NEP->EstimatedDecryptLen(0);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaDecrypt)
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

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaDecryptTwo)
{
    uint64_t rawLen = 0;
    MOCKER_CPP(&AesGcm128::Decrypt, bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(true));

    NEP->mIsNeedEncrypt = 1;
    int ret = NEP->Decrypt(reinterpret_cast<void *>(0), 0, reinterpret_cast<void *>(0), rawLen);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
}

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaGetRemoteUdsIdInfo)
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

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaGetRemoteUdsIdInfoTwo)
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

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaGetPeerIpPort)
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

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaGetPeerIpPortTwo)
{
    NEP->mEp->mQP->mPeerIpPort = "0.0.0.0";
    int ret = NEP->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, false);
    NEP->mEp->mQP->mPeerIpPort = "0.0.0.0:sss";
    ret = NEP->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, false);
}

TEST_F(TestNetRdmaSyncEndpoint, NetSyncEndpointRdmaGetPeerIpPortThree)
{
    NEP->mEp->mQP->mPeerIpPort = "0.0.0.0:0";
    int ret = NEP->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, false);
    NEP->mEp->mQP->mPeerIpPort = "0.0.0.0:16";
    ret = NEP->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, true);
}

TEST_F(TestNetRdmaSyncEndpoint, SyncReceiveFailWithErrorOpType)
{
    // param init
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    RDMAOpContextInfo opCtx{};
    opCtx.opType = RDMAOpContextInfo::SEND;
    NEP->mDelayHandleReceiveCtx = &opCtx;

    MOCKER_CPP(&RDMASyncEndpoint::RePostReceive)
    .stubs()
    .will(returnValue(0));

    NResult ret = NEP->Receive(timeout, ctx);
    EXPECT_EQ(ret, NN_ERROR);
}

TEST_F(TestNetRdmaSyncEndpoint, SyncReceiveFailWithOverDataSize)
{
    // param init
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    RDMAOpContextInfo opCtx{};
    opCtx.opType = RDMAOpContextInfo::RECEIVE;
    UBSHcomNetTransHeader header{};
    header.seqNo = 0;
    header.dataLength = NET_SGE_MAX_SIZE + NN_NO1;
    opCtx.mrMemAddr = reinterpret_cast<uintptr_t>(&header);
    NEP->mDelayHandleReceiveCtx = &opCtx;

    MOCKER_CPP(&RDMASyncEndpoint::RePostReceive)
    .stubs()
    .will(returnValue(0));

    NResult ret = NEP->Receive(timeout, ctx);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestNetRdmaSyncEndpoint, SyncReceiveFailWithErrDataLen)
{
    // param init
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    RDMAOpContextInfo opCtx{};
    opCtx.opType = RDMAOpContextInfo::RECEIVE;
    opCtx.dataSize = NN_NO1024;
    UBSHcomNetTransHeader header{};
    header.seqNo = 0;
    header.dataLength = NN_NO2048;
    opCtx.mrMemAddr = reinterpret_cast<uintptr_t>(&header);
    NEP->mDelayHandleReceiveCtx = &opCtx;

    MOCKER_CPP(&RDMASyncEndpoint::RePostReceive)
    .stubs()
    .will(returnValue(0));

    NResult ret = NEP->Receive(timeout, ctx);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestNetRdmaSyncEndpoint, SyncReceiveFailWithInvalidHeader)
{
    // param init
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    RDMAOpContextInfo opCtx{};
    opCtx.opType = RDMAOpContextInfo::RECEIVE;
    opCtx.dataSize = NN_NO1024;
    UBSHcomNetTransHeader header{};
    header.seqNo = 0;
    header.dataLength = NN_NO1024 - sizeof(UBSHcomNetTransHeader);
    opCtx.mrMemAddr = reinterpret_cast<uintptr_t>(&header);
    NEP->mDelayHandleReceiveCtx = &opCtx;

    MOCKER_CPP(&RDMASyncEndpoint::RePostReceive)
    .stubs()
    .will(returnValue(0));

    NResult ret = NEP->Receive(timeout, ctx);
    EXPECT_EQ(ret, NN_VALIDATE_HEADER_CRC_INVALID);
}

TEST_F(TestNetRdmaSyncEndpoint, TestRDMASyncEndpointFunction)
{
    RDMASyncEndpoint syncEp {"name", nullptr, EVENT_POLLING, nullptr, nullptr, 0};
    EXPECT_EQ(syncEp.Initialize(), static_cast<int>(RR_EP_NOT_INITIALIZED));
    syncEp.mQP = qp;
    EXPECT_EQ(syncEp.Initialize(), static_cast<int>(RR_EP_NOT_INITIALIZED));
    syncEp.mCq = cq;

    MOCKER_CPP(RDMACq::Initialize).stubs()
        .will(returnValue(static_cast<int>(RR_PARAM_INVALID)))
        .then(returnValue(static_cast<int>(RR_OK)));
    EXPECT_EQ(syncEp.Initialize(), static_cast<int>(RR_PARAM_INVALID));
    MOCKER_CPP(RDMAQp::Initialize).stubs()
        .will(returnValue(static_cast<int>(RR_PARAM_INVALID)))
        .then(returnValue(static_cast<int>(RR_OK)));
    EXPECT_EQ(syncEp.Initialize(), static_cast<int>(RR_PARAM_INVALID));
    EXPECT_EQ(syncEp.Initialize(), static_cast<int>(NN_INVALID_PARAM));
    syncEp.mQP = nullptr;
    syncEp.mCq = nullptr;
}

TEST_F(TestNetRdmaSyncEndpoint, PostReceiveFail)
{
    RDMASyncEndpoint syncEp {"name", nullptr, EVENT_POLLING, nullptr, nullptr, 0};
    EXPECT_EQ(syncEp.PostReceive(0, 0, 0), static_cast<int>(RR_PARAM_INVALID));
    EXPECT_EQ(syncEp.RePostReceive(nullptr), static_cast<int>(RR_PARAM_INVALID));
    RDMASendReadWriteRequest rwReq {};
    EXPECT_EQ(syncEp.PostSend(rwReq), static_cast<int>(RR_PARAM_INVALID));
    EXPECT_EQ(syncEp.PostRead(rwReq), static_cast<int>(RR_PARAM_INVALID));
    EXPECT_EQ(syncEp.PostWrite(rwReq), static_cast<int>(RR_PARAM_INVALID));
}

TEST_F(TestNetRdmaSyncEndpoint, PostOneSideSglFail)
{
    RDMASyncEndpoint syncEp {"name", nullptr, EVENT_POLLING, nullptr, nullptr, 0};
    RDMASendSglRWRequest sglRwReq {};
    EXPECT_EQ(syncEp.PostOneSideSgl(sglRwReq), static_cast<int>(RR_PARAM_INVALID));

    RDMASgeCtxInfo sge {};
    uint64_t ctxArr[NET_SGE_MAX_IOV];
    EXPECT_EQ(syncEp.CreateOneSideCtx(sge, nullptr, 0, ctxArr, false), static_cast<int>(RR_PARAM_INVALID));

    RDMAOpContextInfo *opCtxInfo = nullptr;
    uint32_t immData = 0;
    EXPECT_EQ(syncEp.PollingCompletion(opCtxInfo, 0, immData), static_cast<int>(RR_EP_NOT_INITIALIZED));
}

TEST_F(TestNetRdmaSyncEndpoint, SyncPollingCompletionContextInfoNull)
{
    RDMAOpContextInfo *opCtx = nullptr;
    uint32_t immData = 0;
    MOCKER(RDMACq::EventProgressV).stubs().will(returnValue(0));
    RResult ret = ep->PollingCompletion(opCtx, 0, immData);
    EXPECT_EQ(ret, RR_CQ_WC_WRONG);
}
}
}
//#endif
