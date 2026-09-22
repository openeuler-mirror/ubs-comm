# 基于 correlation_id 的 RPC 全链路时延打点设计

> 适用版本：brpc-ybx + ubs-comm-ybx
> 关联文档：
> - `UBSOCKET-BRPC-UB-NATIVE-FLOW.ch.md` — bRPC use_ub_native 调用栈分析（本设计的调用栈依据）
> - `performance/UBSOCKET-PERF-TRACEPOINT-ANALYSIS.ch.md` — 单段 PROF 基线数据（本文引用的实测数字来源）
> 状态：设计定稿（形态 A：双端记录 + 离线 join；口径：brpc IO 边界四点）
> 实现状态：阶段 1（四点打点）+ 阶段 2（join 脚本）+ 阶段 3（字节偏移桥梁逐包归因）+ 阶段 4（软件栈阶段事件）+ 阶段 5（小包路径补齐：RxCqeData + UmqPostSend range 事件）已实现
> - brpc：`src/brpc/rpc_link_trace.{h,cpp}`（ring+flush+落盘）、`socket.cpp`（T1/T4）、`policy/baidu_rpc_protocol.cpp`（T3/T2）、`input_messenger.cpp` + `socket.h`（`_ub_rx_cursor` 连接级 RX 字节游标）；CMake/Bazel 均 glob 自动收编，零构建改动
> - ubsocket：`src/ubsocket/csrc/profiling/trace/ubs_pkt_trace.{h,cpp}`（per-thread ring + flush 线程；阶段 4 增 `UbsStageId` 枚举 + `UbsStageTrace` + S 行落盘；阶段 5 增 `sn_count` 列支持 range 事件）、`ubsocket_data.cpp`（ubs_poll 交付点打 SN+游标）、`core/umq/umq_socket.h`（`delivered_bytes_` 游标）、`core/ubsocket_bigdata.cpp`（阶段 4 十处阶段事件插点；阶段 5 `UmqPostSend` 改打整批 range）、`core/umq/umq_share_jfr_epoll_runner_ops.cpp`（阶段 5 `SiftSocketEventsWithUmqBuffers` 插 `STAGE_RX_CQE_DATA`）
> - 工具：`ubscomm/tools/trace/join_rpc_trace.py`（四点 join + 按 fd+byte_cursor 对齐 cid↔SN 的逐包分段归因 + 阶段事件聚合表/瀑布图）
> - 开关：`-rpc_link_trace_enable`（默认关），输出 `rpc_trace_<pid>.log`（TSV：`point cid port fd ts_ns byte_cursor`）；`UBS_PKT_TRACE_ENABLE=1`（默认关），输出 `ubs_pkt_trace_<pid>.log`（TSV：`fd sn byte_cursor ts_ns`）

---

## 一、背景与目标

### 1.1 问题

现有 profiling 存在两个结构性缺口：

1. **聚合不逐包**：PROF 体系（`src/ubsocket/csrc/profiling/ubsocket_prof.h`，40+ tracepoint）只有 histogram 聚合统计，无法回答"第 N 个 RPC 慢在哪一段"——长尾 RPC 无法逐包归因。
2. **跨节点黑洞**：perf 分析已识别 4 个未覆盖段：网络 RTT REQ（20-30μs）、网络 RTT RSP（10-20μs）、bthread 调度排队（50-90μs）、UMQ async event（1-5μs）。端到端 172μs（avg）中约 50μs 无归属。

### 1.2 目标

- 以 correlation_id 串联整个 RPC 调用栈，**逐包**计算链路时间（通信栈总耗时，剔除业务处理）
- 给出 bRPC / UBSocket / UMQ / URMA 各软件栈分段的时延口径与数据来源
- 零 wire 协议改动，umq / URMA 零改动

### 1.3 非目标

- 不做 req/rsp 单方向链路拆分（四点法天然给双向合量；拆分需时钟同步，精度不可控，见 7.4）
- 不做硬件 CQE 时间戳（URMA 无该支持，`urma_cr_t` 无 timestamp 字段、无 enable 接口）
- 不改变任何数据路径行为，打点可常开

---

## 二、现状盘点与差距

| 已有设施 | 现状 | 差距 |
|----------|------|------|
| PROF 体系（`csrc/profiling/ubsocket_prof.h`） | histogram 聚合，覆盖 `CORE_*` / `BRPC_*` / `UMQ_*` / `UBS_NATIVE_*` 40+ 段 | 无 per-RPC 粒度 |
| SplitTrace（`csrc/profiling/trace/ubsocket_trace.h`） | 全局 256 slot 采样，`(seq_no, fd)` 关联，SN 经 `imm.user_data`（24bit）上 wire | 采样率 1/100 非逐包；不覆盖 bigdata READ_OFFER 路径；不关联 RPC |
| `u_external_rpc_id_ops_t`（`include/ubsocket_def.h:63-66`） | `get_rpc_id` / `get_rpc_call_timestamp` TLS 钩子，brpc 可经 `ubsocket_init` 注册 | 已注册但全仓零调用点（休眠态），阶段 3 激活 |
| imm_data wire 通道（`umq_pro_types.h`） | 64bit 布局 `type:2 \| umq_id:18 \| rsvd1:20 \| user_data:24`，bit20 被 `UBS_IMM_BIG_CTRL_BIT` 占用，user_data 24bit 为连接级 SN | 剩余 rsvd1 仅 19bit，放不下 64bit 时间戳——跨节点时间戳不走 wire 的根因 |
| baidu_std 协议 | `[4B magic][header PB][body PB]`，RpcMeta 天然携带 correlation_id | **现成的逐包关联键，零成本** |

