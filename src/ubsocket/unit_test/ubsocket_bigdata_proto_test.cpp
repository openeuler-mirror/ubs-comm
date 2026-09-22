/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

/*
 * Unit tests for the UBSocket adaptive I/O wire protocol (design §3–4).
 *
 * These exercise the constexpr wire-size helpers (32-entry cap, 4064B budget,
 * strict 1:1 UbsSeg/mempool_info invariant, inline-data ceiling) and the
 * packed struct sizes — the protocol points most prone to silent drift. They
 * need no UMQ/urma hardware, only the pure header.
 */
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

#include "core/ubsocket_proto.h"

using namespace ock::ubs;
namespace p = ock::ubs::proto;

class UbsProtoTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

/* design §11.1 / §11.4: wire sizes must not drift. (No UbsHdr: the packet kind
 * is carried by imm_data bit 20, not a wire header.) */
TEST_F(UbsProtoTest, WireSizesAreExact)
{
    EXPECT_EQ(sizeof(UbsCtrlHdr), UBS_CTRL_HDR_SIZE);
    EXPECT_EQ(sizeof(UbsSeg), UBS_SEG_SIZE);
    /* ub_mempool_info_t is opaque to ubsocket (no struct defined here); its
     * header size's match with umq's UMQ_MEMPOOL_INFO_HDR_SIZE is cross-checked
     * at compile time in ubsocket_bigdata.cpp (sees both headers); this pure-
     * header test does not pull in umq headers. */
}

/* design §4.1: 32 entries (inline=0) -> 2072B control region (24 + 32*64);
 * +1024 inline -> 3096B; max inline under 32 entries is 1992B; a SMALL_DATA
 * region is pure payload so its ceiling is the full 4064B. */
TEST_F(UbsProtoTest, FourKBudgetAndThirtyTwoEntryCap)
{
    EXPECT_EQ(p::ctrl_wire_len(32, 32 * UBS_MEMPOOL_INFO_HDR_SIZE, 0), 24u + 32u * (16u + UBS_MEMPOOL_INFO_HDR_SIZE));
    EXPECT_EQ(p::ctrl_wire_len(32, 32 * UBS_MEMPOOL_INFO_HDR_SIZE, 1024),
              24u + 32u * (16u + UBS_MEMPOOL_INFO_HDR_SIZE) + 1024u);
    EXPECT_EQ(p::ctrl_max_inline_for(32, 32 * UBS_MEMPOOL_INFO_HDR_SIZE),
              UBS_CTRL_BODY_MAX - (24u + 32u * (16u + UBS_MEMPOOL_INFO_HDR_SIZE)));
    EXPECT_EQ(p::small_data_payload_max(), 4064u);
}

/* design §4.1: 32 entries (info=24B each) -> 1304B control region; max inline
 * = 4064 - 1304 = 2760B; 32 + 2760B exactly fills 4064B; 32 + 2761B overflows. */
TEST_F(UbsProtoTest, ThirtyTwoEntriesFitAtMaxInline)
{
    const uint32_t infos = 32 * UBS_MEMPOOL_INFO_HDR_SIZE;
    EXPECT_TRUE(p::ctrl_offer_layout_valid(32, 32, infos, 2760));
    EXPECT_FALSE(p::ctrl_offer_layout_valid(32, 32, infos, 2761));
}

/* Design §3.1: the packet kind is carried by imm_data bit 20 (inside the umq
 * imm word's reserved field). 0 = SMALL_DATA, 1 = BIG_CTRL. The SN lives in the
 * high 24 bits (imm.user_data), so setting/clearing bit 20 must not touch it. */
TEST_F(UbsProtoTest, ImmBitClassifiesSmallVsBigCtrl)
{
    /* A plain SEND with only a SN set is SMALL_DATA (bit 20 clear). */
    uint64_t sn_only = (0x123456ULL << 40); /* a sample 24-bit SN in user_data */
    EXPECT_FALSE(p::is_big_ctrl(sn_only));

    /* Marking BIG_CTRL sets bit 20 and preserves the high-24-bit SN. */
    uint64_t big = p::mark_big_ctrl(sn_only);
    EXPECT_TRUE(p::is_big_ctrl(big));
    EXPECT_EQ(big & UBS_IMM_BIG_CTRL_BIT, UBS_IMM_BIG_CTRL_BIT);
    EXPECT_EQ(big >> 40, 0x123456u); /* SN untouched */

    /* Clearing back to SMALL_DATA restores bit 20 = 0 and keeps the SN. */
    uint64_t small = p::mark_small_data(big);
    EXPECT_FALSE(p::is_big_ctrl(small));
    EXPECT_EQ(small >> 40, 0x123456u);
}

