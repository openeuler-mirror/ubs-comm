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
#include <sys/poll.h>

#include "net_monotonic.h"
#include "ub_common.h"
#include "ub_mr_fixed_buf.h"
#include "ub_urma_wrapper_jetty.h"
#include "under_api/urma/urma_api_wrapper.h"

namespace ock {
namespace hcom {
class TestUbUrmaJetty : public testing::Test {
public:
    TestUbUrmaJetty();
    virtual void SetUp(void);
    virtual void TearDown(void);
    std::string mName = "TestUbUrmaJetty";
    UBJetty *jetty = nullptr;
    UBContext *ctx = nullptr;
    UBJfc *jfc = nullptr;
    urma_jfc_t mUrmaJfc{};
    urma_jetty_t UrmaJetty{};
    UBEId eid{};
    urma_context_t mUrmaContext{};
    UBMemoryRegionFixedBuffer *mJettyMr = nullptr;
};

TestUbUrmaJetty::TestUbUrmaJetty() {}

void TestUbUrmaJetty::SetUp()
{
    ctx = new (std::nothrow) UBContext("ubTest");
    ASSERT_NE(ctx, nullptr);
    ctx->mUrmaContext = &mUrmaContext;

    mJettyMr = new (std::nothrow) UBMemoryRegionFixedBuffer(mName, ctx, 1, 1, 1);
    ASSERT_NE(mJettyMr, nullptr);

    jfc = new (std::nothrow) UBJfc(mName, ctx, false, 0);
    ASSERT_NE(jfc, nullptr);
    jfc->mUrmaJfc = &mUrmaJfc;
    jetty = new (std::nothrow) UBJetty(mName, 0, ctx, jfc);
    ASSERT_NE(jetty, nullptr);
    jetty->mJettyOptions.ubcMode = UBSHcomUbcMode::LowLatency;
    jetty->mUrmaJetty = &UrmaJetty;
    jetty->mJettyMr = mJettyMr;
    jetty->mState = UBJettyState::READY;
    jetty->StoreExchangeInfo(new UBJettyExchangeInfo);
    MOCKER_CPP(HcomUrma::Uninit).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBDeviceHelper::UnInitialize).stubs().will(ignoreReturnValue());
}

void TestUbUrmaJetty::TearDown()
{
    ctx->mUrmaContext = nullptr;
    jetty->mSendJfc = nullptr;
    jetty->mRecvJfc = nullptr;
    jetty->mUBContext = nullptr;
    jetty->mUrmaJetty = nullptr;
    jetty->mJettyMr = nullptr;
    jfc->mUBContext = nullptr;
    jfc->mUrmaJfc = nullptr;

    if (mJettyMr != nullptr) {
        delete mJettyMr;
        mJettyMr = nullptr;
    }

    if (ctx != nullptr) {
        delete ctx;
        ctx = nullptr;
    }

    if (jfc != nullptr) {
        delete jfc;
        jfc = nullptr;
    }

    if (jetty != nullptr) {
        delete jetty;
        jetty = nullptr;
    }

    GlobalMockObject::verify();
}

TEST_F(TestUbUrmaJetty, UnInitializeSuccess)
{
    int tmp;
    mJettyMr->IncreaseRef();
    mJettyMr->IncreaseRef();
    ctx->IncreaseRef();
    jfc->IncreaseRef();
    urma_jfr_t mJfr{};
    jetty->mJfr = &mJfr;
    int urmaErr = 11;

    MOCKER_CPP(&HcomUrma::UnbindJetty).stubs().will(returnValue(0));
    MOCKER_CPP(&HcomUrma::UnimportJetty).stubs().will(returnValue(0));
    MOCKER_CPP(&HcomUrma::DeleteJetty).stubs().will(returnValue(urmaErr));
    MOCKER_CPP(&HcomUrma::DeleteJfr).stubs().will(returnValue(urmaErr));
    MOCKER_CPP(&HcomUrma::DeleteJfc).stubs().will(returnValue(urmaErr));
    MOCKER_CPP(&UBContext::UnInitialize).stubs().will(returnValue(0));
    EXPECT_EQ(jetty->UnInitialize(), 0);
}

TEST_F(TestUbUrmaJetty, UnInitializeHB)
{
    int tmp;
    mJettyMr->IncreaseRef();
    mJettyMr->IncreaseRef();
    ctx->IncreaseRef();
    jfc->IncreaseRef();
    urma_jfr_t mJfr{};
    jetty->mJfr = &mJfr;
    auto localMr = new (std::nothrow) UBMemoryRegion("localMr", nullptr, 0, 0, 0);
    auto remoteMr = new (std::nothrow) UBMemoryRegion("remoteMr", nullptr, 0, 0, 0);
    jetty->mHBLocalMr = localMr;
    jetty->mHBRemoteMr = remoteMr;
    int urmaErr = 11;

    MOCKER_CPP(&HcomUrma::UnbindJetty).stubs().will(returnValue(0));
    MOCKER_CPP(&HcomUrma::UnimportJetty).stubs().will(returnValue(0));
    MOCKER_CPP(&HcomUrma::DeleteJetty).stubs().will(returnValue(urmaErr));
    MOCKER_CPP(&HcomUrma::DeleteJfr).stubs().will(returnValue(urmaErr));
    MOCKER_CPP(&HcomUrma::DeleteJfc).stubs().will(returnValue(urmaErr));
    MOCKER_CPP(&UBContext::UnInitialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::DestroyHBMemoryRegion).stubs().will(ignoreReturnValue());
    EXPECT_EQ(jetty->UnInitialize(), 0);
    localMr = nullptr;
    remoteMr = nullptr;
}

TEST_F(TestUbUrmaJetty, GetProtocol)
{
    ctx->protocol = UBSHcomNetDriverProtocol::UBC;
    EXPECT_EQ(jetty->GetProtocol(), UBSHcomNetDriverProtocol::UBC);
}

TEST_F(TestUbUrmaJetty, FillJfsCfg)
{
    urma_jfs_cfg_t cfg{};

    ctx->protocol = UBSHcomNetDriverProtocol::UBC;
    EXPECT_NO_THROW(jetty->FillJfsCfg(&cfg));
}

TEST_F(TestUbUrmaJetty, FillJfrCfg)
{
    urma_jfr_cfg_t cfg{};

    ctx->protocol = UBSHcomNetDriverProtocol::UBC;
    EXPECT_NO_THROW(jetty->FillJfrCfg(&cfg));
}

TEST_F(TestUbUrmaJetty, PostReceiveParamErr)
{
    jetty->mUrmaJetty = nullptr;
    EXPECT_EQ(jetty->PostReceive(0, 0, nullptr, 0), UB_QP_NOT_INITIALIZED);
}

TEST_F(TestUbUrmaJetty, PostReceive)
{
    MOCKER(HcomUrma::PostJettyRecvWr).stubs().will(returnValue(1)).then(returnValue(0));
    EXPECT_EQ(jetty->PostReceive(0, 0, nullptr, 0), UB_QP_POST_RECEIVE_FAILED);
    EXPECT_EQ(jetty->PostReceive(0, 0, nullptr, 0), UB_OK);
}

TEST_F(TestUbUrmaJetty, PostSendSglInlineJettyNull)
{
    jetty->mUrmaJetty = nullptr;
    EXPECT_EQ(jetty->PostSendSglInline(nullptr, NN_NO10, NN_NO10), UB_QP_NOT_INITIALIZED);
}

TEST_F(TestUbUrmaJetty, PostSendSglInlineJettyFail)
{
    jetty->mUrmaJetty = &UrmaJetty;
    UBSHcomNetTransDataIov iov[1];
    uint64_t testKey = 123;
    iov[0].address = 0X1234;
    iov[0].key = testKey;
    iov[0].size = NN_NO10;
    MOCKER(HcomUrma::PostJettySendWr, urma_status_t(urma_jetty_t *, urma_jfs_wr_t *, urma_jfs_wr_t **))
        .stubs().will(returnValue(1)).then(returnValue(0));
    EXPECT_EQ(jetty->PostSendSglInline(iov, 1, 0), UB_QP_POST_SEND_FAILED);
    EXPECT_EQ(jetty->PostSendSglInline(iov, 1, 0), UB_OK);
}

TEST_F(TestUbUrmaJetty, PostSendSglParamErr)
{
    UBSHcomNetTransSgeIov iov{};
    uint32_t iovCount = 1;

    jetty->mUrmaJetty = nullptr;
    EXPECT_EQ(jetty->PostSendSgl(&iov, iovCount, 0, 0), UB_QP_NOT_INITIALIZED);
}

TEST_F(TestUbUrmaJetty, PostSendSgl)
{
    UBSHcomNetTransSgeIov iov{};
    uint32_t iovCount = 1;
    MOCKER(HcomUrma::PostJettySendWr, urma_status_t(urma_jetty_t *, urma_jfs_wr_t *, urma_jfs_wr_t **))
        .stubs().will(returnValue(1)).then(returnValue(0));
    EXPECT_EQ(jetty->PostSendSgl(&iov, iovCount, 0, 0), UB_QP_POST_SEND_FAILED);
    EXPECT_EQ(jetty->PostSendSgl(&iov, iovCount, 0, 0), UB_OK);

    ctx->protocol = UBSHcomNetDriverProtocol::UBC;
    EXPECT_EQ(jetty->PostSendSgl(&iov, iovCount, 0, 0), UB_OK);
}

TEST_F(TestUbUrmaJetty, PostReadParamErr)
{
    jetty->mUrmaJetty = nullptr;
    EXPECT_EQ(jetty->PostRead(0, nullptr, 0, nullptr, 0, 0), UB_QP_NOT_INITIALIZED);
}

TEST_F(TestUbUrmaJetty, PostRead)
{
    MOCKER(HcomUrma::PostJettySendWr, urma_status_t(urma_jetty_t *, urma_jfs_wr_t *, urma_jfs_wr_t **))
        .stubs().will(returnValue(1)).then(returnValue(0));
    EXPECT_EQ(jetty->PostRead(0, nullptr, 0, nullptr, 0, 0), UB_QP_POST_WRITE_FAILED);
    EXPECT_EQ(jetty->PostRead(0, nullptr, 0, nullptr, 0, 0), UB_OK);
}

TEST_F(TestUbUrmaJetty, UBCPostReadTseg)
{
    urma_target_seg_t *tmpSeg1 = nullptr;
    urma_target_seg_t seg{};
    urma_target_seg_t *tmpSeg2 = &seg;
    MOCKER(HcomUrma::UnimportSeg).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER(HcomUrma::PostJettySendWr, urma_status_t(urma_jetty_t *, urma_jfs_wr_t *, urma_jfs_wr_t **))
        .stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER(HcomUrma::ImportSeg).stubs().will(returnValue(tmpSeg1)).then(returnValue(tmpSeg2));

    EXPECT_EQ(jetty->PostRead(0, static_cast<urma_target_seg_t *>(nullptr), static_cast<uintptr_t>(0),
        static_cast<uint64_t>(0), static_cast<uint32_t>(0), static_cast<uint64_t>(0)), UB_QP_POST_READ_FAILED);

    EXPECT_EQ(jetty->PostRead(0, static_cast<urma_target_seg_t *>(nullptr), static_cast<uintptr_t>(0),
        static_cast<uint64_t>(0), static_cast<uint32_t>(0), static_cast<uint64_t>(0)), UB_QP_POST_READ_FAILED);

    EXPECT_EQ(jetty->PostRead(0, static_cast<urma_target_seg_t *>(nullptr), static_cast<uintptr_t>(0),
        static_cast<uint64_t>(0), static_cast<uint32_t>(0), static_cast<uint64_t>(0)), UB_OK);

    jetty->mUrmaJetty = nullptr;
    EXPECT_EQ(jetty->PostRead(0, static_cast<urma_target_seg_t *>(nullptr), static_cast<uintptr_t>(0),
        static_cast<uint64_t>(0), static_cast<uint32_t>(0), static_cast<uint64_t>(0)), UB_QP_NOT_INITIALIZED);
}

TEST_F(TestUbUrmaJetty, PostWriteParamErr)
{
    jetty->mUrmaJetty = nullptr;
    EXPECT_EQ(jetty->PostWrite(0, nullptr, 0, nullptr, 0, 0), UB_QP_NOT_INITIALIZED);
}

TEST_F(TestUbUrmaJetty, PostWrite)
{
    MOCKER(HcomUrma::PostJettySendWr, urma_status_t(urma_jetty_t *, urma_jfs_wr_t *, urma_jfs_wr_t **))
        .stubs().will(returnValue(1)).then(returnValue(0));
    EXPECT_EQ(jetty->PostWrite(0, nullptr, 0, nullptr, 0, 0), UB_QP_POST_WRITE_FAILED);
    EXPECT_EQ(jetty->PostWrite(0, nullptr, 0, nullptr, 0, 0), UB_OK);
}

TEST_F(TestUbUrmaJetty, UBCPostWriteTseg)
{
    urma_target_seg_t *tmpSeg1 = nullptr;
    urma_target_seg_t seg{};
    urma_target_seg_t *tmpSeg2 = &seg;
    MOCKER(HcomUrma::UnimportSeg).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER(HcomUrma::PostJettySendWr, urma_status_t(urma_jetty_t *, urma_jfs_wr_t *, urma_jfs_wr_t **))
        .stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER(HcomUrma::ImportSeg).stubs().will(returnValue(tmpSeg1)).then(returnValue(tmpSeg2));

    EXPECT_EQ(jetty->PostWrite(0, static_cast<urma_target_seg_t *>(nullptr), static_cast<uintptr_t>(0),
        static_cast<uint64_t>(0), static_cast<uint32_t>(0), static_cast<uint64_t>(0)), UB_QP_POST_WRITE_FAILED);

    EXPECT_EQ(jetty->PostWrite(0, static_cast<urma_target_seg_t *>(nullptr), static_cast<uintptr_t>(0),
        static_cast<uint64_t>(0), static_cast<uint32_t>(0), static_cast<uint64_t>(0)), UB_QP_POST_WRITE_FAILED);

    EXPECT_EQ(jetty->PostWrite(0, static_cast<urma_target_seg_t *>(nullptr), static_cast<uintptr_t>(0),
        static_cast<uint64_t>(0), static_cast<uint32_t>(0), static_cast<uint64_t>(0)), UB_OK);

    jetty->mUrmaJetty = nullptr;
    EXPECT_EQ(jetty->PostWrite(0, static_cast<urma_target_seg_t *>(nullptr), static_cast<uintptr_t>(0),
        static_cast<uint64_t>(0), static_cast<uint32_t>(0), static_cast<uint64_t>(0)), UB_QP_NOT_INITIALIZED);
}

TEST_F(TestUbUrmaJetty, GetId)
{
    EXPECT_NO_FATAL_FAILURE(jetty->GetId());
}

TEST_F(TestUbUrmaJetty, SetAndGetUpId)
{
    EXPECT_NO_FATAL_FAILURE(jetty->SetUpId(1));
    EXPECT_NO_FATAL_FAILURE(jetty->GetUpId());
}

TEST_F(TestUbUrmaJetty, SetAndGetName)
{
    EXPECT_NO_FATAL_FAILURE(jetty->GetName());
    EXPECT_NO_FATAL_FAILURE(jetty->SetName(mName));
}

TEST_F(TestUbUrmaJetty, SetAndGetContext)
{
    EXPECT_NO_FATAL_FAILURE(jetty->SetUpContext(1));
    EXPECT_NO_FATAL_FAILURE(jetty->GetUpContext());
}

TEST_F(TestUbUrmaJetty, SetAndGetContextOne)
{
    EXPECT_NO_FATAL_FAILURE(jetty->SetUpContext1(1));
    EXPECT_NO_FATAL_FAILURE(jetty->GetUpContext1());
}

TEST_F(TestUbUrmaJetty, GetCtxPosted)
{
    UBOpContextInfo *remaining = nullptr;
    EXPECT_NO_FATAL_FAILURE(jetty->GetCtxPosted(remaining));
}

TEST_F(TestUbUrmaJetty, GetPostedCount)
{
    jetty->mCtxPostedCount = 1;
    EXPECT_EQ(jetty->GetPostedCount(), 1);
}

TEST_F(TestUbUrmaJetty, GetPostSendWr)
{
    jetty->mPostSendRef = 0;
    EXPECT_EQ(jetty->GetPostSendWr(), false);
    jetty->mPostSendRef = NN_NO64;
    EXPECT_EQ(jetty->GetPostSendWr(), true);
}

TEST_F(TestUbUrmaJetty, GetOneSideWr)
{
    jetty->mOneSideRef = 0;
    EXPECT_EQ(jetty->GetOneSideWr(), false);
    jetty->mOneSideRef = NN_NO64;
    EXPECT_EQ(jetty->GetOneSideWr(), true);
}

TEST_F(TestUbUrmaJetty, NewId)
{
    EXPECT_NO_FATAL_FAILURE(UBJetty::NewId());
}

TEST_F(TestUbUrmaJetty, PostRegMrSize)
{
    EXPECT_EQ(jetty->PostRegMrSize(), NN_NO1024);
}

TEST_F(TestUbUrmaJetty, StopParamErr)
{
    jetty->mState = UBJettyState::ERROR;
    EXPECT_EQ(jetty->Stop(), UB_OK);
}

TEST_F(TestUbUrmaJetty, Stop)
{
    jetty->mState = UBJettyState::READY;
    MOCKER(HcomUrma::ModifyJetty).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER(HcomUrma::ModifyJfr).stubs().will(returnValue(0));
    EXPECT_EQ(jetty->Stop(), 1);
    EXPECT_EQ(jetty->Stop(), UB_OK);
}

TEST_F(TestUbUrmaJetty, StopTwo)
{
    jetty->mState = UBJettyState::READY;
    MOCKER(HcomUrma::ModifyJetty).stubs().will(returnValue(1));
    jetty->mUBContext->protocol = UBSHcomNetDriverProtocol::UBC;
    EXPECT_EQ(jetty->Stop(), 1);
}

TEST_F(TestUbUrmaJetty, StopModifyJfrFail)
{
    jetty->mState = UBJettyState::READY;
    urma_jfr_t tJfr;
    jetty->mJfr = &tJfr;

    MOCKER(HcomUrma::ModifyJetty).stubs().will(returnValue(0));
    MOCKER(HcomUrma::ModifyJfr).stubs().will(returnValue(1));
    EXPECT_EQ(jetty->Stop(), 1);

    jetty->mJfr = nullptr;
}

TEST_F(TestUbUrmaJetty, CreateUrmaJettyParamErr)
{
    jetty->mUBContext = nullptr;
    EXPECT_EQ(jetty->CreateUrmaJetty(0, 0, 0), UB_PARAM_INVALID);
}

TEST_F(TestUbUrmaJetty, CreateUrmaJettyUBC)
{
    urma_jfr_t jfr{};
    urma_jfr_t *tmpJfr = nullptr;
    urma_jetty_t *tmpJetty = nullptr;
    jetty->mUBContext->protocol = UBSHcomNetDriverProtocol::UBC;
    MOCKER(HcomUrma::CreateJfr).stubs().will(returnValue(tmpJfr)).then(returnValue(&jfr));
    MOCKER(HcomUrma::CreateJetty).stubs().will(returnValue(tmpJetty));
    urma_status_t res = 10;
    MOCKER(HcomUrma::DeleteJfr).stubs().will(returnValue(res));
    EXPECT_EQ(jetty->CreateUrmaJetty(0, 0, 0), UB_PARAM_INVALID);
    EXPECT_EQ(jetty->CreateUrmaJetty(0, 0, 0), UB_QP_CREATE_FAILED);
}

TEST_F(TestUbUrmaJetty, GetJettyMrAndKey)
{
    EXPECT_NO_FATAL_FAILURE(jetty->GetJettyMr());
    EXPECT_NO_FATAL_FAILURE(jetty->GetLKey());
}

TEST_F(TestUbUrmaJetty, InitializeParamErr)
{
    jetty->mUBContext->protocol = UBSHcomNetDriverProtocol::UBC;

    MOCKER_CPP(&UBJetty::CreateJettyMr).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBJetty::CreateUrmaJetty).stubs().will(returnValue(1));