---

## 三、核心设计：四点差值法

### 3.1 原理：时钟偏移自动消除

设 client 与 server 时钟偏移为 θ（未知常数），req 方向链路耗时 d_req、rsp 方向 d_rsp：

```
T3 = T1 + θ + d_req        （server clock）
T2 = T4 - θ + d_rsp        （client clock）

(T2-T1) - (T4-T3) = d_req + d_rsp    （θ 完全消除）
```

与 NTP roundtrip 测量同构。约束仅为：

- T1 / T2 同源（client `CLOCK_MONOTONIC`）
- T3 / T4 同源（server `CLOCK_MONOTONIC`）

时钟漂移 < 100ppm，单 RPC 窗口（ms 级）影响约 172μs × 100ppm ≈ 17ns，忽略。

### 3.2 四点打点位置（口径定义）

| 点 | 节点 | 位置 | cid 来源 |
|----|------|------|----------|
| T1 | client | `Socket::Write(req)` 入口（brpc `socket.cpp:1894`；client request 发送起点） | `opt.id_wait.value`（`WriteOptions.id_wait`，client 侧 `IssueRPC` 置 `wopt.id_wait = cid`，`controller.cpp:1244`；**不走 TLS**） |
| T3 | server | `CutInputMessage` 解出完整 request 的时刻，即 `msg->received_us()`（**对齐 wire 到达时刻**，非 `ProcessRpcRequest` 入口） | 解析出的 RpcMeta 字段 |
| T4 | server | `Socket::Write(rsp)` 入口（`SendRpcResponse` 内序列化完成后调用；`socket.cpp:1894`） | `opt.id_wait.value`（server 侧 `SendRpcResponse` 置 `wopt.id_wait = response_id`，`baidu_rpc_protocol.cpp:569`；**不走 TLS**——`done->Run()` 可能在业务 bthread） |
| T2 | client | `CutInputMessage` 解出 response 的时刻，即 `msg->received_us()`（**对齐 wire 到达时刻**，非 `ProcessRpcResponse` 入口） | 解析出的 RpcMeta（brpc 本就按 cid 匹配 pending RPC） |

> **打点位置与 cid 来源论证**：T1/T4 选在 `Socket::Write` 而非 `DoUbsNativeWrite`，原因有二：① `DoUbsNativeWrite` 只接收 `butil::IOBuf* data_list[]`，**无 cid 上下文**；② `Socket::Write` 才是 RPC "发送起点"的语义边界，两条写路径（无积压单写 `socket.cpp:2044`、KeepWrite 批量 `socket.cpp:2243`）都先经过 `Socket::Write` 入口，是真正的公共漏斗。cid 来源**统一为 `WriteOptions.id_wait.value`**（`socket.h:397`）：client 侧 `IssueRPC` 置 `wopt.id_wait = current_id()`（versioned cid，与 wire `meta.correlation_id` 一致），server 侧 `SendRpcResponse` 置 `wopt.id_wait = response_id`（= request 的 `meta.correlation_id()`），两侧对称、与 wire cid 一致，可直接用于双端 join。**注意不能用 `sock->correlation_id()`**——它只为不能上 wire 的协议（http/nova 等）在 server 侧设置，baidu_std client 侧从不设置（`socket.h:590-593` 注释明确"only 1 RPC call on this socket"，且全仓 client 侧 baidu_std 无 `set_correlation_id` 调用）。

100KB 路径下 T3 天然落在 `FinalizeIo` 交付之后（READ_OFFER 全程自动计入链路），无需特殊处理。

时序示意：

```
client                                            server
────────────────────────────────────────────────────────────
CallMethod（cid = cntl->call_id()）
T1  Socket::Write(req) 入口（cid 取 opt.id_wait.value）
    DoUbsNativeWrite → TrySenderPost → umq_post
                  ── req（OFFER/SMALL_DATA）──►
                                                    JFCE → RxPollQuantum
                                                    FinalizeIo（100KB）
T3                                                  CutInputMessage 解出 req
                                                    （msg->received_us()，wire 到达时刻）
                                                    HandleRequest（业务）
                                                    SerializeResponse（响应序列化）
T4                                                  Socket::Write(rsp) 入口
                    ◄── rsp（OFFER/SMALL_DATA）──
    RxPollQuantum
T2  CutInputMessage 解出 rsp
    （msg->received_us()，wire 到达时刻）
    done 回调

E2E     = T2 - T1                （client clock）
server 处理 = T4 - T3                （server clock，含反序列化+业务+响应序列化）
链路时间 = E2E - (T4 - T3)         （θ 消除，逐 RPC）
```

### 3.3 链路时间的语义边界

此口径下链路时间包含：

