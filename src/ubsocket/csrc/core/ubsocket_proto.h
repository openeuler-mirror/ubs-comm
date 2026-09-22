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
 * UBSocket wire protocol for adaptive small/large I/O (design doc §3–4).
 *
 * There is no per-packet UbsHdr. A SEND data region is classified by imm_data
 * bit 20 (the reserved rsvd1 field of the umq imm word, never touched by the SN
 * which lives in the high 24 bits, nor by the low type/umq_id fields used by the
 * umq backend): 0 = SMALL_DATA (the region is pure payload), 1 = BIG_CTRL (the
 * region starts with a UbsCtrlHdr followed by variable-length regions:
 * UbsSeg[] / mempool_info[] / inline data). The receiver must classify by imm_data
 * bit 20 / UbsCtrlHdr.type, never by user_ctx or magic sniffing.
 *
 * This header is pure wire format (pragma packed, C-ABI) and is shared by the
 * internal C++ engine and the brpc-side bridge.
 */
#ifndef UBS_COMM_UBSOCKET_PROTO_H
#define UBS_COMM_UBSOCKET_PROTO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Fixed-length capacities (design §4.1). Both SMALL_DATA and control packets
 * share the 4064B wire budget. There is no UbsHdr now: a SMALL_DATA region is
 * pure payload, and a control region starts directly at UbsCtrlHdr.
 * ------------------------------------------------------------------------- */
#define UBS_SMALL_DATA_MAX 4064u   /* full SMALL_DATA wire length (pure payload) */
#define UBS_CTRL_BODY_MAX 4064u    /* full control packet wire length, from UbsCtrlHdr */
#define UBS_UMQ_MIN_DATA_BLOCK_SIZE 4096u /* umq_buf_alloc minimum with_data region (smallest qbuf size_class) */
#define UBS_CTRL_HDR_SIZE 24u      /* sizeof(UbsCtrlHdr) — design §4.2: read_gen added */
#define UBS_SEG_SIZE 16u           /* sizeof(UbsSeg) */
#define UBS_MEMPOOL_INFO_HDR_SIZE 24u  /* sizeof(ub_mempool_info_t) header: 5*u32 + 4B reserved (flex-array seg[] excluded) */
#define UBS_SEG_MAX 32u            /* protocol cap on nsegs / nmempool_infos */

/* Design §4.3 / §6: two-stage release grace period (ms) between headroom
 * invalidation (stage 1) and Block DecRef (stage 2). Must be >= a single
 * fallback tick so a late RDMA READ crossing the window reads gen=0. */
#define UBS_GRACE_MS_DEFAULT 100u

/* ---------------------------------------------------------------------------
 * Packet-kind discriminator (design §3.1). Lives in imm_data bit 20, which is
 * inside the umq imm word's reserved rsvd1 field (bits 20-39) — orthogonal to
 * the SN (imm.user_data, high 24 bits) and to the umq backend's low type/
 * umq_id fields. 0 = SMALL_DATA, 1 = BIG_CTRL.
 * ------------------------------------------------------------------------- */
#define UBS_IMM_BIG_CTRL_BIT (1ULL << 20) /* imm_data bit 20: 0=SMALL_DATA, 1=BIG_CTRL */

/* ---------------------------------------------------------------------------
 * Control type (design §3.2) — carried by UbsCtrlHdr.type.
 * ------------------------------------------------------------------------- */
enum UbsCtrlType {
    UBS_READ_OFFER = 1,
    UBS_READ_DONE = 2,
    UBS_READ_ABORT = 3,
};

/* ---------------------------------------------------------------------------
 * UbsCtrlHdr (24B) — design §3.3 / §4.2. Sits at the start of a BIG_CTRL data
 * region (imm_data bit 20 set); there is no preceding UbsHdr. read_gen is the
 * 8B headroom generation carried per offer batch (design §4.2): 0 = this offer
 * does not enable gen check (sliced seg / feature off / old peer fallback).
 * ------------------------------------------------------------------------- */
#pragma pack(push, 1)
struct UbsCtrlHdr {
    uint8_t type;              /* UbsCtrlType */
    uint8_t nmempool_infos;   /* count of ub_mempool_info_t, == nsegs for READ_OFFER, <= 32 */
    uint16_t nsegs;           /* count of UbsSeg, <= 32 for READ_OFFER */
    uint16_t inline_data_len; /* length of trailing inline business data */
    uint16_t total_len;       /* full control message length from UbsCtrlHdr, incl inline_data */
    uint64_t seq;             /* process-wide bigdata transaction id for pin/unpin */
    uint64_t read_gen;        /* design §4.2: per-batch generation; 0 = gen check disabled */
};
#pragma pack(pop)

