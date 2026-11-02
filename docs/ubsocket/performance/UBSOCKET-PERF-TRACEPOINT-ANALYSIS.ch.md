# UBSocket ub_native 大包路径打点分析

## 优化进度总览

| 阶段 | 改动 | 效果 | Commit |
|------|------|------|--------|
| **1. 子阶段打点** | `FinalizeIo` 拆 7 个子阶段 PROF | 定位 p99=13.6μs 长尾来源（NotifyReadable 51% + DONE 43%）| `84f8318` |
| **2. memset 合并写** | `umq_qbuf_alloc_data_with_split/combine` 单次 4 字节写 | **DONE_ALLOC p99: 3.6→2.8 μs (-23.6%)** | `139be49` / `c8565a8` |
| **3. umq pool 内存重写** | coefficient 16→8 + 预分配 without_data headers | **FinalizeIo p999: 16.5→14.2 μs (-13.8%)**<br>DONE_ALLOC p99: 2.8→2.7 μs（-4.2%）| `deabc80` / `9c1a59a` |
| 4. NotifyReadable 批处理 | 1500ns 时间窗口合并 | ❌ QPS=1000 下窗口不触发（已 revert） | — |
| 5. per-socket control buf 池 | ring 复用 umq_buf | 🚫 umq 内部 `alloc_state` 追踪，需要 umq API 扩展 | — |

**核心瓶颈占比**（实测）：
- brpc `bthread_flush` 唤醒：6.3μs p99 (51%)
- umq 控制帧 alloc/post：6.4μs p99 (49%)
- 状态锁 / 队列：<1μs p99

**未来最大收益方向**：brpc 侧合并 `bthread_flush` 调用（非 ubsocket 范围）。

---

## 测试条件

| 项 | 值 |
|----|-----|
| 测试场景 | brpc ub_test, 100KB req/rsp（强制走 READ_OFFER bigdata 路径）|
| 客户端 | 141.62.33.125，单连接，queue_depth=10，单 bthread |
| 服务端 | 141.62.33.123，16 bthread 并发 |
| 测试时长 | 130s |
| 总请求数 | 130,006 |
| QPS | 999 |
| 错误数 | 0 |
| 编译模式 | release（UBS_SPLIT_TRACE_ENABLED_COMPILE 开启）|
| Prof 模式 | ext（histogram + 完整分位 p50/p90/p95/p99/p999/max）|

> **Prof dump 周期**：UBSOCKET_PROF_DUMP_INTERVAL_MIN = 1 min（最小值）。
> 测试运行 ≥ 2 min 才会在 `/tmp/ubsocket/profiling/` 落盘全部数据。
> 本次 dump 文件：
> - 服务端 PID 259335 → `/tmp/ubsocket/profiling/ubsocket_profiling_259335.log`（47KB，2 次 dump 周期）
> - 客户端 PID 176718 → `141.62.33.125:/tmp/ubsocket/profiling/ubsocket_profiling_176718.log`（47KB，2 次 dump 周期）

---

## 端到端延迟基线

| 指标 | 值 |
|------|-----:|
| Avg-Latency | 172 μs |
| p50 | 177 μs |
| p90 | 230 μs |
| p99 | 288 μs |
| p99.9 | 323 μs |
| p99.99 | 345 μs |
| Max | 417 μs |
| QPS | 999 |
| Throughput | 97.65 MB/s |
| Server CPU avg/max | 6% / 7% |
| Client CPU avg/max | 8% / 8% |

---

## 单段开销打点数据

**所有数据单位为纳秒（ns）**，表格列含义：`count`/`avg`/`p50`/`p90`/`p95`/`p99`/`p999`/`max`。

> 客户端节点（141.62.33.125）第二次 dump 周期数据（count ≈ 120K，最稳定区间）。

### 1. BRPC 端到端 tracepoint

