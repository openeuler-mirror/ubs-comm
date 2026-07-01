---
name: add-tracepoint
description: Skill for adding SplitTrace/PROF instrumentation tracepoints to ubsocket hot paths (WriteV, ReadV, PollTx, PollRx, RxDataSet). Covers ProfilingTPId enum, SplitTraceInfo struct, AddWriteTrace/AddReadTrace overloads, UpdateWriteFirstTrace, trace/fd parameter propagation, poll_num field, queue_depth, KeepWrite batching. NOT for UT generation — use ut-gen* skills for that. Trigger on keywords: tracepoint, SplitTrace, tracer, profiling, AddWriteTrace, AddReadTrace, poll_num, ProfilingTPId, UpdateWriteFirstTrace, CORE_WRITE, CORE_READ, 打点, trace分析, queue_depth, KeepWrite, seq_no.
---

# Skill: add-tracepoint

> **权威参考**: `AGENTS.md` §关键陷阱(SplitTrace/Profiling tracepoint新增) — 8 个陷阱的完整描述。
> **架构参考**: `doc/ubsocket/UBSOCKET-ARCHITECTURE.ch.md` — profiling 模块位置。

## 模块范围

`src/ubsocket/csrc/profiling/` 下的 tracer 和 tracepoint 系统：

| 文件 | 职责 |
|------|------|
| `ubsocket_prof.h` | `ProfilingTPId` 枚举、`PROF_START/END` 宏、`ubsocket_get_timeNs()` |
| `trace/ubsocket_trace.h` | `SplitTrace` 类、`SplitTraceInfo` 结构体、`AddWriteTrace`/`AddReadTrace`/`UpdateWriteFirstTrace` |
| `trace/ubsocket_trace.cpp` | `DrainAndPrint()`、`Flush()` 实现 |

## Tracer 架构

```
两层打点系统:

1. PROF (ubsocket_prof)              2. SplitTrace (ubsocket_trace)
   ────────────────                     ──────────────
   PROF_START(type);                    auto *trace = sock->split_trace_;
   ... 业务代码 ...                      if (trace != nullptr) {
   PROF_END(type, good);                   trace->AddWriteTrace(type, fd, start, end);
   记录到环形buffer                        }
   overhead: ~300ns/call(clock_gettime)   overhead: ~150ns/call(clock_gettime)
   适用: 固定周期历史回溯                  适用: 实时双buffer零丢点
```

### SplitTrace 关键方法

| 方法 | 用途 | 参数 |
|------|------|------|
| `AddWriteTrace(type, fd)` | 仅 start timestamp，从TraceRegistry取rpc_id | type, raw_socket |
| `AddWriteTrace(type, fd, seq, data, offset)` | 带业务数据 | type, fd, seq, data_size, offset |
| `AddWriteTrace(type, fd, start, end, poll_num=0)` | 精确 start/end，继承上一trace的rpc_id/seq | type, fd, start_ts, end_ts, poll_num |
| `AddReadTrace(...)` | Read侧，三个重载同上(rpc_id固定为0) | 同上 |
| `UpdateWriteFirstTrace(type, seq, data, offset)` | PostSend WR0时回填CORE_WRITE 及其前的trace | seq_no, data_size, offset |
| `UpdateWriteLastTrace(type, data, offset)` | 改写最后一条trace的type | data_size, offset |
| `UpdateWriteLastTraceEndTime(type)` | 补最后一条trace的end_timestamp | type |

### SplitTraceInfo 结构

```cpp
struct SplitTraceInfo {
    int raw_socket = -1;
    uint64_t rpc_id = -1;
    uint32_t seq_no = 0;
    uint32_t data_size = 0;
    uint32_t offset = 0;
    ProfilingTPId type = CORE_WRITE;
    uint32_t poll_num = 0;          // umq_poll返回的CQE数
    uint64_t start_timestamp = 0;
    uint64_t end_timestamp = 0;
};
```

## 新增 Tracepoint 工作流程

### Step 1: 添加枚举值

