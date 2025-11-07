// Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <mockcpp/mockcpp.hpp>

#include "hcom_num_def.h"
#include "rpc_server.h"
#include "securec.h"

namespace ock {
namespace hcom {
class TestHTracerRpcServer : public testing::Test {
public:
    void SetUp() override {}

    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

TEST_F(TestHTracerRpcServer, TestRpcServerStartSuccess)
{
    auto FakePort = NN_NO7200;
    auto rpcServer = new (std::nothrow) RpcServer();
    EXPECT_NE(rpcServer, nullptr);
    EXPECT_EQ(rpcServer->Start(std::to_string(FakePort)), NN_OK);
    rpcServer->Stop();
    delete rpcServer;
}

TEST_F(TestHTracerRpcServer, TestRpcServerMemSetFailed)
{
    auto FakePort = NN_NO7200;
    auto rpcServer = new (std::nothrow) RpcServer();
    EXPECT_NE(rpcServer, nullptr);
    MOCKER_CPP(memset_s).stubs().will(returnValue(1)).then(returnValue(-1));
    EXPECT_EQ(rpcServer->Start(std::to_string(FakePort)), SER_ERROR);
    rpcServer->Stop();
    delete rpcServer;
}

TEST_F(TestHTracerRpcServer, TestRpcServerMemCpyFailed)
{
    auto FakePort = NN_NO7200;
    auto rpcServer = new (std::nothrow) RpcServer();
    EXPECT_NE(rpcServer, nullptr);
    MOCKER_CPP(memcpy_s).stubs().will(returnValue(1)).then(returnValue(-1));
    EXPECT_EQ(rpcServer->Start(std::to_string(FakePort)), SER_ERROR);
    rpcServer->Stop();
    delete rpcServer;
}
} // namespace hcom
} // namespace ock