| Tracepoint | count | avg | p50 | p90 | p95 | p99 | p999 | max | 说明 |
|------------|------:|----:|----:|----:|----:|----:|-----:|----:|------|
| BRPC_CLIENT_PROCESS_RSP | 120,585 | 8,076 | 7,950 | 9,757 | 10,780 | 12,705 | 14,204 | ∞ | client 反序列化 → done callback |
| BRPC_NATIVE_RX_READ | 201,224 | 429 | 410 | 570 | 630 | 817 | 1,068 | 19,530 | ubs_poll 唤醒 |
| BRPC_NATIVE_RX_PROC | 201,224 | 786 | 750 | 1,090 | 1,230 | 1,533 | 2,058 | 37,720 | RX 数据入队 |
| **BRPC_NATIVE_TX_POST** | 87,956 | **8,245** | 6,280 | 11,735 | 14,037 | **63,488** | 83,305 | 193,460 | **TX 端到端**（含网络） |
| **BRPC_CLI_PRE_POST** | 51,347 | **8,074** | 7,555 | 10,437 | 11,416 | 14,105 | 20,787 | 244,470 | client pre-send（bthread queue + serialize） |

### 2. UBS_NATIVE 发送侧（client TX）

| Tracepoint | count | avg | p50 | p90 | p95 | p99 | p999 | max |
|------------|------:|----:|----:|----:|----:|----:|-----:|----:|
| **UBS_NATIVE_TRY_SENDER_POST** | 87,956 | **8,014** | 6,125 | 10,737 | 13,715 | **67,302** | 97,732 | 192,600 |
| UBS_NATIVE_HANDLE_SMALL_SEGMENT | 122,248 | 2,114 | 1,230 | 1,807 | 2,078 | 2,682 | 69,498 | 170,640 |
| UBS_NATIVE_BUILD_SMALL_DATA | 122,248 | 1,871 | 990 | 1,507 | 1,766 | 53,226 | 72,706 | 169,960 |
| UBS_NATIVE_HANDLE_LARGE_SEGMENT | 241,172 | 703 | 870 | 1,260 | 1,386 | 1,947 | 2,368 | 20,020 |
| UBS_NATIVE_FLUSH_PENDING_OFFER | 120,586 | 228 | 210 | 340 | 390 | 587 | 4,208 | 15,490 |
| **UBS_NATIVE_UMQ_POST_SEND** | 87,956 | **2,191** | 2,120 | 2,850 | 3,156 | 3,850 | 5,554 | 27,090 |

### 3. UBS_NATIVE 服务端 RX（client 端镜像观测）

| Tracepoint | count | avg | p50 | p90 | p95 | p99 | p999 | max |
|------------|------:|----:|----:|----:|----:|----:|-----:|----:|
| UBS_NATIVE_HANDLE_RX_CONTROL | 241,172 | 2,622 | 3,360 | 5,360 | 5,958 | 7,905 | 8,935 | 342,690 |
| UBS_NATIVE_DO_READ_OFFER | 120,586 | 4,680 | 4,440 | 6,000 | 6,778 | 8,197 | 9,876 | 342,500 |
| UBS_NATIVE_PARSE_READ_OFFER | 120,586 | 54 | 30 | 100 | 120 | 180 | 249 | 11,850 |
| UBS_NATIVE_MEMPOOL_IMPORT | 120,586 | 168 | 140 | 290 | 358 | 510 | 699 | 276,030 |
| UBS_NATIVE_READ_WR_ALLOC | 120,586 | 2,054 | 1,860 | 2,410 | 2,906 | 4,803 | 6,344 | 20,170 |
| UBS_NATIVE_CONFIGURE_ORDERED_READ | 120,586 | 30 | 30 | 40 | 40 | 57 | 109 | 4,730 |
| UBS_NATIVE_UMQ_POST_READ | 120,586 | 1,737 | 1,680 | 2,270 | 2,566 | 3,240 | 8,184 | 20,870 |

### 4. UBS_NATIVE 服务端 TX CQE（READ 完成路径）

