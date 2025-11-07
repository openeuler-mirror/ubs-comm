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
#include "shm_validation.h"


namespace ock {
namespace hcom {

constexpr uint32_t ASYNC_EP_SHM_ALLOWD_SIZE = 256;
constexpr uint32_t REQUEST_SIZE = 128;

class TestShmAsyncEndpoint : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);
    static void SetUpTestSuite() {}
    static void TearDownTestSuite() {}

    NetAsyncEndpointShm* mShmAsyncEp = nullptr;
    UBSHcomNetTransRequest mReq;
};

static HResult MockDCGetFreeBuck(uintptr_t &address, uint64_t &offsetToBase,
    uint16_t waitPeriodUs = NN_NO100, int32_t timeoutSecond = -1)
{
    static UBSHcomNetTransHeader mockAsyncBuf{};
    address = reinterpret_cast<uintptr_t>(&mockAsyncBuf);
    offsetToBase = 0;
    return SH_OK;
}

void TestShmAsyncEndpoint::SetUp()
{
    // create and configure NetSyncEndpointShm object
    UBSHcomNetWorkerIndex workerId;
    workerId.wholeIdx = 0;
    ShmMRHandleMap tmpShmMRHandleMap;

    ShmChannelPtr shmCh = new ShmChannel("TestShmAsyncEndpoint", 0, 0, 0);
    if (shmCh == nullptr) {
        NN_LOG_ERROR("new ShmChannel failed");
        return;
    }

    mShmAsyncEp = new (std::nothrow) NetAsyncEndpointShm(0, shmCh.Get(), nullptr, nullptr, workerId, tmpShmMRHandleMap);
    if (mShmAsyncEp == nullptr) {
        NN_LOG_ERROR("new NetSyncEndpointShm failed");
        return;
    }

    mShmAsyncEp->mState.Set(NEP_ESTABLISHED);
    mShmAsyncEp->mAllowedSize = ASYNC_EP_SHM_ALLOWD_SIZE;
    mShmAsyncEp->mIsNeedEncrypt = false;

    // create and config req
    static char buffer[REQUEST_SIZE];
    auto ret = memset_s(buffer, REQUEST_SIZE, '\0', REQUEST_SIZE);
    ASSERT_EQ(ret, 0);
    ret = memset_s(&mReq, sizeof(mReq), '\0', sizeof(mReq));
    ASSERT_EQ(ret, 0);
    mReq.lAddress = reinterpret_cast<uintptr_t>(buffer);
    mReq.size = REQUEST_SIZE;
}

void TestShmAsyncEndpoint::TearDown()
{
    if (mShmAsyncEp != nullptr) {
        delete mShmAsyncEp;
        mShmAsyncEp = nullptr;
    }
    GlobalMockObject::verify();
}

TEST_F(TestShmAsyncEndpoint, PostSendFailWhenValidateStateFail)
{
    mShmAsyncEp->mState.Set(NEP_BROKEN);
    NResult ret = mShmAsyncEp->PostSend(0, mReq, 0);
    EXPECT_EQ(ret, NN_EP_NOT_ESTABLISHED);
    mShmAsyncEp->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestShmAsyncEndpoint, PostSendFailWhenValidateSizeFail)
{
    mShmAsyncEp->mAllowedSize = NN_NO1;
    NResult ret = mShmAsyncEp->PostSend(0, mReq, 0);
    EXPECT_EQ(ret, NN_TWO_SIDE_MESSAGE_TOO_LARGE);
    mShmAsyncEp->mAllowedSize = ASYNC_EP_SHM_ALLOWD_SIZE;
}

TEST_F(TestShmAsyncEndpoint, PostSendFailWhenGetFreeBuckFail)
{
    MOCKER_CPP(&ShmChannel::DCGetFreeBuck)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_NOT_INITIALIZED)));

    NResult ret = mShmAsyncEp->PostSend(0, mReq, 0);
    EXPECT_EQ(ret, SH_NOT_INITIALIZED);
}

TEST_F(TestShmAsyncEndpoint, PostSendFailWhenMemcpyFail)
{
    MOCKER_CPP(&ShmChannel::DCGetFreeBuck,
        HResult (ShmChannel::*)(uintptr_t&, uint64_t&, uint16_t, int32_t))
        .stubs()
        .will(invoke(MockDCGetFreeBuck));

    MOCKER_CPP(&ShmChannel::DCMarkBuckFree)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_OK)));

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));

    mShmAsyncEp->mIsNeedEncrypt = false;
    NResult ret = mShmAsyncEp->PostSend(0, mReq, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestShmAsyncEndpoint, PostSendOpInfoFailWhenValidateStateFail)
{
    mShmAsyncEp->mState.Set(NEP_BROKEN);
    UBSHcomNetTransOpInfo opInfo{};
    NResult ret = mShmAsyncEp->PostSend(0, mReq, opInfo);
    EXPECT_EQ(ret, NN_EP_NOT_ESTABLISHED);
    mShmAsyncEp->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestShmAsyncEndpoint, PostSendOpInfoFailWhenValidateSizeFail)
{
    mShmAsyncEp->mAllowedSize = NN_NO1;
    UBSHcomNetTransOpInfo opInfo{};
    NResult ret = mShmAsyncEp->PostSend(0, mReq, opInfo);
    EXPECT_EQ(ret, NN_TWO_SIDE_MESSAGE_TOO_LARGE);
    mShmAsyncEp->mAllowedSize = ASYNC_EP_SHM_ALLOWD_SIZE;
}

