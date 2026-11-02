# UBSocket 发送方向流控定界统计方案（TX-STAT）

> 目标：在 **ubsocket 侧** 采集 URMA/UMQ **发送方向** 的流控/队列异常统计，用于**快速定界发送路径问题**。
> 约束：热路径零可测影响、默认开启、采样间隔与落盘文件可配置、复用 tx poller 1ms 线程、落盘最小间隔 1s、过滤 0 值。
> **范围**：TX-STAT 覆盖发送方向两类埋点 —— **A) 提交侧 `umq_post` 失败（`UmqTxOps::PostSend`）** 与 **B) 完成侧 TX CQE 完成异常（tx poll 线程处理 `umq_poll` 返回的完成事件）**。两者共同刻画"发送方向的队列异常"：A 是提交即失败，B 是提交成功但发送完成失败（对端 RNR / ACK 超时 / 远端错误 / 本端错误 / 流控失败）。接收方向（RX 数据面 `umq_poll`）不在此范畴。

---

## 0. 统计说明

| 维度 | 统计说明（TX-STAT） |
|---|---|
| 数据来源 |  ubsocket 在 UMQ **API 调用边界**自埋点 |
| 统计方向 |  **发送方向两类**：A) 提交侧 `umq_post` 失败；B) 完成侧 TX CQE 完成异常（tx poll 线程） |
| 定界粒度 |  **per 失败类型**：post 9 桶（提交侧）+ cqe 6 桶（完成侧） |
| RNR 可见性 |  **可见**（完成侧 `cqe.rnr`：对端 RQ 不足，属发送完成异常） |
| 信用不足可见性 |  直接：`umq_post` 返回 `EAGAIN` 且区分全失败/部分失败 |
| 采样线程 |  **零新线程**，复用 `ubs_tp_tx` 1ms 线程（SINGLE 模式复用 `ubs_tx_poller` 100ms 线程） |
| 每周期成本 |  聚合全局 volatile 计数数组（固定大小，无链表遍历） |
| 稳态落盘 IO |  **异常为 0 时一次 write 都不做** |
| 对 UMQ 的耦合 |  无，仅依赖 `umq_buf_status_t` 公共枚举（编译期桶名表） |

---

## 1. 埋点设计

### 1.1 埋点（发送方向两类）

| 编号 | 位置 | 采集内容 | 定界价值 |
|---|---|---|---|
| **A** | `UmqTxOps::PostSend`<br/>`core/umq/umq_data_tx_ops.cpp` 各失败分支 | `umq_post` 失败 errno 直方图 | **提交侧**：信用不足 / jetty 耗尽 / qbuf 耗尽 / 授信超时 |
| **B** | `UmqTxHelper::LogTxCqeErrorMsg`<br/>`core/umq/umq_tx_helper.cpp`（由 tx poll 线程经 `PollTx → PollUmqTxInternal → umq_poll` 完成事件触发） | TX CQE 完成状态 `!= 0` 直方图 | **完成侧**：对端 RNR / ACK 超时 / 远端错误 / 本端错误 / 流控失败 |

> 注：埋点 B 处理的是**发送完成事件**（一个已提交的 send 的完成队列项），属于发送方向，不是 RX 数据面。`LogTxCqeErrorMsg` 是发送完成异常的**单一集中处理点**，SINGLE 与 POOL 两种模式的 tx poll 线程都经此路径，故一处埋点即覆盖两种模式。

### 1.2 失败分类桶（post 9 桶 + cqe 6 桶）

**提交侧（埋点 A，9 桶）**

```cpp
enum PostErr : uint8_t {
    POST_ERR_EAGAIN_ALL = 0, // 流控 credit 不足，全部失败   ★核心指标
    POST_ERR_EAGAIN_PART,    // 流控 credit 不足，部分失败   ★核心指标
    POST_ERR_ENOBUFS_ALL,    // jetty pool qbuf 耗尽，全失败
    POST_ERR_ENOBUFS_PART,
    POST_ERR_EMLINK,         // 无可用 jetty，已入 TpWaitQueue
    POST_ERR_ETIMEDOUT,      // 等对端授信回复超时（默认 1s）★核心指标
    POST_ERR_EFLOWCTL,       // ret == -UMQ_ERR_EFLOWCTL
    POST_ERR_NO_BADQBUF,     // bad_qbuf == nullptr 的异常分支
    POST_ERR_OTHER,
    POST_ERR_MAX
};
```

