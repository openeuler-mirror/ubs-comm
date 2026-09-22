/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of the Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "iobuf/ubsocket_iobuf.h"
#include "iobuf/ubsocket_zcopy_adapter.h"

#include <gtest/gtest.h>

using namespace ock::ubs;

namespace {
constexpr uint32_t BLOCK_CAPACITY = 16;

struct BlockStorage {
    alignas(Block) char bytes[sizeof(Block) + BLOCK_CAPACITY]{};

    char *Data()
    {
        return bytes + sizeof(Block);
    }
};

class TestZeroCopyAllocator : public UbsZeroCopyAllocator {
public:
    void *allocate(size_t, const ubs_iobuf_alloc_option_t *) override
    {
        return this;
    }

    void deallocate(void *) override {}
};
} // namespace

class IobufTest : public ::testing::Test {
protected:
    void TearDown() override
    {
        cache_.Flush();
    }

    Block *Insert(BlockStorage &storage, uint32_t capacity)
    {
        cache_.Insert(storage.Data(), capacity);
        return reinterpret_cast<Block *>(storage.Data() - sizeof(Block));
    }

    BlockCache cache_;
};

TEST_F(IobufTest, Block_ConstructorAndSpaceState)
{
    BlockStorage storage;
    Block *block = new (storage.bytes) Block(storage.Data(), BLOCK_CAPACITY, 2);

    EXPECT_EQ(block->nshared.load(), 2);
    EXPECT_EQ(block->flags, IOBUF_BLOCK_FLAGS_UB);
    EXPECT_EQ(block->size, 0u);
    EXPECT_EQ(block->cap, BLOCK_CAPACITY);
    EXPECT_FALSE(block->Full());
    EXPECT_EQ(block->LeftSpace(), BLOCK_CAPACITY);

    block->size = BLOCK_CAPACITY;
    EXPECT_TRUE(block->Full());
    EXPECT_EQ(block->LeftSpace(), 0u);
    block->~Block();
}

TEST_F(IobufTest, Block_ReferenceAndLinkedListOperations)
{
    BlockStorage first_storage;
    BlockStorage second_storage;
    Block *first = new (first_storage.bytes) Block(first_storage.Data(), BLOCK_CAPACITY);
    Block *second = new (second_storage.bytes) Block(second_storage.Data(), BLOCK_CAPACITY);

    EXPECT_EQ(first->SetNext(second), second);
    EXPECT_EQ(first->GetNext(), second);
    first->IncRef();
    EXPECT_EQ(first->nshared.load(), 2);
    first->DecRef();
    EXPECT_EQ(first->nshared.load(), 1);
    first->DecRef();
    second->DecRef();
}

TEST_F(IobufTest, BlockRef_ResetClearsAllFields)
{
    BlockStorage storage;
    Block block(storage.Data(), BLOCK_CAPACITY);
    BlockRef ref{3, 7, &block};

    ref.Reset();

    EXPECT_EQ(ref.offset, 0u);
    EXPECT_EQ(ref.length, 0u);
    EXPECT_EQ(ref.block, nullptr);
}

TEST_F(IobufTest, ZeroCopyAllocator_DefaultAllocateForwardsNullOption)
{
    TestZeroCopyAllocator allocator;
    UbsZeroCopyAllocator *base = &allocator;

    EXPECT_EQ(base->allocate(32), static_cast<void *>(&allocator));
    base->deallocate(nullptr);
}

TEST_F(IobufTest, CutAndInsertAfter_EmptyOrNullInputReturnsZero)
{
    BlockStorage storage;
    Block *block = new (storage.bytes) Block(storage.Data(), BLOCK_CAPACITY);

    EXPECT_EQ(cache_.CutAndInsertAfter(4, nullptr), 0);
    EXPECT_EQ(cache_.CutAndInsertAfter(4, block), 0);
    block->DecRef();

    Insert(storage, 4);
    EXPECT_EQ(cache_.CutAndInsertAfter(4, nullptr), 0);
}

