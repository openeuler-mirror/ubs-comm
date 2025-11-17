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
#include "test_obj_pool.h"
#include "hcom_def.h"
#include "net_common.h"
#include "net_obj_pool.h"
#include "ut_helper.h"

using namespace ock::hcom;
constexpr uint32_t NN_NO11 = 11;
TestCaseObjPool::TestCaseObjPool() {}

void TestCaseObjPool::SetUp() {}

void TestCaseObjPool::TearDown() {}

TEST_F(TestCaseObjPool, NetObjPool_Serial)
{
    NResult result;
    NetObjPool<DummyObj> pool("test_pool", NN_NO2);
    result = pool.Initialize();
    EXPECT_EQ(result, NN_OK);

    DummyObj *rObj1;
    DummyObj *rObj2;
    DummyObj *rObj3;
    auto ret = pool.Dequeue(rObj1);
    EXPECT_EQ(ret, true);
    rObj1->tag = 1;
    ret = pool.Dequeue(rObj2);
    EXPECT_EQ(ret, true);
    rObj2->tag = NN_NO2;
    ret = pool.Dequeue(rObj3);
    EXPECT_EQ(ret, true);
    pool.Enqueue(rObj1);
    pool.Enqueue(rObj2);
    pool.Enqueue(rObj3);
    DummyObj *rObj4;
    DummyObj *rObj5;
    ret = pool.Dequeue(rObj4);
    EXPECT_EQ(ret, true);
    EXPECT_EQ(rObj4->tag, NN_NO2);
    ret = pool.Dequeue(rObj5);
    EXPECT_EQ(ret, true);
    EXPECT_EQ(rObj5->tag, 1);
}

TEST_F(TestCaseObjPool, NetObjPool_Concurrency)
{
    std::vector<DummyObj *> objs(NN_NO11);

    NetObjPool<DummyObj> pool("test_pool", NN_NO10);
    std::vector<std::thread> ths;
    int vsum = 0;
    for (int i = 0; i < NN_NO10; ++i) {
        vsum += i;
        std::thread th([&]() { pool.Initialize(); });
        ths.push_back(std::move(th));
    }
    for (uint64_t i = 0; i < ths.size(); ++i) {
        ths[i].join();
    }
    ths.clear();

    for (int i = 0; i < NN_NO10; ++i) {
        pool.Dequeue(objs[i]);
        objs[i]->tag = i;
    }

    for (int i = 0; i < NN_NO10; ++i) {
        std::thread th([&, i]() { pool.Enqueue(objs[i]); });
        ths.push_back(std::move(th));
    }
    for (uint64_t i = 0; i < ths.size(); ++i) {
        ths[i].join();
    }
    int sum = 0;
    for (int i = 0; i < NN_NO10; ++i) {
        DummyObj *obj;
        pool.Dequeue(obj);
        sum += obj->tag;
    }
    EXPECT_EQ(sum, vsum);
}