**完成侧（埋点 B，6 桶）** —— 由 `LogTxCqeErrorMsg` 按 `umq_buf_status_t` 映射到桶（`buf->status != 0` 时）：

```cpp
enum CqeErr : uint8_t {
    CQE_ERR_RNR = 0,         // 对端 RQ 不足 (UMQ_BUF_RNR_RETRY_CNT_EXC_ERR)        ★核心指标
    CQE_ERR_ACK_TIMEOUT,     // 对端未回 ACK (UMQ_BUF_ACK_TIMEOUT_ERR)              ★核心指标
    CQE_ERR_FC,              // 流控失败 (UMQ_FAKE_BUF_FC_ERR)
    CQE_ERR_REMOTE,          // 对端错误 (REM_*：resp_len / unsupported_req / op / access_abort)
    CQE_ERR_LOCAL,           // 本端错误 (LOC_*：len / op / access)
    CQE_ERR_OTHER,           // 其他 (unsupported opcode / flush / suspend / poison / 未知)
    CQE_ERR_MAX
};
```

分类直接复用现有判定结果（埋点 A 复用 `if/else if` 链；埋点 B 复用 `LogTxCqeErrorMsg` 已有 `switch (buf->status)`），**不新增任何判断逻辑**，只在既有分支里加一行自增。

---

## 2. 计数容器：全局 volatile 数组（无 TLS、无链表）

```cpp
// 全局计数（进程内共享，无 TLS）。定义见 tx_stat_block.cpp。
alignas(64) volatile uint64_t g_post_ok;
alignas(64) volatile uint64_t g_post_err[POST_ERR_MAX];
alignas(64) volatile uint64_t g_cqe_err[CQE_ERR_MAX];  // 埋点 B：TX CQE 完成异常桶
alignas(64) volatile uint64_t g_err_total;  // 脏标记：任一异常计数（post_err 或 cqe_err）自增时同步 ++
```

* **写侧（热路径）**：`g_tx_stat_on` 一次 bool 读 + 对应计数器一次 `++`（volatile，强制落真实内存）。**无 TLS、无链表、无内存分配、无系统调用、无跨 TU 符号。**
* **读侧（采样线程）**：`Aggregate()` 直接读各全局 volatile 求和到 `TxStatSnapshot`。可能读到滞后值——对统计无影响（下一周期自动补齐）。
* **线程退出**：全局变量进程级生命周期，天然无 use-after-free 风险，数值全程计入总量。
* **脏标记短路**：`g_err_total` 在任一异常计数（`post_err` 或 `cqe_err`）自增时同步 `++`，`SlowPathReport` 只需一次读即可判断是否出现任何异常（O(1) 读）。
* **精度取舍（刻意接受失真）**：用 `volatile uint64_t` 而非 `std::atomic`——本统计**只看失败率趋势、绝对计数允许失真**，因此并发 `++` 在多核下的丢更新（read-modify-write 无原子保证）可接受：分子分母按相近比例丢失，比值趋势基本保留。`volatile` 仅用于强制每次 `++` 都落真实内存，防止编译器把计数缓存进寄存器造成大块丢失（UB 唯一会真的搞乱数据的情形）。代价是 `post_ok`/`post_err`（多发送线程争抢）绝对计数会偏低，但失败率趋势仍可辨。

> **代价（绝对计数丢更新，已刻意接受）**：`volatile ++` 是普通 read-modify-write，无原子 RMW 保证，多核同时写同一计数器（主要是 `post_ok` / `post_err`，提交侧多发送线程争抢）会丢更新、且 cache line 在核间反复失效。`post_*` 单独 `alignas(64)` 仅缓解 false sharing，不解决丢更新。本统计只看失败率趋势，绝对计数偏低不影响定界；若日后需精确绝对值，再上 `std::atomic` 或 per-core 分片即可，非必需。

### 2.1 热路径宏

