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


#include "test_trace_rpc.h"
#include "htracer_service_helper.h"
#include "rpc_server.h"
#include "rpc_msg.h"

using namespace std;
using namespace ock::hcom;
void TestRpc::SetUp() const {}
void TestRpc::TearDown()
{
    if (mRpcServer != nullptr) {
        delete mRpcServer;
        mRpcServer = nullptr;
    }
}

void TestRpc::SetUpTestCase() {}
void TestRpc::TearDownTestCase() {}

TEST_F(TestRpc, test_one_rpc_sercer_start_return_true_shutdown_return_true)
{
    int port = 12345;
    if (mRpcServer == nullptr) {
        mRpcServer = new (std::nothrow) RpcServer();
    }
    EXPECT_NE(mRpcServer, nullptr);
    EXPECT_EQ(mRpcServer->Start(std::to_string(port)), NN_OK);

    mRpcServer->Stop();
}

TEST_F(TestRpc, test_one_rpc_sercer_start_18_port_return_true)
{
    if (mRpcServer == nullptr) {
        mRpcServer = new (std::nothrow) RpcServer();
    }
    EXPECT_NE(mRpcServer, nullptr);
    for (int port = 50000; port < 50100; port++) {
        EXPECT_EQ(mRpcServer->Start(std::to_string(port)), NN_OK);
    }

    mRpcServer->Stop();
}

TEST_F(TestRpc, test_msg_validate_0_size)
{
    QueryTraceInfoRequest *queryRequest = static_cast<QueryTraceInfoRequest *>(malloc(sizeof(QueryTraceInfoRequest)));
    queryRequest->serviceId = 1;
    Message request(queryRequest, 0);
    ASSERT_EQ(MessageValidator::Validate(request), false);
}

TEST_F(TestRpc, test_msg_validate_nullptr_msg)
{
    QueryTraceInfoRequest *queryRequest = static_cast<QueryTraceInfoRequest *>(malloc(sizeof(QueryTraceInfoRequest)));
    Message request(nullptr, sizeof(QueryTraceInfoRequest));
    ASSERT_EQ(MessageValidator::Validate(request), false);
}

TEST_F(TestRpc, test_msg_validate_normal_msg)
{
    QueryTraceInfoRequest *queryRequest = static_cast<QueryTraceInfoRequest *>(malloc(sizeof(QueryTraceInfoRequest)));
    queryRequest->serviceId = 1;
    queryRequest->version = VERSION;
    queryRequest->magicCode = MAGIC_CODE;
    queryRequest->bodySize = 0;

    Message request(queryRequest, sizeof(QueryTraceInfoRequest));
    ASSERT_EQ(MessageValidator::Validate(request), true);
}