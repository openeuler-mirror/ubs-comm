/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the unit test for cli client, etc
 * Author:
 * Create: 2026-02-25
 * Note:
 * History: 2026-02-25
*/

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "file_descriptor.h"
#include "cli_args_parser.h"
#include "cli_terminal_display.h"
#include "cli_client.h"

namespace Statistics {
class CLIClientTest : public testing::Test {
public:
    void SetUp() override {}

    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

TEST_F(CLIClientTest, Query)
{
    CLIMessage response;
    CLIArgsParser::ParsedArgs args;
    CLIClient client("ubscli-", 0);

    MOCKER_CPP(&CLIClient::IsServerAvailable)
        .stubs()
        .will(returnValue(false))
        .then(returnValue(true));
    MOCKER(socket)
        .stubs()
        .will(returnValue(-1))
        .then(returnValue(1));
    MOCKER(strncpy_s)
        .stubs()
        .will(returnValue(-1))
        .then(returnValue(0));
    MOCKER(connect)
        .stubs()
        .with(eq(1), any(), any())
        .will(returnValue(-1))
        .then(returnValue(0));
    MOCKER_CPP(&CLIClient::SetSocketTimeout)
        .stubs()
        .with(eq(1))
        .will(returnValue(-1))
        .then(returnValue(0));
    MOCKER_CPP(&CLIClient::ProcessStat).stubs().will(returnValue(0));
    MOCKER_CPP(&CLIClient::ProcessTopo).stubs().will(returnValue(-1));

    int ret = client.Query(args, response);
    EXPECT_EQ(ret, -1);

    ret = client.Query(args, response);
    EXPECT_EQ(ret, -1);

    ret = client.Query(args, response);
    EXPECT_EQ(ret, -1);

    ret = client.Query(args, response);
    EXPECT_EQ(ret, -1);

    ret = client.Query(args, response);
    EXPECT_EQ(ret, -1);

    args.command = CLICommand::STAT;
    ret = client.Query(args, response);
    EXPECT_EQ(ret, 0);

    args.command = CLICommand::TOPO;
    ret = client.Query(args, response);
    EXPECT_EQ(ret, -1);

    args.command = CLICommand::INVALID;
    ret = client.Query(args, response);
    EXPECT_EQ(ret, 0);
}

TEST_F(CLIClientTest, ProcessStat)
{
    CLIArgsParser::ParsedArgs args{};
    CLIClient client("ubscli-", args.pid);
    CLIMessage response{};

    MOCKER(SocketFd::SendSocketData)
        .stubs()
        .will(returnValue(0))
        .then(returnValue(sizeof(CLIControlHeader)));
    EXPECT_EQ(client.ProcessStat(1, response), -1);
    EXPECT_EQ(client.ProcessStat(1, response), -1);
}

TEST_F(CLIClientTest, ProcessTopo)
{
    int sockfd = 1;
    CLIMessage response;
    CLIArgsParser::ParsedArgs args;
    CLIClient client("ubscli-", 0);

    MOCKER(inet_pton)
        .stubs()
        .will(returnValue(0))
        .then(returnValue(1));
    int ret = client.ProcessTopo(sockfd, response, args);
    EXPECT_EQ(ret, -1);

    ret = client.ProcessTopo(sockfd, response, args);
    EXPECT_EQ(ret, -1);
}

TEST_F(CLIClientTest, IsServerAvailable)
{
    CLIClient client("ubscli-", 0);

    MOCKER(access)
        .stubs()
        .will(returnValue(-1))
        .then(returnValue(0));

    bool ret = client.IsServerAvailable();
    EXPECT_EQ(ret, true);

    ret = client.IsServerAvailable();
    EXPECT_EQ(ret, false);
}

TEST_F(CLIClientTest, SetSocketTimeout)
{
    CLIClient client("ubscli-", 0);
    int sockFd = 1;

    MOCKER(setsockopt)
        .stubs()
        .will(returnValue(-1))
        .then(returnValue(0));

    int ret = client.SetSocketTimeout(sockFd);
    EXPECT_EQ(ret, -1);

    ret = client.SetSocketTimeout(sockFd);
    EXPECT_EQ(ret, 0);
}
}