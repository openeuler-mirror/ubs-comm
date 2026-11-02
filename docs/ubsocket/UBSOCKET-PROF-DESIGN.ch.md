# UBSocket PROF 统计聚合层 设计文档

## 1. 问题域

### 1.1 解决的问题

UBSocket 作为高性能通信库，其数据路径（Write/Read/Epoll/UMQ poll）涉及多层异步协作。当出现性能问题（如 p99 长尾、延迟抖动、吞吐瓶颈）时，缺乏**低开销、可长期运行**的统计手段来量化每个子阶段的耗时分布。

传统日志方式存在以下缺陷：
- **开销不可控** — 每次记录需 I/O 格式化，us 级操作被 ns 级日志拖慢
- **无聚合能力** — 只能看单条事件，无法快速得出 count/avg/min/max/p99
- **不可在线查询** — 需停机分析日志文件

PROF 统计聚合层解决的问题：
1. **热路径耗时量化** — 对每个关键子阶段（BuildIov、PostSend、PollRx、Rearm 等）精确计时并聚合统计
2. **长尾定位** — ext 模式通过蓄水池采样提供 p50/p90/p95/p99/p999/p9999 分位数
3. **低开销持续运行** — thread_local 或 per-CPU 无锁写入，不影响生产行为
4. **在线查询与定时落盘** — 通过 CLI 工具实时查询，或后台线程周期性 dump 到文件

### 1.2 适用场景

| 场景 | 典型用法 |
|------|---------|
| 性能基线测试 | 开启 ext 模式，运行 2+ 分钟后读取 dump 文件分析 p99 |
| 线上问题诊断 | 通过 CLI `ubstat delay` 实时查询当前聚合统计 |
| 回归对比 | 开启 fast 模式，对比版本间 avg/max 变化 |
| 子阶段拆解 | 在长尾阶段内部新增 PROF tracepoint，逐步缩小瓶颈范围 |
| 竞品对比 | 统一打点口径，量化各路径开销占比 |

---

## 2. 功能描述

### 2.1 功能开关控制方式及参数

#### 2.1.1 开关层级

PROF 打点代码始终编译进二进制，无编译时开关。运行时通过环境变量和 CLI 命令控制：

```
环境变量  UBSOCKET_PROF_ENABLE (init 时读取)
    └─ 控制 ubsocket_prof_init() 是否执行
        └─ init 后置 ubsocket_prof_enabled=1，PROF_START/PROF_END 宏生效

CLI 命令  ubstat delay -t enable/disable (运行时动态)
    └─ enable: 调用 Profiling::Init() 初始化 PROF
    └─ disable: 调用 Profiling::Uninit() 关闭 PROF，停止 DumpThread
```

#### 2.1.2 环境变量参数

全部通过 `GlobalSetting::LoadEnv()` 在 `ubsocket_init()` 阶段读取：

| 环境变量 | 类型 | 默认值 | 取值范围 | 说明 |
|----------|------|--------|---------|------|
| `UBSOCKET_PROF_ENABLE` | bool | `false` | `true\|false` | 是否在 init 时初始化 PROF |
| `UBSOCKET_PROF_MODE` | string | `"fast"` | `fast\|ext` | 统计模式：基础统计 / 百分位统计；可通过 CLI `delay -t mode -v` 动态修改 |
| `UBSOCKET_PROF_DUMP_INTERVAL_MIN` | int | `1` | `1~5` | dump 周期（分钟），UT 模式下为 10ms；可通过 CLI `set_interval` 动态修改 |
| `UBSOCKET_PROF_DUMP_FILE_PATH` | string | `/tmp/ubsocket/profiling` | 路径字符串 | dump 文件目录；可通过 CLI `set_path` 动态修改 |

#### 2.1.3 CMake 编译参数

| CMake 选项 | 控制的宏 | 影响 |
|-----------|---------|------|
| `ENABLE_CPU_HARDWARE_ACCELERATION=ON` | `ENABLE_CPU_MONOTONIC` | aarch64 上使用 `cntvct_el0` 硬件计数器替代 `clock_gettime` |

> **注**: PROF 宏始终编译进二进制，由运行时 `ubsocket_prof_enabled` 控制是否生效。`ENABLE_CPU_HARDWARE_ACCELERATION` 仅影响时间戳获取方式（硬件计数器 vs `clock_gettime`），不影响打点代码的编译。

#### 2.1.4 运行时动态开关

通过 CLI 命令 `ubstat delay` 可在运行时动态控制：

| CLI 操作 | 对应代码路径 | 效果 |
|----------|-------------|------|
| `ubstat delay -t query` | `Profiling::Combine()` → `ubsocket_prof_combind()` | 返回当前聚合统计快照 |
| `ubstat delay -t enable` | `Profiling::Init()` → `ubsocket_prof_init()` | 运行时初始化 PROF，置 `ubsocket_prof_enabled=1` |
| `ubstat delay -t disable` | `Profiling::Uninit()` → `ubsocket_prof_uninit()` | 运行时关闭 PROF，置 `ubsocket_prof_enabled=0`，停止 DumpThread |
| `ubstat delay -t reset` | `Profiling::Reset()` → `ubsocket_prof_reset()` | 清零所有 tracepoint 统计数据 |
| `ubstat delay -t interval -v <N>` | `GlobalSetting::UBS_PROF_DUMP_INTERVAL_MIN = N` | 动态修改 dump 周期为 N 分钟（1~5，越界拒绝），DumpThread 下次循环从 GlobalSetting 读取，立即生效 |
| `ubstat delay -t path -v <path>` | `GlobalSetting::UBS_PROF_DUMP_PATH = path` | 动态修改 dump 目录，DumpThread 下次 dump 检测到路径变化后关闭当前文件、写入新路径，立即生效 |
| `ubstat delay -t mode -v <fast\|ext>` | `GlobalSetting::UBS_PROF_MODE = mode`（若已启用则 Uninit + 按新模式重新 Init） | 动态切换统计模式，详见下文模式切换语义 |

#### 2.1.5 模式切换语义

`PROF_OP_MODE` 的切换规则：

