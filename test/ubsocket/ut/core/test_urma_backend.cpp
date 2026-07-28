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
#include <cstring>

#include "urma_backend.h"

using namespace ock::ubs;
using namespace ock::ubs::urma;

static constexpr uint32_t kTestPid = 12345;
static constexpr uint64_t kTestAddr = 0x100000;

static urma_eid_t MakeTestEid(uint64_t prefix, uint64_t iface)
{
    urma_eid_t eid{};
    eid.in6.subnet_prefix = prefix;
    eid.in6.interface_id = iface;
    return eid;
}

// ==========================================================================
// IdentifyNode Tests
// ==========================================================================

TEST(IdentifyNodeTest, DefaultConstructZero)
{
    IdentifyNode node;
    EXPECT_EQ(node.eid.in6.subnet_prefix, 0);
    EXPECT_EQ(node.eid.in6.interface_id, 0);
    EXPECT_EQ(node.pid, 0);
    EXPECT_EQ(node.ns[0], '\0');
}

TEST(IdentifyNodeTest, ConstructWithValues)
{
    auto eid = MakeTestEid(0xAABB, 0xCCDD);
    IdentifyNode node(eid, kTestPid, "test_ns");
    EXPECT_EQ(node.eid.in6.subnet_prefix, 0xAABB);
    EXPECT_EQ(node.eid.in6.interface_id, 0xCCDD);
    EXPECT_EQ(node.pid, kTestPid);
    EXPECT_STREQ(node.ns, "test_ns");
}

TEST(IdentifyNodeTest, ConstructNsTruncation)
{
    auto eid = MakeTestEid(0, 0);
    char long_ns[NAME_MAX + 10];
    memset(long_ns, 'x', sizeof(long_ns));
    long_ns[NAME_MAX + 5] = '\0';

    IdentifyNode node(eid, 0, long_ns);
    EXPECT_EQ(node.ns[NAME_MAX], '\0');
    EXPECT_EQ(strlen(node.ns), static_cast<size_t>(NAME_MAX));
}

TEST(IdentifyNodeTest, OperatorEqualSameValues)
{
    auto eid = MakeTestEid(1, 2);
    IdentifyNode a(eid, 100, "ns1");
    IdentifyNode b(eid, 100, "ns1");
    EXPECT_TRUE(a == b);
}

TEST(IdentifyNodeTest, OperatorEqualDifferentEid)
{
    auto eid1 = MakeTestEid(1, 2);
    auto eid2 = MakeTestEid(1, 3);
    IdentifyNode a(eid1, 100, "ns1");
    IdentifyNode b(eid2, 100, "ns1");
    EXPECT_FALSE(a == b);
}

TEST(IdentifyNodeTest, OperatorEqualDifferentPid)
{
    auto eid = MakeTestEid(1, 2);
    IdentifyNode a(eid, 100, "ns1");
    IdentifyNode b(eid, 200, "ns1");
    EXPECT_FALSE(a == b);
}

TEST(IdentifyNodeTest, OperatorEqualDifferentNs)
{
    auto eid = MakeTestEid(1, 2);
    IdentifyNode a(eid, 100, "ns_a");
    IdentifyNode b(eid, 100, "ns_b");
    EXPECT_FALSE(a == b);
}

// ==========================================================================
// RemoteNodeHash Tests
// ==========================================================================

TEST(RemoteNodeHashTest, SameNodeSameHash)
{
    auto eid = MakeTestEid(1, 2);
    IdentifyNode a(eid, 100, "ns");
    IdentifyNode b(eid, 100, "ns");
    RemoteNodeHash hasher;
    EXPECT_EQ(hasher(a), hasher(b));
}

TEST(RemoteNodeHashTest, DifferentNodeDifferentHash)
{
    auto eid1 = MakeTestEid(1, 2);
    auto eid2 = MakeTestEid(3, 4);
    IdentifyNode a(eid1, 100, "ns1");
    IdentifyNode b(eid2, 200, "ns2");
    RemoteNodeHash hasher;
    EXPECT_NE(hasher(a), hasher(b));
}

