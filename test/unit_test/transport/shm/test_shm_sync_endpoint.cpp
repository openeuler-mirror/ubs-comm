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
#include <cstdint>
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "securec.h"
#include "hcom_def.h"
#include "hcom_log.h"
#include "net_shm_sync_endpoint.h"
#include "net_shm_async_endpoint.h"
#include "shm_common.h"


namespace ock {
namespace hcom {

constexpr uint32_t SYNC_EP_SHM_ALLOWD_SIZE = 256;
constexpr uint32_t REQUEST_SIZE = 128;
static UBSHcomNetTransHeader mockReq{};

class TestShmSyncEndpointNew : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);
    static void SetUpTestSuite() {}
    static void TearDownTestSuite() {}

    NetSyncEndpointShm* mShmSyncEp = nullptr;
    UBSHcomNetTransRequest mReq;
};

static HResult MockDCGetFreeBuck(uintptr_t &address, uint64_t &offsetToBase,
    uint16_t waitPeriodUs = NN_NO100, int32_t timeoutSecond = -1)
{
    static char buffer[SYNC_EP_SHM_ALLOWD_SIZE];
    auto ret = memset_s(buffer, SYNC_EP_SHM_ALLOWD_SIZE, '\0', SYNC_EP_SHM_ALLOWD_SIZE);
    if (ret != 0) {
        NN_LOG_ERROR("MockDCGetFreeBuck memset_s failed");
        return SH_ERROR;
    }
    address = reinterpret_cast<uintptr_t>(buffer);
    offsetToBase = 0;
    return SH_OK;
}

void TestShmSyncEndpointNew::SetUp()
{
    // create and configure NetSyncEndpointShm object
    UBSHcomNetWorkerIndex workerId;
    workerId.wholeIdx = 0;
    ShmMRHandleMap tmpShmMRHandleMap;

    ShmChannelPtr shmCh = new ShmChannel("TestShmSyncEndpoint", 0, 0, 0);
    if (shmCh == nullptr) {
        NN_LOG_ERROR("new ShmChannel failed");
        return;
    }

    ShmSyncEndpointPtr shmEp = new ShmSyncEndpoint("TestShmSyncEndpoint", 0, SHM_EVENT_POLLING);
    if (shmEp == nullptr) {
        NN_LOG_ERROR("new ShmSyncEndpoint failed");
        return;
    }

    mShmSyncEp = new (std::nothrow)
        NetSyncEndpointShm(0, shmCh.Get(), nullptr, workerId, shmEp.Get(), tmpShmMRHandleMap);
    if (mShmSyncEp == nullptr) {
        NN_LOG_ERROR("new NetSyncEndpointShm failed");
        return;
    }

    mShmSyncEp->mState.Set(NEP_ESTABLISHED);
    mShmSyncEp->mAllowedSize = SYNC_EP_SHM_ALLOWD_SIZE;
    mShmSyncEp->mIsNeedEncrypt = false;

    // create and config req
    static char buffer[REQUEST_SIZE];
    auto ret = memset_s(buffer, REQUEST_SIZE, '\0', REQUEST_SIZE);
    ASSERT_EQ(ret, 0);
    ret = memset_s(&mReq, sizeof(mReq), '\0', sizeof(mReq));
    ASSERT_EQ(ret, 0);
    mReq.lAddress = reinterpret_cast<uintptr_t>(buffer);
    mReq.size = REQUEST_SIZE;
}

void TestShmSyncEndpointNew::TearDown()
{
    if (mShmSyncEp != nullptr) {
        delete mShmSyncEp;
        mShmSyncEp = nullptr;
    }
    GlobalMockObject::verify();
}

TEST_F(TestShmSyncEndpointNew, PostSendFailWhenValidateStateFail)
{
    mShmSyncEp->mState.Set(NEP_BROKEN);
    NResult ret = mShmSyncEp->PostSend(0, mReq, 0);
    EXPECT_EQ(ret, NN_EP_NOT_ESTABLISHED);
    mShmSyncEp->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestShmSyncEndpointNew, PostSendFailWhenMessageTooLarge)
{
    mReq.size = SYNC_EP_SHM_ALLOWD_SIZE + 1;
    NResult ret = mShmSyncEp->PostSend(0, mReq, 0);
    ASSERT_EQ(ret, NN_TWO_SIDE_MESSAGE_TOO_LARGE);
}