```cpp
extern volatile bool g_tx_stat_on;  // 初值 true=默认开启；单线程 Init/Finalize 写，运行期多埋点线程只读

// 完整宏见 tx_stat_block.h（含 ::ock::ubs::txstat 命名空间限定与 \ 续行），此处省略示意。
#define UMQ_POST_OK_ADD()                                          \
    do { if (LIKELY(g_tx_stat_on)) ++g_post_ok; } while (0)

#define UMQ_POST_ERR_ADD(k)                                        \
    do { if (LIKELY(g_tx_stat_on)) {                               \
             ++g_post_err[(k)];                                    \
             ++g_err_total; } } while (0)

#define UMQ_CQE_ERR_ADD(k)                                         \
    do { if (LIKELY(g_tx_stat_on)) {                               \
             ++g_cqe_err[(k)];                                     \
             ++g_err_total; } } while (0)
```

语义化热路径宏（调用点只用这些，不暴露底层数组）。命名：UMQ_<域>_<动作>_ADD。
*   - POST：提交侧 umq_post 计数（埋点 A）
*   - CQE ：发送完成侧异常计数（埋点 B）
* 全部为 `volatile ++`，保证每次自增都落真实内存；带 ERR 的宏同时打脏标记 `++g_err_total`（异常计数），用于稳态零 IO 短路；不带 ERR 的仅累加。

### 2.2 TLS 模型说明（本方案不依赖 TLS）

TX-STAT 计数采用**进程内共享的全局 `volatile uint64_t` 数组**（见 §2），不使用 `thread_local`——因此不存在 `global-dynamic` 的 `__tls_get_addr` 开销问题，也无需 `-ftls-model=initial-exec` 来兜底 TX-STAT 的热路径性能。

> CMake 中 `profiling_objects` 的 `-ftls-model=initial-exec` 选项（若存在）仅服务于 profiling 目录内**其余** TLS（如 `Tracer::tls_group` 等未加 per-variable 属性的变量），与 TX-STAT 无关；可保留也可移除，不影响本模块正确性。

---

## 3. 采样与落盘：复用 tx poller 1ms 线程

### 3.1 挂载点

**主路径（POOL 模式，默认）** —— `core/umq/umq_tp_tx_epoll_runner_ops.cpp`，`RUNNER_EVENT_TYPE_TP_TX_TIMER` 分支：

```cpp
if (tx_epoll_event->type == RUNNER_EVENT_TYPE_TP_TX_TIMER) {
    uint64_t expirations = 0;
    ssize_t s = LibcApi::read(tx_epoll_event->timer_fd, &expirations, sizeof(expirations));
    (void)s;
    ...
    do { poll_cnt = UmqTxHelper::PollUmqTx(args, ...); } while (poll_cnt > 0);

    // ★ 新增一行：复用已读出的 expirations，零额外时钟调用
    TxStatReporter::Instance().OnTick(expirations);

    return UBS_OK;
}
```

> **零成本复用点**：`expirations` 在现有代码里已被 `read()` 出来但标记为 `[[maybe_unused]]` 丢弃。直接拿来做 tick 累加，比 `steady_clock::now()` 更准（能自动补偿线程繁忙导致的丢 tick），且**不增加任何系统调用**。

**兜底路径（SINGLE 模式）** —— `core/ubsocket_tx_cqe_poller.cpp` 的 `RunInThread()`（100 ms timerfd）同样调 `OnTick(expirations)`。两条路径只会有一条生效（`UmqSetting::UMQ_TP_TYPE`）。该 tx poll 线程经 `PollTx → PollUmqTxInternal → umq_poll` 处理 TX CQE 完成事件，正是**埋点 B** 的执行上下文。

**多 timer 保护**：`AddTimerEvent()` 是 per-main-umqh 注册的，理论上可能存在多个 1ms 定时器。`TxStatReporter` 用一次性 CAS 声明 owner 线程，非 owner 直接 return，因此内部状态（`tick_` / `last_ns_` / `prev_`）**无需原子**。

### 3.2 OnTick：三级过滤，99.9% 的 tick 只做一次自增