在 `ubsocket_prof.h` 的 `ProfilingTPId` 枚举中，**在 `UBSOCKET_PROF_COUNT` 之前**追加：

```cpp
enum ProfilingTPId : uint32_t {
    ...
    CORE_READ_RX_DATA_SET_REARM,  // 38 ← 新增，必须在 UBSOCKET_PROF_COUNT 前
    CORE_READ_RX_DATA_SET_RECV,   // 39

    UBSOCKET_PROF_COUNT,          // 永远在最后
};
```

### Step 2: 确定打点位置和 trace 可达性

```
Write 侧:
  DataTx::WriteV(sock)                     ← trace = sock->split_trace_
    → tx_ops_->PollTx(sock)                ← 需透传 trace
      → DoUmqTxPoll(sock, err, trace)
        → UmqTxHelper::PollUmqTx(trace, fd)
          → trace->AddWriteTrace(type, fd, start, end, poll_num)

Read 侧:
  DataRx::ReadV(sock)                      ← trace = sock->split_trace_
    → rx_ops_->PollRx(sock)                ← 通过 sock 获取 trace
    → rx_ops_->RxDataSet(buf, size, trace, fd)  ← 需扩展签名
      → trace->AddReadTrace(type, fd, start, end)
```

### Step 3: 透传 trace 和 raw_socket

如果打点位置无法访问 trace/fd，扩展函数签名：

```cpp
// 反面: 无法访问 trace
int Foo::Bar(void *buf) { ... }

// 正面: 扩展签名(需同时改头文件和所有调用点)
int Foo::Bar(void *buf, SplitTrace *trace = nullptr, int raw_socket = -1) {
    if (trace != nullptr) {
        trace->AddWriteTrace(NEW_TYPE, raw_socket, start, end);
    }
}
```

### Step 4: 选择正确的重载

| 场景 | 重载 | 示例 |
|------|------|------|
| 纯时间打点 | `AddWriteTrace(type, fd, start, end)` | `CORE_WRITE_UMQ_POLL` |
| 时间+计数 | `AddWriteTrace(type, fd, start, end, poll_num)` | umq_poll 的 CQE 数 |
| 业务数据打点 | `AddWriteTrace(type, fd, seq, data, offset)` | `CORE_READ_HANDLE_BUF` |

### Step 5: 验证 UpdateWriteFirstTrace

如果在 `CORE_WRITE` 和 PostSend 之间插入新 trace，必须检查级联回填：

```cpp
// 反面: 只看 core_write_pos+1
if (buf.data[core_write_pos + 1].type == CORE_WRITE_POLL_TX) { ... }

// 正面: forward-scan 所有 seq==0 的条目
for (uint32_t i = core_write_pos + 1; i < buf.count; ++i) {
    if (buf.data[i].seq_no != 0) break;
    buf.data[i].seq_no = seq_no;
    buf.data[i].data_size = data_size;
    buf.data[i].offset = offset;
}
```

### Step 6: 构建验证

```bash
UMQ_BUILD=on UBSOCKET_BUILD=on bash build/build_umq_and_ubsocket.sh
```

## 决策矩阵

| 问题 | 是 | 否 |
|------|-----|-----|
| 是后台线程(poller/epoll runner)? | **传 `trace=nullptr`** (陷阱7) | 继续 |
| 在 CORE_WRITE 之后、PostSend 之前? | 验证 UpdateWriteFirstTrace (陷阱3) | 直接打 |
| 子函数内打点? | **扩展签名传 trace/fd** (陷阱5) | 直接打 |
| 新增字段到 SplitTraceInfo? | 注意重载歧义 → bitfield 加 `static_cast` (陷阱6) | 继续 |
| 头文件声明与.cpp实现同步? | 每次参数变更后**对比签名** (陷阱4) | — |

## 常见陷阱速查

> 详细说明 → `AGENTS.md` §关键陷阱(SplitTrace/Profiling tracepoint新增)。

