/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include <cstdlib>
#include <vector>

#include "include/ubsocket.h"
#include "iobuf/ubsocket_iobuf.h"

using namespace ock::ubs;

namespace {
static const uint32_t TOTAL_ALLOC_SIZE = 8192;
static const uint32_t TEST_BLOCK_CAP = TOTAL_ALLOC_SIZE - static_cast<uint32_t>(sizeof(Block));
static const uint32_t TEST_BLOCK_SMALL_CAP = 4096;

} // namespace

class BlockCacheTest : public ::testing::Test {
protected:
    std::vector<void *> raw_bufs_;

    char *MakeBufAligned()
    {
        void *ptr = nullptr;
        int ret = posix_memalign(&ptr, TOTAL_ALLOC_SIZE, TOTAL_ALLOC_SIZE);
        if (ret != 0) {
            return nullptr;
        }
        raw_bufs_.push_back(ptr);
        return static_cast<char *>(ptr);
    }

    char *MakeBufWithBlock()
    {
        char *raw = MakeBufAligned();
        return raw + sizeof(Block);
    }

    Block *MakeAndInitOutputBlock()
    {
        char *raw = MakeBufAligned();
        Block *block = reinterpret_cast<Block *>(raw);
        new (block) Block(raw + sizeof(Block), TEST_BLOCK_CAP);
        return block;
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        for (auto *p : raw_bufs_) {
            free(p);
        }
        raw_bufs_.clear();
    }
};

TEST_F(BlockCacheTest, GetCacheLen_Initial_Zero)
{
    BlockCache cache;
    EXPECT_EQ(cache.GetCacheLen(), 0ULL);
}

TEST_F(BlockCacheTest, Insert_SingleBlock_AddsToCache)
{
    BlockCache cache;
    MOCKER_CPP(::ubsocket_iobuf_deallocate).stubs();

    char *data = MakeBufWithBlock();
    cache.Insert(data, TEST_BLOCK_CAP);
    EXPECT_EQ(cache.GetCacheLen(), static_cast<uint64_t>(TEST_BLOCK_CAP));
}

TEST_F(BlockCacheTest, Insert_MultipleBlocks_SummedLen)
{
    BlockCache cache;
    MOCKER_CPP(::ubsocket_iobuf_deallocate).stubs();

    char *d1 = MakeBufWithBlock();
    char *d2 = MakeBufWithBlock();
    char *d3 = MakeBufWithBlock();

    cache.Insert(d1, TEST_BLOCK_CAP);
    cache.Insert(d2, TEST_BLOCK_SMALL_CAP);
    cache.Insert(d3, TEST_BLOCK_CAP);

    uint64_t expected = static_cast<uint64_t>(TEST_BLOCK_CAP) * 2 + static_cast<uint64_t>(TEST_BLOCK_SMALL_CAP);
    EXPECT_EQ(cache.GetCacheLen(), expected);
}

TEST_F(BlockCacheTest, CutAndInsertAfter_EmptyCache_ReturnsZero)
{
    BlockCache cache;
    MOCKER_CPP(::ubsocket_iobuf_deallocate).stubs();

    Block *out_block = MakeAndInitOutputBlock();

    ssize_t ret = cache.CutAndInsertAfter(1024, out_block);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(cache.GetCacheLen(), 0ULL);
}

TEST_F(BlockCacheTest, CutAndInsertAfter_NullBlock_ReturnsZero)
{
    BlockCache cache;
    MOCKER_CPP(::ubsocket_iobuf_deallocate).stubs();

    char *data = MakeBufWithBlock();
    cache.Insert(data, TEST_BLOCK_CAP);

    ssize_t ret = cache.CutAndInsertAfter(1024, nullptr);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(cache.GetCacheLen(), static_cast<uint64_t>(TEST_BLOCK_CAP));
}