```cpp
ALWAYS_INLINE void OnTick(uint64_t expirations) noexcept
{
    if (!enabled_) return;                              // L0: 关闭时 1 次 bool 读
    tick_ += expirations;
    if (LIKELY(tick_ < tick_gate_)) return;             // L1: 1 次比较，99.9% 在此返回
    tick_ = 0;
    uint64_t now = ubsocket_get_timeNs_compile();       // L2: 1/1000 概率才读时钟
    if (now - last_ns_ < interval_ns_) return;          //     防多 timer / 丢 tick 误触发
    last_ns_ = now;
    SlowPathReport(now);                                // NOINLINE 冷路径
}
```

`tick_gate_ = interval_ms / timer_period_ms`（POOL: 1000/1 = 1000；SINGLE: 1000/100 = 10）。

**单次 tick 成本：1 次 bool 读 + 1 次加 + 1 次比较 ≈ 1 ns**，且完全不触碰任何共享内存。

### 3.3 SlowPathReport：无异常零 IO，有异常异步落盘

```cpp
NOINLINE void SlowPathReport(uint64_t now_ns)
{
    // 无异常时直接返回（零 IO）：仅一次 O(1) 读脏标记 g_err_total 判断是否出现任何异常。
    uint64_t seq = TotalErrSeq();   // = g_err_total
    if (seq == prev_err_seq_ && !heartbeat_due(now_ns)) return;
    prev_err_seq_ = seq;

    // 聚合本窗口增量（cur - prev_）。
    TxStatSnapshot cur{}; Aggregate(cur);
    TxStatSnapshot d = cur.Delta(prev_); prev_ = cur;

    // 拼行：仅输出非 0 桶。
    int n = FormatLine(d, now_ns, "[TX-STAT]");
    if (n <= 0) return;

    // 异步落盘：交由 PrintStatsMgr 后台线程写出，定时线程不阻塞 IO。
    Statistics::PrintStatsMgr::GetPrintStatsMgr()->SubmitExternalLine(line_, n);
}
```

**四个 IO 约束（保证定时线程不受落盘影响）：**

1. **稳态零 IO** —— 异常计数全 0 时，靠 `g_err_total` 一次 O(1) 的读就返回，不聚合、不格式化、不写盘。正常运行时 tx poller 线程一次磁盘调用都不做。
2. **fd 常驻** —— 初始化时 `SetExternalSink` 一次性 `open(O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC, 0640)`，之后由后台线程长期持有，运行期不再 open/close。
3. **后台线程异步写出** —— 单行 < 4 KB，`O_APPEND` 保证单次 write 原子；由 PrintStatsMgr 后台线程在锁外写出，定时线程只入队、零阻塞。
4. **绝不 `fsync`/`fdatasync`** —— 交给 page cache 回写，避免阻塞任何业务线程。

**过滤 0 值的语义定义（重要）：**

| 情况 | 是否落盘 |
|---|---|
| 本窗口内 `post_err` 全为 0 | **不落盘**（即使有大流量） |
| 任一异常计数非 0 | 落盘；行内**只输出非 0 项**，同时附带 `post=成功/总数` 作为分母上下文 |
| `UBSOCKET_TX_STAT_HEARTBEAT_SEC > 0` 且到期 | 落一行心跳（用于确认统计模块存活），默认关闭 |
| `ubsocket_uninit()` | 强制落最后一个窗口 + 一行 `[TX-STAT-TOTAL]` 进程累计总量 |

---

## 4. 配置项（全部可配，默认开启）

| 环境变量 | 类型 | 默认值 | 范围 | 说明 |
|---|---|---|---|---|
| `UBSOCKET_TX_STAT_ENABLE` | bool | **`true`** | true\|false | 总开关，默认开启 |
| `UBSOCKET_TX_STAT_INTERVAL_MS` | int | `1000` | **1000** – 3600000 | 采样/落盘间隔，**下限 1s 硬约束** |
| `UBSOCKET_TX_STAT_FILE` | str | `/tmp/ubsocket/stat/tx_stat_<pid>.log` | ≤ 256 | 落盘文件路径，`<pid>` 自动展开 |
| `UBSOCKET_TX_STAT_MAX_MB` | int | `64` | 1 – 4096 | 单文件上限，超限 rename 为 `.1` 后重开（保留 1 份） |
| `UBSOCKET_TX_STAT_HEARTBEAT_SEC` | int | `0` | 0 – 3600 | 0=关闭；>0 时即使无异常也周期打一行 |

