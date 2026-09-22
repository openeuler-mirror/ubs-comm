#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TX-STAT 日志分析工具 —— 解析 tx_stat_<pid>.log 的 key=value 行，
输出发送方向(post 提交侧 + cqe 完成侧)失败定界提示(含核心结论、提交侧/完成侧累计、桶级明细、Top-N 异常窗口与时间序列)。

注意: 发送方向两类埋点 ——
  A) post.* : UmqTxOps::PostSend 的 umq_post 提交失败(提交侧)。
  B) cqe.*  : tx poll 线程处理 TX CQE 完成异常(完成侧, 含对端 RNR/ACK 超时)。
RX 数据面(umq_poll 接收)不在统计范围内。

用法:
    python3 tx_stat_analyze.py <log_file> [--top N] [--series]

日志行格式示例:
    [TX-STAT] t=2026-08-04T20:15:43.123 uptime=10s win=1000ms post=1000/950 fail_ppm=50000 \
           post.eagain_all=50 post.timeout=10 cqe.rnr=3 cqe.ack_timeout=1
    [TX-STAT-TOTAL] t=... uptime=120s win=1000ms post=100000/99500 fail_ppm=5000 post.eagain_all=500 cqe.rnr=200 ...

PPM 说明: 比率以"每百万次中的发生次数"(Parts Per Million)表达，
    失败率(ppm) = 失败数 / 总数 × 1_000_000；975799 ppm ≈ 97.6%。
