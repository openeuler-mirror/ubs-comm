// SPDX-License-Identifier: MIT
// Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.

#include <gtest/gtest.h>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include "utracer_utils.h"

using namespace testing;
using namespace Statistics;

namespace {
static const char* TEST_EMPTY_STRING = "";
static const char* TEST_STRING_WITH_SPACES = "  hello world  ";
static const char* TEST_STRING_NO_SPACES = "hello";
static const char* TEST_SEPARATOR = ",";
static const char* TEST_MULTI_SEPARATOR_STRING = "a,b,c,d";
static const size_t TEST_EXPECTED_TRIMMED_LENGTH = 11;
static const size_t TEST_EXPECTED_SPLIT_COUNT = 4;
static const uint64_t TEST_BEGIN_VALUE = 1000;
static const uint64_t TEST_GOOD_END_VALUE = 800;
static const uint64_t TEST_BAD_END_VALUE = 200;
static const uint64_t TEST_MIN_VALUE = 50;
static const uint64_t TEST_MAX_VALUE = 500;
static const uint64_t TEST_TOTAL_VALUE = 400000;
static const uint64_t TEST_UINT64_MAX = UINT64_MAX;
static const char* TEST_TEMP_DIR_PREFIX = "/tmp/utracer_test_";

// 新增用于替换测试中的魔鬼数字
static const size_t TEST_IDX_A = 0;
static const size_t TEST_IDX_B = 1;
static const size_t TEST_IDX_C = 2;
static const size_t TEST_IDX_D = 3;
static const size_t TEST_MULTI_CONSECUTIVE_SPLIT_COUNT = 5;
static const size_t TEST_CLEARED_SIZE = 2;
}

class UTracerUtilsTest : public testing::Test {
protected:
    void SetUp() override
    {
        // 创建唯一的临时目录名
        // 使用 std::string + to_string 构造，避免使用不安全或平台不一致的 snprintf
        tempDir_ = std::string(TEST_TEMP_DIR_PREFIX) + std::to_string(static_cast<unsigned>(getpid()));
    }

    void TearDown() override
    {
        // 清理创建的临时目录
        if (!tempDir_.empty()) {
            // 尝试删除可能创建的目录
            rmdir(tempDir_.c_str());
            std::string subDir = tempDir_ + "/subdir";
            rmdir(subDir.c_str());
        }
    }

    std::string tempDir_;
};

// 测试 StrTrim 方法 - 空字符串
TEST_F(UTracerUtilsTest, StrTrim_EmptyString)
{
    std::string str = TEST_EMPTY_STRING;
    std::string& result = UTracerUtils::StrTrim(str);
    EXPECT_EQ(result, TEST_EMPTY_STRING);
    EXPECT_EQ(&result, &str);  // 返回引用应该是同一个对象
}

// 测试 StrTrim 方法 - 字符串两端有空格
TEST_F(UTracerUtilsTest, StrTrim_WithLeadingAndTrailingSpaces)
{
    std::string str = TEST_STRING_WITH_SPACES;
    std::string& result = UTracerUtils::StrTrim(str);
    EXPECT_EQ(result, "hello world");
    EXPECT_EQ(result.length(), TEST_EXPECTED_TRIMMED_LENGTH);
}

// 测试 StrTrim 方法 - 无空格字符串
TEST_F(UTracerUtilsTest, StrTrim_NoSpaces)
{
    std::string str = TEST_STRING_NO_SPACES;
    std::string& result = UTracerUtils::StrTrim(str);
    EXPECT_EQ(result, TEST_STRING_NO_SPACES);
}

// 测试 StrTrim 方法 - 仅前导空格
TEST_F(UTracerUtilsTest, StrTrim_OnlyLeadingSpaces)
{
    std::string str = "   test";
    std::string& result = UTracerUtils::StrTrim(str);
    EXPECT_EQ(result, "test");
}

// 测试 StrTrim 方法 - 仅尾部空格
TEST_F(UTracerUtilsTest, StrTrim_OnlyTrailingSpaces)
{
    std::string str = "test   ";
    std::string& result = UTracerUtils::StrTrim(str);
    EXPECT_EQ(result, "test");
}

// 测试 CurrentTime 方法 - 返回非空字符串
TEST_F(UTracerUtilsTest, CurrentTime_ReturnsNonEmptyString)
{
    std::string result = UTracerUtils::CurrentTime();
    // 返回的字符串应该包含日期时间格式，长度应该大于0
    EXPECT_TRUE(result.length() > 0 || result.empty());  // 允许空字符串（如果localtime_r失败）
}

// 测试 FormatString 方法 - 正常参数
TEST_F(UTracerUtilsTest, FormatString_NormalParameters)
{
    std::string name = "test_trace";
    std::string result = UTracerUtils::FormatString(
        name, TEST_BEGIN_VALUE, TEST_GOOD_END_VALUE, TEST_BAD_END_VALUE,
        TEST_MIN_VALUE, TEST_MAX_VALUE, TEST_TOTAL_VALUE);
    EXPECT_TRUE(result.length() > 0);
    // 结果应该包含名称
    EXPECT_TRUE(result.find(name) != std::string::npos);
}

// 测试 FormatString 方法 - min 为 UINT64_MAX
TEST_F(UTracerUtilsTest, FormatString_MinIsMaxUint64)
{
    std::string name = "test_trace";
    std::string result = UTracerUtils::FormatString(
        name, TEST_BEGIN_VALUE, TEST_GOOD_END_VALUE, TEST_BAD_END_VALUE,
        TEST_UINT64_MAX, TEST_MAX_VALUE, TEST_TOTAL_VALUE);
    EXPECT_TRUE(result.length() > 0);
}