/* ---------------------------------------------------------------------------
 * UbsSeg (16B) — design §3.4. Each UbsSeg has a strictly 1:1 paired mempool
 * info at the same array index: UbsSeg[i].mempool_info_idx == i.
 *
 * mempool_info_len: byte length of the paired ub_mempool_info_t entry that
 * follows in the mempool-info region. Required because ub_mempool_info_t is
 * now variable-length (it carries the urma_get_seg_ctx blob, which includes a
 * has_user_info extension tail on bonding devices). The receiver steps through
 * the mempool-info region by UbsSeg[i].mempool_info_len rather than by a fixed
 * stride. Reuses 2 bytes of the former resv[3], so UBS_SEG_SIZE stays 16.
 * ------------------------------------------------------------------------- */
#pragma pack(push, 1)
struct UbsSeg {
    uint64_t addr;
    uint32_t length;
    uint8_t mempool_info_idx; /* index of the paired ub_mempool_info_t */
    uint8_t resv;             /* reserved, sender must zero, receiver ignores */
    uint16_t mempool_info_len;/* byte length of the paired variable-length ub_mempool_info_t */
};
#pragma pack(pop)

/* ---------------------------------------------------------------------------
 * Mempool-info region — design §3.5. Carried per-UbsSeg (UbsSeg.mempool_info_idx
 * / mempool_info_len), never reused or elided even when multiple UbsSeg
 * reference the same mempool.
 *
 * Opaque to ubsocket: the entry is a umq-owned blob (uint8_t[] + len). umq
 * serializes it on get, parses/imports it on set/check, and extracts the
 * addressing fields on get_remote_fields. ubsocket only memcpy's the blob
 * in/out of the wire packet and steps by UbsSeg[i].mempool_info_len — it never
 * defines or inspects the entry struct. The internal layout is umq's
 * ub_import_mempool_info_t (private header umq_ub_private.h), pinned by umq
 * static_asserts; UBS_MEMPOOL_INFO_HDR_SIZE below must equal umq's
 * UMQ_MEMPOOL_INFO_HDR_SIZE (cross-checked in ubsocket_bigdata.cpp).
 * ------------------------------------------------------------------------- */
#ifndef MEMPOOL_UBVA_SIZE
#define MEMPOOL_UBVA_SIZE 28u
#endif

/* Fixed header byte length of one wire mempool-info entry (the part before the
 * variable-length seg tail). Must match umq's UMQ_MEMPOOL_INFO_HDR_SIZE; the
 * two are cross-checked at compile time in ubsocket_bigdata.cpp. Kept here as a
 * wire budget constant (ubsocket computes control-packet capacity from it). */
#define UBS_MEMPOOL_INFO_HDR_SIZE 24u

#ifdef __cplusplus
} /* extern "C" */

/* Compile-time wire-size guards required by design §11.1 / §11.4. */
static_assert(sizeof(struct UbsCtrlHdr) == UBS_CTRL_HDR_SIZE, "UbsCtrlHdr wire size must be 24 bytes");
static_assert(sizeof(struct UbsSeg) == UBS_SEG_SIZE, "UbsSeg wire size must be 16 bytes");

#include <cstdint>
#include <limits>