| 状态 | 切换动作 | 说明 |
|------|---------|------|
| PROF 未启用 | 仅更新 `GlobalSetting::UBS_PROF_MODE`，不触发 Init | 下次 `-t enable` 时按新模式初始化 |
| PROF 已启用 | `Profiling::Uninit()` + 按新模式 `Profiling::Init()` | 热切换：切换窗口内统计归零 |
| 目标模式 == 当前模式 | 直接返回（幂等） | 不做任何操作 |
| 模式值非法（非 fast/ext） | 返回错误码 retCode=-1 | 服务端校验 |
| 重新 Init 失败 | 置 `UBS_PROF_ENABLE=false`，返回错误码 | 兜底防止"标志位已改但 tracer 未运行"的状态不一致 |

**热切换安全性**：

- `Uninit()` 先置 `ubsocket_prof_enabled=0` → 切换窗口内 `PROF_START/PROF_END` 宏自然静默，业务线程不会访问已析构的 tracer
- `ubsocket_prof_mode_ext` 为 `std::atomic<int>`：CLI 线程写（切换时）、业务线程读（每次 `ubsocket_prof_record` 分发），release/acquire 序保证可见性
- 切换瞬间正在执行的少量打点会丢失，对聚合统计（AVG/P99）无影响
- dump 文件格式随模式变化：fast 输出 6 列、ext 输出 8 列（多 P99/P9999），分析工具按列数自动适配

**协议兼容性**：`PROF_OP_MODE` 枚举值追加在 `CLITypeParam` 末尾，已有枚举值不变，新旧版本 CLI/服务端可混用（旧版本收到未知类型走 default 分支返回错误）。

### 2.2 信息输出途径及信息格式

#### 2.2.1 输出途径

| 途径 | 触发方式 | 实现入口 |
|------|---------|---------|
| **CLI 实时查询** | `ubstat delay -t query` | `Statistics::DealDelayOperation()` → `Profiling::Combine()` → `ubsocket_prof_combind()` |
| **定时文件落盘** | 后台 `DumpThread` 周期触发 | `DumpThread::DumpData()` → `Tracer::Combine(oss)` → `WriteDumpData()` |
| **API 调用** | 用户代码 `Profiling::Combine(str)` | 返回 `std::string` 格式的统计文本 |

CLI 查询与文件落盘使用**相同的输出格式**（对齐列格式），CLI 仅缺少时间戳标题行。

#### 2.2.2 fast 模式输出格式

文件落盘格式（含时间戳标题行），CLI 查询格式相同但无标题行：

```
timeStamp: 2026-08-27 14:30:00
[TRACE_NAME]                          SUCCESS            FAILURE            TOTAL(ns)          AVG(ns)            MAX(ns)            MIN(ns)
[CORE_WRITE]                          130006             0                  247011400          1900               15300              800
[CORE_READ]                           130006             0                  16900800           130                2100               60
[CORE_WRITE_POST_SEND]                130006             0                  91004200           700                5200               200
```

字段顺序：`[name] success_count failure_count total_time_ns avg_ns max_ns min_ns`

#### 2.2.3 ext 模式输出格式

文件落盘格式（含时间戳标题行），CLI 查询格式相同但无标题行：

```
timeStamp: 2026-08-27 14:30:00
[TRACE_NAME]                          SUCCESS            FAILURE            TOTAL(ns)          AVG(ns)            MAX(ns)            MIN(ns)            P99(ns)            P9999(ns)
[UBS_NATIVE_FINALIZE_IO]              120586             0                  1004000338         8333               104160             4470               13578              19242
[UBS_NATIVE_UMQ_POST_SEND]            87956              0                  192760076          2191               27090              1450               3850               5554
```

字段顺序：`[name] success_count failure_count total_time_ns avg_ns max_ns min_ns p99_ns p9999_ns`

#### 2.2.4 dump 文件路径与绕接控制

**文件命名**：

```
当前写入文件:  {dump_path}/ubsocket_profiling_{pid}.log
绕接归档文件:  {dump_path}/ubsocket_profiling_{pid}.log.gz
```

- 默认路径: `/tmp/ubsocket/profiling/ubsocket_profiling_<pid>.log`
- 目录递归创建，权限 `0750`

**绕接规则**：

| 控制项 | 限制 | 说明 |
|--------|------|------|
| 单文件大小上限 | 10 MB (`DUMP_FILE_MAX_SIZE`) | 每次 `WriteDumpData` 写入后检查文件大小，超限触发绕接 |
| 归档文件保留数 | 3 个 (`DUMP_MAX_ARCHIVES`) | 超过 3 个 `.gz` 归档时，按 mtime 从最旧开始删除 |
| 压缩方式 | `gzip -f`（`fork`+`exec`） | 绕接后对 `.log` 文件原地压缩为 `.log.gz` |
| 压缩失败处理 | 删除原始 `.log`，不阻塞后续 dump | `CompressFile` 返回 false 时 `unlink` 原文件，`file_name_` 清空，下次 `WriteDumpData` 自动创建新文件 |

**绕接流程**：

```
WriteDumpData()
  → 写入 oss 数据到 dump_file_
  → flush
  → RotateDumpFile()
    → stat(file_name_) 检查大小
    → if size < 10MB: return (无需绕接)
    → close(dump_file_)
    → CompressFile(file_name_)     // gzip -f ubsocket_profiling_<pid>.log
    → if 压缩失败: unlink(file_name_)  // 清理原文件，不阻塞
    → 扫描目录中 ubsocket_profiling_<pid>.log.gz 文件列表
    → 按 mtime 升序排序
    → while (archives.size() > 3): unlink(最旧的 .gz)
    → file_name_.clear()           // 下次 WriteDumpData 开新文件
```

### 2.3 关键流程定义及关键性能点定义

#### 2.3.1 关键流程

**流程1: 初始化**

```
ubsocket_init()
  → GlobalSetting::LoadEnv()          // 读取环境变量
  → if (UBS_PROF_ENABLE)
      Profiling::Init(UBSOCKET_PROF_COUNT, dump_path, dump_interval)
        → ubsocket_prof_init(&option)
          → 按 UBS_PROF_MODE 选择 Tracer 或 TracerExt
          → 创建 TraceCombiner
          → if (enable_dump) 创建 DumpThread 并启动
          → ubsocket_prof_enabled = 1
```

**流程2: 热路径打点**

