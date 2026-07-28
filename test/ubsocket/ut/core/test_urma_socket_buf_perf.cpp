/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>

#include "common/ubsocket_spsc_ring_queue.h"
#include "urma_socket_buf.h"

using namespace ock::ubs::urma;

static constexpr uint64_t kPerfCap = 65536; // 64KB, 1024 chunks
static constexpr uint64_t kAllocSize = 64;  // single chunk per op
static constexpr int kTotalOps = 500000;    // alloc+release pairs
static constexpr int kHandoffCap = 1024;    // pending queue capacity

struct AllocEntry {
    uint64_t offset;
    uint64_t size;
};

template <typename AllocType>
static double RunConcurrentBenchmark()
{
    AllocType buf(kPerfCap);
    ock::ubs::SPSCRingQueue<AllocEntry> handoff(kHandoffCap);

    std::atomic<uint64_t> alloc_done{0};
    std::atomic<uint64_t> release_done{0};

    auto start = std::chrono::steady_clock::now();

    std::thread producer([&]() {
        for (;;) {
            if (alloc_done.load(std::memory_order_relaxed) >= static_cast<uint64_t>(kTotalOps)) {
                break;
            }
            AllocEntry entry;
            if (!buf.Allocate(kAllocSize, entry.offset)) {
                continue;
            }
            entry.size = kAllocSize;
            while (!handoff.Push(entry)) {}
            alloc_done.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::thread consumer([&]() {
        for (;;) {
            if (release_done.load(std::memory_order_relaxed) >= static_cast<uint64_t>(kTotalOps)) {
                break;
            }
            AllocEntry entry;
            if (!handoff.Pop(entry)) {
                continue;
            }
            buf.Release(entry.offset, entry.size);
            release_done.fetch_add(1, std::memory_order_relaxed);
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(end - start).count();
}

TEST(UrmaSockBufPerf, ConcurrentRingBufVsRbTreeBuf)
{
    std::cout << "\n=== Allocator Concurrent Performance ===" << std::endl;
    std::cout << "  capacity: " << kPerfCap << " B,  chunk: " << kAllocSize << " B" << std::endl;
    std::cout << "  total ops: " << kTotalOps << std::endl;
    std::cout << "  handoff queue: " << kHandoffCap << std::endl;

    double ring_sec = RunConcurrentBenchmark<UrmaSockRingBuf>();
    double ring_rate = kTotalOps / ring_sec;
    std::cout << "  UrmaSockRingBuf: " << ring_sec << " s  (" << static_cast<uint64_t>(ring_rate) << " ops/s)"
              << std::endl;

    double rbtree_sec = RunConcurrentBenchmark<UrmaSockRbTreeBuf>();
    double rbtree_rate = kTotalOps / rbtree_sec;
    std::cout << "  UrmaSockRbTreeBuf: " << rbtree_sec << " s  (" << static_cast<uint64_t>(rbtree_rate) << " ops/s)"
              << std::endl;

    double ratio = ring_rate / rbtree_rate;
    std::cout << "  throughput ratio (RingBuf / RbTreeBuf): " << ratio << "x" << std::endl;

    EXPECT_GT(ring_rate, 0);
    EXPECT_GT(rbtree_rate, 0);
}
