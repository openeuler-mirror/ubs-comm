/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#define _GNU_SOURCE 1

#include <gtest/gtest.h>
#include <cstdlib>

#include "common/ubsocket_defines.h"
#include "umq_setting.h"

using namespace ock::ubs;
using namespace umq;

class UmqSettingMultiLevelTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// ==================== GetIOBufSize ====================

TEST_F(UmqSettingMultiLevelTest, GetIOBufSize_Returns4KMinusDiffByDefault)
{
    EXPECT_EQ(UmqSetting::GetIOBufSize(), static_cast<uint32_t>(SIZE_4K - IOBUF_DIFF));
    EXPECT_EQ(UmqSetting::GetIOBufSize(), 4064u);
}

TEST_F(UmqSettingMultiLevelTest, GetIOBufSizeByClass_Sc0EqualsGetIOBufSize)
{
    EXPECT_EQ(UmqSetting::GetIOBufSizeByClass(0), UmqSetting::GetIOBufSize());
}

TEST_F(UmqSettingMultiLevelTest, GetIOBufSizeByClass_Sc1LargerThanSc0)
{
    EXPECT_GT(UmqSetting::GetIOBufSizeByClass(1), UmqSetting::GetIOBufSizeByClass(0));
}

// ==================== GetSizeClassCount ====================

TEST_F(UmqSettingMultiLevelTest, GetSizeClassCount_DefaultIs2)
{
    EXPECT_EQ(UmqSetting::GetSizeClassCount(), 2u);
}

// ==================== GetRXBufCountsByClass ====================

TEST_F(UmqSettingMultiLevelTest, GetRXBufCountsByClass_EqualDistribution)
{
    uint32_t counts[UMQ_SIZE_CLASS_MAX] = {0};
    UmqSetting::GetRXBufCountsByClass(100, counts, UMQ_SIZE_CLASS_MAX);
    uint32_t total = 0;
    for (uint32_t sc = 0; sc < UmqSetting::GetSizeClassCount(); sc++) {
        total += counts[sc];
    }
    EXPECT_EQ(total, 100u);
}

TEST_F(UmqSettingMultiLevelTest, GetRXBufCountsByClass_AllInClassZero)
{
    uint32_t counts[UMQ_SIZE_CLASS_MAX] = {0};
    UmqSetting::GetRXBufCountsByClass(100, counts, UMQ_SIZE_CLASS_MAX);
    EXPECT_EQ(counts[0], 100u);
    for (uint32_t sc = 1; sc < UmqSetting::GetSizeClassCount(); sc++) {
        EXPECT_EQ(counts[sc], 0u);
    }
}

// ==================== CountRXBufByClass ====================

TEST_F(UmqSettingMultiLevelTest, CountRXBufByClass_UMQInternalBuf4096NoHeadroomIsSC0)
{
    /* UMQ-internal umq_ub_prefill_rx_buf: data_size=4096, headroom=0.
     * Must classify as SC[0] (total 4096 <= 4096), not SC[1]. */
    umq_buf_t buf = {};
    buf.data_size = 4096;
    buf.headroom_size = 0;
    umq_buf_t *ptrs[1] = {&buf};

    uint32_t counts[UMQ_SIZE_CLASS_MAX] = {0};
    UmqSetting::CountRXBufByClass(ptrs, 1, counts, UMQ_SIZE_CLASS_MAX);
    EXPECT_EQ(counts[0], 1u);
    for (uint32_t sc = 1; sc < UmqSetting::GetSizeClassCount(); sc++) {
        EXPECT_EQ(counts[sc], 0u);
    }
}