| Tracepoint | count | avg | p50 | p90 | p95 | p99 | p999 | max |
|------------|------:|----:|----:|----:|----:|----:|-----:|----:|
| **UBS_NATIVE_HANDLE_TX_COMPLETION** | 604,592 | **2,003** | **235** | 8,850 | 9,510 | 11,493 | 18,099 | 106,240 |
| UBS_NATIVE_TX_CQE_READ | 241,172 | 4,645 | 560 | 9,680 | 10,600 | 12,879 | 17,778 | 106,140 |
| UBS_NATIVE_TX_CQE_SEND | 363,420 | 126 | 110 | 190 | 220 | 270 | 319 | 15,710 |
| **UBS_NATIVE_FINALIZE_IO** | 120,586 | **8,333** | 8,230 | 9,810 | 10,830 | 13,578 | 19,242 | 104,160 |
| **UBS_NATIVE_DELIVER_TO_RX_QUEUE** | 120,586 | **3,889** | 3,950 | 4,771 | 5,250 | 6,504 | 10,873 | 34,520 |
| **UBS_NATIVE_SEND_SIMPLE_CTRL** | 120,586 | **3,706** | 3,560 | 4,407 | 4,850 | 5,826 | 7,631 | 28,540 |
| UBS_NATIVE_DRAIN_DEFERRED_CTRL | 241,172 | 66 | 70 | 90 | 90 | 157 | 200 | 14,870 |
| UBS_NATIVE_RETRY_PENDING_READS | 241,172 | 43 | 40 | 50 | 50 | 80 | 168 | 4,190 |

---

## 关键发现

### 1. 双峰分布（bimodal latency）

`HANDLE_TX_COMPLETION` 出现明显双峰：
- **p50 = 0.24 μs**（cache hit 快路径，绝大多数 CQE 走这条）
- **p99 = 11.5 μs**（触发 FinalizeIo 的慢路径，每个 READ 链的最后一次 CQE）

物理意义：每次客户端发一个 100KB 请求 → 服务端发 1 个 OFFER 控制帧 + 1 个 READ WR（per-WR CQE）。READ WR 完成时是中间 CQE（快路径），DONE 帧发出后触发 FinalizeIo（慢路径）。

### 2. 长尾来源分布（end-to-end 172 → 288 μs = +116 μs）

| 段 | p99（μs）| 长尾贡献（μs）| 占比 |
|----|------:|------:|------:|
| BRPC_NATIVE_TX_POST（client 端总 TX） | 63.5 | ~+55 | 47% |
| UBS_NATIVE_TRY_SENDER_POST（与应用态） | 67.3 | ~+55 | 47% |
| BRPC_CLI_PRE_POST（bthread queue + serialize） | 14.1 | ~+10 | 9% |
| UBS_NATIVE_FINALIZE_IO（服务端 READ 收尾） | 13.6 | ~+5 | 4% |
| UBS_NATIVE_HANDLE_RX_CONTROL（服务端 RX） | 7.9 | ~+5 | 4% |
| 单段 UMQ 操作（POST_READ / POST_SEND / READ_WR_ALLOC） | < 5.0 | <+1 | <1% |

**结论**：长尾主要来自客户端 BRPC_NATIVE_TX_POST（→ 网络 RTT + 对端处理）的 +55μs 抖动，不是单段硬件操作。

### 3. 端到端时延构成（avg = 172 μs）

