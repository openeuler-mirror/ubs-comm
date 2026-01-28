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
#include <string>
#include <iostream>

#include "rpc_adpt_vlog.h"


class RpcAdptVlogTest : public testing::Test {
public:
    RpcAdptVlogTest()
        : vlogCtx(nullptr),
        originalLevel(ubsocket::UTIL_VLOG_LEVEL_INFO),
        originalFunction(nullptr)
    {
        // empty
    }

    void SetUp() override
    {
        int utilSuccess = 0;
        int utilFail = -1;
        int setLogOutputRes = utilFail;
        vlogCtx = RpcAdptGetLogCtx();
        if (vlogCtx) {
            originalLevel = vlogCtx->level;
            originalFunction = vlogCtx->vlog_output_func;
        }
        // reset the output function for display in linux
        setLogOutputRes = RpcAdptSetLogCtx(ubsocket::originalLevel);
        if (setLogOutputRes != utilSuccess) {
            std::cout << "RpcAdptSetLogCtx error, set log output res is: " << setLogOutputRes << std::endl;
            std::cout << "It makes your vlog only shows in syslog." << std::endl;
        }
    }

    void TearDown() override
    {
        RpcAdptVlogCtxSet(originalLevel, nullptr);

        if (vlogCtx && originalFunction) {
            vlogCtx->vlog_output_func = originalFunction;
        }

        GlobalMockObject::verify();

        vlogCtx = nullptr;
        originalFunction = nullptr;
    }

private:
    ubsocket::util_vlog_ctx_t *vlogCtx;
    ubsocket::util_vlog_level_t originalLevel;
    void (*originalFunction)(int, char*);
};
// RPC_ADPT_VLOG_ERR
TEST_F(RpcAdptVlogTest, TestRpcAdptVlogErr_ShouldNotOutput)
{
    RpcAdptVlogCtxSet(ubsocket::UTIL_VLOG_LEVEL_CRIT, nullptr);
    testing::internal::CaptureStdout();
    RPC_ADPT_VLOG_ERR("Test error message");
    std::string output = testing::internal::GetCapturedStdout();
    // low level, catch nothing
    std::cout << output << std::endl;
    EXPECT_TRUE(output.empty());
}

TEST_F(RpcAdptVlogTest, TestRpcAdptVlogErr_ShouldOutput)
{
    RpcAdptVlogCtxSet(ubsocket::UTIL_VLOG_LEVEL_ERR, nullptr);
    testing::internal::CaptureStdout();
    RPC_ADPT_VLOG_ERR("Test error message");
    std::string output = testing::internal::GetCapturedStdout();
    // high level, catch vlog output
    std::cout << output << std::endl;
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("Test error message"), std::string::npos);
}
// RPC_ADPT_VLOG_WARN
TEST_F(RpcAdptVlogTest, TestRpcAdptVlogWarn_ShouldNotOutput)
{
    RpcAdptVlogCtxSet(ubsocket::UTIL_VLOG_LEVEL_ERR, nullptr);
    testing::internal::CaptureStdout();
    RPC_ADPT_VLOG_WARN("Test warning message");
    std::string output = testing::internal::GetCapturedStdout();
    std::cout << output << std::endl;
    EXPECT_TRUE(output.empty());
}

TEST_F(RpcAdptVlogTest, TestRpcAdptVlogWarn_ShouldOutput)
{
    RpcAdptVlogCtxSet(ubsocket::UTIL_VLOG_LEVEL_WARN, nullptr);
    testing::internal::CaptureStdout();
    RPC_ADPT_VLOG_WARN("Test warning message");
    std::string output = testing::internal::GetCapturedStdout();
    std::cout << output << std::endl;
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("Test warning message"), std::string::npos);
}
// RPC_ADPT_VLOG_NOTICE
TEST_F(RpcAdptVlogTest, TestRpcAdptVlogNotice_ShouldNotOutput)
{
    RpcAdptVlogCtxSet(ubsocket::UTIL_VLOG_LEVEL_WARN, nullptr);
    testing::internal::CaptureStdout();
    RPC_ADPT_VLOG_NOTICE("Test notice message");
    std::string output = testing::internal::GetCapturedStdout();
    std::cout << output << std::endl;
    EXPECT_TRUE(output.empty());
}

TEST_F(RpcAdptVlogTest, TestRpcAdptVlogNotice_ShouldOutput)
{
    RpcAdptVlogCtxSet(ubsocket::UTIL_VLOG_LEVEL_NOTICE, nullptr);
    testing::internal::CaptureStdout();
    RPC_ADPT_VLOG_NOTICE("Test notice message");
    std::string output = testing::internal::GetCapturedStdout();
    std::cout << output << std::endl;
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("Test notice message"), std::string::npos);
}
// RPC_ADPT_VLOG_INFO
TEST_F(RpcAdptVlogTest, TestRpcAdptVlogInfo_ShouldNotOutput)
{
    RpcAdptVlogCtxSet(ubsocket::UTIL_VLOG_LEVEL_NOTICE, nullptr);
    testing::internal::CaptureStdout();
    RPC_ADPT_VLOG_INFO("Test info message");
    std::string output = testing::internal::GetCapturedStdout();
    std::cout << output << std::endl;
    EXPECT_TRUE(output.empty());
}

TEST_F(RpcAdptVlogTest, TestRpcAdptVlogInfo_ShouldOutput)
{
    RpcAdptVlogCtxSet(ubsocket::UTIL_VLOG_LEVEL_INFO, nullptr);
    testing::internal::CaptureStdout();
    RPC_ADPT_VLOG_INFO("Test info message");
    std::string output = testing::internal::GetCapturedStdout();
    std::cout << output << std::endl;
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("Test info message"), std::string::npos);
}
// RPC_ADPT_VLOG_DEBUG
TEST_F(RpcAdptVlogTest, TestRpcAdptVlogDebug_ShouldNotOutput)
{
    RpcAdptVlogCtxSet(ubsocket::UTIL_VLOG_LEVEL_INFO, nullptr);
    testing::internal::CaptureStdout();
    RPC_ADPT_VLOG_DEBUG("Test debug message");
    std::string output = testing::internal::GetCapturedStdout();
    std::cout << output << std::endl;
    EXPECT_TRUE(output.empty());
}

TEST_F(RpcAdptVlogTest, TestRpcAdptVlogDebug_ShouldOutput)
{
    RpcAdptVlogCtxSet(ubsocket::UTIL_VLOG_LEVEL_DEBUG, nullptr);
    testing::internal::CaptureStdout();
    RPC_ADPT_VLOG_DEBUG("Test debug message");
    std::string output = testing::internal::GetCapturedStdout();
    std::cout << output << std::endl;
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("Test debug message"), std::string::npos);
}