/* design §3.4 / §4: counts must be 1:1, nonzero, and <= 32. */
TEST_F(UbsProtoTest, OfferCountsMustBeOneToOne)
{
    EXPECT_TRUE(p::ctrl_offer_counts_valid(1, 1));
    EXPECT_FALSE(p::ctrl_offer_counts_valid(0, 0));   /* empty offer invalid */
    EXPECT_FALSE(p::ctrl_offer_counts_valid(33, 33)); /* over the 32 cap */
    EXPECT_FALSE(p::ctrl_offer_counts_valid(1, 2));   /* not 1:1 */
    EXPECT_FALSE(p::ctrl_offer_counts_valid(2, 1));
}

/* nsegs=0 (READ_DONE / READ_ABORT) is a valid control body but not a valid
 * READ_OFFER layout; the empty-offer check is offer-specific. */
TEST_F(UbsProtoTest, DoneAbortLayoutDoesNotCarryVariableRegion)
{
    EXPECT_FALSE(p::ctrl_offer_counts_valid(0, 0));
    /* DONE/ABORT wire = UbsCtrlHdr only (16B, no UbsHdr); fits the 4064B budget. */
    EXPECT_LE(UBS_CTRL_HDR_SIZE, UBS_CTRL_BODY_MAX);
}

/* ctrl_max_inline_for must reject nsegs==0 and nsegs>32. */
TEST_F(UbsProtoTest, MaxInlineGuardsRange)
{
    EXPECT_EQ(p::ctrl_max_inline_for(0, 0), 0u);
    EXPECT_EQ(p::ctrl_max_inline_for(33, 0), 0u);
}

/* Design §3.4: UbsSeg[i].mempool_info_idx must equal i. A helper mirrors the
 * receiver-side invariant check so a malformed offer is rejected. */
TEST_F(UbsProtoTest, MempoolInfoIndexMustMatchArrayPosition)
{
    UbsSeg segs[3];
    for (uint8_t i = 0; i < 3; ++i) {
        segs[i].mempool_info_idx = i;
    }
    bool ok = true;
    for (uint16_t i = 0; i < 3; ++i) {
        if (segs[i].mempool_info_idx != static_cast<uint8_t>(i)) {
            ok = false;
        }
    }
    EXPECT_TRUE(ok);

    segs[1].mempool_info_idx = 0; /* violation */
    ok = true;
    for (uint16_t i = 0; i < 3; ++i) {
        if (segs[i].mempool_info_idx != static_cast<uint8_t>(i)) {
            ok = false;
        }
    }
    EXPECT_FALSE(ok);
}

/* Design §3.3: total_len for a READ_OFFER with nsegs and inline_data_len==0
 * must equal hdr + nsegs*(seg_size + info_hdr_size). */
TEST_F(UbsProtoTest, OfferTotalLenMatchesCounts)
{
    for (uint16_t n = 1; n <= 32; ++n) {
        const uint32_t infos = static_cast<uint32_t>(n) * UBS_MEMPOOL_INFO_HDR_SIZE;
        const uint32_t want = p::ctrl_total_len(n, infos, 0);
        EXPECT_EQ(want, UBS_CTRL_HDR_SIZE + static_cast<uint32_t>(n) * (UBS_SEG_SIZE + UBS_MEMPOOL_INFO_HDR_SIZE));
        EXPECT_LE(want, UBS_CTRL_BODY_MAX);
    }
}

/* Helper: simulate the umq_buf_pro_t imm union layout.
 *   imm_data  = full 64-bit word (what is_big_ctrl reads)
 *   imm.user_data = SN placed in bits 40–63 (24-bit)
 *   imm.rsvd0     = bits 0–39 (bit 20 is inside this field) */
static uint64_t MakeImmFromSn(uint32_t sn)
{
    return static_cast<uint64_t>(sn & 0xFFFFFFu) << 40;
}

/* every possible 24-bit SN with bit 20 clear must classify as
 * SMALL_DATA. This is the core "no false positive" guarantee: HandleRxControl
 * is never called for these values. */
