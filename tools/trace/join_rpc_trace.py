#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Join the two endpoints of the four-point RPC link-latency trace, and
optionally align ubsocket per-packet delivery records onto RPCs through the
byte-offset bridge (phase 3: per-segment latency attribution).

brpc rpc_trace_<pid>.log -- one TSV row per timestamped point:
    6-col (current):  point cid port fd ts_ns byte_cursor
    5-col:            point cid port ts_ns byte_cursor   (fd=-1; no pkt join)
    4-col (legacy):   point cid port ts_ns               (fd=-1, bc=0)

    point=1 (T1) client Socket::Write(req) entry       [client clock]
    point=2 (T2) client rsp wire-arrival (received_us)  [client clock]
    point=3 (T3) server req wire-arrival (received_us)  [server clock]
    point=4 (T4) server Socket::Write(rsp) entry        [server clock]

ubsocket ubs_pkt_trace_<pid>.log -- two row kinds share one file:
    P row: fd<TAB>sn<TAB>byte_cursor<TAB>ts_ns         [endpoint-local clock]
           one row per delivered segment (ubs_poll delivery stamp)
    S row: S<TAB>stage_id<TAB>fd<TAB>first_sn<TAB>ts_ns   (phase 4 stage event)
           entry timestamp of one pipeline stage along the bigdata TX/RX path

Byte-offset bridge: on a UB-native connection, brpc's byte_cursor (RX bytes
consumed at message cut-out) and ubsocket's byte_cursor (cumulative bytes
delivered by ubs_poll) count the same byte stream, both starting at 0 at
connection birth. So on the same fd every segment belongs to the first
message whose end cursor reaches it -- mapping cid <-> SN set with no wire
change. Approximations (fine for a diagnostic): a segment spanning two
messages is attributed to the later one; fd reuse across sequential
connections in one log can confuse the bridge.

Stage events (phase 4): keyed by (fd, first_sn), first_sn is PER-OFFER (all
fragments of one offer share its first_sn in imm.user_data, so P rows of one
offer carry the same sn = first_sn). An RPC may span multiple offers; join
matches by membership: first_sn ∈ rpc.sn_set. Segment duration =
ts(next) - ts(cur) within one (fd, first_sn) group sorted by ts; the group's
last RX event is optionally closed by the P-row delivery stamp (the
"delivered" endpoint). Cross-log matching rides on the fd bridge brpc
already records: T1/T2 carry the client fd, T3/T4 the server fd. req and rsp
SN spaces are independent (each endpoint TX-counts its own) and never
cross-matched. Same-node same-clock bonus: ts(ReadCqe) - ts(UmqPostRead) is
a per-offer RDMA READ fabric measurement with no clock-skew concern.

Per-RPC numbers (us; server clock = S, client clock = C):
    e2e        = T2 - T1                        full RPC latency         (C)
    server_prc = T4 - T3                        parse+biz+serialize      (S)
    link_time  = e2e - server_prc               communication stack (skew-free)
    req_tail   = T3 - ts(last req segment)      delivery -> msg cut-out  (S)
    rsp_tail   = T2 - ts(last rsp segment)      delivery -> msg cut-out  (C)
    wire_resid = link_time - req_tail - rsp_tail   wire+queue residual
    unattr     = link_time - Σ(stage segments)     phase-4 residual:
                 cross-node OFFER SEND legs + brpc-internal gaps

Usage:
    join_rpc_trace.py --client rpc_trace_c.log --server rpc_trace_s.log \
        [--client-pkt ubs_pkt_trace_c.log] [--server-pkt ubs_pkt_trace_s.log] \
        [--csv out.csv] [--top N]
