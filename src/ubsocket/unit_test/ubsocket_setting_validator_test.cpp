/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>

#include "common/ubsocket_setting_validator.h"

using namespace ock::ubs;

// ==================== Int64Rule ====================

class SettingValidatorInt64Test : public ::testing::Test {
protected:
    void SetUp() override
    {
        Validator::Instance().AddNumRule(Int64Rule("port", true, 0, 65535));
        Validator::Instance().AddNumRule(Int64Rule("threads", false, 1, 1024));
        Validator::Instance().AddNumRule(Int64Rule("negative_min", false, -100, 100));
    }
};

TEST_F(SettingValidatorInt64Test, Required_WhenRuleRequiresIt)
{
    EXPECT_TRUE(Validator::Instance().Required("port"));
    EXPECT_FALSE(Validator::Instance().Required("threads"));
    EXPECT_FALSE(Validator::Instance().Required("nonexistent_key"));
}

TEST_F(SettingValidatorInt64Test, Validate_ValueInRange)
{
    EXPECT_TRUE(Validator::Instance().Validate("port", int64_t{8080}));
    EXPECT_TRUE(Validator::Instance().Validate("port", int64_t{0}));
    EXPECT_TRUE(Validator::Instance().Validate("port", int64_t{65535}));
    EXPECT_TRUE(Validator::Instance().Validate("threads", int64_t{1}));
    EXPECT_TRUE(Validator::Instance().Validate("threads", int64_t{1024}));
    EXPECT_TRUE(Validator::Instance().Validate("negative_min", int64_t{-50}));
}

TEST_F(SettingValidatorInt64Test, Validate_ValueOutOfRange)
{
    EXPECT_FALSE(Validator::Instance().Validate("port", int64_t{-1}));
    EXPECT_FALSE(Validator::Instance().Validate("port", int64_t{65536}));
    EXPECT_FALSE(Validator::Instance().Validate("threads", int64_t{0}));
    EXPECT_FALSE(Validator::Instance().Validate("threads", int64_t{1025}));
    EXPECT_FALSE(Validator::Instance().Validate("negative_min", int64_t{-101}));
    EXPECT_FALSE(Validator::Instance().Validate("negative_min", int64_t{101}));
}

TEST_F(SettingValidatorInt64Test, Validate_NonexistentKey)
{
    EXPECT_FALSE(Validator::Instance().Validate("nonexistent_key", int64_t{42}));
}

TEST_F(SettingValidatorInt64Test, LastErrMsg_UpdatedOnFailure)
{
    Validator::Instance().Validate("port", int64_t{-1});
    const std::string &msg = Validator::Instance().LastErrMsg();
    EXPECT_FALSE(msg.empty());
}

TEST_F(SettingValidatorInt64Test, Validate_WithCustomKey4log)
{
    EXPECT_TRUE(Validator::Instance().Validate("port", int64_t{8080}, "listening_port"));
    EXPECT_FALSE(Validator::Instance().Validate("port", int64_t{-1}, "listening_port"));
}

// ==================== FloatRule ====================

class SettingValidatorFloatTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        Validator::Instance().AddFloatRule(FloatRule("latency", false, 0.0f, 1.0f));
        Validator::Instance().AddFloatRule(FloatRule("epsilon", false, -0.5f, 0.5f));
    }
};

TEST_F(SettingValidatorFloatTest, Validate_ValueInRange)
{
    EXPECT_TRUE(Validator::Instance().Validate("latency", 0.5f));
    EXPECT_TRUE(Validator::Instance().Validate("latency", 0.0f));
    EXPECT_TRUE(Validator::Instance().Validate("latency", 1.0f));
    EXPECT_TRUE(Validator::Instance().Validate("epsilon", -0.3f));
}

TEST_F(SettingValidatorFloatTest, Validate_ValueOutOfRange)
{
    EXPECT_FALSE(Validator::Instance().Validate("latency", -0.01f));
    EXPECT_FALSE(Validator::Instance().Validate("latency", 1.01f));
    EXPECT_FALSE(Validator::Instance().Validate("epsilon", -0.6f));
    EXPECT_FALSE(Validator::Instance().Validate("epsilon", 0.6f));
}

// ==================== StrEnumRule ====================

class SettingValidatorStrEnumTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        Validator::Instance().AddStrEnumRule(StrEnumRule("transport", true, "tcp|rdma|shm"));
        Validator::Instance().AddStrEnumRule(StrEnumRule("loglevel", false, "debug|info|warn|error"));
    }
};