```
client TX 栈（bRPC IO + UBSocket 路由/OFFER 构造 + UMQ post）
+ req fabric（OFFER SEND 腿 + RDMA READ 往返）
+ server RX 栈（DoReadOffer/FinalizeIo + UMQ CQE 收割 + bRPC 交付）
+ server TX 栈（与 client TX 对称）
+ rsp fabric（与 req 对称）
+ client RX 栈（与 server RX 对称）
```

剔除（归入 server 处理或 E2E 残差）：brpc 序列化/反序列化、业务处理、done 排队。

即**两端全通信软件栈（bRPC IO + UBSocket + UMQ + URMA）+ fabric 的合量**——把"业务/框架处理时间"从 RPC 总耗时中剔除后的通信净耗时。

> **提交点 vs wire 发送点**：T1/T4 所在的 `Socket::Write` → `DoUbsNativeWrite` 只是**把 request/response 提交给 ubsocket 的 TX 队列**（`ubs_post`，`socket.cpp:2387-2389` `PROF_START/END(BRPC_NATIVE_TX_POST)` 即包住此提交），**真正 wire 发送由 ubsocket 内部 TX 线程异步完成**。故链路时间天然包含 ubsocket 内部排队 + 发送延迟，这与"client TX 栈含 UBSocket 路由 + UMQ post"的口径一致。

### 3.4 correlation_id 串联机制

baidu_std RpcMeta 的 correlation_id 是协议标准字段，随 req/rsp 双向跨节点传播，逐包关联零成本：

- client T1：`opt.id_wait.value`（`Socket::Write` 的 `WriteOptions`，`IssueRPC` 置 `wopt.id_wait = cid`），**不走 TLS**
- server T3：解包后的 RpcMeta 字段，直接可得
- server T4：`opt.id_wait.value`（`SendRpcResponse` 置 `wopt.id_wait = response_id`），**不走 TLS**
- client T2：brpc 按 cid 匹配 response 与 pending RPC（`ProcessRpcResponse` 里 `bthread_id_lock(cid)`，`baidu_rpc_protocol.cpp:1052`），匹配点即 T2

单连接 pipeline 并发天然按 cid 区分；多连接场景 join 键用 `(client_port, cid)` 消歧。

### 3.5 落地形态（定稿：形态 A）

| | 形态 A：双端记录 + 离线 join（**定稿**） | 形态 B：T3/T4 随响应 meta 回传 |
|---|---|---|
| wire 改动 | **零** | RpcMeta 加 optional 字段（PB 向后兼容，+16~20B/rsp） |
| 计算 | 离线脚本按 cid join 双端日志 | client 单端闭环，实时 |
| 聚合 | 离线直方图 | 进程内实时直方图 |
| brpc 改动 | 4 个打点 + 落盘 | 4 个打点 + meta 填充/解析 |

选 A 的理由：零 wire 改动；与现有双端 prof dump + `tools/trace/pick_calc_p99_multi.py` 分析流程完全一致。

---

## 四、分段时延模型

### 4.1 分段原则

- **同节点段**：直接打点，精确
- **跨节点 fabric 段**：不直接测量，用残差闭合（见 4.4）
- **例外**：100KB 路径的 RDMA READ 往返——READ WR `umq_post` 返回时刻与 TX CQE 收割时刻**都在接收方节点内**，可精确测量（见 4.5）

### 4.2 100KB 路径（READ_OFFER）完整分段

| # | 软件栈 | 分段 | 数据来源 | 基线 avg |
|---|--------|------|----------|---------:|
| 1 | bRPC | client 序列化+排队（→T1） | 现有 `BRPC_CLI_PRE_POST` | 8.1μs |
| 2 | UBSocket | client TX 路由+OFFER 构造 | 现有 `TRY_SENDER_POST` − `UMQ_POST_SEND` | ~5.8μs |
| 3 | UMQ | client post（WR 填充+SQ 写+doorbell） | 现有 `UMQ_POST_SEND` | 2.2μs |
| 4 | URMA/fabric | OFFER SEND 腿（client→server） | **残差 R1**（新） | ~10μs |
| 5 | UBSocket | server DoReadOffer 控制面 | 现有 `DO_READ_OFFER` | 4.7μs |
| 6 | UMQ | server READ WR alloc+post | 现有 `READ_WR_ALLOC` + `UMQ_POST_READ` | 3.8μs |
| 7 | URMA/fabric | RDMA READ 往返 | **新增：`UbsBigIoCtx` 内 t_post_ret→t_cqe（节点内，精确）** | ~15μs |
| 8 | UBSocket | server FinalizeIo 交付 | 现有 `FINALIZE_IO` | 8.3μs |
| 9 | bRPC | server 反序列化 + **业务处理** + 响应序列化 + 并发控制（T3→T4，即 `msg->received_us()`→`Socket::Write(rsp)`） | **四点法直接给出**（新） | 业务时间为变量，见下注 |
| 10 | URMA/fabric | rsp OFFER SEND 腿（server→client） | **残差 R2**（新） | ~10μs |
| 11 | UBSocket | client RX 唤醒+交付 | 现有 `RX_READ` + `RX_PROC` | ~1.2μs |
| 12 | bRPC | client 反序列化+回调（T2→） | 现有 `BRPC_CLIENT_PROCESS_RSP` | 8.1μs |
| — | （未覆盖） | bthread 调度等 | E2E − Σ上述 | ~50μs |

