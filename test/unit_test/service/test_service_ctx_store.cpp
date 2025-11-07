// Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
// Author: zhiwei

#include <thread>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "net_mem_pool_fixed.h"
#include "service_ctx_store.h"

namespace ock {
namespace hcom {
class TestServiceCtxStore : public testing::Test {
public:
    virtual void SetUp(void)
    {
    }

    virtual void TearDown(void)
    {
        GlobalMockObject::verify();
    }
};

TEST_F(TestServiceCtxStore, CrossThreadReturn)
{
    NetMemPoolFixedOptions options{};
    options.superBlkSizeMB = NN_NO1;  // 1M 一共有 16384 个小块
    options.tcExpandBlkCnt = NN_NO8;  // 扩容时分配的小块个数
    options.minBlkSize = NN_NO64;     // 每个小块大小为 64

    NetMemPoolFixed *mempool1 = new (std::nothrow) NetMemPoolFixed("service1", options);
    ASSERT_NE(mempool1, nullptr);
    mempool1->IncreaseRef();
    mempool1->Initialize();

    NetMemPoolFixed *mempool2 = new (std::nothrow) NetMemPoolFixed("service2", options);
    ASSERT_NE(mempool2, nullptr);
    mempool2->IncreaseRef();
    mempool2->Initialize();

    NetServiceCtxStore *store1 = new (std::nothrow) NetServiceCtxStore(NN_NO1024, mempool1,
        UBSHcomNetDriverProtocol::TCP);
    NetServiceCtxStore *store2 = new (std::nothrow) NetServiceCtxStore(NN_NO1024, mempool2,
        UBSHcomNetDriverProtocol::RDMA);
    ASSERT_NE(store1, nullptr);
    ASSERT_NE(store2, nullptr);

    // 模拟用户线程
    std::vector<int *> ptrs1;
    std::vector<int *> ptrs2;
    std::thread user([&store1, &store2, &ptrs1, &ptrs2]() {
        for (int i = 0; i < NN_NO16; ++i) {
            int *p1 = store1->GetCtxObj<int>();
            int *p2 = store2->GetCtxObj<int>();
            ptrs1.push_back(p1);
            ptrs2.push_back(p2);
        }
    });
    if (user.joinable()) {
        user.join();
    }

    // mempool1 的 1M 内存首地址
    int *start = reinterpret_cast<int *>(mempool1->mSuperBlocks[0].buffer);
    int *end = reinterpret_cast<int *>(reinterpret_cast<uintptr_t>(start) + NN_NO1048576);

    // 如果是旧实现，ptrs2 中的地址都将是在 [start1, start1 + 1MB) 范围之间，后续令 mempool1 析构就会导致 ptrs2 中的内存
    // 访问失败
    for (auto *p : ptrs2) {
        EXPECT_FALSE(p >= start && p < end);
    }

    // 令 mempool1 提前析构，不会出现 coredump
    delete store1;
    mempool1->DecreaseRef();
    EXPECT_NO_FATAL_FAILURE(*ptrs2[0] = 11);

    delete store2;
    mempool2->DecreaseRef();
}

}  // namespace hcom
}  // namespace ock