    EXPECT_EQ(jetty->Initialize(0, 0), 1);
    EXPECT_EQ(jetty->Initialize(0, 0), 1);
}

TEST_F(TestUbUrmaJetty, InitializeSuccess)
{
    int tmp;

    jetty->mUBContext->protocol = UBSHcomNetDriverProtocol::UBC;

    MOCKER_CPP(&UBJetty::CreateJettyMr).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::CreateUrmaJetty).stubs().will(returnValue(1)).then(returnValue(0));

    EXPECT_EQ(jetty->Initialize(0, 0), 1);
    EXPECT_EQ(jetty->Initialize(0, 0), 0);
}

TEST_F(TestUbUrmaJetty, ChangeToInitAndReceive)
{
    urma_jetty_attr_t attr{};
    UBJettyExchangeInfo exInfo{};

    EXPECT_NO_FATAL_FAILURE(jetty->ChangeToInit(attr));
    EXPECT_NO_FATAL_FAILURE(jetty->ChangeToReceive(exInfo, attr));
}

TEST_F(TestUbUrmaJetty, ChangeToSend)
{
    urma_jetty_attr_t attr{};
    EXPECT_NO_FATAL_FAILURE(jetty->ChangeToSend(attr));
}

TEST_F(TestUbUrmaJetty, ChangeToReadyParamErr)
{
    UBJettyExchangeInfo exInfo{};
    jetty->mUrmaJetty = nullptr;
    EXPECT_EQ(jetty->ChangeToReady(exInfo), UB_QP_CHANGE_STATE_FAILED);
    jetty->mUrmaJetty = &UrmaJetty;
    MOCKER_CPP(&UBJetty::SetMaxSendWrConfig).stubs().will(returnValue(1));
    EXPECT_EQ(jetty->ChangeToReady(exInfo), 1);
}

