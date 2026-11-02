# UBSocket Core Dump 问题专项治理

> 专项范围：ubs-comm（UBSocket/UMQ 适配层）与 brpc 集成侧的进程退出时序类 core dump。
> 快照时间：2026-08-29。代码基线以当日工作区为准。

---

## 1. 专项背景

线上/测试环境曾在进程退出阶段发生 coredump，栈顶特征为 `_Hashtable::_M_find_before_node`，
崩溃线程为 `ubs_reaper`。专项目标：

1. 复盘历史 core 根因链，验证修复落地情况；
2. 系统性排查同类风险（静态析构顺序 × 后台线程并发）；
3. 沉淀 core 取证 SOP，供后续新 core 快速定位。

## 2. 历史 core 案例复盘

### 2.1 案例 A：PeerEidTable 退出期 use-after-free（已修复）

**崩溃现场**：reaper 线程在 `Release() → map.find` 解引用已释放的节点指针，`this` 指向静态存储区。

**根因链**（三个条件叠加）：

1. `PeerEidTable` 原为 Meyers 单例，析构随 `__cxa_atexit` 注册，会释放分片 unordered_map 的桶数组与节点；
2. `TxCqePoller` 是 LeakySingleton（永生），其 reaper 线程在 main 返回后的 static destruction 阶段仍经
   `~UmqSocket → PeerEidTable::Instance().Release()` 触达该表；
3. `__cxa_atexit` 为 LIFO：`PeerEidTable` 析构注册晚于 brpc 的
   `atexit(StopAndJoinGlobalDispatchers)`（该函数内才调用 `ubsocket_uninit()` 停 reaper），
   导致**先析构表、后停线程**。

**修复方案**：`umq_socket.cpp:1048-1060` 改为函数级 static 指针 + `new`，永不 delete
（等效 LeakySingleton 语义）。不复用 `LeakySingleton` 模板的原因：其 `Instance()` 走
`std::call_once`，在 bthread 栈上不安全（TLS 传参机制）；static 指针初始化用 `__cxa_guard`（futex），
bthread 安全。

**修复验证**：✅ 已落地，注释完整（含根因、栈顶特征、为何不能继承 LeakySingleton）。

**泄漏代价**：KB 级（16 分片 2-3KB + 少量 64B 条目），仅在进程退出瞬间存在，OS 回收地址空间，
无实际影响。项目内 7+ 单例采用同款故意泄漏模式。

### 2.2 案例 B："mempool tseg not exist"（已修复）

**现象**：退出时报 `mempool <id> tseg not exist`（串来自底层 umq/urma 库，是"释放后仍 poll"的特征）。

**根因**：`EpollRunner<T>` 是 LeakySingleton，退出时不自动析构、后台 poller 线程不自动 join；
若 `UmqBackend::UnInit()`（umq_uninit）先执行释放了 umq/mempool/tseg，runner 线程继续
`umq_poll/umq_post` 即触发。

**修复方案**：`ubsocket_uninit()` 中在 `UmqBackend::UnInit()` **之前** Stop 三个 runner。但
`TRANSPORT_POOL_TX_RUNNER` 不能停太早：socket 析构链（`UmqSocket::UnInitialize`）会调用其
`DelEpollEvent`，而 `Stop()` 会把 `ops_` 置空导致空指针。最终顺序见 §3.2。

**修复验证**：✅ 已落地（`ubsocket.cpp:293-299`），且 `Stop()` 幂等、对未 Start 的 runner 是安全 no-op。

## 3. 已完成修复验证（本次专项复核）

### 3.1 PeerEidTable 永生化

| 项 | 结论 |
|----|------|
| 实现 | `umq_socket.cpp:1048-1060`，static 指针 + new |
| 初始化安全 | `__cxa_guard`（futex），bthread 栈安全 |
| 并发保护 | 16 分片 `std::mutex`，操作全部持锁 |
| 残留访问路径 | `~UmqSocket`（umq_socket.h:148）Release，退出期安全 |

