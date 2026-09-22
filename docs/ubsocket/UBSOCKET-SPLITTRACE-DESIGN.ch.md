# UBSocket 报文流转跟踪 (SplitTrace)

## 1. 需求背景

### 1.1 面临的问题

在 ubsocket 中报文收发非常频繁，面临着报文丢失，跨节点无法关联的问题。SplitTrace 要解决的核心问题：

**报文流转跟踪** — 提供一种基于 socket 连接的报文流转跟踪机制，用于发现报文的丢弃位置。由于报文收发是热路径，不能全量采集每个报文的完整流程，因此引入采样机制。

### 1.2 设计要求

- **写入无竞争**: 不同线程写同一 slot 的不同 phase 索引，天然无数据竞争
- **跨线程关联**: poller/runner 线程通过 seq_no 找到业务线程的 slot，同一 slot 内天然关联
- **低开销**: 热路径仅取模运算 + 概率判断 (未采样 ~1ns)，采样命中 ~15ns；1/100 采样率下平均每 IO ~1ns，影响 < 0.2%
- **按需启停**: 内存延迟分配 — 功能关闭时零内存开销; 功能打开时分配 `GlobalTracePool` (~129KB) + drain 线程批量打印缓冲区 (256KB)。由 ubstat 工具运行时动态调整, 默认关闭
- **确定性采样**: 基于 UMQ seq_no 取模判定 (seq_no % N == 0), 两端独立做出相同采样决策, 确保跨节点同一报文同时被采样。不区分 socket / 线程, 均匀覆盖全部连接。全局共享 256 个 TraceSlot

### 1.3 已有条件

SplitTrace 的核心挑战是：业务线程的同步段（WriteV/ReadV）和 poller/runner 线程的异步段（CQE 处理）属于不同线程，如何将它们的 trace 条目关联到同一次 IO 操作？

**已有机制**: ubsocket 在 `PostSend` 中通过 `FetchAddSeqNum` 分配递增的 seq_no，并通过 `buf_pro->imm.user_data` 嵌入 UMQ buf。当 poller 线程 `umq_poll` 收到 CQE 时，从 `buf_pro->imm.user_data` 读取同一 seq_no。

```
业务线程:                  poller 线程:
  PostSend                   umq_poll
    ├─ seq_no = FetchAdd       ├─ buf = umq_poll(...)
    ├─ buf_pro->imm.user_data  ├─ seq_no = buf_pro->imm.user_data
    │    = seq_no              │
    └─ umq_post(buf)           └─ (seq_no 匹配, 可关联)
```

**设计利用**: 不需要新增跨线程传递机制。seq_no 已在 UMQ buf 中传递，SplitTrace 只需利用它来关联 slot。同时, seq_no 在 wire 上传递的特性使得两端可以基于 `seq_no % N` 独立做出相同的采样决策, 实现跨节点同一报文同时被采样。

- **全局 TraceSlot 池**: `GlobalTracePool` 单例持有 `slots_[256]`，所有 socket 共享。slot 通过 `(seq_no, fd, path)` 三元组关联，poller 线程按此三元组在全局池中查找

### 1.4 功能描述

#### 1.4.1 输出机制

SplitTrace 通过后台 drain 线程周期性消费已完成采样的 TraceSlot，输出 per-IO 时序日志。drain 线程受功能开关控制 (关闭时不启动)。

| 维度 | 说明 |
|------|------|
| **输出触发** | `SplitTraceDrainThread` 后台线程, 每 `UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS` 毫秒 (默认 10ms) 执行一次, 功能开关打开时启动 |
| **输出方式** | 批量格式化所有 DONE slot, 通过 `UBS_SLOG_INFO` 一次 log 调用写入 ubsocket 日志系统, 减少日志接口调用次数 |
| **批量输出** | 每次 drain 将所有已过宽限期的 DONE slot 格式化到 `thread_local` 批量缓冲区 (256KB), 一次 log 调用输出全部 |
| **宽限期** | slot 完成后等待 2ms 宽限期, 确保 poller/runner 线程的异步子阶段已写入, 然后才输出 + Reset |
| **输出生命周期** | slot 输出后立即 Reset (state→IDLE), 可被下次采样复用; 数据不持久化, 仅输出到日志 |
| **功能关闭时** | drain 线程不启动 (零 CPU), 热路径打点短路; `GlobalTracePool` 内存未分配 (零内存) |
| **功能打开时** | 延迟分配 `GlobalTracePool` (~129KB) + drain 线程批量打印缓冲区 256KB (drain 线程独占) |

#### 1.4.2 输出信息内容

每次输出一条完整的 per-IO 时序, 包含头部 + 各子阶段时间戳:

```
===TX_WRITEV fd=42 seq=1234 s=1000 e=1401 d=401
TX_WV_ENTRY,s=1000,e=1050,d=50
TX_WV_BUILD_IOV,s=1050,e=1100,d=50
TX_WV_ALLOC_BUF,s=1100,e=1200,d=100
TX_WV_MEM_COPY,s=1200,e=1350,d=150
TX_WV_UMQ_POST,s=1350,e=1400,d=50
TX_WV_ASYNC_UMQ_POLL,s=5000,e=5200,d=200
TX_WV_ASYNC_PROCESS_CQE,s=5200,e=5280,d=80
TX_WV_ASYNC_BUF_FREE,s=5280,e=5350,d=70
TX_WV_ASYNC_NOTIFY,s=5350,e=5400,d=50
```

**头部字段**:

| 字段 | 说明 |
|------|------|
| `TX_WRITEV` | 路径名称 (4 种: `TX_WRITEV` / `TX_POST` / `RX_READV` / `RX_POLL`) |
| `fd=42` | socket fd, 标识具体连接 |
| `seq=1234` | IO 序列号, 跨线程关联键 |
| `s=1000` | IO 开始时间戳 (纳秒) |
| `e=1401` | IO 结束时间戳 (纳秒) |
| `d=401` | IO 总耗时 (纳秒) |

**子阶段字段**:

| 字段 | 说明 |
|------|------|
| `TX_WV_ENTRY` | 子阶段名称 (按 4 路枚举定义, 同步段在前, 异步段在后) |
| `s=1000` | 子阶段开始时间戳 (纳秒) |
| `e=1050` | 子阶段结束时间戳 (纳秒) |
| `d=50` | 子阶段耗时 (纳秒) |

**时序解读**:
- 同步段 (ENTRY → UMQ_POST) 由业务线程写入, 异步段 (ASYNC_*) 由 poller/runner 线程写入
- `TX_WV_UMQ_POST` (e=1400) → `TX_WV_ASYNC_UMQ_POLL` (s=5000) 的 gap = 3600ns = UMQ 硬件往返延迟
- 未出现的子阶段表示该次 IO 未经过该操作 (bitmap 未置位)

#### 1.4.3 日志定位方法

SplitTrace 输出与 ubsocket 其他日志 (UMQ/URMA/bondp 等) 混在同一日志文件中。通过以下特征快速定位 SplitTrace 输出:

**日志文件位置**:

| 角色 | 文件 | 说明 |
|------|------|------|
| Server 端 | 进程 stderr 重定向的日志文件 (如 brpc `--minloglevel=0` 输出到 stderr) | 包含 `===RX_POLL` (接收) 和 `===TX_POST` (响应) |
| Client 端 | 同上 | 包含 `===TX_POST` (发送) 和 `===RX_POLL` (接收响应) |

**识别特征**:

SplitTrace timeline 以 `===` 开头, 路径名固定为 4 种之一, 是唯一以 `===TX_` 或 `===RX_` 开头的日志:

```
I20260830 00:25:14.010256 225013 ubsocket_trace.cpp:180] [UBSOCKET DrainAll] ===RX_POLL fd=552 seq=1 s=344442723088547 e=344442723088877 d=330
===TX_POST fd=552 seq=1 s=344442723273758 e=344442723274008 d=250
===RX_POLL fd=552 seq=2 s=344442724642579 e=344442724642879 d=300
===TX_POST fd=552 seq=3 s=344442724741050 e=344442724741380 d=330
```

- 日志行前缀 `ubsocket_trace.cpp` 标识来源为 SplitTrace
- 首条 timeline 带有 `[UBSOCKET DrainAll]` 前缀, 同一批次的后续 timeline 直接以 `===` 开头 (批量缓冲区输出)
- 源文件行号 (`:180`) 对应 `DrainAll` 中的 `UBS_SLOG_INFO` 调用

**常用 grep 命令**:

```bash
# 1. 提取所有 SplitTrace timeline (=== 开头的行 + 其前缀行)
grep "===TX_\|===RX_" server.log

# 2. 查看完整 timeline (含各阶段时间戳, === 行及其后续 phase 行)
grep -A10 "===TX_WRITEV fd=42 seq=1234" server.log

# 3. 按 seq_no 跨节点关联 (同一报文在两端都被采样)
grep "seq=1234" server.log client.log

# 4. 统计各路径 trace 数量
grep -o "===TX_WRITEV\|===TX_POST\|===RX_READV\|===RX_POLL" server.log | sort | uniq -c

# 5. 统计 timeline 总条数
grep -c "===TX_\|===RX_" server.log

# 6. 查看特定连接 (fd) 的所有 trace
grep "fd=552" server.log

# 7. 提取所有 SplitTrace 相关日志 (含批量前缀行)
grep "ubsocket_trace.cpp" server.log
```

**跨节点关联方法**:

seq_no 由发送方通过 `buf_pro->imm.user_data` 写入 UMQ buf, 在 wire 上传递。接收方从 CQE 读取同一 seq_no。因此:

```
Client (发送方)                      Server (接收方)
===TX_POST fd=548 seq=500 ...        ===RX_POLL fd=552 seq=500 ...   ← 同一报文
===RX_POLL fd=548 seq=501 ...        ===TX_POST fd=552 seq=501 ...   ← 对端响应
```

`grep "seq=500" client.log server.log` 即可看到同一报文在两端的完整 TX+RX 生命周期。

> **注意**: seq_no 是 per-UMQ-handle 的序列号。Client 和 Server 各自维护独立的 seq 计数器, 但同一报文在 wire 上携带的 seq_no 是发送方写入的值, 接收方读出的也是同一值。两端使用相同的采样率 N (`seq_no % N == 0`) 即可确保同一报文同时被采样。

#### 1.4.4 功能开关与参数说明

SplitTrace 的 `GlobalTracePool` 单例在 `ubsocket_init()` 时构造空壳 (不分配内存)。功能打开时 `LazyInit()` 堆分配。**功能开关控制热路径打点和 drain 线程**, 支持两种控制方式:

**控制方式一: 环境变量 (初始化时读取)**

| 环境变量 | 默认值 | 范围 | 说明 |
|---------|--------|------|------|
| `UBSOCKET_SPLIT_TRACE_ENABLE` | false | true/false | 初始化时读取, 决定 drain 线程是否启动 |
| `UBSOCKET_SPLIT_TRACE_SAMPLE_RATE` | 100 | 1~1000 | 采样率分母 N, seq_no % N == 0 时采样。两端使用相同 N 即可跨节点关联 |
| `UBSOCKET_SPLIT_TRACE_DRAIN_INTERVAL_MS` | 10 | 1~10000 | drain 线程运行间隔 (毫秒), 控制 slot 回收速度 |

环境变量仅在 `ubsocket_init()` 时读取一次, 设置初始状态。后续可通过 ubstat 命令行动态调整。

**控制方式二: ubstat 命令行工具 (运行时动态调整)**

ubstat 工具通过 Unix domain socket 连接到目标进程, 发送 `CLICommand::SPLIT_TRACE` 命令, 服务端 `ProcessSplitTraceRequest` 处理器根据 `CLITypeParam mType` 区分操作类型:

```bash
# 打开 SplitTrace
ubstat strace -p <pid> --enable

# 关闭 SplitTrace
ubstat strace -p <pid> --disable

# 动态调整采样率 (运行时生效, 无需重启进程)
ubstat strace -p <pid> --sample-rate 1000

# 动态调整 drain 线程运行间隔 (运行时生效)
ubstat strace -p <pid> --drain-interval 50
```

**动态切换行为**:

| 当前状态 | ubstat 命令 | 动作 | 效果 |
|---------|------------|------|------|
| 关闭 | `--enable` | `LazyInit()` + `UBS_SPLIT_TRACE_ENABLED=true` + `SplitTraceDrainThread::Start()` | 堆分配 ~129KB, drain 线程启动, 热路径开始采样 |
| 关闭 | `--disable` | 无操作 (已是关闭) | 维持关闭 |
| 打开 | `--enable` | 无操作 (已是打开) | 维持打开 |
| 打开 | `--disable` | `UBS_SPLIT_TRACE_ENABLED=false` + `SplitTraceDrainThread::Stop()` | drain 线程停止, 热路径短路; 内存保留不释放 |
| 任意 | `--sample-rate <N>` | `UBS_SPLIT_TRACE_SAMPLE_RATE=N` | 热路径下次 `SplitTraceTrySample` 即用新 N 取模, 无需重启; 范围 1~1000, 超范围忽略 |
| 任意 | `--drain-interval <ms>` | `UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS=ms` | drain 线程下次 `sleep_for` 即用新间隔, 无需重启; 范围 1~10000, 超范围忽略 |

