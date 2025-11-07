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

#include "hcom_c.h"
#include "hcom_service_c.h"
#include "service_channel_imp.h"
#include "hcom.h"

namespace ock {
namespace hcom {
class TestHcomCapi : public testing::Test {
public:
    TestHcomCapi();
    virtual void SetUp(void);
    virtual void TearDown(void);
};

TestHcomCapi::TestHcomCapi() {}

void TestHcomCapi::SetUp()
{
}

void TestHcomCapi::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(TestHcomCapi, TestCopySglInfo)
{
    ubs_hcom_readwrite_request_sgl *src
        = static_cast<ubs_hcom_readwrite_request_sgl *>(malloc(sizeof(ubs_hcom_readwrite_request_sgl)));
    ASSERT_NE(src, nullptr);
    bzero(src, sizeof(ubs_hcom_readwrite_request_sgl));
    EXPECT_EQ(ubs_hcom_ep_post_send_raw_sgl(1, src, 1), static_cast<uint32_t>(NN_INVALID_PARAM));
    free(src);
}

TEST_F(TestHcomCapi, TestSendRecvFds)
{
    EXPECT_EQ(ubs_hcom_channel_send_fds(0, nullptr, 0), static_cast<uint32_t>(SER_INVALID_PARAM));
    EXPECT_EQ(ubs_hcom_channel_recv_fds(0, nullptr, 0, 0), static_cast<uint32_t>(SER_INVALID_PARAM));
    InnerConnectOptions opt {};
    UBSHcomChannel *ch = new (std::nothrow) HcomChannelImp(0, false, opt);
    EXPECT_NE(ch, nullptr);
    ubs_hcom_channel channel = reinterpret_cast<ubs_hcom_channel>(ch);
    EXPECT_EQ(ubs_hcom_channel_send_fds(channel, nullptr, 0), static_cast<uint32_t>(SER_ERROR));
    EXPECT_EQ(ubs_hcom_channel_recv_fds(channel, nullptr, 0, 0), static_cast<uint32_t>(SER_ERROR));
    channel = 0;
    delete ch;
}

TEST_F(TestHcomCapi, TestConvertServiceConnectOptionsToInnerOptions)
{
    ubs_hcom_service_options opt {};
    ubs_hcom_service service = 0;
    int ret = ubs_hcom_service_create(C_SERVICE_RDMA, "service0", opt, &service);
    ASSERT_EQ(ret, 0);
    ubs_hcom_service_connect_options connectOpt {};
    connectOpt.mode = C_CLIENT_SELF_POLL_BUSY;
    ubs_hcom_channel channel = 0;
    EXPECT_NE(ubs_hcom_service_connect(service, "url", &channel, connectOpt), 0);
    connectOpt.mode = C_CLIENT_SELF_POLL_EVENT;
    EXPECT_NE(ubs_hcom_service_connect(service, "url", &channel, connectOpt), 0);
    ubs_hcom_service_destroy(service, "service0");
}

TEST_F(TestHcomCapi, TestSetTlsOptions)
{
    ubs_hcom_service_options opt {};
    ubs_hcom_service service = 0;
    int ret = ubs_hcom_service_create(C_SERVICE_RDMA, "service0", opt, &service);
    ASSERT_EQ(ret, 0);
    EXPECT_NO_FATAL_FAILURE(ubs_hcom_service_set_tls_opt(service, false, C_SERVICE_TLS_1_2, C_SERVICE_AES_GCM_128,
        nullptr, nullptr, nullptr));
    ubs_hcom_service_destroy(service, "service0");
}

TEST_F(TestHcomCapi, TestSetUbcMode)
{
    ubs_hcom_service_options opt {};
    ubs_hcom_service service = 0;
    int ret = ubs_hcom_service_create(C_SERVICE_UBC, "service0", opt, &service);
    ASSERT_EQ(ret, 0);
    ubs_hcom_service_ubc_mode ubcMode = C_SERVICE_HIGHBANDWIDTH;
    EXPECT_NO_FATAL_FAILURE(ubs_hcom_service_set_ubcmode(service, ubcMode));
    ubs_hcom_service_destroy(service, "service0");
}

void CommonCb(void *arg, ubs_hcom_service_context context)
{
    return;
}

TEST_F(TestHcomCapi, TestChannelRecv)
{
    EXPECT_EQ(ubs_hcom_channel_recv(0, 0, 0, 0, nullptr), static_cast<uint32_t>(SER_INVALID_PARAM));

    InnerConnectOptions opt{};
    UBSHcomChannel *ch = new (std::nothrow) HcomChannelImp(0, false, opt);
    EXPECT_NE(ch, nullptr);
    ubs_hcom_channel channel = reinterpret_cast<ubs_hcom_channel>(ch);
    EXPECT_EQ(ubs_hcom_channel_recv(channel, 0, 0, 0, nullptr), static_cast<uint32_t>(SER_INVALID_PARAM));

    UBSHcomServiceContext ctx{};
    ubs_hcom_service_context serviceContext = reinterpret_cast<ubs_hcom_service_context>(&ctx);
    EXPECT_EQ(ubs_hcom_channel_recv(channel, serviceContext, 0, 0, nullptr), static_cast<uint32_t>(SER_INVALID_PARAM));

    uintptr_t address = NN_NO100;
    EXPECT_EQ(ubs_hcom_channel_recv(channel, serviceContext, address, 0, nullptr),
        static_cast<uint32_t>(SER_INVALID_PARAM));

    MOCKER_CPP_VIRTUAL(*ch, &UBSHcomChannel::Recv).stubs().will(returnValue(static_cast<int>(SER_OK)));
    uint32_t size = NN_NO1024;
    EXPECT_EQ(ubs_hcom_channel_recv(channel, serviceContext, address, size, nullptr), static_cast<uint32_t>(SER_OK));

    ubs_hcom_channel_callback cb;
    cb.cb = CommonCb;
    cb.arg = NULL;
    EXPECT_EQ(ubs_hcom_channel_recv(channel, serviceContext, address, size, &cb), static_cast<uint32_t>(SER_OK));
    channel = 0;
    delete ch;
}

TEST_F(TestHcomCapi, SetTwoSideThreshold)
{
    ubs_hcom_twoside_threshold twoSideThreshold;
    twoSideThreshold.splitThreshold = NN_NO8192;
    twoSideThreshold.rndvThreshold = NN_NO8192;
    EXPECT_EQ(ubs_hcom_channel_set_twoside_threshold(0, twoSideThreshold), static_cast<uint32_t>(SER_INVALID_PARAM));

    InnerConnectOptions opt{};
    UBSHcomChannel *ch = new (std::nothrow) HcomChannelImp(0, false, opt);
    EXPECT_NE(ch, nullptr);
    ubs_hcom_channel channel = reinterpret_cast<ubs_hcom_channel>(ch);
    MOCKER_CPP_VIRTUAL(*ch, &UBSHcomChannel::SetTwoSideThreshold).stubs().will(returnValue(static_cast<int>(SER_OK)));
    EXPECT_EQ(ubs_hcom_channel_set_twoside_threshold(channel, twoSideThreshold), static_cast<uint32_t>(SER_OK));

    channel = 0;
    delete ch;
}
}
}