#!/usr/bin/env python3
"""Generate RPC latency flowchart SVG from trace data.

Auto-calls join_rpc_trace.py to parse RPC + pkt trace logs, then generates
an SVG flowchart. Auto-selects bigdata (19-stage) or small-payload (12-stage)
layout based on stage event presence.

Usage:
    python3 gen_flowchart.py \
        --client-rpc rpc_trace_client.log \
        --server-rpc rpc_trace_server.log \
        --client-pkt ubs_pkt_trace_client.log \
        --server-pkt ubs_pkt_trace_server.log \
        [--prof-server ubsocket_profiling_server.log] \
        [--prof-client ubsocket_profiling_client.log] \
        --output rpc_latency_flowchart.svg
"""

import argparse
import os
import re
import subprocess
import sys
from collections import defaultdict

# Find join_rpc_trace.py: search upward from this script for a tools/trace dir.
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
JOIN_SCRIPT = None
_d = _SCRIPT_DIR
for _ in range(10):
    _d = os.path.dirname(_d)
    candidate = os.path.join(_d, "tools", "trace", "join_rpc_trace.py")
    if os.path.exists(candidate):
        JOIN_SCRIPT = candidate
        break
if not JOIN_SCRIPT:
    # Fallback: ubscomm repo root
    for root in ["/home/gonglei/blue/ubscomm"]:
        c = os.path.join(root, "tools", "trace", "join_rpc_trace.py")
        if os.path.exists(c):
            JOIN_SCRIPT = c
            break

STAGE_NAMES = {
    0: "delivered",
    1: "TRY_SENDER_POST",
    2: "FLUSH_PENDING_OFFER",
    3: "UMQ_POST_SEND",
    4: "TX_CQE_READ",
    5: "HANDLE_RX_CTRL",
    6: "DO_READ_OFFER",
    7: "UMQ_POST_READ",
    8: "FINALIZE_IO",
    9: "DELIVER_TO_RX_QUEUE",
    10: "SEND_SIMPLE_CTRL",
    11: "RX_CQE_DATA",
}

# Stage names that only exist on the bigdata (READ_OFFER) path. Used by the
# layout selection: phase-5 small-payload logs DO carry stage events
# (RxCqeData + UmqPostSend range), so a bare "any stage events" test would
# misclassify them as bigdata.
BIGDATA_STAGE_KEYS = {"DO_READ_OFFER", "UMQ_POST_READ", "FINALIZE_IO",
                      "DELIVER_TO_RX_QUEUE", "SEND_SIMPLE_CTRL"}

# Bigdata stages: (column, row, component, display_name_cn, sub_items)
# column: 0=Client send, 1=Server recv, 2=Server send, 3=Client recv
# Matches the reference rpc_latency_flowchart.svg exactly (19 stages)
STAGES_BIGDATA = [
    # Client send (col 0): 3 rows
    (0, 0, "brpc",      "Client 序列化",          [("BRPC_SERIALIZE", 0)]),
    (0, 1, "ubsocket",  "Client 发送入口",         [("HANDLE_SMALL_SEGMENT", 0), ("HANDLE_LARGE_SEGMENT", 0), ("FLUSH_PENDING_OFFER", 0), ("其他未打点", 0)]),
    (0, 2, "umq",       "Client post send",       []),
    # Server recv (col 1): 6 rows
    (1, 0, "umq",       "Server CQE poll",        [("TX_CQE_READ", 0), ("HANDLE_TX_COMPLETION", 0)]),
    (1, 1, "ubsocket",  "Server HandleRxControl",  []),
    (1, 2, "ubsocket",  "Server DoReadOffer",      [("PARSE_READ_OFFER", 0), ("MEMPOOL_IMPORT", 0), ("READ_WR_ALLOC", 0), ("其他未打点", 0)]),
    (1, 3, "umq",       "Server post read",       []),
    (1, 4, "ubsocket",  "Server FinalizeIo+SendCtrl",[("DELIVER_TO_RX_QUEUE", 0), ("FINALIZE_DELIVER_WAKE", 0), ("SEND_SIMPLE_CTRL", 0)]),
    (1, 5, "brpc",      "Server 反序列化",         [("BRPC_SERVER_DESERIALIZE", 0)]),
    # Server send (col 2): 3 rows
    (2, 0, "brpc",      "Server 序列化",           [("BRPC_SERIALIZE", 0)]),
    (2, 1, "ubsocket",  "Server 发送入口",         [("HANDLE_LARGE_SEGMENT", 0), ("FLUSH_PENDING_OFFER", 0)]),
    (2, 2, "umq",       "Server post send",       []),
    # Client recv (col 3): 6 rows
    (3, 0, "umq",       "Client CQE poll",        [("TX_CQE_READ", 0), ("HANDLE_TX_COMPLETION", 0)]),
    (3, 1, "ubsocket",  "Client HandleRxControl",  []),
    (3, 2, "ubsocket",  "Client DoReadOffer",      [("PARSE_READ_OFFER", 0), ("MEMPOOL_IMPORT", 0), ("READ_WR_ALLOC", 0), ("其他未打点", 0)]),
    (3, 3, "umq",       "Client post read",       []),
    (3, 4, "ubsocket",  "Client FinalizeIo+SendCtrl",[("DELIVER_TO_RX_QUEUE", 0), ("FINALIZE_DELIVER_WAKE", 0), ("SEND_SIMPLE_CTRL", 0)]),
    (3, 5, "brpc",      "Client 反序列化+响应处理",[("BRPC_NATIVE_RX_READ", 0), ("BRPC_NATIVE_RX_PROC", 0), ("BRPC_DESERIALIZE", 0), ("BRPC_CLIENT_PROCESS_RSP", 0), ("BTHREAD_SCHED", 0)]),
]