> **参数热更新无锁**: `UBS_SPLIT_TRACE_SAMPLE_RATE` 和 `UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS` 是 `uint32_t` 静态变量, 在 aarch64/x86_64 上 32 位读写是原子的。CLI 线程写, 业务线程读 `SAMPLE_RATE`, drain 线程读 `DRAIN_INTERVAL_MS` — 单写多读, 无需加锁。最坏情况: 一次 IO 用旧 N 采样, 下一次用新 N, 不影响正确性。

**生命周期**:

- **初始化**: `ubsocket_init()` 时 `GlobalTracePool` 单例构造 (仅构造空壳, 不分配 slots/lookup table)。若环境变量 `UBSOCKET_SPLIT_TRACE_ENABLE=true`, 调用 `LazyInit()` 分配内存并启动 drain 线程; 否则不分配不启动 (零内存)
- **运行时 enable**: CLI `--enable` 时调用 `LazyInit()` 分配 `slots_[256]` + `lookup_table_[8192]` (~129KB), 然后启动 drain 线程 (额外 256KB `thread_local` 批量打印缓冲区)。`LazyInit()` 幂等, 已分配时直接返回 true
- **运行时 disable**: CLI `--disable` 时停止 drain 线程, **不释放内存**。原因: 业务线程通过 `UBS_SPLIT_TRACE_ENABLED` bool 短路检查后访问 `slots_`, flag 设为 false 到实际停止访问之间存在竞态窗口, 无法保证全局 quiescence。保留内存避免 use-after-free
- **销毁**: `ubsocket_uninit()` 时先 `SplitTraceDrainThread::Stop()` (停 drain 线程), 再 `GlobalTracePool::DestroyPool()` 释放 `slots_` / `lookup_table_`。此时业务线程已停止, 无竞态风险

**参数调优建议** (均可通过 ubstat CLI 运行时动态调整, 无需重启进程):

| 场景 | 采样率 | drain 周期 | 调整命令 |
|------|--------|-----------|----------|
| 生产环境 (低开销) | 1000 | 10 | `ubstat strace -p <pid> --enable --sample-rate 1000 --drain-interval 10` |
| 问题定位 (高频采样) | 1~10 | 10 | `ubstat strace -p <pid> --enable` 然后 `--sample-rate 10` |
| 长尾分析 | 100 | 10 | `ubstat strace -p <pid> --enable` 然后 `--sample-rate 100` |
| 低负载场景 | 100 | 100 | `ubstat strace -p <pid> --enable` 然后 `--drain-interval 100` |
| 定位完毕关闭 | — | — | `ubstat strace -p <pid> --disable` |

**全局 slot 池**: `GlobalTracePool::MAX_SLOTS=256` 为编译期常量, 不通过环境变量配置。如需调整, 修改源码中 `MAX_SLOTS` 值后重新编译。256 个 slot 在 1/1000 采样率、100 连接场景下远够用。

### 1.5 IO 路径与 PHASE 定义

SplitTrace 覆盖 4 条 IO 路径, 每条路径独立定义打点枚举。一次被采样的 IO 只走一条路径, TraceSlot 使用 `path` 字段区分, 通过 4 路 union 共享存储空间:

```cpp
enum TracePath : uint8_t {
    PATH_TX_WRITEV = 0,   // POSIX writev 发送
    PATH_TX_POST   = 1,   // bigdata ubs_post 发送
    PATH_RX_READV  = 2,   // POSIX readv 接收
    PATH_RX_POLL   = 3,   // bigdata ubs_poll 接收
};
```

#### PATH_TX_WRITEV — POSIX writev 发送 (业务线程同步 + TxCqePoller 异步)

| Phase | 名称 | 线程 | 说明 |
|-------|------|------|------|
| 0 | `TX_WV_ENTRY` | 业务线程 | WriteV 入口, 状态检查 |
| 1 | `TX_WV_BUILD_IOV` | 业务线程 | BuildIovConverter, 切分 iovec 到 batch |
| 2 | `TX_WV_ALLOC_BUF` | 业务线程 | AllocTxBuf, umq_buf_alloc 分配 TX buffer |
| 3 | `TX_WV_MEM_COPY` | 业务线程 | PostSend MemCopy, 逐 buf 填充数据 |
| 4 | `TX_WV_UMQ_POST` | 业务线程 | umq_post 提交, FetchAddSeqNum 分配 seq_no |
| 5 | `TX_WV_EXIT` | 业务线程 | WriteV 返回 |
| 6 | `TX_WV_ASYNC_UMQ_POLL` | TxCqePoller | umq_poll 轮询 TX CQE |
| 7 | `TX_WV_ASYNC_PROCESS_CQE` | TxCqePoller | block DecRef, 链表遍历 |
| 8 | `TX_WV_ASYNC_BUF_FREE` | TxCqePoller | umq_buf_free 释放 UMQ buffer |
| 9 | `TX_WV_ASYNC_NOTIFY` | TxCqePoller | NotifyWritable 通知 socket 可写 |

#### PATH_TX_POST — bigdata ubs_post 发送 (业务线程同步 + TxCqePoller 异步)

| Phase | 名称 | 线程 | 说明 |
|-------|------|------|------|
| 0 | `TX_POST_ENTRY` | 业务线程 | ubs_post 入口, 参数校验 |
| 1 | `TX_POST_SENDER_POST` | 业务线程 | TrySenderPost 自适应路由 |
| 2 | `TX_POST_HANDLE_SMALL` | 业务线程 | 小段 (≤4064B) 零拷贝 SEND |
| 3 | `TX_POST_HANDLE_LARGE` | 业务线程 | 大段 READ_OFFER 控制报文 |
| 4 | `TX_POST_UMQ_POST` | 业务线程 | umq_post 提交 |
| 5 | `TX_POST_EXIT` | 业务线程 | ubs_post 返回 |
| 6 | `TX_POST_ASYNC_UMQ_POLL` | TxCqePoller | umq_poll 轮询 TX CQE |
| 7 | `TX_POST_ASYNC_PROCESS_CQE` | TxCqePoller | block DecRef |
| 8 | `TX_POST_ASYNC_BUF_FREE` | TxCqePoller | umq_buf_free |
| 9 | `TX_POST_ASYNC_NOTIFY` | TxCqePoller | NotifyWritable |

#### PATH_RX_READV — POSIX readv 接收 (业务线程同步 + epoll runner 异步)

| Phase | 名称 | 线程 | 说明 |
|-------|------|------|------|
| 0 | `RX_RV_ENTRY` | 业务线程 | ReadV 入口, 状态检查 |
| 1 | `RX_RV_POLL_RX` | 业务线程 | PollRx, 从 per-socket 队列取 buf |
| 2 | `RX_RV_HANDLE_BUF` | 业务线程 | HandleBuf, block_cache Insert |
| 3 | `RX_RV_DATA_SET` | 业务线程 | RxDataSet, 切割 block 返回数据 |
| 4 | `RX_RV_REARM` | 业务线程 | RearmRxInterrupt |
| 5 | `RX_RV_EXIT` | 业务线程 | ReadV 返回 |
| 6 | `RX_RV_ASYNC_UMQ_POLL` | epoll runner | umq_poll 轮询主 UMQ RX CQE |
| 7 | `RX_RV_ASYNC_SIFT` | epoll runner | SiftSocketEvents 分发 buf |
| 8 | `RX_RV_ASYNC_ENQUEUE` | epoll runner | AddQbuf 入 per-socket 队列 |
| 9 | `RX_RV_ASYNC_NOTIFY` | epoll runner | AddReadableEvent 唤醒业务线程 |
| 10 | `RX_RV_ASYNC_ALLOC_BUF` | epoll runner | umq_buf_alloc 补充 RX pool |
| 11 | `RX_RV_ASYNC_POST_RX` | epoll runner | umq_post 补充 RX buffer |
| 12 | `RX_RV_ASYNC_REARM` | epoll runner | umq_rearm_interrupt 重新武装 |

#### PATH_RX_POLL — bigdata ubs_poll 接收 (业务线程同步 + epoll runner 异步)

| Phase | 名称 | 线程 | 说明 |
|-------|------|------|------|
| 0 | `RX_POLL_ENTRY` | 业务线程 | ubs_poll 入口 |
| 1 | `RX_POLL_GET_AND_POP` | 业务线程 | GetAndPopQbuf 取 buf |
| 2 | `RX_POLL_BIG_CTRL` | 业务线程 | bigdata 控制报文处理 |
| 3 | `RX_POLL_DELIVER_SEG` | 业务线程 | 填充 ubs_segment_t 返回 |
| 4 | `RX_POLL_EXIT` | 业务线程 | ubs_poll 返回 |
| 5 | `RX_POLL_ASYNC_UMQ_POLL` | epoll runner | umq_poll 轮询主 UMQ RX CQE |
| 6 | `RX_POLL_ASYNC_SIFT` | epoll runner | SiftSocketEvents 分发 buf |
| 7 | `RX_POLL_ASYNC_ENQUEUE` | epoll runner | AddQbuf 入 per-socket 队列 |
| 8 | `RX_POLL_ASYNC_NOTIFY` | epoll runner | AddReadableEvent 唤醒业务线程 |
| 9 | `RX_POLL_ASYNC_ALLOC_BUF` | epoll runner | umq_buf_alloc 补充 RX pool |
| 10 | `RX_POLL_ASYNC_POST_RX` | epoll runner | umq_post 补充 RX buffer |
| 11 | `RX_POLL_ASYNC_REARM` | epoll runner | umq_rearm_interrupt |

**设计要点**:
- 4 条路径各独立枚举: TX writev 10 个, TX ubs_post 10 个, RX readv 13 个, RX ubs_poll 12 个
- 一次 IO 只走一条路径 → TraceSlot 用 4 路 union, 数组大小取 `max(10,10,13,12)=13` — 无浪费
- 同步段 (0~5) 由业务线程写, 异步段 (6~12) 由 poller/runner 线程写 — 同一枚举内不同索引, 天然无竞争
- 异步段在不同路径中重复定义 (如 `TX_WV_ASYNC_UMQ_POLL` 和 `TX_POST_ASYNC_UMQ_POLL`)，因为它们统计的是**不同路径 CQE 的处理延迟**

---

## 2. 实现机制

### 2.1 架构概览

```
┌─────────────────────────────────────────────────────────────────┐
│                   GlobalTracePool (singleton)                   │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ...            │
│  │ TraceSlot 0 │ │ TraceSlot 1 │ │ TraceSlot 2 │ (MAX=256)      │
│  │ TX_WV DONE  │ │ TX_WV TRACE │ │ IDLE        │                │
│  │ fd=42 tx_wv │ │ fd=83 tx_wv │ │             │                │
│  └──────┬──────┘ └──────┬──────┘ └─────────────┘                │
│         │               │                                       │
└─────────┼───────────────┼───────────────────────────────────────┘
          │               │
    ┌─────┴────┐    ┌─────┴────┐
    │ 业务线程  │    │ 业务线程  │
    │ 写 TX_WV: │    │ 写 TX_WV: │
    │ ENTRY     │    │ ENTRY     │
    │ BUILD_IOV │    │ BUILD_IOV │
    │ MEM_COPY  │    │ MEM_COPY  │
    │ UMQ_POST  │    │ UMQ_POST  │
    └──────────┘    └──────────┘
          │               │
          │ seq_no 随 umq_buf 跨线程传递
          ▼               ▼
    ┌──────────┐    ┌──────────┐
    │ Poller   │    │ Poller   │
    │ FindSlot │    │ FindSlot │
    │ by seq_no│    │ by seq_no│
    │ + fd     │    │ + fd     │
    │ 写 TX_WV: │    │ 写 TX_WV: │
    │ ASYNC_   │    │ ASYNC_   │
    │ UMQ_POLL │    │ UMQ_POLL │
    │ PROCESS_ │    │ PROCESS_ │
    │ CQE      │    │ CQE      │
    │ BUF_FREE │    │ BUF_FREE │
    └──────────┘    └──────────┘

DrainThread (10ms 周期):
  遍历 GlobalTracePool 的 256 个 slot, 检查 DONE slot
  宽限期已过的 DONE slot → 输出 phase 时序 (已天然完整)
  slot Reset → IDLE (可被下次采样复用)
  无需跨线程合并 — 业务线程和 poller 写同一 slot 的不同 phase 索引
```

### 2.2 采样约束

| 约束 | 含义 |
|------|------|
| **确定性 1/N 采样率** | 基于 UMQ seq_no 取模判定 (seq_no % N == 0), N=`UBS_SPLIT_TRACE_SAMPLE_RATE`, 默认 100。两端看到同一 seq_no, 独立做出相同采样决策, 确保跨节点关联。不区分 socket / 线程, 均匀覆盖全部连接 |
| **固定 slot 池** | 全局共享 256 个 slot (`GlobalTracePool`, 编译期常量)。满了则停止采样直到 drain 释放 |

