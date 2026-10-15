# URMA 独立编译与 umq 解耦方案

> 范围：`src/ubsocket/csrc/ubsocket.cpp` + `src/ubsocket/CMakeLists.txt` + `src/ubsocket/csrc/CMakeLists.txt` + `src/ubsocket/csrc/ubsocket_epoll.cpp` + `src/ubsocket/csrc/core/ubsocket_socket.cpp` + `src/ubsocket/csrc/core/ubsocket_event_epoll.cpp` + `src/ubsocket/csrc/core/ubsocket_tx_cqe_poller.cpp` + `src/ubsocket/csrc/common/ubsocket_lock.cpp` + `src/ubsocket/csrc/profiling/probe/*` + `src/ubsocket/csrc/profiling/statistics/*` + `src/ubsocket/csrc/cli/*` + `src/ubsocket/csrc/under_api/dl_umq_api.h` + `build/build_umq_and_ubsocket.sh`
> 分支：`br_urma_rw_0728`
> 状态：方案待实施（已确认决策，未落地代码）

## 1. 背景与目标

`br_urma_rw_0728` 分支 ubsocket 初始化已硬编码切到 urma backend（`ubsocket.cpp:160`），但只是"半切换"：
- init/uninit/allocator 切到 urma 了
- profiling/trace/cli 仍直接引用 `umq::UmqSetting`/`UmqApi`（残留耦合）
- `build_umq_and_ubsocket.sh:85` 仍强制 `UMQ_BUILD=on`（urma 模式不需要 libumq.so）

**目标**：用编译选项控制 ubsocket 使用 umq 还是 urma 后端，所有选择点按宏自动切换，不再硬编码注释；urma 模式能脱离 libumq.so 独立编译。

## 2. 设计决策（已确认）

| # | 决策 | 选择 |
|---|------|------|
| 1 | CMake 默认后端 | **urma**（`BUILD_URMA_DLOPEN_BACKEND=ON`, `BUILD_UMQ_DLOPEN_BACKEND=OFF`） |
| 2 | 实现方式 | **统一别名宏**（一处 #ifdef 定义别名，调用处统一） |
| 3 | 公共代码残留 | **先用 `#ifdef` 包起来跳过**（urma 模式跳过 umq trace/probe/runner） |
| 4 | 实施节奏 | 先生成方案文件，确认后再实施 |
| 5 | 解耦范围 | **core/umq 适配层 + libumq.so 都解耦**：urma 模式不编 `core/umq/*.cpp`、不 dlopen/link libumq.so（运行期脱离）；仅 umq 模式依赖 libumq.so。编译期 umq 类型头阶段一保留（源码仓自带），阶段二抽象通用类型消除（见 4.3.1） |

## 3. 前置事实

### 3.1 底层引擎关系

`libumq.so` 内部 link `liburma.so`/`libtpsa.so`（`src/hcom/umq/src/umq_ub/CMakeLists.txt:33`、`src/hcom/umq/src/CMakeLists.txt:34`）。层次：

```
ubsocket 适配层
  ├ umq 适配层 (core/umq) → libumq.so (高层封装) → liburma.so (verbs) → UB 硬件
  └ urma 适配层 (core/urma) → liburma.so (verbs) → UB 硬件   ← 绕过 libumq.so
```

urma verbs 是底层统一接口，umq 是基于 urma 的高层封装。urma 适配层直接用 verbs，能力上是 umq 超集基础。

### 3.2 现有宏体系（复用）

`src/ubsocket/CMakeLists.txt:11-13` 已有三个 option：
- `BUILD_UMQ_DLOPEN_BACKEND`(ON) → 宏 `UMQ_DLOPEN_BACKEND_ENABLED`
- `BUILD_URMA_DLOPEN_BACKEND`(ON) → 宏 `URMA_DLOPEN_BACKEND_ENABLED` + `URMA_BACKEND_ENABLED`
- `BUILD_UMQ_ADAPTER_BACKEND`(OFF) → `UMQ_ADAPTER_BACKEND_ENABLED`

`dl_api.cpp:16` 已用 `#ifdef URMA_DLOPEN_BACKEND_ENABLED`，`dl_umq_api.h:14` 已用 `#ifdef UMQ_DLOPEN_BACKEND_ENABLED`。

## 4. 控制点清单（全量改动）

### 4.1 CMake 层