校验恒等式：**Σ全部段 ≈ E2E**。残差是未覆盖段（主要是 bthread 调度排队）的量尺，健康状态 < 5%。

> **段 9 口径注**：`T4-T3` 区间按代码实际包含 `ProcessRpcRequest` 入口 → `DeserializeRpcMessage`（反序列化）→ `svc->CallMethod`（**业务处理**）→ `SendRpcResponse` → `SerializeResponse`（响应序列化，`baidu_rpc_protocol.cpp:441` 已有 `PROF_START/END(BRPC_SERIALIZE)`）→ `Socket::Write(rsp)`，外加 `ConcurrencyRemover` 并发控制开销。其中**业务处理时间是变量**（测试场景 echo 几乎为 0，生产场景可能主导），故段 9 不给固定基线值。它与段 12（client `BRPC_CLIENT_PROCESS_RSP` 8.1μs 纯反序列化+回调）不对称是正常的——server 侧多了业务与并发控制。

### 4.3 1K 路径（SMALL_DATA）

同模型，差异：

- T3 直接落在 server `OnNewMessages`（无 bigdata 事务）
- 无段 5-8（无 READ_OFFER / READ WR / RDMA READ / FinalizeIo）
- fabric = 两条 SMALL_DATA SEND 腿（残差 R1 / R2）

### 4.4 残差法

四点法给出的是 req+rsp 链路合量，fabric 残差口径：

```
R1 + R2 = 链路时间 − Σ(两侧 TX/RX 软件栈段 + RDMA READ 段)
```

R1/R2 单方向拆分需时钟同步（滑窗估 θ，仅对称流量下近似准确），默认不做。

### 4.5 UMQ/URMA 段口径（重要设计决策）

- **UMQ 提交段**（段 3/6）= `umq_post` 调用前后（WR 填充 + SQ ring 写 + doorbell MMIO 全部内含）——ubsocket 调用边界打点，**umq 库零改动**
- **CQE 收割时刻** = 一次 `umq_poll` 返回的整个 batch 共享同一时刻——ubsocket 侧 `RxPollQuantum` / `TxSweepOnce` 每 batch 打一次，同 batch 所有 qbuf 复用
- **fabric 残差** = post 返回 → 对端 CQE 收割，含硬件执行 + 网络 + 收割积压的合量
- **RDMA READ 段**（段 7）= 接收方 READ WR `umq_post` 返回 → 本端 TX CQE 收割（`HandleTxCompletion` Branch 1），两时刻均在 server（rsp 方向则在 client）节点内——**不依赖时钟同步的精确测量**

---

## 五、数据落盘与格式

| 项 | 设计 |
|----|------|
| 路径 | `/tmp/brpc/rpc_trace_<pid>.log`（client / server 各自落盘，双端独立） |
| 机制 | per-thread 无锁 ring + 周期 flush 线程（对齐现有 `DumpThread` 模式与 `/tmp/ubsocket/profiling/` 轮转惯例） |
| 时间源 | **统一纳秒**：`butil::cpuwide_time_ns()`（brpc 侧）。注意 `msg->received_us()` 是 `CutInputMessage` 切包时刻但**单位为 μs**（`input_messenger.cpp:399` `cpuwide_time_us()`），T3/T2 若复用需在落盘前 ×1000 转 ns；T1/T4 在 `Socket::Write` 直接打 `cpuwide_time_ns()` |
| client 行 | `cid client_port t1_ns t2_ns e2e_ns`（TSV） |
| server 行 | `cid server_port t3_ns t4_ns proc_ns`（TSV） |
| join 工具 | `tools/trace/join_rpc_trace.py`（新）：输入双端日志（可选叠加双端 `ubsocket_profiling_*.log`），输出逐 RPC `cid e2e server_proc link` 表 + link 直方图（p50/p90/p99/p999）+ 残差归因表 |

逐包开销：每端每 RPC 2 次时钟读取（aarch64 `cntvct_el0`，~15ns/次）+ 24B 记录，可常开。

---

## 六、实施计划

### 阶段 1：四点打点（brpc-ybx，~60 行）

复用现有打点基础设施，工作量集中在"按 cid 落盘"，非新打时间点：

- **T1**（client）：`Socket::Write(req)` 入口打 `cpuwide_time_ns()`，cid 取 `opt.id_wait.value` → 写 ring
- **T3**（server）：复用 `msg->received_us()`（`ProcessRpcRequest` 已传入），cid 取 `meta.correlation_id()`，×1000 转 ns → 写 ring
- **T4**（server）：`SendRpcResponse` 内 `Socket::Write(rsp)` 入口打 `cpuwide_time_ns()`，cid 取 `opt.id_wait.value` → 写 ring
- **T2**（client）：复用 `msg->received_us()`（`ProcessRpcResponse` 里 `msg->received_us()` 可用，`baidu_rpc_protocol.cpp:1083`），cid 取 `meta.correlation_id()`，×1000 转 ns → 写 ring
- per-thread 无锁 ring + flush 线程 + TSV 落盘

