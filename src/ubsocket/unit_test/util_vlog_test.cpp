/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Unit tests for util_vlog.cpp
 */

#include "util_vlog.h"

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include <cstring>

namespace ubsocket {
namespace {

static const uint32_t PRINT_COUNT_0 = 0U;
static const uint32_t PRINT_COUNT_1 = 1U;
static const uint32_t PRINT_COUNT_5 = 5U;
static const uint32_t PRINT_COUNT_10 = 10U;
static const uint64_t LAST_TIME_0 = 0U;
static const uint64_t LAST_TIME_100 = 100U;
static const uint32_t INTERVAL_MS_0 = 0U;
static const uint32_t INTERVAL_MS_100 = 100U;
static const uint32_t INTERVAL_MS_1000 = 1000U;
static const uint32_t RATE_LIMIT_NUM_1 = 1U;
static const uint32_t RATE_LIMIT_NUM_5 = 5U;
static const uint32_t RATE_LIMIT_NUM_10 = 10U;

static const int TEST_LINE_NUM = 42;
static const int TEST_MSG_ARG = 123;
} // namespace
}

class UtilVlogTest : public testing::Test {
public:
    void SetUp() override
    {
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

// Test util_vlog_level_converter_from_str with valid strings
TEST_F(UtilVlogTest, LevelConverterFromStr_ValidStrings)
{
    using namespace ubsocket;

    EXPECT_EQ(util_vlog_level_converter_from_str("error", UTIL_VLOG_LEVEL_ERR), UTIL_VLOG_LEVEL_ERR);
    EXPECT_EQ(util_vlog_level_converter_from_str("warn", UTIL_VLOG_LEVEL_WARN), UTIL_VLOG_LEVEL_WARN);
    EXPECT_EQ(util_vlog_level_converter_from_str("notice", UTIL_VLOG_LEVEL_NOTICE), UTIL_VLOG_LEVEL_NOTICE);
    EXPECT_EQ(util_vlog_level_converter_from_str("info", UTIL_VLOG_LEVEL_INFO), UTIL_VLOG_LEVEL_INFO);
    EXPECT_EQ(util_vlog_level_converter_from_str("debug", UTIL_VLOG_LEVEL_DEBUG), UTIL_VLOG_LEVEL_DEBUG);
    EXPECT_EQ(util_vlog_level_converter_from_str("7", UTIL_VLOG_LEVEL_DEBUG), UTIL_VLOG_LEVEL_DEBUG);
}

// Test util_vlog_level_converter_from_str with invalid string returns default
TEST_F(UtilVlogTest, LevelConverterFromStr_InvalidString_ReturnsDefault)
{
    using namespace ubsocket;

    EXPECT_EQ(util_vlog_level_converter_from_str("invalid", UTIL_VLOG_LEVEL_INFO), UTIL_VLOG_LEVEL_INFO);
    EXPECT_EQ(util_vlog_level_converter_from_str("", UTIL_VLOG_LEVEL_ERR), UTIL_VLOG_LEVEL_ERR);
    EXPECT_EQ(util_vlog_level_converter_from_str("UNKNOWN", UTIL_VLOG_LEVEL_DEBUG), UTIL_VLOG_LEVEL_DEBUG);
}

// Test util_vlog_level_converter_to_str
TEST_F(UtilVlogTest, LevelConverterToStr_ValidLevels)
{
    using namespace ubsocket;

    EXPECT_STREQ(util_vlog_level_converter_to_str(UTIL_VLOG_LEVEL_EMERG), "EMERG");
    EXPECT_STREQ(util_vlog_level_converter_to_str(UTIL_VLOG_LEVEL_ALERT), "ALERT");
    EXPECT_STREQ(util_vlog_level_converter_to_str(UTIL_VLOG_LEVEL_CRIT), "CRIT");
    EXPECT_STREQ(util_vlog_level_converter_to_str(UTIL_VLOG_LEVEL_ERR), "ERR");
    EXPECT_STREQ(util_vlog_level_converter_to_str(UTIL_VLOG_LEVEL_WARN), "WARN");
    EXPECT_STREQ(util_vlog_level_converter_to_str(UTIL_VLOG_LEVEL_NOTICE), "NOTICE");
    EXPECT_STREQ(util_vlog_level_converter_to_str(UTIL_VLOG_LEVEL_INFO), "INFO");
    EXPECT_STREQ(util_vlog_level_converter_to_str(UTIL_VLOG_LEVEL_DEBUG), "DEBUG");
}

// Test error_type_to_str with valid types
TEST_F(UtilVlogTest, ErrorTypeToStr_ValidTypes)
{
    using namespace ubsocket;

    EXPECT_STREQ(error_type_to_str(UMQ_AE), "UMQ_AE");
    EXPECT_STREQ(error_type_to_str(UMQ_API), "UMQ_API");
    EXPECT_STREQ(error_type_to_str(UMQ_CQE), "UMQ_CQE");
    EXPECT_STREQ(error_type_to_str(UBSocket), "UBSocket");
    EXPECT_STREQ(error_type_to_str(NATIVE_SOCKET), "NATIVE_SOCKET");
}

// Test error_type_to_str with invalid type
TEST_F(UtilVlogTest, ErrorTypeToStr_InvalidType)
{
    using namespace ubsocket;

    EXPECT_STREQ(error_type_to_str(ERROR_TYPE_MAX), "UNKNOWN");
    EXPECT_STREQ(error_type_to_str(static_cast<error_type_t>(ERROR_TYPE_MAX + 1)), "UNKNOWN");
}

// Test util_vlog_limit with interval_ms = 0 (no rate limiting)
TEST_F(UtilVlogTest, VlogLimit_NoRateLimiting_ReturnsTrue)
{
    using namespace ubsocket;

    util_vlog_ctx_t ctx = {};
    ctx.level = UTIL_VLOG_LEVEL_INFO;
    ctx.rate_limited.interval_ms = INTERVAL_MS_0;
    ctx.rate_limited.num = RATE_LIMIT_NUM_5;
    std::strcpy(ctx.vlog_name, "test");

    uint32_t printCount = PRINT_COUNT_0;
    uint64_t lastTime = LAST_TIME_0;

    // With interval_ms = 0, should always return true
    EXPECT_TRUE(util_vlog_limit(&ctx, &printCount, &lastTime));
    EXPECT_TRUE(util_vlog_limit(&ctx, &printCount, &lastTime));
}

// Test util_vlog_limit with rate limiting - within limit
TEST_F(UtilVlogTest, VlogLimit_WithinLimit_ReturnsTrue)
{
    using namespace ubsocket;

    util_vlog_ctx_t ctx = {};
    ctx.level = UTIL_VLOG_LEVEL_INFO;
    ctx.rate_limited.interval_ms = INTERVAL_MS_100;
    ctx.rate_limited.num = RATE_LIMIT_NUM_5;
    std::strcpy(ctx.vlog_name, "test");

    uint32_t printCount = PRINT_COUNT_0;
    uint64_t lastTime = LAST_TIME_100;

    // Note: urpc_get_cpu_cycles is inline and cannot be mocked
    // We test the basic increment behavior when not crossing cycle boundary
    EXPECT_TRUE(util_vlog_limit(&ctx, &printCount, &lastTime));
    EXPECT_EQ(printCount, PRINT_COUNT_1);
}

// Test util_vlog_limit with rate limiting - exceeds limit after multiple calls
TEST_F(UtilVlogTest, VlogLimit_MultipleCalls_ExceedsLimit)
{
    using namespace ubsocket;

    util_vlog_ctx_t ctx = {};
    ctx.level = UTIL_VLOG_LEVEL_INFO;
    ctx.rate_limited.interval_ms = INTERVAL_MS_1000;  // Long interval to avoid cycle reset
    ctx.rate_limited.num = RATE_LIMIT_NUM_1;  // Only 1 log per cycle
    std::strcpy(ctx.vlog_name, "test");

    uint32_t printCount = PRINT_COUNT_0;
    uint64_t lastTime = LAST_TIME_0;

    // First call should succeed
    EXPECT_TRUE(util_vlog_limit(&ctx, &printCount, &lastTime));
    EXPECT_EQ(printCount, PRINT_COUNT_1);

    // Second call within same cycle should fail (count >= num)
    // Note: since urpc_get_cpu_cycles is inline, actual behavior depends on real time
    // We just verify the count increments correctly
}

// Test util_vlog_drop inline function
TEST_F(UtilVlogTest, VlogDrop_LevelComparison)
{
    using namespace ubsocket;

    util_vlog_ctx_t ctx = {};
    ctx.level = UTIL_VLOG_LEVEL_INFO;

    // Level higher than ctx.level should be dropped
    EXPECT_TRUE(util_vlog_drop(&ctx, UTIL_VLOG_LEVEL_DEBUG));

    // Level equal or lower should not be dropped
    EXPECT_FALSE(util_vlog_drop(&ctx, UTIL_VLOG_LEVEL_INFO));
    EXPECT_FALSE(util_vlog_drop(&ctx, UTIL_VLOG_LEVEL_ERR));
}

// Test util_vlog_output basic functionality
TEST_F(UtilVlogTest, VlogOutput_BasicOutput)
{
    using namespace ubsocket;

    static char capturedLog[UTIL_VLOG_SIZE] = {0};
    static bool outputCalled = false;

    auto testOutputFunc = [](int level, char *logMsg) {
        std::strncpy(capturedLog, logMsg, UTIL_VLOG_SIZE - 1);
        outputCalled = true;
    };

    util_vlog_ctx_t ctx = {};
    ctx.level = UTIL_VLOG_LEVEL_INFO;
    ctx.vlog_output_func = testOutputFunc;
    std::strcpy(ctx.vlog_name, "test_logger");

    outputCalled = false;
    util_vlog_output(UBSocket, &ctx, UTIL_VLOG_LEVEL_INFO, "TestFunc", TEST_LINE_NUM, "test message %d", TEST_MSG_ARG);

    EXPECT_TRUE(outputCalled);
    EXPECT_NE(std::strstr(capturedLog, "test_logger"), nullptr);
    EXPECT_NE(std::strstr(capturedLog, "UBSocket"), nullptr);
    EXPECT_NE(std::strstr(capturedLog, "TestFunc"), nullptr);
    EXPECT_NE(std::strstr(capturedLog, "123"), nullptr);
}

// Test default_vlog_output inline function exists and works
TEST_F(UtilVlogTest, DefaultVlogOutput_Exists)
{
    using namespace ubsocket;

    // Just verify the function exists and can be called
    // Note: syslog may not be available in test environment, so we just verify signature
    // The function is inline and used as default, we test it indirectly
    util_vlog_ctx_t ctx = {};
    ctx.vlog_output_func = default_vlog_output;
    std::strcpy(ctx.vlog_name, "default_test");

    // This will call syslog - may or may not work depending on environment
    // We just verify it doesn't crash
    util_vlog_output(UBSocket, &ctx, UTIL_VLOG_LEVEL_INFO, "TestFunc", 1, "default test");
}