### 3.2 ubsocket_uninit() 清理顺序（`ubsocket.cpp:236-315`）

```
umq_exiting_set(true)                  // umq 侧 poll 入口跳过访问，防退出期 UAF
GlobalSetting::MarkExiting()           // 本仓拆链路径使用
ProbeManager::Stop()                   // (条件) Meyers 单例，join worker
ExecutorService::Stop()                // (条件) Meyers 单例，join 线程池
Profiling::Uninit() / TracePrintThread::Stop() / StopStatsCollection()
StatExporter::Finalize()
TxCqePoller::Stop()                    // 同步 join reaper（先 drain retire 列表）
ArraySet<Socket>::ForEach(shutdown)    // 关所有非 listen fd，防窗口期 writev 回退 TCP
ArraySet<Socket>::ReleaseAll()         // 释放全部 socket（触发 ~UmqSocket 链）
ArraySet<EventPoll>::ReleaseAll()
CleanAllSocketEpollMappers()
SHARE_JFR_RX_RUNNER::Stop()            // 三个 runner 必须在 ReleaseAll 之后
TRANSPORT_POOL_TX_RUNNER::Stop()       // （socket 析构链要 DelEpollEvent）
TRANSPORT_POOL_EVENT_RUNNER::Stop()
delete g_zcopy_allocator
UmqBackend::UnInit()                   // 最后释放 umq/mempool/tseg
```

顺序符合 AGENTS.md 记录的规范（先停 reaper → 释放 socket → 停 runner → umq uninit）。

### 3.3 brpc 侧 atexit 链路（`event_dispatcher.cpp:47-79`）

```
main return
  └─ atexit(StopAndJoinGlobalDispatchers)     // LIFO 注册于首个 dispatcher 创建时
       ├─ Stop()/Join() 全局 EventDispatcher  // epoll 循环退出
       └─ ubsocket_uninit()                   // BRPC_WITH_URMA && FLAGS_ubsocket_enable
```

配套事实：

- `g_task_control`（bthread 调度器，`bthread.cpp:85`）为裸指针 new 后永不 delete —— atexit 阶段
  bthread 存活，`UBSocketPollerConsumer::Stop()` 里的 `bthread_usleep` 轮询（`ubsocket_initializer.cpp:259`）
  与 `UbsEventDispatcher::Join()` 的 `bthread_join` 均可正常工作；
- `ubsocket_uninit` → `EpollRunner backend Stop()` → brpc 注册的
  `remove_consumer/remove_direct_poller` 回调 → `UBSocketPollerConsumer`/`UbsEventDispatcher`
  Stop/Join/delete（`ubsocket_initializer.cpp:344-383`）—— 均在 atexit 阶段完成，时序闭环；
- 业务对象（Server/Channel/SocketMap）析构发生在 main 内，早于 atexit；`SocketMap` 析构时
  `bthread_stop/bthread_join` 掉 WatchConnections（`socket_map.cpp:146-177`），此时 ubs socket
  尚未释放，安全。

## 4. 系统性风险排查结果

### 4.1 单例生命周期清单

**永生单例（安全基线）**

| 单例 | 实现方式 | 备注 |
|------|---------|------|
| `EpollRunner<T>` × 3 | LeakySingleton | 停止顺序陷阱见 §2.2 |
| `TxCqePoller` | LeakySingleton | `Stop()` 同步 join reaper |
| `ArraySet<Socket>` / `ArraySet<EventPoll>` | LeakySingleton | `ForEach` 持 Ref（引用计数），`ReleaseAll` 后遍历安全 |
| `EidRegistry` | LeakySingleton | UT 中需 `UnregisterEid()` 清理 |
| `PeerEidTable` | static 指针 + new | 本次专项修复（§2.1） |
| brpc `g_task_control` | 裸指针永不 delete | 保证 atexit 阶段 bthread 可用 |

**Meyers 单例（按析构副作用分级）**

