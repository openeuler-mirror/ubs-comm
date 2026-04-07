#include "brpc_iobuf_adapter.h"

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

// Named constants to avoid magic numbers (G.CNS.02)
namespace {
static const size_t IOBUF_BLOCK_CAP_100 = 100U;
static const size_t IOBUF_BLOCK_SIZE_50 = 50U;
static const uint32_t IOBUF_REF_OFFSET_10 = 10U;
static const uint32_t IOBUF_REF_LEN_20 = 20U;
static const int IOBUF_NSHARED_INIT = 1;
static const int IOBUF_NSHARED_AFTER_INC = 2;
static const uintptr_t IOBUF_BLOCK_PTR_0x1000 = 0x1000U;
} // namespace

class BrpcIOBufAdapterTest : public testing::Test {
public:
    void SetUp() override
    {
        RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

TEST_F(BrpcIOBufAdapterTest, init)
{
        // Placeholder test body.
}

// Tests for IOBuf::Block
class IOBufBlockTest : public testing::Test {
public:
    void SetUp() override
    {
        RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
    }
    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

TEST_F(IOBufBlockTest, Constructor)
{
    char data[IOBUF_BLOCK_CAP_100];
    Brpc::IOBuf::Block block(data, IOBUF_BLOCK_CAP_100);
    EXPECT_EQ(block.cap, IOBUF_BLOCK_CAP_100);
    EXPECT_EQ(block.size, 0u);
}

TEST_F(IOBufBlockTest, Full_False)
{
    char data[IOBUF_BLOCK_CAP_100];
    Brpc::IOBuf::Block block(data, IOBUF_BLOCK_CAP_100);
    EXPECT_FALSE(block.Full());
}

TEST_F(IOBufBlockTest, Full_True)
{
    char data[IOBUF_BLOCK_CAP_100];
    Brpc::IOBuf::Block block(data, IOBUF_BLOCK_CAP_100);
    block.size = IOBUF_BLOCK_CAP_100;
    EXPECT_TRUE(block.Full());
}

TEST_F(IOBufBlockTest, LeftSpace)
{
    char data[IOBUF_BLOCK_CAP_100];
    Brpc::IOBuf::Block block(data, IOBUF_BLOCK_CAP_100);
    EXPECT_EQ(block.LeftSpace(), IOBUF_BLOCK_CAP_100);

    block.size = IOBUF_BLOCK_SIZE_50;
    EXPECT_EQ(block.LeftSpace(), IOBUF_BLOCK_SIZE_50);
}

TEST_F(IOBufBlockTest, IncRefDecRef)
{
    char data[IOBUF_BLOCK_CAP_100];
    Brpc::IOBuf::Block* block = new Brpc::IOBuf::Block(data, IOBUF_BLOCK_CAP_100);
    EXPECT_EQ(block->nshared.load(), IOBUF_NSHARED_INIT);

    block->IncRef();
    EXPECT_EQ(block->nshared.load(), IOBUF_NSHARED_AFTER_INC);

    block->DecRef();
    EXPECT_EQ(block->nshared.load(), IOBUF_NSHARED_INIT);
}

TEST_F(IOBufBlockTest, SetNextGetNext)
{
    char data1[IOBUF_BLOCK_CAP_100];
    char data2[IOBUF_BLOCK_CAP_100];
    Brpc::IOBuf::Block block1(data1, IOBUF_BLOCK_CAP_100);
    Brpc::IOBuf::Block block2(data2, IOBUF_BLOCK_CAP_100);

    block1.SetNext(&block2);
    EXPECT_EQ(block1.GetNext(), &block2);
}

// Tests for IOBuf::BlockRef
class IOBufBlockRefTest : public testing::Test {
public:
    void SetUp() override {}
    void TearDown() override { GlobalMockObject::verify(); }
};

TEST_F(IOBufBlockRefTest, DefaultConstructor)
{
    Brpc::IOBuf::BlockRef ref;
    EXPECT_EQ(ref.offset, 0u);
    EXPECT_EQ(ref.length, 0u);
    EXPECT_EQ(ref.block, nullptr);
}

TEST_F(IOBufBlockRefTest, Reset)
{
    Brpc::IOBuf::BlockRef ref;
    ref.offset = IOBUF_REF_OFFSET_10;
    ref.length = IOBUF_REF_LEN_20;
    ref.block = reinterpret_cast<Brpc::IOBuf::Block*>(IOBUF_BLOCK_PTR_0x1000);

    ref.Reset();
    EXPECT_EQ(ref.offset, 0u);
    EXPECT_EQ(ref.length, 0u);
    EXPECT_EQ(ref.block, nullptr);
}

// Tests for BlockCache
class BlockCacheTest : public testing::Test {
public:
    void SetUp() override
    {
        RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
    }
    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

TEST_F(BlockCacheTest, GetCacheLen_Initial)
{
    Brpc::BlockCache cache;
    EXPECT_EQ(cache.GetCacheLen(), 0u);
}

TEST_F(BlockCacheTest, Flush_Empty)
{
    Brpc::BlockCache cache;
    cache.Flush();
    EXPECT_EQ(cache.GetCacheLen(), 0u);
}

TEST_F(BrpcIOBufAdapterTest, BlockMemFunctions_Exist)
{
    // Just verify the functions exist and can be called
    // blockmem_allocate_zero_copy and blockmem_deallocate_zero_copy
    void* ptr = Brpc::IOBuf::blockmem_allocate_zero_copy(IOBUF_BLOCK_CAP_100);
    if (ptr != nullptr) {
        Brpc::IOBuf::blockmem_deallocate_zero_copy(ptr);
    }
}