# Small-payload stages (<20KB, no READ_OFFER)
STAGES_SMALL = [
    (0, 0, "brpc",      "Client 序列化",            []),
    (0, 1, "ubsocket",  "Client 发送(SMALL_DATA)",   []),
    (0, 2, "umq",       "Client post send",         []),
    (1, 0, "umq",       "Server CQE poll",          []),
    (1, 1, "ubsocket",  "Server HandleRxControl",    []),
    (1, 2, "brpc",      "Server 反序列化",           []),
    (2, 0, "brpc",      "Server 序列化",             []),
    (2, 1, "ubsocket",  "Server 发送(SMALL_DATA)",   []),
    (2, 2, "umq",       "Server post send",         []),
    (3, 0, "umq",       "Client CQE poll",          []),
    (3, 1, "ubsocket",  "Client HandleRxControl",    []),
    (3, 2, "brpc",      "Client 反序列化+响应处理",  []),
]

COMP_COLORS = {"brpc": "#4A90D9", "ubsocket": "#E67E22", "umq": "#27AE60", "urma": "#8E44AD"}
COMP_LABELS = {"brpc": "brpc", "ubsocket": "ubsocket", "umq": "UMQ", "urma": "URMA"}
STROKE = "#333333"


# Direction split of stage ids (mirror of join_rpc_trace.py): 1-3 TX,
# 4-11 RX. One pkt log carries BOTH directions' events and the two SN
# spaces overlap numerically on the same fd, so (fd, first_sn) groups
# merge cross-direction events -- groups must be split by direction
# before adjacent-pair occupancy segments are computed.
_OCC_TX_STAGES = frozenset((1, 2, 3))
_OCC_RX_STAGES = frozenset((4, 5, 6, 7, 8, 9, 10, 11))


def load_stage_occupancy(path):
    """Parse S lines from a pkt trace log, compute per-stage occupancy
    (duration from this stage's entry to the next stage's entry within the
    same (fd, first_sn) group). Returns {stage_name: avg_us}.

    Each group is SPLIT by direction (1-3 TX, 4-11 RX) before adjacency is
    computed -- mirror of collect_stage_groups in join_rpc_trace.py.
    Cross-direction events share (fd, first_sn) keys only through SN-space
    collision, and merely skipping cross-direction pairs would lose real
    same-direction segments straddling an interleaved opposite-direction
    event (the norm under bidirectional pipelining). No duration
    threshold -- a slow-but-real segment (READ wire of a big offer,
    KeepWrite wait under load) must not be silently dropped and then
    misreported as 未采集.
    """
    if not path or not os.path.exists(path):
        return {}
    groups = defaultdict(list)
    with open(path) as f:
        for line in f:
            if line.startswith("S"):
                parts = line.rstrip("\n").split("\t")
                if len(parts) >= 5:
                    try:
                        stage_id = int(parts[1])
                        fd = int(parts[2])
                        first_sn = int(parts[3])
                        ts = int(parts[4])
                    except ValueError:
                        continue
                    groups[(fd, first_sn)].append((ts, stage_id))
    # Collect per (from, to) pair durations within each direction split
    pair_durs = defaultdict(list)
    for key, evs in groups.items():
        evs.sort()
        for dir_set in (_OCC_TX_STAGES, _OCC_RX_STAGES):
            sub = [e for e in evs if e[1] in dir_set]
            for i in range(len(sub) - 1):
                dur_us = (sub[i + 1][0] - sub[i][0]) / 1000.0
                if dur_us > 0:
                    pair_durs[(sub[i][1], sub[i + 1][1])].append(dur_us)
    # Average all same-direction durations per FROM stage. The local_only
    # stage filter in generate_svg() decides which stages may use these
    # values; others fall back to the join aggregate.
    from_durs = defaultdict(list)
    for (frm, to), durs in pair_durs.items():
        from_durs[frm].extend(durs)
    result = {}
    name_map = {1: "TRY_SENDER_POST", 2: "FLUSH_PENDING_OFFER", 3: "UMQ_POST_SEND",
                4: "TX_CQE_READ", 5: "HANDLE_RX_CTRL", 6: "DO_READ_OFFER",
                7: "UMQ_POST_READ", 8: "FINALIZE_IO", 9: "DELIVER_TO_RX_QUEUE",
                10: "SEND_SIMPLE_CTRL", 11: "RX_CQE_DATA"}
    for sid, durs in from_durs.items():
        if durs:
            result[name_map.get(sid, str(sid))] = sum(durs) / len(durs)
    return result