### 2.3 采样状态机 (per-slot)

```
                           seq_no % N == 0
                           且有空闲 slot
       ┌──────────┐     ┌──────────────┐     ┌──────────────┐
       │ IDLE     │────▶│ TRACING      │────▶│ DONE (slot)  │
       │ no trace │     │ IO in progress│     │ trace stored │
       └──────────┘     └──────────────┘     └──────┬───────┘
            ▲                │                       │
            │                │ IO return             │ drain consumes
            │ seq_no         │ (slot state→DONE)     │ (slot Reset→IDLE)
            │ % N != 0       │                       │
            │ or slots满     │                       │
            │                ▼                       │
            └────────────────────────────────────────┘
```

| 状态 | 含义 | 转换条件 |
|------|------|---------|
| **IDLE** | 无采样进行中 | IO 开始时检查：`seq_no % N == 0` && seq_no ≠ 0 && 有空闲 slot → TRACING；否则保持 IDLE |
| **TRACING** | 正在采样当前 IO，子阶段写入 TraceSlot (按 4 路 TracePath 索引) | IO 返回时 → DONE (slot state→DONE) |
| **DONE** | trace 已完成，存储在 GlobalTracePool 中等待 drain | drain 消费后 → IDLE (slot Reset) |

**确定性采样**: 基于 UMQ seq_no 取模判定 (`seq_no % N == 0`)。seq_no 是 UMQ 级别的序列号, 由 `FetchAddSeqNum(1)` 原子递增, 写入 `buf_pro->imm.user_data`, 随报文在 wire 上传递。接收方从 CQE 的 `buf_pro->imm.user_data` 读出同一个值。两端看到同一 seq_no, 独立计算 `% N`, 结果必然一致 — 确保跨节点同一报文同时被采样。取模运算在 aarch64/x86 上均为单条指令 (~1ns), 对微秒级热路径影响可忽略。seq_no == 0 跳过 (无效/探测包)。

#### 2.3.1 无锁 slot 分配与 O(1) 查找

**问题**: 40000 连接 × 1000 QPS = 40M QPS。poller 线程对**每个 CQE** 都调用 `FindSlot(seq_no, fd, path)` 查找 slot。若遍历 256 个 slot, 40M × 256 = 10.24B 次 atomic load/s ≈ 10 个 CPU 核, 不可接受。

**方案: 直接映射查找表 (direct-mapped lookup table)**

`GlobalTracePool` 内嵌 `lookup_table_` (LazyInit 堆分配, `LOOKUP_SIZE` 个 entry), 每个 entry 为 `std::atomic<uint64_t>`, 打包 `(seq_no, fd, slot_idx)`:

- `AllocSlot` 成功 → `lookup_table_[MixLookupKey(seq_no, fd) & LOOKUP_MASK].store(packed)` 注册
- `FindSlot` → `lookup_table_[MixLookupKey(seq_no, fd) & LOOKUP_MASK].load()` 一次原子读, 匹配 `(seq_no, fd)` → O(1) 直接返回 slot
- `Reset` (drain) → `lookup_table_[MixLookupKey(seq_no, fd) & LOOKUP_MASK].store(0)` 清除

**MixLookupKey 混合函数**: 将 `(seq_no, fd)` 混合为查找表索引, 消除同 seq_no 不同 fd 的必然碰撞:

```cpp
static uint32_t MixLookupKey(uint32_t seqNo, int fd) noexcept
{
    return seqNo ^ (static_cast<uint32_t>(fd) * 0x9E3779B9u);  // Knuth 黄金比例常数
}
```

开销: 1 次乘法 + 1 次 XOR ≈ ~1ns。引入 fd 因子后, 不同连接的相同 seq_no 映射到不同 bucket, 碰撞概率从 100% 降到 ~1/8192。

**碰撞处理**: 两个活跃 trace 的 `MixLookupKey(seq_no, fd) & LOOKUP_MASK` 相同时, `FindSlot` 读到的 entry 可能匹配到错误者 → 回退到遍历 256 slot (仅碰撞时)。`LOOKUP_SIZE=8192` 时, 80 个活跃 trace 的碰撞概率 ~1%, 99% 的查找为 O(1)。

```cpp
class GlobalTracePool {
    static constexpr uint16_t MAX_SLOTS = 256;
    static constexpr uint32_t LOOKUP_SIZE = 8192;  // 2 的幂, % 编译为 &

    // 打包格式: [31:0]=seq_no, [55:32]=fd, [63:56]=slot_idx (0~255)
    // 注意: slotIdx 直接存储 (不加 1), 空条目用 packed==0 表示
    // 前提: seq_no==0 在 SplitTraceTrySample 中已跳过, 故 slotIdx=0 时 packed 不为 0
    static uint32_t MixLookupKey(uint32_t seqNo, int fd) noexcept
    {
        return seqNo ^ (static_cast<uint32_t>(fd) * 0x9E3779B9u);
    }
    static uint64_t PackLookup(uint32_t seqNo, int fd, uint16_t slotIdx) noexcept
    {
        return (static_cast<uint64_t>(seqNo))
             | (static_cast<uint64_t>(static_cast<uint32_t>(fd)) << 32)
             | (static_cast<uint64_t>(slotIdx) << 56);
    }
    static bool UnpackLookup(uint64_t packed, uint32_t seqNo, int fd, uint16_t &outSlot) noexcept
    {
        if (packed == 0) return false;  // empty
        if (static_cast<uint32_t>(packed) != seqNo) return false;
        if (static_cast<int32_t>((packed >> 32) & 0xFFFFFF) != fd) return false;
        outSlot = static_cast<uint16_t>(packed >> 56);
        return true;
    }

    TraceSlot *slots_{nullptr};              // LazyInit() 堆分配
    std::atomic<uint16_t> next_hint_{0};
    std::atomic<uint64_t> *lookup_table_{nullptr};  // LazyInit() 堆分配

    bool LazyInit() noexcept;                // 堆分配 slots_ + lookup_table_
    void DestroyPool() noexcept;             // 释放 slots_ + lookup_table_
    int16_t AllocSlot(uint32_t seqNo, int fd, uint8_t path) noexcept;
    TraceSlot *FindSlot(uint32_t seqNo, uint8_t path, int fd) noexcept;
    void ClearLookup(uint32_t seqNo, int fd) noexcept;
};

int16_t GlobalTracePool::AllocSlot(uint32_t seqNo, int fd, uint8_t path) noexcept
{
    // 1. 轮转起点 CAS 扫描 IDLE slot
    uint16_t start = next_hint_.load(std::memory_order_relaxed);
    for (uint16_t k = 0; k < MAX_SLOTS; k++) {
        uint16_t i = (start + k) % MAX_SLOTS;
        uint8_t expected = TraceSlot::STATE_IDLE;
        if (slots_[i].state.compare_exchange_strong(
                expected, TraceSlot::STATE_TRACING, std::memory_order_acq_rel)) {
            next_hint_.store(static_cast<uint16_t>(i + 1), std::memory_order_relaxed);

            // 2. 初始化 slot 元数据 (独占, 无竞争)
            auto &slot = slots_[i];
            slot.path = path;
            slot.seq_no = seqNo;
            slot.fd = fd;
            slot.io_start_ts = ubsocket_get_timeNs_compile();
            slot.io_end_ts = 0;
            slot.data_size = 0;
            slot.offset = 0;
            switch (path) {
                case PATH_TX_WRITEV: slot.phases.tx_writev.ResetBitmap(); break;
                case PATH_TX_POST:   slot.phases.tx_post.ResetBitmap();   break;
                case PATH_RX_READV:  slot.phases.rx_readv.ResetBitmap();  break;
                case PATH_RX_POLL:   slot.phases.rx_poll.ResetBitmap();   break;
                default: break;
            }

            // 3. 注册到查找表 — 后续 FindSlot 可 O(1) 命中
            lookup_table_[MixLookupKey(seqNo, fd) & (LOOKUP_SIZE - 1)].store(
                PackLookup(seqNo, fd, static_cast<uint16_t>(i)), std::memory_order_release);

            return static_cast<int16_t>(i);
        }
    }
    return -1;  // 采样饱和
}

TraceSlot *GlobalTracePool::FindSlot(uint32_t seqNo, uint8_t path, int fd) noexcept
{
    // 快速路径: O(1) 直接映射查找 — 1 次 atomic load
    uint64_t packed = lookup_table_[MixLookupKey(seqNo, fd) & (LOOKUP_SIZE - 1)].load(std::memory_order_acquire);
    uint16_t slotIdx;
    if (UnpackLookup(packed, seqNo, fd, slotIdx)) {
        uint8_t s = slots_[slotIdx].state.load(std::memory_order_acquire);
        if (s == TraceSlot::STATE_TRACING || s == TraceSlot::STATE_DONE) {
            return &slots_[slotIdx];
        }
    }

    // 慢速路径: 碰撞或未命中 — 遍历 256 slot (仅 ~2% 的调用走到这里)
    bool isTx = IsTxPath(path);
    for (uint16_t i = 0; i < MAX_SLOTS; i++) {
        uint8_t s = slots_[i].state.load(std::memory_order_acquire);
        if ((s == TraceSlot::STATE_TRACING || s == TraceSlot::STATE_DONE)
            && slots_[i].seq_no == seqNo
            && IsTxPath(slots_[i].path) == isTx
            && slots_[i].fd == fd) {
            return &slots_[i];
        }
    }
    return nullptr;
}

void GlobalTracePool::ClearLookup(uint32_t seqNo, int fd) noexcept
{
    // drain 消费 slot 后清除查找表条目
    // 注意: 仅当 lookup_table 条目的 (seq_no, fd) 匹配时才清除, 避免清除已被新 trace 复用的条目
    uint32_t idx = MixLookupKey(seqNo, fd) & (LOOKUP_SIZE - 1);
    uint64_t packed = lookup_table_[idx].load(std::memory_order_relaxed);
    if (packed != 0 && static_cast<uint32_t>(packed) == seqNo &&
        static_cast<int32_t>((packed >> 32) & 0xFFFFFF) == fd) {
        lookup_table_[idx].store(0, std::memory_order_relaxed);
    }
}
```

**为什么 O(1) 安全**:

| 操作 | 并发场景 | 安全性 |
|------|---------|--------|
| `lookup_table_.store` (AllocSlot) | 业务线程独占 (CAS 成功后) | 无竞争, `release` 保证 slot 元数据对 `FindSlot` 的 `acquire` 读可见 |
| `lookup_table_.load` (FindSlot) | poller/业务线程读 | `acquire` 原子读, 无数据竞争 |
| `ClearLookup` (drain) | drain 线程, 宽限期后 | 宽限期保证无业务/poller 访问该 slot, `ClearLookup` 检查 seq_no 匹配后才清除, 不会误清新 trace 的条目 |
| 碰撞 (两个 trace 映射到同一 entry) | AllocSlot B 覆盖了 AllocSlot A 的 entry | A 的 `FindSlot` 读到 B 的 entry → `UnpackLookup` 中 seq_no 或 fd 不匹配 → 回退到遍历 → 正确但慢 |

### 2.4 核心数据结构

#### 2.4.1 `TraceSlot` — 单次采样的完整存储

```cpp
// ubsocket_trace.h

struct TraceSlot {
    static constexpr uint32_t INVALID_SEQ = 0;

    std::atomic<uint8_t> state{0};       // 0=IDLE, 1=TRACING, 2=DONE
    uint8_t path{0};                      // TracePath: TX_WRITEV/TX_POST/RX_READV/RX_POLL
    uint32_t seq_no{0};
    int32_t fd{-1};
    uint64_t io_start_ts{0};
    uint64_t io_end_ts{0};
    uint32_t data_size{0};
    uint32_t offset{0};

    template <uint16_t N>
    struct PhaseData {
        uint64_t phase_start[N];
        uint64_t phase_end[N];
        std::atomic<uint64_t> phase_bitmap[(N + 63) / 64];
        void ResetBitmap() noexcept;
        void RecordPhase(uint16_t idx, uint64_t s, uint64_t e) noexcept;
        bool HasPhase(uint16_t idx) const noexcept;
    };

    union {
        PhaseData<TX_WV_PHASE_COUNT>  tx_writev;  // 10 phases
        PhaseData<TX_POST_PHASE_COUNT> tx_post;    // 10 phases
        PhaseData<RX_RV_PHASE_COUNT>   rx_readv;   // 13 phases
        PhaseData<RX_POLL_PHASE_COUNT>  rx_poll;    // 12 phases
    } phases;

    void Reset() noexcept;
    void RecordPhase(uint8_t p, uint16_t idx, uint64_t s, uint64_t e) noexcept;
};
```

**内存布局** (max(10,10,13,12) = 13):

