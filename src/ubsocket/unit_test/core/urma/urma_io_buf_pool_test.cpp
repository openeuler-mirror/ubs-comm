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

#include "urma_io_buf_pool.h"

#include <gtest/gtest.h>

using namespace ock::ubs;
using namespace ock::ubs::urma;

namespace {
static urma_target_seg_t g_targetSeg;
static urma_target_seg_t *g_importResult = nullptr;
static urma_target_seg_t *g_registerResult = nullptr;
static urma_status_t g_unregisterResult = URMA_SUCCESS;
static uint32_t g_importCalls = 0;
static uint32_t g_registerCalls = 0;
static uint32_t g_unregisterCalls = 0;
static uint32_t g_unimportCalls = 0;

static urma_target_seg_t *MockImportSeg(urma_context_t *, urma_seg_t *, urma_token_t *, uint64_t,
                                        urma_import_seg_flag_t)
{
    ++g_importCalls;
    return g_importResult;
}

static urma_target_seg_t *MockRegisterSeg(urma_context_t *, urma_seg_cfg_t *)
{
    ++g_registerCalls;
    return g_registerResult;
}

static urma_status_t MockUnregisterSeg(urma_target_seg_t *)
{
    ++g_unregisterCalls;
    return g_unregisterResult;
}

static urma_status_t MockUnimportSeg(urma_target_seg_t *)
{
    ++g_unimportCalls;
    return URMA_SUCCESS;
}

static urma_status_t MockDeleteContext(urma_context_t *)
{
    return URMA_SUCCESS;
}

IdentifyNode MakeNode(uint64_t prefix, uint64_t interfaceId, uint32_t pid, const char *name)
{
    urma_eid_t eid{};
    eid.in6.subnet_prefix = prefix;
    eid.in6.interface_id = interfaceId;
    return IdentifyNode(eid, pid, name);
}
} // namespace

class UrmaIoBufPoolTest : public testing::Test {
protected:
    void SetUp() override
    {
        memset(&g_targetSeg, 0, sizeof(g_targetSeg));
        memset(rawContexts_, 0, sizeof(rawContexts_));
        g_importResult = nullptr;
        g_registerResult = nullptr;
        g_unregisterResult = URMA_SUCCESS;
        g_importCalls = 0;
        g_registerCalls = 0;
        g_unregisterCalls = 0;
        g_unimportCalls = 0;

        UrmaApi::urma_import_seg_ptr = MockImportSeg;
        UrmaApi::urma_register_seg_ptr = MockRegisterSeg;
        UrmaApi::urma_unregister_seg_ptr = MockUnregisterSeg;
        UrmaApi::urma_unimport_seg_ptr = MockUnimportSeg;
        UrmaApi::urma_delete_context_ptr = MockDeleteContext;

        urma_device_attr_t attr{};
        device_ = MakeRef<UrmaDevice>("test_dev", "/sys/test", attr);
        urma_eid_info_t eidInfo{};
        ctx_ = MakeRef<UrmaContext>(&rawContexts_[0], device_, eidInfo);
        ctx_->context_id_ = 7;
        otherCtx_ = MakeRef<UrmaContext>(&rawContexts_[1], device_, eidInfo);
        otherCtx_->context_id_ = 9;

        pool_.options_.block_size_mb = 1;
        pool_.options_.mini_block_size_kb = 4;
        pool_.options_.pool_space_size_gb = 1;
        pool_.local_start_addr_ = 0x10000000;
    }

    void TearDown() override
    {
        pool_.un_init();
        pool_.target_segments_.remote_target_segs_.clear();
        otherCtx_ = nullptr;
        ctx_ = nullptr;
        device_ = nullptr;
    }

    UrmaIoBufferPool pool_;
    urma_context_t rawContexts_[2]{};
    UrmaDevicePtr device_;
    UrmaContextPtr ctx_;
    UrmaContextPtr otherCtx_;
};

TEST_F(UrmaIoBufPoolTest, IdentifyNodeEqualityAndHashUseAllFields)
{
    auto first = MakeNode(1, 2, 3, "node");
    auto same = MakeNode(1, 2, 3, "node");
    auto different = MakeNode(1, 2, 4, "node");
    RemoteNodeHash hash;

    EXPECT_TRUE(first == same);
    EXPECT_FALSE(first == different);
    EXPECT_EQ(hash(first), hash(same));
}

TEST_F(UrmaIoBufPoolTest, SizeAccessorsReturnConfiguredValues)
{
    EXPECT_EQ(0x10000000U, pool_.get_local_start_addr());
    EXPECT_EQ(4U * SIZE_1K, pool_.get_mini_block_size());
    EXPECT_EQ(1U * SIZE_1M, pool_.get_block_size());
}