> **简化点**：T3/T2 的"切包时刻"brpc 已有（`msg->received_us()`，`input_messenger.cpp:323`），**无需新打时间点**，只需在 `ProcessRpcRequest/ProcessRpcResponse` 里把它按 cid 落盘；T1/T4 是仅有的两个新时间点（`Socket::Write` 入口）。cid 统一取 `WriteOptions.id_wait.value`（client `controller.cpp:1244` / server `baidu_rpc_protocol.cpp:569`），与 wire `meta.correlation_id` 一致。client RX→done 段 brpc 已有 `g_brpc_ubs_step_latency[]` + `BRPC_CLIENT_PROCESS_RSP`，只是未按 cid 串联、未双端 join。

### 阶段 2：join 分析脚本（本仓 `tools/trace/`，~150 行 python）

- 按 `(client_port, cid)` join 双端 `rpc_trace_*.log`
- 输出逐 RPC 链路时间表 + 直方图 + 残差归因表
- 交叉验证：E2E vs brpc 自带 latency bvar

### 阶段 3：逐包分段归因（可选，ubsocket ~200 行 + brpc ~30 行）

- brpc 实现 `u_external_rpc_id_ops_t`（TLS `get_rpc_id`）经 `ubsocket_init` 注册——激活现有休眠钩子
- ubsocket：TX/RX 边界读 cid 标记逐包段记录；`UbsBigIoCtx`（`csrc/core/ubsocket_bigdata.cpp:207`）内嵌 READ fabric 两时刻（段 7）
- 慢 RPC flight recorder：链路时间 > p99.9 阈值时落盘完整分段记录

三层次职责：

```
第 1 层  逐包链路时间    四点差值法（correlation_id 串联）
                          → 回答"这个 RPC 的通信栈总耗时是多少"
第 2 层  分段时延分布    现有 PROF histogram（40+ tracepoint + 边界补点）
                          → 回答"链路时间统计上耗在哪一段"（bRPC/UBSocket/UMQ/URMA）
第 3 层  长尾逐包归因    阶段 3 flight recorder，按 cid 提取本地各段记录
                          → 回答"这个慢 RPC 具体慢在哪"
```

### 阶段 4：软件栈阶段事件（ubs_pkt_trace 扩展，ubsocket ~80 行 + join ~120 行）

> **状态（2026-09-04）**：已实现。
> 在 `ubs_pkt_trace` 日志中追加**阶段事件**（stage events），复用同一 ring、
> 同一 flush 线程、同一开关（`UBS_PKT_TRACE_ENABLE`），把 SVG 流程图
> （`rpc_latency_flowchart.svg`）展示的 ubsocket/UMQ 各阶段变成**逐包**可观测，
> join 脚本输出阶段瀑布图。不新增任何独立机制，`UBSOCKET_PROF_ENABLE` 保留
> 但本链路不再依赖它。实现落点：`ubs_pkt_trace.{h,cpp}`（UbsStageId 枚举 +
> UbsStageTrace + S 行落盘）、`ubsocket_bigdata.cpp`（10 处插点）、
> `join_rpc_trace.py`（S 行解析 + 聚合表 + 瀑布图）。

**记录格式**（与 packet 记录同文件，首列区分，向后兼容）：

```
<fd>\t<sn>\t<byte_cursor>\t<ts_ns>          # 现有 packet delivery 记录（不变）
S\t<stage_id>\t<fd>\t<first_sn>\t<ts_ns>    # 新增 stage 事件记录
```

`UbsPktTraceRecord` 增加 `uint8_t stage_id`（0 = packet，非 0 = stage event，
stage 记录 `byte_cursor` 置 0）。新增 API：

```cpp
void UbsStageTrace(int32_t fd, uint32_t first_sn, uint8_t stage_id, uint64_t ts_ns);
```

**UbsStageId 枚举**（定稿 10 项；`STAGE_READ_CQE` 是 HandleTxCompletion **READ
完成分支**入口，勿与 PROF 的 `TX_CQE_READ` 混淆）：

| id | 名称 | 插入点（ubs_bigdata） | key 来源 |
|---|---|---|---|
| 1 | STAGE_TRY_SENDER_POST | TrySenderPost 入口 | `LoadSeqNum()` 预读（= 首个将分配 offer 的 first_sn） |
| 2 | STAGE_FLUSH_PENDING_OFFER | FlushPendingOffer | 被刷 offer 的 `first_sn` |
| 3 | STAGE_UMQ_POST_SEND | umq_post(SEND) 成功后 | 同上 |
| 4 | STAGE_READ_CQE | HandleTxCompletion READ 分支入口 | `ctx->first_sn` |
| 5 | STAGE_HANDLE_RX_CTRL | HandleRxControl 入口 | offer imm 的 `first_sn` |
| 6 | STAGE_DO_READ_OFFER | DoReadOffer 入口 | 同上 |
| 7 | STAGE_UMQ_POST_READ | umq_post(READ) 成功后 | `ctx->first_sn` |
| 8 | STAGE_FINALIZE_IO | FinalizeIo 入口 | `ctx->first_sn` |
| 9 | STAGE_DELIVER_TO_RX_QUEUE | DeliverToRxQueue 入口 | `ctx->first_sn` |
| 10 | STAGE_SEND_SIMPLE_CTRL | SendSimpleCtrl 入口 | `ctx->first_sn` |