**`src/ubsocket/CMakeLists.txt`**
- `:11-13` option 默认值改为：`BUILD_UMQ_DLOPEN_BACKEND` **OFF**、`BUILD_URMA_DLOPEN_BACKEND` **ON**
- `:19-21` 已有 adapter/dlopen 互斥，**新增 umq_dlopen/urma_dlopen 互斥**：
  ```cmake
  if (BUILD_UMQ_DLOPEN_BACKEND AND BUILD_URMA_DLOPEN_BACKEND)
      message(FATAL_ERROR "BUILD_UMQ_DLOPEN_BACKEND and BUILD_URMA_DLOPEN_BACKEND cannot both be ON")
  endif ()
  ```

**`src/ubsocket/csrc/CMakeLists.txt:26-28`**
- umq GLOB 从无条件改为 `if(BUILD_UMQ_DLOPEN_BACKEND)` 条件化（与 urma `:30-34` 对称）：
  ```cmake
  if (BUILD_UMQ_DLOPEN_BACKEND)
      file(GLOB UMQ_CORE_SRCS ${UBSOCKET_CSRC_DIR}/core/umq/*.cpp)
      list(APPEND CORE_SRCS ${UMQ_CORE_SRCS})
  endif ()
  ```

### 4.2 ubsocket.cpp 统一别名宏（核心）

在 `ubsocket.cpp` 顶部（include 区后）定义别名宏块，**只此一处 #ifdef**：

```cpp
#ifdef URMA_DLOPEN_BACKEND_ENABLED
    #include "core/urma/urma_backend.h"
    #define UBS_BACKEND_INIT()            urma::UrmaBackend::Instance().Init()
    #define UBS_BACKEND_UNINIT()          urma::UrmaBackend::Instance().UnInit()
    #define UBS_BACKEND_SET_LOG_LEVEL(l)   urma::UrmaBackend::Instance().SetLogLevel(l)
    #define UBS_BACKEND_GET_ALLOCATOR()   urma::UrmaBackend::Instance().GetAllocator()
    #define UBS_LOAD_FLAGS                (LOAD_LIBC | LOAD_URMA)
#elif defined(UMQ_DLOPEN_BACKEND_ENABLED)
    #include "core/umq/umq_backend.h"
    #include "core/umq/umq_setting.h"
    #define UBS_BACKEND_INIT()            umq::UmqBackend::Init()
    #define UBS_BACKEND_UNINIT()          umq::UmqBackend::UnInit()
    #define UBS_BACKEND_SET_LOG_LEVEL(l)  umq::UmqBackend::SetLogLevel(l)
    #define UBS_BACKEND_GET_ALLOCATOR()   (new umq::UmqZeroCopyAllocator())
    #define UBS_LOAD_FLAGS                (LOAD_LIBC | LOAD_UMQ)
#endif
```

调用处改动（删原硬编码/注释，改用别名宏）：

| 行 | 原内容 | 改为 |
|----|--------|------|
| :20-22 | 无条件 include 三个头 | 删除，移入别名宏块条件 include |
| :106 | `DlApi::Load(LOAD_LIBC \| LOAD_URMA)` | `DlApi::Load(UBS_LOAD_FLAGS)` |
| :159-168 | `//#ifdef` 注释 + 硬编码 `UrmaBackend::Instance().Init()` + `//UmqBackend::Init()` 注释 | `result = UBS_BACKEND_INIT();` |
| :279-280 | `//UmqBackend::UnInit()` 注释 + 硬编码 `UrmaBackend::Instance().UnInit()` | `UBS_BACKEND_UNINIT();` |
| :301 | `ock::ubs::umq::UmqBackend::SetLogLevel(level)` | `UBS_BACKEND_SET_LOG_LEVEL(level);` |
| :314-315 | `//UmqZeroCopyAllocator` 注释 + 硬编码 `UrmaBackend::GetAllocator()` | `g_zcopy_allocator = UBS_BACKEND_GET_ALLOCATOR();` |

### 4.3 公共代码残留（#ifdef 包起来跳过）

urma 模式下这些 umq 引用用 `#ifdef UMQ_DLOPEN_BACKEND_ENABLED` 包起来跳过：