TEST_F(UrmaIoBufPoolTest, InitAllocateAndReleaseUseRealFixedAllocator)
{
    allocator::FixedAllocatorOptions options;
    options.pool_space_start_tb = 100;
    options.pool_space_size_gb = 2;
    options.block_size_mb = 2;
    options.init_block_count = 1;
    options.mini_block_size_kb = 8;
    auto contextPool = MakeRef<UrmaContextPool>();

    ASSERT_EQ(UBS_OK, pool_.init(options, contextPool));
    EXPECT_NE(0U, pool_.get_local_start_addr());

    EXPECT_EQ(nullptr, pool_.allocate(9 * SIZE_1K));
    void *miniBlock = pool_.allocate(128);
    ASSERT_NE(nullptr, miniBlock);
    pool_.deallocate(miniBlock);

    void *directBlock = pool_.direct_allocate(SIZE_4K);
    ASSERT_NE(nullptr, directBlock);
    pool_.direct_release(directBlock);
}

TEST_F(UrmaIoBufPoolTest, GetLocalTargetSegHandlesBoundsMissingContextAndHit)
{
    pool_.target_segments_.local_target_segs_[ctx_->Id()] = {&g_targetSeg, nullptr};

    EXPECT_EQ(&g_targetSeg, pool_.get_local_target_seg(ctx_, pool_.local_start_addr_));
    EXPECT_EQ(nullptr, pool_.get_local_target_seg(otherCtx_, pool_.local_start_addr_));
    EXPECT_EQ(nullptr, pool_.get_local_target_seg(ctx_, pool_.local_start_addr_ + SIZE_1G));
}

TEST_F(UrmaIoBufPoolTest, GetRemoteSegmentsCreatesReusesAndReplacesByContext)
{
    auto node = MakeNode(11, 12, 13, "peer");
    auto first = pool_.get_remote_segments(ctx_, node);
    ASSERT_NE(nullptr, first.Get());
    EXPECT_EQ(ctx_->Id(), first->ctx_id);
    EXPECT_EQ(1024U, first->remote_target_segs_.size());
    EXPECT_EQ(first.Get(), pool_.get_remote_segments(ctx_, node).Get());

    auto replacement = pool_.get_remote_segments(otherCtx_, node);
    ASSERT_NE(nullptr, replacement.Get());
    EXPECT_NE(first.Get(), replacement.Get());
    EXPECT_EQ(otherCtx_->Id(), replacement->ctx_id);
}

TEST_F(UrmaIoBufPoolTest, RemoveRemoteSegmentsHonorsContextIdentity)
{
    auto node = MakeNode(21, 22, 23, "peer");
    auto remote = pool_.get_remote_segments(ctx_, node);
    ASSERT_EQ(1U, pool_.target_segments_.remote_target_segs_.size());

    pool_.rm_remote_segments(otherCtx_, node);
    EXPECT_EQ(1U, pool_.target_segments_.remote_target_segs_.size());
    pool_.rm_remote_segments(ctx_, node);
    EXPECT_TRUE(pool_.target_segments_.remote_target_segs_.empty());
}

TEST_F(UrmaIoBufPoolTest, GetRemoteTargetSegHandlesNullUnsetBoundsAndHit)
{
    RemoteSegmentsPtr remote;
    EXPECT_EQ(nullptr, pool_.get_remote_target_seg(remote, 0));

    remote = MakeRef<RemoteSegments>();
    remote->remote_target_segs_.resize(2);
    EXPECT_EQ(nullptr, pool_.get_remote_target_seg(remote, 0x20000000));

    remote->remote_start_addr_ = 0x20000000;
    remote->remote_target_segs_[1] = &g_targetSeg;
    EXPECT_EQ(&g_targetSeg, pool_.get_remote_target_seg(remote, 0x20100000));
    EXPECT_EQ(nullptr, pool_.get_remote_target_seg(remote, 0x20200000));
}

TEST_F(UrmaIoBufPoolTest, StoreDescriptorValidatesRemoteContextAndBounds)
{
    urma_seg_t seg{};
    seg.ubva.va = 0x20000000;
    urma_target_seg_t *target = nullptr;
    RemoteSegmentsPtr remote;
    EXPECT_EQ(UBS_ERROR, pool_.store_remote_target_seg(ctx_, remote, seg, target));

    remote = MakeRef<RemoteSegments>();
    remote->ctx_id = otherCtx_->Id();
    remote->remote_start_addr_ = 0x20000000;
    remote->remote_target_segs_.resize(1);
    EXPECT_EQ(UBS_ERROR, pool_.store_remote_target_seg(ctx_, remote, seg, target));

    remote->ctx_id = ctx_->Id();
    seg.ubva.va += SIZE_1M;
    EXPECT_EQ(UBS_ERROR, pool_.store_remote_target_seg(ctx_, remote, seg, target));
}