TEST_F(TestUbUrmaJetty, ChangeToReadyUbc)
{
    MOCKER_CPP(&UBJetty::SetMaxSendWrConfig).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::ImportAndBindJetty).stubs().will(returnValue(0));

    UBJettyExchangeInfo exInfo{};
    EXPECT_EQ(jetty->ChangeToReady(exInfo), 0);
}

TEST_F(TestUbUrmaJetty, SetMaxSendWrConfig)
{
    UBJettyExchangeInfo exInfo{};

    exInfo.maxReceiveWr = 0;
    EXPECT_EQ(jetty->SetMaxSendWrConfig(exInfo), UB_QP_RECEIVE_CONFIG_ERR);

    exInfo.maxReceiveWr = JETTY_MAX_RECV_WR;
    EXPECT_EQ(jetty->SetMaxSendWrConfig(exInfo), 0);
}

TEST_F(TestUbUrmaJetty, FillExchangeInfoUbc)
{
    UBJettyExchangeInfo exInfo{};
    EXPECT_EQ(jetty->FillExchangeInfo(exInfo), UB_OK);
}

TEST_F(TestUbUrmaJetty, StoreExchangeInfo)
{
    EXPECT_NO_FATAL_FAILURE(jetty->StoreExchangeInfo(new UBJettyExchangeInfo));
}

