/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "core/umq/umq_buffer_receive_queue.h"

#include <cstring>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_global_setting.h"
#include "core/umq/umq_setting.h"
#include "under_api/dl_umq_api.h"

using namespace ock::ubs;
using namespace umq;

namespace {
static int g_bufFreeCallCount = 0;

static void MockBufFreeCount(umq_buf_t *qbuf)
{
    (void)qbuf;
    g_bufFreeCallCount++;
}

/* 探测包/流控包判定用常量(与生产代码一致) */
static const uint32_t TEST_STATUS_FC_UPDATE = UMQ_FAKE_BUF_FC_UPDATE;
static const uint32_t TEST_STATUS_FC_UPDATE_BELOW = UMQ_FAKE_BUF_FC_UPDATE - 1;
static const uint32_t TEST_SN_IN_ORDER_FIRST = 0;
static const uint32_t TEST_SN_GAP_ONE = 1;
static const uint32_t TEST_SN_GAP_TWO = 2;
static const uint32_t TEST_SN_GAP_THREE = 3;
static const uint32_t TEST_SN_PROBE = UmqSetting::UMQ_PROBE_USER_DATA_ID;
static const uint32_t TEST_SN_PROBE_BELOW = UmqSetting::UMQ_PROBE_USER_DATA_ID - 1;
/* MAX_WINDOW = (MODULUS - 1) / 2, MODULUS = UMQ_SOCKET_SEQ_NUM_MAX + 1;相邻值对: MAX_WINDOW / MAX_WINDOW + 1 */
static const uint32_t TEST_SN_WINDOW_MAX = UmqSetting::UMQ_SOCKET_SEQ_NUM_MAX / 2;
static const uint32_t TEST_SN_WINDOW_OVER = TEST_SN_WINDOW_MAX + 1;
static const uint32_t TEST_SN_RAW_HUGE = 0x0FFFFFFF;
static const uint32_t TEST_RX_DEPTH_SMALL = 2;
static const uint32_t TEST_RX_DEPTH_MIN = 1;
static const uint32_t TEST_RX_DEPTH_LARGE = 128;
static const uint64_t TEST_FLUSH_BATCH = 64;   /* FlushReceiveQueueInternal 分批上限 */
static const uint32_t TEST_BUF_SLOTS = 8;      /* m_bufs 容量, 全文件唯一 */
static const uint32_t TEST_DEQUEUE_BATCH = 4;  /* DequeueBatch 出队缓冲容量/批量上限 */
static const uint32_t TEST_HEAP_CAP = 4;       /* 白盒直建 FastHeap 容量, 与 O3_QUEUE_INIT_DEPTH 值一致 */
static const uint32_t TEST_RX_DEPTH_EIGHT = 8; /* O3 深度 8 场景 */
static const uint32_t TEST_RX_DEPTH_GAP3 = 3;  /* max_ooo_gap = 3 场景 */
static const uint32_t TEST_CHAIN_LEN = 3;      /* MakeChain 链片段数 */
/* 非零哨兵: 验证 DequeueBatch 空队列时确实写入 0, 而非跳过输出参数(用 0 初始化则测不出漏写) */
static const uint32_t TEST_DEQUEUE_COUNT_SENTINEL = 99;
/* 断链起始时间哨兵: 拨到过去的非零值, 使 now - start 必然超过超时阈值 */
static const uint64_t TEST_OOO_START_TIME_PAST = 1;
} // namespace

class UmqBufferReceiveQueueTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        g_bufFreeCallCount = 0;
        m_savedRxDepth = GlobalSetting::UBS_RX_DEPTH;
        m_savedTransMode = UmqSetting::UMQ_UB_TRANS_MODE;
        m_savedO3TimeoutMs = UmqSetting::UMQ_O3_TIMEOUT_MS;
        MOCKER_CPP(::umq_buf_free).stubs().will(invoke(MockBufFreeCount));
    }

    void TearDown() override
    {
        GlobalSetting::UBS_RX_DEPTH = m_savedRxDepth;
        UmqSetting::UMQ_UB_TRANS_MODE = m_savedTransMode;
        UmqSetting::UMQ_O3_TIMEOUT_MS = m_savedO3TimeoutMs;
        errno = 0;
        GlobalMockObject::verify();
    }

    /* 构造前必须先设 statics;O3 模式 = RM_CTP */
    void SetNonO3Mode(uint32_t rxDepth)
    {
        GlobalSetting::UBS_RX_DEPTH = rxDepth;
        UmqSetting::UMQ_UB_TRANS_MODE = RC_TP;
    }

    void SetO3Mode(uint32_t rxDepth)
    {
        GlobalSetting::UBS_RX_DEPTH = rxDepth;
        UmqSetting::UMQ_UB_TRANS_MODE = RM_CTP;
    }

    /* 初始化一个栈上 qbuf: qbuf_ext 视为 umq_buf_pro_t, 设置 user_data/status */
    umq_buf_t *InitBuf(umq_buf_t *buf, uint32_t sn, uint32_t status = 0)
    {
        memset(buf, 0, sizeof(umq_buf_t));
        umq_buf_pro_t *pro = reinterpret_cast<umq_buf_pro_t *>(buf->qbuf_ext);
        memset(pro, 0, sizeof(umq_buf_pro_t));
        pro->imm.user_data = sn;
        buf->status = status;
        return buf;
    }

    /* 用 bufs 数组构造 qbuf_next 链, 返回链头 */
    umq_buf_t *MakeChain(umq_buf_t *bufs, size_t count, uint32_t sn, uint32_t status = 0)
    {
        for (size_t i = 0; i < count; ++i) {
            InitBuf(&bufs[i], sn, status);
            bufs[i].qbuf_next = (i + 1 < count) ? &bufs[i + 1] : nullptr;
        }
        return &bufs[0];
    }

    umq_buf_t m_bufs[TEST_BUF_SLOTS];