TEST_F(TestShmSyncEndpointNew, PostSendFailWhenGetFreeBuckFail)
{
    MOCKER_CPP(&ShmChannel::DCGetFreeBuck)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_NOT_INITIALIZED)));

    NResult ret = mShmSyncEp->PostSend(0, mReq, 0);
    ASSERT_EQ(ret, SH_NOT_INITIALIZED);
}

TEST_F(TestShmSyncEndpointNew, PostSendFailWhenEncryptFail)
{
    MOCKER_CPP(&ShmChannel::DCGetFreeBuck,
        HResult (ShmChannel::*)(uintptr_t&, uint64_t&, uint16_t, int32_t))
        .stubs()
        .will(invoke(MockDCGetFreeBuck));

    MOCKER_CPP(&ShmChannel::DCMarkBuckFree)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_OK)));

    MOCKER_CPP(&AesGcm128::EstimatedEncryptLen)
        .stubs()
        .will(returnValue(static_cast<size_t>(0)));

    MOCKER_CPP(&AesGcm128::Encrypt,
        bool (AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false));

    mShmSyncEp->mIsNeedEncrypt = true;
    NResult ret = mShmSyncEp->PostSend(0, mReq, 0);
    ASSERT_EQ(ret, NN_ENCRYPT_FAILED);
}

TEST_F(TestShmSyncEndpointNew, PostSendFailWhenMemcpyFail)
{
    MOCKER_CPP(&ShmChannel::DCGetFreeBuck,
        HResult (ShmChannel::*)(uintptr_t&, uint64_t&, uint16_t, int32_t))
        .stubs()
        .will(invoke(MockDCGetFreeBuck));

    MOCKER_CPP(&ShmChannel::DCMarkBuckFree)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_OK)));

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));

    mShmSyncEp->mIsNeedEncrypt = false;
    NResult ret = mShmSyncEp->PostSend(0, mReq, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestShmSyncEndpointNew, PostSendSuccess)
{
    MOCKER_CPP(&ShmChannel::DCGetFreeBuck,
        HResult (ShmChannel::*)(uintptr_t&, uint64_t&, uint16_t, int32_t))
        .stubs()
        .will(invoke(MockDCGetFreeBuck));
    
    MOCKER_CPP(&ShmSyncEndpoint::PostSend,
        HResult (ShmSyncEndpoint::*)(ShmChannel *, const UBSHcomNetTransRequest&, uint64_t, uint32_t, int32_t))
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_OK)));
    MOCKER(memcpy_s).stubs().will(returnValue(0));

    NResult ret = mShmSyncEp->PostSend(0, mReq, 0);
    ASSERT_EQ(ret, SH_OK);
}

TEST_F(TestShmSyncEndpointNew, PostSendOpInfoFailWhenValidateStateFail)
{
    mShmSyncEp->mState.Set(NEP_BROKEN);
    UBSHcomNetTransOpInfo opInfo{};
    NResult ret = mShmSyncEp->PostSend(0, mReq, opInfo);
    EXPECT_EQ(ret, NN_EP_NOT_ESTABLISHED);
    mShmSyncEp->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestShmSyncEndpointNew, PostSendOpInfoFailWhenValidateSizeFail)
{
    mShmSyncEp->mAllowedSize = NN_NO1;
    UBSHcomNetTransOpInfo opInfo{};
    NResult ret = mShmSyncEp->PostSend(0, mReq, opInfo);
    EXPECT_EQ(ret, NN_TWO_SIDE_MESSAGE_TOO_LARGE);
    mShmSyncEp->mAllowedSize = SYNC_EP_SHM_ALLOWD_SIZE;
}

TEST_F(TestShmSyncEndpointNew, PostSendOpInfoFailWhenGetFreeBuckFail)
{
    MOCKER_CPP(&ShmChannel::DCGetFreeBuck)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_NOT_INITIALIZED)));

    UBSHcomNetTransOpInfo opInfo{};
    NResult ret = mShmSyncEp->PostSend(0, mReq, opInfo);
    EXPECT_EQ(ret, SH_NOT_INITIALIZED);
}