```
PROF_START(CORE_WRITE)
  → if (ubsocket_prof_enabled == 1)
      tpBeginCORE_WRITE = ubsocket_get_timeNs()    // 取时间戳
  → else
      (空操作，仅声明局部变量)

... 业务代码 ...

PROF_END(CORE_WRITE, size >= 0)
  → if (ubsocket_prof_enabled == 1)
      ubsocket_prof_record(CORE_WRITE, "CORE_WRITE", now - tpBegin, good)
        → fast: Tracer::Record() → tls_group->Record()
        → ext:  TracerExt::RecordExt() → agents_[cpuId]->RecordExt()
```

**流程3: 定时落盘**

```
DumpThread::DumpLoop()
  → sleep(interval_min)               // 可被 running_=false 中断
  → DumpData()
    → WriteDumpTitle(oss)             // 写时间戳 + 列头
    → Tracer::Combine(oss)            // 合并所有 thread_local TraceGroup
      → 遍历 trace_groups_[] 求和
      → OutputTraceGroup(oss)
    → WriteDumpData(oss)              // 写文件 + flush
```

**流程4: CLI 查询**

```
CLI client → ubstat delay -t query
  → Statistics::DealDelayOperation()
    → Profiling::Combine(out_str)
      → ubsocket_prof_combind(&out_buf)
        → Tracer::Combine(out_buf)    // 合并 + 格式化为 CLI 文本
      → out_str = string(out_buf, len)
      → free(out_buf)
```

**流程5: 反初始化**

```
ubsocket_uninit()
  → Profiling::Uninit()
    → ubsocket_prof_uninit()
      → ubsocket_prof_enabled = 0
      → DumpThread::DumpStop()        // 停止后台线程 + 最终 dump
      → Tracer/TracerExt 清理
```

**流程6: CLI 模式热切换**

```
CLI client → ubstat delay -t mode -v ext
  → Statistics::ProcessDelayRequest()        // 接收 mode payload 字符串
  → DealDelayOperation() [PROF_OP_MODE]
    → 校验 mode ∈ {fast, ext}，非法则 retCode=-1
    → 若 mode == 当前模式: 幂等返回
    → GlobalSetting::UBS_PROF_MODE = mode
    → if (UBS_PROF_ENABLE)                   // 已在运行 → 热切换
        Profiling::Uninit()                  // ubsocket_prof_enabled=0，打点静默
        Profiling::Init(...)                 // ubsocket_prof_init 重读 UBS_PROF_MODE，
                                             // 初始化新模式 tracer，enabled=1
        if Init 失败: UBS_PROF_ENABLE=false（兜底）
    // 未启用时仅更新 GlobalSetting，下次 enable 按新模式生效
```

#### 2.3.2 关键性能点定义

当前 `ProfilingTPId` 枚举共定义 **~120 个 tracepoint**，按路径分类：

| 路径 | 文件 | PROF 调用数 | 关键 tracepoint |
|------|------|------------|----------------|
| **Write 路径** | `ubsocket_data_tx.cpp` | 3 START + 8 END | `CORE_WRITE`, `CORE_WRITE_POST_SEND`, `CORE_WRITE_BUILD_IOV` |
| **Write UMQ 子阶段** | `umq_data_tx_ops.cpp` | 16 START + 16 END | `CORE_WRITE_POLL_TX`, `CORE_WRITE_UMQ_POST`, `CORE_WRITE_DO_TX_POLL` |
| **Read 路径** | `ubsocket_data_rx.cpp` | 3 START + 6 END | `CORE_READ`, `CORE_READ_EAGAIN`, `CORE_READ_POLL_RX` |
| **Read UMQ 子阶段** | `umq_data_rx_ops.cpp` | 13 START + 13 END | `CORE_READ_HANDLE_BUF`, `CORE_READ_REARM`, `UMQ_POLL_READ`, `UMQ_BUF_ALLOC` |
| **Connect 路径** | `umq_socket_connector.cpp` | 2 START + 2 END | `CORE_CONNECT`, `UMQ_CREATE` |
| **Accept 路径** | `umq_socket_acceptor.cpp` | 2 START + 2 END | `CORE_ACCEPT`, `UMQ_BIND` |
| **ub_native 发送** | `ubsocket_bigdata.cpp` | 38 START | `UBS_NATIVE_TRY_SENDER_POST`, `UBS_NATIVE_UMQ_POST_SEND` |
| **ub_native CQE** | `ubsocket_bigdata.cpp` | (同上) | `UBS_NATIVE_HANDLE_TX_COMPLETION`, `UBS_NATIVE_FINALIZE_IO` |
| **UMQ 函数级** | `umq_data_rx_ops.cpp` 等 | 多处 | `UMQ_BUF_ALLOC`, `UMQ_BUF_FREE`, `UMQ_REARM_INTERRUPT`, `UMQ_GET_CQ_EVENT` |

---

## 3. 功能实现

### 3.1 整体架构

```
┌──────────────────────────────────────────────────────────────────┐
│                    热路径 (WriteV/ReadV/PostSend/...)             │
│                                                                   │
│   PROF_START(TP_ID)  ──────────►  tpBegin##TP_ID = get_timeNs() │
│   ... 业务代码 ...                                                │
│   PROF_END(TP_ID, good) ────────► ubsocket_prof_record(...)      │
│                                                                   │
├──────────────────────────┬───────────────────────────────────────┤
│  fast 模式 (默认)         │  ext 模式                              │
│                          │                                       │
│  Tracer (单例)            │  TracerExt (单例)                      │
│  ├─ thread_local          │  ├─ agents_[1024] (per-CPU)            │
│  │  TraceGroup*           │  │  atomic<TraceGroupExt*>             │
│  ├─ vector<TraceGroupPtr> │  │                                     │
│  │  (所有线程的 group)     │  ├─ TraceCombinerExt                   │
│  ├─ TraceCombiner         │  └─ DumpThreadExt                      │
│  └─ DumpThread            │                                       │
│                          │                                       │
│  TraceGroup               │  TraceGroupExt                         │
│  ├─ vector<Tracepoint>    │  ├─ vector<TracepointExt>              │
│  │  (UBSOCKET_PROF_COUNT) │  │  (UBSOCKET_PROF_COUNT)              │
│  └─ Record()              │  └─ RecordExt()                        │
│                          │                                       │
│  Tracepoint (64B)         │  TracepointExt (~8.3KB)                │
│  ├─ success_count         │  ├─ success_count                      │
│  ├─ failure_count         │  ├─ failure_count                      │
│  ├─ total_time            │  ├─ total_time                         │
│  ├─ min_time / max_time   │  ├─ min_time / max_time                │
│  └─ pp90_time (未实现)    │  ├─ reservoir[1024] (蓄水池)            │
│                          │  └─ pp50~pp9999 (计算后缓存)            │
├──────────────────────────┴───────────────────────────────────────┤
│  DumpThread / DumpThreadExt                                      │
│  ├─ 周期: 1~5 分钟 (可配)                                         │
│  ├─ DumpData() → Combine() → WriteDumpData()                     │
│  └─ 文件: /tmp/ubsocket/profiling/ubsocket_profiling_<pid>.log   │
├──────────────────────────────────────────────────────────────────┤
│  CLI 查询                                                         │
│  ubstat delay -t query → Profiling::Combine() → CLI 返回文本       │
└──────────────────────────────────────────────────────────────────┘
```

