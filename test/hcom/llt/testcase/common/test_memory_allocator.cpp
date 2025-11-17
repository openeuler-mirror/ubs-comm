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
#include <random>
#include <thread>

#include "hcom.h"
#include "hcom_def.h"
#include "hcom_utils.h"
#include "net_mem_allocator.h"
#include "test_memory_allocator.h"

#define SIZE (256UL << 12)
#define MRKEY 1

using namespace ock::hcom;

TestMemoryAllocator::TestMemoryAllocator() {}

void TestMemoryAllocator::SetUp() {}

void TestMemoryAllocator::TearDown() {}

static void ConcurrentRoutine(UBSHcomNetMemoryAllocatorPtr ptr, const int count, bool random,
    std::atomic_uint64_t &allocCost, std::atomic_uint64_t &freeCost)
{
    uint64_t allocTotalTime = 0;

    std::vector<uintptr_t> addrs;
    auto blocks = SIZE / NN_NO256;

    for (int i = 0; i < count; ++i) {
        uint64_t addr = 0;
        auto allocTime = MONOTONIC_TIME_NS();
        auto size = SIZE;
        if (random) {
            size = (i % blocks + 1) * (NN_NO256);
        }
        auto res = ptr->Allocate(size, addr);
        allocTotalTime += (MONOTONIC_TIME_NS() - allocTime);
        ASSERT_EQ(res, NN_OK);
        if (addr > 0) {
            addrs.emplace_back(addr);
        }
    }

    uint64_t freeTotalTime = MONOTONIC_TIME_NS();

    for (uint32_t i = 0; i < addrs.size(); ++i) {
        auto ret = ptr->Free(addrs[i]);
        ASSERT_EQ(ret, NN_OK);
    }

    freeTotalTime = MONOTONIC_TIME_NS() - freeTotalTime;
    allocCost.fetch_add(allocTotalTime);
    freeCost.fetch_add(freeTotalTime);
}

TEST_F(TestMemoryAllocator, Serial)
{
    for (int k = 0; k < NN_NO4; ++k) {
        bool res = false;
        auto address = memalign(NN_NO4096, SIZE);
        UBSHcomNetMemoryAllocatorPtr ptr;
        UBSHcomNetMemoryAllocatorOptions options;
        options.address = reinterpret_cast<uintptr_t>(address);
        options.size = SIZE;
        options.minBlockSize = NN_NO4096;
        UBSHcomNetMemoryAllocator::Create(ock::hcom::DYNAMIC_SIZE, options, ptr);
        uint64_t addr = 0;
        for (int i = 0; i < NN_NO4; ++i) {
            auto expectSize = SIZE / ((i % 2) * 2 + 2) - 16;
            res = ptr->Allocate(expectSize, addr);
            ASSERT_EQ(res, NN_OK);
            res = ptr->Free(addr);
            ASSERT_EQ(res, NN_OK);
        }
        res = ptr->Allocate(SIZE, addr);
        ASSERT_EQ(res, NN_OK);
    }
}

TEST_F(TestMemoryAllocator, GetSizeNoAlign)
{
    bool res = false;
    auto address = memalign(NN_NO4096, SIZE * 16);
    UBSHcomNetMemoryAllocatorPtr ptr;
    UBSHcomNetMemoryAllocatorOptions options;
    options.address = reinterpret_cast<uintptr_t>(address);
    options.size = SIZE * NN_NO16;
    options.minBlockSize = NN_NO4096;
    UBSHcomNetMemoryAllocator::Create(ock::hcom::DYNAMIC_SIZE, options, ptr);
    uint64_t addr = 0;
    uint64_t addrs[16];
    uint64_t sizes[16];
    for (int i = 0; i < NN_NO4; ++i) {
        auto expectSize = random() % SIZE + 1;
        res = ptr->Allocate(expectSize, addr);
        ASSERT_EQ(res, NN_OK);
        addrs[i] = addr;
        sizes[i] = expectSize;
    }

    for (int i = 0; i < NN_NO4; ++i) {
        addr = addrs[i];
        auto expectSize = sizes[i];
        uint64_t retSize;
        res = ptr.ToChild<NetMemAllocator>()->GetSizeByAddressNoAlign(addr, retSize);
        ASSERT_EQ(res, NN_OK);
        ASSERT_EQ(expectSize, retSize);
    }
}