TEST_F(TestShmSyncEndpointNew, PostSendOpInfoFailWhenEncryptFail)
{
    MOCKER_CPP(&ShmChannel::DCGetFreeBuck,
        HResult (ShmChannel::*)(uintptr_t&, uint64_t&, uint16_t, int32_t))
        .stubs()
        .will(invoke(MockDCGetFreeBuck));

    MOCKER_CPP(&ShmChannel::DCMarkBuckFree)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_OK)));

    MOCKER_CPP(&AesGcm128::EstimatedEncryptLen)
        .stubs()
        .will(returnValue(static_cast<size_t>(0)));

    MOCKER_CPP(&AesGcm128::Encrypt,
        bool (AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false));

    mShmSyncEp->mIsNeedEncrypt = true;
    UBSHcomNetTransOpInfo opInfo{};
    NResult ret = mShmSyncEp->PostSend(0, mReq, opInfo);
    EXPECT_EQ(ret, NN_ENCRYPT_FAILED);
}

TEST_F(TestShmSyncEndpointNew, PostSendOpInfoFailWhenMemcpyFail)
{
    MOCKER_CPP(&ShmChannel::DCGetFreeBuck,
        HResult (ShmChannel::*)(uintptr_t&, uint64_t&, uint16_t, int32_t))
        .stubs()
        .will(invoke(MockDCGetFreeBuck));

    MOCKER_CPP(&ShmChannel::DCMarkBuckFree)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_OK)));

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));

    mShmSyncEp->mIsNeedEncrypt = false;
    UBSHcomNetTransOpInfo opInfo{};
    NResult ret = mShmSyncEp->PostSend(0, mReq, opInfo);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestShmSyncEndpointNew, PostSendRawFailWhenValidateStateFail)
{
    mShmSyncEp->mState.Set(NEP_BROKEN);
    NResult ret = mShmSyncEp->PostSendRaw(mReq, 0);
    EXPECT_EQ(ret, NN_EP_NOT_ESTABLISHED);
    mShmSyncEp->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestShmSyncEndpointNew, PostSendRawFailWhenValidateSizeFail)
{
    mShmSyncEp->mSegSize = NN_NO1;
    NResult ret = mShmSyncEp->PostSendRaw(mReq, 0);
    EXPECT_EQ(ret, NN_TWO_SIDE_MESSAGE_TOO_LARGE);
    mShmSyncEp->mSegSize = SYNC_EP_SHM_ALLOWD_SIZE;
}

TEST_F(TestShmSyncEndpointNew, PostSendRawFailWhenMemcpyFail)
{
    MOCKER_CPP(&ShmChannel::DCGetFreeBuck,
        HResult (ShmChannel::*)(uintptr_t&, uint64_t&, uint16_t, int32_t))
        .stubs()
        .will(invoke(MockDCGetFreeBuck));

    MOCKER_CPP(&ShmChannel::DCMarkBuckFree)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_OK)));

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));

    mShmSyncEp->mIsNeedEncrypt = false;
    mShmSyncEp->mSegSize = SYNC_EP_SHM_ALLOWD_SIZE;
    NResult ret = mShmSyncEp->PostSendRaw(mReq, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestShmSyncEndpointNew, PostSendRawSglFailWhenValidateFail)
{
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    mShmSyncEp->mState.Set(NEP_BROKEN);
    NResult ret = mShmSyncEp->PostSendRawSgl(sglReq, 1);
    EXPECT_EQ(ret, NN_EP_NOT_ESTABLISHED);
    mShmSyncEp->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestShmSyncEndpointNew, PostSendRawSglFailWhenGetFreeBuckFail)
{
    MOCKER_CPP(&ShmChannel::DCGetFreeBuck)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_NOT_INITIALIZED)));
    MOCKER_CPP(&NetDriverShmWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));

    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    NResult ret = mShmSyncEp->PostSendRawSgl(sglReq, 1);
    EXPECT_EQ(ret, SH_NOT_INITIALIZED);
}