private:
    uint32_t m_savedRxDepth{0};
    ub_trans_mode m_savedTransMode{RC_TP};
    uint64_t m_savedO3TimeoutMs{0};
};

// ==================== ComputeQueueDepth ====================

TEST_F(UmqBufferReceiveQueueTest, ComputeQueueDepth_RxDepthZero_ReturnsOne)
{
    GlobalSetting::UBS_RX_DEPTH = 0;
    EXPECT_EQ(UmqBufferReceiveQueue::ComputeQueueDepth(), 1ULL);
}

TEST_F(UmqBufferReceiveQueueTest, ComputeQueueDepth_RxDepthOne_ReturnsTwo)
{
    GlobalSetting::UBS_RX_DEPTH = 1;
    /* ceil(1.2 * 1) = 2, next_pow2(2 - 1) = 2 */
    EXPECT_EQ(UmqBufferReceiveQueue::ComputeQueueDepth(), 2ULL);
}

TEST_F(UmqBufferReceiveQueueTest, ComputeQueueDepth_RxDepthFive_ReturnsEight)
{
    GlobalSetting::UBS_RX_DEPTH = 5;
    /* ceil(1.2 * 5) = 6, next_pow2(6 - 1) = 8: 非 2 的幂深度向上取整 */
    EXPECT_EQ(UmqBufferReceiveQueue::ComputeQueueDepth(), 8ULL);
}

// ==================== 构造 / O3 深度 ====================

TEST_F(UmqBufferReceiveQueueTest, Constructor_NonO3Mode_UseO3Disabled)
{
    SetNonO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    EXPECT_FALSE(queue.use_o3_);
    EXPECT_EQ(queue.o3_max_depth_, 0U);
    EXPECT_EQ(queue.receive_queue_.Capacity(), 4ULL);
}

TEST_F(UmqBufferReceiveQueueTest, Constructor_O3Mode_O3DepthEqualsRxDepth)
{
    SetO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    EXPECT_TRUE(queue.use_o3_);
    /* o3_max_depth_ = rx_depth, 不超过队列容量 */
    EXPECT_EQ(queue.o3_max_depth_, TEST_RX_DEPTH_SMALL);
    EXPECT_LE(queue.o3_max_depth_, queue.receive_queue_.Capacity());
}

TEST_F(UmqBufferReceiveQueueTest, Constructor_O3ModeLargeRx_O3DepthEqualsRxDepth)
{
    SetO3Mode(TEST_RX_DEPTH_EIGHT);
    UmqBufferReceiveQueue queue;
    EXPECT_TRUE(queue.use_o3_);
    EXPECT_EQ(queue.o3_max_depth_, TEST_RX_DEPTH_EIGHT);
    EXPECT_LE(queue.o3_max_depth_, queue.receive_queue_.Capacity());
}

// ==================== 参数校验 / shutdown ====================

TEST_F(UmqBufferReceiveQueueTest, Enqueue_NullBuffer_ReturnsError)
{
    SetNonO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    EXPECT_EQ(queue.Enqueue(nullptr), UmqBufferReceiveQueue::OpResult::ERROR);
    EXPECT_EQ(g_bufFreeCallCount, 0);
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_AfterShutdown_FreeAndReturnsError)
{
    SetNonO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    queue.Shutdown();
    umq_buf_t *buf = InitBuf(&m_bufs[0], TEST_SN_IN_ORDER_FIRST);
    EXPECT_EQ(queue.Enqueue(buf), UmqBufferReceiveQueue::OpResult::ERROR);
    EXPECT_EQ(g_bufFreeCallCount, 1);
    EXPECT_TRUE(queue.Empty());
}

TEST_F(UmqBufferReceiveQueueTest, DequeueBatch_NullBuffers_ReturnsError)
{
    SetNonO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    uint32_t dequeuedCount = 0;
    EXPECT_EQ(queue.DequeueBatch(nullptr, 1, &dequeuedCount), UmqBufferReceiveQueue::OpResult::ERROR);
}

TEST_F(UmqBufferReceiveQueueTest, DequeueBatch_ZeroMaxCount_ReturnsError)
{
    SetNonO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    umq_buf_t *out[TEST_DEQUEUE_BATCH] = {nullptr};
    uint32_t dequeuedCount = 0;
    EXPECT_EQ(queue.DequeueBatch(out, 0, &dequeuedCount), UmqBufferReceiveQueue::OpResult::ERROR);
}

TEST_F(UmqBufferReceiveQueueTest, DequeueBatch_NullDequeuedCount_ReturnsError)
{
    SetNonO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    umq_buf_t *out[TEST_DEQUEUE_BATCH] = {nullptr};
    EXPECT_EQ(queue.DequeueBatch(out, 1, nullptr), UmqBufferReceiveQueue::OpResult::ERROR);
}

TEST_F(UmqBufferReceiveQueueTest, DequeueBatch_AfterShutdown_ReturnsError)
{
    SetNonO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    queue.Shutdown();
    umq_buf_t *out[TEST_DEQUEUE_BATCH] = {nullptr};
    uint32_t dequeuedCount = 0;
    EXPECT_EQ(queue.DequeueBatch(out, 1, &dequeuedCount), UmqBufferReceiveQueue::OpResult::ERROR);
}

// ==================== 基础行为 / 队列空 ====================

TEST_F(UmqBufferReceiveQueueTest, IsInitialized_Always_ReturnsTrue)
{
    SetNonO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    EXPECT_TRUE(queue.IsInitialized());
}

TEST_F(UmqBufferReceiveQueueTest, Empty_FreshQueue_ReturnsTrue)
{
    SetNonO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    EXPECT_TRUE(queue.Empty());
}

