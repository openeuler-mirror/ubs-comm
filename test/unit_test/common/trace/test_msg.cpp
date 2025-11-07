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

#include "test_msg.h"

using namespace std;
using namespace ock::hcom;
void TestMsg::SetUp() const {}
void TestMsg::TearDown() const {}

void TestMsg::SetUpTestCase() {}
void TestMsg::TearDownTestCase() {}

TEST_F(TestMsg, test_msg_response_give_nullptr_return_ok)
{
    EXPECT_EQ(HTracerInit("30000"), NN_OK);

    TRACE_DELAY_BEGIN(0);
    TRACE_DELAY_END(0, 0);
    auto tTranceInfos = TracerServiceHelper::GetTraceInfos(0, 0, TraceManager::IsLatencyQuantileEnable());
    Message response(nullptr, 0);
    EXPECT_EQ(QueryTraceInfoResponse::BuildMessage(tTranceInfos, response), NN_OK);
    HTracerExit();
}

TEST_F(TestMsg, test_msg_response_give_new_return_true)
{
    EXPECT_EQ(HTracerInit("2999"), NN_OK);
    QueryTraceInfoRequest *queryRequest = static_cast<QueryTraceInfoRequest *>(malloc(sizeof(QueryTraceInfoRequest)));
    queryRequest->serviceId = 1;
    Message request(queryRequest, sizeof(QueryTraceInfoRequest));

    TRACE_DELAY_BEGIN(0);
    TRACE_DELAY_END(0, 0);
    TRACE_DELAY_BEGIN(1);
    TRACE_DELAY_END(1, 0);
    TRACE_DELAY_BEGIN(2);
    TRACE_DELAY_END(2, 0);

    auto tTranceInfos = TracerServiceHelper::GetTraceInfos(0, 0, TraceManager::IsLatencyQuantileEnable());

    uint32_t bodySize = sizeof(uint32_t) + sizeof(TTraceInfo) * tTranceInfos.size();
    uint32_t messageSize = sizeof(MessageHeader) + bodySize;
    Message queryResponse {};

    EXPECT_EQ(QueryTraceInfoResponse::BuildMessage(tTranceInfos, queryResponse), NN_OK);

    HTracerExit();
}

TEST_F(TestMsg, test_msg_give_request_return_true)
{
    QueryTraceInfoRequest queryRequest;
    queryRequest.serviceId = 1;
    EXPECT_EQ(queryRequest.serviceId, 1);
    EXPECT_EQ(queryRequest.bodySize, 0);
    EXPECT_EQ(queryRequest.crc, 0);
    EXPECT_EQ(queryRequest.version, VERSION);
    EXPECT_EQ(queryRequest.magicCode, MAGIC_CODE);
    EXPECT_EQ(queryRequest.opcode, TRACE_OP_QUERY);
}