**关键语义（评审修正）**：

1. **`first_sn` 是 per-offer，不是 per-RPC**：一个 RPC 可拆多个 offer，每个
   offer 一个 `first_sn`；同 offer 所有 fragment 的 `imm.user_data` 盖同一个
   `first_sn`（`ubsocket_bigdata.cpp:1431`）。join 匹配规则为
   **`first_sn ∈ rpc.sn_set`（成员判定）**，非"首段 SN 相等"；逐 RPC 瀑布图对
   同 stage 多 offer 时长求和。
2. **TX 侧事件的跨日志匹配**：client TX 阶段事件在 client 日志（fd_c），而
   req 的 sn_set 由 server 日志导出；fd_c↔fd_s 桥为 brpc 记录的 fd（T1 带
   fd_c、T3 带 fd_s，cid 配对）。匹配链：
   `cid → (fd_s, sn_set) + fd_c → client 日志中 (fd_c, sn∈sn_set) 的 S 行`。
3. **SN 空间按方向独立**（双端各自 TX 计数、数值重叠）：req/rsp 的 stage
   事件绝不跨方向匹配。
4. **时长相配**：阶段事件仅入口单时间戳；`duration(k) = ts(k+1) − ts(k)`，
   同 `(fd, first_sn)` 组内按 ts 排序；组内末事件无后继则其时长不进统计
   （或用同 key P 行交付时刻收尾，顺带产出"末阶段→ubs_poll 交付"唤醒段）。
5. **同端同钟的 READ fabric 段**：`ts(STAGE_READ_CQE) − ts(STAGE_UMQ_POST_READ)`
   = 单次 RDMA READ fabric 时延，无跨端时钟问题，是逐包级新产出。
   **多 WR offer 口径**：per-WR CQE 模式下 nsegs 个 READ WR 产生 nsegs 条
   `STAGE_READ_CQE`（同 `(fd, first_sn)` 组内按 ts 排序），聚合表的
   `UmqPostRead->ReadCqe [READ wire]` 只反映**首个 CQE** 的 fabric 时延，
   后续 CQE 间隔自成 `ReadCqe->ReadCqe` 行（CQE 到达散布，具诊断价值）；
   单 WR offer 不受影响，ordered completion 模式（终端 CQE 一个）则天然
   给出全链 fabric 时延。
6. **部分接受降级**：FC backpressure 下 `umq_post(SEND)` 部分接受
   （`ret != 0` 且 `accepted_bufs > 0`）时 `STAGE_UMQ_POST_SEND` 不打点
   （与 `PROF_END(UBS_NATIVE_UMQ_POST_SEND, ret == 0)` 口径一致），批首
   offer 缺 2→3 段样本；属降级不属错误，被截尾部由 KeepWrite 重入后正常
   打点。

**join 输出**：聚合表（Stage × Count/AVG/P50/P99/Max，逐事件时长，可出分位
——优于 PROF 聚合）；`--top N` 逐 RPC 瀑布图（Client Send / Server Recv /
Server Send / Client Recv 分组，含 `[READ wire]` 实测段与 `[wire]` 残差段）。

**开销**：关闭时 1 次原子 load + branch（~2ns）；开启时 1 次
`clock_gettime(CLOCK_MONOTONIC)` + 1 次 ring Push（~50-100ns），每 RPC 约 10
次。不依赖 `UBSOCKET_PROF_ENABLE`。

**已知限制**：小消息（≤64KB）走 SMALL_DATA 内联路径，无 offer，阶段 2/3/6-10
无事件（属正常；阶段 5 起 TX 由 range 事件覆盖、RX 由 `STAGE_RX_CQE_DATA`
覆盖）；合包小消息（coalesced）的 TX 事件归属首个 RPC，其余 RPC 该段缺失
（阶段 5 起由 range 事件解决）；阶段事件为入口时刻，"时长"含到下一阶段的
间隙（语义 = 阶段+排队）；多 WR offer 的 READ wire 行仅覆盖首个 CQE（见关键
语义 5）；umq_post 部分接受时 `UmqPostSend` 事件缺失（见关键语义 6）。

### 阶段 5：小包路径补齐（RxCqeData + UmqPostSend range 事件）

> **状态（2026-09-04）**：已实现。
> 阶段 4 的两个覆盖缺口都在小包（≤64KB）路径：**TX 侧** KeepWrite 合批后一批
> 只打批首 offer 一条 `UmqPostSend`，批内其余 RPC 无 TX 事件（事件稀释）；
> **RX 侧** SMALL_DATA 内联数据无 offer、不走阶段 5-10 链路，CQE 收割到
> ubs_poll 交付一段完全黑盒。阶段 5 用两个最小改动补齐，不新增机制：

**方案 A：`STAGE_RX_CQE_DATA`（id=11）——RX 侧 CQE 数据收割点**。插点在
`umq_share_jfr_epoll_runner_ops.cpp` 的 `SiftSocketEventsWithUmqBuffers`：
对 `UMQ_OPC_SEND_IMM` 包在 `AddQbuf` 前打点，key = 段 SN（`imm.user_data`，
与 P 行同空间）。语义 = "内核 CQE 被用户态收割、即将入交付队列"的时刻，与
同 key P 行交付时刻相配产出 `RxCqeData->delivered` 段（= rxQueue 等待 +
唤醒 + brpc 读调度）。READ_OFFER ctrl 包不打点（走阶段 5-10 链路），bigdata
路径不受影响。