### 3.2 打点宏实现 (`ubsocket_prof.h`)

#### 3.2.1 PROF_START

```cpp
#define PROF_START(TP_ID)                           \
    uint64_t tpBegin##TP_ID = 0;                    \
    do {                                            \
        if (ubsocket_prof_enabled == 1) {           \
            tpBegin##TP_ID = ubsocket_get_timeNs(); \
        }                                           \
    } while (0)
```

- 展开为**局部变量声明 + 条件取时间戳**
- `ubsocket_prof_enabled == 0` 时：仅声明一个 `uint64_t` 局部变量，分支不进入，无函数调用
- `ubsocket_prof_enabled == 1` 时：调用 `ubsocket_get_timeNs()` 取时间戳

#### 3.2.2 PROF_END

```cpp
#define PROF_END(TP_ID, GOOD)                                                                  \
    do {                                                                                       \
        if (ubsocket_prof_enabled == 1) {                                                      \
            ubsocket_prof_record(TP_ID, #TP_ID, ubsocket_get_timeNs() - tpBegin##TP_ID, GOOD); \
        }                                                                                      \
    } while (0)
```

- 计算耗时 = 当前时间 - `tpBegin##TP_ID`
- `#TP_ID` 将枚举名字符串化（如 `"CORE_WRITE"`），首次调用时设置到 Tracepoint 的 name 字段
- `GOOD` 控制计入 success_count 还是 failure_count

#### 3.2.3 时间戳获取

```cpp
// aarch64 + ENABLE_CPU_MONOTONIC (CMake 编译时确定)
static __always_inline uint64_t ubsocket_get_timeNs()
{
    uint64_t timeValue = 0;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(timeValue));
    return timeValue * 1000L / ubsocket_arm_cpu_freq;  // freq 在 init 时从 cntfrq_el0 读取
}

// 其他平台
static __always_inline uint64_t ubsocket_get_timeNs()
{
    struct timespec tpDelay = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &tpDelay);
    return tpDelay.tv_sec * 1000000000ULL + tpDelay.tv_nsec;
}
```

- `__always_inline` + `static` 确保内联，无函数调用开销
- aarch64: `mrs cntvct_el0` 读取 CPU 虚拟计数器，~20ns
- 其他平台: `clock_gettime(CLOCK_MONOTONIC)`，~200-300ns

### 3.3 fast 模式实现

#### 3.3.1 Tracer (`ubsocket_prof_tracer.h/cpp`)

单例 `Tracer::Instance()`，核心成员：

| 成员 | 类型 | 说明 |
|------|------|------|
| `tls_group` | `thread_local TraceGroup*` | 每线程惰性创建的 TraceGroup 指针 |
| `trace_groups_` | `vector<TraceGroupPtr>` | 所有线程的 TraceGroup 引用（Combine 时遍历） |
| `trace_combiner_` | `TraceCombinerPtr` | 合并器 |
| `dump_thread_` | `DumpThreadPtr` | 定时落盘线程 |

**Record 路径**（`Tracer::Record`, inline）：

```cpp
ALWAYS_INLINE int Tracer::Record(tp_id, tp_name, timestamp, good)
{
    uint32_t gen = generation_.load(acquire);
    if (UNLIKELY(tls_group == nullptr || tls_generation != gen)) {
        if (tls_group != nullptr) {
            tls_group->DecreaseRef();  // 释放上一代 stale TraceGroup
            tls_group = nullptr;
        }
        tls_generation = gen;
        CreateTraceGroup();    // 创建新的 TraceGroup 并加入 trace_groups_
    }
    return tls_group->Record(tp_id, tp_name, timestamp, good);
}
```

- `generation_` 是 `atomic<uint32_t>`，每次 `Init()` 时 `fetch_add(1)`
- `tls_generation` 是 TLS 变量，记录当前 `tls_group` 所属的代
- disable 后 re-enable 时，`generation_` 递增 → `tls_generation != gen` → 释放 stale 指针并重建
- `UNLIKELY` 标注首次/generation-mismatch 路径，分支预测不进入
- 创建 TraceGroup 时需加锁 `mutex_`（仅首次或 re-init），之后无锁

**Combine 路径**：

```cpp
int Tracer::Combine(TraceGroupPtr &out)
{
    // 1. 快照 trace_groups_ (加锁拷贝 vector)
    // 2. 遍历每个 tracepoint id，累加所有 TraceGroup 的数据
    for (tp_id = 0; tp_id < count; tp_id++) {
        for (each thread_group) {
            CombinerTracePoint(combined, thread->points_[tp_id]);
            // 累加 success/failure/total_time, 取 max/min
        }
    }
}
```

#### 3.3.2 Tracepoint (`ubsocket_prof_tracepoint.h`)

```cpp
struct Tracepoint {          // 64 bytes (刻意对齐一个 cache line 的一半)
    uint32_t id;
    uint32_t has_name;
    struct Data {
        uint64_t success_count;
        uint64_t failure_count;
        uint64_t total_time;
        uint64_t min_time;   // init = UINT64_MAX
        uint64_t max_time;
        uint64_t pp90_time;  // 预留，未实现
    } data;
    char *name;
};
```

**Record 操作**（inline）：

```cpp
inline void Tracepoint::Record(timestamp, good) {
    data.success_count += good;
    data.failure_count += !good;
    if (LIKELY(good)) {
        data.total_time += timestamp;
        data.max_time = std::max(data.max_time, timestamp);
        data.min_time = std::min(data.min_time, timestamp);
    }
}
```

- 仅普通变量操作（非原子），thread_local 隔离保证无竞争
- `LIKELY(good)` 优化成功路径分支预测