TEST_F(BlockCacheTest, CutAndInsertAfter_CacheSmallerThanCut_ConsumesAll)
{
    BlockCache cache;
    MOCKER_CPP(::ubsocket_iobuf_deallocate).stubs();

    char *d1 = MakeBufWithBlock();
    cache.Insert(d1, TEST_BLOCK_SMALL_CAP);

    Block *out_block = MakeAndInitOutputBlock();

    ssize_t ret = cache.CutAndInsertAfter(8192, out_block);
    EXPECT_EQ(ret, static_cast<ssize_t>(TEST_BLOCK_SMALL_CAP));
    EXPECT_EQ(cache.GetCacheLen(), 0ULL);
    EXPECT_EQ(out_block->GetNext(), reinterpret_cast<Block *>(d1 - sizeof(Block)));
}

TEST_F(BlockCacheTest, CutAndInsertAfter_FirstBlockLargerThanCut_CreatesPartial)
{
    BlockCache cache;
    MOCKER_CPP(::ubsocket_iobuf_deallocate).stubs();

    char *d1 = MakeBufWithBlock();
    cache.Insert(d1, TEST_BLOCK_CAP);

    Block *out_block = MakeAndInitOutputBlock();

    uint32_t cut = 1024;
    ssize_t ret = cache.CutAndInsertAfter(cut, out_block);
    EXPECT_EQ(ret, static_cast<ssize_t>(cut));
    EXPECT_GT(cache.GetCacheLen(), 0ULL);
    EXPECT_EQ(out_block->GetNext(), reinterpret_cast<Block *>(d1 - sizeof(Block)));
}

TEST_F(BlockCacheTest, CutAndInsertAfter_FirstBlockExactCap_ConsumesWhole)
{
    BlockCache cache;
    MOCKER_CPP(::ubsocket_iobuf_deallocate).stubs();

    char *d1 = MakeBufWithBlock();
    cache.Insert(d1, TEST_BLOCK_SMALL_CAP);

    Block *out_block = MakeAndInitOutputBlock();

    ssize_t ret = cache.CutAndInsertAfter(TEST_BLOCK_SMALL_CAP, out_block);

    Block *cache_block = reinterpret_cast<Block *>(d1 - sizeof(Block));
    EXPECT_EQ(ret, static_cast<ssize_t>(TEST_BLOCK_SMALL_CAP));
    EXPECT_EQ(cache.GetCacheLen(), 0ULL);
    EXPECT_EQ(out_block->GetNext(), cache_block);
    EXPECT_EQ(cache_block->GetNext(), nullptr);
}

TEST_F(BlockCacheTest, CutAndInsertAfter_TwoBlocksFirstConsumedSecondCantSplit)
{
    BlockCache cache;
    MOCKER_CPP(::ubsocket_iobuf_deallocate).stubs();

    char *d1 = MakeBufWithBlock();
    char *d2 = MakeBufWithBlock();
    cache.Insert(d1, TEST_BLOCK_SMALL_CAP);
    cache.Insert(d2, TEST_BLOCK_SMALL_CAP);

    Block *out_block = MakeAndInitOutputBlock();

    uint32_t cut = TEST_BLOCK_SMALL_CAP + 1;
    ssize_t ret = cache.CutAndInsertAfter(cut, out_block);

    EXPECT_EQ(ret, static_cast<ssize_t>(TEST_BLOCK_SMALL_CAP));
    EXPECT_EQ(cache.GetCacheLen(), static_cast<uint64_t>(TEST_BLOCK_SMALL_CAP));
}

TEST_F(BlockCacheTest, CutAndInsertAfter_NonFirstBlockPartialCut_BreaksEarly)
{
    BlockCache cache;
    MOCKER_CPP(::ubsocket_iobuf_deallocate).stubs();

    char *d1 = MakeBufWithBlock();
    char *d2 = MakeBufWithBlock();
    char *d3 = MakeBufWithBlock();
    cache.Insert(d1, TEST_BLOCK_SMALL_CAP);
    cache.Insert(d2, TEST_BLOCK_CAP);
    cache.Insert(d3, TEST_BLOCK_SMALL_CAP);

    Block *out_block = MakeAndInitOutputBlock();

    uint32_t cut = TEST_BLOCK_SMALL_CAP + 512;
    ssize_t ret = cache.CutAndInsertAfter(cut, out_block);

    EXPECT_EQ(ret, static_cast<ssize_t>(TEST_BLOCK_SMALL_CAP));
    EXPECT_EQ(cache.GetCacheLen(),
              static_cast<uint64_t>(TEST_BLOCK_CAP) + static_cast<uint64_t>(TEST_BLOCK_SMALL_CAP));
}