| 位置 | 内容 | 处理 |
|------|------|------|
| `ubsocket.cpp:195-197` | `umq::UmqSetting::UMQ_TRANS_MODE`（GlobalStatsMgr） | `#ifdef UMQ_DLOPEN_BACKEND_ENABLED` 包 |
| `ubsocket.cpp:209-214` | `umq::UmqSetting::UMQ_TRANS_MODE`（trace transMode） | 同上 |
| `ubsocket.cpp:215-233` | split trace `UmqApi::umq_stats_trace_start` | `#ifdef UMQ_DLOPEN_BACKEND_ENABLED` 包（urma 模式跳过 umq trace） |
| `ubsocket.cpp:243-255` | uninit split trace `UmqApi::umq_stats_trace_stop` | 同上 |
| `ubsocket.cpp:275-277` | 三个 `EpollRunnerFactory` umq runner（SHARE_JFR/TP_TX/TP_EVENT） | `#ifdef UMQ_DLOPEN_BACKEND_ENABLED` 包（urma 用 `UrmaPoller`） |
| `ubsocket_epoll.cpp:31/100` | `UrmaEventPoll` 硬编码 | `#ifdef URMA_DLOPEN_BACKEND_ENABLED` / `#elif UMQ_DLOPEN_BACKEND_ENABLED` 切换 umq EventPoll |
| `profiling/probe/probe_manager.h:44-45` | `using umq::UmqSetting/UmqSocket` | `#ifdef UMQ_DLOPEN_BACKEND_ENABLED` 包 |
| `profiling/probe/probe_manager.cpp` umq 引用（`SendProbePacket`/`HandleReceivedPacket`/`UmqPerfCallback`/`UmqSocket*` 强转） | 同上（整个 umq probe 路径包，urma 模式 probe 暂缺） |
| `profiling/statistics/statistics.cpp:69-92` | `UmqApi::umq_stats_tp_perf_start/info_get/stop`（libumq.so C API） | `#ifdef UMQ_DLOPEN_BACKEND_ENABLED` 包 |
| `profiling/statistics/statistics.h:30-36` | `#include core/umq/umq_eid_table/umq_setting/umq_socket` + `umq_api/umq_dfx_api` | `#ifdef UMQ_DLOPEN_BACKEND_ENABLED` 包 include |
| `profiling/statistics/statistics_statsmgr.h:249` | `UpdateReTxCount(umq_trans_mode_t)` | `#ifdef` 包（类型见 4.3.1） |
| `profiling/statistics/ubsocket_print_stats_mgr.h:68/233` | `umq_trans_mode_t m_trans_mode` + `UMQ_TRANS_MODE_UB` 默认值 | `#ifdef` 包（urma 用占位类型/值） |
| `cli/cli_terminal_display.cpp:146/441/479/517/555` | `umq_flow_control_stats_to_str`/`umq_info_to_str`/`umq_stats_perf_to_str` 等 dfx（libumq.so C API） | `#ifdef UMQ_DLOPEN_BACKEND_ENABLED` 包 |
| `cli/cli_terminal_display.h:18` + `cli_message.h:67/131/137/143` | `#include umq_dfx_api` + `umq_eid_t`/`umq_flow_control_stats_t`/`umq_info_t` 结构字段 | 见 4.3.1 类型依赖处理 |
| `cli/cli_client.cpp:109-119/190/256` + `cli_main.cpp:36/60/76` | `umq_route_list_t` + `DisplayUmqInfo/DisplayUmqPerfInfo` | `#ifdef` 包（urma 模式 CLI 无 umq 命令） |
| `cli/cli_args_parser.cpp:117-206` | `umqinfo`/`umq` 命令解析 | `#ifdef` 包 |

> 注：`profiling/ubsocket_prof.h` 的 `UMQ_*`/`URMA_*` tracepoint 枚举可共存（仅 id 常量），无需改。

### 4.3.1 libumq.so 解耦边界与类型依赖处理

**解耦范围确认**：libumq.so 也解耦——urma 模式下**不 dlopen / 不 link libumq.so**（运行期脱离），仅 umq 模式依赖 libumq.so。

**DlApi::Load**（§4.2 别名宏已体现）：urma 模式 `LOAD_LIBC | LOAD_URMA`（无 `LOAD_UMQ`），umq 模式 `LOAD_LIBC | LOAD_UMQ`。

**umq 类型依赖（编译期）**：`umq_trans_mode_t`/`umq_eid_t`/`umq_buf_t`/`umq_flow_control_stats_t`/`umq_route_list_t` 等类型来自 `umq_types.h`/`umq_dfx_types.h`（libumq.so 头文件，源码仓 `src/hcom/umq/include/umq/` 自带，不依赖 build libumq.so）。分阶段处理：

