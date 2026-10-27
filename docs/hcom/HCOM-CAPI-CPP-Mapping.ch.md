# HCOM C 接口 / C++ 接口对照文档（服务层 + 组播层）

> **适用范围**：HCOM（`ock::hcom`）**服务层**与**组播层**对外 API 的 **C 版本**
> （`src/hcom/api/capi_v2/hcom_service_c.h`、`hcom_multicast_c.h`）与 **C++ 版本**
> （`src/hcom/service_v2/api/`、`src/hcom/multicast/include/`）逐接口对照。
>
> **不含传输层**：`hcom_c.h` 的传输层接口（`ubs_hcom_driver_*` / `ubs_hcom_ep_*` / `ubs_hcom_mem_allocator_*`）
> 当前无外部使用者，本文档不覆盖（`ubs_hcom_ep_get_id` 等组播断链所需接口除外，见 §5.4）。
>
> **代码基线**：本仓库当前工作副本（已含本轮补齐）。
>
> **文档目的**：
> 1. 给出服务层 / 组播层的 C ↔ C++ **完整映射关系**（含参数）；
> 2. 说明本轮**补齐了什么、有哪些破坏性变更**；
> 3. 汇总调用时的**语义约定与易错点**（时序、引用计数、魔法值）。

---

## 目录