| 单例 | 位置 | 析构副作用 | 风险 |
|------|------|-----------|------|
| `DataPlaneTable` | `umq_socket.cpp:990` | **无**（atomic 指针数组，trivial 析构，页永不释放） | 安全，但见 R4 |
| `StatExporter` | `stat_exporter.h:25` | 无（`= default`） | 安全 |
| `ProbeManager` | `probe_manager.h:111` | `Stop()`（幂等）：join worker + `UBS_VLOG_DEBUG` | R1 |
| `ExecutorService` | `ubsocket_thread_pool.h:57` | 未 Stop 则 `Stop()`（join 线程池） | R1 |
| `TracePrintThread` | `ubsocket_trace.cpp:33` | join 线程 + `ForEach(TRACE_FLUSH)` | R1（弱） |

### 4.2 残余风险清单

**R1【中】非 brpc 用户未调用 `ubsocket_uninit()` 的退出路径**

brpc 用户由 atexit 自动覆盖；纯 ubs-comm 用户不调用时，后台线程跑到 static destruction 阶段，
且 `ProbeManager`/`ExecutorService`/`TracePrintThread` 三个 Meyers 单例的析构函数会在该阶段执行
join + 日志调用。若日志后端单例先析构，析构期打日志存在崩溃窗口。
建议：三者的析构路径改为"只 join、不打日志"，或统一永生化（见 §5 建议 2）。

**R2【中低】`exit()` 异常路径（bthread/信号处理器中调 exit）**

跳过栈上业务对象析构，SocketMap 的 WatchConnections bthread 仍在运行。atexit 的
`ubsocket_uninit` 释放 `ArraySet<Socket>` 后，watch bthread 经 brpc::Socket 访问已关闭的 fd
（EBADF，功能异常）；brpc::Socket 对象本身未析构，不至于 UAF。
建议：业务侧避免 exit() 直呼，统一走 main return。

**R3【低】多次调用 `ubsocket_uninit()`**

`TxCqePoller::Stop`（CAS）、`EpollRunner::Stop`（`exit_efd_<0` 直接 return）、`ArraySet::ReleaseAll`
均幂等；`UmqBackend::UnInit` 的幂等性未专项验证。建议补一次幂等性确认或 UT。

**R4【低】`DataPlaneTable` 的安全性依赖"无显式析构函数"**

当前 trivial 析构 = 页永不释放，退出期 `~UmqSocket → Peek/DestroyEntry` 安全。但任何人未来给它
补一个释放 pages_ 的析构函数，就会复现 PeerEidTable 同款 core。
建议：在 `Instance()` 处补注释声明该不变量（与 PeerEidTable 注释同款），或直接永生化。

**R5【信息】新 core 待分析**

本地工作区无新 core 文件；`log/client_error.log`、`log/server_error.log` 为 8/14、8/16 的
port-down/降级测试日志，无崩溃记录。远程环境产出新 core 后按 §6 SOP 取证。

## 5. 治理建议（优先级排序）

1. **防御性注释**（低成本，防回归）：在 `DataPlaneTable::Instance()`（umq_socket.cpp:990）补充
   "禁止添加释放 pages_ 的析构函数"注释，说明退出期 reaper/worker 访问路径。
2. **收敛 Meyers 单例**（中期）：`ProbeManager`/`ExecutorService`/`TracePrintThread` 统一改为
   static 指针 + new 永生模式（PeerEidTable 同款，注意 bthread 安全用 `__cxa_guard` 而非
   `std::call_once`）；或至少剥离析构函数中的日志调用。
3. **补 `ubsocket_uninit` 幂等性 UT**：连续调用两次 uninit + uninit 后再创建/关闭 socket 的行为。
4. **规范业务退出**：文档明确要求纯 ubs-comm 用户在 main 返回前调用 `ubsocket_uninit()`；
   brpc 用户自动覆盖。

## 6. Core 取证 SOP（新 core 快速定位）

