# URMA 传输层开发现状与待办

> 范围：`src/ubsocket/csrc/core/urma/` 下 `urma_backend.cpp`、`urma_setting.cpp`、`urma_socket.cpp` 三个文件。
> 对照基准：`src/ubsocket/csrc/core/umq/` 下已完整实现的同名/同职能文件。
> 用途：供后续 URMA 传输层快速开发认领与排期。

## 1. 总览

URMA 传输层当前是"骨架就位、肉没填"状态：

| 层次 | 文件 | 现状 |
|------|------|------|
| 底座 - 后端生命周期 | `urma_backend.cpp` / `.h` | 结构骨架完整，实质内容不完整 |
| 底座 - RAII 资源封装 | `urma_wrapper.cpp` / `.h` | **已完整实现**（device/context/jfc/jfs/jfr/jetty 全套） |
| 配置壳 | `urma_setting.cpp` / `.h` | 空骨架，仅 1 条校验规则，Init/LoadEnv 空 |
| socket 主体 | `urma_socket.cpp` / `.h` | 空壳（空类 + 空 namespace） |
| 配套适配层 | 无 | connector/acceptor/tx/rx ops/epoll/errno 等一整套缺失 |

urma 目录现有 5 个文件，umq 同职能有 30+ 文件。除 `urma_wrapper`（已实现）和 `urma_backend`（半成品）外，其余均待开发。

## 2. urma_backend.cpp

**文件**：`src/ubsocket/csrc/core/urma/urma_backend.cpp`（106 行）/ `urma_backend.h`（33 行）
**对照**：`umq_backend.cpp`（460 行）/ `umq_backend.h`（70 行）

### 2.1 现状 - 已实现

| 函数 | 位置 | 已做内容 |
|------|------|---------|
| `Urma::Init` | `urma_backend.cpp:47` | `DlApi::Load(LOAD_URMA)` → `UrmaSetting::Init()`（空调用）→ `UrmaApi::urma_init(&init_attr{})`（配置全空）→ `urma_register_log_func(UrmaLog)` → `urma_log_set_level` → `URMA_INITED=true`。互斥 + 幂等框架在。 |
| `Urma::UnInit` | `urma_backend.cpp:88` | `UrmaApi::urma_uninit()` + `URMA_INITED=false`。 |
| `UrmaLog` | `urma_backend.cpp:20` | urma 库日志回调桥接器，把 `URMA_VLOG_LEVEL_*` 反向映射到 ubs `LogLevel`。**urma 特有，umq 无对应**。 |
| `UrmaGetLogLevel` | `urma_backend.cpp:42` | 反向查当前日志级别供设置 urma 库级别用。 |

### 2.2 待补充（对照 umq_backend::Init 8 步）

| 步骤 | umq 实现 | urma 待做 | 优先级 |
|------|---------|----------|--------|
| ① 配置加载 | `UmqSetting::Init()`（已实现，读 17 个 env） | 依赖 `UrmaSetting::Init()` 真正实现（当前空，见第 3 节） | P0 |
| ② 填初始化配置 | `umq_init_cfg_t` 填 20+ 字段（flow control/credit/block/mem_pool/trans_info/tp_pool） | `urma_init_attr_t init_attr{}`（`:67`）当前零初始化，需按 urma 库要求填 flow control/内存池/传输模式等字段 | P0 |
| ③ 库初始化 | `UmqApi::umq_init(&umq_config)` | `UrmaApi::urma_init(&init_attr)` 已调用（但因 ② 空等于裸初始化） | - |
| ④ 添加设备 | `AddUbDev`/`FindDevName`/`FindDevEid`：找 bonding_dev/udma、设 dev_name+eid、`umq_dev_add` 注册 | **不需补** - urma 库自带设备列表，`UrmaDevice::Init`（wrapper）枚举 + `UrmaContext::CreateContext` 按名查找已覆盖。架构差异，非缺失。 | - |
| ⑤ 链路策略 | 按 `UMQ_IS_BONDING` 设 `LINK_SELECTION_POLICY`(RAW/BONDING_ROUTE/BONDING_BACKUP) | 补 bonding/多链路选择策略（若 urma 支持多设备聚合） | P2 |
| ⑥ NUMA 亲和 | `GetCurrentProcessSocketId` + `GetSocketIdsViaNumaSysfs` 取 CPU socket 供调度 | 补 NUMA socket id 获取（多设备 CPU 亲和调度用） | P2 |
| ⑦ 性能统计 | prof 开启时 `umq_stats_perf_start`/`tp_perf_start`/`perf_reset` | 补 urma perf 统计初始化（视 `UrmaApi` 是否有 `urma_stats_*` 系列函数） | P2 |
| ⑧ bonding 预创建 | `CreateShareMainUmq`+`PrefillShareMainUmq`+`InitShareJfrMonitering`+`TransportPool::WarmUp` | 补 share-JFR 监控 + transport pool 预热（若 urma 需要 share-JFR 机制） | P2 |