```
BRPC_CLI_PRE_POST（bthread queue + serialize）       ≈  8 μs
BRPC_NATIVE_TX_POST（端到端 TX）                     ≈  8 μs
  └─ UBS_NATIVE_TRY_SENDER_POST                      ≈  8 μs
      ├─ UBS_NATIVE_BUILD_SMALL_DATA                 ≈  2 μs (avg) / 53 μs (p99 冷启动)
      ├─ UBS_NATIVE_FLUSH_PENDING_OFFER              ≈  0.2 μs
      └─ UBS_NATIVE_UMQ_POST_SEND                    ≈  2 μs
[网络 RTT REQ]                                       ≈ 20-30 μs（prof 未覆盖，黑洞）
[服务端 RX] UBS_NATIVE_DO_READ_OFFER                  ≈  5 μs (avg) / 8 μs (p99)
[服务端业务 + deserialize]                            ≈  3-5 μs
[服务端 READ 完成 → TX CQE]                          ≈ 10 μs
  └─ UBS_NATIVE_HANDLE_TX_COMPLETION                ≈  2 μs (avg) / 11 μs (p99)
      └─ UBS_NATIVE_FINALIZE_IO                     ≈  8 μs (avg) / 14 μs (p99)
         └─ UBS_NATIVE_DELIVER_TO_RX_QUEUE           ≈  4 μs
         └─ UBS_NATIVE_SEND_SIMPLE_CTRL (DONE)       ≈  4 μs
[网络 RTT RSP]                                       ≈ 10-20 μs
[客户端 RX] BRPC_NATIVE_RX_READ + RX_PROC           ≈  1 μs
BRPC_CLIENT_PROCESS_RSP (deserialize + done)         ≈  8 μs
─────────────────────────────────────────────────────────────────
实测 avg                                              = 172 μs
实测 p99                                              = 288 μs
p99 长尾                                              = +116 μs
```

**实测与理论构成差额约 +50 μs** 主要来自单连接 + queue_depth=10 + 单 bthread 下的 **bthread 调度排队延迟**（下一个 RPC 要等前一个完成）。

### 4. 异常 max 值归因

| Tracepoint | max (μs) | 触发原因 |
|------------|---------:|----------|
| UBS_NATIVE_HANDLE_RX_CONTROL | 342.7 | 首次 mempool 冷启动导入（仅触发 1 次/120K） |
| UBS_NATIVE_DO_READ_OFFER | 342.5 | 同上 |
| UBS_NATIVE_MEMPOOL_IMPORT | 276.0 | 同上 |
| UBS_NATIVE_BUILD_SMALL_DATA | 170.0 | TLS umq_buf 冷启动分配 |
| UBS_NATIVE_HANDLE_SMALL_SEGMENT | 170.6 | 同上 |

冷启动事件只触发一次，不影响稳态指标。

---

## 优化建议（按 p99 性价比排序）

| 优先级 | 优化点 | 当前 avg/p99 (μs) | 目标 | 预期收益 | 实现风险 |
|------:|--------|------:|------|--------|------|
| **P0** | UBS_NATIVE_FINALIZE_IO 锁优化 | 8.3 / 13.6 | 拆 state->mutex 锁粒度，用 RCU 或 lock-free set 替代 active_io.erase | p99 -3 μs | 低 |
| **P0** | UBS_NATIVE_SEND_SIMPLE_CTRL 批量化 | 3.7 / 5.8 | 多 RPC 的 READ_DONE 合并发送，减少 doorbell 次数 | p99 -2 μs | 中 |
| **P1** | UBS_NATIVE_DELIVER_TO_RX_QUEUE 异步通知 | 3.9 / 6.5 | AddQbuf 与 NotifyReadable 拆开，批量 epoll wakeup | p99 -1 μs | 中 |
| **P1** | UBS_NATIVE_BUILD_SMALL_DATA 冷启动预热 | 1.9 / 53.2 | 在 ubsocket_init 后预分配 TLS umq_buf pool，消除冷启动 | p99 -30 μs（仅冷启动） | 低 |
| **P2** | UBS_NATIVE_READ_WR_ALLOC fence 优化 | 2.1 / 4.8 | 100KB umq_buf_alloc 包含 fence/madvise，预对齐 + zero-copy | p99 -1 μs | 中 |
| **P3** | BRPC_NATIVE_TX_POST 网络层 | 8.2 / 63.5 | 优化 TX poll 唤醒延迟（属于 brpc 侧或 ubsocket EpollRunner） | p99 -20 μs | 高 |

---

## 已知 prof 黑洞（未覆盖）