```
sizeof(TraceSlot):
  元数据:        1+1+4+4+8+8+4+4 = 34 bytes + padding ≈ 40 bytes
  union phases:  max(
                   TX_WV:    10×8×2 + 8 = 168 bytes
                   TX_POST:  10×8×2 + 8 = 168 bytes
                   RX_RV:    13×8×2 + 8 = 216 bytes
                   RX_POLL:  12×8×2 + 8 = 200 bytes
                 ) = 216 bytes
  ─────────────────────────────────────
  总计:          ~256 bytes (对齐)
```

#### 2.4.2 全局 GlobalTracePool

slot 数量固定为编译期常量 `GlobalTracePool::MAX_SLOTS = 256`，全局共享，不随采样率变化。数组在 `LazyInit()` 时堆分配，所有 socket 共享。

```cpp
// ubsocket_trace.h
class GlobalTracePool {
public:
    static constexpr uint16_t MAX_SLOTS = 256;
    static constexpr uint32_t LOOKUP_SIZE = 8192;  // O(1) 查找表大小 (2 的幂)
    static constexpr uint32_t LOOKUP_MASK = LOOKUP_SIZE - 1;

    static GlobalTracePool &Instance();
    bool LazyInit() noexcept;                        // 堆分配 slots_ + lookup_table_, 幂等
    void DestroyPool() noexcept;                     // 释放 slots_ + lookup_table_
    int16_t AllocSlot(uint32_t seqNo, int fd, uint8_t path) noexcept;
    void EndSlot(int16_t slotIdx, uint32_t dataSize, uint32_t offset) noexcept;
    TraceSlot *FindSlot(uint32_t seqNo, uint8_t path, int fd) noexcept;
    TraceSlot &Slot(int16_t idx) noexcept;
    void DrainAll(uint64_t now) noexcept;
    void ClearLookup(uint32_t seqNo, int fd) noexcept;

    std::atomic<uint16_t> done_count_{0};

    static uint32_t MixLookupKey(uint32_t seqNo, int fd) noexcept;
    static uint64_t PackLookup(uint32_t seqNo, int fd, uint16_t slotIdx) noexcept;
    static bool UnpackLookup(uint64_t packed, uint32_t seqNo, int fd, uint16_t &outSlot) noexcept;

private:
    GlobalTracePool() = default;
    ~GlobalTracePool() { DestroyPool(); }

    TraceSlot *slots_{nullptr};                      // LazyInit() 堆分配
    std::atomic<uint16_t> next_hint_{0};             // AllocSlot 轮转扫描起点
    std::atomic<uint64_t> *lookup_table_{nullptr};   // LazyInit() 堆分配, O(1) 查找表
};
```

> `GlobalTracePool::MAX_SLOTS=256` 是编译期常量，可通过编译宏调整。`LOOKUP_SIZE=8192` 为 O(1) 查找表大小, 内存 64KB, 碰撞率 ~1%。功能打开时 `LazyInit()` 分配 256×256B + 8192×8B = ~129KB; drain 线程额外 256KB `thread_local` 批量打印缓冲区。功能关闭时不分配 (零内存)。不随连接数增长。Socket 无需存储任何 trace 相关字段。

#### 2.4.3 并发安全性分析

**TX writev 路径** (path=PATH_TX_WRITEV):

| 操作 | 线程 | 写入位置 | 竞争? |
|------|------|---------|-------|
| `AllocSlot` CAS(IDLE→TRACING) | 多业务线程 | `slot.state` | CAS 保护, 失败者继续扫描下一个 slot |
| `lookup_table_.store` (AllocSlot 注册) | 业务线程 (独占 slot 后) | `lookup_table_[MixLookupKey(seq_no,fd)&MASK]` | `release` 写, FindSlot 的 `acquire` 读可见; 碰撞时后者覆盖前者, 前者回退到遍历 |
| `lookup_table_.load` (FindSlot) | poller/业务线程 | `lookup_table_[MixLookupKey(seq_no,fd)&MASK]` | `acquire` 读, 无数据竞争 |
| `ClearLookup` (drain) | drain 线程 | `lookup_table_[MixLookupKey(seq_no,fd)&MASK]` | 宽限期后独占, 检查 (seq_no, fd) 匹配后才清除 |
| `next_hint_.load/store` | 多业务线程 | `next_hint_` | `relaxed` 原子, hint 不影响正确性 |
| `RecordPhase(PATH_TX_WRITEV, TX_WV_ENTRY, ...)` | 业务线程 | `phases.tx_writev.phase_start[0]` | 无 (独占, CAS 成功后 slot 独占) |
| `RecordPhase(PATH_TX_WRITEV, TX_WV_MEM_COPY, ...)` | 业务线程 | `phases.tx_writev.phase_start[3]` | 无 (独占) |
| `RecordPhase(PATH_TX_WRITEV, TX_WV_UMQ_POST, ...)` | 业务线程 | `phases.tx_writev.phase_start[4]` | 无 (独占) |
| `RecordPhase(PATH_TX_WRITEV, TX_WV_ASYNC_UMQ_POLL, ...)` | poller 线程 | `phases.tx_writev.phase_start[6]` | 无 (不同索引) |
| `RecordPhase(PATH_TX_WRITEV, TX_WV_ASYNC_PROCESS_CQE, ...)` | poller 线程 | `phases.tx_writev.phase_start[7]` | 无 (不同索引) |
| `state` store(TRACING→DONE) | 业务线程 | `slot.state` | release 写, poller 的 acquire 读能看到 |
| `phase_bitmap` 写 | 业务线程 + poller | 不同 bit 位 | `fetch_or` 原子保护 |
| `Reset` (DONE→IDLE) | drain 线程 | `slot.state` + 全部字段 | drain 线程独占 (宽限期保证无业务/poller 写) |

**RX readv 路径** (path=PATH_RX_READV):

| 操作 | 线程 | 写入位置 | 竞争? |
|------|------|---------|-------|
| `RecordPhase(PATH_RX_READV, RX_RV_ENTRY, ...)` | 业务线程 | `phases.rx_readv.phase_start[0]` | 无 (独占) |
| `RecordPhase(PATH_RX_READV, RX_RV_HANDLE_BUF, ...)` | 业务线程 | `phases.rx_readv.phase_start[2]` | 无 (独占) |
| `RecordPhase(PATH_RX_READV, RX_RV_ASYNC_UMQ_POLL, ...)` | epoll runner | `phases.rx_readv.phase_start[6]` | 无 (不同索引) |
| `RecordPhase(PATH_RX_READV, RX_RV_ASYNC_ENQUEUE, ...)` | epoll runner | `phases.rx_readv.phase_start[8]` | 无 (不同索引) |
| `state` CAS(IDLE→TRACING) | 业务线程 (AllocSlot) | `slot.state` | CAS 保护, 失败者继续扫描 |
| `phase_bitmap` 写 | 业务线程 + epoll runner | 不同 bit 位 | `fetch_or` 原子保护 |

**4 路隔离**: 同一 slot 的 `path` 在 `GlobalTracePool::AllocSlot` 时确定，后续所有打点只访问 `phases.tx_writev` / `phases.tx_post` / `phases.rx_readv` / `phases.rx_poll` 之一。4 条路径不会写同一 slot 的同一内存区域。

**phase_bitmap 并发写**: 业务线程和 poller/runner 线程写同一 `uint64_t` 字中的不同 bit。使用 `std::atomic<uint64_t>` + `fetch_or` 保证原子性:

```cpp
std::atomic_thread_fence(std::memory_order_release);
phase_bitmap[idx / 64].fetch_or(1ULL << (idx % 64), std::memory_order_relaxed);
```

> `fetch_or` 在 x86 上编译为 `lock or`（~10ns），在 aarch64 上编译为 `ldset`（~5ns）。考虑到采样率 1/100，此开销可忽略。

### 2.5 采样生命周期

#### 2.5.1 核心操作

状态机定义见 §2.3, 无锁 slot 分配见 §2.3.1。此处给出 `SplitTraceTrySample` / `SplitTraceAdd` / `SplitTraceEndSample` / `SplitTraceAddSampled` 的实现。

```cpp
// ========== 业务线程: IO 入口采样决策 ==========

bool SplitTraceTrySample(Socket *sock, int fd, uint32_t seq_no, uint8_t path)
{
    if (sock == nullptr || !GlobalSetting::UBS_SPLIT_TRACE_ENABLED) {
        return false;
    }

    // 确定性决策: seq_no % N, 两端一致
    if (seq_no == 0) {
        return false;  // 无效/探测包
    }
    uint32_t rate = GlobalSetting::UBS_SPLIT_TRACE_SAMPLE_RATE;
    if (rate == 0) {
        rate = 100;
    }
    if (seq_no % rate != 0) {
        return false;  // 未命中采样
    }

    // 命中采样: 无锁分配 slot (见 §2.3.1)
    int16_t slot_idx = GlobalTracePool::Instance().AllocSlot(seq_no, fd, path);
    return slot_idx >= 0;
}

// ========== 业务线程: 采样中的子阶段记录 ==========

void SplitTraceAdd(Socket *sock, uint8_t path, uint16_t idx,
                   uint32_t seqNo, uint64_t startTs, uint64_t endTs)
{
    // 通过 (seq_no, fd, path) 在全局池中查找 slot
    TraceSlot *slot = GlobalTracePool::Instance().FindSlot(seqNo, path, sock->raw_socket_);
    if (slot != nullptr) {
        slot->RecordPhase(path, idx, startTs, endTs);
    }
}

// ========== 业务线程: IO 返回时结束采样 ==========

void SplitTraceEndSample(Socket *sock, uint8_t path, uint32_t seqNo,
                         uint32_t data_size, uint32_t offset)
{
    TraceSlot *slot = GlobalTracePool::Instance().FindSlot(seqNo, path, sock->raw_socket_);
    if (slot != nullptr) {
        slot->io_end_ts = ubsocket_get_timeNs_compile();
        slot->data_size = data_size;
        slot->offset = offset;
        slot->state.store(TraceSlot::STATE_DONE, std::memory_order_release);
        GlobalTracePool::Instance().done_count_.fetch_add(1, std::memory_order_relaxed);
    }
}

// ========== poller/runner 线程: 异步子阶段写入 ==========

void SplitTraceAddSampled(int fd, uint8_t path, uint16_t idx,
                          uint32_t seqNo, uint64_t startTs, uint64_t endTs)
{
    // 与 SplitTraceAdd 相同 — 通过 (seq_no, fd, path) 查找 slot
    TraceSlot *slot = GlobalTracePool::Instance().FindSlot(seqNo, path, fd);
    if (slot != nullptr) {
        slot->RecordPhase(path, idx, startTs, endTs);
    }
}
```

> `FindSlot` 的 O(1) 查找表实现见 §2.3.1。`Reset` (drain 消费 slot) 时需调用 `ClearLookup(seq_no, fd)` 清除查找表条目。

> **业务线程的 `SplitTraceAdd` 也使用 `FindSlot`**: 业务线程每次通过 `(seq_no, fd, path)` 查找 slot。由于 `SplitTraceAdd` 在方案 A 中是 PostSend 返回后的批量写入 (同一函数内连续调用多次), 可优化为首次 `FindSlot` 缓存 slot 指针到局部变量, 后续直接使用 (见 3.3 调用示例)。

#### 2.5.2 一次 WriteV 的完整采样流程

```
业务线程:
  WriteV 入口
    │ 方案A (延迟写入): 不在入口调用 TryStartSample, 仅记录时间戳
    │ ... 各子阶段时间戳记录在局部变量中 ...
    │ PostSend 内部: FetchAddSeqNum + umq_post
    │ PostSend 返回后:
    │   seq_no = umq_socket->LoadSeqNum()
    │   do_trace = STRACE_TRY_SAMPLE(sock, fd, seq_no, PATH_TX_WRITEV)
    │   if (do_trace):
    │     STRACE_ADD(sock, PATH_TX_WRITEV, TX_WV_ENTRY, seq_no, ts_entry_start, ts_entry_end)
    │     STRACE_ADD(sock, PATH_TX_WRITEV, TX_WV_BUILD_IOV, seq_no, ...)
    │     STRACE_ADD(sock, PATH_TX_WRITEV, TX_WV_ALLOC_BUF, seq_no, ...)
    │     STRACE_ADD(sock, PATH_TX_WRITEV, TX_WV_MEM_COPY, seq_no, ...)
    │     STRACE_ADD(sock, PATH_TX_WRITEV, TX_WV_UMQ_POST, seq_no, ...)
    │     STRACE_END(sock, PATH_TX_WRITEV, seq_no, tx_total_len, 0)
    │
    ▼ WriteV 返回

TxCqePoller 线程 (异步):
  umq_poll → buf completion
    │ seq_no = buf_pro->imm.user_data
    │ slot = GlobalTracePool::FindSlot(seq_no, PATH_TX_WRITEV, fd)
    │ if (slot != nullptr):
    │     STRACE_SAMPLED(sock, PATH_TX_WRITEV, TX_WV_ASYNC_UMQ_POLL, seq_no, ...)
    │     STRACE_SAMPLED(sock, PATH_TX_WRITEV, TX_WV_ASYNC_PROCESS_CQE, seq_no, ...)
    │     STRACE_SAMPLED(sock, PATH_TX_WRITEV, TX_WV_ASYNC_BUF_FREE, seq_no, ...)
    │     STRACE_SAMPLED(sock, PATH_TX_WRITEV, TX_WV_ASYNC_NOTIFY, seq_no, ...)

Drain 线程 (10ms 周期):
  遍历 GlobalTracePool 的 256 个 slot, 消费过期 DONE slot
    → 每 100 次 drain (≈1s) 打印 1 次 timeline (最多 6 个 slot 合并)
    → slot Reset → IDLE
```