#### 3.3.3 TraceGroup (`ubsocket_prof_tracepoint_group.h`)

```cpp
class TraceGroup {
    const uint32_t max_tp_count_;
    vector<Tracepoint> points_;   // Init 时 resize(UBSOCKET_PROF_COUNT)
};
```

**Record 操作**（inline）：

```cpp
int TraceGroup::Record(tp_id, tp_name, timestamp, good) {
    if (UNLIKELY(tp_id >= max_tp_count_)) return UBS_ERROR;
    if (UNLIKELY(points_[tp_id].has_name == 0)) {
        points_[tp_id].SetName(tp_name);  // 首次调用设置 name
        points_[tp_id].has_name = 1;
    }
    points_[tp_id].Record(timestamp, good);
}
```

### 3.4 ext 模式实现

#### 3.4.1 TracerExt (`ubsocket_prof_tracer_ext.h/cpp`)

单例 `TracerExt::Instance()`，核心差异：

| 成员 | 类型 | 说明 |
|------|------|------|
| `agents_[1024]` | `atomic<TraceGroupExt*>[]` | 按 CPU ID 索引的 per-CPU 代理 |
| `trace_combiner_` | `TraceCombinerExtPtr` | 含蓄水池合并的合并器 |

**Record 路径**（inline）：

```cpp
ALWAYS_INLINE int TracerExt::RecordExt(tp_id, tp_name, timestamp, good)
{
    int cpuId = sched_getcpu();    // ~2ns, VDSO
    TraceGroupExt *agent = agents_[cpuId].load(acquire);  // 无锁读
    if (UNLIKELY(agent == nullptr)) {
        CreateAgentExt(cpuId);     // 首次：加锁创建
        agent = agents_[cpuId].load();
    }
    return agent->RecordExt(tp_id, tp_name, timestamp, good);
}
```

- `sched_getcpu()` 通过 VDSO 实现，无系统调用开销
- per-CPU 分桶避免跨核 cache line 争用

**Combine 路径**（蓄水池合并）：

```cpp
int TracerExt::CombineExt(TraceGroupExtPtr &out)
{
    for (tp_id = 0; tp_id < count; tp_id++) {
        // 1. 累加 success/failure/total_time, 取 max/min
        // 2. 收集所有 CPU 的 reservoir 样本到 allSamples
        // 3. 如果 allSamples.size() <= 1024: 直接复制
        //    否则: Fisher-Yates 随机采样保持 1024 个
        // 4. combinedTp.ComputePercentilesExt()  // 排序 + 线性插值
    }
}
```

#### 3.4.2 TracepointExt (`ubsocket_prof_tracepoint_ext.h`)

```cpp
struct TracepointExt {
    uint32_t id;
    uint32_t has_name;
    char *name;
    struct DataExt {
        uint64_t success_count, failure_count;
        uint64_t total_time, min_time, max_time;
        uint64_t reservoir[1024];      // 蓄水池样本
        size_t reservoir_count;
        uint64_t total_samples;
        uint64_t pp50_time, pp90_time, pp95_time;
        uint64_t pp99_time, pp999_time, pp9999_time;
    } data;
};
```

**蓄水池采样算法**（`RecordExt`, inline）：

```cpp
uint64_t total = data.total_samples++;
if (total < 1024) {
    data.reservoir[total] = timestamp;    // 未满直接填
    data.reservoir_count = total + 1;
} else {
    uint64_t idx = fast_rand_ext() % (total + 1);  // 线性同余
    if (idx < 1024) {
        data.reservoir[idx] = timestamp;  // 概率替换
    }
}
```

**百分位计算**（`ComputePercentilesExt`, 非热路径）：

```cpp
// 1. 复制 reservoir 到临时 vector
// 2. std::sort 排序
// 3. 线性插值计算 p50/p90/p95/p99/p999/p9999
```

- 仅在 Combine（dump 或 CLI 查询）时执行，不在热路径
- `fast_rand_ext()`: thread_local 线性同余生成器，无锁

### 3.5 DumpThread 实现 (`ubsocket_prof_tracepoint_dumper.h`)

```cpp
class DumpThread : public Referable {
    std::string file_path_;       // dump 目录
    std::string file_name_;       // {path}/ubsocket_profiling_{pid}.log
    std::ofstream dump_file_;     // append 模式
    uint16_t interval_min_;       // 1~5 分钟
    std::atomic<bool> running_;
    std::thread dump_thread_;
};
```

**DumpLoop**：

```cpp
void DumpLoop() {
    pthread_setname_np("ubs_prof");
    while (running_) {
        sleep(interval);          // 10ms 粒度可中断 sleep
        DumpData();               // Combine + WriteFile
    }
    DumpData();                   // 退出前最终 dump
}
```

- sleep 以 10ms 为粒度分片，确保 `running_=false` 后 ≤10ms 退出
- UT 模式下 interval 自动变为 10ms

### 3.6 模式选择机制 (`ubsocket_prof.cpp`)

```cpp
/* atomic：CLI 热切换（写）与业务线程打点（读）并发访问 */
static std::atomic<int> ubsocket_prof_mode_ext{0};

int ubsocket_prof_init(option) {
    ubsocket_prof_mode_ext.store((GlobalSetting::UBS_PROF_MODE == "ext") ? 1 : 0, release);

    if (ubsocket_prof_mode_ext.load(acquire) == 1) {
        TracerExt::Instance().InitExt(options);    // 扩展模式
    } else {
        Tracer::Instance().Init(options);          // 高性能模式
    }
    ubsocket_prof_enabled = 1;
}

int ubsocket_prof_record(tp_id, tp_name, timestamp, good) {
    if (ubsocket_prof_mode_ext.load(acquire) == 1) {
        return TracerExt::Instance().RecordExt(...);
    } else {
        return Tracer::Instance().Record(...);
    }
}
```

- 模式在 `init` 时从 `GlobalSetting::UBS_PROF_MODE` 读取；运行中可通过 CLI `ubstat delay -t mode -v <fast|ext>` 动态切换（内部执行 Uninit + 重新 Init，见 2.1.5）
- 每次调用 `ubsocket_prof_record` 需检查 `ubsocket_prof_mode_ext`（一次 relaxed 的 atomic load，x86 上为普通 MOV，无 LOCK 前缀开销）

---

## 4. 影响性分析

### 4.1 内存开销分析

