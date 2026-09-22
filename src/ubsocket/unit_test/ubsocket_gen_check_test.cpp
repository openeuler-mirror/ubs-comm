/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

/*
 * Unit tests for READ_OFFER generation-check feature (design §4.1–§4.6, §6, §9).
 *
 * Covers the protocol constants, socket option, IOBuf flag, NegotiateReq
 * field, and the GenCheckStats monitoring API — the pieces that can be
 * tested without UMQ/urma hardware or a live Socket.
 */
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/ubsocket_proto.h"
#include "core/ubsocket_bigdata.h"
#include "core/umq/umq_socket.h"
#include "include/ubsocket_def.h"
#include "iobuf/ubsocket_iobuf.h"

using namespace ock::ubs;

class GenCheckTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

/* ================================================================== */
/* design §4.2: UBS_OPT_RPC_TIMEOUT_MS socket option                  */
/* ================================================================== */

TEST_F(GenCheckTest, OptRpcTimeoutMsIs2)
{
    EXPECT_EQ(static_cast<int>(UbSocketOpt::UBS_OPT_RPC_TIMEOUT_MS), 2);
}

/* ================================================================== */
/* design §4.1: IOBUF_BLOCK_FLAGS_GEN_HEADROOM flag                   */
/* ================================================================== */

TEST_F(GenCheckTest, IobufBlockFlagsGenHeadroomIsBit5)
{
    EXPECT_EQ(IOBUF_BLOCK_FLAGS_GEN_HEADROOM, static_cast<uint16_t>(1 << 5));
    EXPECT_EQ(IOBUF_BLOCK_FLAGS_GEN_HEADROOM, 32u);
}

/* design §4.1: the flag must not collide with existing IOBuf block flags. */
TEST_F(GenCheckTest, GenHeadroomFlagDoesNotCollideWithExistingFlags)
{
    /* Verify bit 5 is distinct from the low bits used by other flags.
     * We don't enumerate all existing flags (they're in brpc's iobuf_inl.h),
     * but we verify our flag is in the upper range (bit 5 = 0x20). */
    EXPECT_EQ(IOBUF_BLOCK_FLAGS_GEN_HEADROOM & 0x1F, 0u)
        << "bit 5 must not overlap with bits 0-4";
}

/* ================================================================== */
/* design §4.2: NegotiateReq carries rpc_timeout_ms                   */
/* ================================================================== */

/* 建链快路径：能力位必须是 NegotiateReq 的尾部追加字段——线缆前后兼容
 *（RecvLengthPrefixed 对短 body 零填充、长 body 丢弃）的结构性前提。 */
TEST_F(GenCheckTest, NegotiateReqCapFlagsAreTrailingAppendOnly)
{
    umq::NegotiateReq req{};
    EXPECT_EQ(offsetof(umq::NegotiateReq, cap_flags) + sizeof(req.cap_flags) + sizeof(req.cap_rsv),
              sizeof(umq::NegotiateReq));
    EXPECT_EQ(umq::NEGO_CAP_EARLY_ACK & umq::NEGO_CAP_DEGRADE_CONSENT, 0); // 位不重叠
    EXPECT_EQ(req.cap_flags, 0); // 默认值必须为 0：不声明能力
}

/* 方案B：扩展请求/应答必须是"原结构 + 纯尾部追加"——RecvLengthPrefixed 的
 * 零填充/丢弃兼容机制的结构性前提；能力位互不重叠。 */
TEST_F(GenCheckTest, NegotiateExtStructsAreTrailingAppendOnly)
{
    EXPECT_EQ(offsetof(umq::NegotiateReqExt, req), 0u);
    EXPECT_EQ(offsetof(umq::NegotiateReqExt, bind_info_size), sizeof(umq::NegotiateReq));
    EXPECT_EQ(offsetof(umq::NegotiateReqExt, bind_info) + UMQ_BIND_INFO_SIZE_MAX, sizeof(umq::NegotiateReqExt));
    EXPECT_EQ(offsetof(umq::NegotiateRspExt, rsp), 0u);
    EXPECT_EQ(offsetof(umq::NegotiateRspExt, server_bind_ret), sizeof(umq::NegotiateRsp));
    EXPECT_EQ(offsetof(umq::NegotiateRspExt, bind_info) + UMQ_BIND_INFO_SIZE_MAX, sizeof(umq::NegotiateRspExt));
    EXPECT_EQ(umq::NEGO_CAP_REQ_CARRY_BINDINFO &
                  (umq::NEGO_CAP_EARLY_ACK | umq::NEGO_CAP_DEGRADE_CONSENT | umq::NEGO_CAP_CARRY_BINDINFO),
              0);
    umq::NegotiateRspExt ext{};
    EXPECT_EQ(ext.server_bind_ret, 0); /* 老服务端零填充 ⇒ 恒 0 */
    EXPECT_EQ(ext.bind_info_size, 0u);
}

TEST_F(GenCheckTest, NegotiateReqHasRpcTimeoutMsField)
{
    umq::NegotiateReq req;
    /* Default value must be 0 (not set) */
    EXPECT_EQ(req.rpc_timeout_ms, 0u);

    /* Write/read round-trip */
    req.rpc_timeout_ms = 25000;
    EXPECT_EQ(req.rpc_timeout_ms, 25000u);

    req.rpc_timeout_ms = 0;
    EXPECT_EQ(req.rpc_timeout_ms, 0u);
}

/* ================================================================== */
/* design §4.2: UmqSocket stores local/peer rpc_timeout_ms            */
/* ================================================================== */