接入方式与现有配置完全一致：`common/ubsocket_defines.h` 加常量，`common/ubsocket_global_setting.h` 加静态成员，`.cpp` 里加 `ENV_*` 宏 + `AddRules()` 规则 + `LoadEnv()` 解析。`INTERVAL_MS` 走 `Int64Rule{ENV_TX_STAT_INTERVAL_MS, false, 1000, 3600000}`，越界自动落回默认值并告警。

---

## 5. 输出格式

单行 `key=value`，便于 `grep` / `awk` / `split`：

```
[TX-STAT] t=2026-08-04T20:15:43.102 win=1000ms post=1204357/1204291 fail_ppm=55
       post.eagain_all=1523 post.eagain_part=88 post.timeout=3
       cqe.rnr=12 cqe.ack_timeout=5 cqe.remote=2
```

* `post=总数/成功数`，`fail_ppm` = 失败百万分率（分母上下文，便于判断严重程度）
* 命名规则：`<类别>.<桶名>`，类别有 `post`（提交侧）与 `cqe`（完成侧）
* **只出现非 0 项**；上例中 9 个 post 桶只打印了 3 个、`cqe.rnr` 等完成异常只打印非 0 桶
* 进程退出时追加：

```
[TX-STAT-TOTAL] uptime=3612s post=4.31e9/4.31e9 post.eagain_all=91233 ... cqe.rnr=4412 ...
```

配套分析脚本：`src/ubsocket/tools/tx_stat_analyze.py`（按桶汇总、出 top-N 异常、画时间序列；**发送方向 post + cqe 双维度定界**）。

---

## 6. ★ 定界决策表（发送方向，本方案核心交付物）

**提交侧（埋点 A：umq_post 失败）**

| 观察到的指标特征 | 判定 | 下一步动作 |
|---|---|---|
| `post.eagain_all` 高、`post.timeout = 0` | **本端流控 credit 耗尽**（信用不足） | 发送速率超过对端授信；查 UMQ 流控窗口配置、对端回授信延迟 |
| `post.eagain_part` 高但 `eagain_all` 低 | credit 临界，边发边耗尽 | 通常可容忍；若时延敏感需扩窗口 |
| `post.timeout` 非 0 | **等对端授信回复超时（1s）** | 严重信号：对端卡死或链路异常，直接升级排查 |
| `post.emlink` 高 | **jetty pool 耗尽** | 调大 `UBSOCKET_JETTY_POOL_SIZE`；确认 TpWaitQueue 积压 |
| `post.enobufs_all` 高 | **qbuf 池耗尽** | 调大 `UBSOCKET_TX_DEPTH` / buf pool |
| `post.eflowctl` 非 0 | UMQ 流控层错误 | 与 port cooldown 日志联查 |

**完成侧（埋点 B：TX CQE 完成异常）**

| 观察到的指标特征 | 判定 | 下一步动作 |
|---|---|---|
| `cqe.rnr` 高 | **对端 RQ 不足**（RNR retry 耗尽） | 对端接收队列/RQE 不足或收割慢；查对端 RX 侧与链路 |
| `cqe.ack_timeout` 高 | **对端未回 ACK** | 对端卡死或链路异常，直接升级排查 ★核心 |
| `cqe.fc` 非 0 | 流控失败（FAKE_BUF_FC_ERR） | 与 port cooldown / 流控日志联查 |
| `cqe.remote` 高 | **对端错误**（resp_len / unsupported_req / op / access_abort） | 对端处理异常，查对端 URMA |
| `cqe.local` 高 | **本端错误**（len / op / access） | 本端内存/参数异常，查本端 URMA 与 buffer 生命周期 |
| `cqe.other` 非 0 | 其他完成异常（flush / suspend / poison / 未知） | 结合 `[UMQ_CQE]` 日志定位 |

**综合判读**

| 场景 | 结论 |
|---|---|
| `post.*` 全 0 且 `cqe.*` 全 0 | 发送方向健康，**可直接排除 UMSocket 发送方向**（提交与完成均正常） |
| `post.*` 有值、`cqe.*` 全 0 | 本端提交受阻（信用/资源），但已提交的都成功完成 —— 问题在**本端提交侧** |
| `post.*` 全 0（或低）、`cqe.*` 有值 | 提交通畅但**完成异常** —— 问题在**对端/链路/本端完成路径**（RNR/ACK 超时/远端错误） |
| **整个文件长期为空** | 发送方向无异常 | 把时延问题转向 brpc / 内存拷贝 / 调度 / RX 数据面（UMQ 侧 DFX） |