#### 4.1.1 资源生命周期

PROF 功能的所有堆资源在 **enable 时分配、disable 时释放**，不占用常驻内存：

```
enable  (环境变量 UBSOCKET_PROF_ENABLE=true 或 CLI ubstat delay -t enable)
  → ubsocket_prof_init()
    → Tracer/TracerExt 单例构造 (Meyer singleton, 首次 Instance() 调用)
    → trace_groups_.reserve(1024)          (fast) 或 agents_[1024] 初始化 (ext)
    → trace_combiner_ 创建
    → DumpThread 创建 (若 enable_dump)
    → 首次 PROF_END → CreateTraceGroup()  (每线程惰性, fast)
                     或 CreateAgentExt()   (每CPU惰性, ext)

disable (CLI ubstat delay -t disable 或 ubsocket_uninit)
  → ubsocket_prof_uninit()
    → ubsocket_prof_enabled = 0
    → DumpThread::DumpStop() + 释放
    → inited_ = false
    → 注: fast 模式下 trace_groups_、trace_combiner_、tls_group 不清理，保留 disable 前的数据
    → 注: ext 模式下 agents_[] 全部置 nullptr + DecreaseRef()，trace_combiner_ = nullptr
```

**已知限制 (fast 模式)**: disable 不清除 `trace_groups_` 和各线程的 `tls_group` 指针，re-enable 后旧 TraceGroup 继续使用，disable 前积累的统计数据会残留在 re-enable 后的查询结果中。如需清零，应在 re-enable 后执行 `ubstat delay -t reset`。ext 模式无此问题（disable 时 agents_ 被置空，re-enable 后重建）。

**BSS/TLS 占位变量**（始终存在，零初始化，不占堆内存）：

| 变量 | 大小 | 说明 |
|------|------|------|
| `ubsocket_prof_enabled` | 4B | BSS，初始值 0 |
| `ubsocket_prof_mode_ext` | 4B | BSS，初始值 0 |
| `ubsocket_arm_cpu_freq` | 8B | BSS，初始值 1（安全除数） |
| `tls_group` | 8B/线程 | TLS，初始值 nullptr |
| **合计** | **~16B + 8B/线程** | BSS/TLS 占位，无堆分配 |

#### 4.1.2 打开 fast 模式

| 项目 | 大小 | 何时分配 | 何时释放 |
|------|------|---------|---------|
| Tracer 单例 | ~256B | `Instance()` 首次调用 (init) | 不释放 (Meyer singleton) |
| `trace_groups_` 容量 | 8KB | `Init()` 中 `reserve(1024)` | 不释放（disable 不清） |
| TraceCombiner | ~64B | `Init()` 中创建 | 不释放（disable 不清） |
| DumpThread | ~200B | `Init()` 中创建 (若 enable_dump) | `UnInit()` 中 `DumpStop()` + 释放 |
| **每线程 TraceGroup** | **~8KB** | 首次 `Record()` 惰性创建 | 不释放（disable 不清，`tls_group` 保留） |
| **堆合计（N线程）** | **~8.5KB + N×8KB** | | disable 时仅释放 DumpThread |

#### 4.1.3 打开 ext 模式

| 项目 | 大小 | 何时分配 | 何时释放 |
|------|------|---------|---------|
| TracerExt 单例 | ~8.5KB | `Instance()` 首次调用 (init) | 不释放 (Meyer singleton) |
| TraceCombinerExt | ~64B | `InitExt()` 中创建 | `UnInitExt()` 中 `= nullptr` |
| DumpThreadExt | ~200B | `InitExt()` 中创建 (若 enable_dump) | `UnInitExt()` 中 `DumpStopExt()` + 释放 |
| **每 CPU TraceGroupExt** | **~1MB** | 首次 `RecordExt()` 惰性创建 | `UnInitExt()` 中 `agents_[i].store(nullptr)` + `DecreaseRef()` |
| **堆合计（N核活跃）** | **~9KB + N×1MB** | | disable 时 agents_ + combiner 释放 |

#### 4.1.4 内存开销对比

| 状态 | 堆开销 | BSS/TLS 占位 | 说明 |
|------|--------|-------------|------|
| 关闭 (未 enable) | **0** | ~16B + 8B/线程 | 仅零初始化占位变量 |
| fast 启用 | ~8.5KB + 8KB/线程 | 同上 | disable 时仅释放 DumpThread，其余保留 |
| ext 启用 | ~9KB + 1MB/CPU | 同上 | disable 时释放 agents_ + combiner + DumpThread |
| fast disable 后 | ~8.5KB + N×8KB | 同上 | TraceGroup/combiner 保留，re-enable 后复用 |
| ext disable 后 | ~9KB | 同上 | agents_ + combiner 已释放 |

### 4.2 热路径性能开销分析

#### 4.2.1 关闭状态 (ubsocket_prof_enabled == 0)

PROF 打点代码始终编译进二进制，但运行时 `ubsocket_prof_enabled == 0` 时：

**PROF_START 展开**：
```cpp
uint64_t tpBeginCORE_WRITE = 0;
do { if (ubsocket_prof_enabled == 1) { tpBeginCORE_WRITE = ubsocket_get_timeNs(); } } while (0);
```
- `ubsocket_prof_enabled` 是全局 int，编译器无法在编译期确定其值为 0
- 运行时分支不进入：一次 `int == 1` 比较（分支预测 100% 命中 not-taken）→ **~1ns**

**PROF_END 展开**：
```cpp
do { if (ubsocket_prof_enabled == 1) { ubsocket_prof_record(...); } } while (0);
```
- 同上，一次分支比较 → **~1ns**

**总开销：~2ns/对**（一次分支预测命中的比较，无函数调用、无内存访问）

#### 4.2.2 打开 fast 模式 — 逐路径分析

**基础开销（每次 PROF_START + PROF_END 对）**：

| 操作 | aarch64 (cntvct_el0) | x86 (clock_gettime) | 说明 |
|------|---------------------|--------------------|----|
| PROF_START 取时间戳 | ~20ns | ~200ns | `mrs cntvct_el0` / VDSO |
| PROF_END 取时间戳 | ~20ns | ~200ns | 同上 |
| PROF_END Record 调用 | ~6ns | ~6ns | generation atomic load + tls 指针读 + 数组索引 + 累加 |
| **合计** | **~46ns** | **~406ns** | |