TEST_F(TestUbUrmaJetty, ImportAndBindJettyErr)
{
    urma_target_jetty_t *tmpJetty = nullptr;
    urma_target_jetty_t tmpJetty2{};
    ctx->protocol = UBSHcomNetDriverProtocol::UBC;
    MOCKER(HcomUrma::ImportJetty).stubs().will(returnValue(tmpJetty)).then(returnValue(&tmpJetty2));
    MOCKER(HcomUrma::BindJetty).stubs().will(returnValue(1));
    MOCKER(HcomUrma::UnimportJetty).stubs().will(returnValue(0));
    EXPECT_EQ(jetty->ImportAndBindJetty(), UB_QP_IMPORT_FAILED);
    EXPECT_EQ(jetty->ImportAndBindJetty(), UB_QP_BIND_FAILED);
}

TEST_F(TestUbUrmaJetty, ImportAndBindJetty)
{
    urma_target_jetty_t tmpJetty2{};
    ctx->protocol = UBSHcomNetDriverProtocol::UBC;
    MOCKER(HcomUrma::ImportJetty).stubs().will(returnValue(&tmpJetty2));
    MOCKER(HcomUrma::BindJetty).stubs().will(returnValue(0));

    EXPECT_EQ(jetty->ImportAndBindJetty(), 0);
}