namespace ock {
namespace ubs {

/* Design §3.2 — mirror the C control-type enum into a strongly-typed C++ enum
 * so the engine can use scoped names without touching the packed wire structs.
 * Packet-kind discrimination is via imm_data bit 20 (UBS_IMM_BIG_CTRL_BIT), not
 * an enum, since it lives in the umq imm word rather than a wire header. */
enum class UbsCtrlTypeCpp : uint8_t {
    READ_OFFER = UBS_READ_OFFER,
    READ_DONE = UBS_READ_DONE,
    READ_ABORT = UBS_READ_ABORT,
};

/* ---------------------------------------------------------------------------
 * Unified wire-size helpers (design §4 / §4.1).
 *
 * A control packet's data-region layout (there is no UbsHdr) is:
 *   UbsCtrlHdr | UbsSeg[nsegs] | mempool_info[nmempool_infos] | inline
 * The 4064B budget and the 32-entry protocol cap are checked together through
 * the same helpers; no handwritten capacity magic numbers elsewhere.
 *
 * All helpers are constexpr so callers may static_assert known layouts; they
 * saturate at UINT32_MAX on overflow, which the callers treat as invalid.
 * ------------------------------------------------------------------------- */
namespace proto {

/* Design §3.1: classify a SEND data region by imm_data bit 20. The bit sits in
 * the umq imm word's reserved field, so ordinary SEND traffic (which only fills
 * the high-24-bit SN and leaves the field zero) is naturally SMALL_DATA. */
constexpr inline bool is_big_ctrl(uint64_t imm_data) noexcept
{
    return (imm_data & UBS_IMM_BIG_CTRL_BIT) != 0;
}
constexpr inline uint64_t mark_big_ctrl(uint64_t imm_data) noexcept
{
    return imm_data | UBS_IMM_BIG_CTRL_BIT;
}

constexpr inline uint64_t mark_small_data(uint64_t imm_data) noexcept
{
    return imm_data & ~UBS_IMM_BIG_CTRL_BIT;
}

constexpr inline uint32_t ctrl_total_len(uint16_t nsegs, uint32_t infos_bytes,
                                         uint16_t inline_data_len) noexcept
{
    /* Variable-length mempool-info region: infos_bytes is the runtime sum of
     * UbsSeg[i].mempool_info_len over all entries (caller accumulates it), not
     * nmempool_infos * fixed-size. nmempool_infos is no longer needed here
     * because the 1:1 invariant is checked separately and the byte length is
     * what matters for the wire budget. */
    const uint32_t body = UBS_CTRL_HDR_SIZE;
    const uint32_t segs = static_cast<uint32_t>(nsegs) * UBS_SEG_SIZE;
    const uint32_t infos = infos_bytes;
    const uint32_t inline_bytes = inline_data_len;
    /* At the protocol cap (nsegs <= 32) the sum is far below UINT32_MAX; the
     * guards below only ever trigger for intentionally malformed inputs. */
    if (segs > std::numeric_limits<uint32_t>::max() - body ||
        infos > std::numeric_limits<uint32_t>::max() - (body + segs) ||
        inline_bytes > std::numeric_limits<uint32_t>::max() - (body + segs + infos)) {
        return std::numeric_limits<uint32_t>::max();
    }
    return body + segs + infos + inline_bytes;
}

/* Control wire length == ctrl_total_len now that there is no UbsHdr prefix. */
constexpr inline uint32_t ctrl_wire_len(uint16_t nsegs, uint32_t infos_bytes,
                                       uint16_t inline_data_len) noexcept
{
    return ctrl_total_len(nsegs, infos_bytes, inline_data_len);
}

/* Whether (nsegs, nmempool_infos) is a legal READ_OFFER layout: 1:1, 1..32. */
constexpr inline bool ctrl_offer_counts_valid(uint16_t nsegs, uint16_t nmempool_infos) noexcept
{
    if (nsegs == 0 || nsegs > UBS_SEG_MAX) {
        return false;
    }
    return nsegs == nmempool_infos;
}

/* Whether a control packet fits the 4064B wire budget (design §4.1). infos_bytes
 * is the runtime-accumulated byte length of the variable-length mempool-info
 * region (sum of UbsSeg[i].mempool_info_len). */
constexpr inline bool ctrl_wire_fits(uint16_t nsegs, uint32_t infos_bytes,
                                     uint16_t inline_data_len) noexcept
{
    const uint32_t wire = ctrl_wire_len(nsegs, infos_bytes, inline_data_len);
    return wire != std::numeric_limits<uint32_t>::max() && wire <= UBS_CTRL_BODY_MAX;
}

/* Combined READ_OFFER capacity check: 1:1 counts, 1..32, and 4064B budget. */
constexpr inline bool ctrl_offer_layout_valid(uint16_t nsegs, uint16_t nmempool_infos,
                                               uint32_t infos_bytes,
                                               uint16_t inline_data_len) noexcept
{
    return ctrl_offer_counts_valid(nsegs, nmempool_infos) &&
           ctrl_wire_fits(nsegs, infos_bytes, inline_data_len);
}

/* Largest inline_data_len that fits given nsegs and the already-accumulated
 * infos_bytes. Returns 0 when even the empty layout overflows. */
constexpr inline uint16_t ctrl_max_inline_for(uint16_t nsegs, uint32_t infos_bytes) noexcept
{
    if (nsegs == 0 || nsegs > UBS_SEG_MAX) {
        return 0;
    }
    const uint32_t used = ctrl_wire_len(nsegs, infos_bytes, 0);
    if (used == std::numeric_limits<uint32_t>::max() || used > UBS_CTRL_BODY_MAX) {
        return 0;
    }
    const uint32_t room = UBS_CTRL_BODY_MAX - used;
    return room > std::numeric_limits<uint16_t>::max() ? std::numeric_limits<uint16_t>::max()
                                                       : static_cast<uint16_t>(room);
}

/* Small-data payload ceiling: the whole SMALL_DATA wire region is payload
 * (no UbsHdr), so it equals UBS_SMALL_DATA_MAX. */
constexpr inline uint32_t small_data_payload_max() noexcept
{
    return UBS_SMALL_DATA_MAX;
}

} /* namespace proto */
} /* namespace ubs */
} /* namespace ock */

#else /* __cplusplus */

/* C-side wire-size guards via the typedef trick. */
typedef char UBS_CTRL_HDR_IS_24_C[(sizeof(struct UbsCtrlHdr) == UBS_CTRL_HDR_SIZE) ? 1 : -1];
typedef char UBS_SEG_IS_16_C[(sizeof(struct UbsSeg) == UBS_SEG_SIZE) ? 1 : -1];

#endif /* __cplusplus */

#endif /* UBS_COMM_UBSOCKET_PROTO_H */