def run_join(args):
    if not JOIN_SCRIPT:
        print("ERROR: join_rpc_trace.py not found", file=sys.stderr)
        sys.exit(1)
    cmd = [sys.executable, JOIN_SCRIPT,
           "--client", args.client_rpc, "--server", args.server_rpc,
           "--client-pkt", args.client_pkt, "--server-pkt", args.server_pkt,
           "--top", "0"]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print("join_rpc_trace.py failed:", result.stderr, file=sys.stderr)
        sys.exit(1)
    return result.stdout


def parse_join_output(text):
    data = {}
    for m in re.finditer(
        r"^\s+(\w+)\s*:\s*n=(\d+)\s+avg=\s*([\d.]+)\s+p50=\s*([\d.]+)\s+p90=\s*([\d.]+)\s+p99=\s*([\d.]+)\s+p999=\s*([\d.]+)",
        text, re.MULTILINE):
        key = m.group(1)
        # stage_total and unattr are event-pair aggregates (sum across all
        # offers, not per-RPC), not meaningful as per-RPC component totals.
        if key in ("stage_total", "unattr"):
            continue
        data[key] = {"n": int(m.group(2)), "avg": float(m.group(3)),
                            "p50": float(m.group(4)), "p90": float(m.group(5)),
                            "p99": float(m.group(6)), "p999": float(m.group(7))}
    # Parse stage breakdown - two formats:
    # Format 1 (simplified): "  STAGE_NAME   Count  AVG(us)  P50  P99  Max"
    # Format 2 (event-pair): "  FromStage->ToStage: n=...  avg=...  p50=..."
    #                        "  UmqPostRead->ReadCqe  [READ wire]: n=..."
    stages = {}
    for m in re.finditer(
        r"^\s{2}(\w+)\s+(\d+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)",
        text, re.MULTILINE):
        stages[m.group(1)] = {"count": int(m.group(2)), "avg": float(m.group(3)),
                              "p50": float(m.group(4)), "p99": float(m.group(5)),
                              "max": float(m.group(6))}
    # Event-pair rows. The "[READ wire]" tag sits between the pair and the
    # colon, so it must be matched explicitly or the row is silently dropped.
    pair_map = {
        "TrySenderPost": "TRY_SENDER_POST", "UmqPostSend": "UMQ_POST_SEND",
        "FlushPendingOffer": "FLUSH_PENDING_OFFER", "HandleRxCtrl": "HANDLE_RX_CTRL",
        "DoReadOffer": "DO_READ_OFFER", "UmqPostRead": "UMQ_POST_READ",
        "ReadCqe": "TX_CQE_READ", "FinalizeIo": "FINALIZE_IO",
        "DeliverToRxQ": "DELIVER_TO_RX_QUEUE", "SendSimpleCtrl": "SEND_SIMPLE_CTRL",
        "RxCqeData": "RX_CQE_DATA",
    }
    pair_rows = []
    for m in re.finditer(
        r"^\s{2}(\w+)->(\w+)(?:\s+\[READ wire\])?:\s+n=(\d+)\s+avg=\s*([\d.]+)\s+p50=\s*([\d.]+)",
        text, re.MULTILINE):
        pair_rows.append((m.group(1), m.group(2), int(m.group(3)),
                          float(m.group(4)), float(m.group(5))))
    # Pass 1: store the pair duration under the FROM stage -- "A->B" is A's
    # occupancy (A's entry to the next stage's entry). join_rpc_trace.py
    # filters exact stage events by direction, so the aggregate contains no
    # cross-direction pairs; no duration threshold is applied here either --
    # dropping a slow-but-real segment (READ wire of a big offer) would
    # print a dishonest "未采集" over data that was actually collected.
    for from_stage, _to, n, avg, p50 in pair_rows:
        from_key = pair_map.get(from_stage, from_stage)
        if from_key not in stages:
            stages[from_key] = {"count": n, "avg": avg,
                                "p50": p50, "p99": 0, "max": 0}
    # Pass 2 is intentionally omitted: a group-last stage (e.g. UmqPostSend)
    # has no outgoing pair, and its incoming pair's avg is the FROM stage's
    # occupancy — using it as the TO stage's value would be misleading.
    # Such stages render as 未采集, which is more truthful than a fabricated
    # value.
    data["stages"] = stages
    return data


def parse_prof(path):
    if not path or not os.path.exists(path):
        return {}
    result = {}
    with open(path) as f:
        for line in f:
            m = re.match(r"\[([^\]]+)\]\s+\d+\s+\d+\s+\d+\s+(\d+)\s+\d+\s+\d+\s+(\d+)\s+\d+", line.strip())
            if m:
                result[m.group(1)] = {"avg_ns": int(m.group(2)), "p99_ns": int(m.group(3))}
    return result


def ns_to_us(ns):
    return ns / 1000.0