#### 2.5.3 确定性采样的时序图

```
GlobalTracePool, sample_rate=100, MAX_SLOTS=256

socket fd=42 TX 方向:
  IO #1:  seq_no=99   99%100 != 0   → 不采样
  IO #2:  seq_no=100  100%100 == 0  → AllocSlot(seq=100, fd=42, PATH_TX_WRITEV) → slot[0] TRACING
          │  EndSlot → slot[0] DONE

socket fd=42 RX 方向 (与 TX 并发, 共享全局 slot 池):
  IO #3:  seq_no=200  200%100 == 0  → AllocSlot(seq=200, fd=42, PATH_RX_READV) → slot[1] TRACING
          │  EndSlot → slot[1] DONE

socket fd=83 TX 方向 (与 fd=42 并发, 共享 slot 池):
  IO #4:  seq_no=201  201%100 != 0  → 不采样
  IO #5:  seq_no=300  300%100 == 0  → AllocSlot(seq=300, fd=83, PATH_TX_WRITEV) → slot[2] TRACING
```

> **关键**: 采样决策基于 **seq_no % N** — 两端看到同一 seq_no, 独立做出相同采样决策, 确保跨节点同一报文同时被采样。同一 socket 的 TX 和 RX 可以同时采样 (各自占用不同 slot)。容量约束是 **全局共享** 的 — 所有 socket 的 TX 和 RX 从同一个全局 GlobalTracePool(256) 中分配 slot。跨节点关联: 通过 grep `seq=<N>` 两端日志, 即可看到同一报文的完整 TX+RX 生命周期。

### 2.6 数据量估算

假设 100 个 socket, 1000 QPS/socket, 总 100,000 QPS, 1/1000 采样率:

全局每 10ms 采样 trace 数: 100,000 / 1000 / 100 = 1 条 (全局均匀采样)
每条 trace ~10 个子阶段条目
drain 每 10ms 处理: 1 × 10 = 10 条目 (O(0.01ms) 遍历, 可忽略)

假设 8192 个 socket, 999 QPS/socket, 总 ~8.19M QPS, 1/100 采样率:
全局每 10ms 采样 trace 数: 8,190,000 / 100 / 100 = 819 条
drain 每 10ms 处理: 819 × 10 = 8190 条目 (O(1ms) 遍历, 可接受)
宽限期 2ms 内累积: 819 × (2ms / 10ms) ≈ 164 个 slot << 256? → 1/100 采样率下 8192 socket 可能饱和, 建议 1/1000

内存: 功能关闭 0; 功能打开 ~129KB (LazyInit 堆分配) + 256KB (drain 线程批量打印缓冲区), 不随连接数增长

### 2.7 drain 消费与 slot 释放

drain 线程在宽限期过后消费 DONE slot，批量格式化后一次 log 调用输出，然后 Reset slot:

```cpp
void GlobalTracePool::DrainAll(uint64_t now) noexcept
{
    static constexpr uint64_t GRACE_PERIOD_NS = 2000000;  // 2ms
    static thread_local char batch_buf[256 * 1024];
    int batch_pos = 0;
    int batch_count = 0;

    for (uint16_t i = 0; i < MAX_SLOTS; i++) {
        if (slots_[i].state.load(std::memory_order_acquire) != TraceSlot::STATE_DONE) continue;
        if (now < slots_[i].io_end_ts || now - slots_[i].io_end_ts < GRACE_PERIOD_NS) continue;
        if (batch_pos < static_cast<int>(sizeof(batch_buf)) - 2048) {
            int n = SplitTraceDrainThread::FormatSlot(batch_buf + batch_pos,
                                                       sizeof(batch_buf) - batch_pos, slots_[i]);
            batch_pos += n;
            batch_count++;
        }
        ClearLookup(slots_[i].seq_no, slots_[i].fd);  // 清除 O(1) 查找表条目
        slots_[i].Reset();
        done_count_.fetch_sub(1, std::memory_order_relaxed);
    }
    if (batch_count > 0) {
        UBS_SLOG_INFO(std::string(batch_buf, static_cast<size_t>(batch_pos)));
    }
}
```

无需 `ReleaseTraceSlots` / `DrainSampledSeqs` — slot Reset 即释放。drain 线程直接遍历全局 256 个 slot, 无需遍历 ArraySet<Socket>。

**drain 输出示例**:

```
FormatSlot(slot[0]) → 追加到 batch_buf:
   ├── 格式化头部: ===TX_WRITEV fd=42 seq=1234 s=1000 e=1401 d=401
   ├── path==PATH_TX_WRITEV → 遍历 tp = 0..TX_WV_PHASE_COUNT-1:
   │   ├── tp=0: bitmap bit 0 ✓ → TX_WV_ENTRY,s=1000,e=1050,d=50
   │   ├── tp=1: bitmap bit 1 ✓ → TX_WV_BUILD_IOV,s=1050,e=1100,d=50
   │   ├── ...
   └── 批量输出: UBS_SLOG_INFO(batch_buf) 一次 log 调用
   └── slot[0].Reset() → state=0(IDLE)
```

**为什么用定长数组而不是变长列表**:

| 方案 | 优点 | 缺点 |
|------|------|------|
| 定长数组 `phase[13]` | O(1) 索引写入, 无分配, 不同索引无竞争 | 未使用的索引占少量内存 (但 256B 可忽略) |
| 变长 `vector<Entry>` | 紧凑 | 写入需 push_back + 可能 realloc, 多线程竞争 |
| 链表 | 无限扩展 | 缓存不友好, 分配开销, 多线程竞争 |

定长数组的核心优势: **业务线程写 `phase_start[TX_WV_MEM_COPY]`, poller 线程写 `phase_start[TX_WV_ASYNC_UMQ_POLL]` — 不同的 `TxWritevPhase` 值 = 不同的数组索引 = 不同的内存地址 = 天然无数据竞争**。

### 2.8 CLI 参数与协议映射

ubstat 工具通过 Unix domain socket 连接到目标进程, 发送 `CLICommand::SPLIT_TRACE` 命令, 服务端 `ProcessSplitTraceRequest` 处理器根据 `CLITypeParam mType` 区分操作类型。`CLIControlHeader` 已有 `mType` (CLITypeParam) 和 `mValue` (double) 字段, 复用 `delay` 命令的模式 — 每次 CLI 调用执行一个操作, 通过 `mType` 区分操作类型, `mValue` 携带数值参数:

| CLI 选项 | `mType` | `mValue` | 说明 |
|----------|---------|----------|------|
| `--enable` | `INVALID` (走 `mSwitch`) | 不使用 | 启动 SplitTrace |
| `--disable` | `INVALID` (走 `mSwitch`) | 不使用 | 停止 SplitTrace |
| `--sample-rate <N>` | `SPLIT_TRACE_OP_SET_SAMPLE_RATE` (新增) | N (1~1000) | 设置 `UBS_SPLIT_TRACE_SAMPLE_RATE` |
| `--drain-interval <ms>` | `SPLIT_TRACE_OP_SET_DRAIN_INTERVAL` (新增) | ms (1~10000) | 设置 `UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS` |

> **设计约束**: `CLIControlHeader` 仅有一个 `mValue` 字段, 因此 `--sample-rate` 和 `--drain-interval` 需分两次 CLI 调用设置。这与 `delay` 命令 (`-t interval -v 3` 或 `-t path -v /tmp`) 的单次单操作模式一致。

**`ProcessSplitTraceRequest` 处理逻辑**:

```cpp
void ProcessSplitTraceRequest(int fd, CLIMessage &msg, CLIControlHeader &header)
{
    (void)msg;
    if (header.mType == CLITypeParam::SPLIT_TRACE_OP_SET_SAMPLE_RATE) {
        uint32_t rate = static_cast<uint32_t>(header.mValue);
        if (rate >= 1 && rate <= 1000) {
            GlobalSetting::UBS_SPLIT_TRACE_SAMPLE_RATE = rate;
        }
    } else if (header.mType == CLITypeParam::SPLIT_TRACE_OP_SET_DRAIN_INTERVAL) {
        uint32_t interval = static_cast<uint32_t>(header.mValue);
        if (interval >= 1 && interval <= 10000) {
            GlobalSetting::UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS = interval;
        }
    } else {
        // enable/disable 走 mSwitch 位标记 (向后兼容)
        bool enable = header.GetSwitch(CLISwitchPosition::IS_TRACE_ENABLE);
        bool wasEnabled = GlobalSetting::UBS_SPLIT_TRACE_ENABLED;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = enable;
        if (enable && !wasEnabled) {
            if (GlobalTracePool::Instance().LazyInit()) {
                SplitTraceDrainThread::Instance().Start();
            } else {
                GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
            }
        } else if (!enable && wasEnabled) {
            SplitTraceDrainThread::Instance().Stop();
        }
    }
    // send response ...
}
```

> **向后兼容**: `--enable`/`--disable` 保持通过 `mSwitch` 位标记传递的旧路径 (`mType==INVALID` 时进入 else 分支)。新选项 `--sample-rate`/`--drain-interval` 通过 `mType`+`mValue` 传递, 不影响旧的 enable/disable 语义。

**CLI 端 `ProcessSplitTrace` 发送逻辑** (`cli_client.cpp`):

```cpp
if (args.sampleRate > 0) {
    header.mType = CLITypeParam::SPLIT_TRACE_OP_SET_SAMPLE_RATE;
    header.mValue = static_cast<double>(args.sampleRate);
} else if (args.drainInterval > 0) {
    header.mType = CLITypeParam::SPLIT_TRACE_OP_SET_DRAIN_INTERVAL;
    header.mValue = static_cast<double>(args.drainInterval);
} else {
    bool enable = (args.enable == "true" || ...);
    header.SetSwitch(CLISwitchPosition::IS_TRACE_ENABLE, enable);
}
```

**`CLITypeParam` 枚举新增值** (`cli_message.h`):

```cpp
enum class CLITypeParam : uint8_t {
    // ... 既有值 ...
    SPLIT_TRACE_OP_SET_SAMPLE_RATE,     // --sample-rate
    SPLIT_TRACE_OP_SET_DRAIN_INTERVAL,  // --drain-interval
};
```

**`ParsedArgs` 新增字段** (`cli_args_parser.h`):

```cpp
struct ParsedArgs {
    // ... 既有字段 ...
    uint32_t sampleRate = 0;    // --sample-rate N, 0 表示未指定
    uint32_t drainInterval = 0; // --drain-interval ms, 0 表示未指定
};
```

**CLI 长选项** (`cli_args_parser.cpp`):

```cpp
{"sample-rate", required_argument, nullptr, 'r'},
{"drain-interval", required_argument, nullptr, 'i'},
```

短选项 `r` / `i` 加入 `getopt_long` 短串 `"hwp:s:d:t:v:r:i:"`, Parse 中做范围校验 (1~1000 / 1~10000), 超范围打印错误并返回 false。

---

## 3. 热路径 API 与宏

### 3.1 热路径 API

```cpp
// 全局开关 (运行时)
static bool UBS_SPLIT_TRACE_ENABLED = false;

// 业务线程: IO 入口的采样决策 (4 路通用, 传入 path)
bool SplitTraceTrySample(Socket *sock, int fd, uint32_t seqNo, uint8_t path);

// 业务线程: 采样中的子阶段记录 (写入 slot)
void SplitTraceAdd(Socket *sock, uint8_t path, uint16_t idx,
                    uint32_t seqNo, uint64_t startTs, uint64_t endTs);

// 业务线程: IO 返回时结束采样
void SplitTraceEndSample(Socket *sock, uint8_t path, uint32_t seqNo,
                          uint32_t dataSize, uint32_t offset);

// poller/runner 线程: 异步完成, 按 seq_no + path 查找 slot 并写入子阶段
void SplitTraceAddSampled(int fd, uint8_t path, uint16_t idx,
                           uint32_t seqNo,
                           uint64_t startTs, uint64_t endTs);
```

### 3.2 宏定义

```cpp
#define STRACE_TRY_SAMPLE(sock, fd, seq_no, path) \
    ::ock::ubs::SplitTraceTrySample(sock, fd, seq_no, path)

#define STRACE_ADD(sock, path, idx, seq_no, start_ts, end_ts) \
    ::ock::ubs::SplitTraceAdd(sock, path, idx, seq_no, start_ts, end_ts)

#define STRACE_END(sock, path, seq_no, data_size, offset) \
    ::ock::ubs::SplitTraceEndSample(sock, path, seq_no, data_size, offset)

#define STRACE_SAMPLED(fd, path, idx, seq_no, start_ts, end_ts) \
    ::ock::ubs::SplitTraceAddSampled(fd, path, idx, seq_no, start_ts, end_ts)
```