TEST_F(TestMemoryAllocator, SerialAlign4k)
{
    auto address = memalign(NN_NO4096, SIZE);
    UBSHcomNetMemoryAllocatorPtr ptr;
    UBSHcomNetMemoryAllocatorOptions options;
    options.address = reinterpret_cast<uintptr_t>(address);
    options.size = SIZE;
    options.minBlockSize = NN_NO4096;
    options.alignedAddress = true;
    UBSHcomNetMemoryAllocator::Create(ock::hcom::DYNAMIC_SIZE, options, ptr);
    uint64_t addr = 0;
    for (int i = 0; i < NN_NO4; ++i) {
        auto expectSize = NN_NO4096;
        ptr->Allocate(expectSize, addr);
        EXPECT_EQ(addr % NN_NO4096, 0);
    }
}

TEST_F(TestMemoryAllocator, SimpleConcurrent)
{
    uint64_t size = 8192 * 16;
    auto address = memalign(NN_NO4096, size);

    UBSHcomNetMemoryAllocatorPtr ptr;
    UBSHcomNetMemoryAllocatorOptions options;
    options.address = reinterpret_cast<uintptr_t>(address);
    options.size = size;
    options.minBlockSize = NN_NO4096;
    options.alignedAddress = true;
    UBSHcomNetMemoryAllocator::Create(ock::hcom::DYNAMIC_SIZE, options, ptr);
    std::vector<std::thread> ths;
    for (int i = 0; i < NN_NO4; ++i) {
        std::thread th([&]() {
            for (int j = 0; j < NN_NO4; ++j) {
                uint64_t addr;
                auto res = ptr->Allocate(NN_NO8192, addr);
                ASSERT_EQ(res, NN_OK);
            }
        });
        ths.push_back(std::move(th));
    }
    for (int i = 0; i < NN_NO4; ++i) {
        ths[i].join();
    }
    uint64_t addr;
    auto res = ptr->Allocate(NN_NO8192, addr);
    ASSERT_NE(res, NN_OK);
}

TEST_F(TestMemoryAllocator, Concurrent)
{
    std::atomic_uint64_t allocCost { 0 };
    std::atomic_uint64_t freeCost { 0 };

    for (int k = 0; k < NN_NO4; ++k) {
        const auto threadCount = 10;
        const auto blockCount = 20;
        auto totalSize = SIZE * blockCount * threadCount;
        auto address = memalign(NN_NO4096, totalSize);

        UBSHcomNetMemoryAllocatorPtr ptr;
        UBSHcomNetMemoryAllocatorOptions options;
        options.address = reinterpret_cast<uintptr_t>(address);
        options.size = totalSize;
        options.minBlockSize = NN_NO4096;
        options.alignedAddress = true;
        UBSHcomNetMemoryAllocator::Create(ock::hcom::DYNAMIC_SIZE, options, ptr);

        std::vector<std::thread> threads;

        for (int i = 0; i < threadCount; ++i) {
            threads.emplace_back(ConcurrentRoutine, ptr, blockCount, false, std::ref(allocCost), std::ref(freeCost));
        }
        for (int i = 0; i < threadCount; ++i) {
            threads[i].join();
        }

        ptr->Destroy();
        free(address);
    }
}

