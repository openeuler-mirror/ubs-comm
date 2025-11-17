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


#include "test_trace_service.h"
#include <vector>
#include <cstdlib>
#include <cstdio>
#include "htracer.h"


namespace ock {
namespace hcom {
void TestService::SetUp() const {}
void TestService::TearDown() const {}

void TestService::SetUpTestCase() {}
void TestService::TearDownTestCase() {}

constexpr uint32_t TRACE_ID_0 = TRACE_ID(0, NN_NO1);
constexpr uint32_t TRACE_ID_1 = TRACE_ID(1, NN_NO1);
constexpr uint32_t TRACE_ID_2 = TRACE_ID(2, NN_NO0);

TEST_F(TestService, test_one_trace_service_start_return_true_shutdown_return_true)
{
    g_traceService = new HTracerService();
    EXPECT_EQ(g_traceService->StartUp("1234"), NN_OK);

    g_traceService->ShutDown();

    delete g_traceService;
    g_traceService = nullptr;
}

TEST_F(TestService, test_one_trace_service_create_port_return_false_when_used_and_return_true_when_new_port)
{
    g_traceService = new HTracerService();
    EXPECT_EQ(g_traceService->StartUp("12345"), NN_OK);
    EXPECT_EQ(g_traceService->StartUp("12346"), NN_OK);

    g_traceService->ShutDown();
    delete g_traceService;
    g_traceService = nullptr;
}

TEST_F(TestService, test_get_traceinfo_give_normal_value_id_return_normal_value)
{
    EnableHtrace(true);
    EXPECT_EQ(HTracerInit("30000"), NN_OK);
    TRACE_DELAY_BEGIN(TRACE_ID_0);
    TRACE_DELAY_END(TRACE_ID_0, 0);
    TRACE_DELAY_BEGIN(TRACE_ID_2);
    TRACE_DELAY_END(TRACE_ID_2, 0);

    auto tTranceInfos = TracerServiceHelper::GetTraceInfos(0, 0, TraceManager::IsLatencyQuantileEnable());
    ASSERT_EQ(tTranceInfos.size(), 1);
    for (int i = 0; i < 1; i++) {
        EXPECT_EQ(tTranceInfos[i].begin, 1);
        EXPECT_NE(tTranceInfos[i].total, 0);
        EXPECT_EQ(tTranceInfos[i].goodEnd, 1);
        EXPECT_EQ(tTranceInfos[i].badEnd, 0);
    }

    auto tTranceInfos1 = TracerServiceHelper::GetTraceInfos(2, 0, TraceManager::IsLatencyQuantileEnable());
    ASSERT_EQ(tTranceInfos1.size(), 1);
    for (int i = 0; i < 1; i++) {
        EXPECT_EQ(tTranceInfos1[i].begin, 1);
        EXPECT_NE(tTranceInfos1[i].total, 0);
        EXPECT_EQ(tTranceInfos1[i].goodEnd, 1);
        EXPECT_EQ(tTranceInfos1[i].badEnd, 0);
    }
    HTracerExit();
}

TEST_F(TestService, test_get_traceinfo_give_morethan_MAX_SERVICE_NUM_return_empty)
{
    EXPECT_EQ(HTracerInit("30001"), NN_OK);
    TRACE_DELAY_BEGIN(TRACE_ID_0);
    TRACE_DELAY_END(TRACE_ID_0, 0);
    TRACE_DELAY_BEGIN(TRACE_ID_2);
    TRACE_DELAY_END(TRACE_ID_2, 0);
    auto tTranceInfos = TracerServiceHelper::GetTraceInfos(MAX_SERVICE_NUM + 1,
        0, TraceManager::IsLatencyQuantileEnable());

    EXPECT_EQ(tTranceInfos.size(), 0);

    HTracerExit();
}

TEST_F(TestService, test_get_traceinfo_give_invalid_value_return_all_records)
{
    TracerServiceHelper::ResetTraceInfos();
    EXPECT_EQ(HTracerInit("30002"), NN_OK);
    TRACE_DELAY_BEGIN(TRACE_ID_0);
    TRACE_DELAY_END(TRACE_ID_0, 0);
    TRACE_DELAY_BEGIN(TRACE_ID_2);
    TRACE_DELAY_END(TRACE_ID_2, 0);
    auto tTranceInfos = TracerServiceHelper::GetTraceInfos(INVALID_SERVICE_ID,
        0, TraceManager::IsLatencyQuantileEnable());

    EXPECT_EQ(tTranceInfos.size(), NN_NO2);
    for (int i = 0; i < NN_NO2; i++) {
        EXPECT_EQ(tTranceInfos[i].begin, 1);
        EXPECT_NE(tTranceInfos[i].total, 0);
        EXPECT_EQ(tTranceInfos[i].goodEnd, 1);
        EXPECT_EQ(tTranceInfos[i].badEnd, 0);
    }
    HTracerExit();
}

TEST_F(TestService, test_sent_response_return_ok)
{
    g_traceService = new HTracerService();
    EXPECT_EQ(g_traceService->StartUp("33333"), NN_OK);
    Message response(nullptr, 0);
}


TEST_F(TestService, test_sent_request_nullptr_return_false)
{
    g_traceService = new HTracerService();
    EXPECT_EQ(g_traceService->StartUp("33331"), NN_OK);
    Message response(nullptr, 0);
    Message request(nullptr, 0);
    g_traceService->ShutDown();
    delete g_traceService;
    g_traceService = nullptr;
}

TEST_F(TestService, test_sent_response_nullptr_return_ok)
{
    g_traceService = new HTracerService();
    EXPECT_EQ(g_traceService->StartUp("33334"), NN_OK);
    Message response(nullptr, 0);

    int32_t recvBufferSize = 1024;
    char *recvBuffer = static_cast<char *>(malloc(recvBufferSize));
    Message request(recvBuffer, recvBufferSize);

    g_traceService->ShutDown();
    delete g_traceService;
    g_traceService = nullptr;
}

TEST_F(TestService, test_sent_request_opcode_TRACE_OP_MODIFY_return_true)
{
    g_traceService = new HTracerService();
    EXPECT_EQ(g_traceService->StartUp("33335"), NN_OK);
    Message response(nullptr, 0);

    QueryTraceInfoRequest *queryRequest =
        static_cast<QueryTraceInfoRequest *>(malloc(sizeof(QueryTraceInfoRequest)));
    queryRequest->serviceId = 1;
    queryRequest->opcode = INVALID_OPCODE;
    Message request(queryRequest, sizeof(QueryTraceInfoRequest));
    g_traceService->ShutDown();
    delete g_traceService;
    g_traceService = nullptr;
}

TEST_F(TestService, test_sent_request_opcode_TRACE_OP_QUERY_return_true)
{
    g_traceService = new HTracerService();
    EXPECT_EQ(g_traceService->StartUp("33336"), NN_OK);
    Message response(nullptr, 0);

    QueryTraceInfoRequest *queryRequest =
        static_cast<QueryTraceInfoRequest *>(malloc(sizeof(QueryTraceInfoRequest)));
    queryRequest->serviceId = 1;

    Message request(queryRequest, sizeof(queryRequest));
    g_traceService->ShutDown();
    delete g_traceService;
    g_traceService = nullptr;
}

TEST_F(TestService, TestTraceManagerBegin)
{
    EXPECT_NO_FATAL_FAILURE(TraceManager::DelayBegin(NN_NO4, "name"));
}

TEST_F(TestService, TestBuildMessage)
{
    Message msg {};
    ResetTraceInfoResponse::BuildMessage(msg);
}

TEST_F(TestService, TestCentroidList)
{
    CentroidList lst {2};
    EXPECT_EQ(lst.Insert(1, NN_NO2), InsertResultCode::NO_NEED_COMPERSS);
    EXPECT_EQ(lst.Insert(1, NN_NO2), InsertResultCode::NEED_COMPERSS);
    EXPECT_EQ(lst.Insert(-1, NN_NO2), InsertResultCode::NO_NEED_COMPERSS);
}

TEST_F(TestService, TestCentroid_GetMean)
{
    Centroid centroid(1.0, 1);
    EXPECT_EQ(centroid.GetMean(), 1.0);
}

TEST_F(TestService, TestCentroid_GetWeight)
{
    Centroid centroid(1.0, 1);
    EXPECT_EQ(centroid.GetWeight(), 1);
}

TEST_F(TestService, TestCentroidList_GetAndSetCentroids)
{
    CentroidList centroidList(1);
    centroidList.Insert(1.0, 1);
    std::vector<Centroid> centroids = centroidList.GetAndSetCentroids();
    EXPECT_EQ(centroids.size(), 1);
    EXPECT_EQ(centroids[0].GetWeight(), 1);
}

TEST_F(TestService, TestTdigest)
{
    Tdigest tdigest(NN_NO100);
    for (int i = 1; i <= NN_NO100; i++) {
        tdigest.Insert(i);
    }
    tdigest.Merge();
    double p90 = tdigest.Quantile(90);
    tdigest.Reset();
}
}  // namespace hcom
}  // namespace ock