### 2.3 其他待补充项

- **`UrmaZeroCopyAllocator`**：umq 在 `umq_backend.h:41` 附带 `MmqZeroCopyAllocator`（继承 `UbsZeroCopyAllocator`，用 `umq_buf_alloc/free`），供 iobuf `UbsZcopyAdapter` 用。urma 侧无对应零拷贝分配器，需新增（用 urma 的 buf 分配 API 实现 `allocate/deallocate`）。优先级 P1。
- **UnInit perf 收尾**：umq `UnInit` 在 prof 开启时调 `umq_stats_perf_stop`/`tp_perf_stop`。urma 若补了 ⑦ perf 初始化，UnInit 也要对应补 stop。随 ⑦ 一起。
- **复制痕迹清理**：`urma_backend.cpp:53` 日志 `"umq already initialized"`、`:94` `"umq not initialized"` 文案为 "umq" 而非 "urma"，从 umq 复制后忘改。低优先级，随手修。

## 3. urma_setting.cpp

**文件**：`src/ubsocket/csrc/core/urma/urma_setting.cpp`（55 行）/ `urma_setting.h`（38 行）
**对照**：`umq_setting.cpp`（339 行）/ `umq_setting.h`（91 行）

### 3.1 现状 - 已实现

| 项 | 位置 | 内容 |
|----|------|------|
| `AddRules` 框架 | `urma_setting.cpp:23` | 三类规则容器（`Int64Rule`/`StrEnumRule`/`StrNotEmptyRule`）注册到 `Validator`，但 int64/enum 数组为空 `{}`，仅 1 条 `StrNotEmptyRule{ENV_URMA_DEV_NAME, true}` |
| 静态成员 | `:18-19` / `.h:27-28` | `UBS_DEV_NAME`、`UBS_DEV_EID`（均 string，全仓无人读取；`UB_DEV_NAME` 2026-10-19 改名，`UB_DEV_EID` 2026-10-21 改名） |
| 环境变量宏 | `:16` | `ENV_URMA_DEV_NAME = "UBS_DEV_NAME"`（2026-10-19 改名，原名 `UBSOCKET_UB_DEV`） |
| 基础设施依赖 | - | 复用 common 层 `Validator`（`ubsocket_setting_validator.h`）+ `GlobalSetting`（`ubsocket_global_setting.h`） |

### 3.2 待补充