"""
import argparse
import re
import sys
from collections import defaultdict


# 定界规则：发送方向两类桶 —— 提交侧 post.* + 完成侧 cqe.*。顺序重要：更具体的规则优先匹配。
DECODE_RULES = [
    ("post.", "eagain_all",
     "发送队列满(信用池耗尽)",
     "本端发送队列/信用池无可用槽位，umq_post 全部被拒。需查信用归还是否受阻(对端/完成队列)。"),
    ("post.", "eagain_part",
     "发送队列接近满(部分可发)",
     "信用池接近耗尽，仍有部分 umq_post 成功；流量接近上限或本端收割略慢。"),
    ("post.", "timeout",
     "等对端授信回复超时(~1s)",
     "本端请求 credit 后 1s 内未收到对端回复。对端卡死/链路中断/CPU 被抢占。★核心故障信号。"),
    ("post.", "emlink",
     "无可用 jetty",
     "jetty 池耗尽，已入 TpWaitQueue 等待重试。连接数过多或 jetty 回收慢。"),
    ("post.", "enobufs_all",
     "qbuf 池全部耗尽",
     "jetty node 无可用 qbuf。qbuf 池过小或收割(释放)过慢。"),
    ("post.", "enobufs_part",
     "qbuf 池部分耗尽",
     "qbuf 接近耗尽，部分写成功。"),
    ("post.", "eflowctl",
     "UMQ 主动流控(-UMQ_ERR_EFLOWCTL)",
     "底层 UMQ 触发流控，通常与 credit 机制联动。"),
    ("post.", "no_badqbuf",
     "umq_post 返回无 bad_qbuf",
     "umq_post 异常返回但无 bad_qbuf 指针，底层异常，需查 URMA。"),
    ("post.", "other",
     "umq_post 其他错误",
     "未分类的 umq_post 错误(flagEIO)，查日志中的 mapped errno。"),
    # 完成侧(埋点 B): TX CQE 完成异常
    ("cqe.", "rnr",
     "对端 RQ 不足(RNR retry 耗尽)",
     "对端接收队列/RQE 不足或收割慢；查对端 RX 侧与链路。★核心"),
    ("cqe.", "ack_timeout",
     "对端未回 ACK",
     "对端卡死或链路异常，直接升级排查。★核心"),
    ("cqe.", "fc",
     "流控失败(FAKE_BUF_FC_ERR)",
     "底层流控失败，与 port cooldown / 流控日志联查。"),
    ("cqe.", "remote",
     "对端错误(REM_*)",
     "对端处理异常(resp_len/unsupported_req/op/access_abort)，查对端 URMA。"),
    ("cqe.", "local",
     "本端错误(LOC_*)",
     "本端完成错误(len/op/access)，查本端 URMA 与 buffer 生命周期。"),
    ("cqe.", "other",
     "其他完成异常",
     "unsupported opcode / flush / suspend / poison / 未知，结合 [UMQ_CQE] 日志定位。"),
]

POST_BUCKETS = ["eagain_all", "eagain_part", "timeout", "emlink",
               "enobufs_all", "enobufs_part", "eflowctl", "no_badqbuf", "other"]
CQE_BUCKETS = ["rnr", "ack_timeout", "fc", "remote", "local", "other"]


def parse_line(line):
    """解析一行，返回 (tag, ts, metrics_dict) 或 None。"""
    line = line.strip()
    if not line or line.startswith("[TX-STAT] version="):
        return None
    m = re.match(r"\[(TX-STAT(?:-TOTAL)?)\]\s+(.*)", line)
    if not m:
        return None
    tag = m.group(1)
    rest = m.group(2)
    parts = rest.split()
    metrics = {}
    ts = ""
    for p in parts:
        if p.startswith("t="):
            ts = p[2:]
        elif p.startswith("post="):
            total, ok = p[5:].split("/")
            metrics["post_total"] = int(total)
            metrics["post_ok"] = int(ok)
        elif "=" in p:
            k, v = p.split("=", 1)
            try:
                metrics[k] = int(v)
            except ValueError:
                metrics[k] = v
    return tag, ts, metrics


def decode(metrics):
    """根据非零桶给出定界提示列表 [(bucket, count, symptom, conclusion)]。"""
    hits = []
    for prefix, sub, symptom, concl in DECODE_RULES:
        for k, v in metrics.items():
            if not isinstance(v, int) or v <= 0:
                continue
            if k.startswith(prefix) and sub in k:
                hits.append((k, v, symptom, concl))
    return hits


def core_conclusion(agg):
    """核心结论: 发送方向(提交侧 post + 完成侧 cqe)是否健康 / 主导失败原因。"""
    lines = []
    # 提交侧
    ptotal = agg.get("post_total", 0)
    pok = agg.get("post_ok", 0)
    pfail = ptotal - pok
    if ptotal > 0 and pfail > 0:
        ppm = pfail * 1_000_000 // ptotal
        best = None
        for k in POST_BUCKETS:
            v = agg.get(f"post.{k}", 0)
            if v > 0 and (best is None or v > best[1]):
                best = (k, v)
        if best is not None:
            symptom = next((s for p, sub, s, _ in DECODE_RULES
                            if p == "post." and sub == best[0]), best[0])
            lines.append(f"提交侧【umq_post】异常: 失败率 {ppm:.0f} ppm，主导「{symptom}」({best[0]}={best[1]})。")
        else:
            lines.append(f"提交侧【umq_post】存在失败({ppm:.0f} ppm) 但无具体桶计数，需查日志 mapped errno。")
    # 完成侧
    cqe_total = sum(agg.get(f"cqe.{k}", 0) for k in CQE_BUCKETS)
    if cqe_total > 0:
        best = None
        for k in CQE_BUCKETS:
            v = agg.get(f"cqe.{k}", 0)
            if v > 0 and (best is None or v > best[1]):
                best = (k, v)
        if best is not None:
            symptom = next((s for p, sub, s, _ in DECODE_RULES
                            if p == "cqe." and sub == best[0]), best[0])
            lines.append(f"完成侧【TX CQE】异常: 累计 {cqe_total} 次，主导「{symptom}」({best[0]}={best[1]})。")
    if not lines:
        lines.append("发送方向健康: 提交侧(post)与完成侧(cqe)均无异常，可直接排除 UMSocket 发送方向。")
    return lines


def send_delimitation(agg):
    """发送方向定界推理(①②③ 证据链): 提交侧(post) + 完成侧(cqe)。返回结论行列表。"""
    lines = []
    ptotal = agg.get("post_total", 0)
    pok = agg.get("post_ok", 0)
    pfail = ptotal - pok
    if ptotal <= 0:
        return ["无发送数据，未触发发送方向定界。"]
    if pfail <= 0:
        lines.append("① 提交侧健康: umq_post 失败率为 0，本端提交无阻塞。")
    else:
        lines.append(f"① 提交侧失败总量: post 失败 {pfail} 次 (失败率 {pfail*1_000_000//ptotal:.0f} ppm) -> 本端 umq_post 提交受阻。")
        cnt = 0
        for k in POST_BUCKETS:
            v = agg.get(f"post.{k}", 0)
            if v <= 0:
                continue
            cnt += 1
            if cnt > 4:
                lines.append("  ...(其余非主导 post 桶见下方桶级明细)")
                break
            symptom = next((s for p, sub, s, _ in DECODE_RULES
                            if p == "post." and sub == k), k)
            lines.append(f"②-{k}: {symptom} = {v} 次")
    # 完成侧
    cqe_total = sum(agg.get(f"cqe.{k}", 0) for k in CQE_BUCKETS)
    if cqe_total <= 0:
        lines.append("③ 完成侧健康: TX CQE 完成异常为 0，已提交的 send 均正常完成。")
    else:
        lines.append(f"③ 完成侧异常总量: cqe 完成异常 {cqe_total} 次 -> 已提交但发送完成失败(对端/链路/本端完成路径)。")
        cnt = 0
        for k in CQE_BUCKETS:
            v = agg.get(f"cqe.{k}", 0)
            if v <= 0:
                continue
            cnt += 1
            if cnt > 4:
                lines.append("  ...(其余非主导 cqe 桶见下方桶级明细)")
                break
            symptom = next((s for p, sub, s, _ in DECODE_RULES
                            if p == "cqe." and sub == k), k)
            lines.append(f"④-{k}: {symptom} = {v} 次")
    return lines


def main():
    ap = argparse.ArgumentParser(description="TX-STAT 发送侧失败定界日志分析")
    ap.add_argument("log", help="tx_stat_<pid>.log 路径")
    ap.add_argument("--top", type=int, default=10, help="显示 Top-N 异常窗口 (默认 10)")
    ap.add_argument("--series", action="store_true", help="额外打印关键指标时间序列")
    args = ap.parse_args()

    windows = []
    total_line = None
    try:
        with open(args.log, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                pr = parse_line(line)
                if pr is None:
                    continue
                tag, ts, metrics = pr
                if tag == "TX-STAT-TOTAL":
                    total_line = (ts, metrics)
                else:
                    windows.append((ts, metrics))
    except OSError as e:
        print(f"无法读取文件 {args.log}: {e}", file=sys.stderr)
        return 1

    if not windows and total_line is None:
        print("未解析到任何 TX-STAT 数据行。", file=sys.stderr)
        return 1

    print("=" * 78)
    print("TX-STAT 发送方向(提交侧 post + 完成侧 cqe)失败定界分析报告")
    print("=" * 78)
    print(f"日志文件: {args.log}")
    print(f"采样窗口数: {len(windows)}")

    # 汇总：累计各桶
    agg = defaultdict(int)
    anomaly_windows = []
    for ts, m in windows:
        hits = decode(m)
        if hits:
            anomaly_windows.append((ts, m, hits))
        for k, v in m.items():
            if isinstance(v, int):
                agg[k] += v

    print(f"含异常的窗口数: {len(anomaly_windows)}")
    if windows:
        print(f"时间范围: {windows[0][0]}  ~  {windows[-1][0]}")
    print()

    # ---- 核心定界结论 ----
    print("-" * 78)
    print("[核心定界结论]")
    for ln in core_conclusion(agg):
        print(f"  {ln}")
    print()
    # ---- 分 · 发送侧定界推理 ----
    print("-" * 78)
    print("[分 · 发送侧定界推理]")
    for ln in send_delimitation(agg):
        print(f"  {ln}")
    print()

    # ---- 分1 · 提交侧 ----
    print("-" * 78)
    print("[分1 · 提交侧 umq_post 累计]")
    ptotal = agg.get("post_total", 0)
    pok = agg.get("post_ok", 0)
    if ptotal > 0:
        fail = ptotal - pok
        print(f"  post 总计: {ptotal}  成功: {pok}  失败: {fail}  "
              f"失败率: {fail*1_000_000//ptotal:.0f} ppm  "
              f"(PPM=每百万次失败数; {fail*1_000_000//ptotal:.0f} ppm≈{fail*100.0/ptotal:.1f}%)")
    for k in POST_BUCKETS:
        full = f"post.{k}"
        if agg.get(full, 0) > 0:
            print(f"  {full:24s} = {agg[full]}")
    print()

    # ---- 分1b · 完成侧 ----
    print("-" * 78)
    print("[分1b · 完成侧 TX CQE 累计]")
    cqe_total = sum(agg.get(f"cqe.{k}", 0) for k in CQE_BUCKETS)
    if cqe_total > 0:
        print(f"  cqe 完成异常总计: {cqe_total}")
    for k in CQE_BUCKETS:
        full = f"cqe.{k}"
        if agg.get(full, 0) > 0:
            print(f"  {full:22s} = {agg[full]}")
    if cqe_total == 0:
        print("  完成侧健康: 无 TX CQE 完成异常。")
    print()

    # ---- 分2 · 桶级定界明细 ----
    print("-" * 78)
    print("[分2 · 桶级定界明细] (基于累计非零桶)")
    if total_line:
        print(f"  (含 [TX-STAT-TOTAL] 进程累计行 @ {total_line[0]})")
    all_hits = decode(dict(agg))
    if not all_hits:
        print("  未观察到异常桶 —— 发送侧健康。")
    else:
        seen = set()
        for bucket, count, symptom, concl in all_hits:
            key = (symptom, bucket)
            if key in seen:
                continue
            seen.add(key)
            print(f"  • {symptom}  (累计 {count})")
            print(f"      触发桶: {bucket}")
            print(f"      定界: {concl}")
    print()

    # ---- Top-N 异常窗口 ----
    if anomaly_windows:
        print("-" * 78)
        print(f"[Top-{args.top} 异常窗口] (按 fail_ppm 降序)")
        ranked = sorted(anomaly_windows, key=lambda x: x[1].get("fail_ppm", 0), reverse=True)
        for ts, m, hits in ranked[:args.top]:
            print(f"  {ts}  fail_ppm={m.get('fail_ppm',0)}  post={m.get('post_total',0)}/{m.get('post_ok',0)}")
            for bucket, count, symptom, _ in hits:
                print(f"      {bucket}={count}  <- {symptom}")
        print()

    # ---- 时间序列 ----
    if args.series and windows:
        print("-" * 78)
        print("[关键指标时间序列]")
        print(f"  {'时间':<24s} {'fail_ppm':>9s} {'eagain':>8s} {'timeout':>8s} {'rnr':>7s} {'ack_to':>7s}")
        for ts, m in windows:
            eagain = m.get("post.eagain_all", 0) + m.get("post.eagain_part", 0)
            timeout = m.get("post.timeout", 0)
            rnr = m.get("cqe.rnr", 0)
            ack_to = m.get("cqe.ack_timeout", 0)
            if m.get("fail_ppm", 0) == 0 and eagain == 0 and timeout == 0 and rnr == 0 and ack_to == 0:
                continue
            print(f"  {ts:<24s} {m.get('fail_ppm',0):>9d} {eagain:>8d} {timeout:>8d} {rnr:>7d} {ack_to:>7d}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