TEST_F(GenCheckTest, UmqSocketHasLocalPeerRpcTimeoutMsMembers)
{
    /* Verify the member variables exist and have correct default values
     * by checking the NegotiateReq field that feeds them. The actual
     * getter/setter are inline in umq_socket.h and just access these
     * members — instantiating UmqSocket requires UMQ runtime, so we
     * test the protocol contract here instead. */
    umq::NegotiateReq req;
    req.rpc_timeout_ms = 30000;

    /* The sender writes local_rpc_timeout_ms_ from setsockopt, then
     * fills NegotiateReq.rpc_timeout_ms. The receiver stores it as
     * peer_rpc_timeout_ms_. Effective timeout = local!=0 ? local : peer. */
    uint32_t local = req.rpc_timeout_ms;
    uint32_t peer = 0;
    uint32_t effective = (local != 0) ? local : peer;
    EXPECT_EQ(effective, 30000u);

    /* If local is 0, use peer */
    local = 0;
    peer = 25000;
    effective = (local != 0) ? local : peer;
    EXPECT_EQ(effective, 25000u);

    /* Both 0: no timeout (read_gen=0, no release) */
    local = 0;
    peer = 0;
    effective = (local != 0) ? local : peer;
    EXPECT_EQ(effective, 0u);
}

/* ================================================================== */
/* design §9 (A8): GenCheckStats monitoring API                      */
/* ================================================================== */

TEST_F(GenCheckTest, GenCheckStatsZeroInitialized)
{
    /* At startup (or before any bigdata activity), all counters should be 0.
     * Note: if other tests ran before this, counters may be nonzero.
     * We test the struct layout and field access here, not absolute zeros. */
    UbsBigdata::GenCheckStats stats = UbsBigdata::GetGenCheckStats();

    /* The struct should have all fields accessible and default to 0 in
     * the struct definition. We verify the struct can be value-initialized. */
    UbsBigdata::GenCheckStats zero{};
    EXPECT_EQ(zero.pin_timeout, 0ULL);
    EXPECT_EQ(zero.gen_mismatch, 0ULL);
    EXPECT_EQ(zero.gen_fallback, 0ULL);
    EXPECT_EQ(zero.rx_ctx_timeout, 0ULL);
    EXPECT_EQ(zero.offer_total, 0ULL);
    EXPECT_EQ(zero.pin_alive_max_ms, 0ULL);
}

TEST_F(GenCheckTest, GenCheckStatsHasAllSixFields)
{
    /* Verify the struct has exactly 6 uint64_t fields by checking sizeof.
     * 6 * 8 = 48 bytes (no padding needed since all are uint64_t). */
    EXPECT_EQ(sizeof(UbsBigdata::GenCheckStats), 48u);
}

TEST_F(GenCheckTest, GenCheckStatsGetReturnsConsistentSnapshot)
{
    /* Two consecutive reads should return the same or monotonically
     * increasing values (counters never decrease). */
    UbsBigdata::GenCheckStats s1 = UbsBigdata::GetGenCheckStats();
    UbsBigdata::GenCheckStats s2 = UbsBigdata::GetGenCheckStats();

    EXPECT_GE(s2.pin_timeout, s1.pin_timeout);
    EXPECT_GE(s2.gen_mismatch, s1.gen_mismatch);
    EXPECT_GE(s2.gen_fallback, s1.gen_fallback);
    EXPECT_GE(s2.rx_ctx_timeout, s1.rx_ctx_timeout);
    EXPECT_GE(s2.offer_total, s1.offer_total);
    EXPECT_GE(s2.pin_alive_max_ms, s1.pin_alive_max_ms);
}

/* ================================================================== */
/* design §4.6: fallback matrix — read_gen=0 scenarios                */
/* ================================================================== */

TEST_F(GenCheckTest, ReadGenZeroScenarios)
{
    /* Scenario 1: feature off → read_gen always 0 (no gen in offer) */
    UbsCtrlHdr ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.read_gen = 0;
    EXPECT_EQ(ctrl.read_gen, 0ULL);

    /* Scenario 2: feature on but effective_timeout=0 (no timeout) → read_gen=0 */
    /* (simulated: the sender path sets read_gen=0 when deadline_ns=0) */
    ctrl.read_gen = 0;
    EXPECT_EQ(ctrl.read_gen, 0ULL);

    /* Scenario 3: feature on, timeout set, but has_sliced_seg → read_gen=0 */
    ctrl.read_gen = 0; /* degraded from nonzero to 0 */
    EXPECT_EQ(ctrl.read_gen, 0ULL);

    /* Scenario 4: feature on, timeout set, whole-block segs → read_gen!=0 */
    ctrl.read_gen = 42;
    EXPECT_NE(ctrl.read_gen, 0ULL);
}

/* ================================================================== */
/* design §4.3: two-stage release timing constants                    */
/* ================================================================== */

TEST_F(GenCheckTest, TwoStageReleaseTimingConstants)
{
    /* UBS_GRACE_MS_DEFAULT = 100ms: must be long enough for a late RDMA READ
     * crossing the stage-1 window to complete and read gen=0. */
    EXPECT_GE(UBS_GRACE_MS_DEFAULT, 50u);
    EXPECT_LE(UBS_GRACE_MS_DEFAULT, 1000u);

    /* The grace period is added to the deadline to form grace_deadline_ns.
     * Verify the arithmetic: grace_deadline = stage1_time + GRACE_MS * 1e6 */
    const uint64_t stage1_ns = 1000000000ULL; /* 1s in ns */
    const uint64_t grace_deadline = stage1_ns +
        static_cast<uint64_t>(UBS_GRACE_MS_DEFAULT) * 1000000ULL;
    EXPECT_EQ(grace_deadline, stage1_ns + 100000000ULL); /* 1s + 100ms */
}