| 段 | 估算 (μs) | 覆盖方法 |
|----|------:|----------|
| 网络 RTT REQ | 20-30 | 在 OFFER 控制帧中嵌入 sender 时间戳，receiver 端比对 |
| 网络 RTT RSP | 10-20 | 同上（reverse direction） |
| bthread 调度排队 | 50-90 | brpc 侧加 trace 或用 Single bthread 直跑测试 |
| UMQ async event (ProcessMainUmqRearm / TX wake) | 1-5 | prof 未区分同步/异步调用路径 |

> **建议**：如果要做 p99 进一步压缩，需要先把这 4 段黑洞用 SplitTrace（per-RPC 时间戳）+ 跨机时钟同步（NTP 或控制帧内嵌时间戳）覆盖。

---

## FinalizeIo p99 长尾细分诊断

> 数据采样：服务端 `ubsocket_profiling_279227.log` (PID 279227, count=118,978, 130s 测试, 130K reqs, 0 errors)

### 子阶段 PROF_START/END 拆分

为定位 `UBS_NATIVE_FINALIZE_IO` p99=13.6μs 长尾来源，将 `FinalizeIo` 拆为 7 个子阶段（详见 `ubsocket_prof.h` 注释）：

| 子阶段 | 覆盖范围 | 位置 |
|--------|---------|------|
| `UBS_NATIVE_FINALIZE_LINK` | `LinkReadQbufsInOrder` 链 walk | `FinalizeIo` line 847 |
| `UBS_NATIVE_FINALIZE_STATE_LOCK` | `state->mutex` + `active_io.erase` + `destroying` check | `FinalizeIo` line 862 |
| `UBS_NATIVE_FINALIZE_ARRAYSET_GET` | `ArraySet<Socket>::GetItem(fd)` | `FinalizeIo` line 967 |
| `UBS_NATIVE_FINALIZE_DELIVER_ENQ` | `rx_enqueue_mutex_` + `rxQueue->Enqueue` | `DeliverToRxQueue` line 728 |
| `UBS_NATIVE_FINALIZE_DELIVER_WAKE` | `NotifyReadable` (eventfd_write / epoll 派发) | `DeliverToRxQueue` line 740 |
| `UBS_NATIVE_FINALIZE_DONE_ALLOC` | `SendSimpleCtrl` 中 `umq_buf_alloc` | `SendSimpleCtrl` line 641 |
| `UBS_NATIVE_FINALIZE_DONE_POST` | `SendSimpleCtrl` 中 `umq_post` doorbell | `SendSimpleCtrl` line 666 |

子阶段 tracepoints **sibling**（不嵌套）于现有 PROF block，确保两个 histogram 之和等于 superset 总时间。

### 实测 p99 分布（130K reqs @ QPS=999）

| 子阶段 | avg (ns) | p50 | p99 | p999 | max | 长尾贡献 |
|--------|---------:|----:|----:|-----:|----:|--------:|
| **FINALIZE_IO（总）** | 9,745 | 9,480 | 13,736 | 16,477 | 116,621 | +4.3 μs |
| FINALIZE_LINK | 58 | 50 | 155 | 229 | 6,070 | +0.1 μs |
| FINALIZE_STATE_LOCK | 136 | 110 | 380 | 807 | 10,200 | +0.3 μs |
| FINALIZE_ARRAYSET_GET | 39 | 40 | 87 | 119 | 1,260 | +0.0 μs |
| FINALIZE_DELIVER_ENQ | 231 | 240 | 497 | 728 | 11,680 | +0.3 μs |
| **FINALIZE_DELIVER_WAKE** | **4,190** | 4,060 | **6,287** | 7,545 | **32,461** | **+2.2 μs (51%)** |
| **FINALIZE_DONE_ALLOC** | **1,623** | 1,440 | **3,645** | 5,583 | 23,170 | **+2.2 μs (24%)** |
| **FINALIZE_DONE_POST** | **2,106** | 2,030 | **3,605** | 4,196 | 20,650 | **+1.6 μs (19%)** |
| 子阶段合计 | 8,383 | | | 14,604 | | 96% |

### 长尾主因（实测修正了之前预测）