TEST_F(TestShmAsyncEndpoint, PostSendOpInfoFailWhenGetFreeBuckFail)
{
    MOCKER_CPP(&ShmChannel::DCGetFreeBuck)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_NOT_INITIALIZED)));

    UBSHcomNetTransOpInfo opInfo{};
    NResult ret = mShmAsyncEp->PostSend(0, mReq, opInfo);
    EXPECT_EQ(ret, SH_NOT_INITIALIZED);
}

TEST_F(TestShmAsyncEndpoint, PostSendOpInfoFailWhenEncryptFail)
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

    mShmAsyncEp->mIsNeedEncrypt = true;
    UBSHcomNetTransOpInfo opInfo{};
    NResult ret = mShmAsyncEp->PostSend(0, mReq, opInfo);
    EXPECT_EQ(ret, NN_ENCRYPT_FAILED);
}

TEST_F(TestShmAsyncEndpoint, PostSendOpInfoFailWhenMemcpyFail)
{
    MOCKER_CPP(&ShmChannel::DCGetFreeBuck,
        HResult (ShmChannel::*)(uintptr_t&, uint64_t&, uint16_t, int32_t))
        .stubs()
        .will(invoke(MockDCGetFreeBuck));

    MOCKER_CPP(&ShmChannel::DCMarkBuckFree)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_OK)));

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));

    mShmAsyncEp->mIsNeedEncrypt = false;
    UBSHcomNetTransOpInfo opInfo{};
    NResult ret = mShmAsyncEp->PostSend(0, mReq, opInfo);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestShmAsyncEndpoint, PostSendOpInfoFailWhenSendFail)
{
    MOCKER_CPP(&ShmChannel::DCGetFreeBuck,
        HResult (ShmChannel::*)(uintptr_t&, uint64_t&, uint16_t, int32_t))
        .stubs()
        .will(invoke(MockDCGetFreeBuck));

    MOCKER_CPP(&ShmChannel::DCMarkBuckFree)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_OK)));

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));

    MOCKER_CPP(&ShmWorker::PostSend,
        HResult (ShmWorker::*)(ShmChannel *, const UBSHcomNetTransRequest&, uint64_t, uint32_t, int32_t))
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_OP_CTX_FULL)))
        .then(returnValue(static_cast<HResult>(SH_SEND_COMPLETION_CALLBACK_FAILURE)))
        .then(returnValue(1));

    mShmAsyncEp->mIsNeedEncrypt = false;
    UBSHcomNetTransOpInfo opInfo{};
    NResult ret = mShmAsyncEp->PostSend(0, mReq, opInfo);
    EXPECT_EQ(ret, SH_SEND_COMPLETION_CALLBACK_FAILURE);

    ret = mShmAsyncEp->PostSend(0, mReq, opInfo);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestShmAsyncEndpoint, PostSendRawFailWhenValidateStateFail)
{
    mShmAsyncEp->mState.Set(NEP_BROKEN);
    NResult ret = mShmAsyncEp->PostSendRaw(mReq, 0);
    EXPECT_EQ(ret, NN_EP_NOT_ESTABLISHED);
    mShmAsyncEp->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestShmAsyncEndpoint, PostSendRawFailWhenValidateSizeFail)
{
    mShmAsyncEp->mSegSize = NN_NO1;
    NResult ret = mShmAsyncEp->PostSendRaw(mReq, 0);
    EXPECT_EQ(ret, NN_TWO_SIDE_MESSAGE_TOO_LARGE);
    mShmAsyncEp->mSegSize = ASYNC_EP_SHM_ALLOWD_SIZE;
}

TEST_F(TestShmAsyncEndpoint, PostSendRawFailWhenGetFreeBuckFail)
{
    MOCKER_CPP(&ShmChannel::DCGetFreeBuck)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_NOT_INITIALIZED)));

    mShmAsyncEp->mSegSize = ASYNC_EP_SHM_ALLOWD_SIZE;
    NResult ret = mShmAsyncEp->PostSendRaw(mReq, 0);
    EXPECT_EQ(ret, SH_NOT_INITIALIZED);
}

