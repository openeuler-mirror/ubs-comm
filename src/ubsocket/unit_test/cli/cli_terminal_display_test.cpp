/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the unit test for cli terminal display, etc
 * Author:
 * Create: 2026-02-25
 * Note:
 * History: 2026-02-25
*/

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "cli_terminal_display.h"

namespace Statistics {
class CLITerminalDisplayTest : public testing::Test {
public:
    void SetUp() override {}

    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

TEST_F(CLITerminalDisplayTest, DisplayTopoInfo)
{
    TerminalDisplay display{};
    umq_route_t validRouteBuf{};
    umq_route_list_t testRouteList{};
    testRouteList.len = 1;
    testRouteList.buf[0] = validRouteBuf;

    const uint32_t validDataLen = sizeof(umq_route_list_t);
    const uint32_t invalidDataLen = validDataLen - 1;
    char dummyStr[INET6_ADDRSTRLEN] = "::1";

    MOCKER_CPP(&TerminalDisplay::PrintTitle).stubs();
    MOCKER_CPP(&TerminalDisplay::NewLine).stubs();

    EXPECT_NO_FATAL_FAILURE(display.DisplayTopoInfo(&testRouteList, validDataLen));
    testRouteList.len = 0;
    EXPECT_NO_FATAL_FAILURE(display.DisplayTopoInfo(&testRouteList, validDataLen));
    testRouteList.len = 1;
    EXPECT_NO_FATAL_FAILURE(display.DisplayTopoInfo(&testRouteList, validDataLen));
    EXPECT_NO_FATAL_FAILURE(display.DisplayTopoInfo(&testRouteList, validDataLen));
    EXPECT_NO_FATAL_FAILURE(display.DisplayTopoInfo(&testRouteList, validDataLen));
}

TEST_F(CLITerminalDisplayTest, DisplaySocketInfo)
{
    TerminalDisplay display;
    const uint32_t headerSize = sizeof(CLIDataHeader);
    const uint32_t socketDataSize = sizeof(CLISocketData);
    const uint32_t validSocketNum = 1;
    const uint32_t validDataLen = headerSize + validSocketNum * socketDataSize;

    uint8_t validDataBuf[validDataLen] = {0};
    uint8_t smallDataBuf[headerSize - 1] = {0};
    uint8_t mismatchDataBuf[validDataLen - 1] = {0};

    CLIDataHeader *validHeader = reinterpret_cast<CLIDataHeader *>(validDataBuf);
    validHeader->socketNum = validSocketNum;

    EXPECT_NO_FATAL_FAILURE(display.DisplaySocketInfo(smallDataBuf, sizeof(smallDataBuf)));

    EXPECT_NO_FATAL_FAILURE(display.DisplaySocketInfo(mismatchDataBuf, sizeof(mismatchDataBuf)));

    EXPECT_NO_FATAL_FAILURE(display.DisplaySocketInfo(validDataBuf, validDataLen));
}

constexpr uint64_t testBytes1k = 1024;

TEST_F(CLITerminalDisplayTest, BytesToHumanReadable)
{
    TerminalDisplay display;
    std::string retStr;

    retStr = display.BytesToHumanReadable(0);
    EXPECT_EQ(retStr, "0.00B");

    retStr = display.BytesToHumanReadable(testBytes1k); // 1k
    EXPECT_EQ(retStr, "1.00K");
}
}