**Write 路径 (`DataTx::WriteV`)**：

| 打点位置 | tracepoint | 额外开销 |
|---------|-----------|---------|
| 入口 | `PROF_START(CORE_WRITE)` | 20ns |
| BuildIov 前 | `PROF_START(CORE_WRITE_POST_SEND)` | 20ns |
| BuildIov 前 | `PROF_START(CORE_WRITE_BUILD_IOV)` | 20ns |
| BuildIov 后 | `PROF_END(CORE_WRITE_BUILD_IOV)` | 25ns |
| PostSend 后 | `PROF_END(CORE_WRITE_POST_SEND)` | 25ns |
| 出口 | `PROF_END(CORE_WRITE)` | 25ns |
| **Write 路径总 PROF 开销** | | **~135ns** |

Write 路径本身的典型耗时（1KB 小包 PostSend）≈ 3-8μs，PROF 开销占比 ≈ **1.7-4.5%**

**Write UMQ 子阶段 (`umq_data_tx_ops.cpp`)**：

该文件内 16 对 PROF_START/END，覆盖 PollTx、PostSend、AllocTxBuf、DoUmqTxPoll 等子阶段。每对 ~45ns。

| 子阶段 | PROF 对数 | 开销 |
|--------|----------|------|
| PollTx (3个 sub-poll) | 3 | 135ns |
| PostSend | 5 | 225ns |
| DoUmqTxPoll | 4 | 180ns |
| 其他 | 4 | 180ns |
| **TX 子阶段总** | **16** | **~720ns** |

Write 完整路径（含 UMQ 子阶段）≈ 5-10μs，PROF 总开销 ≈ 855ns，占比 ≈ **8.5-17%**

**Read 路径 (`DataRx::ReadV`)**：

| 打点位置 | tracepoint | 开销 |
|---------|-----------|------|
| 入口 | `PROF_START(CORE_READ)` + `PROF_START(CORE_READ_EAGAIN)` | 40ns |
| PollRx 前 | `PROF_START(CORE_READ_POLL_RX)` | 20ns |
| PollRx 后 | `PROF_END(CORE_READ_POLL_RX)` | 25ns |
| EAGAIN 路径 | `PROF_END(CORE_READ_EAGAIN)` | 25ns |
| 成功路径 | `PROF_END(CORE_READ)` | 25ns |
| **Read 路径总** | | **~135ns** |

Read 路径典型耗时 ≈ 1-4μs，占比 ≈ **3.4-13.5%**

**Read UMQ 子阶段 (`umq_data_rx_ops.cpp`)**：

13 对 PROF_START/END，覆盖 HandleBuf、Rearm、PollRead、BufAlloc、BufFree、GetCqEvent、AckInterrupt 等。

| 子阶段 | PROF 对数 | 开销 |
|--------|----------|------|
| HandleBuf | 2 | 90ns |
| Rearm | 2 | 90ns |
| PollRead | 4 | 180ns |
| BufAlloc/Free | 3 | 135ns |
| GetCqEvent/AckInterrupt | 2 | 90ns |
| **RX 子阶段总** | **13** | **~585ns** |

**ub_native 大包路径 (`ubsocket_bigdata.cpp`)**：

38 对 PROF_START/END，覆盖 TrySenderPost、HandleTxCompletion、FinalizeIo 等 7 个子阶段 + DIAG 5 个 + 发送/接收/CQE 各分支。

| 子阶段 | PROF 对数 | 开销 |
|--------|----------|------|
| 发送 (TrySenderPost 等) | 8 | 360ns |
| TX CQE (HandleTxCompletion 等) | 8 | 360ns |
| RX (HandleRxControl 等) | 8 | 360ns |
| FinalizeIo 子阶段 | 7 | 315ns |
| DIAG (gen-check) | 5 | 225ns |
| 端到端 | 2 | 90ns |
| **ub_native 总** | **38** | **~1710ns** |

ub_native 典型耗时 ≈ 100-200μs，占比 ≈ **0.9-1.7%**（开销可忽略）

#### 4.2.3 打开 ext 模式 — 增量分析

ext 模式在 fast 模式基础上，每次 `Record` 额外执行蓄水池采样：

```cpp
// fast: Tracepoint::Record = 3 累加 + max/min = ~5ns
// ext:  TracepointExt::RecordExt = 3 累加 + max/min + 蓄水池采样 = ~10ns
```

| 操作 | fast | ext | 增量 |
|------|------|-----|------|
| Record (热路径) | ~6ns | ~11ns | +5ns |
| `sched_getcpu()` | 无 | ~2ns | +2ns |
| **每对 START+END** | ~46ns | ~53ns | +7ns |

ext 模式热路径增量 ≈ **+7ns/对**，16 对 Write 子阶段 ≈ +112ns，占比增量 ≈ **1.1%**。

#### 4.2.4 性能开销汇总

| 路径 | 关闭 (prof_enabled=0) | fast (aarch64) | ext (aarch64) | fast (x86) |
|------|----------------------|---------------|---------------|-----------|
| Write (tx.cpp) | ~6ns | 138ns | 159ns | 1218ns |
| Write UMQ 子阶段 | ~32ns | 736ns | 848ns | 6496ns |
| Write 完整 | ~38ns | 874ns | 1007ns | 7714ns |
| Read (rx.cpp) | ~6ns | 138ns | 159ns | 1218ns |
| Read UMQ 子阶段 | ~26ns | 598ns | 689ns | 5278ns |
| Read 完整 | ~32ns | 736ns | 848ns | 6496ns |
| ub_native 完整 | ~76ns | 1748ns | 2014ns | 15428ns |
| Connect | ~4ns | ~90ns | ~104ns | ~810ns |
| Accept | ~4ns | ~90ns | ~104ns | ~810ns |

#### 4.2.5 实测性能影响

在 aarch64 (Kunpeng 920) 环境下，使用 brpc ub_test echo 基准测试（单连接、queue_depth=10、req_size=100KB、expected_qps=1000），对比 PROF 关闭、PROF 打开 fast 模式、PROF 打开 ext 模式的端到端 RPC 时延，3 轮测试结果：