**方案 B：`UmqPostSend` range 事件——一条记录覆盖整批 SN**。S 行扩为 6 列：

```
S\t<stage_id>\t<fd>\t<first_sn>\t<ts_ns>\t<sn_count>
```

`sn_count = 0`（或第 6 列缺省，向后兼容）= 单 SN 事件，语义同阶段 4；
`sn_count > 0` = **range 事件**，覆盖闭区间 `[first_sn, first_sn + sn_count − 1]`。
`ubsocket_bigdata.cpp` 在 `umq_post(SEND)` 成功后累加**实际上链的前
`post_count` 个 buf** 的 `BatchBufMeta.sn_count`（批内每个 buf——SMALL_DATA /
coalesced / offer——严格分配 1 个 SN，自 `entry_sn` 连续递增），以 `total_sns`
调 `UbsStageTrace(fd, entry_sn, STAGE_UMQ_POST_SEND, ts, total_sns)`。
**注意不能按 `ctx.batch` 全量求和**：批可能被 `UMQ_BATCH_SIZE`(256) /
SQ 额度（`SenderPostBudget`）裁剪，被裁尾部回滚（`FetchSubSeqNum`）后 SN
会被下一批复用，全量求和会让同一 SN 被两条 range 覆盖，join 侧产生虚假的
`UmqPostSend->UmqPostSend` 段。批内所有**已 post** SN 由这一条记录全部归属：
枚举靠区间算术，不落盘 SN 列表，每批仍只 1 次 ring Push + 1 行日志。

**join 侧（`join_rpc_trace.py`）**：

- `load_pkt` 把 S 行拆为 exact 事件（`sn_count=0`）与 range 事件两张表；
  range 按 `(first_sn, end_sn)` 预排序索引（`prep_ranges`，同 fd 批 SN 单调
  分配，两端均可 bisect），成员查询双 bisect O(log n)。
- **所有事件源均按方向隔离**：同 fd 上双向 SN 空间均从 1 递增、数值必然
  重叠，`(fd, first_sn)` 键会跨方向碰撞（client 的 req-TX SN 可能等于
  server 的 rsp-TX SN）。`collect_stage_groups` 对 exact S 行先按 stage id
  过滤（TX 组仅收 1-3，RX 组仅收 4-11），再对 `sn_set` 中每个 SN 做 range
  区间成员匹配且**仅 TX 组**参与——否则会把反向事件吸入同组，伪造
  `UmqPostSend->RxCqeData`（横跨一次往返）、`RxCqeData->TrySenderPost`、
  `delivered->TrySenderPost` 等虚假段（测试 fixture 故意让 rsp SN 数值
  落入 req range 内验证隔离）。
- 批内非批首 SN 的 TX 组通常只有 range 单事件，无相邻对则不产时长——
  `stage_total` 只计真实段，不为非批首 RPC 虚构 `TrySenderPost->UmqPostSend`
  （该段时长归批首 offer，其值 = 整个批的 post 窗口，语义正确且不重复计数）。
- 聚合表新增 `RxCqeData->delivered` 行（SMALL_DATA RX 唤醒段）；显示序把
  RxCqeData 排在 HandleRxCtrl 前——两者同为"RX 侧首次可见"事件，按消息
  类型互斥（SMALL_DATA vs READ_OFFER）。

**已知限制更新**：阶段 4"已知限制"中小包 TX 事件缺失、SMALL_DATA RX 黑盒两
条由本阶段解决；保留：批内非批首 SN 的 TX 组仅 `UmqPostSend` 单点无段时长
（见上）；`TrySenderPost`/`FlushPendingOffer` 仍只打批首 offer（其覆盖的
批范围未记录，非批首 SN 的 1→2、2→3 段不可见——如需可后续同样 range 化）；
`STAGE_RX_CQE_DATA` 仅插在 shared-JFR 主 umq 路径
（`SiftSocketEventsWithUmqBuffers`），sub-umq RX 路径
（`HandleSubUmqPollBuffers`）无此打点，该部署形态下小包 RX 黑盒仍在。

---

## 七、边界情况与已知限制

| # | 场景 | 处理 |
|---|------|------|
| 7.1 | KeepWrite 重试 / EAGAIN | T1 只记首次进入，重试耗时计入链路时间（语义正确，属 TX 栈） |
| 7.2 | pipeline 并发 | cid 逐包区分，天然支持 |
| 7.3 | 时钟漂移 | 单 RPC 窗口内 < 100ppm，影响 ~17ns，忽略 |
| 7.4 | 双向合量不可拆 | 四点法给 req+rsp 合量；单方向拆分需时钟同步，默认不做 |
| 7.5 | RPC 失败/超时 | T2/T4 缺失时标记 incomplete，不进 link 统计 |
| 7.6 | 多连接 / 多 Channel | join 键 `(client_port, cid)` 消歧 |
| 7.7 | 读写时钟源 | 双端统一 `CLOCK_MONOTONIC`（`Func::CurrentTimeNs` / `cntvct_el0`） |