| 阶段 | 类型依赖处理 | 效果 |
|------|-------------|------|
| 阶段一 | urma 模式仍 `#include` umq 类型头（源码仓自带，编译期可用），但 `UmqApi` 调用 + `umq_*_to_str` dfx 调用 `#ifdef` 包（不引用 `umq_*` 符号 → 不 link libumq.so） | 编译期依赖 umq 源码头，运行期不依赖 libumq.so |
| 阶段二 | 抽象通用类型替代（`GlobalSetting::UBS_TRANS_MODE` 等），`cli_message.h` umq 类型字段替换为通用类型，消除 umq 头 include | 编译期也脱离 umq |

**阶段一 `cli_message.h` 类型字段**：`CLISocketData`/`CLIUmqInfoData` 等结构含 `umq_flow_control_stats_t` 字段——保留字段（类型头可用），urma 模式填零/不查。CLI 协议结构不变但 umq 字段无效。

### 4.3.2 #ifdef 跳过点 TODO 标注规范

每个 `#ifdef UMQ_DLOPEN_BACKEND_ENABLED` 包起来跳过的功能点，在 `#else` 分支加 `TODO` 注释标注缺失功能 + 后续补全方向，便于阶段二补全追踪。

**TODO 注释模板**：
```cpp
#ifdef UMQ_DLOPEN_BACKEND_ENABLED
    /* umq 模式：原 umq 功能代码 */
    UmqApi::umq_stats_trace_start(&cfg);
#else
    /* TODO(urma-decouple): urma 模式缺失 <功能描述>，待阶段二补 <补全方向> */
#endif
```

**各跳过点缺失功能与补全方向**：

| #ifdef 跳过点 | urma 模式缺失功能 | TODO 补全方向 |
|---|---|---|
| `ubsocket.cpp:215-233/243-255` split trace | umq 通道 split trace 点 | 补 urma trace 等价（urma 有 ProfTracer 体系） |
| `ubsocket.cpp:275-277` EpollRunner 三个 umq runner | umq share-JFR/TP runner | **功能等价已替代**（urma 用 `UrmaPoller`），TODO 标注"已替代" |
| `ubsocket.cpp:195-197/209-214` UMQ_TRANS_MODE | umq trans mode 传 stats/trace | 抽象 `GlobalSetting::UBS_TRANS_MODE`（阶段二） |
| `ubsocket_epoll.cpp:31/100` umq EventPoll | umq EventPoll 实现 | **功能等价已替代**（`UrmaEventPoll`），TODO 标注"已替代" |
| `profiling/probe/probe_manager.*` umq probe | umq buf 探针（RTT 测量） | 补 urma probe 等价（用 urma buf/jetty） |
| `profiling/statistics` umq_stats_tp_perf | umq TP perf 统计 | 补 urma perf 等价（`UrmaPoller` 有 perf 基础） |
| `cli/cli_terminal_display` umq_*_to_str + `cli_args_parser` umqinfo/umq 命令 | CLI 查 umq 路由/信息/性能 | 补 urma CLI 等价命令（urma 有 context/jetty/poller 可查） |
| `core/ubsocket_socket.cpp` 工厂 UMQ 分支（Create/CreateTxOps/RxOps/AcceptorOps/ConnectorOps） | umq socket/ops 创建 | **功能等价已替代**（urma 走 URMA 分支 → `UrmaBackend::Create*`） |
| `core/ubsocket_event_epoll.cpp` EpollRunner new umq ops + AsyncEventPoll UMQ 事件（5 处 RefConvert<UmqSocket>） | umq socket epoll 事件处理 | **功能等价已替代**（urma 用 `UrmaEventPoll`/`UrmaPoller`） |
| `core/ubsocket_tx_cqe_poller.cpp:214` static_cast<UmqTxOps> | umq TX CQE poll | 补 urma tx cqe poll 等价（urma 有 poller 体系） |
| `common/ubsocket_lock.cpp` UmqApi mutex/rwlock register | umq external mutex 注册 | urma 模式跳过（urma 无 external mutex 注册机制，用 ubs 自有锁） |

**分两类**：
- **功能等价无需补**（urma 已有替代）：EpollRunner（→UrmaPoller）、EventPoll（→UrmaEventPoll）—— #ifdef 是切换而非缺失，TODO 标注"已替代，无需补"
- **真正缺失需补**：split trace、probe、TP perf 统计、CLI umq 命令—— TODO 标注缺失功能 + 阶段二补 urma 等价实现

### 4.4 构建脚本