最后一行是这套统计最大的价值：**用"没有输出"这一事实低成本地排除 UMSocket 发送方向**，避免反复怀疑。

> 说明：TX CQE 完成异常（埋点 B）属于发送完成侧，故 `cqe.rnr`（对端 RQE 不足）、`cqe.ack_timeout`（对端未回 ACK）等**对端侧信号现已可见**。本方案仍不采集 RX 数据面（`umq_data_rx_ops.cpp` 的接收 `umq_poll`），故"本端 RX 收割不及时"类问题仍需结合 UMQ 侧 DFX 判断。

---

## 7. 代码落点清单

### 源文件（均在 `profiling/statistics/` 下）

| 文件 | 内容 |
|---|---|
| `profiling/statistics/tx_stat_defs.h` | `PostErr`（9 桶）/ `CqeErr`（6 桶）枚举与桶名字符串表 |
| `profiling/statistics/tx_stat_block.h` | 全局 `volatile uint64_t` 计数声明、`UMQ_POST_*` / `UMQ_CQE_*` 语义宏（底层 `++` 原语）、`g_tx_stat_on`（初值 true=默认开启） |
| `profiling/statistics/tx_stat_block.cpp` | 全局 volatile 计数定义、`Aggregate()`（普通读求和）、`TotalErrSeq()`（读 `g_err_total`） |
| `profiling/statistics/tx_stat_reporter.{h,cpp}` | `OnTick()` / `SlowPathReport()` / `FormatLine()` / `Init()` / `Finalize()`（落盘复用 PrintStatsMgr 后台线程，无 Rotate） |

> CMake 已有 `file(GLOB_RECURSE PROFILING_SRCS ${UBSOCKET_CSRC_DIR}/profiling/*.cpp)`，**这些文件自动纳入编译，无需改 CMakeLists**；`-ftls-model` 选项与 TX-STAT 无关（见 §2.2）。

### 修改（埋点接入）

| 文件 | 改动 |
|---|---|
| `common/ubsocket_defines.h` | 5 个常量（默认值/上下限） |
| `common/ubsocket_global_setting.{h,cpp}` | 5 个静态成员 + `ENV_*` 宏 + `AddRules()` + `LoadEnv()` |
| `core/umq/umq_data_tx_ops.cpp` | 埋点 A（PostSend 成功 / 各失败分支各 1 行 `UMQ_POST_*_ADD`） |
| `core/umq/umq_tx_helper.cpp` | 埋点 B（`LogTxCqeErrorMsg` 按 `buf->status` 映射 `CqeErr` 桶，1 行 `UMQ_CQE_ERR_ADD`） |
| `core/umq/umq_tp_tx_epoll_runner_ops.cpp` | TIMER 分支加 `OnTick(expirations)`（复用已读出的 expirations，零额外系统调用） |
| `core/ubsocket_tx_cqe_poller.cpp` | SINGLE 模式兜底 `OnTick(expirations)`；其 tx poll 线程同时驱动埋点 B |
| `ubsocket.cpp` | `ubsocket_init` 中 `TxStatReporter::Init()`（在 `UBS_INITED=true` 之前）；`ubsocket_uninit` 中 `TxStatReporter::Finalize()`（在 runner Stop **之后**，确保无并发写） |

**净新增代码量估算：约 480 行**（其中 ~200 行为桶名表和格式化）。热路径改动为 **2 行单语句自增**（`UMQ_POST_OK_ADD` / `UMQ_POST_ERR_ADD`）。

---

## 8. 性能预算与验证

### 预算