TEST_F(SettingValidatorStrEnumTest, ValidateStrEnum_ValidSingleValue)
{
    EXPECT_TRUE(Validator::Instance().ValidateStrEnum("transport", "tcp"));
    EXPECT_TRUE(Validator::Instance().ValidateStrEnum("transport", "rdma"));
    EXPECT_TRUE(Validator::Instance().ValidateStrEnum("transport", "shm"));
    EXPECT_TRUE(Validator::Instance().ValidateStrEnum("loglevel", "debug"));
    EXPECT_TRUE(Validator::Instance().ValidateStrEnum("loglevel", "error"));
}

TEST_F(SettingValidatorStrEnumTest, ValidateStrEnum_InvalidValue)
{
    EXPECT_FALSE(Validator::Instance().ValidateStrEnum("transport", "udp"));
    EXPECT_FALSE(Validator::Instance().ValidateStrEnum("transport", ""));
    EXPECT_FALSE(Validator::Instance().ValidateStrEnum("loglevel", "trace"));
}

TEST_F(SettingValidatorStrEnumTest, ValidateStrEnum_MultipleValues)
{
    EXPECT_TRUE(Validator::Instance().ValidateStrEnum("transport", "tcp|rdma"));
    EXPECT_TRUE(Validator::Instance().ValidateStrEnum("transport", "tcp|shm|rdma"));
    EXPECT_TRUE(Validator::Instance().ValidateStrEnum("loglevel", "debug|info"));
}

TEST_F(SettingValidatorStrEnumTest, ValidateStrEnum_MultipleValuesOneInvalid)
{
    EXPECT_FALSE(Validator::Instance().ValidateStrEnum("transport", "tcp|udp"));
    EXPECT_FALSE(Validator::Instance().ValidateStrEnum("loglevel", "debug|trace"));
}

TEST_F(SettingValidatorStrEnumTest, MakeSortedAllEnum_SkipsEmpty)
{
    StrEnumRule rule("test_rule", false, "a|  |b");
    EXPECT_TRUE(rule.Validate("a"));
    EXPECT_TRUE(rule.Validate("b"));
}

TEST_F(SettingValidatorStrEnumTest, StrEnumRequiredFlag)
{
    EXPECT_TRUE(Validator::Instance().Required("transport"));
    EXPECT_FALSE(Validator::Instance().Required("loglevel"));
}

// ==================== StrNotEmptyRule ====================

class SettingValidatorStrNotEmptyTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        Validator::Instance().AddStrNotEmtpyRule(StrNotEmptyRule("hostname", true));
        Validator::Instance().AddStrNotEmtpyRule(StrNotEmptyRule("short_key", false, 10));
    }
};

TEST_F(SettingValidatorStrNotEmptyTest, ValidateStrEmpty_Valid)
{
    EXPECT_TRUE(Validator::Instance().ValidateStrEmpty("hostname", "server01"));
    EXPECT_TRUE(Validator::Instance().ValidateStrEmpty("hostname", "x"));
    EXPECT_TRUE(Validator::Instance().ValidateStrEmpty("short_key", "abc"));
}

TEST_F(SettingValidatorStrNotEmptyTest, ValidateStrEmpty_EmptyString)
{
    EXPECT_FALSE(Validator::Instance().ValidateStrEmpty("hostname", ""));
}

TEST_F(SettingValidatorStrNotEmptyTest, ValidateStrEmpty_ExceedsMaxLen)
{
    EXPECT_FALSE(Validator::Instance().ValidateStrEmpty("short_key", "12345678901"));
}

TEST_F(SettingValidatorStrNotEmptyTest, ValidateStrEmpty_NonexistentKey)
{
    EXPECT_FALSE(Validator::Instance().ValidateStrEmpty("nonexistent_key", "value"));
}

TEST_F(SettingValidatorStrNotEmptyTest, StrNotEmptyRequiredFlag)
{
    EXPECT_TRUE(Validator::Instance().Required("hostname"));
    EXPECT_FALSE(Validator::Instance().Required("short_key"));
}

// ==================== Validator Dump ====================

class SettingValidatorDumpTest : public ::testing::Test {
};

TEST_F(SettingValidatorDumpTest, DumpString_NotEmpty)
{
    std::string dump = Validator::Instance().DumpString();
    EXPECT_FALSE(dump.empty());
    EXPECT_NE(dump.find("int rules:"), std::string::npos);
    EXPECT_NE(dump.find("float rules:"), std::string::npos);
    EXPECT_NE(dump.find("str enum rules:"), std::string::npos);
    EXPECT_NE(dump.find("str not empty rules:"), std::string::npos);
}