TEST_F(UmqSettingMultiLevelTest, CountRXBufByClass_UbsocketBuf4064Headroom32IsSC0)
{
    /* ubsocket PrefillRx: data_size=4064, headroom=32 (sizeof(Block)).
     * total = 4096 <= 4096 → SC[0]. */
    umq_buf_t buf = {};
    buf.data_size = 4064;
    buf.headroom_size = 32;
    umq_buf_t *ptrs[1] = {&buf};

    uint32_t counts[UMQ_SIZE_CLASS_MAX] = {0};
    UmqSetting::CountRXBufByClass(ptrs, 1, counts, UMQ_SIZE_CLASS_MAX);
    EXPECT_EQ(counts[0], 1u);
    for (uint32_t sc = 1; sc < UmqSetting::GetSizeClassCount(); sc++) {
        EXPECT_EQ(counts[sc], 0u);
    }
}

TEST_F(UmqSettingMultiLevelTest, CountRXBufByClass_SmallBufIsSC0)
{
    /* Typical 512B RPC: data_size=512, headroom=32 → SC[0]. */
    umq_buf_t buf = {};
    buf.data_size = 512;
    buf.headroom_size = 32;
    umq_buf_t *ptrs[1] = {&buf};

    uint32_t counts[UMQ_SIZE_CLASS_MAX] = {0};
    UmqSetting::CountRXBufByClass(ptrs, 1, counts, UMQ_SIZE_CLASS_MAX);
    EXPECT_EQ(counts[0], 1u);
    EXPECT_EQ(counts[1], 0u);
}

TEST_F(UmqSettingMultiLevelTest, CountRXBufByClass_LargeBufIsSC1)
{
    /* READ dest buf from DoReadOffer: data_size=65504, headroom=32.
     * total = 65536 > 4096 → SC[1]. */
    umq_buf_t buf = {};
    buf.data_size = 65504;
    buf.headroom_size = 32;
    umq_buf_t *ptrs[1] = {&buf};

    uint32_t counts[UMQ_SIZE_CLASS_MAX] = {0};
    UmqSetting::CountRXBufByClass(ptrs, 1, counts, UMQ_SIZE_CLASS_MAX);
    EXPECT_EQ(counts[0], 0u);
    EXPECT_EQ(counts[1], 1u);
}

TEST_F(UmqSettingMultiLevelTest, CountRXBufByClass_MixedBufs)
{
    /* Mixed batch: 2× UMQ-internal (4096+0), 1× ubsocket (4064+32),
     * 1× large READ dest (65504+32). */
    umq_buf_t bufs[4] = {};
    bufs[0].data_size = 4096; bufs[0].headroom_size = 0;
    bufs[1].data_size = 4096; bufs[1].headroom_size = 0;
    bufs[2].data_size = 4064; bufs[2].headroom_size = 32;
    bufs[3].data_size = 65504; bufs[3].headroom_size = 32;
    umq_buf_t *ptrs[4] = {&bufs[0], &bufs[1], &bufs[2], &bufs[3]};

    uint32_t counts[UMQ_SIZE_CLASS_MAX] = {0};
    UmqSetting::CountRXBufByClass(ptrs, 4, counts, UMQ_SIZE_CLASS_MAX);
    EXPECT_EQ(counts[0], 3u);
    EXPECT_EQ(counts[1], 1u);
}

TEST_F(UmqSettingMultiLevelTest, CountRXBufByClass_ZeroBufs)
{
    uint32_t counts[UMQ_SIZE_CLASS_MAX] = {0};
    UmqSetting::CountRXBufByClass(nullptr, 0, counts, UMQ_SIZE_CLASS_MAX);
    for (uint32_t sc = 0; sc < UmqSetting::GetSizeClassCount(); sc++) {
        EXPECT_EQ(counts[sc], 0u);
    }
}

// ==================== FloorMask ====================

TEST_F(UmqSettingMultiLevelTest, FloorMask_Returns4KMaskByDefault)
{
    EXPECT_EQ(UmqSetting::FloorMask(), SIZE_4K - MASK_DIFF);
    EXPECT_EQ(UmqSetting::FloorMask(), static_cast<uint64_t>(4095));
}

TEST_F(UmqSettingMultiLevelTest, FloorMask_IsPower2MinusOne)
{
    uint64_t mask = UmqSetting::FloorMask();
    EXPECT_EQ((mask & (mask + 1)), static_cast<uint64_t>(0));
}