| 路径 | 频率 | 单次成本 | 说明 |
|---|---|---|---|
| `PostSend` 成功 | 每次 writev | **≈ 1 ns** | 1 bool 读 + 2 次 volatile ++（普通内存 RMW，无 lock） |
| `PostSend` 失败 | 异常时 | ≈ 2 ns | 冷分支，本身已有日志/回滚开销 |
| `OnTick` 常规 | 1000 次/s | **≈ 1 ns** | 1 bool + 1 加 + 1 比较 |
| `SlowPathReport` 无异常 | 1 次/s | ≈ 0.1–1 μs | 读全局数组（固定大小，无链表遍历） |
| `SlowPathReport` 有异常 | 1 次/s | ≈ 3–8 μs | 聚合 + snprintf + 1 次 write |

**结论**：稳态下对 1 ms 周期 poller 线程的占用 < 0.1 %，对 writev 路径（微秒级）的影响 < 0.05 %。有异常时 1s 一次的 μs 级抖动，且此时系统已处于异常态。

### 验证方法

1. **A/B 压测**：`UBSOCKET_TX_STAT_ENABLE=true/false` 各跑 3 轮，对比 P50/P99/P999 时延与 QPS，要求差异在测量噪声内（< 0.5 %）。
2. **微基准**：单独对 `UMQ_POST_ERR_ADD` 等语义宏做 `rdtsc` 循环基准，确认生成的是普通内存自增（反汇编应见 `add [rip+off], 1` / `inc [mem]` 之类，**不应见 `lock` 前缀**，证明未退化为原子 RMW、开销最低）。
3. **零 IO 验证**：无异常场景跑 10 分钟，`strace -f -e trace=write -p <pid>` 应观察到 **0 次**对统计文件的 write。
4. **功能验证**：人为制造 umq_post 失败（如调小信用窗口），确认 `post.<bucket>` 计数出现且与预期失败原因一致。

---

## 9. 风险与缓解

| 风险 | 影响 | 缓解 |
|---|---|---|
| 编译器把计数缓存进寄存器，大块丢失 | 绝对计数严重失真 | 已用 `volatile` 强制每次 ++ 落真实内存（§2），杜绝寄存器聚合 |
| 多核并发 ++ 丢更新 | 绝对计数偏低（提交侧 `post_*` 尤甚） | 本统计只看失败率趋势，比值基本保留（§2 精度取舍）；需精确值时改 `std::atomic` 或 per-core 分片 |
| 采样线程读到撕裂值 | 单周期数值略偏 | 计数为单调递增，下周期自动补齐；不影响趋势判断 |
| `uninit` 与 poller 线程并发 | 悬垂指针 | `Finalize()` 必须在 `EpollRunner::Stop()` 之后调用（顺序已在 §7 明确） |
| 落盘目录不可写 | 统计失效 | `Init()` 时试写一次，失败则打 WARN 并自动置 `g_tx_stat_on = false`，**不影响业务** |
| 多 main_umqh → 多个 1ms timer | 重复上报 | owner CAS + 时钟二次校验（§3.1/§3.2） |
| 日志文件无限增长 | 磁盘打满 | `MAX_MB` 限制 + 单份轮转；且稳态零写入 |

---

## 10. 实施步骤

| 阶段 | 内容 | 产出 |
|---|---|---|
| P1 | `profiling/statistics/` 四个源文件 + 5 项配置接入 | 可编译、可开关，无埋点 |
| P2 | 埋点 A 接入（`umq_data_tx_ops.cpp` 各失败分支）+ 埋点 B 接入（`umq_tx_helper.cpp::LogTxCqeErrorMsg`） | 提交侧 + 完成侧计数可用 |
| P3 | `OnTick` 挂载（POOL + SINGLE 双路径）+ 落盘 | 端到端可用 |
| P4 | 性能 A/B + 反汇编验证 + 人工构造失败验证 | 验证报告 |
| P5 | `tools/tx_stat_analyze.py` + 用户指南章节 | 交付文档 |

---

## 附录 A：桶名速查（post 9 桶 + cqe 6 桶）