| # | 陷阱 | 场景 |
|---|------|------|
| 1 | 变量名冲突 | 新变量与已有变量同名 |
| 2 | Read/Write buffer 用错 | AddReadTrace 误写入 write_bufs_ |
| 3 | UpdateWriteFirstTrace 回填阻断 | CORE_WRITE 和新trace间插入多个tracepoint |
| 4 | 签名不一致 | 头文件加了参数、.cpp没同步 |
| 5 | 子函数无 trace | RxDataSet内无法访问 SplitTrace |
| 6 | 重载歧义 | bitfield 实参触发多个候选重载 |
| 7 | Poller trace 爆炸 | 后台线程以高频写 SplitTrace |
| 8 | 枚举值位置 | 新值必须加在 UBSOCKET_PROF_COUNT 前 |

## 如何使用本Skill

触发关键词见 YAML frontmatter。加载方式：

```
/skill add-tracepoint
```

或对话中出现 `tracepoint`/`打点`/`SplitTrace`/`AddWriteTrace`/`AddReadTrace` 等关键词时自动触发。

## 知识回流

| 发现类型 | 判断条件 | 回流目标 |
|----------|---------|----------|
| 新陷阱 | 与 SplitTrace/PROF 相关 | `AGENTS.md` §关键陷阱 |
| 新打点模式 | 跨模块适用 | 本skill §决策矩阵 |

## 上游调用约定（Trace 分析必读）

**brpc KeepWrite 批量写入**: 分析 SplitTrace 时，seq 乱序/跳跃不是多线程竞争，是 KeepWrite 聚合的正常表现。

```
bthread(单线程) → Channel::CallMethod → queue_depth 允许堆积 N 个 WriteRequest
    ↓
 DoWrite() 触发 → 收集 1~N 个请求 → ubsocket_writev(iov, cnt=1~N)
    ↓
 每个 WriteRequest → PostSend → FetchAddSeqNum(1) → 独立 seq_no → 赋予 1 个 buf
          (TX_SGE_MAX=1, 每个 WR 只有一个 SGE)
    ↓
 umq_post → N 个 buf, 各自携带不同 seq_no
    ↓
 对端 JFR poll → N 个 buf, 各自不同 seq → 采样记录部分 seq
```

**seq 号来源**: 每个 `umq_buf_t` 独立携带 `buf_pro->imm.user_data`（TX 侧 WriteRequest 逐迭代调用 `FetchAddSeqNum(1)` 分配，`TX_SGE_MAX=1` 保证每 WR 一个 SGE）。非"一批 buf 共享一个 seq"。

**seq 跳跃原因**: queue_depth>1 时，KeepWrite 一次堆积 N 个 WriteRequest，每个消耗一个 seq_no。下次 DoWrite 时 seq 已跳 N 个号。正常现象，非丢包。

| queue_depth | WriteV 批大小 | 对端 buf 数 | seq 变化 |
|:-----------:|:------------:|:----------:|:--------:|
| 1 | 1 req/次 | 3~5 | 连续 +1 |
| 10 | 1~10 req/次 | 20~60 | 跳跃多个号 |

不要误判为"多线程竞争导致 seq 跳跃"或"丢包"。

## 参考文件

| 文件 | 用途 |
|------|------|
| `src/ubsocket/csrc/profiling/ubsocket_prof.h` | 枚举、宏、get_timeNs |
| `src/ubsocket/csrc/profiling/trace/ubsocket_trace.h` | SplitTrace 类 |
| `src/ubsocket/csrc/profiling/trace/ubsocket_trace.cpp` | DrainAndPrint |
| `src/ubsocket/csrc/core/ubsocket_data_tx.cpp` | Write 侧打点调用 |
| `src/ubsocket/csrc/core/ubsocket_data_rx.cpp` | Read 侧打点调用 |
| `src/ubsocket/csrc/core/umq/umq_tx_helper.cpp` | PollUmqTxInternal 打点 |
| `src/ubsocket/csrc/core/umq/umq_data_tx_ops.cpp` | DoUmqTxPoll/PostSend 打点 |
| `AGENTS.md` §关键陷阱 | 8 个陷阱完整描述 |
| `doc/ubsocket/POLLTX_PERF_ANALYSIS.md` | Write 侧性能分析 |