TEST_F(TestShmAsyncEndpoint, PostSendRawFailWhenEncryptFail)
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

    mShmAsyncEp->mIsNeedEncrypt = true;
    mShmAsyncEp->mSegSize = ASYNC_EP_SHM_ALLOWD_SIZE;
    NResult ret = mShmAsyncEp->PostSendRaw(mReq, 0);
    EXPECT_EQ(ret, NN_ENCRYPT_FAILED);
}

TEST_F(TestShmAsyncEndpoint, PostSendRawFailWhenMemcpyFail)
{
    MOCKER_CPP(&ShmChannel::DCGetFreeBuck,
        HResult (ShmChannel::*)(uintptr_t&, uint64_t&, uint16_t, int32_t))
        .stubs()
        .will(invoke(MockDCGetFreeBuck));

    MOCKER_CPP(&ShmChannel::DCMarkBuckFree)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_OK)));

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));

    mShmAsyncEp->mIsNeedEncrypt = false;
    mShmAsyncEp->mSegSize = ASYNC_EP_SHM_ALLOWD_SIZE;
    NResult ret = mShmAsyncEp->PostSendRaw(mReq, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestShmAsyncEndpoint, PostSendRawFailWhenSendFail)
{
    MOCKER_CPP(&ShmChannel::DCGetFreeBuck,
        HResult (ShmChannel::*)(uintptr_t&, uint64_t&, uint16_t, int32_t))
        .stubs()
        .will(invoke(MockDCGetFreeBuck));

    MOCKER_CPP(&ShmChannel::DCMarkBuckFree)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_OK)));

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));

    MOCKER_CPP(&ShmWorker::PostSend,
        HResult (ShmWorker::*)(ShmChannel *, const UBSHcomNetTransRequest&, uint64_t, uint32_t, int32_t))
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_OP_CTX_FULL)))
        .then(returnValue(static_cast<HResult>(SH_SEND_COMPLETION_CALLBACK_FAILURE)))
        .then(returnValue(1));

    mShmAsyncEp->mIsNeedEncrypt = false;
    mShmAsyncEp->mSegSize = ASYNC_EP_SHM_ALLOWD_SIZE;
    NResult ret = mShmAsyncEp->PostSendRaw(mReq, 0);
    EXPECT_EQ(ret, SH_SEND_COMPLETION_CALLBACK_FAILURE);

    ret = mShmAsyncEp->PostSendRaw(mReq, 0);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestShmAsyncEndpoint, PostSendRawSglFailWhenValidateFail)
{
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    mShmAsyncEp->mState.Set(NEP_BROKEN);
    NResult ret = mShmAsyncEp->PostSendRawSgl(sglReq, 1);
    EXPECT_EQ(ret, NN_EP_NOT_ESTABLISHED);
    mShmAsyncEp->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestShmAsyncEndpoint, PostSendRawSglFailWhenGetFreeBuckFail)
{
    MOCKER_CPP(&ShmChannel::DCGetFreeBuck)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_NOT_INITIALIZED)));
    MOCKER_CPP(&NetDriverShmWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));

    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    NResult ret = mShmAsyncEp->PostSendRawSgl(sglReq, 1);
    EXPECT_EQ(ret, SH_NOT_INITIALIZED);
}

TEST_F(TestShmAsyncEndpoint, PostSendRawSglFailWhenEncryptFail)
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
        .will(returnValue(true));

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));

    MOCKER_CPP(&AesGcm128::Encrypt,
        bool (AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false));

    MOCKER_CPP(&NetDriverShmWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));

    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    mShmAsyncEp->mIsNeedEncrypt = true;
    NResult ret = mShmAsyncEp->PostSendRawSgl(sglReq, 1);
    EXPECT_EQ(ret, NN_ENCRYPT_FAILED);
}

TEST_F(TestShmAsyncEndpoint, PostSendRawSglFailWhenMemcpyFail)
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
    mShmAsyncEp->mIsNeedEncrypt = false;
    NResult ret = mShmAsyncEp->PostSendRawSgl(sglReq, 1);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestShmAsyncEndpoint, PostReadValidateFail)
{
    mShmAsyncEp->mState.Set(NEP_BROKEN);
    NResult ret = mShmAsyncEp->PostRead(mReq);
    EXPECT_EQ(ret, NN_EP_NOT_ESTABLISHED);
    mShmAsyncEp->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestShmAsyncEndpoint, PostReadSglValidateFail)
{
    mShmAsyncEp->mState.Set(NEP_BROKEN);
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    NResult ret = mShmAsyncEp->PostRead(sglReq);
    EXPECT_EQ(ret, NN_EP_NOT_ESTABLISHED);
    mShmAsyncEp->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestShmAsyncEndpoint, PostReadSglFail)
{
    MOCKER_CPP(&ShmWorker::PostReadSgl)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_OP_CTX_FULL)))
        .then(returnValue(1));

    MOCKER_CPP(&NetDriverShmWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));

    NetDriverShmWithOOB *driver = new (std::nothrow) NetDriverShmWithOOB("PostReadSgl", false, SHM);
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    mShmAsyncEp->mDriver = driver;
    NResult ret = mShmAsyncEp->PostRead(sglReq);
    EXPECT_EQ(ret, 1);

    mShmAsyncEp->mDriver = nullptr;
    if (driver != nullptr) {
        delete driver;
        driver = nullptr;
    }
}

