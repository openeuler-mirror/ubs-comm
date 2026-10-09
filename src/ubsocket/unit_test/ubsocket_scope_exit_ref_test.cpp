/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>

#include "common/ubsocket_ref.h"
#include "common/ubsocket_scope_exit.h"

using namespace ock::ubs;

// Helpers for RefStaticCast / RefDynamicCast tests
class RefTestBase : public Referable {
public:
    virtual ~RefTestBase() = default;
};

class RefTestDerived : public RefTestBase {
public:
    int data = 42;
};

class RefTestObj : public Referable {
public:
    explicit RefTestObj(int v) : value(v) {}
    int value;
};

// ==================== ScopeExit ====================

class ScopeExitTest : public ::testing::Test {
};

TEST_F(ScopeExitTest, FiresOnDestruction)
{
    int count = 0;
    {
        auto guard = MakeScopeExit([&count] { count++; });
        EXPECT_TRUE(guard.Active());
    }
    EXPECT_EQ(count, 1);
}

TEST_F(ScopeExitTest, DeactivatePreventsFire)
{
    int count = 0;
    {
        auto guard = MakeScopeExit([&count] { count++; });
        guard.Deactivate();
        EXPECT_FALSE(guard.Active());
    }
    EXPECT_EQ(count, 0);
}

TEST_F(ScopeExitTest, DefaultActiveIsTrue)
{
    int count = 0;
    {
        auto guard = MakeScopeExit([&count] { count++; });
        EXPECT_TRUE(guard.Active());
    }
    EXPECT_EQ(count, 1);
}

TEST_F(ScopeExitTest, ManualActiveFalse)
{
    int count = 0;
    {
        auto fn = [&count] {
            count++;
        };
        ScopeExit<decltype(fn)> guard(std::move(fn), false);
        EXPECT_FALSE(guard.Active());
    }
    EXPECT_EQ(count, 0);
}

TEST_F(ScopeExitTest, MoveDeactivatesSource)
{
    int count = 0;
    auto guard1 = MakeScopeExit([&count] { count++; });
    auto guard2 = std::move(guard1);
    EXPECT_FALSE(guard1.Active());
    EXPECT_TRUE(guard2.Active());
    EXPECT_EQ(count, 0);
}

TEST_F(ScopeExitTest, MultipleGuardsEachFireOnce)
{
    int a = 0;
    int b = 0;
    {
        auto g1 = MakeScopeExit([&a] { a++; });
        auto g2 = MakeScopeExit([&b] { b++; });
        EXPECT_EQ(a, 0);
        EXPECT_EQ(b, 0);
    }
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);
}

TEST_F(ScopeExitTest, DeactivateAfterMove)
{
    int count = 0;
    auto guard1 = MakeScopeExit([&count] { count++; });
    auto guard2 = std::move(guard1);
    guard2.Deactivate();
    EXPECT_EQ(count, 0);
}

// ==================== Ref ====================

TEST_F(ScopeExitTest, Ref_DefaultConstructIsNull)
{
    Ref<RefTestObj> r;
    EXPECT_EQ(r.Get(), nullptr);
}

TEST_F(ScopeExitTest, Ref_ConstructFromRawPointer)
{
    auto *obj = new RefTestObj(42);
    {
        Ref<RefTestObj> r(obj);
        EXPECT_EQ(r.Get(), obj);
        EXPECT_EQ(r->value, 42);
    }
    // r destroyed, obj deleted via DecreaseRef
}

TEST_F(ScopeExitTest, Ref_CopyConstructSharesRef)
{
    auto *obj = new RefTestObj(10);
    Ref<RefTestObj> r1(obj);
    {
        Ref<RefTestObj> r2(r1);
        EXPECT_EQ(r1.Get(), r2.Get());
        EXPECT_EQ(r2->value, 10);
    }
    // r2 destroyed, obj stays alive (r1 still holds ref)
    EXPECT_EQ(r1->value, 10);
}

TEST_F(ScopeExitTest, Ref_MoveConstructTransfers)
{
    auto *obj = new RefTestObj(20);
    Ref<RefTestObj> r1(obj);
    Ref<RefTestObj> r2(std::move(r1));
    EXPECT_EQ(r1.Get(), nullptr);
    EXPECT_EQ(r2.Get(), obj);
}