TEST_F(TestUbUrmaJetty, CreatePollingCq)
{
    urma_jfc_t *tmpJfc = nullptr;
    MOCKER(HcomUrma::CreateJfc).stubs().will(returnValue(tmpJfc)).then(returnValue(&mUrmaJfc));
    EXPECT_EQ(jfc->CreatePollingCq(), UB_NEW_OBJECT_FAILED);
    EXPECT_EQ(jfc->CreatePollingCq(), UB_OK);
}

TEST_F(TestUbUrmaJetty, CtxInitializeParamErr)
{
    urma_device_t **devList = nullptr;
    uint8_t bw = 0;
    EXPECT_EQ(ctx->Initialize(bw), UB_OK);

    MOCKER(HcomUrma::GetDeviceList).stubs().will(returnValue(devList));
    ctx->mUrmaContext = nullptr;
    EXPECT_EQ(ctx->Initialize(bw), UB_DEVICE_FAILED_OPEN);
}

TEST_F(TestUbUrmaJetty, CreateJettyMrErr)
{
    MOCKER(UBMemoryRegionFixedBuffer::Create).stubs().will(returnValue(1));
    EXPECT_EQ(jetty->CreateJettyMr(), 1);
}

TEST_F(TestUbUrmaJetty, CreateJettyMr)
{
    UBMemoryRegionFixedBuffer *mJettyMR = new (std::nothrow) UBMemoryRegionFixedBuffer(mName, ctx, 0, 0, 0);
    ASSERT_NE(mJettyMR, nullptr);
    MOCKER(UBMemoryRegionFixedBuffer::Create)
        .stubs()
        .with(any(), any(), any(), any(), any(), outBound(mJettyMR))
        .will(returnValue(0));
    MOCKER_CPP_VIRTUAL(*mJettyMR, &UBMemoryRegionFixedBuffer::Initialize)
        .stubs()
        .will(returnValue(1))
        .then(returnValue(0));
    EXPECT_EQ(jetty->CreateJettyMr(), 1);

    EXPECT_EQ(jetty->CreateJettyMr(), 0);
    jetty->mJettyMr->DecreaseRef();
    jetty->mJettyMr->DecreaseRef();
}