TEST_F(TestShmSyncEndpointNew, PostSendRawSglFailWhenEncryptFail)
{
    MOCKER_CPP(&ShmChannel::DCGetFreeBuck,
        HResult (ShmChannel::*)(uintptr_t&, uint64_t&, uint16_t, int32_t))
        .stubs()
        .will(invoke(MockDCGetFreeBuck));

    MOCKER_CPP(&ShmChannel::DCMarkBuckFree)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_OK)));

    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed)
        .stubs()
        .will(returnValue(false))
        .then(returnValue(true));

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));

    MOCKER_CPP(&NetDriverShmWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));

    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    mShmSyncEp->mIsNeedEncrypt = true;
    NResult ret = mShmSyncEp->PostSendRawSgl(sglReq, 1);
    EXPECT_EQ(ret, NN_MALLOC_FAILED);

    ret = mShmSyncEp->PostSendRawSgl(sglReq, 1);
    EXPECT_EQ(ret, NN_ERROR);
}

TEST_F(TestShmSyncEndpointNew, PostSendRawSglFailWhenMemcpyFail)
{
    MOCKER_CPP(&ShmChannel::DCGetFreeBuck,
        HResult (ShmChannel::*)(uintptr_t&, uint64_t&, uint16_t, int32_t))
        .stubs()
        .will(invoke(MockDCGetFreeBuck));

    MOCKER_CPP(&ShmChannel::DCMarkBuckFree)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_OK)));

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));

    MOCKER_CPP(&NetDriverShmWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));

    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    mShmSyncEp->mIsNeedEncrypt = false;
    NResult ret = mShmSyncEp->PostSendRawSgl(sglReq, 1);
    EXPECT_EQ(ret, NN_ERROR);
}

TEST_F(TestShmSyncEndpointNew, PostReadValidateFail)
{
    mShmSyncEp->mState.Set(NEP_BROKEN);
    NResult ret = mShmSyncEp->PostRead(mReq);
    EXPECT_EQ(ret, NN_EP_NOT_ESTABLISHED);
    mShmSyncEp->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestShmSyncEndpointNew, PostReadSglValidateFail)
{
    mShmSyncEp->mState.Set(NEP_BROKEN);
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    NResult ret = mShmSyncEp->PostRead(sglReq);
    EXPECT_EQ(ret, NN_EP_NOT_ESTABLISHED);
    mShmSyncEp->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestShmSyncEndpointNew, PostWriteValidateFail)
{
    mShmSyncEp->mState.Set(NEP_BROKEN);
    NResult ret = mShmSyncEp->PostWrite(mReq);
    EXPECT_EQ(ret, NN_EP_NOT_ESTABLISHED);
    mShmSyncEp->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestShmSyncEndpointNew, PostWriteSglValidateFail)
{
    mShmSyncEp->mState.Set(NEP_BROKEN);
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    NResult ret = mShmSyncEp->PostWrite(sglReq);
    EXPECT_EQ(ret, NN_EP_NOT_ESTABLISHED);
    mShmSyncEp->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestShmSyncEndpointNew, SendFdsFail)
{
    int fds1[NN_NO4] = {1, 2, 3, 4};
    int fds2[NN_NO4] = {0};
    uint32_t len = NN_NO4;
    MOCKER_CPP(::send).stubs().will(returnValue(0));
    EXPECT_EQ(mShmSyncEp->SendFds(fds1, len), NN_ERROR);
    EXPECT_EQ(mShmSyncEp->SendFds(fds2, len), NN_INVALID_PARAM);
}

static HResult MockGetPeerDataAddressByOffset(uint64_t offset, uintptr_t &address)
{
    address = reinterpret_cast<uintptr_t>(&mockReq);
    offset = 0;
    return 0;
}

TEST_F(TestShmSyncEndpointNew, ReceiveRawPeerChannelAddressFail)
{
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    mShmSyncEp->mShmEp->mName = "Test";
    mShmSyncEp->mExistDelayEvent = true;
    mShmSyncEp->mDelayHandleReceiveEvent.peerChannelAddress = 0;
    EXPECT_EQ(mShmSyncEp->ReceiveRaw(timeout, ctx), NN_ERROR);
}

