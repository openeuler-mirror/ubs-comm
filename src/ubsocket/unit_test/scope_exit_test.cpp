// SPDX-License-Identifier: MIT
// Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.

#include <gtest/gtest.h>
#include "scope_exit.h"

using namespace testing;
using namespace ubsocket;

namespace {
static const int TEST_INITIAL_VALUE = 0;
static const int TEST_EXPECTED_VALUE = 42;
static const int TEST_DOUBLE_MULTIPLIER = 2;
}

class ScopeExitTest : public testing::Test {
protected:
    // No special setup/teardown required
};

// 测试 ScopeExit 构造函数和析构函数基本功能 - active=true 时执行回调
TEST_F(ScopeExitTest, ConstructAndDestruct_ActiveTrue)
{
    int value = TEST_INITIAL_VALUE;
    {
        auto scopeExit = MakeScopeExit([&value]() {
            value = TEST_EXPECTED_VALUE;
        }, true);
        EXPECT_TRUE(scopeExit.Active());
    }
    // 作用域退出时回调应该被执行
    EXPECT_EQ(value, TEST_EXPECTED_VALUE);
}

// 测试 ScopeExit 构造函数和析构函数 - active=false 时不执行回调
TEST_F(ScopeExitTest, ConstructAndDestruct_ActiveFalse)
{
    int value = TEST_INITIAL_VALUE;
    {
        auto scopeExit = MakeScopeExit([&value]() {
            value = TEST_EXPECTED_VALUE;
        }, false);
        EXPECT_FALSE(scopeExit.Active());
    }
    // 作用域退出时回调不应该被执行
    EXPECT_EQ(value, TEST_INITIAL_VALUE);
}

// 测试 Deactivate 方法 - 构造后禁用，析构时不执行回调
TEST_F(ScopeExitTest, Deactivate_PreventsCallback)
{
    int value = TEST_INITIAL_VALUE;
    {
        auto scopeExit = MakeScopeExit([&value]() {
            value = TEST_EXPECTED_VALUE;
        }, true);
        EXPECT_TRUE(scopeExit.Active());
        scopeExit.Deactivate();
        EXPECT_FALSE(scopeExit.Active());
    }
    // Deactivate 后析构时不应该执行回调
    EXPECT_EQ(value, TEST_INITIAL_VALUE);
}

// 测试移动构造函数 - 原对象被移动后应该不再活跃
TEST_F(ScopeExitTest, MoveConstructor_TransfersOwnership)
{
    int value = TEST_INITIAL_VALUE;
    {
        auto scopeExit1 = MakeScopeExit([&value]() {
            value = TEST_EXPECTED_VALUE;
        }, true);
        EXPECT_TRUE(scopeExit1.Active());

        // 移动构造
        auto scopeExit2 = std::move(scopeExit1);
        EXPECT_TRUE(scopeExit2.Active());
    }
    // 移动后的对象应该执行回调
    EXPECT_EQ(value, TEST_EXPECTED_VALUE);
}

// 测试 MakeScopeExit 工厂函数 - 默认 active=true
TEST_F(ScopeExitTest, MakeScopeExit_DefaultActive)
{
    int value = TEST_INITIAL_VALUE;
    {
        auto scopeExit = MakeScopeExit([&value]() {
            value = TEST_EXPECTED_VALUE;
        });
        EXPECT_TRUE(scopeExit.Active());
    }
    // 默认 active=true，析构时应该执行回调
    EXPECT_EQ(value, TEST_EXPECTED_VALUE);
}

// 测试 Active 方法返回值
TEST_F(ScopeExitTest, Active_ReturnsCorrectValue)
{
    auto scopeExitActive = MakeScopeExit([]() {}, true);
    EXPECT_TRUE(scopeExitActive.Active());

    auto scopeExitInactive = MakeScopeExit([]() {}, false);
    EXPECT_FALSE(scopeExitInactive.Active());
}

// 测试复杂回调 - 修改多个值
TEST_F(ScopeExitTest, ComplexCallback_ModifiesMultipleValues)
{
    int a = TEST_INITIAL_VALUE;
    int b = TEST_INITIAL_VALUE;
    {
        auto scopeExit = MakeScopeExit([&a, &b]() {
            a = TEST_EXPECTED_VALUE;
            b = TEST_EXPECTED_VALUE * TEST_DOUBLE_MULTIPLIER;
        }, true);
    }
    EXPECT_EQ(a, TEST_EXPECTED_VALUE);
    EXPECT_EQ(b, TEST_EXPECTED_VALUE * TEST_DOUBLE_MULTIPLIER);
}