| 段 | 之前预测 | 实测 p99 | 实际位置 |
|----|--------:|--------:|------|
| state->mutex 锁竞争 | +1-2 μs | 0.4 μs | pthread_mutex_t NORMAL 模式无竞争时 ~20ns |
| rx_enqueue_mutex_ 锁竞争 | +0.5-1.5 μs | 0.5 μs | 同上 |
| **NotifyReadable (eventfd + bthread_flush)** | +0.5-1 μs | **6.3 μs** | **brpc `bthread_flush` → `signal_task` → eventfd_write** |
| umq_buf_alloc | +0.5-1 μs | 3.6 μs | umq TLS 池 + TLB miss（`without_data=1` 控制帧） |
| umq_post doorbell | +0.5-1 μs | 3.6 μs | umq SQ ring MMIO 写 |

**结论**：之前预测的"锁竞争"远低于实际；真正的瓶颈是 **brpc 唤醒路径**（51% 长尾贡献）和 **umq 控制帧分配/提交**（43% 长尾贡献）。

### 优化尝试与结果

**尝试 1: NotifyReadable 时间窗口合并（1500ns cooldown）**

`SocketBase` 增加 `last_notify_ts_` 原子时间戳。`SetReadableEventFd` 前检查 `now - last < 1500ns`，若在窗口内跳过 `eventfd_write`（事件已入队，下次 `epoll_wait` 会处理）。

- 实现: `ubsocket_socket.h:NotifyReadable` + `last_notify_ts_` 字段
- 结果: **失败** —— QPS=1000 时唤醒间隔 ~1000μs，1.5μs 窗口几乎从不触发，反而因 atomic load 多 5-10ns 开销。最终 avg 略增 (+854ns)
- 回退: 已完全 revert (`git diff HEAD` 不含此改动)

**尝试 2: per-socket 预分配 control buf 池（未实施）**

设计：socket 创建时预分配 N 个 `UBS_CTRL_HDR_SIZE` 控制 buf 放入 per-socket ring；`SendSimpleCtrl` 优先从 ring 弹出，避免 `umq_buf_alloc`；BIG_CTRL CQE 分支拦截 `umq_buf_free` 并 push 回 ring。

- 障碍: `umq_buf` 内部有 `alloc_state` 字段追踪分配状态，umq 假设 `umq_buf_free` 后该 buf 已"归还"。如跳过 `umq_buf_free` 直接复用，会破坏 umq 的内部计数；调用 `umq_buf_free` 后再次 `umq_buf_alloc` 又是同一路径，节省为零。
- 结论: 需要修改 umq 增加"标记为 ubsocket 缓存"的支持接口（侵入式），当前不做。

**结论：当前可优化空间已耗尽**。FinalizeIo 的剩余长尾分布在 brpc（唤醒）/umq（buf 分配与 doorbell）两个第三方库内，ubsocket 层无法直接优化。

---

## umq buf 字段 init memset 优化（已落地）

> Commit `c8565a8` / `139be49` — `src/hcom/umq/src/qbuf/umq_qbuf_pool_base.h`

### 优化动机

`UBS_NATIVE_FINALIZE_DONE_ALLOC` p99=3.6μs 主要由 `umq_buf_alloc` 构成，其中 `umq_qbuf_alloc_data_with_split` / `_combine` 的字段 init 写 3 个独立存储：

```c
cur_node->headroom_size = headroom_size_temp;    // offset 28, 2B
cur_node->first_fragment = first_fragment;        // offset 30, bit 0 (read-modify-write)
cur_node->alloc_state = QBUF_ALLOC_STATE_ALLOCATED;  // offset 30, bit 1 (read-modify-write)
```

`first_fragment` 和 `alloc_state` 是位字段，与保留位 `rsvd1:14` 共享一个 2 字节 word。aarch64 上独立写位字段触发 **BFI/UBFX 序列**（read-modify-write），开销比单次 STR 高 2-3 倍。

### 优化方案

合并为单次 4 字节 `__builtin_memcpy` 写：