- [0 概览（先看这一页）](#0-概览先看这一页)
  - [0.1 两套接口是什么关系](#01-两套接口是什么关系)
  - [0.2 当前状态一览](#02-当前状态一览)
  - [0.3 本轮变化速览](#03-本轮变化速览)
  - [0.4 怎么查阅本文档](#04-怎么查阅本文档)
- [1 阅读约定](#1-阅读约定)
- [2 变更记录（相对上一版）](#2-变更记录相对上一版)
- [3 服务层对照](#3-服务层对照)
  - [3.1 服务生命周期 `UBSHcomService`](#31-服务生命周期-ubshcomservice)
  - [3.2 服务连接与内存管理](#32-服务连接与内存管理)
  - [3.3 服务回调注册](#33-服务回调注册)
  - [3.4 服务高级配置项](#34-服务高级配置项)
  - [3.5 通道数据操作 `UBSHcomChannel`](#35-通道数据操作-ubshcomchannel)
  - [3.6 通道配置与查询](#36-通道配置与查询)
  - [3.7 服务上下文 `UBSHcomServiceContext`](#37-服务上下文-ubshcomservicecontext)
  - [3.8 枚举与结构体对照](#38-枚举与结构体对照)
- [4 组播层对照](#4-组播层对照)
  - [4.1 发布者服务 `PublisherService`](#41-发布者服务-publisherservice--ubs_hcom_publisher_service_)
  - [4.2 订阅者服务 `SubscriberService`](#42-订阅者服务-subscriberservice--ubs_hcom_subscriber_service_)
  - [4.3 `Publisher` / `Subscriber` / 订阅信息 / 上下文](#43-publisher--subscriber--订阅信息--上下文)
  - [4.4 `MulticastConfig`](#44-multicastconfig--ubs_hcom_multicast_config_)
  - [4.5 组播结构体与枚举](#45-组播结构体与枚举)
- [5 使用约束与陷阱](#5-使用约束与陷阱)
  - [5.1 时序约束](#51-时序约束)
  - [5.2 引用计数约定](#52-引用计数约定)
  - [5.3 语义差异与易错点](#53-语义差异与易错点)
  - [5.4 组播断链链路（本轮已打通）](#54-组播断链链路本轮已打通)
- [6 未处理项与后续建议](#6-未处理项与后续建议)
- [7 附录：C 侧独有接口](#7-附录c-侧独有接口)

---

## 0 概览（先看这一页）

### 0.1 两套接口是什么关系

HCOM 对外提供**两套等价接口**，C 接口是 C++ 接口的**薄适配层**，逐接口一一映射：

| | C 接口（`ubs_hcom_*`） | C++ 接口（`ock::hcom`） |
|---|---|---|
| 头文件 | `src/hcom/api/capi_v2/hcom_service_c.h`、`hcom_multicast_c.h` | `src/hcom/service_v2/api/`、`src/hcom/multicast/include/` |
| 回调 | 函数指针 + `void *usrArg` | `std::function` |
| 句柄 | `uintptr_t`（实为对象指针） | `NetRef<T>` / `T *` |
| 容器/字符串 | 定长数组 + count、`char[]` | `std::vector` / `std::string` |
| 错误 | 函数返回码 + 出参 | 返回 `SerResult` / 直接返回值 |
| 调用链 | `C API → capi_v2 适配层 → C++ 对象` | 原生实现 |

```text
   业务代码
      |  (1) C 风格：ubs_hcom_service_* / ubs_hcom_publisher_* ...
      v
  capi_v2 适配层   <-- 本文档对照的边界（参数/句柄/回调在此转换）
      |
      v
   service_v2 / multicast  (C++ 实现，ock::hcom)
```

因此两套接口**能力等价**。差异只有两类：
1. **形态差异**（如 `std::vector` ↔ "count + index"），不影响能力，仅写法不同；
2. **语义约定**（引用计数、调用时序、魔法值），需要调用方按约定使用。

### 0.2 当前状态一览

| 层次 | C 侧覆盖情况 | 结论 |
|---|---|---|
| **服务层**（service / channel / context） | 本轮补齐 9 项后**无功能缺失** | C++ 对外能力 C 侧均有对应接口 |
| **组播层**（publisher / subscriber / config） | 本就**无接口缺失** | 本轮打通"断链后清理订阅"链路 |

> **一句话结论**
> **服务层与组播层的 C 接口已与 C++ 接口对齐，不存在"C++ 有、C 没有"的功能缺口。**
> 剩余差异只有：数据类型形态、以及需要调用方注意的语义约定（见 §5）。

### 0.3 本轮变化速览

| 类别 | 数量 | 一句话 | 详见 |
|---|---|---|---|
| **修复缺陷** | 3 | 枚举值错配、枚举值缺失、结构体缺字段 | §2.1 |
| **补齐能力** | 9 (+2) | 补齐 C++ 已有但 C 未导出的接口/字段（9 项 P0），另含 2 项枚举补全（P1） | §2.2 |
| **破坏性变更** | 5 | 调用方**必须**同步修改，不向后兼容 | §2.3 |

**破坏性变更速记（最需要对齐的 5 条）**

1. `ubs_hcom_service_set_tls_opt`：参数 **7 → 9**（新增 PSK 两个回调）；
2. `ubs_hcom_service_add_workergroup`：参数 **5 → 6**（新增 `multirailIdx`）；
3. `ubs_hcom_channel_request`：结构体新增 `uint64_t key`；
4. `C_DRIVER_UBC` / `C_SERVICE_UBC`：数值 **6 → 7**（与 C++ `UBC = 7` 对齐）；
5. `ubs_hcom_service_channel_handler`：新增 `const char *ipPort`（第 2 参），回调须同步加参数。

### 0.4 怎么查阅本文档

| 你的问题 | 去看 |
|---|---|
| 某个 C 接口对应哪个 C++ 接口？参数怎么对应？ | [§3 服务层](#3-服务层对照) / [§4 组播层](#4-组播层对照) |
| 本轮改了什么？我要不要改代码？ | [§2 变更记录](#2-变更记录相对上一版) |
| 调用时有什么坑？时序/引用计数怎么处理？ | [§5 使用约束与陷阱](#5-使用约束与陷阱) |
| 还有哪些没对齐/待决策？ | [§6 未处理项](#6-未处理项与后续建议) |
| 哪些接口是 C 独有的？ | [§7 附录](#7-附录c-侧独有接口) |

---

## 1 阅读约定

### 1.1 状态图例

| 标记 | 含义 |
|---|---|
| ✅ | C 与 C++ **一一对应**，语义与参数一致 |
| ⚠️ | **部分对应**：存在参数/语义/形态差异，需注意（详见备注列） |
| ➕ | **C 侧独有**：C 提供的便捷接口，C++ 需组合实现 |
| ⛔ | **陷阱/风险**：可能导致崩溃或误用，见 [§5.3](#53-语义差异与易错点) |

> 说明：本章各表描述的是**当前工作副本**的状态。上一版的 `❌ C 侧缺失` 项已全部补齐，故不再出现。

### 1.2 涉及文件

| 侧 | 文件 | 说明 |
|---|---|---|
| C | `src/hcom/api/capi_v2/hcom_service_c.h` / `hcom_service_c.cpp` | 服务层（service / channel / context） |
| C | `src/hcom/api/capi_v2/hcom_multicast_c.h` / `hcom_multicast_c.cpp` | 组播层 |
| C | `src/hcom/api/capi_v2/hcom_def_inner_c.h` | C→C++ 回调适配器（非对外） |
| C++ | `src/hcom/service_v2/api/hcom_service.h` | `UBSHcomService` |
| C++ | `src/hcom/service_v2/api/hcom_service_channel.h` | `UBSHcomChannel`、`Callback` |
| C++ | `src/hcom/service_v2/api/hcom_service_context.h` | `UBSHcomServiceContext` |
| C++ | `src/hcom/service_v2/api/hcom_service_def.h` | 服务层结构体/枚举 |
| C++ | `src/hcom/multicast/include/multicast_publisher_service.h`、`multicast_subscriber_service.h`、`multicast_publisher.h`、`multicast_subscriber.h`、`multicast_config.h`、`multicast_def.h` | 组播层 |

### 1.3 命名映射规则

| C 前缀 | 对应 C++ |
|---|---|
| `ubs_hcom_service_*` | `UBSHcomService::*` |
| `ubs_hcom_channel_*` | `UBSHcomChannel::*` |
| `ubs_hcom_context_*` | `UBSHcomServiceContext::*` |
| `ubs_hcom_publisher_*` | `Publisher::*` / `PublisherService::*` / `PublisherContext::*` |
| `ubs_hcom_subscriber_*` | `Subscriber::*` / `SubscriberService::*` |
| `ubs_hcom_multicast_config_*` | `MulticastConfig::*` |
| `ubs_hcom_multicast_*` | 组播结构体/回调相关 |

句柄类型：C 侧统一为 `uintptr_t`（如 `ubs_hcom_service`、`ubs_hcom_channel`），本质是 C++ 对象指针。
**部分句柄由适配层内部 `IncreaseRef`**，调用方需用对应的 `disconnect` / `derefer` / `free` 平衡（详见各表备注与 [§5.2](#52-引用计数约定)）。

---

## 2 变更记录（相对上一版）

> 本章记录按上一版第 4/5 章结论所做的**实际代码修改**。修改范围为 `src/hcom/api/capi_v2/` 下的 C 接口头文件与实现及受影响的调用点。

### 2.1 已修复缺陷（E1 ~ E3）

| ID | 问题 | 修复内容 | 涉及文件 |
|---|---|---|---|
| **E1** | `C_DRIVER_UBC` 与 `C_SERVICE_UBC` 值为 `6`，与 C++ `UBSHcomNetDriverProtocol::UBC = 7` **不一致** | 均改为 `7`；补 `C_DRIVER_UNKNOWN = 255`、`C_SERVICE_UNKNOWN = 255`；并把 `ChangeDriverTypeToDriverProto` 中误用的 `case C_SERVICE_UBC:` 改为 `case C_DRIVER_UBC:`（类型误用） | `hcom_c.h`、`hcom_service_c.h`、`hcom_c.cpp` |
| **E2** | `ubs_hcom_service_context_type` 缺 `SER_RNDV_SGL = 6`、`SER_MULTIRAIL_RNDV_RAW = 7`，回调中这两类操作会被判为 `SERVICE_INVALID_OP_TYPE` | 按 C++ 文件级 `enum class Operation` 补齐两项（已有枚举值不变） | `hcom_service_c.h` |
| **E3** | `ubs_hcom_channel_request` 缺 C++ `UBSHcomRequest::key` 字段，RNDV 场景 C 用户无法传 key | 结构体新增 `uint64_t key`，并在 `channel_send` / `channel_call` / `channel_reply` / `channel_call_with_hlc` / `channel_reply_with_hlc` **五处**打通到 `UBSHcomRequest::key` | `hcom_service_c.h`、`hcom_service_c.cpp` |

> **E1 补充**：`docs/hcom/HCOM-API-Spec.md` 与 `docs/zh/ubscomm_api_reference.md` 记载 `C_SERVICE_UBC = 6` 且存在 `C_SERVICE_HSHMEM = 7`。
> 但本仓库代码中**不存在 HSHMEM**，而 C++ 侧 `UBC = 7`。故以**代码为准**把 C 侧对齐为 `7`；
> 这两份 API 参考文档与代码的脱节需单独处理（见 §6）。

### 2.2 已补齐能力（P0 / P1）

| 编号 | 新增/变更 | 对应 C++ | 说明 |
|---|---|---|---|
| P0-1 | `uint64_t ubs_hcom_ep_get_id(ubs_hcom_endpoint ep)` | `UBSHcomNetEndpoint::Id()` | **直接解决组播断链后无法反查订阅的问题**（见 §5.4）；无效 ep 返回 `0` |
| P0-2 | `int ubs_hcom_ep_get_peer_ipport(ubs_hcom_endpoint ep, char *ip, uint32_t *ipLen, uint16_t *port)` | `UBSHcomNetEndpoint::GetPeerIpPort()` | **两段式缓冲区**：`ipLen` 入参容量 / 出参实际长度（不含 `'\0'`） |
| P0-3 | `bool ubs_hcom_context_is_timeout(ubs_hcom_service_context context)` | `UBSHcomServiceContext::IsTimeout()` | 异步/超时场景判断上下文是否已超时 |
| P0-4 | `void ubs_hcom_context_invalidate(ubs_hcom_service_context context)` | `UBSHcomServiceContext::Invalidate()` | 手动失效语义 |
| P0-5 | `ubs_hcom_channel_request::key` | `UBSHcomRequest::key` | 同 E3 |
| P0-6 | `ubs_hcom_service_set_tls_opt(...)` 末尾新增 `pskUseCb` / `pskFindCb` 两个参数，并新增对应 C typedef | `UBSHcomTlsOptions::pskUseCb` / `pskFindCb` | **TLS PSK 模式在 C 服务层恢复可用**；PSK 回调与证书回调同属一个 setter，避免重复调用互相覆盖 |
| P0-7 | `ubs_hcom_service_add_workergroup(...)` 末尾新增 `uint16_t multirailIdx` | `UBSHcomService::AddWorkerGroup` 第 5 个参数 | 多 rail 场景可把 worker group 绑定到指定 rail |
| P0-8 | `ubs_hcom_publisher_context_get_reply_count(ctx)` / `ubs_hcom_publisher_context_get_send_count(ctx)` | `PublisherContext::GetReplyCount()` / `GetSendCount()` | 补齐"已回复数 / 已发送数" |
| P0-9 | `ubs_hcom_service_context_type` 补 `SERVICE_RNDV_SGL` / `SERVICE_MULTIRAIL_RNDV_RAW`（即 E2） | — | 同 E2 |
| P1-8 | `ubs_hcom_service_polling_mode` 补 `C_CLIENT_UNKNOWN = 255` | `UBSHcomClientPollingMode::UNKNOWN` | 仅枚举补全，无行为影响 |
| P1-9 | `ubs_hcom_service_type` / `ubs_hcom_driver_type` 补 `C_SERVICE_UNKNOWN = 255` / `C_DRIVER_UNKNOWN = 255` | — | 表达"未知协议" |

### 2.3 破坏性变更（调用方必须同步修改）

以下变更**不向后兼容**：

| # | 变更 | 影响面 | 适配方式 |
|---|---|---|---|
| 1 | `ubs_hcom_service_set_tls_opt`：**7 → 9** 个参数 | 所有调用点 | 末尾补 `pskUseCb`、`pskFindCb`（不用 PSK 时传 `NULL`）；参考 `test/hcom/tools/capi_demo/capi_demo.c` |
| 2 | `ubs_hcom_service_add_workergroup`：**5 → 6** 个参数 | 所有调用点 | 末尾补 `multirailIdx`（单 rail 场景传 `0`） |
| 3 | `ubs_hcom_channel_request`：新增 `uint64_t key` | 结构体 | 用 `memset` + 字段赋值的代码不受影响；使用**位置初始化**的代码需按新字段顺序适配 |
| 4 | `C_DRIVER_UBC` / `C_SERVICE_UBC`：**6 → 7** | 硬编码 `6` 的代码 | 改为使用枚举常量，勿硬编码数值 |
| 5 | `ubs_hcom_service_channel_handler`（`ubs_hcom_service_bind` / `ubs_hcom_service_register_broken_handler` 的回调）：新增 **`const char *ipPort`（第 2 参）** | 所有实现该回调的调用点；**必须重编**（老二进制与新库混用会导致参数错位） | 回调签名改为 `int (*)(ubs_hcom_channel, const char *ipPort, uint64_t usrCtx, const char *payLoad)`；建链回调取对端 `"ip:port"`，断链回调恒为空串 |

> 说明：变更 1、2 属于"本可另开新接口、但选择改签名"的实现方式；变更 5 为对齐 C++ 建链回调的对端地址语义而主动补充（C++ 侧 `UBSHcomServiceNewChannelHandler` 首参即 `ipPort`）；若后续对兼容性有硬要求，可再评估拆分。

### 2.4 本次未处理（需另行决策）

| 项 | 原因 |
|---|---|
| 组播断链回调的 endpoint **引用语义**（是否自动 `IncreaseRef`） | 自动 refer 会改变所有权模型并要求用户 `derefer`，现有两个官方 C 示例未做平衡会泄漏；本次仅补齐 `ubs_hcom_ep_get_id` / `ubs_hcom_ep_get_peer_ipport`，使回调内可正常使用 |
| 两份 API 参考文档（`HCOM-API-Spec.md`、`ubscomm_api_reference.md`） | 描述的版本含本仓库不存在的 HSHMEM，且签名已落后（见 §6） |

---

## 3 服务层对照

### 3.1 服务生命周期 `UBSHcomService`

| # | C++ 原型（`hcom_service.h`） | C 原型（`hcom_service_c.h`） | 状态 | 备注 |
|---|---|---|---|---|
| 1 | `static UBSHcomService *Create(UBSHcomServiceProtocol t, const std::string &name, const UBSHcomServiceOptions &opt = {})` | `int ubs_hcom_service_create(ubs_hcom_service_type t, const char *name, ubs_hcom_service_options options, ubs_hcom_service *service)` | ✅ | 失败返回 `nullptr` ↔ 返回错误码 |
| 2 | `static int32_t Destroy(const std::string &name)` | `int ubs_hcom_service_destroy(ubs_hcom_service service, const char *name)` | ⚠️ | C++ 仅按 name 销毁，C 需同时传句柄与 name |
| 3 | `virtual int32_t Bind(const std::string &listenerUrl, const UBSHcomServiceNewChannelHandler &handler)` | `int ubs_hcom_service_bind(ubs_hcom_service service, const char *listenerUrl, ubs_hcom_service_channel_handler h)` | ✅ | URL 形式：`tcp://ip:port`、`uds://file:perm`、`ubc://eid:jettyId`；回调 `ipPort`（第 2 参）对应 C++ 首参，`usrCtx` 恒为 0 |
| 4 | `virtual int32_t Start()` | `int ubs_hcom_service_start(ubs_hcom_service service)` | ✅ | 必须先注册好回调再 Start（见 [T1](#51-时序约束)） |

### 3.2 服务连接与内存管理

| # | C++ 原型 | C 原型 | 状态 | 备注 |
|---|---|---|---|---|
| 1 | `virtual int32_t Connect(const std::string &serverUrl, UBSHcomChannelPtr &ch, const UBSHcomConnectOptions &opt = {})` | `int ubs_hcom_service_connect(ubs_hcom_service service, const char *serverUrl, ubs_hcom_channel *channel, ubs_hcom_service_connect_options options)` | ✅ | 内部 `IncreaseRef`（`hcom_service_c.cpp:956`），需 `disconnect` 平衡 |
| 2 | `virtual void Disconnect(const UBSHcomChannelPtr &ch)` | `int ubs_hcom_service_disconnect(ubs_hcom_service service, ubs_hcom_channel channel)` | ⚠️ | C 返回 `int`（C++ 为 void）；内部 `DecreaseRef` |
| 3 | `virtual int32_t RegisterMemoryRegion(uint64_t size, UBSHcomRegMemoryRegion &mr)` | `int ubs_hcom_service_register_memory_region(ubs_hcom_service service, uint64_t size, ubs_hcom_memory_region *mr)` | ✅ | **必须在 `Start()` 之后调用**（见 [T2](#51-时序约束)） |
| 4 | `virtual int32_t RegisterMemoryRegion(uintptr_t address, uint64_t size, UBSHcomRegMemoryRegion &mr)` | `int ubs_hcom_service_register_assign_memory_region(ubs_hcom_service service, uintptr_t address, uint64_t size, ubs_hcom_memory_region *mr)` | ✅ | |
| 5 | `virtual void DestroyMemoryRegion(UBSHcomRegMemoryRegion &mr)` | `int ubs_hcom_service_destroy_memory_region(ubs_hcom_service service, ubs_hcom_memory_region mr)` | ⚠️ | C 返回 `int`（C++ 为 void） |
| 6 | `virtual int32_t ImportUrmaSeg(uintptr_t address, uint64_t size, UBSHcomMemoryKey &key)` | `int ubs_hcom_reg_seg(ubs_hcom_service service, uintptr_t address, uint64_t size, ubs_hcom_oneside_key *key)` | ✅ | `ubs_hcom_oneside_key` ↔ `UBSHcomMemoryKey`；入参携带 `keys`/`eid`，出参回填 `tokens` |
| 7 | `UBSHcomRegMemoryRegion::GetAddress()` / `GetSize()` / `GetMemoryKey(UBSHcomMemoryKey &)` | `int ubs_hcom_service_get_memory_region_info(ubs_hcom_memory_region mr, ubs_hcom_mr_info *info)` | ⚠️ | **该 C 接口无 `service` 参数**；`ubs_hcom_mr_info = {lAddress, lKey(oneside_key), size}` |

### 3.3 服务回调注册

| # | C++ 原型 | C 原型 | 状态 | 备注 |
|---|---|---|---|---|
| 1 | `void RegisterChannelBrokenHandler(const UBSHcomServiceChannelBrokenHandler &handler, const UBSHcomChannelBrokenPolicy policy)` | `void ubs_hcom_service_register_broken_handler(ubs_hcom_service service, ubs_hcom_service_channel_handler h, ubs_hcom_service_channel_policy policy, uint64_t usrCtx)` | ✅ | 回调 `ipPort` 恒为空串（断链时对端地址不可得），见 [§2.3](#23-破坏性变更调用方必须同步修改) 变更 5 |
| 2 | `void RegisterIdleHandler(const UBSHcomServiceIdleHandler &handler)` | `void ubs_hcom_service_register_idle_handler(ubs_hcom_service service, ubs_hcom_service_idle_handler h, uint64_t usrCtx)` | ✅ | |
| 3 | `void RegisterRecvHandler(const UBSHcomServiceRecvHandler &recvHandler)` | `void ubs_hcom_service_register_handler(ubs_hcom_service service, ubs_hcom_service_handler_type t, ubs_hcom_service_request_handler h, uint64_t usrCtx)`，`t = C_SERVICE_REQUEST_RECEIVED` | ✅ | C 用类型参数区分三种 handler |
| 4 | `void RegisterSendHandler(const UBSHcomServiceSendHandler &sendHandler)` | 同上，`t = C_SERVICE_REQUEST_POSTED` | ✅ | |
| 5 | `void RegisterOneSideHandler(const UBSHcomServiceOneSideDoneHandler &oneSideDoneHandler)` | 同上，`t = C_SERVICE_READWRITE_DONE` | ✅ | |
| 6 | `void SetTlsOptions(const UBSHcomTlsOptions &opt)`（结构体，含 `certCb`/`priKeyCb`/`caCb` + `pskUseCb`/`pskFindCb`） | `void ubs_hcom_service_set_tls_opt(ubs_hcom_service service, bool enableTls, ubs_hcom_service_tls_version version, ubs_hcom_service_cipher_suite cipherSuite, ubs_hcom_tls_get_cert_cb certCb, ubs_hcom_tls_get_pk_cb priKeyCb, ubs_hcom_tls_get_ca_cb caCb, ubs_hcom_psk_use_session_cb pskUseCb, ubs_hcom_psk_find_session_cb pskFindCb)` | ⚠️ | **已含 PSK 回调**（破坏性变更 1）。C++ 结构体 ↔ C 参数列表；`enableTls=true` 时证书组与 PSK 组**至少提供一组**，否则调用被忽略 |
| 7 | `void SetConnSecureOpt(const UBSHcomConnSecureOptions &opt)` | `void ubs_hcom_service_set_secure_opt(ubs_hcom_service service, ubs_hcom_service_secure_type secType, ubs_hcom_secinfo_provider provider, ubs_hcom_secinfo_validator validator, uint16_t magic, uint8_t version)` | ⚠️ | **`provider`/`validator` 为 `nullptr` 时整个调用被忽略**（记 ERROR 日志后 return，`hcom_service_c.cpp:1249`），见 [R1](#53-语义差异与易错点) |

> **注意**：C++ `RegisterRecvHandler` 的 `UBSHcomServiceRecvHandler` 接收**非 const 引用**（`UBSHcomServiceContext &`），
> 而 `RegisterSendHandler` / `RegisterOneSideHandler` 接收 **const 引用**（`const UBSHcomServiceContext &`）。
> C 侧统一为 `ubs_hcom_service_request_handler`（`int (*)(ubs_hcom_service_context, uint64_t)`），无 const 差异。

### 3.4 服务高级配置项

| # | C++ 原型 | C 原型 | 状态 | 备注 |
|---|---|---|---|---|
| 1 | `void AddWorkerGroup(uint16_t workerGroupId, uint32_t threadCount, const std::pair<uint32_t,uint32_t> &cpuIdsRange, int8_t priority = 0, uint16_t multirailIdx = 0)` | `void ubs_hcom_service_add_workergroup(ubs_hcom_service service, int8_t priority, uint16_t workerGroupId, uint32_t threadCount, const char *cpuIdsRange, uint16_t multirailIdx)` | ⚠️ | **已含 `multirailIdx`**（破坏性变更 2）；`cpuIdsRange` 由 `std::pair` 变为字符串 `"a-b"`；**C 参数顺序与 C++ 不同** |
| 2 | `void AddListener(const std::string &url, uint16_t workerCount = UINT16_MAX)` | `void ubs_hcom_service_add_listener(ubs_hcom_service service, const char *url, uint16_t workerCount)` | ✅ | |
| 3 | `void SetConnectLBPolicy(UBSHcomServiceLBPolicy lbPolicy)` | `void ubs_hcom_service_set_lbpolicy(ubs_hcom_service service, ubs_hcom_service_lb_policy lbPolicy)` | ✅ | `ROUND_ROBIN=0` / `HASH_IP_PORT=1` 值一致 |
| 4 | `void SetTcpUserTimeOutSec(uint16_t timeOutSec)` | `void ubs_hcom_service_set_tcp_usr_timeout(ubs_hcom_service service, uint16_t timeOutSec)` | ⚠️ | **`65535` 是"不设置"魔法值**（非 65535 秒），见 [R2](#53-语义差异与易错点) |
| 5 | `void SetTcpSendZCopy(bool tcpSendZCopy)` | `void ubs_hcom_service_set_tcp_send_zcopy(ubs_hcom_service service, bool tcpSendZCopy)` | ✅ | |
| 6 | `void SetDeviceIpMask(const std::vector<std::string> &ipMasks)` | `void ubs_hcom_service_set_ipmask(ubs_hcom_service service, const char *ipMask)` | ⚠️ | `vector` ↔ **单值**；语义为**覆盖**（多次调用不追加），见 [R9](#53-语义差异与易错点) |
| 7 | `void SetDeviceIpGroups(const std::vector<std::string> &ipGroups)` | `void ubs_hcom_service_set_ipgroup(ubs_hcom_service service, const char *ipGroup)` | ⚠️ | 同上 |
| 8 | `void SetCompletionQueueDepth(uint16_t depth)` | `void ubs_hcom_service_set_cq_depth(ubs_hcom_service service, uint16_t depth)` | ✅ | |
| 9 | `void SetSendQueueSize(uint32_t sqSize)`（默认 256） | `void ubs_hcom_service_set_sq_size(ubs_hcom_service service, uint32_t sqSize)` | ✅ | |
| 10 | `void SetRecvQueueSize(uint32_t rqSize)`（默认 256） | `void ubs_hcom_service_set_rq_size(ubs_hcom_service service, uint32_t rqSize)` | ✅ | |
| 11 | `void SetQueuePrePostSize(uint32_t prePostSize)`（默认 64） | `void ubs_hcom_service_set_prepost_size(ubs_hcom_service service, uint32_t prePostSize)` | ✅ | |
| 12 | `void SetPollingBatchSize(uint16_t pollSize)`（默认 4） | `void ubs_hcom_service_set_polling_batchsize(ubs_hcom_service service, uint16_t pollSize)` | ✅ | |
| 13 | `void SetEventPollingTimeOutUs(uint16_t pollTimeout)`（默认 500） | `void ubs_hcom_service_set_polling_timeoutus(ubs_hcom_service service, uint16_t pollTimeout)` | ✅ | |
| 14 | `void SetTimeOutDetectionThreadNum(uint32_t threadNum)`（默认 1） | `void ubs_hcom_service_set_timeout_threadnum(ubs_hcom_service service, uint32_t threadNum)` | ✅ | |
| 15 | `void SetMaxConnectionCount(uint32_t maxConnCount)`（默认 250） | `void ubs_hcom_service_set_max_connection_cnt(ubs_hcom_service service, uint32_t maxConnCount)` | ✅ | |
| 16 | `void SetHeartBeatOptions(const UBSHcomHeartBeatOptions &opt)` | `void ubs_hcom_service_set_heartbeat_opt(ubs_hcom_service service, uint16_t idleSec, uint16_t probeTimes, uint16_t intervalSec)` | ✅ | 结构体 ↔ 三参数展开 |
| 17 | `void SetActiveBackup(bool enable)` | `void ubs_hcom_service_set_active_backup(ubs_hcom_service service, bool enable)` | ✅ | |
| 18 | `void SetMultiRailOptions(const UBSHcomMultiRailOptions &opt)` | `void ubs_hcom_service_set_multirail_opt(ubs_hcom_service service, bool enable, uint32_t threshold)` | ✅ | |
| 19 | `void SetUbcMode(UBSHcomUbcMode ubcMode)` | `void ubs_hcom_service_set_ubcmode(ubs_hcom_service service, ubs_hcom_service_ubc_mode ubcMode)` | ✅ | `LOWLATENCY=0` / `HIGHBANDWIDTH=1` |
| 20 | `void SetMaxSendRecvDataCount(uint32_t maxSendRecvDataCount)` | `void ubs_hcom_service_set_max_send_recv_data_cnt(ubs_hcom_service service, uint32_t maxSendRecvDataCount)` | ✅ | |
| 21 | `void SetCtxStoreCapacity(uint32_t ctxStoreCapacity)`（范围 [128, 16777216]，默认 2097152） | `void ubs_hcom_service_set_ctx_store_capacity(ubs_hcom_service service, uint32_t ctxStoreCapacity)` | ✅ | |
| 22 | `void SetUbPriority(uint32_t ubPriority)`（范围 [0, 15]） | `void ubs_hcom_service_set_ub_priority(ubs_hcom_service service, uint32_t ubPriority)` | ✅ | |
| 23 | `void SetTcpEpollMode(bool isTcpEpollLT = false)` | `void ubs_hcom_service_set_tcp_epoll_mode(ubs_hcom_service service, bool isTcpEpollLT)` | ✅ | |
| 24 | `void SetEnableMemPoolThreadCache(bool enable)`（默认 true） | `void ubs_hcom_service_set_enable_mem_pool_thread_cache(ubs_hcom_service service, bool enable)` | ✅ | |
| 25 | `void SetEnableMrCache(bool enableMrCache)`（默认 false，RNDV 需 true） | `void ubs_hcom_service_set_enable_mrcache(ubs_hcom_service service, bool enableMrCache)` | ✅ | |

### 3.5 通道数据操作 `UBSHcomChannel`

C++ 每个数据操作都有"带 `done` 回调（异步）"与"不带 `done`（同步）"两个重载；C 用 `ubs_hcom_channel_callback *cb` 是否为 `NULL` 区分（`cb == NULL` → 同步）。

| # | C++ 原型（`hcom_service_channel.h`） | C 原型 | 状态 | 备注 |
|---|---|---|---|---|
| 1 | `virtual int32_t Send(const UBSHcomRequest &req, const Callback *done)` / `int32_t Send(const UBSHcomRequest &req)` | `int ubs_hcom_channel_send(ubs_hcom_channel channel, ubs_hcom_channel_request req, ubs_hcom_channel_callback *cb)` | ✅ | `ubs_hcom_channel_request = {address, size, key, opcode}`（`key` 见 E3） |
| 2 | `virtual int32_t Call(const UBSHcomRequest &req, UBSHcomResponse &rsp, const Callback *done)` / `int32_t Call(const UBSHcomRequest &req, UBSHcomResponse &rsp)` | `int ubs_hcom_channel_call(ubs_hcom_channel channel, ubs_hcom_channel_request req, ubs_hcom_channel_response *rsp, ubs_hcom_channel_callback *cb)` | ✅ | 同步调用会回填 `rsp->address` / `size` / `errorCode` |
| 3 | `virtual int32_t Reply(const UBSHcomReplyContext &ctx, const UBSHcomRequest &req, const Callback *done)` / `int32_t Reply(ctx, req)` | `int ubs_hcom_channel_reply(ubs_hcom_channel channel, ubs_hcom_channel_request req, ubs_hcom_channel_reply_context ctx, ubs_hcom_channel_callback *cb)` | ✅ | `ubs_hcom_channel_reply_context = {rspCtx, errorCode}` ↔ `UBSHcomReplyContext{rspCtx, errorCode}` |
| 4 | `virtual int32_t CallWithHlc(const UBSHcomRequest &req, UBSHcomResponse &rsp, const Callback *done)` / `int32_t CallWithHlc(req, rsp)` | `int ubs_hcom_channel_call_with_hlc(ubs_hcom_channel channel, ubs_hcom_channel_request req, ubs_hcom_channel_response *rsp, ubs_hcom_channel_callback *cb)` | ✅ | 固定阻塞 + 零拷贝，建议 epoll LT |
| 5 | `virtual int32_t ReplyWithHlc(const UBSHcomReplyContext &ctx, const UBSHcomRequest &req, const Callback *done)` / `int32_t ReplyWithHlc(ctx, req)` | `int ubs_hcom_channel_reply_with_hlc(ubs_hcom_channel channel, ubs_hcom_channel_request req, ubs_hcom_channel_reply_context ctx, ubs_hcom_channel_callback *cb)` | ✅ | |
| 6 | `virtual int32_t Put(const UBSHcomOneSideRequest &req, const Callback *done)` / `int32_t Put(req)` | `int ubs_hcom_channel_put(ubs_hcom_channel channel, ubs_hcom_oneside_request req, ubs_hcom_channel_callback *cb)` | ✅ | `ubs_hcom_oneside_request = {lAddress, rAddress, lKey(oneside_key), rKey(oneside_key), size}` |
| 7 | `virtual int32_t Get(const UBSHcomOneSideRequest &req, const Callback *done)` / `int32_t Get(req)` | `int ubs_hcom_channel_get(ubs_hcom_channel channel, ubs_hcom_oneside_request req, ubs_hcom_channel_callback *cb)` | ✅ | |
| 8 | `virtual int32_t PutV(const UBSHcomOneSideSglRequest &req, const Callback *done)` / `int32_t PutV(req)` | `int ubs_hcom_channel_putv(ubs_hcom_channel channel, ubs_hcom_onesidesgl_request req, ubs_hcom_channel_callback *cb)` | ✅ | `ubs_hcom_onesidesgl_request = {ubs_hcom_oneside_request iov[4], iovCount}`（C++ 为指针 + 计数） |
| 9 | `virtual int32_t GetV(const UBSHcomOneSideSglRequest &req, const Callback *done)` / `int32_t GetV(req)` | `int ubs_hcom_channel_getv(ubs_hcom_channel channel, ubs_hcom_onesidesgl_request req, ubs_hcom_channel_callback *cb)` | ✅ | |
| 10 | `virtual int32_t Recv(const UBSHcomServiceContext &context, uintptr_t address, uint32_t size, const Callback *done = nullptr)` | `int ubs_hcom_channel_recv(ubs_hcom_channel channel, ubs_hcom_service_context ctx, uintptr_t address, uint32_t size, ubs_hcom_channel_callback *cb)` | ✅ | 仅用于接收 RNDV 请求，收到后必须 reply |
| 11 | `virtual int32_t SendFds(int fds[], uint32_t len)` | `int ubs_hcom_channel_send_fds(ubs_hcom_channel channel, int fds[], uint32_t len)` | ✅ | 仅 SHM |
| 12 | `virtual int32_t ReceiveFds(int fds[], uint32_t len, int32_t timeoutSec)` | `int ubs_hcom_channel_recv_fds(ubs_hcom_channel channel, int fds[], uint32_t len, int32_t timeoutSec)` | ✅ | 仅 SHM |

### 3.6 通道配置与查询

| # | C++ 原型 | C 原型 | 状态 | 备注 |
|---|---|---|---|---|
| 1 | `virtual int32_t SetFlowControlConfig(const UBSHcomFlowCtrlOptions &opt)` | `int ubs_hcom_channel_set_flowctl_cfg(ubs_hcom_channel channel, ubs_hcom_flowctl_opts opt)` | ✅ | `HIGH_LEVEL_BLOCK=0`（自旋）/ `LOW_LEVEL_BLOCK=1`（休眠） |
| 2 | `virtual void SetChannelTimeOut(int16_t oneSideTimeout, int16_t twoSideTimeout)` | `void ubs_hcom_channel_set_timeout(ubs_hcom_channel channel, int16_t oneSideTimeout, int16_t twoSideTimeout)` | ✅ | |
| 3 | `virtual int32_t SetTwoSideThreshold(const UBSHcomTwoSideThreshold &threshold)` | `int ubs_hcom_channel_set_twoside_threshold(ubs_hcom_channel channel, ubs_hcom_twoside_threshold threshold)` | ✅ | `{splitThreshold, rndvThreshold}`，默认 `UINT32_MAX`（禁用） |
| 4 | `virtual void SetTraceId(const std::string &traceId)` | `void ubs_hcom_channel_set_trace_id(ubs_hcom_channel channel, const char *traceId)` | ✅ | |
| 5 | `virtual uint64_t GetId()` | `uint64_t ubs_hcom_channel_get_id(ubs_hcom_channel channel)` | ✅ | 无效 channel 返回 0 |
| 6 | `virtual std::string GetPeerConnectPayload()` | `int ubs_hcom_channel_get_peer_connect_payload(ubs_hcom_channel channel, char *payload, uint32_t *len)` | ⚠️ | C 为**两段式缓冲区协议**（`len` 入参容量 / 出参实际长度），不足返回 `SER_RSP_SIZE_TOO_SMALL(508)`，见 [R6](#53-语义差异与易错点) |
| 7 | `virtual int32_t GetRemoteUdsIdInfo(UBSHcomNetUdsIdInfo &idInfo)` | `int ubs_hcom_channel_get_remote_uds_id_info(ubs_hcom_channel channel, ubs_hcom_uds_id_info *idInfo)` | ✅ | 仅 UDS channel 有效 |
| 8 | `virtual void SetUpCtx(uint64_t ctx)` / `virtual uint64_t GetUpCtx()` | `void ubs_hcom_channel_set_up_ctx(ubs_hcom_channel channel, uint64_t ctx)` / `uint64_t ubs_hcom_channel_get_up_ctx(ubs_hcom_channel channel)` | ✅ | |
| 9 | `DEFINE_RDMA_REF_COUNT_FUNCTIONS`（`IncreaseRef` / `DecreaseRef` / `GetRef`） | `void ubs_hcom_channel_refer(ubs_hcom_channel channel)` / `void ubs_hcom_channel_derefer(ubs_hcom_channel channel)` | ✅ | 见 [§5.2 引用计数约定](#52-引用计数约定) |

### 3.7 服务上下文 `UBSHcomServiceContext`

| # | C++ 原型（`hcom_service_context.h`） | C 原型 | 状态 | 备注 |
|---|---|---|---|---|
| 1 | `SerResult Result() const` | `int ubs_hcom_context_get_result(ubs_hcom_service_context context, int *result)` | ✅ | C 返回错误码 + 出参，C++ 直接返回结果值 |
| 2 | `const UBSHcomChannelPtr &Channel() const` | `int ubs_hcom_context_get_channel(ubs_hcom_service_context context, ubs_hcom_channel *channel)` | ⚠️ | 拿到的 channel **未增加引用计数**，仅可在回调内使用，见 [R8](#53-语义差异与易错点) |
| 3 | `Operation OpType() const` | `int ubs_hcom_context_get_type(ubs_hcom_service_context context, ubs_hcom_service_context_type *type)` | ✅ | 枚举已含 `SERVICE_RNDV_SGL` / `SERVICE_MULTIRAIL_RNDV_RAW`（E2） |
| 4 | `uintptr_t RspCtx() const` | `int ubs_hcom_context_get_rspctx(ubs_hcom_service_context context, ubs_hcom_channel_reply_context *rspCtx)` | ✅ | 仅 `SER_RECEIVED` / `SER_RECEIVED_RAW` 可调用 |
| 5 | `uint16_t OpCode() const` | `uint16_t ubs_hcom_context_get_opcode(ubs_hcom_service_context context)` | ✅ | 仅 `SER_SENT` / `SER_RECEIVED` 有效 |
| 6 | `int32_t ErrorCode() const` | `int ubs_hcom_context_get_error_code(ubs_hcom_service_context context, int32_t *errorCode)` | ✅ | |
| 7 | `void *MessageData() const` | `void *ubs_hcom_context_get_data(ubs_hcom_service_context context)` | ✅ | 生命周期**限于回调内**，跨线程需自行拷贝 |
| 8 | `uint32_t MessageDataLen() const` | `uint32_t ubs_hcom_context_get_datalen(ubs_hcom_service_context context)` | ✅ | |
| 9 | `static SerResult Clone(UBSHcomServiceContext &newOne, const UBSHcomServiceContext &oldOne, bool copyData = true)` | `ubs_hcom_service_context ubs_hcom_context_clone(ubs_hcom_service_context context)` / `ubs_hcom_service_context ubs_hcom_context_clone_ex(ubs_hcom_service_context context, bool copyData)` | ✅ | C 返回**堆分配**的新句柄，需 `ubs_hcom_context_free` 释放 |
| 10 | `bool IsTimeout() const` | `bool ubs_hcom_context_is_timeout(ubs_hcom_service_context context)` | ✅ | **本轮补齐（P0-3）** |
| 11 | `void Invalidate()` | `void ubs_hcom_context_invalidate(ubs_hcom_service_context context)` | ✅ | **本轮补齐（P0-4）** |
| 12 | — | `void ubs_hcom_context_free(ubs_hcom_service_context context)` | ➕ | 释放 `clone` / `clone_ex` 产生的上下文 |

### 3.8 枚举与结构体对照

#### 3.8.1 服务类型

| C++ `UBSHcomServiceProtocol`（= `UBSHcomNetDriverProtocol`） | 值 | C `ubs_hcom_service_type` | 值 | 状态 |
|---|---|---|---|---|
| `RDMA` / `TCP` / `UDS` / `SHM` / `UBC` / `UNKNOWN` | 0 / 1 / 2 / 3 / **7** / 255 | `C_SERVICE_RDMA` / `C_SERVICE_TCP` / `C_SERVICE_UDS` / `C_SERVICE_SHM` / `C_SERVICE_UBC` / `C_SERVICE_UNKNOWN` | 0 / 1 / 2 / 3 / **7** / 255 | ✅ 已对齐（E1） |

#### 3.8.2 服务上下文类型

| C++ | 值 | C | 状态 |
|---|---|---|---|
| `UBSHcomServiceContext::Operation`：`SER_RECEIVED=0, SER_RECEIVED_RAW=1, SER_SENT=2, SER_SENT_RAW=3, SER_ONE_SIDE=4, SER_RNDV=5, SER_RNDV_SGL=6, SER_MULTIRAIL_RNDV_RAW=7, SER_INVALID_OP_TYPE=255` | — | `ubs_hcom_service_context_type`：`SERVICE_RECEIVED=0 … SERVICE_RNDV=5, SERVICE_RNDV_SGL=6, SERVICE_MULTIRAIL_RNDV_RAW=7, SERVICE_INVALID_OP_TYPE=255` | ✅ 已对齐（E2） |

#### 3.8.3 通道断开策略 / 轮询模式 / 回调类型 / 流控等级

| C++ | C | 状态 |
|---|---|---|
| `UBSHcomChannelBrokenPolicy`：`BROKEN_ALL=0, RECONNECT=1, KEEP_ALIVE=2` | `ubs_hcom_service_channel_policy`：`C_CHANNEL_BROKEN_ALL=0, C_CHANNEL_RECONNECT=1, C_CHANNEL_KEEP_ALIVE=2` | ✅ |
| `UBSHcomClientPollingMode`：`WORKER_POLL=0, SELF_POLL_BUSY=1, SELF_POLL_EVENT=2, UNKNOWN=255` | `ubs_hcom_service_polling_mode`：`C_CLIENT_WORKER_POLL=0, C_CLIENT_SELF_POLL_BUSY=1, C_CLIENT_SELF_POLL_EVENT=2, C_CLIENT_UNKNOWN=255` | ✅ 已对齐（P1-8） |
| `UBSHcomChannelCallBackType`：`CHANNEL_FUNC_CB=0, CHANNEL_GLOBAL_CB=1` | `ubs_hcom_channel_cb_type`：`C_CHANNEL_FUNC_CB=0, C_CHANNEL_GLOBAL_CB=1` | ✅ |
| `UBSHcomFlowCtrlLevel`：`HIGH_LEVEL_BLOCK=0, LOW_LEVEL_BLOCK=1` | `ubs_hcom_channel_flowctl_level`：同序 | ✅ |
| `UBSHcomWorkerMode`（= `UBSHcomNetDriverWorkingMode`）：`NET_BUSY_POLLING=0, NET_EVENT_POLLING=1` | `ubs_hcom_service_worker_mode`：`C_SERVICE_BUSY_POLLING=0, C_SERVICE_EVENT_POLLING=1` | ✅ |
| `UBSHcomNetDriverSecType`：`NET_SEC_DISABLED=0, NET_SEC_VALID_ONE_WAY=1, NET_SEC_VALID_TWO_WAY=2` | `ubs_hcom_service_secure_type`：`C_SERVICE_NET_SEC_DISABLED=0, C_SERVICE_NET_SEC_ONE_WAY=1, C_SERVICE_NET_SEC_TWO_WAY=2` | ✅ |
| `UBSHcomTlsVersion`：`TLS_1_2=771, TLS_1_3=772` | `ubs_hcom_service_tls_version`：`C_SERVICE_TLS_1_2=771, C_SERVICE_TLS_1_3=772` | ✅ |
| `UBSHcomCipherSuite`：`AES_GCM_128=0 … CHACHA20_POLY1305=3` | `ubs_hcom_service_cipher_suite`：同序 | ✅ |
| `UBSHcomUbcMode`：`LowLatency=0, HighBandwidth=1` | `ubs_hcom_service_ubc_mode`：`C_SERVICE_LOWLATENCY=0, C_SERVICE_HIGHBANDWIDTH=1` | ✅ |
| `UBSHcomServiceHandler` 三类（recv/send/oneSide） | `ubs_hcom_service_handler_type`：`C_SERVICE_REQUEST_RECEIVED=0, C_SERVICE_REQUEST_POSTED=1, C_SERVICE_READWRITE_DONE=2` | ✅ |
| `UBSHcomPeerCertVerifyType`：`VERIFY_BY_NONE=0, VERIFY_BY_DEFAULT=1, VERIFY_BY_CUSTOM_FUNC=2` | `ubs_hcom_peer_cert_verify_type`：同序 | ✅ |
| `UBSHcomServiceLBPolicy`（= `UBSHcomNetDriverLBPolicy`）：`NET_ROUND_ROBIN=0, NET_HASH_IP_PORT=1` | `ubs_hcom_service_lb_policy`：`SERVICE_ROUND_ROBIN=0, SERVICE_HASH_IP_PORT=1` | ✅ |
| 新连接/断链回调类型 | `ubs_hcom_service_channel_handler_type`：`C_CHANNEL_NEW=0, C_CHANNEL_BROKEN=1` | ✅ |

#### 3.8.4 服务选项 `UBSHcomServiceOptions` ↔ `ubs_hcom_service_options`

| C++ 字段 | 类型 | C 字段 | 状态 |
|---|---|---|---|
| `maxSendRecvDataSize` | `uint32_t`（默认 1024） | `maxSendRecvDataSize` | ✅ 0 值回退为 1024 |
| `workerGroupId` | `uint16_t` | `workerGroupId` | ✅ |
| `workerGroupThreadCount` | `uint16_t`（默认 1） | `workerGroupThreadCount` | ✅ 0 值回退为 1 |
| `workerGroupMode` | `UBSHcomWorkerMode` | `workerGroupMode` | ✅ |
| `workerThreadPriority` | `int8_t` | `workerThreadPriority` | ✅ |
| `workerGroupCpuIdsRange` | `std::pair<uint32_t,uint32_t>`（默认 `{UINT32_MAX,UINT32_MAX}`） | `workerGroupCpuRange[64]`（字符串 `"a-b"`） | ⚠️ **格式不同**；空串表示不绑核，见 [R3](#53-语义差异与易错点) |
| `enableMemPoolThreadCache` | `bool`（默认 **true**） | `disableMemPoolThreadCache`（`uint8_t`，**0 = 开启**） | ⚠️ **语义反转**，极易误用，见 [R4](#53-语义差异与易错点) |

#### 3.8.5 连接选项 `UBSHcomConnectOptions` ↔ `ubs_hcom_service_connect_options`

| C++ 字段 | C 字段 | 状态 |
|---|---|---|
| `clientGroupId` / `serverGroupId` | 同名字段 | ✅ |
| `linkCount`（`uint8_t`） | `linkCount` | ✅ |
| `mode`（`UBSHcomClientPollingMode`） | `mode`（`ubs_hcom_service_polling_mode`） | ✅ |
| `cbType`（`UBSHcomChannelCallBackType`） | `cbType`（`ubs_hcom_channel_cb_type`） | ✅ |
| `payload`（`std::string`） | `payLoad[512]`（`char`） | ⚠️ **固定 512 字节，超长静默截断**；解析依赖 `'\0'` 结尾，见 [R5](#53-语义差异与易错点) |

#### 3.8.6 请求/响应/单边/流控结构体

| C++ | C | 状态 |
|---|---|---|
| `UBSHcomRequest`：`{void *address; uint32_t size; uint64_t key; uint16_t opcode;}` | `ubs_hcom_channel_request`：`{void *address; uint32_t size; uint64_t key; uint16_t opcode;}` | ✅ 已对齐（E3） |
| `UBSHcomResponse`：`{void *address; uint32_t size; int16_t errorCode;}` | `ubs_hcom_channel_response`：同名字段 | ✅ |
| `UBSHcomReplyContext`：`{uintptr_t rspCtx; int16_t errorCode;}` | `ubs_hcom_channel_reply_context`：`{void *rspCtx; int16_t errorCode;}` | ⚠️ `rspCtx` 类型不同（`uintptr_t` ↔ `void *`） |
| `UBSHcomMemoryKey`：`{uint64_t keys[4]; uint64_t tokens[4]; uint8_t eid[16];}` | `ubs_hcom_oneside_key`：`{uint64_t keys[4]; uint64_t tokens[4]; uint8_t eid[16];}` | ✅ |
| `UBSHcomOneSideRequest`：`{uintptr_t lAddress; uintptr_t rAddress; UBSHcomMemoryKey lKey; UBSHcomMemoryKey rKey; uint32_t size;}` | `ubs_hcom_oneside_request`：`{void *lAddress; void *rAddress; ubs_hcom_oneside_key lKey; ubs_hcom_oneside_key rKey; uint32_t size;}` | ✅ |
| `UBSHcomOneSideSglRequest`：`{UBSHcomOneSideRequest *iov; uint16_t iovCount;}` | `ubs_hcom_onesidesgl_request`：`{ubs_hcom_oneside_request iov[4]; uint16_t iovCount;}` | ⚠️ 指针 ↔ **定长数组[4]**（`NET_SGE_MAX_IOV=4`） |
| `UBSHcomFlowCtrlOptions`：`{uint16_t intervalTimeMs; uint64_t thresholdByte; UBSHcomFlowCtrlLevel flowCtrlLevel;}` | `ubs_hcom_flowctl_opts`：同名 | ✅ |
| `UBSHcomTwoSideThreshold`：`{uint32_t splitThreshold; uint32_t rndvThreshold;}` | `ubs_hcom_twoside_threshold`：同名 | ✅ |
| `UBSHcomHeartBeatOptions`：`{uint16_t heartBeatIdleSec; uint16_t heartBeatProbeTimes; uint16_t heartBeatProbeIntervalSec;}` | `ubs_hcom_service_set_heartbeat_opt(service, idleSec, probeTimes, intervalSec)` | ✅ 结构体 ↔ 三参数 |
| `UBSHcomNetUdsIdInfo`：`{uint32_t pid; uint32_t uid; uint32_t gid;}` | `ubs_hcom_uds_id_info`：同名 | ✅ |

---

## 4 组播层对照

组播层 C 接口**覆盖度最高，无功能性缺失**；差异集中在两类形态：`std::vector` ↔ 两段式数组、三个独立 TLS 注册接口 ↔ 一个合并接口。

### 4.1 发布者服务 `PublisherService` ↔ `ubs_hcom_publisher_service_*`

| # | C++ 原型（`multicast_publisher_service.h`） | C 原型（`hcom_multicast_c.h`） | 状态 |
|---|---|---|---|
| 1 | `static PublisherService *Create(const std::string &name, const MulticastServiceOptions &opt = {})` | `int ubs_hcom_publisher_service_create(const char *name, const ubs_hcom_multicast_service_options *options, ubs_hcom_publisher_service *service)` | ✅ |
| 2 | `static int32_t Destroy(const std::string &name)` | `int ubs_hcom_publisher_service_destroy(ubs_hcom_publisher_service service, const char *name)` | ✅ |
| 3 | `virtual int32_t Start()` | `int ubs_hcom_publisher_service_start(ubs_hcom_publisher_service service)` | ✅ |
| 4 | `virtual void Stop()` | `void ubs_hcom_publisher_service_stop(ubs_hcom_publisher_service service)` | ✅ |
| 5 | `virtual int32_t CreatePublisher(NetRef<Publisher> &publisher)` | `int ubs_hcom_publisher_service_create_publisher(ubs_hcom_publisher_service service, ubs_hcom_publisher *publisher)` | ✅ |
| 6 | `virtual void DestroyPublisher(NetRef<Publisher> &publisher)` | `void ubs_hcom_publisher_service_destroy_publisher(ubs_hcom_publisher_service service, ubs_hcom_publisher publisher)` | ✅ |
| 7 | `virtual int32_t Bind(const std::string &listenerUrl, const NewSubscriptionHandler &handler, const int cpuId = -1)` | `int ubs_hcom_publisher_service_bind(ubs_hcom_publisher_service service, const char *listenerUrl, ubs_hcom_multicast_new_subscription_handler handler, void *usrArg, int32_t cpuId)` | ✅ C 额外提供 `usrArg` 透传 |
| 8 | `virtual MulticastConfig &GetConfig()` | `ubs_hcom_multicast_config ubs_hcom_publisher_service_get_config(ubs_hcom_publisher_service service)` | ✅ 借用句柄 |
| 9 | `virtual void RegisterSubscriptionExceptionHandler(const SubscriptionExceptionHandler &handler)` | `void ubs_hcom_publisher_service_register_subscription_exception_handler(ubs_hcom_publisher_service service, ubs_hcom_multicast_subscription_exception_handler handler, void *usrArg)` | ✅ |
| 10 | `virtual void RegisterBrokenHandler(const MulticastEpBrokenHandler &handler)` | `void ubs_hcom_publisher_service_register_broken_handler(ubs_hcom_publisher_service service, ubs_hcom_multicast_ep_broken_handler handler, void *usrArg)` | ✅ **本轮打通**：回调内可用 `ubs_hcom_ep_get_id` 反查并清理订阅（见 §5.4） |
| 11 | `virtual void RegisterSendHandler(const MulticastReqPostedHandler &handler)` | `void ubs_hcom_publisher_service_register_send_handler(ubs_hcom_publisher_service service, ubs_hcom_multicast_req_posted_handler handler, void *usrArg)` | ✅ |
| 12 | `virtual void RegisterPubRecvHandler(const MulticastPubReqRecvHandler &handler)` | `void ubs_hcom_publisher_service_register_pub_recv_handler(ubs_hcom_publisher_service service, ubs_hcom_multicast_pub_recv_handler handler, void *usrArg)` | ✅ |
| 13 | `RegisterTLSCaCallback` / `RegisterTLSCertificationCallback` / `RegisterTLSPrivateKeyCallback`（3 个独立注册） | `void ubs_hcom_publisher_service_register_tls_callbacks(ubs_hcom_publisher_service service, ubs_hcom_tls_get_cert_cb certCb, ubs_hcom_tls_get_pk_cb priKeyCb, ubs_hcom_tls_get_ca_cb caCb)` | ⚠️ 合并为一个接口（三回调一次传齐，任一为 `nullptr` 则跳过该回调） |
| 14 | `virtual void AddWorkerGroup(uint16_t workerGroupId, uint32_t threadCount, const std::pair<uint32_t,uint32_t> &cpuIdsRange, int8_t priority)` | `void ubs_hcom_publisher_service_add_workergroup(ubs_hcom_publisher_service service, uint16_t workerGroupId, uint32_t threadCount, uint32_t cpuIdsRangeFirst, uint32_t cpuIdsRangeSecond, int8_t priority)` | ✅ `pair` 拆为两个 `uint32_t` |
| 15 | `virtual SerResult RegisterMemoryRegion(uint64_t size, UBSHcomNetMemoryRegionPtr &mr)` | `int ubs_hcom_publisher_service_register_memory_region(ubs_hcom_publisher_service service, uint64_t size, ubs_hcom_memory_region *mr)` | ✅ |
| 16 | `virtual SerResult RegisterMemoryRegion(uintptr_t address, uint64_t size, UBSHcomNetMemoryRegionPtr &mr)` | `int ubs_hcom_publisher_service_register_assign_memory_region(ubs_hcom_publisher_service service, uintptr_t address, uint64_t size, ubs_hcom_memory_region *mr)` | ✅ |
| 17 | `virtual void DestroyMemoryRegion(UBSHcomNetMemoryRegionPtr &mr)` | `void ubs_hcom_publisher_service_destroy_memory_region(ubs_hcom_publisher_service service, ubs_hcom_memory_region mr)` | ✅ |

### 4.2 订阅者服务 `SubscriberService` ↔ `ubs_hcom_subscriber_service_*`

| # | C++ 原型（`multicast_subscriber_service.h`） | C 原型 | 状态 |
|---|---|---|---|
| 1 | `static SubscriberService *Create(const std::string &name, const MulticastServiceOptions &opt = {})` | `int ubs_hcom_subscriber_service_create(const char *name, const ubs_hcom_multicast_service_options *options, ubs_hcom_subscriber_service *service)` | ✅ |
| 2 | `static int32_t Destroy(const std::string &name)` | `int ubs_hcom_subscriber_service_destroy(ubs_hcom_subscriber_service service, const char *name)` | ✅ |
| 3 | `virtual int32_t Start()` | `int ubs_hcom_subscriber_service_start(ubs_hcom_subscriber_service service)` | ✅ |
| 4 | `virtual void Stop()` | `void ubs_hcom_subscriber_service_stop(ubs_hcom_subscriber_service service)` | ✅ |
| 5 | `virtual int32_t CreateSubscriber(const std::string &serverUrl, NetRef<Subscriber> &subscriber)` | `int ubs_hcom_subscriber_service_create_subscriber(ubs_hcom_subscriber_service service, const char *serverUrl, ubs_hcom_subscriber *subscriber)` | ✅ |
| 6 | `virtual int32_t CreateSubscriber(const std::string &serverUrl, uint16_t publisherGroupIdx, NetRef<Subscriber> &subscriber)` | `int ubs_hcom_subscriber_service_create_subscriber_with_group(ubs_hcom_subscriber_service service, const char *serverUrl, uint16_t publisherGroupIdx, ubs_hcom_subscriber *subscriber)` | ✅ |
| 7 | `virtual void DestroySubscriber(NetRef<Subscriber> &subscriber)` | `void ubs_hcom_subscriber_service_destroy_subscriber(ubs_hcom_subscriber_service service, ubs_hcom_subscriber subscriber)` | ✅ |
| 8 | `virtual MulticastConfig &GetConfig()` | `ubs_hcom_multicast_config ubs_hcom_subscriber_service_get_config(ubs_hcom_subscriber_service service)` | ✅ |
| 9 | `virtual void RegisterBrokenHandler(const MulticastEpBrokenHandler &handler)` | `void ubs_hcom_subscriber_service_register_broken_handler(ubs_hcom_subscriber_service service, ubs_hcom_multicast_ep_broken_handler handler, void *usrArg)` | ✅ 同 4.1 #10，本轮打通 |
| 10 | `virtual void RegisterRecvHandler(const MulticastReqRecvHandler &handler)` | `void ubs_hcom_subscriber_service_register_recv_handler(ubs_hcom_subscriber_service service, ubs_hcom_multicast_req_recv_handler handler, void *usrArg)` | ✅ |
| 11 | TLS 三回调（独立注册） | `void ubs_hcom_subscriber_service_register_tls_callbacks(ubs_hcom_subscriber_service service, ubs_hcom_tls_get_cert_cb certCb, ubs_hcom_tls_get_pk_cb priKeyCb, ubs_hcom_tls_get_ca_cb caCb)` | ⚠️ 合并为一个 |

> **已确认无缺口**：订阅者回包（`ubs_hcom_multicast_context_reply`）**无需注册 MR**，与 C++ 一致——
> `subscriber.cpp:52-57` 用栈上缓冲区直接 `Reply`，不传 lkey；订阅者侧 `SubscriberService` 也没有
> `RegisterMemoryRegion`（C/C++ 一致）。

### 4.3 `Publisher` / `Subscriber` / 订阅信息 / 上下文

| # | C++ 原型 | C 原型 | 状态 | 备注 |
|---|---|---|---|---|
| 1 | `SerResult Publisher::Call(const UBSHcomNetTransOpInfo &opInfo, const MultiRequest &req, const MultiCastCallback *done)` | `int ubs_hcom_publisher_call(ubs_hcom_publisher publisher, ubs_hcom_multicast_op_info opInfo, ubs_hcom_multicast_request req, ubs_hcom_multicast_call_done_handler done, void *usrArg)` | ✅ | `done` 不可为 `nullptr` |
| 2 | `SerResult Publisher::Call(const UBSHcomNetTransOpInfo &opInfo, const std::string &remoteIp, const MultiRequest &req, const MultiCastCallback *done)` | `int ubs_hcom_publisher_call_to(ubs_hcom_publisher publisher, ubs_hcom_multicast_op_info opInfo, const char *remoteIp, ubs_hcom_multicast_request req, ubs_hcom_multicast_call_done_handler done, void *usrArg)` | ✅ | 定向到指定 IP 的订阅者 |
| 3 | `bool Publisher::AddSubscription(SubscriptionInfoPtr &info)` | `bool ubs_hcom_publisher_add_subscription(ubs_hcom_publisher publisher, ubs_hcom_subscription_info info)` | ✅ | 接受订阅返回 true |
| 4 | `bool Publisher::DelSubscription(SubscriptionInfoPtr &info)` | `bool ubs_hcom_publisher_del_subscription(ubs_hcom_publisher publisher, ubs_hcom_subscription_info info)` | ✅ | |
| 5 | `uint32_t Publisher::GetSubscriberNum()` | `uint32_t ubs_hcom_publisher_get_subscriber_num(ubs_hcom_publisher publisher)` | ✅ | |
| 6 | `std::vector<SubscriptionInfoPtr> Publisher::GetAllSubscriberInfo()` | `int ubs_hcom_publisher_get_all_subscriber_info(ubs_hcom_publisher publisher, ubs_hcom_subscription_info *infos, uint32_t *count)` | ⚠️ `vector` ↔ **两段式数组**（`infos = NULL` 时仅查询数量） |
| 7 | `SubscriptionInfoPtr Publisher::GetSubscribeByEpId(uint64_t id)` | `int ubs_hcom_publisher_get_subscribe_by_ep_id(ubs_hcom_publisher publisher, uint64_t epId, ubs_hcom_subscription_info *info)` | ✅ | 未找到时 `*info = 0`；配合 `ubs_hcom_ep_get_id` 使用 |
| 8 | `uint64_t SubscriptionInfo::GetId()` | `uint64_t ubs_hcom_subscription_info_get_id(ubs_hcom_subscription_info info)` | ✅ | |
| 9 | `const std::string &SubscriptionInfo::GetName()` | `const char *ubs_hcom_subscription_info_get_name(ubs_hcom_subscription_info info)` | ✅ | 借用字符串，生命周期随订阅信息 |
| 10 | `const std::string &SubscriptionInfo::GetIp()` | `const char *ubs_hcom_subscription_info_get_ip(ubs_hcom_subscription_info info)` | ✅ | 同上 |
| 11 | `uint16_t SubscriptionInfo::GetPort()` | `uint16_t ubs_hcom_subscription_info_get_port(ubs_hcom_subscription_info info)` | ✅ | |
| 12 | `PublisherContext::GetSubscriberRspInfo()`（返回 `std::vector<SubscriberRspInfo>`） | `uint32_t ubs_hcom_publisher_context_get_subscriber_count(ctx)` + `int ubs_hcom_publisher_context_get_subscriber_rsp_info(ctx, uint32_t index, ubs_hcom_multicast_subscriber_rsp_info *info)` | ⚠️ C 用 **count + index** 两段式 |
| 13 | `PublisherContext::GetReplyCount()` | `uint32_t ubs_hcom_publisher_context_get_reply_count(ubs_hcom_publisher_context ctx)` | ✅ **本轮补齐（P0-8）** |
| 14 | `PublisherContext::GetSendCount()` | `uint32_t ubs_hcom_publisher_context_get_send_count(ubs_hcom_publisher_context ctx)` | ✅ **本轮补齐（P0-8）** |
| 15 | `SubscriberContext::Reply(const MultiRequest &req)` | `int ubs_hcom_multicast_context_reply(ubs_hcom_service_context ctx, ubs_hcom_multicast_request req)` | ✅ | 订阅者收到请求后应答 |
| 16 | `Subscriber::Close()` | `void ubs_hcom_subscriber_close(ubs_hcom_subscriber subscriber)` | ✅ | |
| 17 | `const std::string &Subscriber::GetIp()` | `const char *ubs_hcom_subscriber_get_ip(ubs_hcom_subscriber subscriber)` | ✅ | |
| 18 | `const uint16_t Subscriber::GetPort()` | `uint16_t ubs_hcom_subscriber_get_port(ubs_hcom_subscriber subscriber)` | ✅ | |

### 4.4 `MulticastConfig` ↔ `ubs_hcom_multicast_config_*`

| # | C++ 原型（`multicast_config.h`） | C 原型 | 状态 | 备注 |
|---|---|---|---|---|
| 1 | `virtual bool Init(const std::string &name, const MulticastServiceOptions &opt)` | — | ✅ | C 由 `*_service_create` 内部完成初始化 |
| 2 | `virtual const std::string &GetName() const` | `const char *ubs_hcom_multicast_config_get_name(ubs_hcom_multicast_config config)` | ✅ | |
| 3 | `virtual void SetDeviceIpMask(const std::vector<std::string> &ipMasks)` | `void ubs_hcom_multicast_config_set_device_ip_mask(ubs_hcom_multicast_config config, const char *const *ipMasks, uint32_t count)` | ⚠️ `vector` ↔ 数组 + count |
| 4 | `virtual const std::vector<std::string> &GetDeviceIpMask() const` | `uint32_t ubs_hcom_multicast_config_get_device_ip_mask_count(config)` + `const char *ubs_hcom_multicast_config_get_device_ip_mask(config, uint32_t index)` | ⚠️ `vector` ↔ count + index |
| 5 | `virtual void SetCompletionQueueDepth(uint16_t depth)` | `void ubs_hcom_multicast_config_set_cq_depth(ubs_hcom_multicast_config config, uint16_t depth)` | ✅ |
| 6 | `virtual const uint16_t GetCompletionQueueDepth() const` | `uint16_t ubs_hcom_multicast_config_get_cq_depth(ubs_hcom_multicast_config config)` | ✅ |
| 7 | `virtual void SetSendQueueSize(uint32_t sqSize)` | `void ubs_hcom_multicast_config_set_sq_size(ubs_hcom_multicast_config config, uint32_t sqSize)` | ✅ |
| 8 | `virtual const uint32_t GetSendQueueSize() const` | `uint32_t ubs_hcom_multicast_config_get_sq_size(ubs_hcom_multicast_config config)` | ✅ |
| 9 | `virtual void SetRecvQueueSize(uint32_t rqSize)` | `void ubs_hcom_multicast_config_set_rq_size(ubs_hcom_multicast_config config, uint32_t rqSize)` | ✅ |
| 10 | `virtual const uint32_t GetRecvQueueSize() const` | `uint32_t ubs_hcom_multicast_config_get_rq_size(ubs_hcom_multicast_config config)` | ✅ |
| 11 | `virtual void SetQueuePrePostSize(uint32_t prePostSize)` | `void ubs_hcom_multicast_config_set_prepost_size(ubs_hcom_multicast_config config, uint32_t prePostSize)` | ✅ |
| 12 | `virtual const uint32_t GetQueuePrePostSize() const` | `uint32_t ubs_hcom_multicast_config_get_prepost_size(ubs_hcom_multicast_config config)` | ✅ |
| 13 | `virtual void SetPollingBatchSize(uint16_t pollSize)` | `void ubs_hcom_multicast_config_set_polling_batchsize(ubs_hcom_multicast_config config, uint16_t pollSize)` | ✅ |
| 14 | `virtual const uint16_t GetPollingBatchSize() const` | `uint16_t ubs_hcom_multicast_config_get_polling_batchsize(ubs_hcom_multicast_config config)` | ✅ |
| 15 | `virtual void SetEventPollingTimeOutUs(uint16_t pollTimeout)` | `void ubs_hcom_multicast_config_set_polling_timeoutus(ubs_hcom_multicast_config config, uint16_t pollTimeout)` | ✅ |
| 16 | `virtual const uint16_t GetEventPollingTimeOutUs() const` | `uint16_t ubs_hcom_multicast_config_get_polling_timeoutus(ubs_hcom_multicast_config config)` | ✅ |
| 17 | `virtual void SetHeartBeatOptions(const MulticastHeartBeatOptions &opt)` | `void ubs_hcom_multicast_config_set_heartbeat(ubs_hcom_multicast_config config, ubs_hcom_multicast_heartbeat_options opt)` | ✅ |
| 18 | `virtual const MulticastHeartBeatOptions &GetHeartBeatOptions() const` | `ubs_hcom_multicast_heartbeat_options ubs_hcom_multicast_config_get_heartbeat(ubs_hcom_multicast_config config)` | ✅ |
| 19 | `virtual void SetMaxSendRecvDataCount(uint32_t maxSendRecvDataCount)` | `void ubs_hcom_multicast_config_set_max_send_recv_data_count(ubs_hcom_multicast_config config, uint32_t count)` | ✅ |
| 20 | `virtual const uint32_t GetMaxSendRecvDataCount() const` | `uint32_t ubs_hcom_multicast_config_get_max_send_recv_data_count(ubs_hcom_multicast_config config)` | ✅ |
| 21 | `virtual void SetMaxSendRecvDataSize(uint32_t maxSendRecvDataSize)` | `void ubs_hcom_multicast_config_set_max_send_recv_data_size(ubs_hcom_multicast_config config, uint32_t size)` | ✅ |
| 22 | `virtual const uint32_t GetMaxSendRecvDataSize() const` | `uint32_t ubs_hcom_multicast_config_get_max_send_recv_data_size(ubs_hcom_multicast_config config)` | ✅ |
| 23 | `virtual void SetWorkerGroupInfo(std::vector<UBSHcomWorkerGroupInfo> &info)` | `void ubs_hcom_multicast_config_set_worker_group_info(ubs_hcom_multicast_config config, const ubs_hcom_multicast_worker_group_info *info, uint32_t count)` | ⚠️ `vector` ↔ 数组 + count |
| 24 | `virtual const std::vector<UBSHcomWorkerGroupInfo> &GetWorkerGroupInfo() const` | `uint32_t ubs_hcom_multicast_config_get_worker_group_info_count(config)` + `int ubs_hcom_multicast_config_get_worker_group_info(config, uint32_t index, ubs_hcom_multicast_worker_group_info *info)` | ⚠️ `vector` ↔ count + index |
| 25 | `virtual void SetPeriodicThreadNum(uint32_t threadNum)` | `void ubs_hcom_multicast_config_set_periodic_thread_num(ubs_hcom_multicast_config config, uint32_t threadNum)` | ✅ |
| 26 | `virtual const uint32_t GetPeriodicThreadNum() const` | `uint32_t ubs_hcom_multicast_config_get_periodic_thread_num(ubs_hcom_multicast_config config)` | ✅ |
| 27 | `virtual void SetMaxSubscriberNum(uint32_t maxSubscriberNum)` | `void ubs_hcom_multicast_config_set_max_subscriber_num(ubs_hcom_multicast_config config, uint32_t maxSubscriberNum)` | ✅ |
| 28 | `virtual const uint32_t GetMaxSubscriberNum() const` | `uint32_t ubs_hcom_multicast_config_get_max_subscriber_num(ubs_hcom_multicast_config config)` | ✅ |
| 29 | `virtual void SetPeriodicCpuId(int cpuId)` | `void ubs_hcom_multicast_config_set_periodic_cpu_id(ubs_hcom_multicast_config config, int32_t cpuId)` | ✅ |
| 30 | `virtual const int GetPeriodicCpuId() const` | `int32_t ubs_hcom_multicast_config_get_periodic_cpu_id(ubs_hcom_multicast_config config)` | ✅ |
| 31 | `virtual UBSHcomNetDriverProtocol GetProtocol() const` | `ubs_hcom_multicast_protocol ubs_hcom_multicast_config_get_protocol(ubs_hcom_multicast_config config)` | ⚠️ C 用独立枚举 `ubs_hcom_multicast_protocol`（`C_MULTICAST_RDMA=0, C_MULTICAST_TCP=1, C_MULTICAST_UNKNOWN=255`） |

> `ubs_hcom_multicast_config` 是**借用句柄**（由 `*_service_get_config` 返回），随 service 生命周期有效，**不可销毁**。

### 4.5 组播结构体与枚举

| C++ | C | 状态 |
|---|---|---|
| `MulticastServiceOptions`（20 字段，`workerGroupCpuIdsRange` 为 `std::pair`） | `ubs_hcom_multicast_service_options`（20 字段，`workerGroupCpuIdsRangeFirst` + `workerGroupCpuIdsRangeSecond`） | ✅ 仅 `pair` 拆分差异 |
| `MulticastHeartBeatOptions`：`{heartBeatIdleSec; heartBeatProbeTimes; heartBeatProbeIntervalSec;}` | `ubs_hcom_multicast_heartbeat_options`：同名 | ✅ |
| `UBSHcomWorkerGroupInfo`：`{int8_t threadPriority; uint16_t threadCount; uint16_t groupId; std::pair<uint32_t,uint32_t> cpuIdsRange;}` | `ubs_hcom_multicast_worker_group_info`：`{int8_t threadPriority; uint16_t threadCount; uint16_t groupId; uint32_t cpuIdsRangeFirst; uint32_t cpuIdsRangeSecond;}` | ✅ |
| `MultiRequest`：`{void *data; uint32_t size; uint64_t lkey;}` | `ubs_hcom_multicast_request`：`{void *data; uint32_t size; uint64_t lkey;}` | ✅ |
| `MultiResponse`：`{void *data; uint32_t size;}` | `ubs_hcom_multicast_response`：`{void *data; uint32_t size;}` | ✅ |
| `UBSHcomNetTransOpInfo`：`{uint32_t seqNo; int16_t timeout; int16_t errorCode; uint8_t flags;}` | `ubs_hcom_multicast_op_info`：同名 | ✅ |
| `SubscriberRspStatus`：`SUCCESS=0, INIT=1, SEND_ERROR=2, TIMEOUT=3, BROKEN=4, UNKNOWN_ERROR=5` | `ubs_hcom_multicast_subscriber_rsp_status`：`C_MULTICAST_SUB_RSP_SUCCESS=0 … C_MULTICAST_SUB_RSP_UNKNOWN_ERROR=5` | ✅ |
| `SubscriberRspInfo`（`GetSubInfos()` / `GetStatus()` / `GetMultiResponse()`） | `ubs_hcom_multicast_subscriber_rsp_info`：`{subInfo; status; rspData; rspSize;}` | ✅ |
| `UBSHcomNetDriverProtocol`：`RDMA=0, TCP=1` | `ubs_hcom_multicast_protocol`：`C_MULTICAST_RDMA=0, C_MULTICAST_TCP=1, C_MULTICAST_UNKNOWN=255` | ⚠️ 独立枚举，值一致 |

**组播 `MulticastServiceOptions` 零值语义**（C 头文件注释："fields with a non-zero default fall back to the default when set to 0"）：

| 字段 | 传 0 时的行为 |
|---|---|
| `maxSendRecvDataSize` / `maxSendRecvDataCount` / `multicastIoContextCount` | 回退为 C++ 默认值（1024 / 8192 / 8192） |
| `workerGroupThreadCount` | 回退为 1 |
| `qpSendQueueSize` / `qpRecvQueueSize` / `qpPrePostSize` / `qpBatchRePostSize` | 回退为 C++ 默认值（1024 / 1024 / 256 / 10） |
| `completionQueueDepth` / `maxSubscriberNum` | 回退为 2048 / 7 |
| `workerGroupCpuIdsRangeFirst` + `Second` | 两者均为 0 时不绑核 |
| `workerGroupId` / `workerThreadPriority` / `publisherWrkGroupNo` / `periodicCpuId` | **按原值使用**（0 就是 0） |
| `enableTls` | ⛔ **按原值使用**：`memset(0)` 后为 `false`（**C 侧默认关闭 TLS**，与 C++ 默认 `true` 不同），见 [R13](#53-语义差异与易错点) |
| `protocol` | `0 = C_MULTICAST_RDMA`（与 C++ 默认 RDMA 一致） |

---

## 5 使用约束与陷阱

### 5.1 时序约束

| ID | 约束 | 依据 | 说明 |
|---|---|---|---|
| **T1** | **回调必须先注册、再 `ubs_hcom_service_start()`** | `service_imp.cpp` CreateService 流程；`perf_test/test_case/service_v2/service_helper.cpp:110-128` | 若在 `Start()` 后注册，早期连接/请求会丢失对应回调 |
| **T2** | **MR 注册必须在 `ubs_hcom_service_start()` 之后** | `service_imp.cpp:839-842`：`mDriverPtrs` 在 `Start()` 时创建，Start 前注册 MR 返回 `NN_ERROR` | 导致"服务端注册 MR → 客户端交换 key → 单边写"流程中，**client 首次请求可能早于 server MR 就绪**；C 侧**无"服务端就绪"通知接口**，业务须自行重试等待 |
| **T6** | 单边 put/get 仅 **rdma / ubc** 支持 | `hcom_service_c.h` 中 `ubs_hcom_channel_recv` 注释 | TCP / UDS / SHM 协议下调用 `channel_put/get/putv/getv` 不被支持 |
| **T7** | `*_service_create` 的名字长度上限 64 字节 | `hcom_service_c.cpp:867-868`（`strlen(name) > NN_NO64` 报错） | 组播 service 的 name 同样受此约束 |

**典型调用顺序（服务端）**

```text
Create → 注册全部回调（recv/send/oneSide/broken/idle + 可选 TLS/secure）
       → Start
       → RegisterMemoryRegion / ImportUrmaSeg   (必须在 Start 之后)
       → Bind / AddListener
```

**典型调用顺序（客户端）**

```text
Create → 注册回调 → Start → Connect → (同步/异步) Send / Call / Put / Get
```

### 5.2 引用计数约定

| ID | 约定 | 依据 |
|---|---|---|
| **T3** | **服务端 NewChannel 回调内的 channel 已 `IncreaseRef`** | `hcom_def_inner_c.h:415`（`newCh->IncreaseRef()`）——server 侧结束前必须 `ubs_hcom_channel_derefer` 平衡，否则句柄泄漏 |
| **T4** | **`ubs_hcom_service_connect` 返回的 channel 已 `IncreaseRef`** | `hcom_service_c.cpp:956`——需 `ubs_hcom_service_disconnect`（内部 `DecreaseRef`）平衡 |
| **R8** | **`ubs_hcom_context_get_channel` 返回的 channel 未 `IncreaseRef`** | 仅在当前回调内有效；跨线程使用必须自行 `ubs_hcom_channel_refer` / `derefer` |
| **T5** | 组播 `ubs_hcom_multicast_config` 为**借用句柄** | `hcom_multicast_c.h` 注释——随 service 生命周期有效，**不可销毁**；订阅信息 / 发布者上下文的句柄同样是借用语义（仅在回调内有效） |

### 5.3 语义差异与易错点

| ID | 内容 | 说明 | 建议 |
|---|---|---|---|
| **R1** | `ubs_hcom_service_set_secure_opt` | 当 `provider` 或 `validator` 为 `nullptr` 时**整个调用被忽略**（记 ERROR 日志后 return，`hcom_service_c.cpp:1249-1252`），不返回错误。只想设置 `magic`/`version` 会被无声忽略 | 文档明确；或对非法组合返回错误码 |
| **R2** | `ubs_hcom_service_set_tcp_usr_timeout(service, uint16_t timeOutSec)` | 该值最终写入驱动 `int16_t tcpUserTimeout`。C++ `HcomServiceImp::mOptions.tcpTimeOutSec` 默认 `-1`（存入 `uint16_t` = **65535**），传驱动时回落为 -1 表示"不设置"。故 **65535 是"不设置"魔法值，而非 65535 秒** | 文档标注 `65535 = 不设置`；或提供显式宏 |
| **R3** | `ubs_hcom_service_options::workerGroupCpuRange[64]` | 格式 `"a-b"`（如 `"6-10"`），**空串表示不绑核**（保留 `{UINT32_MAX,UINT32_MAX}`）；而 `ubs_hcom_service_add_workergroup` 的 `cpuIdsRange` **不允许空串**。同概念两处规则不同 | 文档分别说明；建议统一 |
| **R4** | `ubs_hcom_service_options::disableMemPoolThreadCache` | C 用**取反**语义表达 C++ 的 `enableMemPoolThreadCache`（**0 = 开启缓存**）。`memset(0)` 后恰好等于"开启"（与 C++ 默认一致），但阅读代码极易误解 | 文档明确；或改名与 C++ 一致 |
| **R5** | `ubs_hcom_service_connect_options::payLoad[512]` | 固定 512 字节，C++ 为 `std::string`（无长度限制）。超长 payload **静默截断**；解析依赖 `'\0'` 结尾（`NN_CHAR_ARRAY_TO_STRING`） | 文档标注上限；调用方保证 `'\0'` 结尾 |
| **R6** | `ubs_hcom_channel_get_peer_connect_payload` | 两段式协议：`len` 入参为缓冲区容量、出参为实际长度（不含 `'\0'`）；容量不足返回 `SER_RSP_SIZE_TOO_SMALL(508)`，此时 `len` 回填所需容量。首次可传 `payload = NULL` 只取长度 | 文档明确调用范式 |
| **R7** | `ubs_hcom_channel_reply_context::rspCtx` | C 为 `void *`，C++ `UBSHcomReplyContext::rspCtx` 为 `uintptr_t`。C 侧实现内部做了 `reinterpret_cast`（`hcom_service_c.cpp:758`），功能等价但类型不严谨 | 无（仅形态差异） |
| **R9** | `ubs_hcom_service_set_ipmask` / `set_ipgroup` | C 为**单值**入参，C++ 接受 `std::vector<std::string>`。C++ 实现为整体赋值，即**多次调用 C 接口是覆盖而非追加**（头文件未说明） | 文档明确"覆盖"语义 |
| **R10** | `ubs_hcom_service_destroy` / `ubs_hcom_service_disconnect` / `ubs_hcom_service_destroy_memory_region` | C 返回 `int`，对应 C++ 接口返回 `void`（`Destroy` 返回 `int32_t`）；C 侧成功恒返回 `SER_OK`，**失败信息有限** | 无（仅形态差异） |
| **R11** | C 侧 `ubs_hcom_service_connect` 的 `options` 传值 | `ubs_hcom_service_connect_options` 按**值**传递（含 512 字节 `payLoad`），每次调用都有一次结构体拷贝 | 无（可接受）；若关注性能可改为指针 |
| **R12** | ⛔ **高危陷阱**：把组播回调里的 `ubs_hcom_endpoint` 误传给 `ubs_hcom_channel_get_id` | 两个句柄类型不通用。`ubs_hcom_channel_get_id` 仅做非零校验后 `reinterpret_cast<UBSHcomChannel *>(channel)->GetId()`，而 `GetId()` 是**纯虚函数** → 对 endpoint 对象做虚表调用属**未定义行为（通常崩溃或返回垃圾）**。**本轮已补 `ubs_hcom_ep_get_id` 消除该诱因** | 明确 ep 与 channel 句柄**不可混用** |
| **R13** | ⛔ 组播 `options.enableTls` 零值语义 | C 侧 `memset(0)` → `enableTls = false`（**默认关闭 TLS**）；C++ `MulticastServiceOptions::enableTls` 默认 **true**。C 侧不做"非零默认回退"，按原值直传（`hcom_multicast_c.cpp:155`） | 若不打算关闭 TLS，必须显式 `options.enableTls = true` |

### 5.4 组播断链链路（本轮已打通）

**C++ 的标准做法**（`test/hcom/tools/multicast_perf/publisher.cpp:67-73`）：

```cpp
void PublisherSubscriberEpBroken(const ock::hcom::UBSHcomNetEndpointPtr &ep)
{
    NN_LOG_INFO("publisher ep broken id " << ep->Id());
    auto info = g_publisher->GetSubscribeByEpId(ep->Id());  // 需要 ep->Id()
    g_publisher->DelSubscription(info);                     // 清理该订阅
    g_isBroken = true;
}
```

**C 侧等价写法（本轮补齐后可用）**：

```c
static void PublisherSubscriberEpBroken(ubs_hcom_endpoint ep, void *usrArg)
{
    (void)usrArg;
    uint64_t epId = ubs_hcom_ep_get_id(ep);              /* 本轮补齐（P0-1） */
    if (epId == 0) {
        return;
    }
    ubs_hcom_subscription_info info = 0;
    if (ubs_hcom_publisher_get_subscribe_by_ep_id(g_publisher, epId, &info) == 0 && info != 0) {
        (void)ubs_hcom_publisher_del_subscription(g_publisher, info);  /* 清理订阅 */
    }
    g_isBroken = true;
}
```

**本轮补齐前的问题（已消除）**：

| C 试图做的事 | 补齐前 | 现在 |
|---|---|---|
| 取 ep id（用于 `get_subscribe_by_ep_id`） | ❌ 无 `ubs_hcom_ep_get_id` | ✅ `ubs_hcom_ep_get_id` |
| 取对端 ip:port | ❌ `UBSHcomNetEndpoint::GetPeerIpPort()` 未导出 | ✅ `ubs_hcom_ep_get_peer_ipport` |
| 取对端 payload | ❌ `PeerConnectPayload()` 未导出 | ⚠️ 仍未导出（非必需） |
| 取 worker 索引 | ⚠️ 有 `ubs_hcom_ep_get_worker_idx` / `..._workergroup_idx`，但与订阅无对应关系 | ⚠️ 不变 |

**连带影响的接口**（现在均已可用）：

1. `ubs_hcom_publisher_get_subscribe_by_ep_id(publisher, epId, ...)` —— 不再因拿不到 `epId` 而成为死接口；
2. `ubs_hcom_publisher_del_subscription` —— 可在断链路径触达，消除"订阅者断开后订阅列表残留、
   `get_subscriber_num` 虚高、`call` 仍向已断开订阅者发送"的问题。

> **遗留**：两个官方 C 示例仍使用旧的规避写法（`publisher_c.c` 只置 `g_isBroken = true`；
> `subscriber_c.c` 注释 "C API does not expose the peer ip/port of an endpoint"），**示例尚未更新**；
> 该注释在当前版本已过期。

---

## 6 未处理项与后续建议

| 项 | 说明 | 建议动作 |
|---|---|---|
| 两份 API 参考文档落后 | `docs/hcom/HCOM-API-Spec.md` 的 `ubs_hcom_service_set_tls_opt` / `add_workergroup` 仍是旧签名（§2.3 的破坏性变更未同步），且记载了本仓库不存在的 `C_SERVICE_HSHMEM` | 按当前代码更新，或注明"以代码为准" |
| 官方 C 示例未更新 | 组播断链示例仍为旧规避写法；`capi_demo.c` 已更新到新签名 | 更新 `publisher_c.c` / `subscriber_c.c` 使用 `ubs_hcom_ep_get_id` |
| 组播断链 endpoint 引用语义 | 是否在回调内自动 `IncreaseRef`（当前不自动） | 维持现状（文档说明），避免改变所有权模型 |
| `PeerConnectPayload` 未导出到 C | 断链回调内无法取对端 payload | 视需求决定是否补齐；非必需 |
| `hcom_service_c.h` 中 `ubs_hcom_ep_*` 的归属 | `ubs_hcom_ep_get_id` / `_get_peer_ipport` 属传输层接口（放在 `hcom_c.h`），因组播断链需要而存在 | 保持现状；本文档因组播链路需要而覆盖这两个接口 |

---

## 7 附录：C 侧独有接口

以下接口在 C 侧提供，C++ 侧需组合实现或不存在对应物：

| # | C 接口 | 说明 |
|---|---|---|
| 1 | `ubs_hcom_context_free(ctx)` | 释放 `ubs_hcom_context_clone` / `clone_ex` 产生的上下文；C++ 由对象析构/`NetRef` 管理 |
| 2 | `ubs_hcom_channel_refer(channel)` / `ubs_hcom_channel_derefer(channel)` | C++ 通过 `NetRef` 与 `UBSHcomChannelPtr` 管理引用计数，C 显式暴露 |
| 3 | `ubs_hcom_set_log_handler(ubs_hcom_log_handler h)` | C 为函数指针式日志回调；C++ 使用 `UBSHcomNetOutLogger` 类。**该声明同时出现在 `hcom_c.h:540` 与 `hcom_service_c.h:1127`**（重复声明，实现仅一处：`hcom_c.cpp:288`） |
| 4 | `ubs_hcom_err_str(int16_t errCode)` | 等价 C++ 自由函数 `UBSHcomNetErrStr`（非类成员），C 侧在多处错误码注释中被引用 |
| 5 | `ubs_hcom_multicast_context_reply(ctx, req)` | 组播订阅者应答；C++ 为 `SubscriberContext::Reply(req)`（需先构造 `SubscriberContext`） |
| 6 | `ubs_hcom_publisher_context_get_subscriber_count` + `_get_subscriber_rsp_info(index)` | C 的两段式遍历；C++ 为 `PublisherContext::GetSubscriberRspInfo()` 返回 `vector` |
| 7 | `ubs_hcom_publisher_get_all_subscriber_info(publisher, infos, &count)` | 两段式查询（`infos = NULL` 取数量）；C++ 返回 `vector` |
| 8 | 组播各 `*_destroy_*` 接口（`destroy_publisher` / `destroy_subscriber` / `destroy_memory_region`） | C 侧显式销毁；C++ 通过 `NetRef` 析构 + 引用计数自动管理 |

---

## 8 核对方法

本文档所有结论均基于以下方式核对：

1. **声明比对**：`hcom_service_c.h` / `hcom_multicast_c.h` 的全部函数声明 ↔ `service_v2/api/*.h` / `multicast/include/*.h` 的公开成员与自由函数；
2. **实现核对**：`hcom_service_c.cpp` / `hcom_multicast_c.cpp` 的参数转换、默认值回退与引用计数处理（含 `IncreaseRef` / `DecreaseRef` 位置）；
3. **结构体逐字段比对**：C 结构体 ↔ C++ 结构体/类字段（含类型、默认值与零值回退规则）；
4. **枚举逐值比对**：C 枚举 ↔ C++ 枚举的数值与语义。

> 若后续修改 `src/hcom/api/capi_v2/hcom_service_c.*` / `hcom_multicast_c.*` 或对应的 C++ 对外头文件，请同步更新本文档。
