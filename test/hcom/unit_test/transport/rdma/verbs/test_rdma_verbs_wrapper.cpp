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
#include <unistd.h>
#include <utility>

#include "hcom_utils.h"
#include "net_common.h"
#include "rdma_verbs_wrapper_qp.h"
#include "rdma_mr_dm_buf.h"
#include "rdma_mr_fixed_buf.h"
#include "rdma_verbs_wrapper_ctx.h"

namespace ock {
namespace hcom {
class TestRdmaVerbsWrapper : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);
};

void TestRdmaVerbsWrapper::SetUp()
{
}

void TestRdmaVerbsWrapper::TearDown()
{
}

TEST_F(TestRdmaVerbsWrapper, DeviceHelperUnInitialize)
{
    RDMADeviceHelper::G_Inited = true;
    EXPECT_NO_FATAL_FAILURE(RDMADeviceHelper::UnInitialize());
    EXPECT_EQ(RDMADeviceHelper::G_Inited, false);
}

inline NResult MockFilterIp(const std::string &ipMask, std::vector<std::string> &outIps)
{
    outIps.emplace_back("192.168.0.0");
    return 0;
}

inline RResult MockGetDeviceByIp(const std::string &ip, RDMAGId &gid)
{
    gid.devIndex = 0;
    return RR_OK;
}

TEST_F(TestRdmaVerbsWrapper, DeviceHelperGetEnableDeviceCountWithEmptyMatchIp)
{
    std::vector<std::string> enableIps;
    uint16_t enableCount = 0;
    RResult result = RDMADeviceHelper::GetEnableDeviceCount("12345", enableCount, enableIps, "");
    EXPECT_EQ(result, NN_INVALID_IP);

    MOCKER_CPP(&FilterIp).stubs().will(returnValue(0)).then(invoke(MockFilterIp));
    result = 0;
    result = RDMADeviceHelper::GetEnableDeviceCount("192.168.0.0/24", enableCount, enableIps, "");
    EXPECT_EQ(result, NN_INVALID_IP);
}

TEST_F(TestRdmaVerbsWrapper, DeviceHelperGetEnableDeviceCountWithMatchIp)
{
    std::vector<std::string> enableIps;
    uint16_t enableCount = 0;
    MOCKER(RDMADeviceHelper::Initialize).stubs().will(returnValue(205)).then(returnValue(0));
    MOCKER_CPP(&FilterIp).stubs().will(invoke(MockFilterIp));

    RResult result = RDMADeviceHelper::GetEnableDeviceCount("192.168.0.0/24", enableCount, enableIps, "");
    EXPECT_EQ(result, RR_DEVICE_FAILED_OPEN);

    RDMADeviceSimpleInfo simpleInfo {};
    simpleInfo.active = true;
    RDMADeviceHelper::G_RDMADevMap[0] = simpleInfo;
    MOCKER_CPP(&RDMADeviceHelper::GetDeviceByIp).stubs().will(invoke(MockGetDeviceByIp));
    result = RDMADeviceHelper::GetEnableDeviceCount("192.168.0.0/24", enableCount, enableIps, "");
    EXPECT_EQ(enableCount, 1);
    EXPECT_EQ(result, RR_OK);
}

TEST_F(TestRdmaVerbsWrapper, OpResult)
{
    ibv_wc wc {};
    wc.status = IBV_WC_SUCCESS;
    EXPECT_EQ(RDMAOpContextInfo::OpResult(wc), RDMAOpContextInfo::OpResultType::SUCCESS);
    wc.status = IBV_WC_RETRY_EXC_ERR;
    EXPECT_EQ(RDMAOpContextInfo::OpResult(wc), RDMAOpContextInfo::OpResultType::ERR_TIMEOUT);
    wc.status = IBV_WC_RNR_RETRY_EXC_ERR;
    EXPECT_EQ(RDMAOpContextInfo::OpResult(wc), RDMAOpContextInfo::OpResultType::ERR_TIMEOUT);
    wc.status = IBV_WC_WR_FLUSH_ERR;
    EXPECT_EQ(RDMAOpContextInfo::OpResult(wc), RDMAOpContextInfo::OpResultType::ERR_CANCELED);
    wc.status = IBV_WC_LOC_LEN_ERR;
    EXPECT_EQ(RDMAOpContextInfo::OpResult(wc), RDMAOpContextInfo::OpResultType::ERR_IO_ERROR);
}

TEST_F(TestRdmaVerbsWrapper, GetNResult)
{
    RDMAOpContextInfo::OpResultType type = RDMAOpContextInfo::OpResultType::SUCCESS;
    EXPECT_EQ(RDMAOpContextInfo::GetNResult(type), NN_OK);
    type = RDMAOpContextInfo::OpResultType::ERR_TIMEOUT;
    EXPECT_EQ(RDMAOpContextInfo::GetNResult(type), NN_MSG_TIMEOUT);
    type = RDMAOpContextInfo::OpResultType::ERR_CANCELED;
    EXPECT_EQ(RDMAOpContextInfo::GetNResult(type), NN_MSG_CANCELED);
    type = RDMAOpContextInfo::OpResultType::ERR_EP_BROKEN;
    EXPECT_EQ(RDMAOpContextInfo::GetNResult(type), NN_EP_BROKEN);
    type = RDMAOpContextInfo::OpResultType::ERR_EP_CLOSE;
    EXPECT_EQ(RDMAOpContextInfo::GetNResult(type), NN_EP_CLOSE);
    type = RDMAOpContextInfo::OpResultType::ERR_IO_ERROR;
    EXPECT_EQ(RDMAOpContextInfo::GetNResult(type), NN_MSG_ERROR);
}