TEST_F(UmqBufferReceiveQueueTest, DequeueBatch_EmptyQueue_ReturnsOkCountZero)
{
    SetNonO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    umq_buf_t *out[TEST_DEQUEUE_BATCH] = {nullptr};
    uint32_t dequeuedCount = TEST_DEQUEUE_COUNT_SENTINEL;
    EXPECT_EQ(queue.DequeueBatch(out, TEST_DEQUEUE_BATCH, &dequeuedCount), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(dequeuedCount, 0U);
}

TEST_F(UmqBufferReceiveQueueTest, Empty_AfterEnqueue_ReturnsFalse)
{
    SetNonO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    umq_buf_t *buf = InitBuf(&m_bufs[0], TEST_SN_IN_ORDER_FIRST);
    EXPECT_EQ(queue.Enqueue(buf), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_FALSE(queue.Empty());
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_SingleBuffer_DequeueReturnsBuffer)
{
    SetNonO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    umq_buf_t *buf = InitBuf(&m_bufs[0], TEST_SN_IN_ORDER_FIRST);
    EXPECT_EQ(queue.Enqueue(buf), UmqBufferReceiveQueue::OpResult::OK);

    umq_buf_t *out[TEST_DEQUEUE_BATCH] = {nullptr};
    uint32_t dequeuedCount = 0;
    EXPECT_EQ(queue.DequeueBatch(out, TEST_DEQUEUE_BATCH, &dequeuedCount), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(dequeuedCount, 1U);
    EXPECT_EQ(out[0], buf);
    EXPECT_EQ(out[0]->qbuf_next, nullptr);
    EXPECT_TRUE(queue.Empty());
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_MultiBuffer_DequeueKeepsFifoOrder)
{
    SetNonO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    umq_buf_t *bufs[3];
    for (uint32_t i = 0; i < 3; ++i) {
        bufs[i] = InitBuf(&m_bufs[i], i);
        EXPECT_EQ(queue.Enqueue(bufs[i]), UmqBufferReceiveQueue::OpResult::OK);
    }

    umq_buf_t *out[TEST_DEQUEUE_BATCH] = {nullptr};
    uint32_t dequeuedCount = 0;
    EXPECT_EQ(queue.DequeueBatch(out, TEST_DEQUEUE_BATCH, &dequeuedCount), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(dequeuedCount, 3U);
    for (uint32_t i = 0; i < 3; ++i) {
        EXPECT_EQ(out[i], bufs[i]);
    }
}

// ==================== 环满 / 链 ====================

TEST_F(UmqBufferReceiveQueueTest, Enqueue_RingFull_ReturnsQueueFullAndFreesBuffer)
{
    SetNonO3Mode(TEST_RX_DEPTH_MIN); /* 环容量 2 */
    UmqBufferReceiveQueue queue;
    umq_buf_t *buf1 = InitBuf(&m_bufs[0], TEST_SN_IN_ORDER_FIRST);
    umq_buf_t *buf2 = InitBuf(&m_bufs[1], TEST_SN_GAP_ONE);
    umq_buf_t *buf3 = InitBuf(&m_bufs[2], TEST_SN_GAP_TWO);
    EXPECT_EQ(queue.Enqueue(buf1), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(queue.Enqueue(buf2), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(queue.Enqueue(buf3), UmqBufferReceiveQueue::OpResult::QUEUE_FULL);
    EXPECT_EQ(static_cast<UmqBufferReceiveQueue::OpResult>(queue.pending_error_),
              UmqBufferReceiveQueue::OpResult::QUEUE_FULL);
    EXPECT_EQ(g_bufFreeCallCount, 1);
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_ChainHead_PushesAllFragments)
{
    SetNonO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    umq_buf_t *head = MakeChain(m_bufs, TEST_CHAIN_LEN, TEST_SN_IN_ORDER_FIRST);
    EXPECT_EQ(queue.Enqueue(head), UmqBufferReceiveQueue::OpResult::OK);

    umq_buf_t *out[TEST_DEQUEUE_BATCH] = {nullptr};
    uint32_t dequeuedCount = 0;
    EXPECT_EQ(queue.DequeueBatch(out, TEST_DEQUEUE_BATCH, &dequeuedCount), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(dequeuedCount, 3U);
    for (uint32_t i = 0; i < 3; ++i) {
        EXPECT_EQ(out[i], &m_bufs[i]);
        EXPECT_EQ(out[i]->qbuf_next, nullptr); /* 链已打散, 环中只存单 qbuf */
    }
    EXPECT_EQ(g_bufFreeCallCount, 0);
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_ChainHeadRingAlreadyFull_FreesWholeChain)
{
    SetNonO3Mode(TEST_RX_DEPTH_MIN); /* 环容量 2 */
    UmqBufferReceiveQueue queue;
    EXPECT_EQ(queue.Enqueue(InitBuf(&m_bufs[0], TEST_SN_IN_ORDER_FIRST)), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(queue.Enqueue(InitBuf(&m_bufs[1], TEST_SN_GAP_ONE)), UmqBufferReceiveQueue::OpResult::OK);

    /* 环已满: 链头首推失败, 释放整条链 */
    umq_buf_t *head = MakeChain(&m_bufs[2], TEST_CHAIN_LEN, TEST_SN_GAP_TWO);
    EXPECT_EQ(queue.Enqueue(head), UmqBufferReceiveQueue::OpResult::QUEUE_FULL);
    EXPECT_EQ(g_bufFreeCallCount, 3);
    EXPECT_EQ(static_cast<UmqBufferReceiveQueue::OpResult>(queue.pending_error_),
              UmqBufferReceiveQueue::OpResult::QUEUE_FULL);
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_ChainHeadRingFull_FreesRemainderAndQueueFull)
{
    SetNonO3Mode(TEST_RX_DEPTH_MIN); /* 环容量 2 */
    UmqBufferReceiveQueue queue;
    umq_buf_t *head = MakeChain(m_bufs, TEST_CHAIN_LEN, TEST_SN_IN_ORDER_FIRST);
    EXPECT_EQ(queue.Enqueue(head), UmqBufferReceiveQueue::OpResult::QUEUE_FULL);
    EXPECT_EQ(static_cast<UmqBufferReceiveQueue::OpResult>(queue.pending_error_),
              UmqBufferReceiveQueue::OpResult::QUEUE_FULL);
    EXPECT_EQ(g_bufFreeCallCount, 1); /* 链尾第 3 个被释放 */

    umq_buf_t *out[TEST_DEQUEUE_BATCH] = {nullptr};
    uint32_t dequeuedCount = 0;
    EXPECT_EQ(queue.DequeueBatch(out, TEST_DEQUEUE_BATCH, &dequeuedCount), UmqBufferReceiveQueue::OpResult::QUEUE_FULL);
}

// ==================== 快路径(FC 更新 / 探测包) ====================

TEST_F(UmqBufferReceiveQueueTest, Enqueue_StatusFcUpdate_FastPathSkipsSeqCheck)
{
    SetO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    /* status >= UMQ_FAKE_BUF_FC_UPDATE 时绕过序列号逻辑直接入环, 任意 SN 均可 */
    umq_buf_t *buf = InitBuf(&m_bufs[0], TEST_SN_RAW_HUGE, TEST_STATUS_FC_UPDATE);
    EXPECT_EQ(queue.Enqueue(buf), UmqBufferReceiveQueue::OpResult::OK);

    umq_buf_t *out[TEST_DEQUEUE_BATCH] = {nullptr};
    uint32_t dequeuedCount = 0;
    EXPECT_EQ(queue.DequeueBatch(out, TEST_DEQUEUE_BATCH, &dequeuedCount), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(dequeuedCount, 1U);
    EXPECT_EQ(out[0], buf);
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_FastPathRingFull_ReturnsQueueFullAndFrees)
{
    SetO3Mode(TEST_RX_DEPTH_MIN); /* 环容量 2 */
    UmqBufferReceiveQueue queue;
    EXPECT_EQ(queue.Enqueue(InitBuf(&m_bufs[0], TEST_SN_RAW_HUGE, TEST_STATUS_FC_UPDATE)),
              UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(queue.Enqueue(InitBuf(&m_bufs[1], TEST_SN_RAW_HUGE, TEST_STATUS_FC_UPDATE)),
              UmqBufferReceiveQueue::OpResult::OK);
    /* 快路径同样受环容量约束: 满时释放 + QUEUE_FULL */
    umq_buf_t *buf3 = InitBuf(&m_bufs[2], TEST_SN_RAW_HUGE, TEST_STATUS_FC_UPDATE);
    EXPECT_EQ(queue.Enqueue(buf3), UmqBufferReceiveQueue::OpResult::QUEUE_FULL);
    EXPECT_EQ(g_bufFreeCallCount, 1);
    EXPECT_EQ(static_cast<UmqBufferReceiveQueue::OpResult>(queue.pending_error_),
              UmqBufferReceiveQueue::OpResult::QUEUE_FULL);
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_StatusFcUpdateBelow_NormalSeqPath)
{
    SetO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    /* 相邻值: status = 191 不触发快路径, 正常按 SN 处理 */
    umq_buf_t *buf = InitBuf(&m_bufs[0], TEST_SN_IN_ORDER_FIRST, TEST_STATUS_FC_UPDATE_BELOW);
    EXPECT_EQ(queue.Enqueue(buf), UmqBufferReceiveQueue::OpResult::OK);

    umq_buf_t *out[TEST_DEQUEUE_BATCH] = {nullptr};
    uint32_t dequeuedCount = 0;
    EXPECT_EQ(queue.DequeueBatch(out, TEST_DEQUEUE_BATCH, &dequeuedCount), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(dequeuedCount, 1U);
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_ProbeUserData_FastPathSkipsSeqCheck)
{
    SetO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    umq_buf_t *buf = InitBuf(&m_bufs[0], TEST_SN_PROBE);
    EXPECT_EQ(queue.Enqueue(buf), UmqBufferReceiveQueue::OpResult::OK);

    umq_buf_t *out[TEST_DEQUEUE_BATCH] = {nullptr};
    uint32_t dequeuedCount = 0;
    EXPECT_EQ(queue.DequeueBatch(out, TEST_DEQUEUE_BATCH, &dequeuedCount), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(dequeuedCount, 1U);
}

// ==================== 序列号窗口 ====================

TEST_F(UmqBufferReceiveQueueTest, Enqueue_ProbeUserDataBelow_GapExceedsWindowDropped)
{
    SetO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    /* 相邻值: 0xFFFFFE 走正常路径, 距期望 0 超过 MAX_WINDOW → 丢包 */
    umq_buf_t *buf = InitBuf(&m_bufs[0], TEST_SN_PROBE_BELOW);
    EXPECT_EQ(queue.Enqueue(buf), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(g_bufFreeCallCount, 1);
    EXPECT_TRUE(queue.Empty());
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_GapExceedsMaxWindow_FreeAndReturnsOk)
{
    SetO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    /* gap = MAX_WINDOW + 1 → 超出防环绕窗口, 丢包 */
    umq_buf_t *buf = InitBuf(&m_bufs[0], TEST_SN_WINDOW_OVER);
    EXPECT_EQ(queue.Enqueue(buf), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(g_bufFreeCallCount, 1);
    EXPECT_TRUE(queue.Empty());
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_GapEqualsMaxWindow_MeltdownTriggered)
{
    SetO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    /* 相邻值: gap == MAX_WINDOW 不丢包, 但超过 max_ooo_gap(2) → 熔断 */
    umq_buf_t *buf = InitBuf(&m_bufs[0], TEST_SN_WINDOW_MAX);
    EXPECT_EQ(queue.Enqueue(buf), UmqBufferReceiveQueue::OpResult::MELTDOWN_TRIGGERED);
    EXPECT_EQ(g_bufFreeCallCount, 1);
    EXPECT_EQ(queue.m_ooo_start_time_ns, 0ULL);
}

// ==================== O3 乱序堆 / gap 熔断 ====================

TEST_F(UmqBufferReceiveQueueTest, Enqueue_GapWithinMaxOooGap_OooQueued)
{
    SetO3Mode(TEST_RX_DEPTH_SMALL); /* max_ooo_gap = 2 */
    UmqBufferReceiveQueue queue;
    umq_buf_t *buf = InitBuf(&m_bufs[0], TEST_SN_GAP_ONE);
    EXPECT_EQ(queue.Enqueue(buf), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_NE(queue.out_of_order_queue, nullptr);
    EXPECT_FALSE(queue.out_of_order_queue->IsEmpty());
    EXPECT_TRUE(queue.Empty());
    EXPECT_EQ(g_bufFreeCallCount, 0);
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_GapEqualsMaxOooGap_OooQueued)
{
    SetO3Mode(TEST_RX_DEPTH_SMALL); /* max_ooo_gap = 2 */
    UmqBufferReceiveQueue queue;
    /* 相邻值: gap == max_ooo_gap 不熔断, 入乱序堆 */
    umq_buf_t *buf = InitBuf(&m_bufs[0], TEST_SN_GAP_TWO);
    EXPECT_EQ(queue.Enqueue(buf), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(queue.out_of_order_queue->Size(), 1U);
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_GapExceedsMaxOooGapNoHeap_MeltdownTriggered)
{
    SetO3Mode(TEST_RX_DEPTH_SMALL); /* max_ooo_gap = 2 */
    UmqBufferReceiveQueue queue;
    /* 首包即超 gap: 堆未创建(懒创建) → 直接熔断, 不产生堆 */
    umq_buf_t *buf = InitBuf(&m_bufs[0], TEST_SN_GAP_THREE);
    EXPECT_EQ(queue.Enqueue(buf), UmqBufferReceiveQueue::OpResult::MELTDOWN_TRIGGERED);
    EXPECT_EQ(g_bufFreeCallCount, 1);
    EXPECT_EQ(queue.out_of_order_queue, nullptr);
    EXPECT_EQ(queue.m_ooo_start_time_ns, 0ULL);
    EXPECT_EQ(queue.m_expect_sn, 0U);
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_MeltdownFlushHeap_AdvancesExpectAndTriggers)
{
    SetO3Mode(TEST_RX_DEPTH_SMALL); /* max_ooo_gap = 2, 环容量 4 */
    UmqBufferReceiveQueue queue;
    umq_buf_t *buf1 = InitBuf(&m_bufs[0], TEST_SN_GAP_ONE);
    umq_buf_t *buf2 = InitBuf(&m_bufs[1], TEST_SN_GAP_TWO);
    EXPECT_EQ(queue.Enqueue(buf1), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(queue.Enqueue(buf2), UmqBufferReceiveQueue::OpResult::OK);

    /* gap = 3 触发熔断: 回灌堆 [1,2] 到环, 期望推进到 3 */
    umq_buf_t *trigger = InitBuf(&m_bufs[2], TEST_SN_GAP_THREE);
    EXPECT_EQ(queue.Enqueue(trigger), UmqBufferReceiveQueue::OpResult::MELTDOWN_TRIGGERED);
    EXPECT_EQ(g_bufFreeCallCount, 1); /* 仅触发包被释放 */
    EXPECT_EQ(queue.m_expect_sn, 3U);
    EXPECT_EQ(queue.m_ooo_start_time_ns, 0ULL);
    EXPECT_TRUE(queue.out_of_order_queue->IsEmpty());

    /* pending_error_ 已置位: 后续出队直接返回熔断错误 */
    umq_buf_t *out[TEST_DEQUEUE_BATCH] = {nullptr};
    uint32_t dequeuedCount = 0;
    EXPECT_EQ(queue.DequeueBatch(out, TEST_DEQUEUE_BATCH, &dequeuedCount),
              UmqBufferReceiveQueue::OpResult::MELTDOWN_TRIGGERED);
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_OooHeapFull_FreeAndQueueFull)
{
    SetO3Mode(TEST_RX_DEPTH_MIN); /* o3_max_depth_ = 1, 堆初始容量被强制到 4 */
    UmqBufferReceiveQueue queue;
    for (uint32_t i = 0; i < TEST_HEAP_CAP; ++i) {
        EXPECT_EQ(queue.Enqueue(InitBuf(&m_bufs[i], TEST_SN_GAP_ONE)), UmqBufferReceiveQueue::OpResult::OK);
    }
    /* 第 5 个: 堆达到容量上限 → QUEUE_FULL + 释放 */
    umq_buf_t *buf5 = InitBuf(&m_bufs[4], TEST_SN_GAP_ONE);
    EXPECT_EQ(queue.Enqueue(buf5), UmqBufferReceiveQueue::OpResult::QUEUE_FULL);
    EXPECT_EQ(static_cast<UmqBufferReceiveQueue::OpResult>(queue.pending_error_),
              UmqBufferReceiveQueue::OpResult::QUEUE_FULL);
    EXPECT_EQ(g_bufFreeCallCount, 1);
    EXPECT_EQ(queue.out_of_order_queue->Size(), 4U);
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_MeltdownFlushRingOverflow_DropRemainingQueueFull)
{
    SetO3Mode(TEST_RX_DEPTH_MIN); /* max_ooo_gap = 1, 环容量 2, 堆可容纳 4 */
    UmqBufferReceiveQueue queue;
    for (uint32_t i = 0; i < 3; ++i) {
        EXPECT_EQ(queue.Enqueue(InitBuf(&m_bufs[i], TEST_SN_GAP_ONE)), UmqBufferReceiveQueue::OpResult::OK);
    }
    /* gap = 2 熔断: 1a/1b 回灌填满环, 1c 溢出丢弃 + QUEUE_FULL */
    umq_buf_t *trigger = InitBuf(&m_bufs[3], TEST_SN_GAP_TWO);
    EXPECT_EQ(queue.Enqueue(trigger), UmqBufferReceiveQueue::OpResult::QUEUE_FULL);
    EXPECT_EQ(static_cast<UmqBufferReceiveQueue::OpResult>(queue.pending_error_),
              UmqBufferReceiveQueue::OpResult::QUEUE_FULL);
    EXPECT_EQ(g_bufFreeCallCount, 2); /* 溢出丢弃的 1c + 触发包 */

    umq_buf_t *out[TEST_DEQUEUE_BATCH] = {nullptr};
    uint32_t dequeuedCount = 0;
    EXPECT_EQ(queue.DequeueBatch(out, TEST_DEQUEUE_BATCH, &dequeuedCount), UmqBufferReceiveQueue::OpResult::QUEUE_FULL);
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_MeltdownFlushTopNull_SkipsNullEntry)
{
    SetO3Mode(TEST_RX_DEPTH_MIN); /* max_ooo_gap = 1 */
    UmqBufferReceiveQueue queue;
    /* 白盒: 空堆直接压入 nullptr(唯一元素, 不触发 comparator 比较) */
    queue.out_of_order_queue =
        new FastHeap<umq_buf_t *, UmqBufferReceiveQueue::O3QueueComparator>(TEST_HEAP_CAP, TEST_HEAP_CAP);
    queue.out_of_order_queue->Push(static_cast<umq_buf_t *>(nullptr));

    umq_buf_t *trigger = InitBuf(&m_bufs[0], TEST_SN_GAP_TWO);
    EXPECT_EQ(queue.Enqueue(trigger), UmqBufferReceiveQueue::OpResult::MELTDOWN_TRIGGERED);
    EXPECT_EQ(g_bufFreeCallCount, 1); /* 仅触发包被释放, nullptr 跳过 */
    EXPECT_TRUE(queue.out_of_order_queue->IsEmpty());
    EXPECT_EQ(queue.m_ooo_start_time_ns, 0ULL);
}

// ==================== 熔断超时 ====================

TEST_F(UmqBufferReceiveQueueTest, Enqueue_OooTimeoutExceeded_MeltdownTriggered)
{
    SetO3Mode(TEST_RX_DEPTH_MIN);
    UmqSetting::UMQ_O3_TIMEOUT_MS = 50; /* m_ooo_timeout_ns = 50ms, 构造时固化 */
    UmqBufferReceiveQueue queue;
    EXPECT_EQ(queue.Enqueue(InitBuf(&m_bufs[0], TEST_SN_GAP_ONE)), UmqBufferReceiveQueue::OpResult::OK);
    /* 白盒: 把断链起始时间拨到过去, 模拟超过 50ms 超时 */
    queue.m_ooo_start_time_ns = TEST_OOO_START_TIME_PAST;

    umq_buf_t *buf2 = InitBuf(&m_bufs[1], TEST_SN_GAP_ONE);
    EXPECT_EQ(queue.Enqueue(buf2), UmqBufferReceiveQueue::OpResult::MELTDOWN_TRIGGERED);
    EXPECT_EQ(g_bufFreeCallCount, 1); /* 触发包被释放 */
    EXPECT_EQ(static_cast<UmqBufferReceiveQueue::OpResult>(queue.pending_error_),
              UmqBufferReceiveQueue::OpResult::MELTDOWN_TRIGGERED);
    EXPECT_EQ(queue.m_ooo_start_time_ns, 0ULL);

    /* pending_error_ 已置位: 乱序堆回灌的 sn=1 暂不可出队 */
    umq_buf_t *out[TEST_DEQUEUE_BATCH] = {nullptr};
    uint32_t dequeuedCount = 0;
    EXPECT_EQ(queue.DequeueBatch(out, TEST_DEQUEUE_BATCH, &dequeuedCount),
              UmqBufferReceiveQueue::OpResult::MELTDOWN_TRIGGERED);
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_OooTimeoutNotExceeded_OooQueued)
{
    SetO3Mode(TEST_RX_DEPTH_MIN);
    UmqSetting::UMQ_O3_TIMEOUT_MS = 50;
    UmqBufferReceiveQueue queue;
    EXPECT_EQ(queue.Enqueue(InitBuf(&m_bufs[0], TEST_SN_GAP_ONE)), UmqBufferReceiveQueue::OpResult::OK);
    /* 相邻值: 断链时间未超时(刚刚开始) → 正常入乱序堆 */
    umq_buf_t *buf2 = InitBuf(&m_bufs[1], TEST_SN_GAP_ONE);
    EXPECT_EQ(queue.Enqueue(buf2), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(g_bufFreeCallCount, 0);
    EXPECT_EQ(queue.out_of_order_queue->Size(), 2U);
}

// ==================== 在序处理 / 乱序堆回灌 ====================

TEST_F(UmqBufferReceiveQueueTest, Enqueue_InOrderNoHeap_FastPathAdvancesExpect)
{
    SetO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue queue;
    umq_buf_t *buf = InitBuf(&m_bufs[0], TEST_SN_IN_ORDER_FIRST);
    EXPECT_EQ(queue.Enqueue(buf), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(queue.m_expect_sn, 1U);
    EXPECT_EQ(queue.out_of_order_queue, nullptr); /* 无乱序, 堆保持未创建 */
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_InOrderAfterOoo_DrainsHeapInOrder)
{
    SetO3Mode(TEST_RX_DEPTH_SMALL); /* max_ooo_gap = 2, 环容量 4 */
    UmqBufferReceiveQueue queue;
    umq_buf_t *buf1 = InitBuf(&m_bufs[0], TEST_SN_GAP_ONE);
    umq_buf_t *buf2 = InitBuf(&m_bufs[1], TEST_SN_GAP_TWO);
    EXPECT_EQ(queue.Enqueue(buf1), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(queue.Enqueue(buf2), UmqBufferReceiveQueue::OpResult::OK);

    umq_buf_t *inOrder = InitBuf(&m_bufs[2], TEST_SN_IN_ORDER_FIRST);
    EXPECT_EQ(queue.Enqueue(inOrder), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(queue.m_expect_sn, 3U);
    EXPECT_EQ(queue.m_ooo_start_time_ns, 0ULL);
    EXPECT_TRUE(queue.out_of_order_queue->IsEmpty());

    umq_buf_t *out[TEST_DEQUEUE_BATCH] = {nullptr};
    uint32_t dequeuedCount = 0;
    EXPECT_EQ(queue.DequeueBatch(out, TEST_DEQUEUE_BATCH, &dequeuedCount), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(dequeuedCount, 3U);
    EXPECT_EQ(out[0], inOrder);
    EXPECT_EQ(out[1], buf1);
    EXPECT_EQ(out[2], buf2);
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_InOrderWithStaleHeapEntry_FreesStale)
{
    SetO3Mode(TEST_RX_DEPTH_SMALL); /* max_ooo_gap = 2, 环容量 4 */
    UmqBufferReceiveQueue queue;
    /* 重复 SN 制造陈旧条目: 堆 [1,1], 期望推进到 2 后第二个 1 判定为在后方 */
    EXPECT_EQ(queue.Enqueue(InitBuf(&m_bufs[0], TEST_SN_GAP_ONE)), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(queue.Enqueue(InitBuf(&m_bufs[1], TEST_SN_GAP_ONE)), UmqBufferReceiveQueue::OpResult::OK);

    umq_buf_t *inOrder = InitBuf(&m_bufs[2], TEST_SN_IN_ORDER_FIRST);
    EXPECT_EQ(queue.Enqueue(inOrder), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(g_bufFreeCallCount, 1); /* 陈旧条目被释放 */
    EXPECT_EQ(queue.m_expect_sn, 2U);
    EXPECT_TRUE(queue.out_of_order_queue->IsEmpty());

    umq_buf_t *out[TEST_DEQUEUE_BATCH] = {nullptr};
    uint32_t dequeuedCount = 0;
    EXPECT_EQ(queue.DequeueBatch(out, TEST_DEQUEUE_BATCH, &dequeuedCount), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(dequeuedCount, 2U);
    EXPECT_EQ(out[0], inOrder);
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_InOrderTopAhead_BreaksAndKeepsOoo)
{
    SetO3Mode(TEST_RX_DEPTH_GAP3); /* max_ooo_gap = 3, 环容量 4 */
    UmqBufferReceiveQueue queue;
    umq_buf_t *ooo = InitBuf(&m_bufs[0], TEST_SN_GAP_THREE);
    EXPECT_EQ(queue.Enqueue(ooo), UmqBufferReceiveQueue::OpResult::OK);

    umq_buf_t *inOrder = InitBuf(&m_bufs[1], TEST_SN_IN_ORDER_FIRST);
    EXPECT_EQ(queue.Enqueue(inOrder), UmqBufferReceiveQueue::OpResult::OK);
    /* 堆顶 3 仍在期望 1 前方 → 中断回灌, 堆保留 */
    EXPECT_EQ(queue.m_expect_sn, 1U);
    EXPECT_NE(queue.m_ooo_start_time_ns, 0ULL);
    EXPECT_EQ(queue.out_of_order_queue->Size(), 1U);

    umq_buf_t *out[TEST_DEQUEUE_BATCH] = {nullptr};
    uint32_t dequeuedCount = 0;
    EXPECT_EQ(queue.DequeueBatch(out, TEST_DEQUEUE_BATCH, &dequeuedCount), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(dequeuedCount, 1U);
    EXPECT_EQ(out[0], inOrder);
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_InOrderStaleEntries_FreeStaleAndSkipNull)
{
    SetO3Mode(TEST_RX_DEPTH_MIN); /* max_ooo_gap = 1, 环容量 2 */
    UmqBufferReceiveQueue queue;
    /* 堆 [1,1,1]: 在序 0 入环后, 1 入环成功, 其余两个 1 陈旧释放 */
    for (uint32_t i = 0; i < 3; ++i) {
        EXPECT_EQ(queue.Enqueue(InitBuf(&m_bufs[i], TEST_SN_GAP_ONE)), UmqBufferReceiveQueue::OpResult::OK);
    }
    umq_buf_t *inOrder = InitBuf(&m_bufs[3], TEST_SN_IN_ORDER_FIRST);
    EXPECT_EQ(queue.Enqueue(inOrder), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(g_bufFreeCallCount, 2);
    EXPECT_EQ(queue.m_expect_sn, 2U);
    EXPECT_TRUE(queue.out_of_order_queue->IsEmpty());

    umq_buf_t *out[TEST_DEQUEUE_BATCH] = {nullptr};
    uint32_t dequeuedCount = 0;
    EXPECT_EQ(queue.DequeueBatch(out, TEST_DEQUEUE_BATCH, &dequeuedCount), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(dequeuedCount, 2U);
}

// ==================== pending_error_ 早退 ====================

TEST_F(UmqBufferReceiveQueueTest, Enqueue_AfterPendingError_EarlyReturnsPendingError)
{
    SetO3Mode(TEST_RX_DEPTH_MIN);
    UmqBufferReceiveQueue queue;
    for (uint32_t i = 0; i < TEST_HEAP_CAP; ++i) {
        EXPECT_EQ(queue.Enqueue(InitBuf(&m_bufs[i], TEST_SN_GAP_ONE)), UmqBufferReceiveQueue::OpResult::OK);
    }
    /* 堆满(容量 4)→ pending_error_ = QUEUE_FULL */
    umq_buf_t *buf5 = InitBuf(&m_bufs[4], TEST_SN_GAP_ONE);
    EXPECT_EQ(queue.Enqueue(buf5), UmqBufferReceiveQueue::OpResult::QUEUE_FULL);

    /* 之后任意 Enqueue 在熔断检查处早退, 直接返回 pending 错误 */
    umq_buf_t *buf6 = InitBuf(&m_bufs[5], TEST_SN_GAP_TWO);
    EXPECT_EQ(queue.Enqueue(buf6), UmqBufferReceiveQueue::OpResult::QUEUE_FULL);
    EXPECT_EQ(g_bufFreeCallCount, 2);                /* buf5 + buf6 */
    EXPECT_EQ(queue.out_of_order_queue->Size(), 4U); /* 堆内容不再变化 */
}

TEST_F(UmqBufferReceiveQueueTest, Enqueue_AfterPendingError_DequeueReturnsPendingError)
{
    SetNonO3Mode(TEST_RX_DEPTH_MIN);
    UmqBufferReceiveQueue queue;
    EXPECT_EQ(queue.Enqueue(InitBuf(&m_bufs[0], TEST_SN_IN_ORDER_FIRST)), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(queue.Enqueue(InitBuf(&m_bufs[1], TEST_SN_GAP_ONE)), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(queue.Enqueue(InitBuf(&m_bufs[2], TEST_SN_GAP_TWO)), UmqBufferReceiveQueue::OpResult::QUEUE_FULL);

    umq_buf_t *out[TEST_DEQUEUE_BATCH] = {nullptr};
    uint32_t dequeuedCount = 0;
    EXPECT_EQ(queue.DequeueBatch(out, TEST_DEQUEUE_BATCH, &dequeuedCount), UmqBufferReceiveQueue::OpResult::QUEUE_FULL);
}

// ==================== 析构 / 资源清理 ====================

TEST_F(UmqBufferReceiveQueueTest, Destructor_RingAndOooBuffers_FreesAll)
{
    SetO3Mode(TEST_RX_DEPTH_SMALL);
    UmqBufferReceiveQueue *queue = new UmqBufferReceiveQueue();
    EXPECT_EQ(queue->Enqueue(InitBuf(&m_bufs[0], TEST_SN_IN_ORDER_FIRST)), UmqBufferReceiveQueue::OpResult::OK);
    EXPECT_EQ(queue->Enqueue(InitBuf(&m_bufs[1], TEST_SN_GAP_ONE)), UmqBufferReceiveQueue::OpResult::OK);
    delete queue;
    EXPECT_EQ(g_bufFreeCallCount, 2); /* 环 1 个 + 乱序堆 1 个 */
}

TEST_F(UmqBufferReceiveQueueTest, Destructor_RingOverBatchSize_FreesAllBatches)
{
    SetNonO3Mode(TEST_RX_DEPTH_LARGE); /* 环容量 256 */
    UmqBufferReceiveQueue *queue = new UmqBufferReceiveQueue();
    umq_buf_t bufs[TEST_FLUSH_BATCH + 1];
    for (uint32_t i = 0; i < TEST_FLUSH_BATCH + 1; ++i) {
        EXPECT_EQ(queue->Enqueue(InitBuf(&bufs[i], i)), UmqBufferReceiveQueue::OpResult::OK);
    }
    delete queue;
    /* 64 + 1 个缓存 → 分批释放(FLUSH_BATCH = 64) */
    EXPECT_EQ(g_bufFreeCallCount, static_cast<int>(TEST_FLUSH_BATCH + 1));
}

TEST_F(UmqBufferReceiveQueueTest, Destructor_OooHeapNullEntry_SkipsNull)
{
    SetO3Mode(TEST_RX_DEPTH_MIN);
    UmqBufferReceiveQueue *queue = new UmqBufferReceiveQueue();
    EXPECT_EQ(queue->Enqueue(InitBuf(&m_bufs[0], TEST_SN_IN_ORDER_FIRST)), UmqBufferReceiveQueue::OpResult::OK);
    /* 白盒: 堆中唯一元素为 nullptr, 清理时跳过不释放 */
    queue->out_of_order_queue =
        new FastHeap<umq_buf_t *, UmqBufferReceiveQueue::O3QueueComparator>(TEST_HEAP_CAP, TEST_HEAP_CAP);
    queue->out_of_order_queue->Push(static_cast<umq_buf_t *>(nullptr));
    delete queue;
    EXPECT_EQ(g_bufFreeCallCount, 1); /* 仅环中 1 个被释放 */
}

// ==================== umq_intrusive_buf_queue.h 纯头文件直接用例 ====================

TEST_F(UmqBufferReceiveQueueTest, IntrusiveQueue_PushNull_ReturnsFalse)
{
    UmqIntrusiveBufQueue queue(2);
    EXPECT_FALSE(queue.Push(nullptr));
    EXPECT_EQ(queue.Size(), 0);
    EXPECT_TRUE(queue.Empty());
}

TEST_F(UmqBufferReceiveQueueTest, IntrusiveQueue_Capacity_ReturnsConfiguredValue)
{
    UmqIntrusiveBufQueue queue(7);
    EXPECT_EQ(queue.Capacity(), 7);
    EXPECT_TRUE(queue.Empty());
}
