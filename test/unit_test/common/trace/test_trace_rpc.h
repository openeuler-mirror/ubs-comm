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

#ifndef DFS_TRACER_TEST_TRACE_RPC_H
#define DFS_TRACER_TEST_TRACE_RPC_H

#include <thread>
#include "gtest/gtest.h"
#include "htracer_manager.h"
#include "rpc_server.h"
#include "htracer_msg.h"

class TestRpc : public testing::Test {
public:
    TestRpc() {}
    ~TestRpc() {}

    // TestCase only enter once
    static void SetUpTestCase();
    static void TearDownTestCase();

    // every TEST_F macro will enter one
    void SetUp() const;
    void TearDown();
    ock::hcom::RpcServer *mRpcServer = nullptr;
};


#endif // DFS_TRACER_TEST_TRACE_RPC_H