**`build/build_umq_and_ubsocket.sh`**
- `:85-88` 解除强制 `UMQ_BUILD=on`：urma 模式不要求 libumq.so
- 加后端选项透传：
  ```bash
  cmake -S. -Bbuild \
      -DBUILD_URMA_DLOPEN_BACKEND=${URMA_BACKEND:-ON} \
      -DBUILD_UMQ_DLOPEN_BACKEND=${UMQ_BACKEND:-OFF} \
      -DBUILD_WITH_UBS_SHM=${UBSOCKET_BUILD_SHM}
  ```
- urma 模式（`UMQ_BACKEND=OFF`）不传 `UMQ_INCLUDE`/`UMQ_LIB`，不要求 `UMQ_BUILD=on`
- umq 模式（`UMQ_BACKEND=ON`）保持原逻辑（需 libumq.so）

## 5. 实施进度（阶段一）

### 5.1 已完成改动（12 个文件）

| 文件 | 改动 | 状态 |
|------|------|------|
| `src/ubsocket/CMakeLists.txt` | 默认 urma ON/umq OFF + umq_dlopen/urma_dlopen 互斥约束 | ✅ |
| `src/ubsocket/csrc/CMakeLists.txt` | umq GLOB 条件化（`if(BUILD_UMQ_DLOPEN_BACKEND)`） | ✅ |
| `src/ubsocket/csrc/ubsocket.cpp` | 别名宏块 + 6 调用点（init/load/uninit/loglevel/allocator/include）+ split trace/EpollRunner/GlobalStatsMgr/trace/ProbeManager/StopStatsCollection #ifdef 包 + TODO | ✅ |
| `src/ubsocket/csrc/ubsocket_epoll.cpp` | EventPoll 创建 `#ifdef` 切换（urma→UrmaEventPoll, umq→AsyncEventPoll） | ✅ |
| `src/ubsocket/csrc/profiling/probe/probe_manager.h` | 整体 `#ifdef UMQ` 包（header-only，urma 模式空） | ✅ |
| `src/ubsocket/csrc/profiling/statistics/statistics.h` | 整体 `#ifdef UMQ` 包（守卫内） | ✅ |
| `src/ubsocket/csrc/profiling/statistics/statistics.cpp` | 整体 `#ifdef UMQ` 包 | ✅ |
| `src/ubsocket/csrc/profiling/statistics/ubsocket_print_stats_mgr.h` | 整体 `#ifdef UMQ` 包 | ✅ |
| `src/ubsocket/csrc/common/ubsocket_lock.cpp` | `RegisterLockOps`/`RegisterRwLockOps` 的 UmqApi mutex register `#ifdef` 包 | ✅ |
| `src/ubsocket/csrc/core/ubsocket_event_epoll.cpp` | EpollRunner new umq ops（UmqShareJfr/UmqTpTx/UmqTpEvent）`#ifdef` 包 | ✅ |
| `src/ubsocket/csrc/core/ubsocket_socket.cpp` | Create/CreateTxOps/RxOps/AcceptorOps/ConnectorOps 5 处 UMQ 工厂分支 `#ifdef` 包 | ✅ |
| `build/build_umq_and_ubsocket.sh` | 解除强制 + 透传后端选项 | ⏳ 待改 |

### 5.2 剩余工作（1-2 轮迭代）

| 文件 | 改动 | 待解决符号 |
|------|------|-----------|
| `src/ubsocket/csrc/core/ubsocket_event_epoll.cpp` | AsyncEventPoll 的 5 处 `RefConvert<Socket,UmqSocket>` UMQ 事件分支（:591/690/735/766/890）`#ifdef` 包 | UmqSocket::AddTxEvent/DelTxEvent/ShouldRegisterTxEvent/ProcessEpollEvent/GetTxFd + typeinfo |
| `src/ubsocket/csrc/core/ubsocket_tx_cqe_poller.cpp:214` | `static_cast<UmqTxOps*>` `#ifdef` 包 | UmqTxOps::PollTx + vtable |
| `build/build_umq_and_ubsocket.sh` | 解除强制 + 透传后端选项 | — |
| 远端验证 | urma-only 编译通过 + `nm` 无 umq 符号 | — |

### 5.3 实施中发现的新情况

br_urma_rw_0728 分支 umq 引用散布范围**超出原方案预估**——不只 profiling/cli，还深入 core/common 抽象层：