| 项 | 对照 umq | 待做 | 优先级 |
|----|---------|------|--------|
| `Init()` 实现 | umq `Init()` 调 `AddRules`+`LoadEnv`+`VerifySetting` 三步（`umq_setting.cpp:249`） | 当前空（`:21`）。填为 `AddRules()` → `LoadEnv()` → （可选）`VerifySetting()` | P0 |
| `LoadEnv()` 实现 | umq 逐项 `GS::GetEnvAndValidate` 读 17 个 env 赋静态成员（`:111-227`） | 当前空返回 `UBS_OK`（`:49`）。用 `GlobalSetting::GetEnvAndValidate` 读 `UBS_DEV_NAME` 赋 `UBS_DEV_NAME`，读 eid 赋 `UBS_DEV_EID` | P0 |
| `UBS_DEV_EID` 解析 | umq `UBSOCKET_SRC_EID` → `UMQ_DEV_SRC_EID_STR` + `inet_pton(AF_INET6,...)` → `umq_eid_t`（`:191`） | `urma_eid_t` 是 16 字节 raw（`urma_wrapper.cpp:696` 格式化印证为 IPv6 地址形态），需 `inet_pton(AF_INET6)` 解析。补 `UBS_DEV_EID` 对应的环境变量宏 + 解析逻辑 | P0 |
| 扩展配置项 | umq 17 个 env（传输模式/队列深度/流控 credit/内存池/block 大小/调度策略/TP 类型/链接优先级等） | 按 urma 需求增量补环境变量宏 + `AddRules` 规则 + `LoadEnv` 读取。建议至少补：传输模式（`URMA_TM_RC`/`URMA_TM_RM` 映射）、队列深度、流控、内存池 | P1 |
| `VerifySetting()` | umq 做跨字段约束（如 `max ≥ init+64M`，`:229`） | 视 urma 配置项间的约束关系，可选补 | P2 |
| `XxxFromStr()` 辅助 | umq `BlockTypeFromStr`/`TransModeFromStr`/`SchedulePolicyFromStr`（`:304-336`） | 补 urma 的字符串转枚举辅助（如 `TransModeFromStr` 把 "RC"/"RM" 转成 `urma_transport_mode_t`） | 随扩展项 |
| 下游消费打通 | umq 的 `UMQ_DEV_NAME` 等被 `UmqBackend`/`UmqSocket` 大量读取 | `UrmaSocket`/`UrmaConnector` 等创建 context 时读 `UrmaSetting::UBS_DEV_NAME` 调 `UrmaContext::CreateContext(devName, eidIndex)`。当前全仓零引用，需接入 | P1 |

## 4. urma_socket.cpp

**文件**：`src/ubsocket/csrc/core/urma/urma_socket.cpp`（18 行，仅 namespace）/ `urma_socket.h`（26 行，`class UrmaSocket : public SocketBase {};` 空类）
**对照**：`umq_socket.h`（284 行，`UmqSocket` 完整子类）/ `umq_socket.cpp`（数百行）

### 4.1 现状 - 已实现

无。`UrmaSocket` 是空类，无任何成员/方法。`.cpp` 只有空的 namespace 块。

### 4.2 待补充 - UrmaSocket 类成员/方法（对照 `UmqSocket`）

| 类别 | UmqSocket 已实现（urma 要照填） | umq 位置 | 优先级 |
|------|-------------------------------|---------|--------|
| 生命周期 | 重写 `Initialize()`/`UnInitialize()` | `umq_socket.h:49-50` | P1 |
| handle 管理 | umq 持有 `uint64_t umq_handle_`/`share_umq_handle_`；`CreateLocalUmq`/`DestroyLocalUmq`/`CreateSubUmq`/`GetOrCreateMainUmq` | `:61-69` `:167-169` `:185-186` `:198-201` | P1（urma 改持有 `UrmaJettyPtr`，见第 5 节） |
| epoll 事件 | `AddTxEvent`/`DelTxEvent`/`ShouldRegisterTxEvent`/`ProcessEpollEvent`/`GetTxFd`、`NewRxEpollIn`/`NewTxEpollIn` | `:131-148` `:160-164` | P1 |
| 绑定状态 | `IsBindRemote`/`SetBindRemote`/`IsBonding`/`SetIsBind` | `:71-89` | P1 |
| 传输属性 | `GetTransMode`/`SetTransMode`、`GetTopoType`/`SetTopoType` | `:91-109` | P1 |
| 版本协商 | `negotiated_version_`/`peer_version_` + getter/setter | `:111-129` `:193-194` | P1 |
| Jetty 资源状态机 | `JettyAllocState{IDLE,WAITING,READY}` + getter/setter | `:29-34` `:150-158` `:205` | P1 |
| 队列管理 | `AddQbuf`/`GetAndPopQbuf`/`FlushRxQueue`/`UpdateRxQueueAvailNum`/`CheckDevAdd` | `:166` `:170-173` | P1 |
| CLI 诊断 | `OutputStats` + 6 个 `GetSocket*Data`（CLISocketData/FlowControl/QbufPool/UmqInfo/IoPacket/UmqPerf） | `:176-182` | P2 |
| 控制面结构体 | `CpMsg`/`NegotiateReq`/`NegotiateRsp`/`NegotiateRoute`/`OtherRouteMessage`（acceptor/connector 共用） | `:210-260` | P1 |