```bash
# 1. 加载（用产出 core 的同版本二进制 + 带 debug 符号）
gdb ./ub_test_client core.<pid>

# 2. 全线程栈 + 崩溃帧定位
(gdb) thread apply all bt
(gdb) frame <N>            # 进入栈顶业务帧

# 3. 判读崩溃对象来源（关键：this 指针在静态区 or 堆）
(gdb) info symbol <this指针值>
#   - 静态存储区符号 → Meyers 单例已析构（案例 A 模式）
#   - 堆地址 → 对象被提前 delete / 写坏（需查引用计数路径）

# 4. 确认退出阶段（是否 static destruction 期崩溃）
(gdb) bt                   # 栈底是否有 __run_exit_handlers / _dl_fini
#   栈底 __run_exit_handlers → 退出期时序问题，对照 §3.2 顺序排查

# 5. reaper 线程专项（若崩溃线程是 ubs_reaper）
(gdb) thread apply all bt   # 找 ubs_reaper / ubs_tp_tx / ubs_trace 线程
(gdb) p TxCqePoller 单例状态 / stopped_ / retired_ 列表

# 6. "mempool tseg not exist" 类（案例 B 模式）
#    检查崩溃线程是否在 umq_poll/umq_post → 对照 runner Stop 顺序（§3.2）
```

**判读速查**：

| 栈顶特征 | 指向 | 对照章节 |
|----------|------|---------|
| `_Hashtable::_M_find_before_node` + 静态区 this | Meyers 单例析构后并发访问 | §2.1 |
| `mempool ... tseg not exist` | 释放后仍 poll（runner 未先停） | §2.2 |
| `EpollRunner` 相关空指针（ops_ 解引用） | Stop 顺序颠倒（TX runner 停早了） | §3.2 |
| `std::terminate` + joinable thread | Meyers 单例析构未 join | R1 |
| bthread 栈上 call_once/TLS 崩溃 | 单例初始化方式不兼容 bthread | §2.1 |

## 7. 关键代码位置索引

| 内容 | 位置 |
|------|------|
| PeerEidTable 修复（永生化） | `src/ubsocket/csrc/core/umq/umq_socket.cpp:1048-1060` |
| DataPlaneTable（Meyers，无析构副作用） | `src/ubsocket/csrc/core/umq/umq_socket.cpp:990-995` |
| ~UmqSocket 退出期触达链 | `src/ubsocket/csrc/core/umq/umq_socket.h:137-150` |
| ubsocket_uninit 完整顺序 | `src/ubsocket/csrc/ubsocket.cpp:236-315` |
| TxCqePoller::Stop（join reaper） | `src/ubsocket/csrc/core/ubsocket_tx_cqe_poller.cpp:160-201` |
| EpollRunner backend Stop（触发 brpc 回调） | `src/ubsocket/csrc/core/ubsocket_event_epoll.cpp:146-154,201-209` |
| ArraySet（ForEach Ref 保护 / ReleaseAll） | `src/ubsocket/csrc/common/ubsocket_set.h:106-139` |
| LeakySingleton 模板 | `src/ubsocket/csrc/common/ubsocket_leaky_singleton.h` |
| TracePrintThread（Meyers + join） | `src/ubsocket/csrc/profiling/trace/ubsocket_trace.cpp:33-46` |
| ProbeManager（Meyers + Stop 幂等） | `src/ubsocket/csrc/profiling/probe/probe_manager.h:111-172,448-451` |
| ExecutorService（Meyers + 析构 Stop） | `src/ubsocket/csrc/common/ubsocket_thread_pool.h:57-67` + cpp:12-17 |
| brpc atexit 注册 | `brpc/src/brpc/event_dispatcher.cpp:47-79` |
| brpc poller 回调注册/移除 | `brpc/src/brpc/ubsocket_initializer.cpp:325-383,817` |
| UbsEventDispatcher（bthread + pipe 唤醒） | `brpc/src/brpc/ubs_event_dispatcher.cpp:32-149` |
| SocketMap 析构（WatchConnections join） | `brpc/src/brpc/socket_map.cpp:146-203` |
| g_task_control（永生） | `brpc/src/bthread/bthread.cpp:85` |