"""
import argparse
import sys
import bisect
from collections import defaultdict

POINT_T1, POINT_T2, POINT_T3, POINT_T4 = 1, 2, 3, 4

# Phase-4 stage ids (mirror of UbsStageId in ubs_pkt_trace.h). 0 is the
# synthetic "delivered" endpoint we append to RX groups from P-row stamps.
STAGE_DELIVERED = 0
STAGE_NAMES = {
    STAGE_DELIVERED: "delivered",
    1: "TrySenderPost", 2: "FlushPendingOffer", 3: "UmqPostSend",
    4: "ReadCqe", 5: "HandleRxCtrl", 6: "DoReadOffer",
    7: "UmqPostRead", 8: "FinalizeIo", 9: "DeliverToRxQ",
    10: "SendSimpleCtrl", 11: "RxCqeData",
}
STAGE_UMQ_POST_READ, STAGE_READ_CQE = 7, 4
# Canonical display order for the aggregate table: event-pair segments are
# keyed (from_stage, to_stage); sort by this rank then by name.
# 11 (RxCqeData, inline-data CQE dispatch) ranks before 5 (HandleRxCtrl):
# both are "first RX-side sight" events, mutually exclusive per message kind
# (SMALL_DATA vs READ_OFFER).
_STAGE_ORDER = {sid: i for i, sid in enumerate(
    [1, 2, 3, 11, 5, 6, 7, 4, 8, 9, 10, STAGE_DELIVERED])}

# Direction split of stage ids: 1-3 are the TX pipeline (stamped in the
# sender's log), 4-11 the RX pipeline (receiver's log). Exact S rows are
# keyed (fd, first_sn), and on one connection both directions' SN spaces
# count up from 1, so the same key collides across directions in one log
# (e.g. a client req-TX SN equals a server rsp-TX SN carried in
# imm.user_data). Every group must accept only its own direction's exact
# events -- otherwise it absorbs an opposite-direction event and fabricates
# a bogus cross-direction segment (UmqPostSend->RxCqeData spanning a whole
# round trip, RxCqeData->TrySenderPost, delivered->TrySenderPost, ...).
# Range events are direction-gated separately (TX only, see below).
_TX_STAGES = frozenset((1, 2, 3))
_RX_STAGES = frozenset((4, 5, 6, 7, 8, 9, 10, 11))


def load_brpc(path, want_points):
    """Return ({cid: {point: (ts_ns, fd, byte_cursor)}}, n_bad).

    Keeps the LAST occurrence of each (cid, point) -- a cid is unique per RPC
    on a connection, and a repeated point for the same cid only happens if the
    ring overflowed and re-emitted, in which case the newest is authoritative.
    """
    table = defaultdict(dict)
    n_bad = 0
    with open(path, "r") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) not in (4, 5, 6):
                n_bad += 1
                continue
            try:
                point = int(parts[0])
                cid = int(parts[1])
                if len(parts) == 6:
                    fd, ts, bc = int(parts[3]), int(parts[4]), int(parts[5])
                elif len(parts) == 5:
                    fd, ts, bc = -1, int(parts[3]), int(parts[4])
                else:
                    fd, ts, bc = -1, int(parts[3]), 0
            except ValueError:
                n_bad += 1
                continue
            if point in want_points:
                table[cid][point] = (ts, fd, bc)
    return table, n_bad


def load_pkt(path):
    """Parse one ubs_pkt_trace log (P rows + phase-4 S rows).

    Returns (per_fd, stage_events, range_events, deliver_ts, n_bad):
      per_fd:       {fd: [(byte_cursor, sn, ts_ns)]} sorted by byte_cursor
      stage_events: {(fd, first_sn): [(ts_ns, stage_id)]} from S rows with
                    sn_count == 0 (exact single-SN events)
      range_events: {fd: [(first_sn, end_sn, stage_id, ts_ns)]} from S rows
                    with sn_count > 0 (batch-covering UmqPostSend; end_sn =
                    first_sn + sn_count - 1), sorted by first_sn. Same-fd
                    batch SNs are allocated monotonically, so end_sn is
                    monotonic too -- both bisectable.
      deliver_ts:   {(fd, sn): [ts_ns]} P-row delivery stamps by segment SN
                    (bigdata fragments of one offer all carry sn = first_sn)
    """
    per_fd = defaultdict(list)
    stage_events = defaultdict(list)
    range_events = defaultdict(list)
    deliver_ts = defaultdict(list)
    n_bad = 0
    with open(path, "r") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if parts[0] == "S":
                if len(parts) not in (5, 6):
                    n_bad += 1
                    continue
                try:
                    stage_id = int(parts[1])
                    fd = int(parts[2])
                    first_sn = int(parts[3])
                    ts = int(parts[4])
                    sn_count = int(parts[5]) if len(parts) == 6 else 0
                except ValueError:
                    n_bad += 1
                    continue
                if sn_count > 0:
                    range_events[fd].append((first_sn, first_sn + sn_count - 1,
                                             stage_id, ts))
                else:
                    stage_events[(fd, first_sn)].append((ts, stage_id))
                continue
            if len(parts) != 4:
                n_bad += 1
                continue
            try:
                fd = int(parts[0])
                sn = int(parts[1])
                bc = int(parts[2])
                ts = int(parts[3])
            except ValueError:
                n_bad += 1
                continue
            per_fd[fd].append((bc, sn, ts))
            deliver_ts[(fd, sn)].append(ts)
    for fd in per_fd:
        per_fd[fd].sort()
    for fd in range_events:
        range_events[fd].sort()
    return per_fd, stage_events, range_events, deliver_ts, n_bad


def assign_segments(brpc_table, point, pkt_per_fd):
    """Map cid -> [(sn, ts_ns)] via the byte-offset bridge.

    Per fd, walk brpc RX records (sorted by byte_cursor) and ubs segments
    (sorted by byte_cursor) with two pointers: a segment belongs to the
    first message whose end cursor reaches it. Returns
    ({cid: [(sn, ts_ns)]}, n_msgs_no_fd, n_tail_segs_past_last_msg).
    """
    msgs_by_fd = defaultdict(list)  # fd -> [(byte_cursor, cid)]
    no_fd = 0
    for cid, pts in brpc_table.items():
        rec = pts.get(point)
        if rec is None:
            continue
        _ts, fd, bc = rec
        if fd < 0:
            no_fd += 1
            continue
        msgs_by_fd[fd].append((bc, cid))
    cid_segs = {}
    tail_segs = 0
    for fd, msgs in msgs_by_fd.items():
        msgs.sort()
        segs = pkt_per_fd.get(fd, [])
        i = 0
        cur = []
        for bc, cid in msgs:
            while i < len(segs) and segs[i][0] <= bc:
                cur.append((segs[i][1], segs[i][2]))
                i += 1
            cid_segs[cid] = cur
            cur = []
        tail_segs += len(segs) - i  # delivered bytes of an in-flight message
    return cid_segs, no_fd, tail_segs


def prep_ranges(range_events):
    """Pre-index range events per fd for O(log n) membership queries.

    Returns {fd: (firsts, ends, ranges)} where firsts/ends are the sorted
    first_sn/end_sn columns (monotonic: same-fd batch SNs are allocated
    contiguously), built ONCE per log instead of per RPC."""
    return {fd: ([r[0] for r in rs], [r[1] for r in rs], rs)
            for fd, rs in range_events.items()}


def _match_ranges(prepped, sn):
    """Return range events covering sn. The covering set is ranges[j:i]
    with i = upper_bound(first_sn <= sn) and j = lower_bound(end_sn >= sn);
    both bisects are O(log n) on the pre-indexed columns."""
    if prepped is None:
        return ()
    firsts, ends, ranges = prepped
    i = bisect.bisect_right(firsts, sn)
    j = bisect.bisect_left(ends, sn)
    if j >= i:
        return ()
    return ranges[j:i]


def collect_stage_groups(stage_events, ranges_prep, deliver_ts, fd, sn_set,
                         close_rx):
    """Pick the (fd, first_sn) stage groups whose first_sn ∈ sn_set.

    Returns [(first_sn, [(ts_ns, stage_id)])] with each group's events sorted
    by ts. Three event sources per group:
      - exact S rows keyed (fd, sn), filtered to the group's direction
        (_TX_STAGES for TX groups, _RX_STAGES for RX groups): both
        directions' SN spaces overlap numerically on one fd, so an
        unfiltered lookup would absorb opposite-direction events sharing the
        same (fd, sn) key and fabricate cross-direction segments;
      - batch RANGE events (UmqPostSend, one record per posted batch): matched
        by interval membership sn ∈ [first_sn, end_sn], TX groups only
        (close_rx=False). RX groups must NOT see them -- same-fd SN values of
        the opposite direction overlap numerically and would cross-match;
      - for RX groups (close_rx=True) a synthetic STAGE_DELIVERED endpoint
        merged from the P-row delivery stamps of the same (fd, sn) -- the
        last-fragment delivery closes the "last stage -> ubs_poll delivery"
        wake segment.
    A group is created whenever any source contributes (SMALL_DATA segments
    have no exact TX event but are covered by the batch range; their RX
    group carries RxCqeData + delivered). ranges_prep is the output of
    prep_ranges (or None)."""
    groups = []
    allow = _RX_STAGES if close_rx else _TX_STAGES
    prepped = ranges_prep.get(fd) if (ranges_prep and not close_rx) else None
    for sn in sn_set:
        evs = [e for e in stage_events.get((fd, sn), ()) if e[1] in allow]
        for _first, _end, sid, ts in _match_ranges(prepped, sn):
            evs.append((ts, sid))
        if close_rx:
            dts = deliver_ts.get((fd, sn))
            if dts:
                evs.append((max(dts), STAGE_DELIVERED))
        if not evs:
            continue
        evs.sort()
        groups.append((sn, evs))
    return groups


def iter_seg_durations(evs):
    """Yield (from_stage, to_stage, dur_us) for adjacent events (ts-sorted)."""
    for i in range(len(evs) - 1):
        yield evs[i][1], evs[i + 1][1], (evs[i + 1][0] - evs[i][0]) / 1000.0


def seg_label(frm, to):
    tag = "  [READ wire]" if (frm, to) == (STAGE_UMQ_POST_READ, STAGE_READ_CQE) else ""
    return "%s->%s%s" % (STAGE_NAMES.get(frm, "?%d" % frm),
                         STAGE_NAMES.get(to, "?%d" % to), tag)


def print_stage_waterfall(row):
    """Per-RPC stage waterfall for --top: four direction groups, each offer
    group printed with per-event relative offsets and segment durations."""
    fd_c, fd_s = row.get("fd_c", -1), row.get("fd_s", -1)
    for label, gkey, fd in (("Client Send (req TX)", "req_tx_g", fd_c),
                            ("Server Recv (req RX)", "req_rx_g", fd_s),
                            ("Server Send (rsp TX)", "rsp_tx_g", fd_s),
                            ("Client Recv (rsp RX)", "rsp_rx_g", fd_c)):
        groups = row.get(gkey)
        if not groups:
            continue
        print("    %s fd=%d offers=%d:" % (label, fd, len(groups)))
        for sn, evs in groups:
            t0 = evs[0][0]
            print("      offer first_sn=%d:" % sn)
            for i, (ts, sid) in enumerate(evs):
                rel = (ts - t0) / 1000.0
                if i + 1 < len(evs):
                    dur = (evs[i + 1][0] - ts) / 1000.0
                    tail = "  +%-9.2f-> %s%s" % (
                        dur, STAGE_NAMES.get(evs[i + 1][1], "?"),
                        "  [READ wire]" if (sid, evs[i + 1][1]) ==
                        (STAGE_UMQ_POST_READ, STAGE_READ_CQE) else "")
                else:
                    tail = ""
                print("        +%10.2fus  %-16s%s" %
                      (rel, STAGE_NAMES.get(sid, "?%d" % sid), tail))


def pct(sorted_vals, q):
    """Percentile with linear interpolation; q in [0,100]."""
    if not sorted_vals:
        return 0.0
    if len(sorted_vals) == 1:
        return float(sorted_vals[0])
    k = (len(sorted_vals) - 1) * (q / 100.0)
    lo = int(k)
    hi = min(lo + 1, len(sorted_vals) - 1)
    frac = k - lo
    return sorted_vals[lo] + (sorted_vals[hi] - sorted_vals[lo]) * frac


def describe(name, vals, unit="us"):
    if not vals:
        print("  %-12s: (no samples)" % name)
        return
    s = sorted(vals)
    n = len(s)
    print("  %-12s: n=%-8d avg=%9.2f  p50=%9.2f  p90=%9.2f  p99=%9.2f  "
          "p999=%9.2f  max=%9.2f  (%s)" % (
              name, n, sum(s) / n, pct(s, 50), pct(s, 90),
              pct(s, 99), pct(s, 99.9), s[-1], unit))


def fmt_sns(segs, cap=8):
    sns = [str(sn) for sn, _ts in segs]
    if len(sns) > cap:
        return ",".join(sns[:cap]) + ",...(+%d)" % (len(sns) - cap)
    return ",".join(sns)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--client", required=True, help="client rpc_trace_*.log (T1/T2)")
    ap.add_argument("--server", required=True, help="server rpc_trace_*.log (T3/T4)")
    ap.add_argument("--client-pkt", default="",
                    help="client ubs_pkt_trace_*.log (response segments)")
    ap.add_argument("--server-pkt", default="",
                    help="server ubs_pkt_trace_*.log (request segments)")
    ap.add_argument("--csv", default="", help="optional per-RPC CSV output path")
    ap.add_argument("--top", type=int, default=0,
                    help="print the N slowest RPCs by link_time")
    args = ap.parse_args()

    client, bad_c = load_brpc(args.client, {POINT_T1, POINT_T2})
    server, bad_s = load_brpc(args.server, {POINT_T3, POINT_T4})

    # Phase-3 byte-offset bridge: cid -> per-SN delivery segments.
    # Phase-4 stage events ride on the same logs (S rows; batch-covering
    # UmqPostSend rows carry sn_count and become range events).
    req_map, rsp_map = {}, {}
    stage_c, stage_s, deliver_c, deliver_s = {}, {}, {}, {}
    ranges_c, ranges_s = None, None
    bridge_notes = []
    if args.server_pkt:
        pkt_s, stage_s, range_s, deliver_s, bad_ps = load_pkt(args.server_pkt)
        ranges_s = prep_ranges(range_s)
        req_map, nofd_s, tail_s = assign_segments(server, POINT_T3, pkt_s)
        bridge_notes.append(
            "server-pkt: %s (%d fds, %d stage keys, %d range evts, %d bad "
            "lines); req segs aligned to %d cids (msgs_without_fd=%d, "
            "tail_segs_inflight=%d)" % (
                args.server_pkt, len(pkt_s), len(stage_s), len(range_s),
                bad_ps, len(req_map), nofd_s, tail_s))
    if args.client_pkt:
        pkt_c, stage_c, range_c, deliver_c, bad_pc = load_pkt(args.client_pkt)
        ranges_c = prep_ranges(range_c)
        rsp_map, nofd_c, tail_c = assign_segments(client, POINT_T2, pkt_c)
        bridge_notes.append(
            "client-pkt: %s (%d fds, %d stage keys, %d range evts, %d bad "
            "lines); rsp segs aligned to %d cids (msgs_without_fd=%d, "
            "tail_segs_inflight=%d)" % (
                args.client_pkt, len(pkt_c), len(stage_c), len(range_c),
                bad_pc, len(rsp_map), nofd_c, tail_c))
    have_stage = bool(stage_c or stage_s or ranges_c or ranges_s)

    rows = []          # dict per joined RPC
    only_client = 0    # cids with T1/T2 but no matching server T3/T4
    only_server = 0
    incomplete_c = 0   # cids missing T1 or T2
    incomplete_s = 0

    for cid, cp in client.items():
        if POINT_T1 not in cp or POINT_T2 not in cp:
            incomplete_c += 1
            continue
        sp = server.get(cid)
        if not sp or POINT_T3 not in sp or POINT_T4 not in sp:
            only_client += 1
            continue
        t1 = cp[POINT_T1][0]
        t2 = cp[POINT_T2][0]
        t3 = sp[POINT_T3][0]
        t4 = sp[POINT_T4][0]
        row = {
            "cid": cid,
            "e2e": (t2 - t1) / 1000.0,
            "sprc": (t4 - t3) / 1000.0,
            "link": ((t2 - t1) - (t4 - t3)) / 1000.0,
            "fd_c": cp[POINT_T1][1],
            "fd_s": sp[POINT_T3][1],
        }
        req_segs = req_map.get(cid)
        if req_segs:
            row["req_nseg"] = len(req_segs)
            row["req_span"] = (req_segs[-1][1] - req_segs[0][1]) / 1000.0
            row["req_tail"] = (t3 - req_segs[-1][1]) / 1000.0
            row["req_segs"] = req_segs
        rsp_segs = rsp_map.get(cid)
        if rsp_segs:
            row["rsp_nseg"] = len(rsp_segs)
            row["rsp_span"] = (rsp_segs[-1][1] - rsp_segs[0][1]) / 1000.0
            row["rsp_tail"] = (t2 - rsp_segs[-1][1]) / 1000.0
            row["rsp_segs"] = rsp_segs
        if "req_tail" in row and "rsp_tail" in row:
            row["wire_resid"] = row["link"] - row["req_tail"] - row["rsp_tail"]

        # Phase-4: match stage-event groups by (fd, first_sn ∈ sn_set).
        # req TX events live in the client log (fd_c) but share the sender's
        # SN space with the server-log P rows that built req sn_set; rsp is
        # symmetric. RX groups get a P-row "delivered" endpoint appended.
        if have_stage and row["fd_c"] >= 0 and row["fd_s"] >= 0:
            if req_segs:
                req_sns = {sn for sn, _ts in req_segs}
                row["req_tx_g"] = collect_stage_groups(
                    stage_c, ranges_c, None, row["fd_c"], req_sns, close_rx=False)
                row["req_rx_g"] = collect_stage_groups(
                    stage_s, None, deliver_s, row["fd_s"], req_sns, close_rx=True)
            if rsp_segs:
                rsp_sns = {sn for sn, _ts in rsp_segs}
                row["rsp_tx_g"] = collect_stage_groups(
                    stage_s, ranges_s, None, row["fd_s"], rsp_sns, close_rx=False)
                row["rsp_rx_g"] = collect_stage_groups(
                    stage_c, None, deliver_c, row["fd_c"], rsp_sns, close_rx=True)
            segsum = defaultdict(float)
            for gkey in ("req_tx_g", "req_rx_g", "rsp_tx_g", "rsp_rx_g"):
                for _sn, evs in row.get(gkey, ()):
                    for frm, to, dur in iter_seg_durations(evs):
                        segsum[(frm, to)] += dur  # multi-offer RPC: sum
            if segsum:
                row["segsum"] = dict(segsum)
                row["stage_total"] = sum(segsum.values())
                row["unattr"] = row["link"] - row["stage_total"]
        rows.append(row)

    for cid, sp in server.items():
        if cid not in client:
            if POINT_T3 in sp and POINT_T4 in sp:
                only_server += 1
            else:
                incomplete_s += 1

    print("=" * 72)
    print("Four-point RPC link-latency join" +
          (" + per-segment attribution" if bridge_notes else ""))
    print("=" * 72)
    print("client log: %s  (%d cids, %d bad lines)" % (args.client, len(client), bad_c))
    print("server log: %s  (%d cids, %d bad lines)" % (args.server, len(server), bad_s))
    for note in bridge_notes:
        print(note)
    print("joined RPCs: %d" % len(rows))
    if incomplete_c or incomplete_s or only_client or only_server:
        print("  dropped: incomplete_client=%d incomplete_server=%d "
              "only_client=%d only_server=%d" % (
                  incomplete_c, incomplete_s, only_client, only_server))
    print("-" * 72)

    describe("e2e", [r["e2e"] for r in rows])
    describe("server_prc", [r["sprc"] for r in rows])
    describe("link_time", [r["link"] for r in rows])
    if bridge_notes:
        print("-" * 72)
        describe("req_nseg", [float(r["req_nseg"]) for r in rows if "req_nseg" in r], unit="segs")
        describe("req_span", [r["req_span"] for r in rows if "req_span" in r])
        describe("req_tail", [r["req_tail"] for r in rows if "req_tail" in r])
        describe("rsp_nseg", [float(r["rsp_nseg"]) for r in rows if "rsp_nseg" in r], unit="segs")
        describe("rsp_span", [r["rsp_span"] for r in rows if "rsp_span" in r])
        describe("rsp_tail", [r["rsp_tail"] for r in rows if "rsp_tail" in r])
        describe("wire_resid", [r["wire_resid"] for r in rows if "wire_resid" in r])

    # Phase-4 aggregate: per-event-pair segment durations (one sample per
    # adjacent pair inside one offer group; NOT per-RPC sums, so counts track
    # offer count). READ wire = UmqPostRead→ReadCqe, same-node same-clock.
    if have_stage:
        agg = defaultdict(list)
        n_groups = 0
        for r in rows:
            for gkey in ("req_tx_g", "req_rx_g", "rsp_tx_g", "rsp_rx_g"):
                for _sn, evs in r.get(gkey, ()):
                    n_groups += 1
                    for frm, to, dur in iter_seg_durations(evs):
                        agg[(frm, to)].append(dur)
        if agg:
            print("-" * 72)
            print("Stage segments (per event-pair, us; %d offer groups matched):"
                  % n_groups)
            for frm, to in sorted(agg, key=lambda k: (
                    _STAGE_ORDER.get(k[0], 99), _STAGE_ORDER.get(k[1], 99))):
                describe(seg_label(frm, to), agg[(frm, to)])
            describe("stage_total", [r["stage_total"] for r in rows
                                     if "stage_total" in r])
            describe("unattr", [r["unattr"] for r in rows if "unattr" in r])
            print("  note: unattr = link_time - Σ stage segments = cross-node "
                  "OFFER SEND legs + brpc-internal gaps (T1→TX entry, etc.)")

    # Residual checks: link_time must be physically positive (d_req + d_rsp);
    # tails must be >= 0 (cut-out happens at/after last byte delivered).
    link = [r["link"] for r in rows]
    neg = sum(1 for v in link if v < 0)
    neg_rt = sum(1 for r in rows if r.get("req_tail", 0) < 0)
    neg_pt = sum(1 for r in rows if r.get("rsp_tail", 0) < 0)
    if link:
        print("-" * 72)
        print("sanity: link_time<0 count=%d (%.1f%%)  -- expect ~0; theta cancels "
              "so this should be physically positive" % (neg, 100.0 * neg / len(link)))
        if bridge_notes:
            print("sanity: req_tail<0 count=%d, rsp_tail<0 count=%d  -- expect ~0; "
                  "cut-out is at/after last segment delivery" % (neg_rt, neg_pt))

    if args.top > 0 and rows:
        print("-" * 72)
        print("Top %d slowest RPCs by link_time:" % min(args.top, len(rows)))
        hdr = "  %-22s %10s %10s %10s" % ("cid", "e2e(us)", "server(us)", "link(us)")
        if bridge_notes:
            hdr += "  %6s %10s %10s  %6s %10s %10s %12s" % (
                "reqseg", "reqspan", "reqtail", "rspseg", "rspspan", "rsptail", "wireresid")
        if have_stage:
            hdr += " %10s %10s" % ("stage_tot", "unattr")
        print(hdr)
        for r in sorted(rows, key=lambda x: x["link"], reverse=True)[:args.top]:
            line = "  %-22d %10.2f %10.2f %10.2f" % (r["cid"], r["e2e"], r["sprc"], r["link"])
            if bridge_notes:
                line += "  %6s %10s %10s  %6s %10s %10s %12s" % (
                    r.get("req_nseg", "-"),
                    "%.2f" % r["req_span"] if "req_span" in r else "-",
                    "%.2f" % r["req_tail"] if "req_tail" in r else "-",
                    r.get("rsp_nseg", "-"),
                    "%.2f" % r["rsp_span"] if "rsp_span" in r else "-",
                    "%.2f" % r["rsp_tail"] if "rsp_tail" in r else "-",
                    "%.2f" % r["wire_resid"] if "wire_resid" in r else "-")
            if have_stage:
                line += " %10s %10s" % (
                    "%.2f" % r["stage_total"] if "stage_total" in r else "-",
                    "%.2f" % r["unattr"] if "unattr" in r else "-")
            print(line)
            if "req_segs" in r:
                print("    req SNs (server): %s" % fmt_sns(r["req_segs"]))
            if "rsp_segs" in r:
                print("    rsp SNs (client): %s" % fmt_sns(r["rsp_segs"]))
            if have_stage and "segsum" in r:
                print_stage_waterfall(r)

    if args.csv:
        cols = ["cid", "e2e", "sprc", "link", "req_nseg", "req_span", "req_tail",
                "rsp_nseg", "rsp_span", "rsp_tail", "wire_resid",
                "stage_total", "unattr"]
        names = {"e2e": "e2e_us", "sprc": "server_prc_us", "link": "link_us",
                 "req_span": "req_span_us", "req_tail": "req_tail_us",
                 "rsp_span": "rsp_span_us", "rsp_tail": "rsp_tail_us",
                 "wire_resid": "wire_resid_us", "stage_total": "stage_total_us",
                 "unattr": "unattr_us"}
        with open(args.csv, "w") as f:
            f.write(",".join(names.get(c, c) for c in cols) + "\n")
            for r in rows:
                out = []
                for c in cols:
                    v = r.get(c, "")
                    out.append("%.2f" % v if isinstance(v, float) else str(v))
                f.write(",".join(out) + "\n")
        print("-" * 72)
        print("wrote per-RPC CSV: %s (%d rows)" % (args.csv, len(rows)))

    return 0


if __name__ == "__main__":
    sys.exit(main())