TEST_F(IobufTest, InsertAndFlush_ReleasesAllCachedBlocks)
{
    BlockStorage first_storage;
    BlockStorage second_storage;
    Insert(first_storage, 4);
    Insert(second_storage, 8);

    EXPECT_EQ(cache_.GetCacheLen(), 12u);
    EXPECT_NE(cache_.head_block_, nullptr);
    EXPECT_NE(cache_.tail_block_, nullptr);

    cache_.Flush();

    EXPECT_EQ(cache_.GetCacheLen(), 0u);
    EXPECT_EQ(cache_.head_block_, nullptr);
    EXPECT_EQ(cache_.tail_block_, nullptr);
}

TEST_F(IobufTest, CutAndInsertAfter_PartialBlockCanBeConsumedInPieces)
{
    BlockStorage cached_storage;
    BlockStorage output_storage;
    Block *cached = Insert(cached_storage, 10);
    Block *output = new (output_storage.bytes) Block(output_storage.Data(), 1);

    EXPECT_EQ(cache_.CutAndInsertAfter(4, output), 4);
    ASSERT_NE(cache_.partial_block_.block, nullptr);
    EXPECT_EQ(cache_.partial_block_.offset, 4u);
    EXPECT_EQ(cache_.partial_block_.length, 6u);
    EXPECT_EQ(cached->cap, 4u);
    EXPECT_EQ(cache_.GetCacheLen(), 6u);

    EXPECT_EQ(cache_.CutAndInsertAfter(2, output), 2);
    EXPECT_EQ(cache_.partial_block_.offset, 6u);
    EXPECT_EQ(cache_.partial_block_.length, 4u);
    EXPECT_EQ(cached->cap, 6u);

    EXPECT_EQ(cache_.CutAndInsertAfter(4, output), 4);
    EXPECT_EQ(cache_.partial_block_.block, nullptr);
    EXPECT_EQ(cache_.GetCacheLen(), 0u);
    output->DecRef();
}

TEST_F(IobufTest, Flush_WithPartialBlock_ReleasesPartialReference)
{
    BlockStorage cached_storage;
    BlockStorage output_storage;
    Insert(cached_storage, 10);
    Block *output = new (output_storage.bytes) Block(output_storage.Data(), 1);

    EXPECT_EQ(cache_.CutAndInsertAfter(4, output), 4);
    ASSERT_NE(cache_.partial_block_.block, nullptr);

    cache_.Flush();

    EXPECT_EQ(cache_.partial_block_.block, nullptr);
    EXPECT_EQ(cache_.GetCacheLen(), 6u);
    output->DecRef();
}

TEST_F(IobufTest, Flush_WithPartialAndRemainingBlocks_ClearsBothLists)
{
    BlockStorage first_storage;
    BlockStorage second_storage;
    BlockStorage output_storage;
    Insert(first_storage, 10);
    Insert(second_storage, 5);
    Block *output = new (output_storage.bytes) Block(output_storage.Data(), 1);

    EXPECT_EQ(cache_.CutAndInsertAfter(4, output), 4);
    ASSERT_NE(cache_.partial_block_.block, nullptr);
    ASSERT_NE(cache_.head_block_, nullptr);

    cache_.Flush();

    EXPECT_EQ(cache_.partial_block_.block, nullptr);
    EXPECT_EQ(cache_.head_block_, nullptr);
    EXPECT_EQ(cache_.tail_block_, nullptr);
    output->DecRef();
}

TEST_F(IobufTest, CutAndInsertAfter_ZeroCutKeepsPartialBlock)
{
    BlockStorage cached_storage;
    BlockStorage output_storage;
    Insert(cached_storage, 10);
    Block *output = new (output_storage.bytes) Block(output_storage.Data(), 1);

    EXPECT_EQ(cache_.CutAndInsertAfter(4, output), 4);
    EXPECT_EQ(cache_.CutAndInsertAfter(0, output), 0);
    ASSERT_NE(cache_.partial_block_.block, nullptr);
    EXPECT_EQ(cache_.partial_block_.offset, 4u);
    EXPECT_EQ(cache_.partial_block_.length, 6u);
    output->DecRef();
}