### 3.3 调用示例

#### WriteV (方案A 延迟写入)

```cpp
ssize_t DataTx::WriteV(const SocketPtr &sock, const struct iovec *iov, int iovcnt) {
    int fd = sock->raw_socket_;
    auto *sock_ptr = sock.Get();

    // 方案A: 不在入口调用 TryStartSample, 仅记录时间戳
    auto ts_entry_start = ubsocket_get_timeNs_compile();
    // ... 状态检查 ...
    auto ts_entry_end = ubsocket_get_timeNs_compile();

    // BuildIovConverter
    auto ts_iov_start = ubsocket_get_timeNs_compile();
    auto converter = tx_ops_->BuildIovConverter(iov, iovcnt);
    auto ts_iov_end = ubsocket_get_timeNs_compile();

    // AllocTxBuf
    auto ts_alloc_start = ubsocket_get_timeNs_compile();
    uintptr_t txBuf = tx_ops_->AllocTxBuf(0, buf_cnt);
    auto ts_alloc_end = ubsocket_get_timeNs_compile();

    // PostSend: 内部分配 seq_no (FetchAddSeqNum + imm.user_data = seq_no)
    auto ts_post_start = ubsocket_get_timeNs_compile();
    int64_t ret = tx_ops_->PostSend(sock, txBuf, batch, converter);
    auto ts_post_end = ubsocket_get_timeNs_compile();

    // PostSend 返回后: 获取 seq_no, 一次性采样 + 批量写入
    auto *umq_socket = static_cast<UmqSocket *>(sock_ptr);
    uint32_t seq_no = umq_socket->LoadSeqNum();
    bool do_trace = STRACE_TRY_SAMPLE(sock_ptr, fd, seq_no, PATH_TX_WRITEV);
    if (do_trace) {
        STRACE_ADD(sock_ptr, PATH_TX_WRITEV, TX_WV_ENTRY, seq_no, ts_entry_start, ts_entry_end);
        STRACE_ADD(sock_ptr, PATH_TX_WRITEV, TX_WV_BUILD_IOV, seq_no, ts_iov_start, ts_iov_end);
        STRACE_ADD(sock_ptr, PATH_TX_WRITEV, TX_WV_ALLOC_BUF, seq_no, ts_alloc_start, ts_alloc_end);
        STRACE_ADD(sock_ptr, PATH_TX_WRITEV, TX_WV_MEM_COPY, seq_no, ts_post_start, ts_post_end);
        STRACE_ADD(sock_ptr, PATH_TX_WRITEV, TX_WV_UMQ_POST, seq_no, ts_post_start, ts_post_end);
        STRACE_END(sock_ptr, PATH_TX_WRITEV, seq_no, tx_total_len, 0);
    }
    return ret;
}
```

**关键**: 各子阶段时间戳先记录在局部变量中，PostSend 返回后获取 seq_no 并一次性采样 + 批量写入。`STRACE_END` 将 slot state 设为 DONE。通过 `(seq_no, fd, path)` 三元组在全局池中关联 slot。

> **seq_no 延迟写入**: 本设计采用方案A — seq_no 仍在 PostSend 内部分配，WriteV 入口不调用 SplitTraceTrySample，各子阶段时间戳先记录在局部变量中，PostSend 返回后获取 seq_no 并一次性采样 + 批量写入。

#### ReadV (RX 侧 seq_no 获取)

ReadV 路径不调用 PostSend, seq_no 来自接收到的 UMQ CQE。`DataRxOps::PollRx()` 内部消费 CQE 时, 从 `buf_pro->imm.user_data` 读取 seq_no 并存入 `last_rx_seq_no_` 字段。ReadV 在 PollRx 返回后读取该字段作为采样依据:

```cpp
ssize_t DataRx::ReadV(const SocketPtr &sock, const struct iovec *iov, int iovcnt) {
    auto ts_entry_start = ubsocket_get_timeNs_compile();
    // ... 状态检查 ...
    auto ts_entry_end = ubsocket_get_timeNs_compile();

    auto ts_pollrx_start = ubsocket_get_timeNs_compile();
    int ret = rx_ops_->PollRx(sock);           // 内部记录 last_rx_seq_no_
    auto ts_pollrx_end = ubsocket_get_timeNs_compile();

    auto ts_dataset_start = ubsocket_get_timeNs_compile();
    ret = rx_ops_->RxDataSet(iov[0].iov_base, max_buf_size);
    auto ts_dataset_end = ubsocket_get_timeNs_compile();

    auto ts_exit = ubsocket_get_timeNs_compile();

    // seq_no 从 PollRx 获取 (与 TX 侧同一 seq_no, 跨节点关联)
    uint32_t seq_no = rx_ops_->last_rx_seq_no_;
    bool do_trace = STRACE_TRY_SAMPLE(sock.Get(), sock->raw_socket_, seq_no, PATH_RX_READV);
    if (do_trace) {
        STRACE_ADD(sock.Get(), PATH_RX_READV, RX_RV_ENTRY, seq_no, ts_entry_start, ts_entry_end);
        STRACE_ADD(sock.Get(), PATH_RX_READV, RX_RV_POLL_RX, seq_no, ts_pollrx_start, ts_pollrx_end);
        STRACE_ADD(sock.Get(), PATH_RX_READV, RX_RV_DATA_SET, seq_no, ts_dataset_start, ts_dataset_end);
        STRACE_ADD(sock.Get(), PATH_RX_READV, RX_RV_REARM, seq_no, ts_dataset_end, ts_exit);
        STRACE_END(sock.Get(), PATH_RX_READV, seq_no, rx_total_len, 0);
    }
    return rx_total_len;
}
```

> **`last_rx_seq_no_` 局限**: PollRx 可能一次消费多个 CQE, `last_rx_seq_no_` 只记录最后一个有效 seq_no。这对采样判定足够 — 只要 TX 侧同一个 seq_no 也被采样到, 两端就能关联。

#### ubs_poll (RX 侧 seq_no 获取)

ubs_poll 路径从第一个交付的 data segment 中提取 seq_no:

```cpp
int ubs_poll(int fd, ubs_data_list_t *out) {
    auto ts_entry_start = ubsocket_get_timeNs_compile();
    // ... 初始化 ...

    uint16_t filled = 0;
    uint32_t first_seq_no = 0;
    while (filled < capacity) {
        umq_buf_t *qbuf = nullptr;
        umq_sock->GetAndPopQbuf(&qbuf, 1);
        // ... error/status/big_ctrl 检查 ...
        if (filled == 0 && qbuf->qbuf_ext != nullptr) {
            first_seq_no = reinterpret_cast<umq_buf_pro_t *>(qbuf->qbuf_ext)->imm.user_data;
        }
        ++filled;
    }

    auto ts_exit = ubsocket_get_timeNs_compile();

    bool do_trace = STRACE_TRY_SAMPLE(holder.Get(), fd, first_seq_no, PATH_RX_POLL);
    if (do_trace) {
        STRACE_ADD(holder.Get(), PATH_RX_POLL, RX_POLL_ENTRY, first_seq_no, ts_entry_start, ts_entry_end);
        STRACE_ADD(holder.Get(), PATH_RX_POLL, RX_POLL_GET_AND_POP, first_seq_no, ts_entry_end, ts_exit);
        STRACE_ADD(holder.Get(), PATH_RX_POLL, RX_POLL_DELIVER_SEG, first_seq_no, ts_entry_end, ts_exit);
        STRACE_END(holder.Get(), PATH_RX_POLL, first_seq_no, filled, 0);
    }
    return 1;
}
```

#### TxCqePoller 线程

```cpp
int UmqTxHelper::PollUmqTxInternal(PollArgs &poll_args, ICallback &error_cb) {
    auto poll_start = ubsocket_get_timeNs_compile();
    int poll_num = UmqApi::umq_poll(poll_args.umq_handle, &poll_args.poll_option, buf, POLL_BATCH_MAX);
    auto poll_end = ubsocket_get_timeNs_compile();

    if (poll_num > 0 && sock != nullptr) {
        for (int i = 0; i < poll_num; i++) {
            umq_buf_pro_t *buf_pro = (umq_buf_pro_t *)buf[i]->qbuf_ext;
            uint32_t seq_no = buf_pro->imm.user_data;

            STRACE_SAMPLED(sock, PATH_TX_WRITEV, TX_WV_ASYNC_UMQ_POLL, seq_no, poll_start, poll_end);
        }
    }
    // ProcessTxCqe 内部:
    //   STRACE_SAMPLED(sock, PATH_TX_WRITEV, TX_WV_ASYNC_PROCESS_CQE, seq_no, ...);
    //   STRACE_SAMPLED(sock, PATH_TX_WRITEV, TX_WV_ASYNC_BUF_FREE, seq_no, ...);
    // NotifyWritable:
    //   STRACE_SAMPLED(sock, PATH_TX_WRITEV, TX_WV_ASYNC_NOTIFY, seq_no, ...);
}
```

#### Share-JFR epoll runner 线程

```cpp
// RxPollQuantum 中, 按 seq_no 查找 slot
for (int i = 0; i < poll_num; i++) {
    auto socket_ptr = ArraySet<Socket>::GetInstance().GetItem(socket_fd);
    if (socket_ptr.Get() == nullptr) continue;

    umq_buf_pro_t *buf_pro = (umq_buf_pro_t *)buf[i]->qbuf_ext;
    uint32_t seq_no = buf_pro->imm.user_data;

    STRACE_SAMPLED(socket_ptr.Get(), PATH_RX_READV, RX_RV_ASYNC_UMQ_POLL, seq_no, poll_start, poll_end);
    STRACE_SAMPLED(socket_ptr.Get(), PATH_RX_READV, RX_RV_ASYNC_ENQUEUE, seq_no, enqueue_start, enqueue_end);
    STRACE_SAMPLED(socket_ptr.Get(), PATH_RX_READV, RX_RV_ASYNC_NOTIFY, seq_no, notify_start, notify_end);
}
```

---

## 4. Drain 与输出

### 4.1 宽限期

drain 线程在消费 DONE slot 前，等待 2ms 宽限期，确保 poller/runner 线程的异步子阶段已写入：

```
业务线程:     IO 开始 ──→ IO 返回 (EndSlot, state→DONE)
                               │
                               │ ← 2ms 宽限期 →
                               │                   poller 线程: umq_poll → 写异步子阶段
                               │
drain 线程:                                        ↓
  t=io_end_ts+2ms ──→ 消费 slot, 输出完整时序, Reset
```

slot 池容量与采样率的关系见 §3.6 数据量估算。

### 4.2 Drain 线程

drain 线程 10ms 周期运行，消费过宽限期的 TraceSlot, 输出 per-IO 时序。

**timeline 限频**: 每 100 次 drain (≈1s) 打印 1 次 timeline (最多 6 个 slot 合并), 与采样率解耦。

### 4.3 Drain 实现

drain 线程的实现代码见 §2.7。

**TraceSlot 消费特点**:

| 维度 | TraceSlot |
|------|-------------------|
| **数据粒度** | per-IO per-phase 时序 |
| **消费后** | Reset (清零, 可复用) |
| **存储位置** | GlobalTracePool 全局单例 (`slots_`) |
| **并发模型** | 不同线程写不同索引 |
| **输出内容** | 每条采样的完整子阶段时间线 |
| **输出文件** | timeline 日志 |
| **导出频率** | 每 100 次 drain (≈1s) |

---

## 5. 实施计划

### Phase 1: 报文收发流程分析 + 打点枚举定义

| Step | 内容 | 说明 |
|------|------|------|
| 1.1 | 定义 4 路枚举: `TxWritevPhase`(10), `TxPostPhase`(10), `RxReadvPhase`(13), `RxPollPhase`(12) | `ubsocket_trace.h`, 每条路径独立, 含同步+异步段 |
| 1.2 | 定义 `TracePath` 枚举 (4 值) + `PhaseData<N>` 模板 | 同文件, 4 路标识 + 通用 phase 数组模板 |
| 1.3 | 定义 `TraceSlot` 结构体 | `ubsocket_trace.h`, 4 路 union (PhaseData<N>) + path 字段 |
| 1.4 | 实现 `TraceSlot::RecordPhase(path, idx, ...)` | `fetch_or` bitmap + release fence + 按 path 分发到对应 union |
| 1.5 | 新增 `GlobalSetting` 配置项 | `UBS_SPLIT_TRACE_SAMPLE_RATE=100`, slot 数量固定 `MAX_SLOTS=256` |

### Phase 2: TraceSlot 采样状态机