TEST_F(ScopeExitTest, Ref_CopyAssign)
{
    auto *obj1 = new RefTestObj(1);
    auto *obj2 = new RefTestObj(2);
    Ref<RefTestObj> r1(obj1);
    Ref<RefTestObj> r2(obj2);
    r1 = r2;
    EXPECT_EQ(r1.Get(), obj2);
    EXPECT_EQ(r2.Get(), obj2);
}

TEST_F(ScopeExitTest, Ref_MoveAssign)
{
    auto *obj1 = new RefTestObj(1);
    auto *obj2 = new RefTestObj(2);
    Ref<RefTestObj> r1(obj1);
    Ref<RefTestObj> r2(obj2);
    r1 = std::move(r2);
    EXPECT_EQ(r1.Get(), obj2);
    EXPECT_EQ(r2.Get(), nullptr);
}

TEST_F(ScopeExitTest, Ref_AssignRawPointer)
{
    auto *obj1 = new RefTestObj(1);
    auto *obj2 = new RefTestObj(2);
    Ref<RefTestObj> r(obj1);
    r = obj2;
    EXPECT_EQ(r.Get(), obj2);
}

TEST_F(ScopeExitTest, Ref_SetSamePointerNoOp)
{
    auto *obj = new RefTestObj(5);
    Ref<RefTestObj> r(obj);
    r.Set(obj);
    EXPECT_EQ(r.Get(), obj);
}

TEST_F(ScopeExitTest, Ref_SetNullptrReleases)
{
    auto *obj = new RefTestObj(99);
    Ref<RefTestObj> r(obj);
    r.Set(nullptr);
    EXPECT_EQ(r.Get(), nullptr);
}

TEST_F(ScopeExitTest, Ref_OperatorEqual)
{
    auto *obj1 = new RefTestObj(1);
    auto *obj2 = new RefTestObj(2);
    Ref<RefTestObj> r1(obj1);
    Ref<RefTestObj> r2(obj2);
    Ref<RefTestObj> r3(obj1);

    EXPECT_TRUE(r1 == r3);
    EXPECT_FALSE(r1 == r2);
    EXPECT_TRUE(r1 != r2);
    EXPECT_FALSE(r1 != r3);
}

TEST_F(ScopeExitTest, Ref_CompareWithRawPointer)
{
    auto *obj = new RefTestObj(7);
    Ref<RefTestObj> r(obj);

    EXPECT_TRUE(r == obj);
    EXPECT_FALSE(r != obj);
    EXPECT_FALSE(r == static_cast<RefTestObj *>(nullptr));
    EXPECT_TRUE(r != static_cast<RefTestObj *>(nullptr));
}

TEST_F(ScopeExitTest, Ref_OperatorArrow)
{
    auto *obj = new RefTestObj(33);
    Ref<RefTestObj> r(obj);
    EXPECT_EQ(r->value, 33);
}

TEST_F(ScopeExitTest, MakeRef_CreatesObject)
{
    auto r = MakeRef<RefTestObj>(55);
    EXPECT_NE(r.Get(), nullptr);
    EXPECT_EQ(r->value, 55);
}

TEST_F(ScopeExitTest, RefStaticCast_Works)
{
    auto derived = MakeRef<RefTestDerived>();
    Ref<RefTestBase> base = RefStaticCast<RefTestBase>(derived);
    EXPECT_EQ(base.Get(), derived.Get());
}

TEST_F(ScopeExitTest, RefDynamicCast_Works)
{
    auto derived = MakeRef<RefTestDerived>();
    Ref<RefTestBase> base = RefStaticCast<RefTestBase>(derived);
    Ref<RefTestDerived> back = RefDynamicCast<RefTestDerived>(base);
    EXPECT_NE(back.Get(), nullptr);
    EXPECT_EQ(back.Get(), derived.Get());
}

TEST_F(ScopeExitTest, Referable_RefCounting)
{
    auto *obj = new RefTestObj(0);
    obj->IncreaseRef();
    obj->IncreaseRef();
    obj->DecreaseRef();
    obj->DecreaseRef();
    // Final DecreaseRef (to 0) would delete; we avoid double-free
}
