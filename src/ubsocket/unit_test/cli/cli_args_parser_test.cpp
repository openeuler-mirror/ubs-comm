/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the unit test for cli args parser, etc
 * Author:
 * Create: 2026-02-25
 * Note:
 * History: 2026-02-25
*/

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "cli_args_parser.h"

namespace Statistics {
class CLIArgsParserTest : public testing::Test {
public:
    void SetUp() override {}

    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

TEST_F(CLIArgsParserTest, ParseStat)
{
    CLIArgsParser parser{};
    CLIArgsParser::ParsedArgs args{};

    MOCKER_CPP(&CLIArgsParser::IsCommandValid)
        .stubs()
        .will(returnValue(false))
        .then(returnValue(true));

    char* argv1[] = {
        static_cast<char*>("ubstat"),
        static_cast<char*>("-p"),
        static_cast<char*>("-1"),
        static_cast<char*>("stat"),
        nullptr
    };
    int argc1 = sizeof(argv1) / sizeof(char*) - 1;
    bool ret = parser.Parse(argc1, argv1, args);
    EXPECT_EQ(ret, false);
    optind = 0;

    char* argv2[] = {
        static_cast<char*>("ubstat"),
        static_cast<char*>("-p"),
        static_cast<char*>("1"),
        static_cast<char*>("stat"),
        nullptr
    };
    int argc2 = sizeof(argv2) / sizeof(char*) - 1;
    ret = parser.Parse(argc2, argv2, args);
    EXPECT_EQ(args.pid, 1); // pid
    optind = 0;

    char* argv3[] = {
        static_cast<char*>("ubstat"),
        static_cast<char*>("-w"),
        static_cast<char*>("stat"),
        nullptr
    };
    int argc3 = sizeof(argv3) / sizeof(char*) - 1;
    ret = parser.Parse(argc3, argv3, args);
    EXPECT_EQ(args.watch, true); // watch
    optind = 0;
}

TEST_F(CLIArgsParserTest, ParseStatErr)
{
    CLIArgsParser parser{};
    CLIArgsParser::ParsedArgs args{};

    MOCKER_CPP(&CLIArgsParser::IsCommandValid).stubs().will(returnValue(true));

    char* argv4[] = {
        static_cast<char*>("ubstat"),
        static_cast<char*>("-s"),
        static_cast<char*>("invalid_ipv6"),
        static_cast<char*>("stat"),
        nullptr
    };
    int argc4 = sizeof(argv4) / sizeof(char*) - 1;
    bool ret = parser.Parse(argc4, argv4, args);
    EXPECT_EQ(ret, false);
    optind = 0;

    char* argv5[] = {
        static_cast<char*>("ubstat"),
        static_cast<char*>("-s"),
        static_cast<char*>("::1"),
        static_cast<char*>("stat"),
        nullptr
    };
    int argc5 = sizeof(argv5) / sizeof(char*) - 1;
    ret = parser.Parse(argc5, argv5, args);
    EXPECT_EQ(ret, true);
    optind = 0;

    char* argv6[] = {
        static_cast<char*>("ubstat"),
        static_cast<char*>("-d"),
        static_cast<char*>("invalid_ipv6"),
        static_cast<char*>("stat"),
        nullptr
    };
    int argc6 = sizeof(argv6) / sizeof(char*) - 1;
    ret = parser.Parse(argc6, argv6, args);
    EXPECT_EQ(ret, false);
    optind = 0;

    char* argv7[] = {
        static_cast<char*>("ubstat"),
        static_cast<char*>("-d"),
        static_cast<char*>("::1"),
        static_cast<char*>("stat"),
        nullptr
    };
    int argc7 = sizeof(argv7) / sizeof(char*) - 1;
    ret = parser.Parse(argc7, argv7, args);
    EXPECT_EQ(ret, true);
    optind = 0;
}

TEST_F(CLIArgsParserTest, ParseTopo)
{
    CLIArgsParser parser{};
    CLIArgsParser::ParsedArgs args{};

    MOCKER_CPP(&CLIArgsParser::IsCommandValid).stubs().will(returnValue(true));

    char* argv8[] = {
        static_cast<char*>("ubstat"),
        static_cast<char*>("-h"),
        nullptr
    };
    int argc8 = sizeof(argv8) / sizeof(char*) - 1;
    bool ret = parser.Parse(argc8, argv8, args);
    EXPECT_EQ(ret, false);
    optind = 0;

    char* argv9[] = {
        static_cast<char*>("ubstat"),
        static_cast<char*>("-p"),
        static_cast<char*>("123"),
        nullptr
    };
    int argc9 = sizeof(argv9) / sizeof(char*) - 1;
    ret = parser.Parse(argc9, argv9, args);
    EXPECT_EQ(ret, false);
    optind = 0;

    char* argv10[] = {
        static_cast<char*>("ubstat"),
        static_cast<char*>("invalid_cmd"),
        nullptr
    };
    int argc10 = sizeof(argv10) / sizeof(char*) - 1;
    ret = parser.Parse(argc10, argv10, args);
    EXPECT_EQ(ret, true);
    optind = 0;

    char* argv11[] = {
        static_cast<char*>("ubstat"),
        static_cast<char*>("-p"),
        static_cast<char*>("123"),
        static_cast<char*>("-w"),
        static_cast<char*>("-s"),
        static_cast<char*>("::1"),
        static_cast<char*>("-d"),
        static_cast<char*>("::1"),
        static_cast<char*>("stat"),
        nullptr
    };
    int argc11 = sizeof(argv11) / sizeof(char*) - 1;
    ret = parser.Parse(argc11, argv11, args);
    EXPECT_EQ(ret, true);
}

TEST_F(CLIArgsParserTest, IsCommandValid)
{
    CLIArgsParser parser;
    std::string cmd;

    cmd = "stat";
    bool ret = parser.IsCommandValid(cmd);
    EXPECT_EQ(ret, true);

    cmd = "topo";
    ret = parser.IsCommandValid(cmd);
    EXPECT_EQ(ret, true);

    cmd = "invalid";
    ret = parser.IsCommandValid(cmd);
    EXPECT_EQ(ret, false);

    cmd = "";
    ret = parser.IsCommandValid(cmd);
    EXPECT_EQ(ret, false);
}

TEST_F(CLIArgsParserTest, GetCmd)
{
    CLIArgsParser parser;
    std::string cmd;

    cmd = "topo";
    CLICommand ret = parser.GetCmd(cmd);
    EXPECT_EQ(ret, CLICommand::TOPO);

    cmd = "stat";
    ret = parser.GetCmd(cmd);
    EXPECT_EQ(ret, CLICommand::STAT);

    cmd = "invalid";
    ret = parser.GetCmd(cmd);
    EXPECT_EQ(ret, CLICommand::INVALID);

    cmd = "";
    ret = parser.GetCmd(cmd);
    EXPECT_EQ(ret, CLICommand::INVALID);
}
}