| Step | 内容 | 说明 |
|------|------|------|
| 2.1 | `GlobalTracePool` 单例 256 slots + `LazyInit`/`DestroyPool` | `ubsocket_trace.h` 全局池, 延迟分配, Socket 无需 trace 字段 |
| 2.2 | 实现 `GlobalTracePool::AllocSlot` / `EndSlot` / `FindSlot` / `LazyInit` / `DestroyPool` + `SplitTraceTrySample` / `SplitTraceEndSample` / `SplitTraceAddSampled` | CAS 状态机: IDLE→TRACING→DONE, seq_no % N 确定性采样, poller 按 (seq_no, fd, path) 查找 slot |
| 2.3 | 实现 `DrainAll()` | drain 线程批量消费 DONE slot + Reset, 一次 log 调用输出 |
| 2.4 | 替换 `TryCreateSplitTrace()` 调用点 | `umq_socket_connector.cpp`, `umq_socket_acceptor.cpp` |
| 2.5 | 实现 `SplitTraceDrainThread` | 10ms 周期遍历 `GlobalTracePool` 的 256 slots 读 DONE slot, path 区分 4 路输出 |

### Phase 3: 写入路径改造

| Step | 内容 | 说明 |
|------|------|------|
| 3.1 | 定义新宏 `STRACE_TRY_SAMPLE` / `STRACE_ADD(path,idx,...)` / `STRACE_END(path,...)` / `STRACE_SAMPLED(path,idx,seq_no,...)` | 4 路通用宏, 含 path 标识 |
| 3.2 | (合并到 3.1) | TX/RX 共用同一套宏, 调用时传入不同 path |
| 3.3 | WriteV 路径: 延迟写入 + `STRACE_TRY_SAMPLE` PostSend后 + 批量 `STRACE_ADD` + `STRACE_END` 出口 | `ubsocket_data_tx.cpp`, seq_no 从 PostSend 后 `LoadSeqNum()` 获取 |
| 3.4 | ReadV 路径: `STRACE_TRY_SAMPLE` PollRx后 + `STRACE_ADD` 子阶段 + `STRACE_END` 出口 | `ubsocket_data_rx.cpp` + `umq_data_rx_ops.cpp`, seq_no 从 `DataRxOps::last_rx_seq_no_` 获取 (PollRx 内部记录) |
| 3.5 | 改造 TxCqePoller 写入点 | `umq_tx_helper.cpp` — 用 `STRACE_SAMPLED` (`GlobalTracePool::FindSlot` 按 seq_no+fd 查找) |
| 3.6 | 改造 epoll runner 写入点 | `umq_share_jfr_epoll_runner_ops.cpp` — 用 `STRACE_SAMPLED` |

### Phase 4: Drain 与输出

| Step | 内容 | 说明 |
|------|------|------|
| 4.1 | `ubsocket_init()` 中 `GlobalTracePool` 构造空壳 + `LazyInit()` 受开关控制 | `GlobalTracePool` 单例构造不分配内存; `UBS_SPLIT_TRACE_ENABLED=true` 时 `LazyInit()` 堆分配 + drain 线程启动 |
| 4.2 | 实现 drain 宽限期机制 | `io_end_ts + GRACE_PERIOD_NS` 检查, 确保异步子阶段已写入 |
| 4.3 | 实现 `DrainAll`: 消费 TraceSlot | 遍历 `GlobalTracePool` 的 256 slots, 消费过期 DONE slot, Reset 复用 |
| 4.4 | 实现 per-IO 时序输出格式 | 按 4 路 TracePath 枚举顺序, 标注线程来源和时间戳 |
| 4.5 | `ubsocket_uninit()` 中 `SplitTraceDrainThread::Stop()` + `GlobalTracePool::DestroyPool()` | Stop drain 线程后释放 `slots_` / `lookup_table_` 内存 |

### Phase 5: CLI 动态参数调整

| Step | 内容 | 说明 |
|------|------|------|
| 5.1 | `CLITypeParam` 枚举新增 `SPLIT_TRACE_OP_SET_SAMPLE_RATE` / `SPLIT_TRACE_OP_SET_DRAIN_INTERVAL` | `cli_message.h`, 复用 delay 命令的 `mType`+`mValue` 模式 |
| 5.2 | `CLIArgsParser` 新增 `--sample-rate` / `--drain-interval` 长选项 | `cli_args_parser.cpp` / `.h`, `ParsedArgs` 新增 `sampleRate` / `drainInterval` 字段; `PrintUsage` 新增 strace 帮助文本 |
| 5.3 | `CLIClient::ProcessSplitTrace` 扩展: 按 CLI 选项设置 `mType`+`mValue` | `cli_client.cpp`, `--enable`→`PROF_OP_ENABLE`, `--disable`→`PROF_OP_DISABLE`, `--sample-rate N`→`PROF_OP_SET_SAMPLE_RATE`+`mValue=N`, `--drain-interval ms`→`PROF_OP_SET_DRAIN_INTERVAL`+`mValue=ms` |
| 5.4 | `ProcessSplitTraceRequest` 扩展: 按 `mType` 分发, 写 `GlobalSetting` 静态变量 | `statistics.h`, enable/disable 保持 `mSwitch` 向后兼容; sample-rate/drain-interval 通过 `mType`+`mValue` 传递, 范围校验 1~1000 / 1~10000 |
| 5.5 | UT: CLI 参数调整验证 | `ProcessSplitTraceRequest` 收到 `SET_SAMPLE_RATE` 后 `GlobalSetting::UBS_SPLIT_TRACE_SAMPLE_RATE` 更新; 超范围值忽略 |

### Phase 6: 验证

| Step | 内容 | 说明 |
|------|------|------|
| 6.1 | UT: 采样概率验证 | seq_no=1~10000, rate=100, 验证 seq_no%100==0 的 100 次被采样 |
| 6.2 | UT: 确定性采样跨节点一致性 | TX seq_no=1000 和 RX seq_no=1000 均被采样 (两端 seq_no%100==0) |
| 6.3 | UT: 容量上限验证 | 256 个全局 slot 满后停止采样, drain 后恢复 |
| 6.4 | UT: 宽限期验证 | EndSlot 后立即 drain 不消费, 2ms 后才消费 |
| 6.5 | UT: poller 异步写入验证 | EndSlot 后 poller 仍能找到 slot 并写入子阶段 |
| 6.6 | UT: 多线程写入 + drain | 跨线程关联验证 (业务线程 + poller/runner) |
| 6.7 | 功能: WriteV 完整时序 | 业务线程 + poller 线程条目合并 |
| 6.8 | 功能: ReadV 完整时序 | epoll runner + 业务线程条目合并 |
| 6.9 | 性能: 非采样 IO 开销 (99%) | `SplitTraceTrySample` 短路 < 2ns (取模运算 + 比较) |
| 6.10 | 性能: 采样 IO 热路径开销 (1%) | `STRACE_ADD` 不超过 20ns |
| 6.11 | 性能: 1/100 采样率分摊开销 | 平均每 IO ~5ns, 对微秒级热路径影响 < 0.5% |
| 6.12 | 内存: 8192 sockets 峰值 | 功能关闭 0; 功能打开 ~385KB (129KB pool + 256KB drain 缓冲区), 不随连接数增长 |

---

## 6. 性能分析

### 6.1 功能关闭时开销

`UBS_SPLIT_TRACE_ENABLED=false` (默认) 时, drain 线程不启动, 热路径打点短路, `GlobalTracePool` 不分配内存 (零开销):

**热路径开销** (每次 IO):

| 操作 | 调用次数/IO | 单次开销 | 总开销/IO |
|------|------------|---------|----------|
| `SplitTraceTrySample` | 1 | ~0.3ns (bool 短路) | ~0.3ns |
| `ubsocket_get_timeNs_compile()` | 0 (不记录时间戳) | — | 0 |
| `SplitTraceAdd` | 0 (不调用) | — | 0 |
| `SplitTraceEndSample` | 0 (不调用) | — | 0 |
| `SplitTraceAddSampled` (poller) | 0 (不调用) | — | 0 |

**后台开销** (功能关闭时):

| 组件 | 开销 | 说明 |
|------|------|------|
| `GlobalTracePool` 内存 | 功能关闭: 0; 功能打开: ~129KB (LazyInit) + 256KB (drain 缓冲区) | `LazyInit()` 堆分配, `DestroyPool()` 在 `ubsocket_uninit()` 释放; `--disable` 时不释放 (避免 UAF) |
| `SplitTraceDrainThread` | **0** (不启动) | 功能关闭时 drain 线程不创建, 零 CPU |

> **功能关闭时零开销**: 热路径仅 1 次 bool 短路 (~0.3ns), drain 线程不启动 (零 CPU), `GlobalTracePool` 不分配 (零内存)。功能打开时 `LazyInit()` 分配 ~129KB + drain 线程 256KB `thread_local` 批量打印缓冲区。

### 6.2 功能打开时开销 — 按 IO 路径分析

`UBS_SPLIT_TRACE_ENABLED=true`, 采样率 1/N。每个 IO 路径的同步段 phase 数不同, poller 异步段 phase 数也不同, 因此开销按路径分别计算:

| IO 路径 | 同步 phase 数 | 异步 phase 数 | `get_timeNs` 调用数 | 未命中采样开销 | 命中采样开销 |
|---------|-------------|-------------|-------------------|--------------|------------|
| `TX_WRITEV` (writev) | 6 (ENTRY~EXIT) | 4 (ASYNC_*) | ~4 | ~5ns | ~5ns + 6×FindSlot + 6×RecordPhase + 1×EndSlot |
| `TX_POST` (ubs_post) | 6 (ENTRY~EXIT) | 4 (ASYNC_*) | ~3 | ~5ns | ~5ns + 6×FindSlot + 6×RecordPhase + 1×EndSlot |
| `RX_READV` (readv) | 6 (ENTRY~EXIT) | 7 (ASYNC_*) | ~5 | ~5ns | ~5ns + 6×FindSlot + 6×RecordPhase + 1×EndSlot |
| `RX_POLL` (ubs_poll) | 5 (ENTRY~EXIT) | 7 (ASYNC_*) | ~4 | ~5ns | ~5ns + 5×FindSlot + 5×RecordPhase + 1×EndSlot |

> `get_timeNs` 调用数: 各子阶段边界处调用 `ubsocket_get_timeNs_compile()`, 即使未命中采样也会执行。aarch64 上 ~2ns, x86 上 ~10ns。

**未命中采样的 IO** (概率 (N-1)/N):

| 操作 | 开销 | 说明 |
|------|------|------|
| `SplitTraceTrySample` | ~1ns | `seq_no % N != 0` → 短路 (取模 + 比较, 无 atomic) |
| `ubsocket_get_timeNs_compile()` × m | m × 2~10ns | 记录各阶段时间戳到局部变量 (即使不采样也执行) |
| `STRACE_ADD` / `STRACE_END` | 0 | `do_trace=false`, if 分支不进入 |
| `STRACE_SAMPLED` (poller) | ~1ns | `FindSlot` → lookup table load → seq_no 不匹配 → 短路 |

各路径未命中开销 (aarch64, `get_timeNs` ~2ns):

| 路径 | `get_timeNs` 数 | 时间戳开销 | 采样决策开销 | 总开销 |
|------|----------------|-----------|------------|--------|
| `TX_WRITEV` | 4 | 8ns | 2ns | **~10ns** |
| `TX_POST` | 3 | 6ns | 2ns | **~8ns** |
| `RX_READV` | 5 | 10ns | 2ns | **~12ns** |
| `RX_POLL` | 4 | 8ns | 2ns | **~10ns** |

**命中采样的 IO** (概率 1/N):

命中时额外开销 = `AllocSlot` (~3ns) + K×`FindSlot` (~1ns each) + K×`RecordPhase` (~3ns each) + `EndSlot` (~3ns)。其中 K = 同步 phase 数。

| 路径 | K (同步 phase) | AllocSlot | K×FindSlot | K×RecordPhase | EndSlot | 命中额外开销 |
|------|---------------|-----------|-----------|--------------|---------|-----------|
| `TX_WRITEV` | 6 | 3ns | 6ns | 18ns | 3ns | **~30ns** |
| `TX_POST` | 6 | 3ns | 6ns | 18ns | 3ns | **~30ns** |
| `RX_READV` | 6 | 3ns | 6ns | 18ns | 3ns | **~30ns** |
| `RX_POLL` | 5 | 3ns | 5ns | 15ns | 3ns | **~26ns** |

**分摊开销** (per-IO 平均, 采样率 1/N):

平均开销 = 未命中开销 + (1/N) × 命中额外开销

| 路径 | 未命中 | 1/1000 分摊 | 1/100 分摊 | 1/10 分摊 |
|------|--------|-----------|-----------|----------|
| `TX_WRITEV` | 10ns | 10.03ns | 10.3ns | 13ns |
| `TX_POST` | 8ns | 8.03ns | 8.3ns | 11ns |
| `RX_READV` | 12ns | 12.03ns | 12.3ns | 15ns |
| `RX_POLL` | 10ns | 10.03ns | 10.3ns | 12.6ns |

### 6.3 高并发场景分析