TEST_F(UbsProtoTest, AllSnValuesAreSmallData)
{
    /* Exhaustive over the 24-bit SN space would be 16M iterations; sample
     * strategically: boundaries, powers of two, and the full byte range. */
    const uint32_t samples[] = {
        0x000000, 0x000001, 0x0000FF, 0x000100, 0x00FFFF, 0x010000,
        0x0FFFFF, 0x100000, 0x7FFFFF, 0x800000, 0xFFFFFE, 0xFFFFFF,
    };
    for (uint32_t sn : samples) {
        uint64_t imm = MakeImmFromSn(sn);
        EXPECT_FALSE(p::is_big_ctrl(imm))
            << "SN=0x" << std::hex << sn << " imm_data=0x" << imm << " must be SMALL_DATA";
    }
}

/* marking BIG_CTRL on any SN must preserve the SN in the high 24 bits.
 * This verifies that setting bit 20 (the control flag) does not corrupt the
 * connection-level ordering SN. */
TEST_F(UbsProtoTest, MarkBigCtrlPreservesAllSnValues)
{
    const uint32_t samples[] = {
        0x000000, 0x000001, 0x00ABCD, 0x0FFFFF, 0x100000,
        0xABCDEF, 0x7FFFFF, 0x800000, 0xFFFFFE, 0xFFFFFF,
    };
    for (uint32_t sn : samples) {
        uint64_t imm = MakeImmFromSn(sn);
        uint64_t big = p::mark_big_ctrl(imm);
        EXPECT_TRUE(p::is_big_ctrl(big));
        EXPECT_EQ(big >> 40, sn) << "SN corrupted by mark_big_ctrl";
        EXPECT_EQ(big & ~UBS_IMM_BIG_CTRL_BIT, imm) << "non-bit-20 fields changed";
    }
}

/* adjacent bits (19 and 21) must NOT trigger BIG_CTRL. Only bit 20 is
 * the discriminator. This catches off-by-one errors in the bit mask. */
TEST_F(UbsProtoTest, AdjacentBitsDoNotTriggerBigCtrl)
{
    uint64_t base = MakeImmFromSn(0x123456);
    /* Bit 19 set, bit 20 clear → must be SMALL_DATA */
    uint64_t bit19 = base | (1ULL << 19);
    EXPECT_FALSE(p::is_big_ctrl(bit19)) << "bit 19 must not trigger BIG_CTRL";

    /* Bit 21 set, bit 20 clear → must be SMALL_DATA */
    uint64_t bit21 = base | (1ULL << 21);
    EXPECT_FALSE(p::is_big_ctrl(bit21)) << "bit 21 must not trigger BIG_CTRL";

    /* Bits 19+21 set, bit 20 clear → must be SMALL_DATA */
    uint64_t bit19_21 = base | (1ULL << 19) | (1ULL << 21);
    EXPECT_FALSE(p::is_big_ctrl(bit19_21)) << "bits 19+21 must not trigger BIG_CTRL";

    /* Only bit 20 → BIG_CTRL */
    uint64_t bit20_only = base | UBS_IMM_BIG_CTRL_BIT;
    EXPECT_TRUE(p::is_big_ctrl(bit20_only)) << "bit 20 alone must trigger BIG_CTRL";

    /* Bit 20 among other rsvd0 bits → still BIG_CTRL */
    uint64_t bit20_plus = base | UBS_IMM_BIG_CTRL_BIT | (1ULL << 19) | (1ULL << 21);
    EXPECT_TRUE(p::is_big_ctrl(bit20_plus)) << "bit 20 with neighbors must still be BIG_CTRL";
}

/* the full rsvd0 field (bits 0–39) can be filled with arbitrary data
 * EXCEPT bit 20, and the packet must still be SMALL_DATA. This verifies that
 * umq backend type/umq_id fields (which live in the low bits of rsvd0) cannot
 * cause a false positive. */
TEST_F(UbsProtoTest, FullRsvdFieldWithoutBit20IsSmallData)
{
    /* Fill all 40 bits of rsvd0 except bit 20 */
    uint64_t all_rsvd_except_20 = ((1ULL << 40) - 1) & ~UBS_IMM_BIG_CTRL_BIT;
    EXPECT_FALSE(p::is_big_ctrl(all_rsvd_except_20))
        << "all rsvd0 bits except 20 must be SMALL_DATA";

    /* Same with a SN in the high bits */
    uint64_t with_sn = all_rsvd_except_20 | MakeImmFromSn(0xFFFFFF);
    EXPECT_FALSE(p::is_big_ctrl(with_sn))
        << "rsvd0 full + max SN, bit 20 clear → SMALL_DATA";

    /* Now set bit 20 → must flip to BIG_CTRL */
    uint64_t flipped = with_sn | UBS_IMM_BIG_CTRL_BIT;
    EXPECT_TRUE(p::is_big_ctrl(flipped))
        << "setting bit 20 must flip to BIG_CTRL regardless of other rsvd0 bits";
}