---

## 八、验证计划

复用 perf 文档测试条件（双机 100KB ub_test，单连接 queue_depth=10，130s / 130K reqs）：

1. **Σ段 ≈ E2E**：分段求和与端到端残差 < 5%
2. **link > 0 恒成立**（d_req + d_rsp 物理为正）
3. **E2E 与 brpc latency bvar 一致**（交叉验证）
4. 阶段 3 后：慢 RPC（link > p99.9）flight recorder 记录可复现归因

---

## 附录 A：设计演进记录

初版方案曾设计 **TRACE 伴随帧**：新增 `UBS_TRACE = 4` 控制帧类型（`UbsCtrlType` 追加），携带发送方打点时刻跨节点传播（wire：`[UbsCtrlHdr 24B][UbsTraceEntry × N，56B/条]`，经 `SendSimpleCtrl` 路径发出），兼任时钟同步探测包。评审后**否决**，改用四点差值法：

| 对比项 | TRACE 帧 | 四点差值法（定稿） |
|--------|----------|--------------------|
| wire 改动 | 新增帧类型 | **零** |
| 时钟同步 | 需滑窗 offset 估计（±10-50μs） | **不需要** |
| 额外流量 | 1K 路径 1:1 控制帧（~5% 带宽） | 零 |
| 兼容性 | 老版本未知 ctrl type 行为需确认 | 零风险 |
| 实现量 | brpc + ubsocket ~900 行 | **brpc ~100 行 + 脚本** |
| 代价 | req/rsp 方向可拆 | 方向不可拆（接受） |

否决根因：四点法在零 wire 改动、零时钟同步依赖下取得链路时间，实现量小一个数量级；TRACE 帧的流量开销与兼容风险均不可控。

## 附录 B：关键代码挂接点索引

> ubsocket 侧路径相对 `ubscomm/`，brpc 侧路径相对 brpc 仓库根（`src/brpc/`）。

### brpc 侧（阶段 1 打点位置）

| 挂接点 | 位置 | 用途 |
|--------|------|------|
| `Socket::Write` | `socket.cpp:1894` | **T1 / T4 打点位置**（公共漏斗） |
| `WriteOptions.id_wait` | `socket.h:397` | **T1 / T4 的 cid 来源**（client `controller.cpp:1244` / server `baidu_rpc_protocol.cpp:569` 设置，与 wire cid 一致） |
| `Socket::Write` 单写路径 | `socket.cpp:2044` | 无积压单写 → `DoUbsNativeWrite` |
| `Socket::Write` KeepWrite 路径 | `socket.cpp:2243` | 有积压批量 → `DoUbsNativeWrite` |
| `msg->received_us()` | `input_messenger.cpp:323`（`_received_us = received_us`） | **T3 / T2 复用**（CutInputMessage 切包时刻，μs） |
| `ProcessRpcRequest` | `baidu_rpc_protocol.cpp:958/988`（`meta.correlation_id()`） | T3 cid 来源 + T4 触发点（done closure） |
| `SendRpcResponse` | `baidu_rpc_protocol.cpp:382`（`wopt.id_wait = response_id` 于 :569） | T4 所在函数（内含 `SerializeResponse` → `Socket::Write(rsp)`） |
| `ProcessRpcResponse` | `baidu_rpc_protocol.cpp:1052/1083`（`bthread_id_lock(cid)` + `msg->received_us()`） | T2 cid 匹配点 + T2 时间戳来源 |
| `butil::cpuwide_time_ns()` | brpc 全局 | T1 / T4 时间源（ns） |
| 现有 client RX→done 打点 | `input_messenger.cpp:531` + `controller.cpp:977`（`g_brpc_ubs_step_latency` + `BRPC_CLIENT_PROCESS_RSP`） | 可复用参考 |
| ~~`sock->correlation_id()`~~ | `socket.h:594-596` | **不可用于 baidu_std client**（仅 server 侧 http/nova 等协议设置） |

### ubsocket 侧（阶段 3）

| 挂接点 | 位置 | 用途 |
|--------|------|------|
| `u_external_rpc_id_ops_t` | `src/ubsocket/include/ubsocket_def.h:63-66` | cid TLS 钩子（阶段 3 激活） |
| `TraceRegistry::RegisterRpcIdOps` | `src/ubsocket/csrc/profiling/trace/ubsocket_trace.h:255-261` | 钩子注册入口（`ubsocket_init` 已有链路） |
| `UbsBigIoCtx` | `src/ubsocket/csrc/core/ubsocket_bigdata.cpp:207-232` | 阶段 3 内嵌 READ fabric 两时刻 |
| `DoReadOffer` / `FinalizeIo` / `HandleTxCompletion` | `src/ubsocket/csrc/core/ubsocket_bigdata.cpp`（1465 / 1241 / 2808） | 段 5-8 打点位置 |
| imm 布局 `umq_ub_imm_t` | `src/hcom/umq/src/umq_ub/core/umq_ub_imm_data.h:57-109` | SN 上 wire 通道（bit20 marker / user_data 24bit SN） |
| `UBS_IMM_BIG_CTRL_BIT` | `src/ubsocket/csrc/core/ubsocket_proto.h:60` | imm bit20 已占用证据 |