TEST_F(IobufTest, CutAndInsertAfter_ExactBlockCreatesEmptyPartial)
{
    BlockStorage first_storage;
    BlockStorage second_storage;
    BlockStorage output_storage;
    Insert(first_storage, 10);
    Insert(second_storage, 5);
    Block *output = new (output_storage.bytes) Block(output_storage.Data(), 1);

    EXPECT_EQ(cache_.CutAndInsertAfter(10, output), 10);
    ASSERT_NE(cache_.partial_block_.block, nullptr);
    EXPECT_EQ(cache_.partial_block_.length, 0u);
    ASSERT_NE(cache_.head_block_, nullptr);
    EXPECT_EQ(cache_.head_block_->cap, 5u);
    output->DecRef();
}

TEST_F(IobufTest, CutAndInsertAfter_PreservesExistingOutputChain)
{
    BlockStorage cached_storage;
    BlockStorage output_storage;
    BlockStorage next_output_storage;
    Insert(cached_storage, 4);
    Block *output = new (output_storage.bytes) Block(output_storage.Data(), 1);
    Block *next_output = new (next_output_storage.bytes) Block(next_output_storage.Data(), 1);
    output->SetNext(next_output);

    EXPECT_EQ(cache_.CutAndInsertAfter(4, output), 4);
    ASSERT_NE(output->GetNext(), next_output);
    EXPECT_EQ(output->GetNext()->GetNext(), next_output);
    EXPECT_EQ(cache_.GetCacheLen(), 0u);
    output->DecRef();
    next_output->DecRef();
}

TEST_F(IobufTest, CutAndInsertAfter_ExactPartialThenRemainingBlock)
{
    BlockStorage first_storage;
    BlockStorage second_storage;
    BlockStorage output_storage;
    Insert(first_storage, 10);
    Insert(second_storage, 5);
    Block *output = new (output_storage.bytes) Block(output_storage.Data(), 1);

    EXPECT_EQ(cache_.CutAndInsertAfter(4, output), 4);
    ASSERT_NE(cache_.partial_block_.block, nullptr);
    EXPECT_EQ(cache_.CutAndInsertAfter(6, output), 6);
    EXPECT_EQ(cache_.partial_block_.block, nullptr);
    EXPECT_EQ(cache_.GetCacheLen(), 5u);
    EXPECT_EQ(cache_.head_block_, nullptr);
    output->DecRef();
}

TEST_F(IobufTest, CutAndInsertAfter_NonFirstBlockStopsBeforeCut)
{
    BlockStorage first_storage;
    BlockStorage second_storage;
    BlockStorage output_storage;
    Insert(first_storage, 3);
    Insert(second_storage, 5);
    Block *output = new (output_storage.bytes) Block(output_storage.Data(), 1);

    EXPECT_EQ(cache_.CutAndInsertAfter(6, output), 3);
    EXPECT_EQ(cache_.GetCacheLen(), 5u);
    EXPECT_NE(cache_.head_block_, nullptr);
    EXPECT_EQ(cache_.head_block_->cap, 5u);
    output->DecRef();
}

TEST_F(IobufTest, CutAndInsertAfter_CutBeyondAllBlocksReturnsAvailableBytes)
{
    BlockStorage first_storage;
    BlockStorage second_storage;
    BlockStorage output_storage;
    Insert(first_storage, 3);
    Insert(second_storage, 5);
    Block *output = new (output_storage.bytes) Block(output_storage.Data(), 1);

    EXPECT_EQ(cache_.CutAndInsertAfter(10, output), 8);
    EXPECT_EQ(cache_.GetCacheLen(), 0u);
    EXPECT_EQ(cache_.partial_block_.block, nullptr);
    cache_.Flush();
    output->DecRef();
}