TEST_F(UrmaIoBufPoolTest, StoreDescriptorReturnsExistingOrImportsSegment)
{
    auto remote = MakeRef<RemoteSegments>();
    remote->ctx_id = ctx_->Id();
    remote->remote_start_addr_ = 0x20000000;
    remote->remote_target_segs_.resize(1);
    urma_seg_t seg{};
    seg.ubva.va = remote->remote_start_addr_;
    urma_target_seg_t *target = nullptr;

    remote->remote_target_segs_[0] = &g_targetSeg;
    EXPECT_EQ(UBS_OK, pool_.store_remote_target_seg(ctx_, remote, seg, target));
    EXPECT_EQ(&g_targetSeg, target);
    EXPECT_EQ(0U, g_importCalls);

    remote->remote_target_segs_[0] = nullptr;
    EXPECT_EQ(UBS_ERROR, pool_.store_remote_target_seg(ctx_, remote, seg, target));
    EXPECT_EQ(1U, g_importCalls);

    g_importResult = &g_targetSeg;
    EXPECT_EQ(UBS_OK, pool_.store_remote_target_seg(ctx_, remote, seg, target));
    EXPECT_EQ(&g_targetSeg, target);
    EXPECT_EQ(&g_targetSeg, remote->remote_target_segs_[0]);
}

TEST_F(UrmaIoBufPoolTest, StoreAddressValidatesAndImportsSegment)
{
    urma_eid_t eid{};
    RemoteSegmentsPtr remote;
    EXPECT_EQ(UBS_ERROR, pool_.store_remote_target_seg(ctx_, remote, eid, 1, 0x30000000, 2));

    remote = MakeRef<RemoteSegments>();
    remote->ctx_id = otherCtx_->Id();
    remote->remote_start_addr_ = 0x30000000;
    remote->remote_target_segs_.resize(1);
    EXPECT_EQ(UBS_ERROR, pool_.store_remote_target_seg(ctx_, remote, eid, 1, 0x30000000, 2));

    remote->ctx_id = ctx_->Id();
    EXPECT_EQ(UBS_ERROR, pool_.store_remote_target_seg(ctx_, remote, eid, 1, 0x30100000, 2));
    EXPECT_EQ(UBS_ERROR, pool_.store_remote_target_seg(ctx_, remote, eid, 1, 0x30000000, 2));

    g_importResult = &g_targetSeg;
    EXPECT_EQ(UBS_OK, pool_.store_remote_target_seg(ctx_, remote, eid, 1, 0x30000000, 2));
    EXPECT_EQ(&g_targetSeg, remote->remote_target_segs_[0]);
    EXPECT_EQ(2U, g_importCalls);

    EXPECT_EQ(UBS_OK, pool_.store_remote_target_seg(ctx_, remote, eid, 1, 0x30000000, 2));
    EXPECT_EQ(2U, g_importCalls);
}

TEST_F(UrmaIoBufPoolTest, RegisterMemSkipsNullContextAndStoresSegment)
{
    auto contextPool = MakeRef<UrmaContextPool>();
    contextPool->contexts_[{"null", 0}] = nullptr;
    contextPool->contexts_[{"test", 1}] = ctx_;
    pool_.target_segments_.local_target_segs_[ctx_->Id()].resize(2);
    pool_.local_start_addr_ = 0;
    g_registerResult = &g_targetSeg;

    EXPECT_EQ(UBS_OK, pool_.register_mem(contextPool, 0x40000000, SIZE_1M));
    EXPECT_EQ(0x40000000U, pool_.local_start_addr_);
    EXPECT_EQ(1U, g_registerCalls);
    EXPECT_EQ(&g_targetSeg, pool_.target_segments_.local_target_segs_[ctx_->Id()][0]);

    g_registerResult = nullptr;
    EXPECT_EQ(UBS_ERROR, pool_.register_mem(contextPool, 0x40100000, SIZE_1M));
}

TEST_F(UrmaIoBufPoolTest, DeregisterMemCoversBoundsNullFailureAndSuccess)
{
    auto contextPool = MakeRef<UrmaContextPool>();
    pool_.target_segments_.local_target_segs_[ctx_->Id()] = {};
    EXPECT_EQ(UBS_ERROR, pool_.deregister_mem(contextPool, pool_.local_start_addr_, SIZE_1M));

    pool_.target_segments_.local_target_segs_[ctx_->Id()].resize(1);
    EXPECT_EQ(UBS_ERROR, pool_.deregister_mem(contextPool, pool_.local_start_addr_, SIZE_1M));

    pool_.target_segments_.local_target_segs_[ctx_->Id()][0] = &g_targetSeg;
    g_unregisterResult = URMA_FAIL;
    EXPECT_EQ(UBS_ERROR, pool_.deregister_mem(contextPool, pool_.local_start_addr_, SIZE_1M));

    g_unregisterResult = URMA_SUCCESS;
    EXPECT_EQ(UBS_OK, pool_.deregister_mem(contextPool, pool_.local_start_addr_, SIZE_1M));
    EXPECT_EQ(2U, g_unregisterCalls);
}

TEST_F(UrmaIoBufPoolTest, RemoteSegmentsDestructorUnimportsEveryStoredSegment)
{
    {
        auto remote = MakeRef<RemoteSegments>();
        remote->remote_target_segs_ = {nullptr, &g_targetSeg, &g_targetSeg};
    }
    EXPECT_EQ(2U, g_unimportCalls);
}