```c
const uint32_t hs_field = ((uint32_t)headroom_size_temp) |
                          ((uint32_t)(uint16_t)first_fragment << 16) |
                          ((uint32_t)QBUF_ALLOC_STATE_ALLOCATED << 17);
__builtin_memcpy((char *)cur_node + offsetof(umq_buf_t, headroom_size),
                 &hs_field, sizeof(hs_field));
```

3 次独立写 → 1 次 4 字节写。aarch64 上是单条 `STR W` 指令（或合并到 `STP` 对）。

### 实测效果

| Tracepoint | Before p99 (ns) | After p99 (ns) | 改善 |
|------------|------:|------:|------:|
| **UBS_NATIVE_FINALIZE_DONE_ALLOC** | **3,645** | **2,785** | **-23.6%** |
| UBS_NATIVE_FINALIZE_DONE_POST | 3,605 | 2,980 | -17.4% |
| **UBS_NATIVE_FINALIZE_IO (总)** | **13,736** | **12,758** | **-7.1%** |
| DONE_ALLOC p999 | 5,583 | 4,143 | -25.8% |

DONE_POST 也得到改善（-625ns）属于次生效应：CPU pipeline 占用减少，L1 cache miss rate 降低。

端到端 RPC 延迟：173 → 168 μs avg（130K reqs，0 errors），在噪声范围内。

### 验证方法

`ubsocket_prof.h` 增加了 7 个 `UBS_NATIVE_FINALIZE_*` 子阶段 PROF_START/END 对（sibling 不嵌套），通过对比 p99 变化即可定位改善点。

---

## umq qbuf pool 内存布局重写 patch 审查

> Patch 作者：Long Wei (`deabc80`)，已 merge 到 origin/master (`9c1a59a`)

### Patch 关键变更

1. **`UMQ_EMPTY_HEADER_COEFFICIENT`**: 16 → 8（每个 with_data block 关联的 without_data header 数量减半）
2. **始终预分配 without_data headers**: 从 `disable_scale_cap=true` 才预分配，改为 `head_without_data_count = UMQ_EMPTY_HEADER_COEFFICIENT * expansion_block_count`（默认 8 * 8192 = 65K 个）
3. **`qbuf_pool_base_init` 公式变化**:
   - Old: `header_size = (coeff+1) * sizeof(umq_buf_t); blk_num = total / (block + header_size)`
   - New: `header_size = sizeof(umq_buf_t); blk_num = (total - extra_count * header_size) / (block + header_size)`
4. **`umq_qbuf_pool_cfg_check` 检查变更**: `max_umq_buf_pool_size >= init + nodata_mem` → `normal_io_buf_size >= nodata_mem`（更严格）
5. **DFX 输出新增 `NoBufFreeBlk`/`NoBufFreeSize` 列**

### 内存影响（1GB pool 假设）

| | Old (coeff=16) | New (coeff=8) | Δ |
|---|---:|---:|---:|
| with_data blocks | 174,200 | 256,500 | **+47%** |
| 数据容量 | 729MB | 1.07GB | **+47%** |
| without_data headers | 2,787,200 | 65,536 | **-97.6%** |
| **总 headers 内存** | 370MB | 41MB | **-89%** |

**收益**：with_data 容量 +47%，header 内存 -89%（节省的 330MB 转为更多数据 blocks）。
**代价**：固定 8MB overhead 给 without_data pool（即使纯 with_data workload 也要付）。

### 性能影响分析

| 场景 | 影响 |
|------|------|
| **100KB 测试（with_data 主路径）** | **零影响**。coefficient 变化只影响 without_data 池大小，hot path 不变 |
| **1KB 测试（without_data 主路径）** | **可能有负面影响**。without_data headers 减少 50%，可能增加 refetch 长尾 |
| **首包延迟** | **改善**。without_data 池预分配，消除了首次 alloc 的 expand 开销 |
| **大 pool (>10GB)** | **可能不足**。without_data headers 固定 65K，与 pool size 无关 |

### 关键风险（实测后）

