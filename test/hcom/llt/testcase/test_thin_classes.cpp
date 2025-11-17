/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <thread>
#include <unordered_set>

#include "net_obj_pool.h"
#include "ut_helper.h"
#include "test_thin_classes.h"

using namespace ock::hcom;
TestCaseThinClasses::TestCaseThinClasses() {}

void TestCaseThinClasses::SetUp() {}

void TestCaseThinClasses::TearDown() {}

TEST_F(TestCaseThinClasses, NetUId)
{
    int count = 1000;
    std::unordered_set<uint64_t> set;
    for (int i = 0; i < count; ++i) {
        auto id = NetUuid::GenerateUuid();
        EXPECT_EQ(set.count(id), 0);
        set.insert(id);
    }
    set.clear();

    const std::string ip = "10.10.1.14";
    std::mutex locker;
    std::vector<std::thread> ths;
    for (int i = 0; i < count; ++i) {
        std::thread th([&]() {
            auto id = NetUuid::GenerateUuid(ip);
            locker.lock();
            EXPECT_EQ(set.count(id), 0);
            set.insert(id);
            locker.unlock();
        });
        ths.push_back(std::move(th));
    }
    for (int i = 0; i < count; ++i) {
        ths[i].join();
    }
}

TEST_F(TestCaseThinClasses, NetRef)
{
    OBJ_LIFE_CYCLE olc(NONE);
    OBJ_LIFE_CYCLE olc1(NONE);
    OBJ_LIFE_CYCLE olc2(NONE);
    auto obj = new NoisyObj(olc);
    auto obj1 = new NoisyObj(olc1);
    EXPECT_EQ(olc, INIT);
    EXPECT_EQ(obj->GetRef(), 0);
    NetRef<NoisyObj> ref(obj);
    EXPECT_EQ(obj->GetRef(), 1);
    ref.Set(obj1);
    EXPECT_EQ(obj1->GetRef(), 1);
    EXPECT_EQ(olc, DEINIT);
    {
        auto obj2 = new NoisyObj(olc2);
        EXPECT_EQ(obj2->GetRef(), 0);
        NetRef<NoisyObj> ref2(obj2);
        EXPECT_EQ(obj2->GetRef(), 1);
    }
    EXPECT_EQ(olc2, DEINIT);
}

TEST_F(TestCaseThinClasses, UBSHcomNetAtomicState)
{
    UBSHcomNetAtomicState<int> atomicState(2);
    EXPECT_EQ(atomicState.Get(), 2);
    auto ret = atomicState.Compare(2);
    EXPECT_EQ(ret, true);
    ret = atomicState.Compare(1);
    EXPECT_EQ(ret, false);
    atomicState.Set(1);
    EXPECT_EQ(atomicState.Get(), 1);
    ret = atomicState.CAS(0, 1);
    EXPECT_EQ(ret, false);
    ret = atomicState.CAS(1, 2);
    EXPECT_EQ(ret, true);
    EXPECT_EQ(atomicState.Get(), 2);
}