def get_stage_avg(data, prof, stage_name, default=None):
    """Measured stage average (us), or None when neither the join stage
    table nor the prof log carries it. Callers render None as 未采集."""
    stages = data.get("stages", {})
    if stage_name in stages:
        return stages[stage_name]["avg"]
    if stage_name in prof:
        return ns_to_us(prof[stage_name]["avg_ns"])
    return default


def calc_component_totals(data, prof_server, prof_client):
    """Collect MEASURED per-component durations (us). No ratio estimation:
    anything not directly instrumented is returned as None and rendered as
    "未采集" — a visible gap is more truthful than a fabricated split.

      brpc_server : T4 - T3 (measured, server clock)
      brpc_client : PROF serialize+deserialize+process_rsp, else None
      stage_total : link_time (通信栈 = e2e - server_prc, measured residual
                    of the four-point trace), None when unavailable
      read_wire   : UmqPostRead->ReadCqe per-offer RDMA READ fabric time
                    (bigdata only, same-node same-clock), else None
    """
    e2e = data.get("e2e", {}).get("avg", 0)
    server_prc = data.get("server_prc", {}).get("avg", 0)

    client_brpc = None
    if prof_client:
        v = (ns_to_us(prof_client.get("BRPC_SERIALIZE", {}).get("avg_ns", 0)) +
             ns_to_us(prof_client.get("BRPC_DESERIALIZE", {}).get("avg_ns", 0)) +
             ns_to_us(prof_client.get("BRPC_CLIENT_PROCESS_RSP", {}).get("avg_ns", 0)))
        client_brpc = v if v > 0 else None

    # join's stage_total/unattr are event-pair aggregates (not per-RPC), so
    # they are skipped by parse_join_output. Use link_time (= e2e -
    # server_prc, a four-point measured residual) as the 通信栈 total.
    link_time = data.get("link_time", {}).get("avg", 0)
    ubs_umq_total = link_time if link_time > 0 else None

    s = data.get("stages", {})
    is_bigdata = any(k in s for k in BIGDATA_STAGE_KEYS)
    read_wire = s.get("UMQ_POST_READ", {}).get("avg") if is_bigdata else None

    return {"brpc_server": server_prc, "brpc_client": client_brpc,
            "stage_total": ubs_umq_total, "read_wire": read_wire,
            "e2e": e2e}