| 层 | 文件 | umq 引用 | 原方案是否覆盖 |
|----|------|---------|--------------|
| core 抽象层 | `ubsocket_socket.cpp` | socket/ops 工厂按 type new UmqSocket/UmqTxOps/UmqConnectorOps/UmqAcceptorOps/UmqRxOps | ❌ 原方案未列 |
| core 抽象层 | `ubsocket_event_epoll.cpp` | EpollRunner new umq ops + AsyncEventPoll 的 UMQ 事件分派（5 处 RefConvert<UmqSocket>） | ❌ 原方案未列 |
| core 抽象层 | `ubsocket_tx_cqe_poller.cpp` | `static_cast<UmqTxOps*>` | ❌ 原方案未列 |
| common 层 | `ubsocket_lock.cpp` | `UmqApi::umq_external_mutex/rwlock_ops_register` | ❌ 原方案未列 |

这些是 core/common 抽象层（urma 模式编入）按 SocketType 分派到 umq 适配层类的代码。urma 模式运行时不走 UMQ 分支（socket type 是 URMA），但编译时引用 UmqSocket/UmqTxOps 等 vtable/typeinfo → 链接 undefined。均需 `#ifdef UMQ_DLOPEN_BACKEND_ENABLED` 包 UMQ 分支。

**迭代验证**：每包一批文件需 scp 到远端 + make 验证，已迭代 6 轮编译，错误从 30+ 降到 6。

## 6. 构建命令与验证

**urma-only（默认）**：
```bash
cmake -S src/ubsocket -B build -DBUILD_URMA_DLOPEN_BACKEND=ON -DBUILD_UMQ_DLOPEN_BACKEND=OFF
cmake --build build -j$(nproc)
```
- 验证：`nm build/libubsocket.so | grep -i umq` 应空（不含 umq 符号）
- 验证：`make VERBOSE=1` 不见 `-I.../umq` 头文件路径
- 运行：dlopen liburma.so 正常（需系统装 liburma.so）

**umq-only（回归）**：
```bash
cmake -S src/ubsocket -B build -DBUILD_URMA_DLOPEN_BACKEND=OFF -DBUILD_UMQ_DLOPEN_BACKEND=ON
cmake --build build -j$(nproc)
```
- 验证：`nm build/libubsocket.so | grep -i urma` 应空
- 验证：现有 umq 用户行为不变

**互斥校验**：
```bash
cmake -S src/ubsocket -B build -DBUILD_URMA_DLOPEN_BACKEND=ON -DBUILD_UMQ_DLOPEN_BACKEND=ON
# 应 FATAL_ERROR
```

## 7. 风险与回滚

- **风险**：urma 模式跳过 umq trace/probe/runner，trace/probe 功能在 urma 模式暂缺（阶段二补 urma 等价实现）。可接受作为阶段一。
- **umq 模式回归**：umq-only 模式应与改动前行为一致（只是从硬编码切回宏选择）。
- **回滚**：改动均为 CMakeLists/脚本文本/源码 #ifdef，`git checkout` 即可恢复。

## 8. 后续阶段（阶段一完成后）

- **阶段二**：抽象 `GlobalSetting::UBS_TRANS_MODE` 等公共抽象，消除 profiling/trace 对 umq setting 的直接依赖；为 urma 补 trace/probe 等价实现
- **阶段三（可选）**：抽 `IUbsocketBackend` 接口，彻底消除 #ifdef

## 9. 预期产物

| 模式 | 产物 | 内容 |
|------|------|------|
| urma-only（默认） | `libubsocket.so` + `libubsocket_static.a` | common + core抽象 + urma 适配层（不含 umq） |
| umq-only | `libubsocket.so` + `libubsocket_static.a` | common + core抽象 + umq 适配层（不含 urma） |

## 10. 附：urma 完全替代 umq 的可行性结论

技术功能上 urma 能完全替代 umq（urma 是 umq 的底层 verbs 重写，已实现 umq 适配层 + libumq.so 联合的全部功能，且设计更优）。本方案是替代的前提——让 urma 适配层能脱离 umq 独立编译。替代推进路径：

1. 本方案（阶段一）→ urma 适配层可独立编译
2. grep `UmqApi` 在 `src/hcom` 确认 libumq.so 是否有 ubsocket 之外的消费者（决定 libumq.so 能否废弃）
3. 逐项功能等价性验证 + 性能对比
4. 灰度：urma/umq 适配层共存，按 socket 类型选择，稳定后下线 umq 适配层