TEST_F(TestUbUrmaJetty, GetFreeBuff)
{
    uintptr_t item = 0;
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer, bool (UBMemoryRegionFixedBuffer::*)(uintptr_t &))
        .stubs()
        .will(returnValue(false));
    EXPECT_EQ(jetty->GetFreeBuff(item), false);
}

TEST_F(TestUbUrmaJetty, GetFreeBufferN)
{
    uintptr_t *items = nullptr;
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBufferN).stubs().will(returnValue(false));
    EXPECT_EQ(jetty->GetFreeBufferN(items, 0), false);
}

TEST_F(TestUbUrmaJetty, ReturnBuffer)
{
    uintptr_t value = 0;
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::ReturnBuffer).stubs().will(returnValue(false));
    EXPECT_EQ(jetty->ReturnBuffer(value), false);
}

TEST_F(TestUbUrmaJetty, PostOneSideSglImportSegFail)
{
    UBSHcomNetTransSgeIov iov[NET_SGE_MAX_IOV];
    uint32_t iovCount = NET_SGE_MAX_IOV;
    uint64_t ctx[NET_SGE_MAX_IOV];
    urma_target_seg_t seg{};
    urma_target_seg_t *tmpSeg = nullptr;
    MOCKER(HcomUrma::ImportSeg).stubs().will(returnValue(tmpSeg));
    MOCKER(HcomUrma::PostJettySendWr,
        urma_status_t(urma_jetty_t *, urma_jfs_wr_t *, uint32_t, urma_jfs_wr_t **))
        .stubs().will(returnValue(1));
    EXPECT_EQ(jetty->PostOneSideSgl(iov, iovCount, ctx, true, NET_SGE_MAX_IOV), UB_QP_POST_READ_FAILED);
}