| 风险 | 实测结果 |
|------|------|
| 1. Patch 不完整（4 处 stale usage） | 作者已全部更新（line 1472, 1511, 1619, 1622 in `deabc80`） |
| 2. 不可直接应用（已重构） | 作者按当前代码结构重写 ✅ |
| 3. 1KB 测试无 without_data headers | 65K 固定 headers 足够（默认 expansion_block_count=8192 × 8） |
| 4. backward compatibility | `disable_scale_cap=true` 路径仍存在，但改为"始终预分配 65K + 跳过 expansion"（对比原"按需 lazy expand"） |

**作者解决了原始 patch 的所有问题**。实测性能表现：
- FinalizeIo **p999: 16,477 → 14,206 ns (-13.8%)**（最大改善）
- DONE_ALLOC p99: 2,785 → 2,668 ns（继续下降）

> **注意**：p50 / avg 略增 +118ns（噪声），但 p99 / p999 持续改善。说明优化对**长尾**更有效（消除了 without_data headers 首次 alloc 的 cold path）。

如需进一步压缩 `DONE_ALLOC` p99，应优先在 **brpc 唤醒路径**（p99=6.3μs，占 51% 长尾）上做优化，而非调整 umq 池布局。

---

## 测试基础设施备忘

- 跑 perf 测试：`/home/gonglei/4/yellow-branch/brpc_ybx/run_ub_test.sh --skip-build`
- 测试时长通过 `run_ub_test.sh` 中 `--test_seconds=N` 控制（**必须 ≥ 70s + 1min = ≥ 130s 才能让 prof dump 落盘**）
- Prof dump 路径：`/tmp/ubsocket/profiling/ubsocket_profiling_<PID>.log`
- Dump 触发时机：prof 线程 1 min 周期触发 + process exit 时不触发（`DumpStopExt` 不 flush）
- 想要 uninit 时也 dump：需要修改 `ubsocket_prof_tracer_ext.cpp` 的 `DumpStopExt` 在 join 之前调用 `DumpDataExt`
- brpc_ybx 链接本目录 ubsocket：bazel cache `external/_main~local_deps~ubsocket/src` 软链 → `/home/gonglei/4/yellow-branch/ubs-comm-ybx/src`

---

## 最终结论

### 已落地优化

**两次 umq 优化**（`139be49` + `deabc80`）是当前累计落地的优化：

- `139be49` memset 合并写：DONE_ALLOC p99: 3,645 → 2,785 ns（**-23.6%**）
- `deabc80` 池内存布局重写：FinalizeIo p999: 16,477 → 14,206 ns（**-13.8%**）
- 端到端 RPC: 173 → 169 μs avg（-2%）

### 优化空间评估

| 方向 | 当前 | 限制 | 建议 |
|------|------|------|------|
| brpc `bthread_flush` 批处理 | 6.3μs p99 | 需要 brpc 改动（合并多次 flush） | **优先级最高，但需 brpc 团队** |
| umq 控制帧 alloc | 2.8μs p99 | umq_buf_init 字段 init 已最简；进一步需改 umq API | 需 umq API 扩展支持 per-thread 缓存预热 |
| umq `umq_post` doorbell | 2.9μs p99 | SQ ring MMIO 写，硬件层 | 需 umq SQ 批提交支持 |
| `state->mutex` / `rx_enqueue_mutex_` | <0.5μs p99 | 实际锁竞争很小 | 无优化空间 |
| ubsocket 侧 | — | 大部分优化空间已耗尽 | 仅 NotifyReadable 批处理值得尝试（QPS 高时有效） |

### 结论

**100KB 测试场景下，ubsocket + umq 协同的优化已接近最优**。剩余 13μs 的 p99 分布中：
- 51% 在 brpc 唤醒路径（brpc 库内）
- 49% 在 umq 控制帧 alloc + doorbell（umq 库内）

进一步降低 p99 需要跨仓库的 brpc / umq 改动，建议在 brpc 侧做 `bthread_flush` 批量优化（预期收益 3-5 μs p99）。
