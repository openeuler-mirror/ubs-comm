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

#include "net_obj_pool.h"
#include "test_spinlock.h"

using namespace ock::hcom;
TestCaseSpinLock::TestCaseSpinLock() {}

void TestCaseSpinLock::SetUp() {}

void TestCaseSpinLock::TearDown() {}

TEST_F(TestCaseSpinLock, Lock)
{
    std::vector<std::thread> ths;
    int count = 1000;
    int counter = 0;
    int scounter = 0;
    NetSpinLock lock;
    for (int i = 0; i < count; ++i) {
        scounter++;
        std::thread th([&]() {
            lock.Lock();
            counter++;
            lock.Unlock();
        });
        ths.push_back(std::move(th));
    }
    for (int i = 0; i < count; ++i) {
        ths[i].join();
    }
    ths.clear();
    EXPECT_EQ(scounter, counter);
    for (int i = 0; i < count; ++i) {
        scounter++;
        std::thread th([&]() {
            lock.Lock();
            counter--;
            lock.Unlock();
        });
        ths.push_back(std::move(th));
    }
    for (int i = 0; i < count; ++i) {
        ths[i].join();
    }
    EXPECT_EQ(0, counter);
}