TEST(RemoteNodeHashTest, HashUsedInUnorderedMap)
{
    auto eid1 = MakeTestEid(1, 2);
    auto eid2 = MakeTestEid(3, 4);
    IdentifyNode node1(eid1, 100, "ns1");
    IdentifyNode node2(eid2, 200, "ns2");

    std::unordered_map<IdentifyNode, int, RemoteNodeHash> m;
    m[node1] = 42;
    m[node2] = 84;

    EXPECT_EQ(m[node1], 42);
    EXPECT_EQ(m[node2], 84);

    IdentifyNode node1_copy(eid1, 100, "ns1");
    EXPECT_EQ(m[node1_copy], 42);
}

// ==========================================================================
// RemoteSegments Tests
// ==========================================================================

TEST(RemoteSegmentsTest, ConstructDefault)
{
    RemoteSegments rs;
    EXPECT_EQ(rs.remote_start_addr_, 0);
    EXPECT_TRUE(rs.remote_target_segs_.empty());
}

TEST(RemoteSegmentsTest, RefCounting)
{
    auto rs = MakeRef<RemoteSegments>();
    EXPECT_NE(rs.Get(), nullptr);
    rs->remote_start_addr_ = kTestAddr;
    EXPECT_EQ(rs->remote_start_addr_, kTestAddr);
}

// ==========================================================================
// MemPoolTargetSegments Tests
// ==========================================================================

TEST(MemPoolTargetSegmentsTest, ConstructAndInit)
{
    MemPoolTargetSegments mpts;
    EXPECT_TRUE(mpts.local_target_segs_.empty());
    EXPECT_TRUE(mpts.remote_target_segs_.empty());
}

TEST(MemPoolTargetSegmentsTest, LocalSegsResizeAndAccess)
{
    MemPoolTargetSegments mpts;
    mpts.local_target_segs_.resize(10, nullptr);
    EXPECT_EQ(mpts.local_target_segs_.size(), 10u);
    EXPECT_EQ(mpts.local_target_segs_[0], nullptr);
    EXPECT_EQ(mpts.local_target_segs_[9], nullptr);
}

TEST(MemPoolTargetSegmentsTest, RemoteSegsInsertAndFind)
{
    MemPoolTargetSegments mpts;
    auto eid = MakeTestEid(1, 2);
    IdentifyNode node(eid, kTestPid, "ns");

    auto rs = MakeRef<RemoteSegments>();
    rs->remote_start_addr_ = kTestAddr;
    rs->remote_target_segs_.resize(5, nullptr);

    mpts.remote_target_segs_.emplace(node, rs);

    auto it = mpts.remote_target_segs_.find(node);
    ASSERT_NE(it, mpts.remote_target_segs_.end());
    EXPECT_EQ(it->second->remote_start_addr_, kTestAddr);
    EXPECT_EQ(it->second->remote_target_segs_.size(), 5u);
}

// ==========================================================================
// UrmaIoBufferPool Tests
// ==========================================================================

class UrmaIoBufferPoolTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(UrmaIoBufferPoolTest, AllocateSizeExceedsLimit)
{
    UrmaIoBufferPool alloc;
    void *ptr = alloc.allocate(128 * 1024);
    EXPECT_EQ(ptr, nullptr);
}

TEST_F(UrmaIoBufferPoolTest, GetLocalTargetSegOutOfBounds)
{
    UrmaIoBufferPool alloc;
    urma_target_seg_t *seg = alloc.get_local_target_seg(0xFFFFFFFF);
    EXPECT_EQ(seg, nullptr);
}

TEST_F(UrmaIoBufferPoolTest, GetRemoteTargetSegNullRemote)
{
    UrmaIoBufferPool alloc;
    RemoteSegmentsPtr null_remote{nullptr};
    urma_target_seg_t *seg = alloc.get_remote_target_seg(null_remote, kTestAddr);
    EXPECT_EQ(seg, nullptr);
}

TEST_F(UrmaIoBufferPoolTest, GetRemoteTargetSegZeroStartAddr)
{
    UrmaIoBufferPool alloc;
    auto rs = MakeRef<RemoteSegments>();
    rs->remote_start_addr_ = 0;
    rs->remote_target_segs_.resize(10, nullptr);
    urma_target_seg_t *seg = alloc.get_remote_target_seg(rs, kTestAddr);
    EXPECT_EQ(seg, nullptr);
}