def generate_svg(data, prof_server, prof_client, totals, output_path,
                 title_suffix="", server_stages=None, client_stages=None):
    e2e = totals["e2e"]
    wire_resid = data.get("wire_resid", {}).get("avg", 0)
    req_nseg = data.get("req_nseg", {}).get("avg", 0)

    # Per-endpoint prof lookup: col 0/3 = client, col 1/2 = server.
    # Fixes the bug where a single merged prof dict let client's
    # BRPC_SERIALIZE overwrite server's (or vice versa).
    def prof_for_col(col):
        return prof_client if col in (0, 3) else prof_server

    def prof_us(key, col=None):
        p = prof_for_col(col) if col is not None else prof_server
        v = p.get(key, {}).get("avg_ns", 0)
        return ns_to_us(v) if v > 0 else None

    def prof_us_sum(col, *keys):
        p = prof_for_col(col)
        vals = [p.get(k, {}).get("avg_ns", 0) for k in keys]
        return ns_to_us(sum(vals)) if any(v > 0 for v in vals) else None

    def fmt_us(v):
        """None means the path carries no instrumentation -- say so instead
        of printing a fabricated 0.0us."""
        return f"{v:.1f}us" if v is not None else "未采集"

    s = data.get("stages", {})
    is_small = not any(k in s for k in BIGDATA_STAGE_KEYS)

    W = 1280
    COL_W = 274
    COL_GAP = 54
    COL_X = [30, 30+COL_W+COL_GAP, 30+2*(COL_W+COL_GAP), 30+3*(COL_W+COL_GAP)]
    BOX_W = 250

    if is_small:
        H = 680
        stages_def = STAGES_SMALL
        row_heights = [38, 38, 38]
        row_heights_4 = [38, 38, 38]
        title = "bRPC UB-Native 小包路径时延分析"
        summary_label = "实测时延分解 (小包路径)"
    else:
        H = 1080
        stages_def = STAGES_BIGDATA
        # Col 0/2: 3 rows (38, 102, 38)
        row_heights = [38, 102, 38]
        # Col 1/3: 6 rows (72, 38, 102, 38, 87, 87)
        row_heights_4 = [72, 38, 102, 38, 87, 87]
        title = "bRPC UB-Native 全链路时延分析"
        summary_label = "实测时延分解"

    def row_y(col, row_idx):
        y = 80
        heights = row_heights_4 if col in (1, 3) else row_heights
        for i in range(row_idx):
            y += heights[i] + 17
        return y

    def row_h(col, row_idx):
        heights = row_heights_4 if col in (1, 3) else row_heights
        return heights[row_idx]

    parts = []
    parts.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
                 f'viewBox="0 0 {W} {H}" font-family="sans-serif">')
    parts.append(f'<rect width="{W}" height="{H}" fill="#FAFAFA"/>')
    if title_suffix:
        title += f" ({title_suffix})"
    parts.append(f'<text x="{W//2}" y="30" text-anchor="middle" font-size="18" '
                 f'font-weight="bold" fill="#222">{title}</text>')

    # Column headers
    col_titles = ["Client 发送", "Server 接收", "Server 发送", "Client 接收"]
    for i, t in enumerate(col_titles):
        x = COL_X[i] - 12
        parts.append(f'<rect x="{x}" y="50" width="{COL_W}" height="22" rx="5" fill="#EEE" stroke="#DDD"/>')
        parts.append(f'<text x="{x+COL_W//2}" y="65" text-anchor="middle" font-size="12" '
                     f'font-weight="bold" fill="#555">{t}</text>')

    # Compute per-stage durations. Measured values only: per-endpoint pkt
    # trace occupancy (server_stages / client_stages) for reliably-local
    # stages, else join aggregate, else PROF. A stage with no instrumentation
    # stays None and renders as 未采集 -- no ratio splits.
    server_stages = server_stages or {}
    client_stages = client_stages or {}

    def endpoint_stage(col, key):
        """Get stage avg for a specific endpoint (server=col 1/2, client=col 0/3).
        Prefer per-endpoint pkt-trace value, but only for stages where the
        FROM->TO pair is reliably local (HANDLE_RX_CTRL, DO_READ_OFFER,
        UMQ_POST_READ, FINALIZE_IO, DELIVER_TO_RX_QUEUE, TX_CQE_READ).
        For FLUSH_PENDING_OFFER and UMQ_POST_SEND, use join aggregate
        (direction-filtered) because pkt-trace FROM occupancy mixes local
        and cross-node TO targets."""
        local_only = {"HANDLE_RX_CTRL", "DO_READ_OFFER", "UMQ_POST_READ",
                      "FINALIZE_IO", "DELIVER_TO_RX_QUEUE", "TX_CQE_READ"}
        if key in local_only:
            if col in (1, 2) and key in server_stages:
                return server_stages[key]
            if col in (0, 3) and key in client_stages:
                return client_stages[key]
        # join aggregate fallback; stage names are never prof keys
        return get_stage_avg(data, {}, key)

    if is_small:
        stage_durs = {}
        for idx, (col, row, comp, name, _) in enumerate(stages_def):
            if comp == "ubsocket" and col in (0, 2):
                stage_durs[idx] = endpoint_stage(col, "TRY_SENDER_POST")
            elif comp == "ubsocket" and col in (1, 3) and row == 1:
                stage_durs[idx] = endpoint_stage(col, "RX_CQE_DATA")
            elif comp == "umq" and col in (0, 2):
                stage_durs[idx] = endpoint_stage(col, "UMQ_POST_SEND")
            elif comp == "umq" and col in (1, 3):
                stage_durs[idx] = None
            elif comp == "brpc" and col == 0:
                stage_durs[idx] = prof_us("BRPC_SERIALIZE", col)
            elif comp == "brpc" and col == 1:
                stage_durs[idx] = prof_us("BRPC_DESERIALIZE", col)
            elif comp == "brpc" and col == 2:
                stage_durs[idx] = prof_us("BRPC_SERIALIZE", col)
            elif comp == "brpc" and col == 3:
                stage_durs[idx] = prof_us_sum(col, "BRPC_DESERIALIZE",
                                              "BRPC_CLIENT_PROCESS_RSP")
            else:
                stage_durs[idx] = None
    else:
        stage_durs = {}
        for idx, (col, row, comp, name, _) in enumerate(stages_def):
            key_map = {1: "FLUSH_PENDING_OFFER", 2: "UMQ_POST_SEND",
                       4: "HANDLE_RX_CTRL", 5: "DO_READ_OFFER", 6: "UMQ_POST_READ",
                       7: "FINALIZE_IO",
                       10: "FLUSH_PENDING_OFFER", 11: "UMQ_POST_SEND",
                       13: "HANDLE_RX_CTRL", 14: "DO_READ_OFFER", 15: "UMQ_POST_READ",
                       16: "FINALIZE_IO"}
            dur = endpoint_stage(col, key_map[idx]) if idx in key_map else None
            if idx in (3, 12):
                dur = endpoint_stage(col, "TX_CQE_READ")
            elif idx in (0, 9):
                dur = prof_us("BRPC_SERIALIZE", col)
            elif idx == 8:
                dur = prof_us("BRPC_DESERIALIZE", col)
            elif idx == 17:
                dur = prof_us_sum(col, "BRPC_DESERIALIZE",
                                  "BRPC_CLIENT_PROCESS_RSP")
            stage_durs[idx] = dur

    # Sub-item duration lookup (measured only; None -> 未采集)
    # col-aware: brpc sub-items read from the correct endpoint's prof dict.
    def sub_dur(name, col=None):
        if name in STAGE_NAMES.values():
            return get_stage_avg(data, prof_server, name)
        prof_map = {"BRPC_SERIALIZE": "BRPC_SERIALIZE", "BRPC_DESERIALIZE": "BRPC_DESERIALIZE",
                    "BRPC_CLIENT_PROCESS_RSP": "BRPC_CLIENT_PROCESS_RSP",
                    "BRPC_SERVER_DESERIALIZE": "BRPC_DESERIALIZE",
                    "PARSE_READ_OFFER": "UBS_NATIVE_PARSE_READ_OFFER",
                    "MEMPOOL_IMPORT": "UBS_NATIVE_MEMPOOL_IMPORT",
                    "READ_WR_ALLOC": "UBS_NATIVE_READ_WR_ALLOC",
                    "HANDLE_LARGE_SEGMENT": "UBS_NATIVE_HANDLE_LARGE_SEGMENT",
                    "HANDLE_SMALL_SEGMENT": "UBS_NATIVE_HANDLE_SMALL_SEGMENT",
                    "HANDLE_TX_COMPLETION": "UBS_NATIVE_HANDLE_TX_COMPLETION",
                    "FINALIZE_DELIVER_WAKE": "UBS_NATIVE_FINALIZE_DELIVER_WAKE",
                    "BTHREAD_SCHED": "BRPC_BTHREAD_SCHED",
                    "BRPC_NATIVE_RX_READ": "BRPC_NATIVE_RX_READ",
                    "BRPC_NATIVE_RX_PROC": "BRPC_NATIVE_RX_PROC"}
        if name in prof_map:
            p = prof_for_col(col) if col is not None else prof_server
            if prof_map[name] in p:
                return ns_to_us(p[prof_map[name]]["avg_ns"])
        return None

    # Draw stage boxes
    for idx, (col, row, comp, name, sub_items) in enumerate(stages_def):
        x = COL_X[col]
        y = row_y(col, row)
        h = row_h(col, row)
        color = COMP_COLORS[comp]
        dur = stage_durs.get(idx)
        stage_num = idx + 1

        parts.append(f'<rect x="{x}" y="{y}" width="{BOX_W}" height="{h}" rx="6" fill="{color}" stroke="none"/>')
        cx, cy = x + 16, y + 19
        parts.append(f'<circle cx="{cx}" cy="{cy}" r="10" fill="white" fill-opacity="0.3"/>')
        parts.append(f'<text x="{cx}" y="{cy+4}" text-anchor="middle" font-size="11" font-weight="bold" fill="white">{stage_num}</text>')
        parts.append(f'<text x="{x+34}" y="{y+20}" dominant-baseline="central" font-size="12" fill="white">{name}</text>')
        # For post-send (group-last, no outgoing pair), show "含在上框"
        # instead of "未采集" — its cost is included in the preceding
        # 发送入口/发送(SMALL_DATA) box's occupancy.
        if comp == "umq" and col in (0, 2) and dur is None:
            dur_str = "含在上框"
        else:
            dur_str = fmt_us(dur)
        parts.append(f'<text x="{x+BOX_W-10}" y="{y+20}" dominant-baseline="central" text-anchor="end" font-size="11" font-weight="bold" fill="white" fill-opacity="0.85">{dur_str}</text>')
        parts.append(f'<text x="{x-5}" y="{y+20}" dominant-baseline="central" text-anchor="end" font-size="9" fill="{color}" font-weight="bold">{COMP_LABELS[comp]}</text>')

        if sub_items and h > 50:
            sub_y = y + 48
            for sub_name, _ in sub_items:
                sd = sub_dur(sub_name, col)
                parts.append(f'<text x="{x+10}" y="{sub_y}" dominant-baseline="central" font-size="9" fill="white" fill-opacity="0.75">├ {sub_name}</text>')
                parts.append(f'<text x="{x+BOX_W-10}" y="{sub_y}" dominant-baseline="central" text-anchor="end" font-size="9" fill="white" fill-opacity="0.6">{fmt_us(sd)}</text>')
                sub_y += 15

        next_row = row + 1
        max_rows = len(row_heights_4) if col in (1, 3) else len(row_heights)
        if next_row < max_rows:
            ax = x + BOX_W // 2
            ay1, ay2 = y + h, y + h + 8
            parts.append(f'<line x1="{ax}" y1="{ay1}" x2="{ax}" y2="{ay2}" stroke="{STROKE}" stroke-width="1.5"/>')
            parts.append(f'<polygon points="{ax-4},{ay2} {ax+4},{ay2} {ax},{ay2+4}" fill="{STROKE}"/>')

    # Wire arrows
    last_row_c0 = len(row_heights) - 1
    wire_y = row_y(0, last_row_c0) + 19
    wire_x1, wire_x2 = COL_X[0] + BOX_W, COL_X[1]
    wire_mid = (wire_x1 + wire_x2) / 2
    target_y = row_y(1, 0) + 19
    parts.append(f'<line x1="{wire_x1}" y1="{wire_y}" x2="{wire_mid-18}" y2="{wire_y}" stroke="{COMP_COLORS["urma"]}" stroke-width="2"/>')
    parts.append(f'<line x1="{wire_mid-18}" y1="{wire_y}" x2="{wire_mid-18}" y2="{target_y}" stroke="{COMP_COLORS["urma"]}" stroke-width="2"/>')
    parts.append(f'<line x1="{wire_mid-18}" y1="{target_y}" x2="{wire_x2-7}" y2="{target_y}" stroke="{COMP_COLORS["urma"]}" stroke-width="2"/>')
    parts.append(f'<polygon points="{wire_x2-7},{target_y-4} {wire_x2-7},{target_y+4} {wire_x2},{target_y}" fill="{COMP_COLORS["urma"]}"/>')
    parts.append(f'<rect x="{wire_mid-17}" y="{wire_y-30}" width="78" height="16" rx="3" fill="{COMP_COLORS["urma"]}"/>')
    parts.append(f'<text x="{wire_mid+22}" y="{wire_y-22}" text-anchor="middle" font-size="10" font-weight="bold" fill="white">④ URMA 未采集</text>')

    last_row_c2 = len(row_heights) - 1
    wire_y2 = row_y(2, last_row_c2) + 19
    wire_x1b, wire_x2b = COL_X[2] + BOX_W, COL_X[3]
    wire_midb = (wire_x1b + wire_x2b) / 2
    target_y2 = row_y(3, 0) + 19
    parts.append(f'<line x1="{wire_x1b}" y1="{wire_y2}" x2="{wire_midb-18}" y2="{wire_y2}" stroke="{COMP_COLORS["urma"]}" stroke-width="2"/>')
    parts.append(f'<line x1="{wire_midb-18}" y1="{wire_y2}" x2="{wire_midb-18}" y2="{target_y2}" stroke="{COMP_COLORS["urma"]}" stroke-width="2"/>')
    parts.append(f'<line x1="{wire_midb-18}" y1="{target_y2}" x2="{wire_x2b-7}" y2="{target_y2}" stroke="{COMP_COLORS["urma"]}" stroke-width="2"/>')
    parts.append(f'<polygon points="{wire_x2b-7},{target_y2-4} {wire_x2b-7},{target_y2+4} {wire_x2b},{target_y2}" fill="{COMP_COLORS["urma"]}"/>')
    parts.append(f'<rect x="{wire_midb-17}" y="{wire_y2-30}" width="78" height="16" rx="3" fill="{COMP_COLORS["urma"]}"/>')
    parts.append(f'<text x="{wire_midb+22}" y="{wire_y2-22}" text-anchor="middle" font-size="10" font-weight="bold" fill="white">⑭ URMA 未采集</text>')

    # Server recv → Server send
    last_row_c1 = len(row_heights_4) - 1
    sr_end_y = row_y(1, last_row_c1) + 19
    ss_start_y = row_y(2, 0) + 19
    parts.append(f'<line x1="{COL_X[1]+BOX_W}" y1="{sr_end_y}" x2="{COL_X[2]-7}" y2="{ss_start_y}" stroke="{STROKE}" stroke-width="1.5"/>')
    parts.append(f'<polygon points="{COL_X[2]-7},{ss_start_y-4} {COL_X[2]-7},{ss_start_y+4} {COL_X[2]},{ss_start_y}" fill="{STROKE}"/>')

    # Dashed column borders (reference SVG style)
    if not is_small:
        col_bottom = row_y(1, last_row_c1) + row_h(1, last_row_c1) + 10
        for i in range(4):
            bx = COL_X[i] - 15
            parts.append(f'<rect x="{bx}" y="45" width="280" height="{col_bottom-45}" rx="8" fill="none" stroke="#DDD" stroke-width="1" stroke-dasharray="4,3"/>')

    # Measured breakdown: e2e = server处理(T4-T3) + 通信栈(link_time) is the
    # four-point identity, so every number here is measured; None renders
    # as 未采集 and no ratio split is fabricated to fill the gap.
    if is_small:
        sum_y = H - 200
        sum_h = 120
    else:
        sum_y = 629
        sum_h = 95
    parts.append(f'<rect x="140" y="{sum_y}" width="1000" height="{sum_h}" rx="8" fill="white" stroke="#CCC" stroke-width="1"/>')
    parts.append(f'<text x="{W//2}" y="{sum_y+18}" text-anchor="middle" font-size="13" font-weight="bold" fill="#333">{summary_label}</text>')
    measured_items = [
        ("server处理 (T4-T3)", COMP_COLORS["brpc"], totals["brpc_server"], True),
        ("brpc client (e2e之外)", COMP_COLORS["brpc"], totals["brpc_client"], False),
        ("通信栈 (link_time)", COMP_COLORS["ubsocket"], totals["stage_total"], True),
    ]
    bar_x = 233
    for label, color, val, in_e2e in measured_items:
        parts.append(f'<rect x="{bar_x}" y="{sum_y+30}" width="11" height="11" rx="2" fill="{color}"/>')
        parts.append(f'<text x="{bar_x+18}" y="{sum_y+39}" dominant-baseline="central" font-size="11" fill="#333">{label}</text>')
        parts.append(f'<text x="{bar_x+32}" y="{sum_y+57}" text-anchor="middle" font-size="12" fill="#555">{fmt_us(val)}</text>')
        if val is not None and in_e2e and e2e > 0:
            parts.append(f'<text x="{bar_x+32}" y="{sum_y+76}" text-anchor="middle" font-size="13" font-weight="bold" fill="{color}">{val / e2e * 100:.1f}%</text>')
        elif val is not None:
            parts.append(f'<text x="{bar_x+32}" y="{sum_y+76}" text-anchor="middle" font-size="10" fill="#999">prof实测</text>')
        bar_x += 250
    note = (f'注: e2e={e2e:.0f}us P99={data.get("e2e",{}).get("p99",0):.0f}us'
            f'; 未采集=该路径无打点, 不做比例分摊'
            f'; 阶段值优先端点实测, 否则取 join 合并均值')
    if totals["read_wire"] is not None:
        note += f'; READ wire={totals["read_wire"]:.1f}us/offer (⊂通信栈, 同节点实测)'
    parts.append(f'<text x="{W//2}" y="{sum_y+sum_h-8}" text-anchor="middle" font-size="10" fill="#999">{note}</text>')

    # RPC four-point trace summary
    ts_y = sum_y + sum_h + 10
    ts_h = 80
    parts.append(f'<rect x="140" y="{ts_y}" width="1000" height="{ts_h}" rx="8" fill="white" stroke="#CCC" stroke-width="1"/>')
    parts.append(f'<text x="{W//2}" y="{ts_y+20}" text-anchor="middle" font-size="13" font-weight="bold" fill="#333">RPC 四点时延分解 ({data.get("e2e",{}).get("n",0)} RPCs joined)</text>')
    e2e_d = data.get("e2e", {})
    sp_d = data.get("server_prc", {})
    lt_d = data.get("link_time", {})
    wr_d = data.get("wire_resid", {})
    parts.append(f'<text x="190" y="{ts_y+45}" font-size="11" fill="#555">e2e (T2-T1):</text>')
    parts.append(f'<text x="290" y="{ts_y+45}" font-size="12" font-weight="bold" fill="#333">avg={e2e_d.get("avg",0):.0f}us  P50={e2e_d.get("p50",0):.0f}us  P99={e2e_d.get("p99",0):.0f}us  P999={e2e_d.get("p999",0):.0f}us</text>')
    parts.append(f'<text x="190" y="{ts_y+65}" font-size="11" fill="#555">server_prc (T4-T3):</text>')
    parts.append(f'<text x="320" y="{ts_y+65}" font-size="12" font-weight="bold" fill="#333">avg={sp_d.get("avg",0):.0f}us  P50={sp_d.get("p50",0):.0f}us  P99={sp_d.get("p99",0):.0f}us</text>')
    parts.append(f'<text x="660" y="{ts_y+45}" font-size="11" fill="#555">link_time:</text>')
    parts.append(f'<text x="740" y="{ts_y+45}" font-size="12" font-weight="bold" fill="#333">avg={lt_d.get("avg",0):.0f}us  P50={lt_d.get("p50",0):.0f}us  P99={lt_d.get("p99",0):.0f}us</text>')
    parts.append(f'<text x="660" y="{ts_y+65}" font-size="11" fill="#555">wire_resid:</text>')
    parts.append(f'<text x="750" y="{ts_y+65}" font-size="12" font-weight="bold" fill="#333">avg={wr_d.get("avg",0):.0f}us  P50={wr_d.get("p50",0):.0f}us  P99={wr_d.get("p99",0):.0f}us  nseg={req_nseg:.1f}</text>')

    parts.append('</svg>')
    svg_content = '\n'.join(parts)
    with open(output_path, 'w') as f:
        f.write(svg_content)
    layout = "small" if is_small else "bigdata"
    print(f"SVG written to {output_path} ({len(svg_content)} bytes, layout={layout})")


def main():
    parser = argparse.ArgumentParser(description="Generate RPC latency flowchart SVG from trace data")
    parser.add_argument("--client-rpc", required=True)
    parser.add_argument("--server-rpc", required=True)
    parser.add_argument("--client-pkt", required=True)
    parser.add_argument("--server-pkt", required=True)
    parser.add_argument("--prof-server", default=None)
    parser.add_argument("--prof-client", default=None)
    parser.add_argument("--output", default="/tmp/brpc/rpc_latency_flowchart.svg")
    parser.add_argument("--title-suffix", default="")
    args = parser.parse_args()

    join_output = run_join(args)
    data = parse_join_output(join_output)
    prof_server = {}
    prof_client = {}
    if args.prof_server:
        prof_server = parse_prof(args.prof_server)
    if args.prof_client:
        prof_client = parse_prof(args.prof_client)
    totals = calc_component_totals(data, prof_server, prof_client)
    server_stages = load_stage_occupancy(args.server_pkt)
    client_stages = load_stage_occupancy(args.client_pkt)
    generate_svg(data, prof_server, prof_client, totals, args.output,
                 args.title_suffix, server_stages, client_stages)


if __name__ == "__main__":
    main()