TEST_F(TestUbUrmaJetty, PostOneSideSglFail)
{
    UBSHcomNetTransSgeIov iov[NET_SGE_MAX_IOV];
    uint32_t iovCount = NET_SGE_MAX_IOV;
    uint64_t ctx[NET_SGE_MAX_IOV];
    urma_target_seg_t seg{};
    urma_target_seg_t *tmpSeg = &seg;
    MOCKER(HcomUrma::ImportSeg).stubs().will(returnValue(tmpSeg));
    MOCKER(HcomUrma::PostJettySendWr,
        urma_status_t(urma_jetty_t *, urma_jfs_wr_t *, uint32_t, urma_jfs_wr_t **))
        .stubs().will(returnValue(1));
    MOCKER_CPP(HcomUrma::UnimportSeg).stubs().will(returnValue(0));
    EXPECT_EQ(jetty->PostOneSideSgl(iov, iovCount, ctx, true, NET_SGE_MAX_IOV), UB_QP_POST_READ_FAILED);
    EXPECT_EQ(jetty->PostOneSideSgl(iov, iovCount, ctx, false, NET_SGE_MAX_IOV), UB_QP_POST_WRITE_FAILED);
}

TEST_F(TestUbUrmaJetty, PostOneSideSgl)
{
    UBSHcomNetTransSgeIov iov[NET_SGE_MAX_IOV];
    uint32_t iovCount = NET_SGE_MAX_IOV;
    uint64_t ctx[NET_SGE_MAX_IOV];
    urma_target_seg_t seg{};
    urma_target_seg_t *tmpSeg = &seg;
    MOCKER(HcomUrma::ImportSeg).stubs().will(returnValue(tmpSeg));
    MOCKER(HcomUrma::PostJettySendWr,
        urma_status_t(urma_jetty_t *, urma_jfs_wr_t *, uint32_t, urma_jfs_wr_t **))
        .stubs().will(returnValue(0));
    MOCKER(HcomUrma::UnimportSeg).stubs().will(returnValue(1));
    EXPECT_EQ(jetty->PostOneSideSgl(iov, iovCount, ctx, false, NET_SGE_MAX_IOV), UB_OK);
}