TEST_F(TestShmSyncEndpointNew, ReceiveRawGetPeerDataAddressByOffsetAndOpTypeFail)
{
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    mShmSyncEp->mExistDelayEvent = true;
    mShmSyncEp->mDelayHandleReceiveEvent.peerChannelAddress = reinterpret_cast<uintptr_t>(&mockReq);
    MOCKER_CPP(&ShmChannel::GetPeerDataAddressByOffset)
        .stubs()
        .will(returnValue(1))
        .then(invoke(MockGetPeerDataAddressByOffset));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree)
        .stubs()
        .will(returnValue(0));

    EXPECT_EQ(mShmSyncEp->ReceiveRaw(timeout, ctx), 1);
    mShmSyncEp->mExistDelayEvent = true;
    EXPECT_EQ(mShmSyncEp->ReceiveRaw(timeout, ctx), NN_ERROR);
}

TEST_F(TestShmSyncEndpointNew, ReceiveRawNotExistDelayEvent)
{
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    mShmSyncEp->mExistDelayEvent = false;
    MOCKER_CPP(&ShmSyncEndpoint::Receive).stubs().will(returnValue(1));
    EXPECT_EQ(mShmSyncEp->ReceiveRaw(timeout, ctx), 1);
}

TEST_F(TestShmSyncEndpointNew, ReceiveRawSeqNoErr)
{
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    mShmSyncEp->mExistDelayEvent = true;
    mShmSyncEp->mDelayHandleReceiveEvent.peerChannelAddress = reinterpret_cast<uintptr_t>(&mockReq);
    mShmSyncEp->mDelayHandleReceiveEvent.opType = ShmOpContextInfo::SH_RECEIVE;
    mShmSyncEp->mLastSendSeqNo = 1;
    MOCKER_CPP(&ShmChannel::GetPeerDataAddressByOffset)
        .stubs()
        .will(invoke(MockGetPeerDataAddressByOffset));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree)
        .stubs()
        .will(returnValue(0));

    EXPECT_EQ(mShmSyncEp->ReceiveRaw(timeout, ctx), NN_SEQ_NO_NOT_MATCHED);
}

TEST_F(TestShmSyncEndpointNew, ReceiveRawEncryptAllocateFail)
{
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    mockReq.dataLength = NN_NO1024 - sizeof(UBSHcomNetTransHeader);
    mShmSyncEp->mExistDelayEvent = true;
    mShmSyncEp->mIsNeedEncrypt = true;
    mShmSyncEp->mDelayHandleReceiveEvent.peerChannelAddress = reinterpret_cast<uintptr_t>(&mockReq);
    mShmSyncEp->mDelayHandleReceiveEvent.opType = ShmOpContextInfo::SH_RECEIVE;
    mShmSyncEp->mLastSendSeqNo = 0;
    mShmSyncEp->mDelayHandleReceiveEvent.dataSize = NN_NO1024;
    MOCKER_CPP(&ShmChannel::GetPeerDataAddressByOffset)
        .stubs()
        .will(invoke(MockGetPeerDataAddressByOffset));
    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed)
        .stubs()
        .will(returnValue(false));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree)
        .stubs()
        .will(returnValue(0));

    EXPECT_EQ(mShmSyncEp->ReceiveRaw(timeout, ctx), NN_MALLOC_FAILED);
}

TEST_F(TestShmSyncEndpointNew, ReceiveRawDecryptFail)
{
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    mockReq.dataLength = NN_NO1024 - sizeof(UBSHcomNetTransHeader);
    mShmSyncEp->mExistDelayEvent = true;
    mShmSyncEp->mIsNeedEncrypt = true;
    mShmSyncEp->mDelayHandleReceiveEvent.peerChannelAddress = reinterpret_cast<uintptr_t>(&mockReq);
    mShmSyncEp->mDelayHandleReceiveEvent.opType = ShmOpContextInfo::SH_RECEIVE;
    mShmSyncEp->mLastSendSeqNo = 0;
    mShmSyncEp->mDelayHandleReceiveEvent.dataSize = NN_NO1024;
    MOCKER_CPP(&ShmChannel::GetPeerDataAddressByOffset)
        .stubs()
        .will(invoke(MockGetPeerDataAddressByOffset));
    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed)
        .stubs()
        .will(returnValue(true));
    MOCKER_CPP(&AesGcm128::Decrypt, bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree)
        .stubs()
        .will(returnValue(0));

    EXPECT_EQ(mShmSyncEp->ReceiveRaw(timeout, ctx), NN_DECRYPT_FAILED);
}