TEST_F(TestShmAsyncEndpoint, PostWriteValidateFail)
{
    mShmAsyncEp->mState.Set(NEP_BROKEN);
    NResult ret = mShmAsyncEp->PostWrite(mReq);
    EXPECT_EQ(ret, NN_EP_NOT_ESTABLISHED);
    mShmAsyncEp->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestShmAsyncEndpoint, PostWriteFail)
{
    MOCKER_CPP(&ShmWorker::PostWrite)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_OP_CTX_FULL)))
        .then(returnValue(1));

    MOCKER_CPP(&NetDriverShmWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));

    NetDriverShmWithOOB *driver = new (std::nothrow) NetDriverShmWithOOB("PostWrite", false, SHM);
    mShmAsyncEp->mDriver = driver;
    NResult ret = mShmAsyncEp->PostWrite(mReq);
    EXPECT_EQ(ret, 1);

    mShmAsyncEp->mDriver = nullptr;
    if (driver != nullptr) {
        delete driver;
        driver = nullptr;
    }
}

TEST_F(TestShmAsyncEndpoint, PostWriteSglValidateFail)
{
    mShmAsyncEp->mState.Set(NEP_BROKEN);
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    NResult ret = mShmAsyncEp->PostWrite(sglReq);
    EXPECT_EQ(ret, NN_EP_NOT_ESTABLISHED);
    mShmAsyncEp->mState.Set(NEP_ESTABLISHED);
}

TEST_F(TestShmAsyncEndpoint, PostWriteSglFail)
{
    MOCKER_CPP(&ShmWorker::PostWriteSgl)
        .stubs()
        .will(returnValue(static_cast<HResult>(SH_OP_CTX_FULL)))
        .then(returnValue(1));

    MOCKER_CPP(&NetDriverShmWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));

    NetDriverShmWithOOB *driver = new (std::nothrow) NetDriverShmWithOOB("PostWriteSgl", false, SHM);
    UBSHcomNetTransSgeIov iov[NN_NO4];
    UBSHcomNetTransSglRequest sglReq(iov, NN_NO4, 0);
    mShmAsyncEp->mDriver = driver;
    NResult ret = mShmAsyncEp->PostWrite(sglReq);
    EXPECT_EQ(ret, 1);

    mShmAsyncEp->mDriver = nullptr;
    if (driver != nullptr) {
        delete driver;
        driver = nullptr;
    }
}

TEST_F(TestShmAsyncEndpoint, SendFdsFail)
{
    int fds1[NN_NO4] = {1, 2, 3, 4};
    int fds2[NN_NO4] = {0};
    uint32_t len = NN_NO4;
    MOCKER_CPP(::send).stubs().will(returnValue(0));
    EXPECT_EQ(mShmAsyncEp->SendFds(fds1, len), NN_ERROR);
    EXPECT_EQ(mShmAsyncEp->SendFds(fds2, len), NN_INVALID_PARAM);
}

TEST_F(TestShmAsyncEndpoint, DCMarkPeerBuckFree)
{
    ShmChannelPtr shmCh = new ShmChannel("TestShmAsyncEndpoint", 0, 0, 0);
    EXPECT_NO_FATAL_FAILURE(shmCh->DCMarkPeerBuckFree(0));
    EXPECT_NO_FATAL_FAILURE(shmCh->DCMarkBuckFree(0));
}

TEST_F(TestShmAsyncEndpoint, PostSendValidationOpCodeFail)
{
    UBSHcomNetAtomicState<UBSHcomNetEndPointState> state{NEP_ESTABLISHED};
    uint16_t opCode = MAX_OPCODE;
    UBSHcomNetTransRequest req{};
    EXPECT_EQ(PostSendValidation(state, 0, opCode, req), NN_INVALID_OPCODE);
}

TEST_F(TestShmAsyncEndpoint, PostSendValidationSizeFail)
{
    UBSHcomNetAtomicState<UBSHcomNetEndPointState> state{NEP_ESTABLISHED};
    UBSHcomNetTransRequest req{};
    req.size = 0;
    EXPECT_EQ(PostSendValidation(state, 0, 0, req), NN_INVALID_PARAM);
}
}
}