TEST_F(BlockCacheTest, CutAndInsertAfter_PreservesOutputChain)
{
    BlockCache cache;
    MOCKER_CPP(::ubsocket_iobuf_deallocate).stubs();

    char *d1 = MakeBufWithBlock();
    cache.Insert(d1, TEST_BLOCK_SMALL_CAP);

    Block *out_block = MakeAndInitOutputBlock();
    Block *out_sec = MakeAndInitOutputBlock();
    out_block->SetNext(out_sec);

    ssize_t ret = cache.CutAndInsertAfter(TEST_BLOCK_SMALL_CAP, out_block);

    EXPECT_EQ(ret, static_cast<ssize_t>(TEST_BLOCK_SMALL_CAP));
    EXPECT_EQ(cache.GetCacheLen(), 0ULL);

    Block *cache_block = reinterpret_cast<Block *>(d1 - sizeof(Block));
    EXPECT_EQ(out_block->GetNext(), cache_block);
    EXPECT_EQ(cache_block->GetNext(), out_sec);
}

TEST_F(BlockCacheTest, Flush_EmptyCache_NoOp)
{
    BlockCache cache;
    MOCKER_CPP(::ubsocket_iobuf_deallocate).stubs();
    cache.Flush();
    EXPECT_EQ(cache.GetCacheLen(), 0ULL);
}

TEST_F(BlockCacheTest, Flush_WithSingleBlock_ClearsCache)
{
    BlockCache cache;
    MOCKER_CPP(::ubsocket_iobuf_deallocate).stubs();

    char *d1 = MakeBufWithBlock();
    cache.Insert(d1, TEST_BLOCK_CAP);
    EXPECT_EQ(cache.GetCacheLen(), static_cast<uint64_t>(TEST_BLOCK_CAP));

    cache.Flush();
    EXPECT_EQ(cache.GetCacheLen(), 0ULL);
}

TEST_F(BlockCacheTest, Flush_WithMultipleBlocks_ClearsAll)
{
    BlockCache cache;
    MOCKER_CPP(::ubsocket_iobuf_deallocate).stubs();

    char *d1 = MakeBufWithBlock();
    char *d2 = MakeBufWithBlock();
    char *d3 = MakeBufWithBlock();
    cache.Insert(d1, TEST_BLOCK_CAP);
    cache.Insert(d2, TEST_BLOCK_CAP);
    cache.Insert(d3, TEST_BLOCK_CAP);
    EXPECT_EQ(cache.GetCacheLen(), static_cast<uint64_t>(TEST_BLOCK_CAP) * 3);

    cache.Flush();
    EXPECT_EQ(cache.GetCacheLen(), 0ULL);
}

TEST_F(BlockCacheTest, CutAndInsertAfter_StepByStepScenario)
{
    BlockCache cache;
    MOCKER_CPP(::ubsocket_iobuf_deallocate).stubs();

    char *d1 = MakeBufWithBlock();
    char *d2 = MakeBufWithBlock();
    cache.Insert(d1, 8160);
    cache.Insert(d2, 68);

    Block *out1 = MakeAndInitOutputBlock();
    ssize_t r1 = cache.CutAndInsertAfter(32, out1);
    EXPECT_EQ(r1, static_cast<ssize_t>(32));

    Block *out2 = MakeAndInitOutputBlock();
    ssize_t r2 = cache.CutAndInsertAfter(8128, out2);
    EXPECT_EQ(r2, static_cast<ssize_t>(8128));

    Block *out3 = MakeAndInitOutputBlock();
    ssize_t r3 = cache.CutAndInsertAfter(136, out3);
    EXPECT_EQ(r3, static_cast<ssize_t>(68));
}