TEST_F(TestUbUrmaJetty, UnInitialize)
{
    urma_target_jetty_t tmpJetty{};
    jetty->mTargetJetty = &tmpJetty;
    jetty->mJettyOptions.ubcMode = UBSHcomUbcMode::LowLatency;
    jfc->IncreaseRef();
    MOCKER_CPP(&HcomUrma::UnbindJetty).stubs().will(returnValue(0));
    MOCKER_CPP(&HcomUrma::UnimportJetty).stubs().will(returnValue(0));
    MOCKER_CPP(&HcomUrma::DeleteJetty).stubs().will(returnValue(0));
    MOCKER_CPP(&UBContext::UnInitialize).stubs().will(returnValue(0));
    EXPECT_EQ(jetty->UnInitialize(), 0);
    jetty->mTargetJetty = nullptr;
}

TEST_F(TestUbUrmaJetty, ImportAndBindJettyFail)
{
    urma_target_jetty_t tmpJetty2{};
    jetty->mJettyOptions.ubcMode = UBSHcomUbcMode::LowLatency;
    MOCKER(HcomUrma::ImportJetty).stubs().will(returnValue(&tmpJetty2));
    MOCKER(HcomUrma::BindJetty).stubs().will(returnValue(1));
    MOCKER(HcomUrma::UnimportJetty).stubs().will(returnValue(0));

    EXPECT_EQ(jetty->ImportAndBindJetty(), UB_QP_BIND_FAILED);
}

TEST_F(TestUbUrmaJetty, CreateHBMemoryRegion)
{
    UBSHcomNetMemoryRegionPtr mr = nullptr;
    EXPECT_EQ(jetty->CreateHBMemoryRegion(0, mr), NN_INVALID_PARAM);

    MOCKER_CPP(UBMemoryRegion::Create, UResult(const std::string &, UBContext *, uint64_t, UBMemoryRegion *&))
        .stubs().will(returnValue(1)).then(returnValue(0));
    EXPECT_EQ(jetty->CreateHBMemoryRegion(1, mr), 1);

    MOCKER_CPP(&UBMemoryRegion::InitializeForOneSide).stubs().will(returnValue(1)).then(returnValue(0));
    EXPECT_EQ(jetty->CreateHBMemoryRegion(1, mr), 1);
    EXPECT_EQ(jetty->CreateHBMemoryRegion(1, mr), 0);
}

TEST_F(TestUbUrmaJetty, DestroyHBMemoryRegion)
{
    UBSHcomNetMemoryRegionPtr mr = nullptr;
    EXPECT_NO_FATAL_FAILURE(jetty->DestroyHBMemoryRegion(mr));
}

TEST_F(TestUbUrmaJetty, GetNextLocalHBAddress)
{
    auto localMr = new (std::nothrow) UBMemoryRegion("localMr", nullptr, 1, 1, 1);
    jetty->mHBLocalMr = localMr;
    jetty->mHBLocalMr->IncreaseRef();
    EXPECT_NO_FATAL_FAILURE(jetty->GetNextLocalHBAddress());
    if (localMr != nullptr) {
        delete localMr;
        localMr = nullptr;
    }
    jetty->mHBLocalMr.Set(nullptr);
}

TEST_F(TestUbUrmaJetty, GetLocalHBKey)
{
    auto localMr = new (std::nothrow) UBMemoryRegion("localMr", nullptr, 1, 1, 1);
    jetty->mHBLocalMr = localMr;
    jetty->mHBLocalMr->IncreaseRef();
    MOCKER_CPP(&UBMemoryRegion::GetLKey).stubs().will(returnValue(1));
    EXPECT_NO_FATAL_FAILURE(jetty->GetLocalHBKey());
    if (localMr != nullptr) {
        delete localMr;
        localMr = nullptr;
    }
    jetty->mHBLocalMr.Set(nullptr);
}

TEST_F(TestUbUrmaJetty, GetRemoteHbInfo)
{
    auto remoteMr = new (std::nothrow) UBMemoryRegion("remoteMr", nullptr, 1, 1, 1);
    jetty->mHBRemoteMr = remoteMr;
    jetty->mHBRemoteMr->IncreaseRef();
    UBJettyExchangeInfo info{};
    MOCKER_CPP(&UBMemoryRegion::GetLKey).stubs().will(returnValue(1));
    EXPECT_NO_FATAL_FAILURE(jetty->GetRemoteHbInfo(info));
    if (remoteMr != nullptr) {
        delete remoteMr;
        remoteMr = nullptr;
    }
    jetty->mHBRemoteMr.Set(nullptr);
}

}  // namespace hcom
}  // namespace ock
#endif
