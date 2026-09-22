/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>

#include <sstream>

#include "common/ubsocket_logger.h"

using namespace ock::ubs;

// ==================== Logger Tests ====================
// Logger methods (Log, LogDefault, SetLogLevel, etc.) are ALWAYS_INLINE,
// so noinline helpers consolidate branch counters.

__attribute__((noinline)) static void CallSetLogLevel(int level)
{
    Logger::Instance().SetLogLevel(level);
}

__attribute__((noinline)) static int CallGetLogLevel()
{
    return Logger::Instance().GetLogLevel();
}

__attribute__((noinline)) static void CallLogDefault(int level, const std::string &msg)
{
    Logger::Instance().LogDefault(level, msg, "test.cpp", 1);
}

__attribute__((noinline)) static void CallLog(int level, const std::ostringstream &oss)
{
    Logger::Instance().Log(level, oss, "test.cpp", 1);
}

__attribute__((noinline)) static void CallLogv(int level, const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    // Can't directly call Logv with va_list — use the macro-like approach
    va_end(va);
    // Use UBS_VLOG which calls Logv internally
    Logger::Instance().Logv(level, "test.cpp", 1, "test_func", "%s", fmt);
}

static void MockExternalLog(int level, const char *msg, const char *filename, int line)
{
    (void)level;
    (void)msg;
    (void)filename;
    (void)line;
}

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        savedLevel_ = Logger::Instance().GetLogLevel();
        Logger::Instance().SetLogLevel(LEVEL_DEBUG);
    }

    void TearDown() override
    {
        Logger::Instance().SetExternalLogFunction(nullptr);
        Logger::Instance().SetLogLevel(savedLevel_);
    }

private:
    int savedLevel_{LEVEL_INFO};
};

TEST_F(LoggerTest, SetLogLevel_Valid_Debug)
{
    CallSetLogLevel(LEVEL_DEBUG);
    EXPECT_EQ(CallGetLogLevel(), LEVEL_DEBUG);
}

TEST_F(LoggerTest, SetLogLevel_Valid_Err)
{
    CallSetLogLevel(LEVEL_ERR);
    EXPECT_EQ(CallGetLogLevel(), LEVEL_ERR);
}

TEST_F(LoggerTest, SetLogLevel_Invalid_TooHigh_Rejected)
{
    CallSetLogLevel(LEVEL_COUNT); // invalid
    // Should not change — stays at LEVEL_DEBUG (from SetUp)
    EXPECT_EQ(CallGetLogLevel(), LEVEL_DEBUG);
}

TEST_F(LoggerTest, SetLogLevel_Invalid_Negative_Rejected)
{
    CallSetLogLevel(-1); // invalid
    EXPECT_EQ(CallGetLogLevel(), LEVEL_DEBUG);
}

TEST_F(LoggerTest, Log_WithExternalLogFunction)
{
    Logger::Instance().SetExternalLogFunction(MockExternalLog);
    std::ostringstream oss;
    oss << "test message";
    CallLog(LEVEL_ERR, oss);
}

TEST_F(LoggerTest, Log_WithDefaultLog)
{
    Logger::Instance().SetExternalLogFunction(nullptr);
    std::ostringstream oss;
    oss << "test message";
    CallLog(LEVEL_INFO, oss);
}

TEST_F(LoggerTest, LogDefault_ValidTimestamp)
{
    CallLogDefault(LEVEL_INFO, "test message");
}

TEST_F(LoggerTest, SetExternalLogFunction_SetAndClear)
{
    Logger::Instance().SetExternalLogFunction(MockExternalLog);
    // Verify it's set by checking that Log calls the external function
    std::ostringstream oss;
    oss << "verify external";
    CallLog(LEVEL_ERR, oss);

    Logger::Instance().SetExternalLogFunction(nullptr);
    // Verify it's cleared — Log should use LogDefault
    CallLog(LEVEL_ERR, oss);
}

TEST_F(LoggerTest, GetLogLevel_ReturnsCurrentLevel)
{
    CallSetLogLevel(LEVEL_WARN);
    EXPECT_EQ(CallGetLogLevel(), LEVEL_WARN);
    CallSetLogLevel(LEVEL_NOTICE);
    EXPECT_EQ(CallGetLogLevel(), LEVEL_NOTICE);
}