TEST_F(TestShmSyncEndpointNew, ReceiveRawDecryptSuccess)
{
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    mockReq.dataLength = NN_NO1024 - sizeof(UBSHcomNetTransHeader);
    mShmSyncEp->mExistDelayEvent = true;
    mShmSyncEp->mIsNeedEncrypt = true;
    mShmSyncEp->mDelayHandleReceiveEvent.peerChannelAddress = reinterpret_cast<uintptr_t>(&mockReq);
    mShmSyncEp->mDelayHandleReceiveEvent.opType = ShmOpContextInfo::SH_RECEIVE;
    mShmSyncEp->mLastSendSeqNo = 0;
    mShmSyncEp->mDelayHandleReceiveEvent.dataSize = NN_NO1024;
    MOCKER_CPP(&ShmChannel::GetPeerDataAddressByOffset)
        .stubs()
        .will(invoke(MockGetPeerDataAddressByOffset));
    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed)
        .stubs()
        .will(returnValue(true));
    MOCKER_CPP(&AesGcm128::Decrypt, bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(true));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree)
        .stubs()
        .will(returnValue(0));

    EXPECT_EQ(mShmSyncEp->ReceiveRaw(timeout, ctx), NN_OK);
}

TEST_F(TestShmSyncEndpointNew, ReceiveRawAllocateFail)
{
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    mockReq.dataLength = NN_NO1024 - sizeof(UBSHcomNetTransHeader);
    mShmSyncEp->mExistDelayEvent = true;
    mShmSyncEp->mIsNeedEncrypt = false;
    mShmSyncEp->mDelayHandleReceiveEvent.peerChannelAddress = reinterpret_cast<uintptr_t>(&mockReq);
    mShmSyncEp->mDelayHandleReceiveEvent.opType = ShmOpContextInfo::SH_RECEIVE;
    mShmSyncEp->mLastSendSeqNo = 0;
    mShmSyncEp->mDelayHandleReceiveEvent.dataSize = NN_NO1024;
    MOCKER_CPP(&ShmChannel::GetPeerDataAddressByOffset)
        .stubs()
        .will(invoke(MockGetPeerDataAddressByOffset));
    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed)
        .stubs()
        .will(returnValue(false));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree)
        .stubs()
        .will(returnValue(0));

    EXPECT_EQ(mShmSyncEp->ReceiveRaw(timeout, ctx), NN_MALLOC_FAILED);
}

TEST_F(TestShmSyncEndpointNew, ReceiveRawMemcpyFail)
{
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    mockReq.dataLength = NN_NO1024 - sizeof(UBSHcomNetTransHeader);
    mShmSyncEp->mExistDelayEvent = true;
    mShmSyncEp->mIsNeedEncrypt = false;
    mShmSyncEp->mDelayHandleReceiveEvent.peerChannelAddress = reinterpret_cast<uintptr_t>(&mockReq);
    mShmSyncEp->mDelayHandleReceiveEvent.opType = ShmOpContextInfo::SH_RECEIVE;
    mShmSyncEp->mLastSendSeqNo = 0;
    mShmSyncEp->mDelayHandleReceiveEvent.dataSize = NN_NO1024;
    MOCKER_CPP(&ShmChannel::GetPeerDataAddressByOffset)
        .stubs()
        .will(invoke(MockGetPeerDataAddressByOffset));
    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed)
        .stubs()
        .will(returnValue(true));
    MOCKER_CPP(&memcpy_s)
        .stubs()
        .will(returnValue(1));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree)
        .stubs()
        .will(returnValue(0));

    EXPECT_EQ(mShmSyncEp->ReceiveRaw(timeout, ctx), NN_INVALID_PARAM);
}

}
}