### 4.3 待补充 - 配套文件（urma 目录均缺失）

urma 不仅缺 socket 主体，整条传输适配层配套文件都没有。逐个对照 umq 新建：

| 功能 | umq 对照文件 | urma 待建 | 优先级 |
|------|------------|----------|--------|
| connector | `umq_socket_connector.h/.cpp` | `urma_socket_connector.h/.cpp` | P1 |
| acceptor | `umq_socket_acceptor.h/.cpp` | `urma_socket_acceptor.h/.cpp` | P1 |
| TX ops | `umq_data_tx_ops.h/.cpp` | `urma_data_tx_ops.h/.cpp` | P1 |
| RX ops | `umq_data_rx_ops.h/.cpp` | `urma_data_rx_ops.h/.cpp` | P1 |
| epoll ops | `umq_epoll_ops.h` | `urma_epoll_ops.h` | P1 |
| errno converter | `umq_errno_converter.h/.cpp` | `urma_errno_converter.h/.cpp` | P1 |
| conn helper | `umq_conn_helper.h/.cpp` | `urma_conn_helper.h/.cpp` | P1 |
| buf converter | `umq_buf_converter.h` | `urma_buf_converter.h` | P1 |
| buffer receive queue | `umq_buffer_receive_queue.h/.cpp` | `urma_buffer_receive_queue.h/.cpp` | P1 |
| bounded seq | `umq_bounded_seq.h` | `urma_bounded_seq.h` | P1 |
| eid table | `umq_eid_table.h` | `urma_eid_table.h` | P2 |
| share-jfr epoll runner ops | `umq_share_jfr_epoll_runner_ops.h/.cpp` | `urma_share_jfr_epoll_runner_ops.h/.cpp` | P2 |
| tp tx/event epoll runner ops | `umq_tp_*.h/.cpp`（×4） | `urma_tp_*.h/.cpp` | P2 |
| tx helper | `umq_tx_helper.h/.cpp` | `urma_tx_helper.h/.cpp` | P1 |
| transport pool | `umq_transport_pool.h/.cpp` | `urma_transport_pool.h/.cpp` | P2 |
| tp wait queue | `umq_tp_wait_queue.h/.cpp` | `urma_tp_wait_queue.h/.cpp` | P2 |
| qbuf list | `umq_qbuf_list.h` | `urma_qbuf_list.h` | P1 |

> 注：`umq_errno_converter.h` 是 frozen 文件（不可改），urma 侧是**新建**独立文件，不受此约束。

### 4.4 待补充 - 工厂接入

AGENTS.md 已记录："EpollRunnerFactory 尚未添加 `SOCK_TYPE_URMA` case（当前仅 UMQ）"。`ubsocket_socket.cpp` 的工厂方法也需新增 `SOCK_TYPE_URMA` 分支以创建 `UrmaSocket`。优先级 P1。

## 5. 关键设计差异（urma vs umq，移植时必须注意）

