#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Synthetic end-to-end test for join_rpc_trace.py phase-3 bridge and
phase-4 stage events (S rows)."""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
JOIN = os.path.join(HERE, "join_rpc_trace.py")

# brpc 6-col: point cid port fd ts_ns byte_cursor
client_rpc = "\n".join([
    "1\t100\t5000\t9\t1000000\t0",      # T1 cid100
    "2\t100\t5000\t9\t1500000\t300",    # T2 cid100, rsp ends at bc=300
    "1\t101\t5000\t9\t2000000\t0",      # T1 cid101
    "2\t101\t5000\t9\t2400000\t600",    # T2 cid101, rsp ends at bc=600
    "bad line",
    "1\t999\t5000\t9\t50\t0",           # cid999 T1/T2 on client only
    "2\t999\t5000\t9\t100\t100",        #   -> only_client
])
server_rpc = "\n".join([
    "3\t100\t8000\t7\t9000000\t1000",   # T3 cid100, req ends at bc=1000
    "4\t100\t8000\t7\t9200000\t0",      # T4 cid100
    "3\t101\t8000\t7\t9400000\t1500",   # T3 cid101, req ends at bc=1500
    "4\t101\t8000\t7\t9450000\t0",      # T4 cid101
    "3\t998\t8000\t7\t100\t50\textra",  # malformed -> bad
])
# ubs pkt: fd sn byte_cursor ts_ns
server_pkt = "\n".join([
    "7\t1\t500\t8900000",               # seg1 -> cid100
    "7\t2\t1000\t8950000",              # seg2 -> cid100 (bc == msg bc)
    "7\t3\t1500\t9300000",              # seg3 -> cid101
    "7\t4\t1800\t9999999",              # tail: in-flight, unassigned
])
client_pkt = "\n".join([
    "9\t5\t300\t1450000",               # rsp seg -> cid100
    "9\t6\t600\t2380000",               # rsp seg -> cid101
])

# ---------------------------------------------------------------------------
# Phase-4 fixture: one bigdata RPC (cid200), one offer per direction.
# req offer first_sn=100 (2 fragments, P rows share sn=100), fd_c=9 / fd_s=7
# rsp offer first_sn=50  (1 fragment),                          fd_c=9 / fd_s=7
# link = e2e(1000us) - sprc(200us) = 800us
client_rpc4 = "\n".join([
    "1\t200\t5000\t9\t1000000\t0",      # T1
    "2\t200\t5000\t9\t2000000\t300",    # T2 (rsp bc=300)
])
server_rpc4 = "\n".join([
    "3\t200\t8000\t7\t9000000\t1000",   # T3 (req bc=1000)
    "4\t200\t8000\t7\t9200000\t0",      # T4
])
# client log: req TX stage events (fd=9, sn=100) + rsp RX events (fd=9, sn=50)
#             + rsp P row
client_pkt4 = "\n".join([
    "S\t1\t9\t100\t1010000",            # TrySenderPost
    "S\t2\t9\t100\t1015000",            # FlushPendingOffer   (+5us)
    "S\t3\t9\t100\t1017000",            # UmqPostSend         (+2us)
    "S\t5\t9\t50\t1950000",             # rsp RX chain (client clock)
    "S\t6\t9\t50\t1954000",             #   (+4us)
    "S\t7\t9\t50\t1957000",             #   (+3us)
    "S\t4\t9\t50\t1969000",             #   (+12us READ wire)
    "S\t8\t9\t50\t1976000",             #   (+7us)
    "S\t9\t9\t50\t1977000",             #   (+1us)
    "S\t10\t9\t50\t1978000",            #   (+1us)
    "9\t50\t300\t1980000",              # rsp P row: delivered (+2us wake)
])
# server log: req RX stage events (fd=7, sn=100) + rsp TX events (fd=7, sn=50)
#             + req P rows (both fragments carry sn=100)
server_pkt4 = "\n".join([
    "S\t5\t7\t100\t8900000",            # HandleRxCtrl
    "S\t6\t7\t100\t8904700",            # DoReadOffer         (+4.7us)
    "S\t7\t7\t100\t8908500",            # UmqPostRead         (+3.8us)
    "S\t4\t7\t100\t8923500",            # ReadCqe             (+15us READ wire)
    "S\t8\t7\t100\t8931800",            # FinalizeIo          (+8.3us)
    "S\t9\t7\t100\t8932900",            # DeliverToRxQ        (+1.1us)
    "S\t10\t7\t100\t8933800",           # SendSimpleCtrl      (+0.9us)
    "S\t1\t7\t50\t9210000",             # rsp TX chain (server clock)
    "S\t2\t7\t50\t9214000",             #   (+4us)
    "S\t3\t7\t50\t9216000",             #   (+2us)
    "7\t100\t600\t8934000",             # req P row frag1 (delivered)
    "7\t100\t1000\t8936300",            # req P row frag2 (delivered, +2.5us wake)
])


# ---------------------------------------------------------------------------
# Phase-5 fixture: SMALL_DATA RPCs with batch range events + RxCqeData.
# Two RPCs share ONE posted batch per direction (KeepWrite coalescing):
#   req batch (client): SNs 200,201 (cid300, 2 segs) + 202 (cid301, 1 seg)
#     -> ONE UmqPostSend row with sn_count=3 (range [200,202])
#   rsp batch (server): SNs 201 (cid300) + 202 (cid301)
#     -> ONE UmqPostSend row with sn_count=2 (range [201,202])
# rsp SN values deliberately overlap the req range numerically to prove
# direction isolation (RX groups must NOT absorb TX range events).
# link: cid300 = 1000-200 = 800us; cid301 = 1000-50 = 950us
client_rpc5 = "\n".join([
    "1\t300\t5000\t9\t1000000\t0",      # T1 cid300
    "2\t300\t5000\t9\t2000000\t150",    # T2 (rsp bc=150)
    "1\t301\t5000\t9\t1100000\t0",      # T1 cid301
    "2\t301\t5000\t9\t2100000\t300",    # T2 (rsp bc=300)
])
server_rpc5 = "\n".join([
    "3\t300\t8000\t7\t9000000\t1000",   # T3 cid300 (req bc=1000)
    "4\t300\t8000\t7\t9200000\t0",      # T4
    "3\t301\t8000\t7\t9100000\t1500",   # T3 cid301 (req bc=1500)
    "4\t301\t8000\t7\t9150000\t0",      # T4
])
client_pkt5 = "\n".join([
    "S\t1\t9\t200\t1010000",            # req batch TrySenderPost (entry_sn=200)
    "S\t3\t9\t200\t1017000\t3",         # req batch UmqPostSend range [200,202]
    "S\t11\t9\t201\t1950000",           # cid300 rsp RX CQE dispatch
    "9\t201\t150\t1955000",             # cid300 rsp P row (delivered, +5us)
    "S\t11\t9\t202\t1960000",           # cid301 rsp RX CQE dispatch
    "9\t202\t300\t1965000",             # cid301 rsp P row (delivered, +5us)
])
server_pkt5 = "\n".join([
    "S\t11\t7\t200\t8900000",           # cid300 req seg1 RX CQE
    "7\t200\t500\t8905000",             #   delivered (+5us)
    "S\t11\t7\t201\t8910000",           # cid300 req seg2 RX CQE
    "7\t201\t1000\t8915000",            #   delivered (+5us)
    "S\t11\t7\t202\t8920000",           # cid301 req RX CQE
    "7\t202\t1500\t8925000",            #   delivered (+5us)
    "S\t1\t7\t201\t9300000",            # rsp batch TrySenderPost (entry_sn=201)
    "S\t3\t7\t201\t9305000\t2",         # rsp batch UmqPostSend range [201,202]
])


def write(d, name, content):
    p = os.path.join(d, name)
    with open(p, "w") as f:
        f.write(content + "\n")
    return p


def main():
    d = tempfile.mkdtemp(prefix="join_test_")
    c = write(d, "rpc_trace_c.log", client_rpc)
    s = write(d, "rpc_trace_s.log", server_rpc)
    cp = write(d, "ubs_pkt_trace_c.log", client_pkt)
    sp = write(d, "ubs_pkt_trace_s.log", server_pkt)
    csv = os.path.join(d, "out.csv")

    out = subprocess.run(
        [sys.executable, JOIN, "--client", c, "--server", s,
         "--client-pkt", cp, "--server-pkt", sp, "--top", "2", "--csv", csv],
        capture_output=True, text=True)
    print(out.stdout)
    if out.returncode != 0:
        print(out.stderr)
        return 1

    fails = []

    def check(name, cond):
        print(("PASS" if cond else "FAIL") + ": " + name)
        if not cond:
            fails.append(name)

    o = out.stdout
    check("joined 2 RPCs", "joined RPCs: 2" in o)
    check("only_client counted", "only_client=1" in o)
    check("req segs aligned to 2 cids", "req segs aligned to 2 cids" in o)
    check("tail segs inflight=1", "tail_segs_inflight=1" in o)
    # cid100: e2e=500us sprc=200us link=300us req_span=50us req_tail=50us
    # cid101: e2e=400us sprc=50us link=350us rsp_tail=20us
    check("top1 is cid101 (link 350us)", "101" in o.split("Top 2 slowest")[1].splitlines()[2])
    with open(csv) as f:
        lines = f.read().strip().splitlines()
    hdr = lines[0].split(",")
    check("csv header", hdr == ["cid", "e2e_us", "server_prc_us", "link_us",
                                "req_nseg", "req_span_us", "req_tail_us",
                                "rsp_nseg", "rsp_span_us", "rsp_tail_us",
                                "wire_resid_us", "stage_total_us", "unattr_us"])
    r100 = {k: v for k, v in zip(hdr, lines[1].split(","))}
    r101 = {k: v for k, v in zip(hdr, lines[2].split(","))}
    check("cid100 link=300", r100["link_us"] == "300.00")
    check("cid100 req_nseg=2", r100["req_nseg"] == "2")
    check("cid100 req_span=50us", r100["req_span_us"] == "50.00")
    check("cid100 req_tail=50us", r100["req_tail_us"] == "50.00")
    check("cid100 rsp_tail=50us", r100["rsp_tail_us"] == "50.00")
    check("cid100 wire_resid=200us", r100["wire_resid_us"] == "200.00")
    check("cid100 no stage events -> empty cols",
          r100["stage_total_us"] == "" and r100["unattr_us"] == "")
    check("cid101 link=350", r101["link_us"] == "350.00")
    check("cid101 req_nseg=1", r101["req_nseg"] == "1")
    check("cid101 req_span=0", r101["req_span_us"] == "0.00")
    check("cid101 req_tail=100us", r101["req_tail_us"] == "100.00")
    check("cid101 rsp_tail=20us", r101["rsp_tail_us"] == "20.00")

    # ------------------------------------------------------------------
    # Phase-4 stage events: one bigdata RPC, one offer per direction.
    # stage_total = TX(5+2) + RX(4.7+3.8+15+8.3+1.1+0.9+2.5)
    #             + TX(4+2) + RX(4+3+12+7+1+1+2) = 79.3us
    # unattr = link(800) - 79.3 = 720.7us
    c4f = write(d, "rpc4_c.log", client_rpc4)
    s4f = write(d, "rpc4_s.log", server_rpc4)
    cp4 = write(d, "pkt4_c.log", client_pkt4)
    sp4 = write(d, "pkt4_s.log", server_pkt4)
    csv4 = os.path.join(d, "out4.csv")
    out4 = subprocess.run(
        [sys.executable, JOIN, "--client", c4f, "--server", s4f,
         "--client-pkt", cp4, "--server-pkt", sp4, "--top", "1", "--csv", csv4],
        capture_output=True, text=True)
    if out4.returncode != 0:
        print(out4.stderr)
        fails.append("phase4 run")
    else:
        o4 = out4.stdout
        print(o4)
        check("phase4 joined 1 RPC", "joined RPCs: 1" in o4)
        check("stage table present", "Stage segments (per event-pair" in o4)
        check("4 offer groups matched", "4 offer groups matched" in o4)
        check("READ wire row present", "UmqPostRead->ReadCqe  [READ wire]" in o4)
        check("READ wire avg=13.50", "avg=    13.50" in o4)
        check("delivered wake row", "SendSimpleCtrl->delivered" in o4)
        check("waterfall req TX group", "Client Send (req TX) fd=9 offers=1" in o4)
        check("waterfall req RX group", "Server Recv (req RX) fd=7 offers=1" in o4)
        check("waterfall rsp TX group", "Server Send (rsp TX) fd=7 offers=1" in o4)
        check("waterfall rsp RX group", "Client Recv (rsp RX) fd=9 offers=1" in o4)
        check("waterfall offer first_sn=100", "offer first_sn=100" in o4)
        with open(csv4) as f:
            l4 = f.read().strip().splitlines()
        h4 = l4[0].split(",")
        r200 = {k: v for k, v in zip(h4, l4[1].split(","))}
        check("cid200 req_nseg=2 (frags share first_sn)",
              r200["req_nseg"] == "2")
        check("cid200 stage_total=79.30", r200["stage_total_us"] == "79.30")
        check("cid200 unattr=720.70", r200["unattr_us"] == "720.70")

    # backward-compat: 4/5-col logs, no pkt logs
    c4 = write(d, "rpc_trace_c4.log", "\n".join([
        "1\t100\t5000\t1000000",
        "2\t100\t5000\t1500000\t300",
    ]))
    s4 = write(d, "rpc_trace_s4.log", "\n".join([
        "3\t100\t8000\t9000000\t1000",
        "4\t100\t8000\t9200000",
    ]))
    out2 = subprocess.run([sys.executable, JOIN, "--client", c4, "--server", s4],
                          capture_output=True, text=True)
    check("legacy 4/5-col join works", "joined RPCs: 1" in out2.stdout
          and "300.00" in out2.stdout)

    # ------------------------------------------------------------------
    # Phase-5: batch range events + RxCqeData (SMALL_DATA path).
    # cid300 stage_total = reqTX(7) + reqRX(5+5) + rspTX(5) + rspRX(5) = 27
    # cid301 stage_total = reqRX(5) + rspRX(5) = 10 (batch non-head SNs:
    # range event only, no adjacent pair)
    c5f = write(d, "rpc5_c.log", client_rpc5)
    s5f = write(d, "rpc5_s.log", server_rpc5)
    cp5 = write(d, "pkt5_c.log", client_pkt5)
    sp5 = write(d, "pkt5_s.log", server_pkt5)
    csv5 = os.path.join(d, "out5.csv")
    out5 = subprocess.run(
        [sys.executable, JOIN, "--client", c5f, "--server", s5f,
         "--client-pkt", cp5, "--server-pkt", sp5, "--top", "2", "--csv", csv5],
        capture_output=True, text=True)
    if out5.returncode != 0:
        print(out5.stderr)
        fails.append("phase5 run")
    else:
        o5 = out5.stdout
        print(o5)
        check("phase5 joined 2 RPCs", "joined RPCs: 2" in o5)
        check("range events counted (client)", "1 range evts" in o5)
        check("RxCqeData->delivered row", "RxCqeData->delivered" in o5)
        check("batch-head TrySenderPost->UmqPostSend row",
              "TrySenderPost->UmqPostSend" in o5)
        check("10 groups matched (6 cid300 + 4 cid301)",
              "10 offer groups matched" in o5)
        # Direction isolation: rsp SN 201/202 overlap the client req range
        # [200,202] numerically; RX groups must NOT absorb UmqPostSend.
        check("no cross-direction UmqPostSend->RxCqeData",
              "UmqPostSend->RxCqeData" not in o5)
        check("no cross-direction RxCqeData->UmqPostSend",
              "RxCqeData->UmqPostSend" not in o5)
        check("waterfall batch range offer first_sn=201",
              "offer first_sn=201" in o5)
        with open(csv5) as f:
            l5 = f.read().strip().splitlines()
        h5 = l5[0].split(",")
        rows5 = {r[0]: r for r in (ln.split(",") for ln in l5[1:])}
        r300 = dict(zip(h5, rows5["300"]))
        r301 = dict(zip(h5, rows5["301"]))
        check("cid300 stage_total=27.00", r300["stage_total_us"] == "27.00")
        check("cid300 unattr=773.00", r300["unattr_us"] == "773.00")
        check("cid301 stage_total=10.00", r301["stage_total_us"] == "10.00")
        check("cid301 unattr=940.00", r301["unattr_us"] == "940.00")

    print("\n%s" % ("ALL PASS" if not fails else "FAILURES: %s" % fails))
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