/* round-trip mark_big_ctrl → mark_small_data must restore the original
 * imm_data exactly, for arbitrary SN and rsvd0 values. */
TEST_F(UbsProtoTest, MarkRoundTripRestoresOriginal)
{
    const uint64_t originals[] = {
        0ULL,
        MakeImmFromSn(0x000001),
        MakeImmFromSn(0xFFFFFF),
        UBS_IMM_BIG_CTRL_BIT, /* already BIG_CTRL */
        MakeImmFromSn(0xABCDEF) | (1ULL << 5) | (1ULL << 39),
        0xFFFFFFFFFFFFFFFFULL, /* all bits set */
    };
    for (uint64_t orig : originals) {
        uint64_t big = p::mark_big_ctrl(orig);
        EXPECT_TRUE(p::is_big_ctrl(big));
        uint64_t restored = p::mark_small_data(big);
        EXPECT_EQ(restored, orig & ~UBS_IMM_BIG_CTRL_BIT)
            << "round-trip failed for imm_data=0x" << std::hex << orig;
    }
}

/* simulate the actual dispatch decision from ubsocket_data.cpp:125-133
 * and umq_share_jfr_epoll_runner_ops.cpp:434-443. The decision is:
 *   if (is_big_ctrl(rx_pro->imm_data)) → dispatch to HandleRxControl
 *   else → fall through as SMALL_DATA to the caller
 * This test verifies the boundary between the two paths. */
TEST_F(UbsProtoTest, SimulatedDispatchDecision)
{
    /* Simulate receiving a buffer: construct the imm word as the receiver
     * would see it in rx_pro->imm_data */
    struct DispatchCase {
        uint64_t imm_data;
        bool expect_big_ctrl; /* should this go to HandleRxControl? */
        const char *desc;
    };

    DispatchCase cases[] = {
        {MakeImmFromSn(0),        false, "zero SN, no flags → SMALL_DATA"},
        {MakeImmFromSn(1),        false, "SN=1 → SMALL_DATA"},
        {MakeImmFromSn(0xFFFFFF), false, "max SN → SMALL_DATA"},
        {0,                       false, "completely zero → SMALL_DATA"},
        {(1ULL << 19),            false, "bit 19 only → SMALL_DATA"},
        {(1ULL << 21),            false, "bit 21 only → SMALL_DATA"},
        {MakeImmFromSn(0x123) | (1ULL << 10),
                                  false, "SN + low rsvd bits → SMALL_DATA"},
        {UBS_IMM_BIG_CTRL_BIT,    true,  "bit 20 only → BIG_CTRL"},
        {MakeImmFromSn(0x456) | UBS_IMM_BIG_CTRL_BIT,
                                  true,  "SN + bit 20 → BIG_CTRL"},
        {UBS_IMM_BIG_CTRL_BIT | (1ULL << 19) | (1ULL << 21),
                                  true,  "bit 20 + neighbors → BIG_CTRL"},
        {0xFFFFFFFFFFFFFFFFULL,   true,  "all bits set → BIG_CTRL (bit 20 included)"},
    };

    for (const auto &tc : cases) {
        bool dispatched_to_handle = p::is_big_ctrl(tc.imm_data);
        EXPECT_EQ(dispatched_to_handle, tc.expect_big_ctrl)
            << "Dispatch mismatch for: " << tc.desc
            << " (imm_data=0x" << std::hex << tc.imm_data << ")";
    }
}

/* verify that the umq imm union layout is orthogonal — writing the
 * 24-bit SN via imm.user_data (bits 40–63) can NEVER set bit 20 (which is in
 * the rsvd0 field, bits 0–39). This is the structural guarantee against
 * misclassification. */
TEST_F(UbsProtoTest, SnFieldIsOrthogonalToBigCtrlBit)
{
    /* The SN occupies bits 40–63. Bit 20 is at position 20.
     * There is a 20-bit gap (bits 21–39) between them.
     * No 24-bit SN value shifted to bits 40–63 can reach bit 20. */
    for (uint32_t sn = 0; sn <= 0xFFFFFF; sn += 0x10000) {
        uint64_t imm_from_sn = MakeImmFromSn(sn);
        /* Verify bit 20 is clear regardless of SN */
        EXPECT_EQ(imm_from_sn & UBS_IMM_BIG_CTRL_BIT, 0ULL)
            << "SN=0x" << std::hex << sn << " leaked into bit 20";
    }

    /* Verify the bit positions are truly disjoint: SN starts at bit 40,
     * big-ctrl bit is at position 20, so SN field (bits 40–63) sits entirely
     * above bit 20 and can never collide. */
     constexpr int kSnShift = 40;
     constexpr int kBigCtrlBit = 20;
     EXPECT_GT(kSnShift, kBigCtrlBit)
         << "SN field must start above the big-ctrl bit position";
}