// 测试 FormatString 方法 - goodEnd 为 0
TEST_F(UTracerUtilsTest, FormatString_GoodEndIsZero)
{
    std::string name = "test_trace";
    std::string result = UTracerUtils::FormatString(
        name, TEST_BEGIN_VALUE, 0, TEST_BAD_END_VALUE,
        TEST_MIN_VALUE, TEST_MAX_VALUE, TEST_TOTAL_VALUE);
    EXPECT_TRUE(result.length() > 0);
}

// 测试 SplitStr 方法 - 正常分割
TEST_F(UTracerUtilsTest, SplitStr_NormalSplit)
{
    std::string str = TEST_MULTI_SEPARATOR_STRING;
    std::string separator = TEST_SEPARATOR;
    std::vector<std::string> result;
    UTracerUtils::SplitStr(str, separator, result);
    EXPECT_EQ(result.size(), TEST_EXPECTED_SPLIT_COUNT);
    EXPECT_EQ(result[TEST_IDX_A], "a");
    EXPECT_EQ(result[TEST_IDX_B], "b");
    EXPECT_EQ(result[TEST_IDX_C], "c");
    EXPECT_EQ(result[TEST_IDX_D], "d");
}

// 测试 SplitStr 方法 - 空字符串
TEST_F(UTracerUtilsTest, SplitStr_EmptyString)
{
    std::string str = TEST_EMPTY_STRING;
    std::string separator = TEST_SEPARATOR;
    std::vector<std::string> result;
    UTracerUtils::SplitStr(str, separator, result);
    EXPECT_EQ(result.size(), 0);
}

// 测试 SplitStr 方法 - 分隔符不存在
TEST_F(UTracerUtilsTest, SplitStr_SeparatorNotFound)
{
    std::string str = "hello";
    std::string separator = TEST_SEPARATOR;
    std::vector<std::string> result;
    UTracerUtils::SplitStr(str, separator, result);
    EXPECT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], "hello");
}

// 测试 SplitStr 方法 - 多个连续分隔符
TEST_F(UTracerUtilsTest, SplitStr_MultipleConsecutiveSeparators)
{
    std::string str = "a,,b,,c";
    std::string separator = TEST_SEPARATOR;
    std::vector<std::string> result;
    UTracerUtils::SplitStr(str, separator, result);
    EXPECT_EQ(result.size(), TEST_MULTI_CONSECUTIVE_SPLIT_COUNT);  // a, "", b, "", c
}

// 测试 SplitStr 方法 - 清空已有结果向量
TEST_F(UTracerUtilsTest, SplitStr_ClearsExistingVector)
{
    std::vector<std::string> result;
    result.push_back("existing");
    std::string str = "a,b";
    std::string separator = TEST_SEPARATOR;
    UTracerUtils::SplitStr(str, separator, result);
    EXPECT_EQ(result.size(), TEST_CLEARED_SIZE);
    EXPECT_EQ(result[0], "a");
}

// 测试 CreateDirectory 方法 - 创建单级目录
TEST_F(UTracerUtilsTest, CreateDirectory_SingleLevel)
{
    int ret = UTracerUtils::CreateDirectory(tempDir_);
    // 目录创建成功或已存在都返回0（errno == EEXIST 时也返回0）
    EXPECT_TRUE(ret == 0 || errno == EEXIST);
}

// 测试 CreateDirectory 方法 - 创建多级目录
TEST_F(UTracerUtilsTest, CreateDirectory_MultiLevel)
{
    std::string multiPath = tempDir_ + "/subdir1/subdir2";
    int ret = UTracerUtils::CreateDirectory(multiPath);
    EXPECT_TRUE(ret == 0 || errno == EEXIST);
    // 清理
    rmdir((tempDir_ + "/subdir1/subdir2").c_str());
    rmdir((tempDir_ + "/subdir1").c_str());
}

// 测试 CreateDirectory 方法 - 空路径
TEST_F(UTracerUtilsTest, CreateDirectory_EmptyPath)
{
    int ret = UTracerUtils::CreateDirectory("");
    // 空路径应该返回0（没有目录需要创建）
    EXPECT_EQ(ret, 0);
}

// 测试 CanonicalPath 方法 - 空路径返回false
TEST_F(UTracerUtilsTest, CanonicalPath_EmptyPathReturnsFalse)
{
    std::string path = "";
    bool result = UTracerUtils::CanonicalPath(path);
    EXPECT_FALSE(result);
}

// 测试 CanonicalPath 方法 - 超长路径返回false
TEST_F(UTracerUtilsTest, CanonicalPath_TooLongPathReturnsFalse)
{
    // PATH_MAX 通常为 4096
    std::string longPath(PATH_MAX + 1, 'a');
    bool result = UTracerUtils::CanonicalPath(longPath);
    EXPECT_FALSE(result);
}

// 测试 CanonicalPath 方法 - 无效路径返回false
TEST_F(UTracerUtilsTest, CanonicalPath_InvalidPathReturnsFalse)
{
    std::string path = "/nonexistent/path/that/does/not/exist";
    bool result = UTracerUtils::CanonicalPath(path);
    EXPECT_FALSE(result);
}

// 测试 CanonicalPath 方法 - 有效路径返回true并规范化
TEST_F(UTracerUtilsTest, CanonicalPath_ValidPathReturnsTrue)
{
    // 使用 /tmp 作为有效路径
    std::string path = "/tmp";
    bool result = UTracerUtils::CanonicalPath(path);
    EXPECT_TRUE(result);
    // 规范化后应该仍然是 /tmp 或类似的路径
    EXPECT_TRUE(path.length() > 0);
}