TEST_F(TestMemoryAllocator, PerfSerialWithRandomSize)
{
    auto bigSize = SIZE << 4;

    for (int k = 0; k < NN_NO4; ++k) {
        bool res = false;
        auto address = memalign(NN_NO4096, bigSize);
        UBSHcomNetMemoryAllocatorPtr ptr;
        UBSHcomNetMemoryAllocatorOptions options;
        options.address = reinterpret_cast<uintptr_t>(address);
        options.size = bigSize;
        options.minBlockSize = NN_NO4096;
        UBSHcomNetMemoryAllocator::Create(ock::hcom::DYNAMIC_SIZE, options, ptr);
        uint64_t addr = 0;
        for (int i = 0; i < NN_NO4; ++i) {
            auto expectSize = random() % bigSize - 16;
            res = ptr->Allocate(expectSize, addr);
            ASSERT_EQ(res, NN_OK);
            res = ptr->Free(addr);
            ASSERT_EQ(res, NN_OK);
        }
        res = ptr->Allocate(SIZE, addr);
        ASSERT_EQ(res, NN_OK);
    }
}

TEST_F(TestMemoryAllocator, CompareToNudeMalloc)
{
    auto size = SIZE << 4;
    auto address = memalign(NN_NO4096, SIZE * 16);
    uintptr_t addr = 0;
    uint64_t block = size / NN_NO256;
    uint64_t cost[4] = {0, 0, 0, 0};
    int loopCount = 100;

    UBSHcomNetMemoryAllocatorPtr ptr;
    UBSHcomNetMemoryAllocatorOptions options;
    options.address = reinterpret_cast<uintptr_t>(address);
    options.size = SIZE * NN_NO16;
    options.minBlockSize = NN_NO4096;
    UBSHcomNetMemoryAllocator::Create(ock::hcom::DYNAMIC_SIZE, options, ptr);
    for (int i = 0; i < loopCount; ++i) {
        auto esize = (random() % block) * NN_NO256;
        auto maAllocCost = MONOTONIC_TIME_NS();
        ptr->Allocate(esize, addr);
        maAllocCost = MONOTONIC_TIME_NS() - maAllocCost;
        cost[0] += maAllocCost;

        auto maFreeCost = MONOTONIC_TIME_NS();
        ptr->Free(addr);
        maFreeCost = MONOTONIC_TIME_NS() - maFreeCost;
        cost[1] += maFreeCost;

        auto nuAllocCost = MONOTONIC_TIME_NS();
        auto addr1 = malloc(esize);
        nuAllocCost = MONOTONIC_TIME_NS() - nuAllocCost;
        cost[NN_NO2] += nuAllocCost;

        auto nuFreeCost = MONOTONIC_TIME_NS();
        free(addr1);
        nuFreeCost = MONOTONIC_TIME_NS() - nuFreeCost;
        cost[NN_NO3] += nuFreeCost;
    }

    NN_LOG_INFO("ma alloc cost:" << cost[NN_NO0] / loopCount << "ns, " <<
                "ma free cost:" << cost[NN_NO1] / loopCount << "ns, " <<
                "na free cost:" << cost[NN_NO2] / loopCount << "ns, " <<
                "na free cost:" << cost[NN_NO3] / loopCount << "ns");
}

TEST_F(TestMemoryAllocator, PerfConcurrentWithRandomSize)
{
    std::atomic_uint64_t allocCost { 0 };
    std::atomic_uint64_t freeCost { 0 };
    const auto threadCount = 10;
    const auto blockCount = 20;

    auto totalSize = SIZE * blockCount * threadCount;

    auto address = memalign(NN_NO4096, totalSize);

    bzero(address, totalSize);

    UBSHcomNetMemoryAllocatorPtr ptr;
    UBSHcomNetMemoryAllocatorOptions options;
    options.address = reinterpret_cast<uintptr_t>(address);
    options.size = totalSize;
    options.minBlockSize = NN_NO4096;
    UBSHcomNetMemoryAllocator::Create(ock::hcom::DYNAMIC_SIZE, options, ptr);

    std::vector<std::thread> threads;

    for (int i = 0; i < threadCount; ++i) {
        threads.emplace_back(ConcurrentRoutine, ptr, blockCount, true, std::ref(allocCost), std::ref(freeCost));
    }
    for (int i = 0; i < threadCount; ++i) {
        threads[i].join();
    }

    NN_LOG_INFO("alloc avg cost " << allocCost / threadCount / blockCount << "ns"
                                  << " free avg cost " << freeCost / threadCount / blockCount << "ns");
    ptr->Destroy();
    free(address);
}