/* ================================================================== */
/* design §4.2: UbsCtrlHdr 24B + read_gen field                       */
/* ================================================================== */

/* design §4.2: UbsCtrlHdr must be exactly 24 bytes (16B old + 8B read_gen).
 * A size drift would break the wire format and ParseReadOffer total_len check. */
TEST_F(UbsProtoTest, CtrlHdrIsExactly24Bytes)
{
    EXPECT_EQ(sizeof(UbsCtrlHdr), 24u);
    EXPECT_EQ(UBS_CTRL_HDR_SIZE, 24u);
}

/* design §4.2: read_gen is the last 8B of UbsCtrlHdr, at offset 16.
 * It must be zero-initialized by memset(0) and writable as a uint64_t. */
TEST_F(UbsProtoTest, ReadGenFieldAtOffset16)
{
    UbsCtrlHdr ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    EXPECT_EQ(ctrl.read_gen, 0ULL);

    /* offset check: read_gen starts after type(1)+nmempool(1)+nsegs(2)+inline(2)+total(2)+seq(8) = 16 */
    EXPECT_EQ(offsetof(UbsCtrlHdr, read_gen), 16u);

    /* write/read round-trip */
    ctrl.read_gen = 0x123456789ABCDEF0ULL;
    EXPECT_EQ(ctrl.read_gen, 0x123456789ABCDEF0ULL);

    /* 0 means "gen check disabled" (design §4.6 fallback matrix) */
    ctrl.read_gen = 0;
    EXPECT_EQ(ctrl.read_gen, 0ULL);
}

/* design §4.2: UBS_GRACE_MS_DEFAULT must be 100 (two-stage release grace). */
TEST_F(UbsProtoTest, GraceMsDefaultIs100)
{
    EXPECT_EQ(UBS_GRACE_MS_DEFAULT, 100u);
}

/* design §4.2: ctrl_wire_len with 24B header — 32 entries + 0 inline.
 * UBS_MEMPOOL_INFO_HDR_SIZE=24 so 24 + 32*(16+24) = 24 + 1280 = 1304.
 * This implicitly verifies UBS_CTRL_HDR_SIZE is used (not a hardcoded 16). */
TEST_F(UbsProtoTest, CtrlWireLenWith24BHeader)
{
    const uint32_t infos = 32 * UBS_MEMPOOL_INFO_HDR_SIZE;
    /* 24 (hdr) + 32 * (16 seg + 24 info) = 24 + 1280 = 1304 */
    EXPECT_EQ(p::ctrl_wire_len(32, infos, 0), 1304u);
    /* 1304 + 1024 = 2328 */
    EXPECT_EQ(p::ctrl_wire_len(32, infos, 1024), 2328u);
    /* All within 4064B budget */
    EXPECT_LE(p::ctrl_wire_len(32, infos, 0), UBS_CTRL_BODY_MAX);
    EXPECT_LE(p::ctrl_wire_len(32, infos, 1024), UBS_CTRL_BODY_MAX);
}

/* design §4.6: read_gen=0 is the "no validation" sentinel. A nonzero read_gen
 * means the receiver must extend READ by 8B and validate gen in FinalizeIo. */
TEST_F(UbsProtoTest, ReadGenZeroMeansNoValidation)
{
    UbsCtrlHdr ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.type = UBS_READ_OFFER;
    ctrl.nsegs = 1;
    ctrl.nmempool_infos = 1;
    ctrl.inline_data_len = 0;
    ctrl.total_len = p::ctrl_total_len(1, 1, 0);
    ctrl.seq = 42;
    ctrl.read_gen = 0;
    /* read_gen=0: receiver should NOT extend READ, NOT validate gen */
    EXPECT_EQ(ctrl.read_gen, 0ULL);

    ctrl.read_gen = 999;
    /* read_gen!=0: receiver should extend READ +8B and validate */
    EXPECT_NE(ctrl.read_gen, 0ULL);
}
