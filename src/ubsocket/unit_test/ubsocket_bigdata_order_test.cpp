/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

/*
 * Unit tests for ubsocket_bigdata_order.h(纯头 inline 辅助函数,零覆盖补全).
 *
 * 覆盖范围: 7 个 inline 函数 + 1 个 struct(51 可执行行,数据源 §6 零覆盖
 * .h 表)——BigdataOrderDebugEnabled / BigdataOrderPrefix /
 * PrepareSyntheticReadQbuf / FindReadWrTail / ValidateStandaloneReadQbufChain /
 * ConfigureOrderedReadCompletions / LinkReadQbufsInOrder / PrependReadQbuf。
 *
 * 测法: 纯内存操作直测,零 mock、零 fixture、零 UMQ 调用——不链
 * ubsocket_static/mockcpp(仅 GTest::gtest_main)。与 ubsocket_bigdata_test.cpp
 * 解耦的拆分依据: research doc §9.1(纯头形态独立 target,仿
 * ubsocket_bigdata_proto_test 先例)。
 */
#include "ubsocket_bigdata_order.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace detail = ock::ubs::detail;

namespace {

/* ================================================================== */
/* §1 ubsocket_bigdata_order.h inline 辅助(零覆盖补全)                 */
/* ================================================================== */

TEST(BigdataOrderTest, BigdataOrderDebugEnabled_DefaultEnvDisabled)
{
    /* env 分支按默认值测试(静态缓存不可复位,research §3.3) */
    EXPECT_FALSE(detail::BigdataOrderDebugEnabled());
}

TEST(BigdataOrderTest, BigdataOrderPrefix_NullOrZero_ReturnsZero)
{
    EXPECT_EQ(detail::BigdataOrderPrefix(nullptr, 0), 0u);
    uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    EXPECT_EQ(detail::BigdataOrderPrefix(data, 0), 0u);
    EXPECT_EQ(detail::BigdataOrderPrefix(nullptr, 8), 0u);
}

TEST(BigdataOrderTest, BigdataOrderPrefix_LessThan8_TruncatedCopy)
{
    const uint8_t data[4] = {0x11, 0x22, 0x33, 0x44};
    EXPECT_EQ(detail::BigdataOrderPrefix(data, sizeof(data)), 0x44332211u);
}

TEST(BigdataOrderTest, BigdataOrderPrefix_Full8Bytes)
{
    const uint8_t data[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    EXPECT_EQ(detail::BigdataOrderPrefix(data, sizeof(data)), 0x0807060504030201ULL);
}

TEST(BigdataOrderTest, PrepareSyntheticReadQbuf_Null_NoOp)
{
    detail::PrepareSyntheticReadQbuf(nullptr, 100);
}

TEST(BigdataOrderTest, PrepareSyntheticReadQbuf_FillsFields)
{
    umq_buf_t qbuf = {};
    qbuf.qbuf_ext[0] = 0xDEADBEEF;
    qbuf.status = 5;
    qbuf.io_direction = UMQ_IO_TX;
    qbuf.qbuf_next = reinterpret_cast<umq_buf_t *>(0x1);

    detail::PrepareSyntheticReadQbuf(&qbuf, 300);

    EXPECT_EQ(qbuf.data_size, 300u);
    EXPECT_EQ(qbuf.total_data_size, 300u);
    EXPECT_EQ(qbuf.qbuf_next, nullptr);
    EXPECT_EQ(qbuf.status, 0u);
    EXPECT_EQ(qbuf.io_direction, UMQ_IO_RX);
    EXPECT_EQ(qbuf.qbuf_ext[0], 0u);
}

TEST(BigdataOrderTest, FindReadWrTail_NullHead_ReturnsNull)
{
    EXPECT_EQ(detail::FindReadWrTail(nullptr), nullptr);
}

TEST(BigdataOrderTest, FindReadWrTail_ZeroTotal_ReturnsNull)
{
    umq_buf_t qbuf = {};
    qbuf.total_data_size = 0;
    qbuf.data_size = 100;
    EXPECT_EQ(detail::FindReadWrTail(&qbuf), nullptr);
}

TEST(BigdataOrderTest, FindReadWrTail_SingleBuf_ReturnsHead)
{
    umq_buf_t qbuf = {};
    qbuf.total_data_size = 100;
    qbuf.data_size = 100;
    EXPECT_EQ(detail::FindReadWrTail(&qbuf), &qbuf);
}

TEST(BigdataOrderTest, FindReadWrTail_MultiBuf_ReturnsExactTail)
{
    umq_buf_t a = {}, b = {}, c = {};
    a.total_data_size = 100;
    a.data_size = 30;
    a.qbuf_next = &b;
    b.data_size = 30;
    b.qbuf_next = &c;
    c.data_size = 40;
    EXPECT_EQ(detail::FindReadWrTail(&a), &c);
}

TEST(BigdataOrderTest, FindReadWrTail_MidChainDataTooBig_ReturnsNull)
{
    umq_buf_t a = {}, b = {};
    a.total_data_size = 50;
    a.data_size = 30;
    a.qbuf_next = &b;
    b.data_size = 25; /* 剩余 20 < 25 */
    EXPECT_EQ(detail::FindReadWrTail(&a), nullptr);
}

TEST(BigdataOrderTest, FindReadWrTail_SumInsufficient_ReturnsNull)
{
    umq_buf_t a = {}, b = {};
    a.total_data_size = 100;
    a.data_size = 30;
    a.qbuf_next = &b;
    b.data_size = 30; /* 链耗尽仍未归零 */
    EXPECT_EQ(detail::FindReadWrTail(&a), nullptr);
}

TEST(BigdataOrderTest, FindReadWrTail_ZeroDataMidChain_ReturnsNull)
{
    umq_buf_t a = {}, b = {};
    a.total_data_size = 100;
    a.data_size = 30;
    a.qbuf_next = &b;
    b.data_size = 0;
    EXPECT_EQ(detail::FindReadWrTail(&a), nullptr);
}

TEST(BigdataOrderTest, ValidateStandaloneReadQbufChain_NullHead_ReturnsFalse)
{
    umq_buf_t dummy = {};
    umq_buf_t *tail = &dummy; /* 函数入口会先写 *tail_out=nullptr,必须是真实变量 */
    EXPECT_FALSE(detail::ValidateStandaloneReadQbufChain(nullptr, 100, &tail));
    EXPECT_EQ(tail, nullptr);
    EXPECT_FALSE(detail::ValidateStandaloneReadQbufChain(nullptr, 100, nullptr)); /* tail_out 可为空 */
}

TEST(BigdataOrderTest, ValidateStandaloneReadQbufChain_TotalMismatch_ReturnsFalse)
{
    umq_buf_t qbuf = {};
    qbuf.total_data_size = 99;
    qbuf.data_size = 99;
    umq_buf_t *tail = nullptr;
    EXPECT_FALSE(detail::ValidateStandaloneReadQbufChain(&qbuf, 100, &tail));
    EXPECT_EQ(tail, nullptr);
}

TEST(BigdataOrderTest, ValidateStandaloneReadQbufChain_ValidSingle_ReturnsTrue)
{
    umq_buf_t qbuf = {};
    qbuf.total_data_size = 100;
    qbuf.data_size = 100;
    umq_buf_t *tail = nullptr;
    EXPECT_TRUE(detail::ValidateStandaloneReadQbufChain(&qbuf, 100, &tail));
    EXPECT_EQ(tail, &qbuf);
}

TEST(BigdataOrderTest, ValidateStandaloneReadQbufChain_TailHasNext_ReturnsFalse)
{
    umq_buf_t qbuf = {}, extra = {};
    qbuf.total_data_size = 100;
    qbuf.data_size = 100;
    qbuf.qbuf_next = &extra;
    umq_buf_t *tail = nullptr;
    EXPECT_FALSE(detail::ValidateStandaloneReadQbufChain(&qbuf, 100, &tail));
    EXPECT_EQ(tail, nullptr);
}

TEST(BigdataOrderTest, ValidateStandaloneReadQbufChain_ValidMultiBuf_ReturnsTrue)
{
    umq_buf_t a = {}, b = {};
    a.total_data_size = 100;
    a.data_size = 40;
    a.qbuf_next = &b;
    b.data_size = 60;
    umq_buf_t *tail = nullptr;
    EXPECT_TRUE(detail::ValidateStandaloneReadQbufChain(&a, 100, &tail));
    EXPECT_EQ(tail, &b);
}

TEST(BigdataOrderTest, ConfigureOrderedReadCompletions_NullOrZero_ReturnsFalse)
{
    EXPECT_FALSE(detail::ConfigureOrderedReadCompletions(nullptr, 1));
    detail::UbsBigQbufSlot slots[2] = {};
    EXPECT_FALSE(detail::ConfigureOrderedReadCompletions(slots, 0));
    EXPECT_FALSE(detail::ConfigureOrderedReadCompletions(slots, static_cast<size_t>(UINT32_MAX) + 1));
}

TEST(BigdataOrderTest, ConfigureOrderedReadCompletions_NullPending_ReturnsFalse)
{
    detail::UbsBigQbufSlot slots[1] = {};
    slots[0].pending = nullptr;
    EXPECT_FALSE(detail::ConfigureOrderedReadCompletions(slots, 1));
}

TEST(BigdataOrderTest, ConfigureOrderedReadCompletions_NotReadOpcode_ReturnsFalse)
{
    umq_buf_t qbuf = {};
    auto *pro = reinterpret_cast<umq_buf_pro_t *>(qbuf.qbuf_ext);
    pro->opcode = UMQ_OPC_SEND_IMM;
    detail::UbsBigQbufSlot slots[1] = {};
    slots[0].pending = &qbuf;
    EXPECT_FALSE(detail::ConfigureOrderedReadCompletions(slots, 1));
}

TEST(BigdataOrderTest, ConfigureOrderedReadCompletions_Valid_EnablesPerWrCqe)
{
    umq_buf_t qbuf = {};
    auto *pro = reinterpret_cast<umq_buf_pro_t *>(qbuf.qbuf_ext);
    pro->opcode = UMQ_OPC_READ;
    detail::UbsBigQbufSlot slots[1] = {};
    slots[0].pending = &qbuf;
    EXPECT_TRUE(detail::ConfigureOrderedReadCompletions(slots, 1));
    EXPECT_EQ(pro->flag.bs.complete_enable, 1);
    EXPECT_EQ(pro->flag.bs.comp_order, 0);
    EXPECT_EQ(slots[0].completion_span, 1u);
}

TEST(BigdataOrderTest, LinkReadQbufsInOrder_NullOut_ReturnsFalse)
{
    detail::UbsBigQbufSlot slots[1] = {};
    umq_buf_t *head = nullptr;
    EXPECT_FALSE(detail::LinkReadQbufsInOrder(slots, 1, nullptr, &head));
    EXPECT_FALSE(detail::LinkReadQbufsInOrder(slots, 1, &head, nullptr));
}

TEST(BigdataOrderTest, LinkReadQbufsInOrder_EmptySlots_ReturnsFalseAndClears)
{
    umq_buf_t *head = reinterpret_cast<umq_buf_t *>(0x1);
    umq_buf_t *tail = reinterpret_cast<umq_buf_t *>(0x2);
    EXPECT_FALSE(detail::LinkReadQbufsInOrder(nullptr, 0, &head, &tail));
    EXPECT_EQ(head, nullptr);
    EXPECT_EQ(tail, nullptr);
}

TEST(BigdataOrderTest, LinkReadQbufsInOrder_InvalidChain_ReturnsFalse)
{
    umq_buf_t qbuf = {}; /* total_data_size=0 → FindReadWrTail 失败 */
    detail::UbsBigQbufSlot slots[1] = {};
    slots[0].pending = &qbuf;
    umq_buf_t *head = nullptr;
    umq_buf_t *tail = nullptr;
    EXPECT_FALSE(detail::LinkReadQbufsInOrder(slots, 1, &head, &tail));
    EXPECT_EQ(head, nullptr);
}

TEST(BigdataOrderTest, LinkReadQbufsInOrder_ValidChains_LinkedInOrder)
{
    umq_buf_t a = {}, b = {}, c = {}, d = {};
    /* slot0: a->b; slot1: c->d */
    a.total_data_size = 200;
    a.data_size = 100;
    a.qbuf_next = &b;
    b.data_size = 100;
    c.total_data_size = 200; /* c 100 + d 100,FindReadWrTail 按 total 累减找尾 */
    c.data_size = 100;
    c.qbuf_next = &d;
    d.data_size = 100;
    detail::UbsBigQbufSlot slots[2] = {};
    slots[0].pending = &a;
    slots[1].pending = &c;
    umq_buf_t *head = nullptr;
    umq_buf_t *tail = nullptr;
    EXPECT_TRUE(detail::LinkReadQbufsInOrder(slots, 2, &head, &tail));
    EXPECT_EQ(head, &a);
    EXPECT_EQ(tail, &d);
    EXPECT_EQ(b.qbuf_next, &c); /* slot0 的链尾接 slot1 链头 */
    EXPECT_EQ(d.qbuf_next, nullptr);
}

TEST(BigdataOrderTest, PrependReadQbuf_NullArgs_NoOp)
{
    umq_buf_t prefix = {};
    umq_buf_t *head = nullptr;
    umq_buf_t *tail = nullptr;
    detail::PrependReadQbuf(nullptr, &head, &tail);
    EXPECT_EQ(head, nullptr);
    detail::PrependReadQbuf(&prefix, nullptr, &tail);
    EXPECT_EQ(head, nullptr);
    detail::PrependReadQbuf(&prefix, &head, nullptr);
    EXPECT_EQ(head, nullptr);
}

TEST(BigdataOrderTest, PrependReadQbuf_EmptyList_SetsBothHeadTail)
{
    umq_buf_t prefix = {};
    umq_buf_t *head = nullptr;
    umq_buf_t *tail = nullptr;
    detail::PrependReadQbuf(&prefix, &head, &tail);
    EXPECT_EQ(head, &prefix);
    EXPECT_EQ(tail, &prefix);
    EXPECT_EQ(prefix.qbuf_next, nullptr);
}

TEST(BigdataOrderTest, PrependReadQbuf_NonEmptyList_KeepsTail)
{
    umq_buf_t prefix = {}, first = {}, last = {};
    first.qbuf_next = &last;
    umq_buf_t *head = &first;
    umq_buf_t *tail = &last;
    detail::PrependReadQbuf(&prefix, &head, &tail);
    EXPECT_EQ(head, &prefix);
    EXPECT_EQ(prefix.qbuf_next, &first);
    EXPECT_EQ(tail, &last);
}
} // namespace