| 维度 | umq | urma | 影响 |
|------|-----|------|------|
| 资源句柄形式 | `uint64_t umq_handle_`（裸句柄），直接调 `umq_*` C API | 持有 `UrmaJettyPtr` + 关联 `UrmaJfcPtr` 等 `Ref`（urma_wrapper 已封装的 RAII 对象） | `UrmaSocket` 析构时引用计数自动释放整条 jetty→jfs/jfr→jfc→context 链，**无需手写** umq 那样的 `DestroyLocalUmq` 显式清理 |
| 设备/context 管理 | umq 无中间封装，backend 层 `AddUbDev`/`FindDevName` 直接操作 | urma 已有 `urma_wrapper`：`UrmaDevice::Init` 枚举、`UrmaContext::CreateContext` 创建+缓存 | urma_socket 建 context 时调 `UrmaContext::CreateContext(devName, eidIndex)` 拿 `UrmaContextPtr`，再 `CreateJfc/Jfs/Jfr/Jetty` 组装端点。无需重复设备查找逻辑 |
| 配置→handle 链 | `UmqSetting::UMQ_DEV_NAME` → `umq_create` 的 dev_name 参数 | `UrmaSetting::UB_DEV_NAME` → `UrmaContext::CreateContext` 的 devName 参数 | 链路一致，仅 API 形态不同（裸 C API vs wrapper 方法） |

**移植建议**：umq 里 `umq_handle_` 相关的显式 create/destroy 逻辑，在 urma 中对应成 `UrmaJettyPtr` 的构造与引用计数释放。`UmqSocket` 的 `CreateLocalUmq`/`DestroyLocalUmq`/`CreateSubUmq`/`GetOrCreateMainUmq` 四个方法，urma 侧可简化为"创建 jetty 时持有 `UrmaJettyPtr`、析构靠引用计数"，方法数量减少。

## 6. 开发优先级建议

```
P0（基础，阻塞后续）：
  1. urma_setting.cpp 补全 Init/LoadEnv/UBS_DEV_EID 解析
     - 让 UrmaSetting::Init() 真正加载配置
  2. urma_backend.cpp Init 配置联动
     - init_attr 填充（flow control/mem pool/trans mode 等）
     - 配合 setting 加载结果
     - 顺手修 "umq" → "urma" 日志文案

P1（传输主体，使 urma socket 可用）：
  3. UrmaSocket 类实现（对照 UmqSocket 成员/方法，handle 改 UrmaJettyPtr）
  4. urma_data_tx_ops / urma_data_rx_ops（收发核心）
  5. urma_socket_connector / urma_socket_acceptor（连接建立）
  6. urma_epoll_ops / urma_errno_converter / urma_conn_helper / urma_buf_converter
  7. urma_buffer_receive_queue / urma_bounded_seq / urma_qbuf_list / urma_tx_helper
  8. 控制面结构体（CpMsg/NegotiateReq/NegotiateRsp 等）+ 版本协商
  9. UrmaZeroCopyAllocator（零拷贝内存接入 iobuf）
  10. 工厂接入（ubsocket_socket.cpp 新增 SOCK_TYPE_URMA、EpollRunnerFactory 新增 case）

P2（高级特性，视需求）：
  11. bonding/多链路策略 + NUMA 亲和
  12. perf 统计（urma_stats_*，视 UrmaApi 是否提供）
  13. share-JFR 监控 / transport pool / tp_wait_queue / eid_table
  14. CLI 诊断数据（GetSocket*Data 系列）
```

## 7. 速查 - urma 目录文件现状

| 文件 | 行数 | 状态 | 对照 umq |
|------|------|------|---------|
| `urma_backend.h` | 33 | 骨架 | umq_backend.h（70 行，多 AddUbDev 等 private + ZeroCopyAllocator） |
| `urma_backend.cpp` | 106 | 半成品 | umq_backend.cpp（460 行） |
| `urma_wrapper.h` | 370 | **完整** | （umq 无对应，umq 直接用裸 C API） |
| `urma_wrapper.cpp` | 804 | **完整** | （同上） |
| `urma_setting.h` | 38 | 空壳 | umq_setting.h（91 行） |
| `urma_setting.cpp` | 55 | 空壳 | umq_setting.cpp（339 行） |
| `urma_socket.h` | 26 | 空壳 | umq_socket.h（284 行） |
| `urma_socket.cpp` | 18 | 空壳 | umq_socket.cpp（数百行） |
| `urma_socket_types.h` | - | 类型定义 | （umq 散见于各头文件） |