| 轮次 | 关闭 avg(us) | 关闭 p99(us) | fast avg(us) | fast p99(us) | ext avg(us) | ext p99(us) | fast/关闭 | ext/关闭 |
|------|-------------|-------------|-------------|-------------|-------------|-------------|----------|---------|
| Round 1 | — | — | 91 | 188 | 89 | 277 | — | — |
| Round 2 | 89 | 194 | 95 | 264 | 97 | 202 | 1.07 | 1.09 |
| Round 3 | 88 | 171 | 91 | 330 | 94 | 189 | 1.03 | 1.07 |

**结论**：
- fast 模式打开后，平均时延增加 3-7%，QPS 无变化
- ext 模式打开后，平均时延增加 7-9%，QPS 无变化
- p99 波动在正常范围内（171-330us），无系统性劣化
- 两种模式的开销均远低于 2 倍阈值，可用于生产环境长期运行
- 实测开销与理论分析一致：ext 模式比 fast 模式多 ~7ns/对，对端到端 ~90us 时延的影响约 1-2%

### 4.3 打开/关闭及配置参数对 CPU 的复杂度分析

#### 4.3.1 时间复杂度

| 操作 | fast 模式 | ext 模式 | 说明 |
|------|----------|---------|------|
| **单次 Record** | O(1) | O(1) | 数组索引 + 累加；ext 多一次 `sched_getcpu()` + 蓄水池 O(1) |
| **Combine** | O(T×N) | O(T×N + T×S×logS) | T=tracepoint数, N=线程/CPU数, S=1024(蓄水池排序) |
| **Init** | O(1) | O(1) | 创建单例 + TraceCombiner |
| **DumpThread::DumpData** | O(T×N) | O(T×N + T×S×logS) | 同 Combine |

具体数值：
- T = 120, N = 16（线程或核）
- fast Combine: 120×16 = 1920 次累加 ≈ **<0.1ms**
- ext Combine: 120×16 + 120×1024×10 ≈ 125万次操作 ≈ **~5ms**（仅 dump 时执行，不影响热路径）

#### 4.3.2 空间复杂度

| 模式 | 复杂度 | 说明 |
|------|--------|------|
| fast | O(T×N) | T=120 tracepoint × N=线程数 |
| ext | O(T×C) | T=120 tracepoint × C=CPU数 |

#### 4.3.3 配置参数对 CPU 的影响

| 参数 | 影响 |
|------|------|
| `UBSOCKET_PROF_ENABLE=false` | 打点代码始终编译，运行时 `ubsocket_prof_enabled==0` → 分支不进入，~2ns/对开销（仅分支预测命中的比较） |
| `UBSOCKET_PROF_ENABLE=true` + `fast` | 热路径 ~45ns/对 (aarch64)；Combine <0.1ms；内存 ~8KB/线程 |
| `UBSOCKET_PROF_ENABLE=true` + `ext` | 热路径 ~52ns/对 (aarch64)；Combine ~5ms；内存 ~1MB/CPU |
| `UBSOCKET_PROF_DUMP_INTERVAL_MIN=1` | DumpThread 每 1 分钟触发一次 Combine + 文件 I/O，~0.1ms~5ms 脉冲 |
| `UBSOCKET_PROF_DUMP_INTERVAL_MIN=5` | 降为每 5 分钟一次，脉冲频率 1/5 |
| `ENABLE_CPU_HARDWARE_ACCELERATION=ON` | aarch64 上时间戳从 ~200ns 降至 ~20ns，**热路径开销降低 4.5×** |
| `ENABLE_CPU_HARDWARE_ACCELERATION=OFF` | 回退到 `clock_gettime`，热路径开销增加 ~180ns/对 |

#### 4.3.4 CPU 缓存影响分析

**fast 模式**：

- `tls_group` 是 thread_local 指针，首次访问后驻留 L1 cache（8B）
- `TraceGroup::points_` 数组 = 7.5KB，约 120 个 cache line
  - 每次 Record 只访问 1 个 `Tracepoint`（64B = 1 cache line）
  - 热路径打点集中在前 ~20 个 tracepoint → **~20 cache line 驻留 L1**
  - 冷 tracepoint 首次访问触发 L1 miss → ~5ns penalty（仅一次）

**ext 模式**：

- `agents_[cpuId]` 原子读：per-CPU 指针常驻 L1（8B）
- `TraceGroupExt::points_` = ~1MB，远超 L1（通常 64KB）和 L2（通常 512KB-1MB）
  - 每次 Record 访问 1 个 `TracepointExt`（~8.4KB = ~132 cache line）
  - 蓄水池写入 `reservoir[i]` 在 8KB 范围内顺序写入 → **1-2 cache line miss per Record**
  - 相比 fast 模式额外 **~5-10ns cache miss penalty**

**DumpThread 对 CPU 的影响**：

- DumpThread 以 1-5 分钟周期运行，非实时
- Combine 时遍历所有 TraceGroup → 触发大量 cache miss（冷数据）
- 但在独立线程执行，不抢占热路径 CPU 时间
- `pthread_setname_np("ubs_prof")` 便于监控

#### 4.3.5 分支预测分析

PROF 宏内的 `if (ubsocket_prof_enabled == 1)` 分支：

- **关闭时**：`ubsocket_prof_enabled` 是全局 int（非编译常量），编译器无法消除分支。运行时分支始终不进入，CPU 分支预测器 100% 命中 not-taken，**~1ns/对**（一次 int 比较的流水线开销）
- **打开时**：分支始终进入，预测器 100% 命中 taken，**0 预测开销**
- **切换瞬间**：首次 enable 后的 1-2 次调用可能 mispredict → **~10ns 一次性 penalty**

`TraceGroup::Record` 中的 `UNLIKELY` 标注：

- `tp_id >= max_tp_count_`：永不触发（合法调用），预测器命中
- `has_name == 0`：首次调用触发一次，之后永不触发，预测器命中
- `LIKELY(good)`：成功路径占 >99%，预测器命中

#### 4.3.6 多核扩展性分析

| 模式 | 扩展性 | 瓶颈 |
|------|--------|------|
| fast | 线性扩展 | 无锁（thread_local），唯一锁在 Combine 时（低频） |
| ext | 线性扩展 | 无锁（per-CPU），`sched_getcpu()` ~2ns，唯一锁在 CreateAgentExt（首次） |
| Combine | O(N) | 遍历所有线程/CPU 的 group，N=16 时 <0.1ms（fast）或 ~5ms（ext） |

**结论**：两种模式在多核下均线性扩展，热路径无锁竞争。Combine 在独立线程执行，不影响数据路径吞吐。