40000 连接 × 1000 QPS = 40M QPS 下的 slot 池与查找表压力:

**1/1000 采样率** (生产环境推荐):

| 指标 | 值 | 说明 |
|------|-----|------|
| 全局每 10ms 采样次数 | 400 | 40M / 1000 / 100 |
| 平均同时占用 slot | 80 | 400 × 2ms/10ms |
| slot 池利用率 | 31% | 80/256 |
| 碰撞概率 | ~1% | 80/8192 |
| FindSlot 快速路径命中 | 99% | O(1), 1 次 atomic load ≈ 1ns |
| FindSlot 慢速路径 (碰撞回退) | 1% | O(256), 256 次 atomic load ≈ 256ns |
| 平均 FindSlot 开销 | ~3.6ns | 0.99×1ns + 0.01×256ns |
| 40M QPS 下 FindSlot CPU 开销 | ~0.09 核 | 40M × 2.3ns / 1e9 |
| AllocSlot CAS 冲突概率 | ~31% | 80/256 slot 被占用 |
| AllocSlot 平均扫描长度 | ~1.45 | 1/(1-0.31) |

**1/100 采样率** (长尾分析):

| 指标 | 值 | 说明 |
|------|-----|------|
| 全局每 10ms 采样次数 | 4000 | 40M / 100 / 100 |
| 平均同时占用 slot | 800 | 4000 × 2ms/10ms |
| slot 池状态 | **饱和** | 800 > 256, 大量采样被丢弃 |
| 碰撞概率 | ~10% | 800/8192 |
| FindSlot 快速路径命中 | 90% | O(1) |
| FindSlot 慢速路径 (碰撞回退) | 10% | O(256) |
| 平均 FindSlot 开销 | ~26.5ns | 0.90×1ns + 0.10×256ns |
| 40M QPS 下 FindSlot CPU 开销 | ~0.55 核 | 40M × 13.7ns / 1e9 |
| 建议 | 调大 MAX_SLOTS 或降低采样率 | — |

**1/10 采样率** (问题定位, 仅适用于低连接数):

| 指标 | 值 | 说明 |
|------|-----|------|
| 全局每 10ms 采样次数 | 40000 | 40M / 10 / 100 |
| 平均同时占用 slot | 8000 | 40000 × 2ms/10ms |
| slot 池状态 | **严重饱和** | 8000 >> 256 |
| 碰撞概率 | ~98% | 8000/8192, 表项不足 |
| FindSlot 快速路径命中 | 2% | O(1) |
| FindSlot 慢速路径 (碰撞回退) | 98% | O(256) |
| 平均 FindSlot 开销 | ~250ns | 0.02×1ns + 0.98×256ns |
| 40M QPS 下 FindSlot CPU 开销 | ~5.0 核 | 40M × 125ns / 1e9 |
| 建议 | 调大 MAX_SLOTS + LOOKUP_SIZE, 或降低连接数 | — |

> **结论**: 1/1000 采样率下 slot 池利用率 31%, FindSlot 平均 3.6ns, 适合生产环境。1/100 采样率下 FindSlot 平均 26.5ns 仍可接受。1/10 采样率下 slot 池严重饱和, 需调大 `MAX_SLOTS` (如 512) 和 `LOOKUP_SIZE` (如 16384)。`LOOKUP_SIZE=8192` 为编译期常量, 可调整: 8192→64KB, 16384→128KB, 32768→256KB。

### 6.4 实测性能对比

aarch64 Kunpeng 920, 本机回环单连接, 1000 QPS, 10s 持续测试, 3 轮取平均:

| 配置 | Avg (us) | p50 (us) | p90 (us) | p99 (us) | p99.9 (us) | QPS | CPU (srv/cli) | Errors |
|------|---------|---------|---------|---------|-----------|-----|--------------|--------|
| 功能关闭 | 85 | 84 | 100 | 122 | 169 | 999 | 13%/18% | 0 |
| 功能打开 1/1000 采样 | 89 | 87 | 104 | 127 | 166 | 1000 | 12%/17% | 0 |
| 功能打开 1/100 采样 | 91 | 89 | 106 | 131 | 169 | 999 | 12%/18% | 0 |
| 功能打开 1/1 采样 | 100 | 99 | 115 | 142 | 177 | 999 | 14%/19% | 0 |

- **Avg 延迟**: 1/1000 采样增加 4us (~5%), 1/100 增加 6us (~7%), 1/1 全量采样增加 15us (~18%), 开销不随采样率线性增长
- **p50/p90**: 1/1000 与 1/100 差异 ≤ 6us, 1/1 增加 ~15us。开销来源: 采样命中时热路径额外执行 6 次 `ubsocket_get_timeNs_compile()` (~20ns/次) + AllocSlot CAS + RecordPhase 原子写 + EndSlot (~300ns/IO); 同时 drain 后台线程格式化 + 写日志与业务线程争抢 CPU、污染 cache
- **p99**: 功能关闭 122us → 1/1 采样 142us, 增加 20us (16%), 可接受
- **p99.9**: 各采样率持平 (~166-177us), 采样开销在长尾不可见
- **QPS / CPU**: 各采样率持平, 无额外 CPU 消耗
- **结论**: SplitTrace 功能开销稳定, 1/1000 采样率下 Avg 仅增加 4us (~5%), 对生产环境无影响

---

## 7. 内存分析

### 7.1 逐项拆解：新设计的 SplitTrace 内存开销

#### 全局 (GlobalTracePool)

| 字段 | 类型 | 大小 | 说明 |
|------|------|------|------|
| `GlobalTracePool::slots_[256]` | `TraceSlot*[256]` (堆分配) | 256 × 256 = **65KB** | 功能打开时 `LazyInit()` 分配, `ubsocket_uninit()` 时释放 |
| `GlobalTracePool::lookup_table_[8192]` | `atomic<uint64_t>*[8192]` (堆分配) | 8192 × 8 = **64KB** | 功能打开时 `LazyInit()` 分配, `ubsocket_uninit()` 时释放 |
| **GlobalTracePool 合计** | | **~129KB** | 功能打开时分配, 关闭时保留 (不释放) |
| `DrainAll::batch_buf` | `thread_local char[256KB]` | **256KB** | drain 线程批量打印缓冲区, 随线程分配/释放 |

> 功能关闭时 `GlobalTracePool` 不分配内存 (零开销)。功能打开时 `LazyInit()` 堆分配 ~129KB (256 TraceSlot + 8192 查找表), drain 线程额外分配 256KB `thread_local` 批量打印缓冲区。功能关闭时 (CLI `--disable`) **不释放** ~129KB 内存 — 避免业务线程 use-after-free 竞态。`ubsocket_uninit()` 时统一释放。Socket 无需存储任何 trace 相关字段。

### 7.2 全局内存总量

| 场景 | 内存 |
|------|------|
| 任意连接数 (功能关闭) | **0** (零内存, 不分配) |
| 任意连接数 (功能打开后关闭) | **~129KB** (分配后保留, 不释放) |
| 任意连接数 (功能打开) | **~385KB** (~129KB + 256KB drain 缓冲区) |

> 功能关闭时 `GlobalTracePool` 不分配内存 (零开销)。功能打开时 `LazyInit()` 分配 ~129KB (256 TraceSlot × 256B + 8192 查找表 × 8B), drain 线程额外分配 256KB `thread_local` 批量打印缓冲区。功能关闭时 (CLI `--disable`) 不释放 ~129KB — 避免业务线程 use-after-free 竞态。`ubsocket_uninit()` 时统一释放。不随连接数增长。

### 7.3 内存开销的构成比例

```
功能关闭: 0 (零内存, GlobalTracePool 不分配)

功能打开 ~385KB 构成:

TraceSlot 256 slots (堆)            65KB  ~17%  ████████
  ↑ 256 个 slot 数组, LazyInit 分配

O(1) 查找表 8192 entries (堆)       64KB  ~17%  ████████
  ↑ seq_no → slot_idx 直接映射表

DrainAll batch_buf (thread_local) 256KB  ~66%  ██████████████████████████
  ↑ 256 × 1024B, drain 线程批量打印缓冲区
```

如果未来需要进一步缩减内存，可选：

| 优化手段 | 效果 | 代价 |
|---------|------|------|
| `MAX_SLOTS` 从 256 降到 128 | 全局省 ~32KB | 采样饱和时缓冲减少 (1/1000 采样率下 128 仍够用) |
| `LOOKUP_SIZE` 从 8192 降到 4096 | 全局省 ~32KB | 碰撞率从 ~1% 升到 ~2% |
| `phase_start/end` 改用 `uint32_t` (纳秒→微秒, 4 字节) | 每 slot 省 ~100B | 精度从 ns 降到 μs |
| `phase_start/end` 改用偏移量 (相对 `io_start_ts`, `uint16_t` 微秒) | 每 slot 省 ~150B | 精度+范围限制 (最大 65ms) |
| 只对采样命中的 phase 分配 (稀疏存储) | 理论最优 | 失去定长数组的无竞争优势 |

默认功能关闭时零内存。功能打开时 ~385KB (~129KB pool + 256KB drain 缓冲区) 的开销可通过调小 `MAX_SLOTS` 或 `LOOKUP_SIZE` 编译宏来降低 (如 MAX_SLOTS=64 + LOOKUP_SIZE=4096 → ~49KB pool, 不随连接数增长)。

---

## 8. 注意事项

1. **seq_no 延迟写入的副作用**: seq_no 仍在 PostSend 内部分配，WriteV 中途返回 EAGAIN/error 时不会跳号。但如果 PostSend 成功而后续处理失败，drain 时该组只有业务线程的条目(无 poller/RX 条目)，可正常输出但标注为 "incomplete IO"。

2. **epoll runner 的 seq_no 来源**: RX 侧的 `buf_pro->imm.user_data` 是**发送端**写入的 seq_no。接收端 socket 的 TraceSlot 通过 `GlobalTracePool::FindSlot(seq_no, path, fd)` 关联。TraceSlot 的 `path` 字段区分 4 条路径 — 发送端和接收端各自独立采样，端到端时序通过 seq_no 关联。

3. **Share-JFR 场景**: Share-JFR 模式下多个 socket 共享一个 UMQ handle，poller 线程的 `umq_poll` 返回的 buf 可能属于不同 socket。每个 buf 的 `imm.user_data` 携带各自的 seq_no，poller 通过 `GlobalTracePool::FindSlot(seq_no, path, fd)` 在全局池中找到对应 socket 的 slot 并写入子阶段。`FindSlot` 匹配 seq_no + fd + direction，不会混淆。

4. **probe 包的 seq_no**: ProbeManager 的探针包也使用 `imm.user_data` 字段（值为 `PROBE_USER_DATA_ID`）。probe 包未经过 `SplitTraceTrySample` 采样，`GlobalTracePool::FindSlot(PROBE_USER_DATA_ID, path, fd)` 不会匹配任何 slot，poller 自然跳过，无需额外过滤。

5. **线程标识**: 全局 slot 池设计中无需分配 thread_id — 不同线程通过不同的 phase 索引天然区分，drain 时按枚举顺序输出即可识别线程来源 (同步段=业务线程, 异步段=poller/runner线程)。

6. **C++11 兼容**: 全局 slot 池设计中 drain 线程仅遍历全局 256 slots + 读 `TraceSlot` 定长数组，无 STL 容器依赖。`TraceSlot` 使用 `std::atomic` 和定长数组，C++11 完全支持。

7. **延迟分配, 线程受开关控制**: `GlobalTracePool` 单例在 `ubsocket_init()` 时仅构造空壳 (不分配 slots/lookup table)。`LazyInit()` 在功能首次打开时 (环境变量 `UBSOCKET_SPLIT_TRACE_ENABLE=true` 或 CLI `--enable`) 堆分配 ~129KB。`SplitTraceDrainThread` 受 `UBS_SPLIT_TRACE_ENABLED` 控制 — 功能打开时启动 (额外分配 256KB `thread_local` 批量打印缓冲区), 关闭时停止。CLI `--disable` 时不释放内存 — 避免业务线程 use-after-free 竞态 (flag 设 false 到实际停止访问 slots_ 之间存在窗口)。`ubsocket_uninit()` 时 `Stop()` + `DestroyPool()` 统一释放。功能关闭时 `SplitTraceTrySample` 在 bool 检查后立即返回 (~0.3ns), 不执行取模运算, 不记录时间戳。

8. **宽限期与 CQE 长尾**: 宽限期 2ms 覆盖 UMQ CQE 正常延迟 (<100μs) 的 20 倍余量。极端长尾 (>2ms) 的 CQE 其 poller 子阶段会丢失，但 trace 采样率 1/100，一次丢失不影响整体诊断。宽限期可通过环境变量调整。

9. **宽限期内 slot 不会被复用**: `GlobalTracePool::AllocSlot` 只找 `state==IDLE`(0) 的 slot。宽限期内 slot `state==DONE`(2)，不会被新采样选中。只有 drain 消费并 `Reset()` 后才变回 IDLE。因此宽限期不会导致数据覆盖。