| 类别 | 桶名 | 含义 |
|---|---|---|
| post | `eagain_all` / `eagain_part` | 流控 credit 不足（全/部分失败） |
| post | `enobufs_all` / `enobufs_part` | qbuf 池耗尽 |
| post | `emlink` | 无可用 jetty |
| post | `timeout` | 等授信回复超时 |
| post | `eflowctl` | UMQ 流控层错误 |
| post | `no_badqbuf` / `other` | 异常分支 |
| cqe | `rnr` | 对端 RQ 不足（RNR retry 耗尽） |
| cqe | `ack_timeout` | 对端未回 ACK |
| cqe | `fc` | 流控失败（FAKE_BUF_FC_ERR） |
| cqe | `remote` | 对端错误（resp_len / unsupported_req / op / access_abort） |
| cqe | `local` | 本端错误（len / op / access） |
| cqe | `other` | 其他完成异常（flush / suspend / poison / 未知） |

---

## 附录 B：使用指南

### B.1 配置项（环境变量，默认开启）

| 环境变量 | 默认值 | 范围 | 说明 |
|---|---|---|---|
| `UBSOCKET_TX_STAT_ENABLE` | `true` | true\|false | 总开关 |
| `UBSOCKET_TX_STAT_INTERVAL_MS` | `1000` | 1000~3600000 | 采样/落盘间隔（下限 1s 硬约束） |
| `UBSOCKET_TX_STAT_FILE` | `/tmp/ubsocket/stat/tx_stat_<pid>.log` | 非空，≤256 字符 | 落盘文件，`<pid>` 自动展开，父目录自动创建 |
| `UBSOCKET_TX_STAT_MAX_MB` | `64` | 1~4096 | 单文件上限，超限轮转为 `.1` |
| `UBSOCKET_TX_STAT_HEARTBEAT_SEC` | `0` | 0~3600 | 0=关闭；>0 时即使无异常也按周期打一行存活心跳 |

### B.2 输出格式

每行 `key=value`，仅打印非零桶；稳态（无异常）零 IO。示例：

```
[TX-STAT] t=2026-08-04T20:15:43.123 uptime=10s win=1000ms post=1000/950 fail_ppm=50000 \
  post.eagain_all=50 post.timeout=10
```

字段说明：
- `post=total/ok`：本窗口 umq_post 总次数 / 成功次数；`fail_ppm` = 失败百万分率。
- `post.<bucket>`：umq_post 失败分类（见附录 A）。
- `cqe.<bucket>`：TX CQE 完成异常分类（见附录 A）；仅打印非 0 桶。

### B.3 定界决策表（发送方向）

| 观测到的非零桶 | 定界结论 |
|---|---|
| `post.eagain_all` ↑ 且 `post.timeout`=0 | 本端 credit 耗尽，提交即失败 → 流量超限或对端授信慢 |
| `post.eagain_all` ↑ 且 `post.timeout` > 0 | 对端授信回复超时(~1s) → 对端卡死/链路中断/CPU 抢占 ★核心 |
| `post.eagain_part` ↑ 但 `eagain_all` 低 | credit 临界，边发边耗尽，通常可容忍 |
| `post.emlink` > 0 | jetty 池耗尽 → 连接过多/jetty 回收慢 |
| `post.enobufs_all` > 0 | qbuf 池耗尽 → 池过小/释放慢 |
| `post.eflowctl` > 0 | UMQ 流控层错误 → 与 port cooldown 日志联查 |
| `cqe.rnr` > 0 | 对端 RQ 不足(RNR) → 对端 RX 侧/链路问题 ★核心 |
| `cqe.ack_timeout` > 0 | 对端未回 ACK → 对端卡死/链路中断 ★核心 |
| `cqe.fc` > 0 | 流控失败 → 与 port cooldown 联查 |
| `cqe.remote` > 0 | 对端处理错误 → 查对端 URMA |
| `cqe.local` > 0 | 本端完成错误 → 查本端 URMA/buffer 生命周期 |

> 注意：埋点 B 的 `cqe.*` 已覆盖发送完成侧（含对端 RNR / ACK 超时等**对端侧信号**）。本方案仍不采集 RX 数据面，故"本端 RX 收割不及时"类问题需另查 UMQ 侧 DFX。

### B.4 分析脚本

`src/ubsocket/tools/tx_stat_analyze.py`：解析 `tx_stat_<pid>.log`，输出**发送方向(post + cqe 双维度)失败定界**报告，内容包括：核心结论、发送侧定界推理、提交侧/完成侧累计、桶级明细、Top-N 异常窗口与时间序列。

```bash
python3 tx_stat_analyze.py <log_file> [--top N] [--series]
```