TEST_F(TestRdmaVerbsWrapper, DoInitialize)
{
    MOCKER_CPP(RDMADeviceHelper::DoUpdate).stubs()
        .will(returnValue(static_cast<int>(RR_DEVICE_FAILED_OPEN)));
    EXPECT_EQ(RDMADeviceHelper::DoInitialize(), RR_DEVICE_FAILED_OPEN);
    EXPECT_NO_FATAL_FAILURE(RDMADeviceHelper::Update());
}

TEST_F(TestRdmaVerbsWrapper, DoUpdate)
{
    std::vector<RDMAGId> outGidVec;
    EXPECT_NO_FATAL_FAILURE(RDMADeviceHelper::GetGidVec(nullptr, "name", 0, 0, 0, outGidVec));

    ibv_context ctx {};
    VerbsAPI::hcomInnerQueryGid =
        [](struct ibv_context *context, uint8_t port_num, int index, union ibv_gid *gid) { return 1; };
    EXPECT_NO_FATAL_FAILURE(RDMADeviceHelper::GetGidVec(&ctx, "name", 0, 0, 1, outGidVec));

    VerbsAPI::hcomInnerQueryGid =
        [](struct ibv_context *context, uint8_t port_num, int index, union ibv_gid *gid) { return 0; };
    EXPECT_NO_FATAL_FAILURE(RDMADeviceHelper::GetGidVec(&ctx, "name", 0, 0, 1, outGidVec));
}

TEST_F(TestRdmaVerbsWrapper, StrToRoCEVersion)
{
    EXPECT_EQ(RDMADeviceHelper::StrToRoCEVersion("IB/RoCE v1"), RoCE_V1);
    EXPECT_EQ(RDMADeviceHelper::StrToRoCEVersion("RoCE v2"), RoCE_V2);
    EXPECT_EQ(RDMADeviceHelper::StrToRoCEVersion("5555"), RoCE_V15);
}

TEST_F(TestRdmaVerbsWrapper, RDMAContextInitializeFail)
{
    RDMAGId gid {};
    RDMAContext ctx {"name", false, gid};
    ibv_context ctx1 {};
    ctx.mContext = &ctx1;

    MOCKER_CPP(RDMAContext::UnInitialize).stubs()
        .will(returnValue(static_cast<int>(RR_OK)));

    EXPECT_EQ(ctx.Initialize(), RR_OK);
    ctx.mContext = nullptr;
}

TEST_F(TestRdmaVerbsWrapper, Initialize)
{
    RDMAGId gid {};
    RDMAContext ctx {"name", false, gid};
    MOCKER_CPP(RDMADeviceHelper::Update).stubs()
        .will(returnValue(static_cast<int>(RR_DEVICE_FAILED_OPEN)))
        .then(returnValue(static_cast<int>(RR_OK)));
    EXPECT_NO_FATAL_FAILURE(ctx.UpdateGid("IP"));
    EXPECT_NO_FATAL_FAILURE(ctx.UpdateGid("IP"));
}

TEST_F(TestRdmaVerbsWrapper, RDMACqInitializeFail)
{
    RDMACq cq {"name", nullptr};

    MOCKER_CPP(RDMACq::UnInitialize).stubs()
        .will(returnValue(static_cast<int>(RR_OK)));
    ibv_cq ibvCq {};
    EXPECT_EQ(cq.Initialize(), RR_PARAM_INVALID);
    cq.mCompletionQueue = &ibvCq;
    EXPECT_EQ(cq.Initialize(), RR_OK);
    int count = 0;
    EXPECT_EQ(cq.ProgressV(nullptr, count), RR_CQ_NOT_INITIALIZED);
}

TEST_F(TestRdmaVerbsWrapper, RDMAQpCreateFail)
{
    RDMAQp qp {"name", 0, nullptr, nullptr};
    MOCKER_CPP(RDMAQp::UnInitialize).stubs()
        .will(returnValue(static_cast<int>(RR_OK)));
    EXPECT_EQ(qp.CreateIbvQp(), RR_PARAM_INVALID);
    RDMAQpExchangeInfo info {};
    EXPECT_EQ(qp.ChangeToReady(info), RR_QP_CHANGE_STATE_FAILED);
    EXPECT_EQ(qp.GetExchangeInfo(info), RR_QP_NOT_INITIALIZED);

    MOCKER_CPP(RDMAMemoryRegionFixedBuffer::Create).stubs()
        .will(returnValue(static_cast<int>(RR_PARAM_INVALID)));
    EXPECT_EQ(qp.CreateQpMr(), RR_PARAM_INVALID);

    RDMAMemoryRegionFixedBuffer mr {"name", nullptr, 0, 0};
    qp.mQpMr = &mr;

    uintptr_t item = 0;
    EXPECT_EQ(qp.GetFreeBuff(item), false);
}

TEST_F(TestRdmaVerbsWrapper, GetEnableDeviceCount)
{
    int ret;
    std::string ipMask(NN_NO1024, 'a');
    uint16_t enableDevCount = 0;
    std::vector<std::string> enableIps{};
    std::string ipGroup{};

    ret = RDMADeviceHelper::GetEnableDeviceCount(ipMask, enableDevCount, enableIps, ipGroup);
    EXPECT_EQ(ret, NN_INVALID_IP);
}

TEST_F(TestRdmaVerbsWrapper, GetMaxRdAtomic)
{
    setenv("HCOM_MAX_RD_ATOMIC", "2", 1);
    auto ret = GetMaxRdAtomic();
    const uint8_t RESULT = 2;
    printf("GetMaxRdAtomic ret: %d\n", ret);
    EXPECT_EQ(ret, RESULT);
}

}
}
