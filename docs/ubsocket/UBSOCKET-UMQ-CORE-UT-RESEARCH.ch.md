# UBSocket UMQ Core UT 前期调研

> 本文件是 `csrc/core/umq/` 下**除 `umq_socket*` 与 `*_epoll_runner_ops*` 之外**文件的 UT 补充工作调研报告。
> 全部论断基于源码一级资料,标注 `文件路径:行号`。覆盖率数字来源与日期见 [§5 覆盖率汇总](#5-覆盖率汇总)。
> 政策: 每模块行 ≥80% / 分支 ≥50%(`docs/adr/0001-coverage-policy-per-module-80-50-no-cap.md`)。

**刷新记录(2026-08-28)**: 目标范围内 10 个 .cpp **已全部达标,本轮为收官刷新**。
- 新增 7 个 umq 测试 target: 覆盖目标范围的 `umq_tx_helper_test`/`umq_data_rx_ops_test`/`umq_data_tx_ops_test`/`umq_transport_pool_test`/`umq_backend_test`,以及范围外的 `umq_tp_event_epoll_runner_ops_test`/`umq_tp_tx_epoll_runner_ops_test`(见 §2.1)。
- 认领状态(以 `UBSOCKET-CLAIMING.md` 为准,其 2026-08-28 刷新): 目标范围 9 个可认领 .cpp 全部 `done`(`umq_backend.cpp` 2026-08-28 流转 done);范围外 epoll_runner_ops 两个也已 done。§6.1 已整体对齐为"已完成"记录表。
- 覆盖率数字来源: `UBSOCKET-COVERAGE-ANALYSIS.ch.md`(快照 2026-08-28,mtime 10:38)+ `build/coverage_filtered.info`(mtime 2026-08-28 10:36),两来源完全一致;实测 `ctest` 47/47 全绿(2026-08-28,见 §5.1)。全局: 行 52.6%(7181/13661)、函数 69.4%(1108/1597)、分支 38.8%(5836/15040)。
- 源码变化(相对上次快照): `umq_data_rx_ops.h`/`umq_data_tx_ops.h` 为纯类型别名(`using UmqRxOps = ::ock::ubs::DataRxOps` / `using UmqTxOps = ::ock::ubs::DataTxOps`,30dfed7f"v1.7 去虚化合并"时改名,**上版文档未同步类名,本次修正**);`umq_tx_helper.cpp` 因 b87d6d4e(2026-08-26 17:52,RNR 错误码 99/10 语义适配)从 `ProcessErrorTxCqe` 起行号 +1,总行数 541→542。另有上版遗漏的函数(`umq_backend.cpp` 的 `DestroyShareMainUmq`、`umq_setting.cpp` 的 `GetSizeClassCount`/`DefaultBlockTypeCheck`、rx/tx 的 `OwnerUmqh`),本次在 §4 补录。§4 行号已按最新源码逐条重核。
- 覆盖数据变化: `umq_buf_converter.h`/`umq_eid_table.h`/`umq_transport_pool.h` 由零覆盖变为达标(协调页 §附注"需专项"已过时,以数据源为准,见 §5.2);`umq_data_plane.h` 首次产生 LF 记录(100.0%/83.3%)。

**刷新记录(2026-08-26)**: 上次快照 2026-08-25 16:51。本次变更: 新增 4 个 umq 测试 target——`umq_setting_test`(b43c9281)/`umq_buffer_receive_queue_test`(e4f19f2c)/`umq_conn_helper_test`(ca65b79c)/`umq_tp_wait_queue_test`(0360a45c);通用层 data tx/rx 测试(473c24e4/8284c103,测 `core/ubsocket_data_*`,不影响 `umq_data_*_ops`);源码重构 30dfed7f(intrusive RX queue 双 cache line 化、UmqSocket 热/冷拆分、新增 `umq_data_plane.h`;`umq_data_rx_ops.*` 位移 +14~16 行、`umq_data_tx_ops.*` 位移 +14~21 行;`umq_tx_helper.cpp` 仅 3 处行内替换、引用行号未漂移)。覆盖率数字来源: `UBSOCKET-COVERAGE-ANALYSIS.ch.md`(快照 2026-08-26,mtime 12:38)+ `build/coverage_filtered.info`(mtime 2026-08-26 12:24),两来源完全一致。§4 行号已按重构后源码逐条重核;认领/流转状态以 `UBSOCKET-CLAIMING.md` 为准。

## 1. 目标范围清单

源码目录: `src/ubsocket/csrc/core/umq/`(下文路径省略此前缀)。

| # | .cpp | 行数 | 对应 .h |
|---|------|------|---------|
| 1 | `umq_backend.cpp` | 584 | `umq_backend.h`(含 inline 类 `UmqZeroCopyAllocator`) |
| 2 | `umq_buffer_receive_queue.cpp` | 366 | `umq_buffer_receive_queue.h` |
| 3 | `umq_conn_helper.cpp` | 270 | `umq_conn_helper.h` |
| 4 | `umq_data_rx_ops.cpp` | 473 | `umq_data_rx_ops.h`(**纯别名**: `using UmqRxOps = ::ock::ubs::DataRxOps`,无可执行行) |
| 5 | `umq_data_tx_ops.cpp` | 885 | `umq_data_tx_ops.h`(**纯别名**: `using UmqTxOps = ::ock::ubs::DataTxOps`,无可执行行) |
| 6 | `umq_errno_converter.cpp` | 139 | `umq_errno_converter.h`(冻结,见 §4.6) |
| 7 | `umq_setting.cpp` | 476 | `umq_setting.h` |
| 8 | `umq_tp_wait_queue.cpp` | 120 | `umq_tp_wait_queue.h` |
| 9 | `umq_transport_pool.cpp` | 389 | `umq_transport_pool.h` |
| 10 | `umq_tx_helper.cpp` | 542 | `umq_tx_helper.h` |

| # | 纯头文件(.h,无 .cpp) | 行数 | 内容形态 |
|---|----------------------|------|---------|
| A | `umq_bounded_seq.h` | 161 | header-only 模板 (`UmqBoundedSeqTraits`/`UmqSocketBoundedSequence`) |
| B | `umq_buf_converter.h` | 72 | header-only (`UmqIovConverter`/`UmqBufferConverter`) |
| C | `umq_eid_table.h` | 348 | header-only (`UmqEidTable`/`EidRegistry`/`RouteListRegistry`/`MainUmqState`) |
| D | `umq_epoll_ops.h` | 29 | header-only,死代码(见 §6.3) |
| E | `umq_intrusive_buf_queue.h` | 167 | header-only SPSC 侵入式队列 (`UmqIntrusiveBufQueue`) |
| F | `umq_qbuf_list.h` | 79 | 纯宏(无可执行行,无需 UT) |
| G | `umq_data_plane.h` | 81 | 数据面分页全局表(`DataPlaneTable`/`DataPlaneEntry`),30dfed7f 新增;仅被 `umq_socket.h:23` include,`Live` 为 header-inline(h:62-67,上版所称"实现体在 `umq_socket.cpp:957-1013`"已不成立——b9db620d 后实现整体在头内),已被范围外 `umq_socket.*` 相关测试实例化覆盖(LF=6,100.0%/83.3%,见 §5.2) |

**变化(2026-08-28)**: 文件清单与上次快照一致(无新增/删除/改名);`umq_data_rx_ops.h`/`umq_data_tx_ops.h` 的内容形态已在 30dfed7f"去虚化合并"后变为纯类型别名(上版文档 §4.4/§4.5 的类名 `UmqRxOps`/`UmqTxOps` 与内联构造体描述已不成立,详见 §4.4/§4.5);`umq_tx_helper.cpp` 541→542 行(b87d6d4e 后行号自 §4.10 的 `ProcessErrorTxCqe` 起 +1)。

## 2. 现有测试与目标文件的对应关系

### 2.1 target 编译构成(`src/ubsocket/unit_test/umq/CMakeLists.txt`)

| target | 编入源文件 | 链接 | 覆盖的目标文件 |
|--------|-----------|------|---------------|
| `umq_errno_converter_test` | 仅 `umq_errno_converter_test.cpp` + `umq_errno_converter.cpp`(CMakeLists:21-24) | 仅 `GTest::gtest_main`(CMakeLists:20),不链 mockcpp | `umq_errno_converter.*` |
| `umq_socket_connector_test` | 仅测试 .cpp(CMakeLists:41-43) | `ubsocket_static` + `boundscheck` + `mockcpp`(CMakeLists:38-40) | `umq_socket_connector.*`(不在目标范围) |
| `umq_tp_event_epoll_runner_ops_test` | 仅测试 .cpp(CMakeLists:57-59) | `ubsocket_static` + `boundscheck` + `mockcpp`(CMakeLists:54-56) | `umq_tp_event_epoll_runner_ops.*`(不在目标范围;5 用例,commit 3e2a20c7) |
| `umq_tp_tx_epoll_runner_ops_test` | 仅测试 .cpp(CMakeLists:73-75) | `ubsocket_static` + `boundscheck` + `mockcpp`(CMakeLists:70-72) | `umq_tp_tx_epoll_runner_ops.*`(不在目标范围;27 用例) |
| `umq_setting_multi_level_test` | 仅测试 .cpp(CMakeLists:90-92) | `ubsocket_static` + `mockcpp`(CMakeLists:87-89) | `umq_setting.cpp` 部分公共 API |
| `umq_setting_test` | 仅测试 .cpp(CMakeLists:106-108) | `ubsocket_static` + `mockcpp`(CMakeLists:103-105) | `umq_setting.cpp`(57 用例,b43c9281,见 §4.7) |
| `umq_tx_helper_multi_level_test` | 仅测试 .cpp(CMakeLists:122-124) | `ubsocket_static` + `mockcpp`(CMakeLists:119-121) | `UmqTxHelper::DataToBlock` 单函数 |
| `umq_zcopy_allocator_multi_level_test` | 仅测试 .cpp(CMakeLists:138-140) | `ubsocket_static` + `mockcpp`(CMakeLists:135-137) | `umq_backend.h` 内 `UmqZeroCopyAllocator` |
| `umq_buffer_receive_queue_test` | 仅测试 .cpp(CMakeLists:154-156) | `ubsocket_static` + `mockcpp`(CMakeLists:151-153) | `umq_buffer_receive_queue.*`(+ 顺带 `umq_bounded_seq.h`/`umq_intrusive_buf_queue.h`) |
| `umq_conn_helper_test` | 仅测试 .cpp(CMakeLists:173-175) | `ubsocket_static` + `boundscheck` + `mockcpp`(CMakeLists:170-172) | `umq_conn_helper.*` |
| `umq_tp_wait_queue_test` | 仅测试 .cpp(CMakeLists:190-192) | `ubsocket_static` + `boundscheck` + `mockcpp`(CMakeLists:187-189) | `umq_tp_wait_queue.*` |
| `umq_tx_helper_test` | 仅测试 .cpp(CMakeLists:209-211) | `ubsocket_static` + `boundscheck` + `mockcpp`(CMakeLists:206-208) | `umq_tx_helper.*`(73 用例,commit 121cbb3b,见 §4.10) |
| `umq_data_rx_ops_test` | 仅测试 .cpp(CMakeLists:228-230) | `ubsocket_static` + `boundscheck` + `mockcpp`(CMakeLists:225-227) | `umq_data_rx_ops.cpp`(58 用例,commit 62bf268b,见 §4.4) |
| `umq_data_tx_ops_test` | 仅测试 .cpp(CMakeLists:247-249) | `ubsocket_static` + `boundscheck` + `mockcpp`(CMakeLists:244-246) | `umq_data_tx_ops.cpp`(81 用例,commit ae60b08e,见 §4.5) |
| `umq_transport_pool_test` | 仅测试 .cpp(CMakeLists:266-268) | `ubsocket_static` + `boundscheck` + `mockcpp`(CMakeLists:263-265) | `umq_transport_pool.*`(39 用例,commit 85786f21,见 §4.9) |
| `umq_backend_test` | 仅测试 .cpp(CMakeLists:285-287) | `ubsocket_static` + `boundscheck` + `mockcpp`(CMakeLists:282-284) | `umq_backend.cpp` + `umq_backend.h` inline(61 用例,commit 00298325,见 §4.1) |

要点:

- **`umq_zcopy_allocator_multi_level_test` 测的是什么**: 被测对象是 `umq_backend.h:52-135` 内联定义的 `UmqZeroCopyAllocator`(无单独 .cpp 文件,这就是目录里找不到 `umq_zcopy_allocator.*` 的原因)。8 个用例(`umq_zcopy_allocator_multi_level_test.cpp:117-273`): `allocate` 的 4 种 pool type + 分配失败;`deallocate` 的 null 指针 / 正常路径 / `block->data==nullptr` 逃逸路径 / 二次 `umq_data_to_head` 间接路径。全部用 `MOCKER_CPP(::umq_buf_alloc)` / `MOCKER_CPP(::umq_data_to_head)` / `MOCKER_CPP(::umq_buf_free)` mock 全局 C 函数。
- 除 `umq_errno_converter_test` 外,其余 target 都链 `ubsocket_static`(libubsocket 全量静态库),因此**测试执行时可能顺带执行到目标文件**——早期 `umq_data_tx_ops.cpp`/`umq_tx_helper.cpp` 那 6.5%/5.4% 的低覆盖率就是这类顺带执行,自各文件专用测试(08-27/08-28 合入)后已不再是主要覆盖来源。
- **历史遗留**: `umq_setting_multi_level_test.cpp` 在顶层 `src/ubsocket/unit_test/CMakeLists.txt:169-201` 被重复注册为 `ubsocket_umq_setting_multi_level_test`(不链 mockcpp,仅 `ubsocket_static` + `boundscheck` + `gtest_main`),与 `umq/CMakeLists.txt:77-92` 的 `umq_setting_multi_level_test` 编同一份源码。新测试不要学这个模式。
- **变化**: 本轮新增 7 个 target(`umq_tp_event_epoll_runner_ops_test`/`umq_tp_tx_epoll_runner_ops_test`/`umq_tx_helper_test`/`umq_data_rx_ops_test`/`umq_data_tx_ops_test`/`umq_transport_pool_test`/`umq_backend_test`),覆盖目标范围 5 个此前零覆盖/低覆盖文件;其余既有 target 的 CMakeLists 行号因新增而整体下移(上表已按最新 :行号 更新)。

### 2.2 目标范围外的测试也在贡献覆盖(实测数据,见 §5)

| 测试文件(非 umq 目录) | 顺带覆盖的目标文件 |
|----------------------|-------------------|
| `unit_test/ubsocket_bitset_lock_seq_test.cpp` | `umq_bounded_seq.h`(95.9% 行 / 79.2% 分支来源;另 `umq_buffer_receive_queue_test` 经 `umq_buffer_receive_queue.h:14` 引入也执行其 `UmqSeqTraits` 逻辑) |
| `unit_test/ubsocket_tx_unified_poller_test.cpp` | 曾覆盖 `umq_data_rx_ops.h`/`umq_data_tx_ops.h` 内联构造体与 `umq_data_tx_ops.cpp` 的 32 行——两 .h 去虚化后已无可执行行、`.cpp` 已有专用测试(94.8%),**该项顺带贡献已清零**;该测试现主要顺带覆盖 `umq_socket.*`(目标范围外) |

> 注: `473c24e4`/`8284c103` 新增的 `ubsocket_data_rx_test`/`ubsocket_data_tx_test` 只 include 通用层头文件(`core/ubsocket_data_rx.h`/`core/ubsocket_data_tx.h`),测的是通用层 shell,不触碰 `umq_data_*_ops`——当时这是 `umq_data_rx_ops.cpp` 保持 0.0% 的原因之一;该状态已被各自的专用测试(见 §2.1)终结。

## 3. 外部依赖与可 mock 点总览

所有目标文件的外部依赖分三类:

1. **UmqApi**(`under_api/dl_umq_api.h`): 全局 C 函数指针封装。测试只能 `MOCKER_CPP(::umq_xxx)`(adapter 后端无 `_ptr`,`UmqApi::xxx_ptr` 赋值编译不过——见 `.opencode/skills/ut-gen/modules/umq.md` 陷阱 1)。
2. **LibcApi**(`under_api/dl_libc_api.h`): `LibcApi::shutdown`(`umq_data_rx_ops.cpp:381`、`umq_data_tx_ops.cpp:671/683`、`umq_tx_helper.cpp:502`);`LibcApi::close`(`umq_transport_pool.cpp:313`);`LibcApi::open` 为 variadic 需用 `open_ptr` 替换。测试需 `LibcApi::Load()` 或逐个设 `_ptr`。
3. **进程内组件**: `EpollRunnerFactory`/`EpollRunnerBase`(`umq_backend.cpp:515`、`umq_conn_helper.cpp:214`、`umq_transport_pool.cpp:305/365/286`)、`TxCqePoller`(`umq_data_tx_ops.cpp:300/317/318/469/470/772`、`umq_transport_pool.cpp:77`)、`UmqEidTable`/`UmqTpWaitQueue`/`Statistics::ProbeManager`/`ArraySet<Socket>`/`PortCooldownManager`/`Validator` 等单例。

> 变化: 无新增依赖种类;`umq_tx_helper.cpp` 三处行号随 b87d6d4e 位移 +1(`shutdown` :501→:502,`PortCooldownManager` :515→:516)。

## 4. 逐文件分析

### 4.1 `umq_backend.cpp` / `umq_backend.h`

**职责**: UMQ 全局初始化/反初始化(`Init`/`UnInit`),设备发现(`AddUbDev`/`FindDevName`/`FindDevEid`),bonding 主 UMQ 创建与预填充(`CreateShareMainUmq`/`PrefillShareMainUmq`/`InitShareJfrMonitering`),share-JFR 中断 fd 注册;`UmqZeroCopyAllocator`(inline)为零拷贝分配器适配。

**关键函数**: `Init`(`.cpp:35-192`)、`UmqCleanup`(:194-203)、`DestroyShareMainUmq`(:205-220,新增)、`UnInit`(:222-235)、`AddUbDev`(:237-290)、`FindDevName`(:292-334)、`FindDevEid`(:336-370)、`CreateShareMainUmq`(:372-490)、`PrefillShareMainUmq`(:492-511)、`InitShareJfrMonitering`(:513-581)。

**UMQ API 依赖**: `umq_init`(:90)、`umq_dev_add`(:278)、`umq_dev_info_list_get`/`_free`(:296/332/340/350)、`umq_create`(:486)、`umq_destroy`(:212)、`umq_uninit`(:201)、`umq_stats_perf_start`/`_reset`/`_tp_perf_start`/`_tp_perf_stop`(:131/142/152/197)、`umq_interrupt_fd_get`(:522/553)。

**errno 处理**: `Convert(UmqOperation::CONNECT, ...)`(:93/134/145/155/281/525/556);`ConvertHandleResult(UmqOperation::BIND_INFO_GET, ...)`(:299/343)。Init 阶段统一走 CONNECT。

**边界敏感判定点**:
- 重复初始化守卫 `UMQ_INITED`(:31,static bool,测试需 SetUp 置 false——AGENTS.md 已知陷阱)。
- 设备名长度: `UMQ_DEV_NAME.length() >= DEV_NAME_STR_LEN_MAX`(64,`common/ubsocket_defines.h:196`)(:253);`UMQ_DEV_NAME.size() >= UMQ_DEV_NAME_SIZE`(:325);`sprintf` 返回值 `ret < 0 || ret >= UMQ_DEV_NAME_SIZE`(:263-264,截断判定)。
- 设备名前缀分支: `strncmp(name, "udma", 4) == 0`(:246)、`strstr(name, "bonding_dev")`(:269)、`strcmp(name, "bonding_dev_0")`(:311)。
- `devCount <= 0`(:297/341)、`bondingIndex == -1 || bondingIndex > devCount || eid_cnt == 0`(:319)。
- trans mode switch `UMQ_TRANS_MODE_IB/UB/default`(:99-110)。
- 路由表: `route_num` 循环、`unique_chip_ids` 去重、`targetChipId = UINT32_MAX` 哨兵(:390-397);`used_ports` 按 chip/die/port 排序 + `value` 去重(:410-437)。
- `strncpy(...) == nullptr` 检查(:450/456)。

**现状覆盖**: .cpp 97.4% 行 / 100% 函数 / 74.3% 分支(LF=349,来源: `umq_backend_test` 61 用例,commit `00298325`,协调页 2026-08-28 流转 done);`umq_backend.h` 92.5% 行 / 75.0% 分支(LF=53,来源: zcopy 测试 + backend 测试)。**已达标**。

### 4.2 `umq_buffer_receive_queue.cpp` / `umq_buffer_receive_queue.h`

**职责**: 每链路接收队列。在序/乱序两条路径: 在序包直接入侵入式 SPSC 环(`receive_queue_`),乱序包进懒创建 `FastHeap`(`out_of_order_queue`),超过 gap/超时触发熔断回灌。

**关键函数**: `ComputeQueueDepth`(:32-37)、构造(:39-58)、`EnsureOooQueue`(:61-70,补录)、`Shutdown`(:78-82,补录)、`IsInitialized`(:83-87)、`PushChainToRing`(static,:89-110)、`Enqueue`(:112-134)、`DequeueBatch`(:136-155)、`ClearAllocations`(:157-168,补录)、`EnqueueInOrder`(:169-223)、`FlushOooQueueInternal`(:225-237)、`FlushReceiveQueueInternal`(:239-251)、`ProcessNormalInOrder`(:253-302)、`CheckAndTriggerMeltdown`(:304-358)、`Empty`(:360-366,补录)。

**UMQ API 依赖**: 仅 `UmqApi::umq_buf_free`(多处)和 `GetCurrentTimeNs` 的 `clock_gettime`(`.cpp:23`,aarch64 下走 `cntvct_el0` 汇编:17-20)。**外部依赖极少,是目标范围里最容易测的 .cpp**。

**errno 处理**: 无 Convert 调用;错误经 `OpResult` 枚举(`umq_buffer_receive_queue.h:28-33`: `OK`/`ERROR=-EPERM`/`QUEUE_FULL=-ENOBUFS`/`MELTDOWN_TRIGGERED=-EPIPE`)。

**边界敏感判定点**(§边界清单新候选):
- 队列深度 2 的幂取整: `queue_depth <= 1 ? 1 : 1ULL << (64 - clzll(queue_depth-1))`(:36);`QUEUE_DEPTH_FACTOR=1.2`(h:100)。
- O3 深度截断: `o3_queue_depth > queue_depth` → clamp 到队列容量(:50-53);`o3_max_depth_` 懒创建时 `min(O3_QUEUE_INIT_DEPTH=4, o3_max_depth_)`(:66)。
- 队列满/空: 环 Push 满 → `QUEUE_FULL` + `pending_error_` 置位(:175-179/127-129);乱序堆满 → `QUEUE_FULL`(:213-220);`DequeueBatch` 在 `pending_error_ != OK` 时反复返回错误(:149-151)。
- 熔断: `gap > m_max_ooo_gap`(:310)与 `now - m_ooo_start_time_ns > m_ooo_timeout_ns`(:311-312,默认 `UMQ_O3_TIMEOUT_MS=60000`);熔断回灌中环溢出 → 丢弃剩余乱序包 + `QUEUE_FULL`(:336-349)。
- 序列号: `gap > UmqSeqTraits::MAX_WINDOW` 丢包(:189);`status >= UMQ_FAKE_BUF_FC_UPDATE || raw_sn == UMQ_PROBE_USER_DATA_ID` 快路径(:174)。
- 参数校验: `DequeueBatch` 的 `buffers==nullptr || max_count==0 || dequeued_count==nullptr`(:139);`Enqueue(nullptr)`(:114);`is_shutdown_` 拒绝(:118-122/144-147)。
- `IsInitialized()` 恒返回 true(:83-87,历史残留,无分支价值)。

**现状覆盖**: .cpp 92.8% 行 / 100% 函数 / 81.1% 分支(分支较上轮 71.0% 上升;来源: `umq_buffer_receive_queue_test`,50 用例,commit `e4f19f2c`,协调页 done);h 4 行 100.0%(无分支数据)。**已达标**。

### 4.3 `umq_conn_helper.cpp` / `umq_conn_helper.h`

**职责**: 连接侧纯逻辑 + 建链辅助。`GetTpInfo`/`GetTargetChipId` 为纯函数;`PrefillRx`/`GetLeftPostRxNum`/`GetRouteList`/`RegisterSharedJfrForRead`/`GetDevEid`/`NewBaseUmqCreateOptions` 走 UMQ API。

**关键函数**: `GetDevEid`(:22-47)、`PrefillRx`(:49-105)、`GetLeftPostRxNum`(:107-124)、`NewBaseUmqCreateOptions`(:126-156)、`GetTpInfo`(:158-177)、`GetRouteList`(:179-210)、`RegisterSharedJfrForRead`(:212-250)、`GetTargetChipId`(:252-266)。

**UMQ API 依赖**: `umq_dev_info_get`(:26)、`umq_post`(:87)、`umq_cfg_get`(:113)、`umq_get_route_list`(:194)、`umq_interrupt_fd_get`(:221)。进程内依赖: `EpollRunnerFactory`(:214)、`UmqShareJfrEpollRunnerOps::ExtContext`(:243)。

**errno 处理**: `Convert(UmqOperation::CONNECT, ...)`(:30/93/198/225),建链上下文统一 CONNECT。

**边界敏感判定点**(§边界清单新候选):
- `PrefillRx`: `left_post_rx_num == 0` → 错误(:52);批量上限 `left > UMQ_POST_BATCH_MAX(256) ? UMQ_POST_BATCH_MAX : left`(:61);`do-while ((left_post_rx_num -= cur_post_rx_num) > 0)` 循环(:102)。
- `GetTpInfo` 四分支 + 默认报错(:160-176);`NewBaseUmqCreateOptions` 中 `trans_mode_str[trans_mode]` 以枚举直接索引(:145-146,**先于** GetTpInfo 校验,`trans_mode > RC_CTP` 会越界——只影响日志)。
- `GetRouteList`: `route_num == 0` → 错误(:205)。
- `GetTargetChipId`: socket_id 未找到 → `UINT32_MAX`(:256-258);`index >= chip_id_list.size()` 越界 → `UINT32_MAX`(:260-263)。
- 优先级: `UMQ_LINK_PRIORITY != UBSOCKET_LINK_PRIORITY_NOT_SET` → 置 `UMQ_CREATE_FLAG_PRIORITY`(:140-143)。
- `RegisterSharedJfrForRead`: `umq_interrupt_fd_get` 返回 `< 0`(:222);`AddEpollEvent` 非 0(:245)。

**现状覆盖**: .cpp 100.0% 行 / 100% 函数 / 60.0% 分支(分支较上轮 54.4% 上升;来源: `umq_conn_helper_test`,34 用例,commit `ca65b79c`,`MOCKER_CPP_VIRTUAL` 拦截纯虚 EpollRunner,协调页 done)。**已达标**。(注意: `umq_socket_connector_test.cpp:301-336` 的 `GetTargetChipId_*` 用例测的是 `UmqConnectorOps::GetTargetChipId`,不是本文件 `UmqConnHelper::GetTargetChipId`,别混。)

### 4.4 `umq_data_rx_ops.cpp` / `umq_data_rx_ops.h`

**职责**: RX 数据通路。`PollRx` 处理共享 JFR 弹出/`umq_poll` 的 qbuf: 探测包、流控/错误状态、`block_cache_` 记账;`UmqPollAndRefillRx` 维护 RX 窗口;`FlushRx` 关闭时排空。(30dfed7f 重构后本文件整体位移 +14~16 行,并新增 `OwnerFd` 访问器。)

**变化(2026-08-28)**: 类名已由 `UmqRxOps` 改为 `DataRxOps`(30dfed7f"v1.7 去虚化合并",上版文档未同步);`umq_data_rx_ops.h` 已是纯类型别名(`using UmqRxOps = ::ock::ubs::DataRxOps`),不再有内联构造体等可执行代码。

**关键函数**: `OwnerFd`(:27-30,新增)、`OwnerUmqh`(:32-35,补录)、`PollRx`(:37-127)、`DataToBlock`(:129-137)、`GetQbuf`(:138-152)、`UmqPollAndRefillRx`(:153-214)、`HandleBadQBuf`(:216-239)、`GetAndAckEvent`(:241-266)、`HandleErrorRxCqe`(:268-383)、`RearmRxInterrupt`(:385-404)、`PollSubUmqRx`(:406-421)、`FlushRx`(:423-473)。

**UMQ API 依赖**: `umq_poll`(:158/411/443)、`umq_post`(:196)、`umq_get_cq_event`(:245)、`umq_ack_interrupt`(:261)、`umq_rearm_interrupt`(:393)、`umq_buf_free`、`umq_data_to_head`(:131)。进程内依赖: `UmqSocket::GetAndPopQbuf`(:144)、`Statistics::ProbeManager`(:75)、`sock->State/NotifyReadable`、`LibcApi::shutdown`(:381)。

**errno 处理**: `Convert(UmqOperation::READV, ...)`(:164/202/252/396);`ConvertBufStatus(UmqOperation::READV, ...)`(:271)。RX 数据通路统一 READV。

**边界敏感判定点**(§边界清单新候选):
- 探测包判定: `opcode == UMQ_OPC_SEND_IMM && imm.user_data == UMQ_PROBE_USER_DATA_ID(0xFFFFFF)`(:73)。
- CQE 状态分支: `status != 0` 时 `>= UMQ_FAKE_BUF_FC_UPDATE` 的三种细分(:86-110);`HandleErrorRxCqe` 的 ~20 个 status case 大 switch(:276-373)。
- 窗口阈值: `(UBS_RX_DEPTH - rx_queue_avail_num_) > TX_REFILL_THRESHOLD(32)` 触发 refill(:173);refill 失败不致命,`poll_num==0 && rx_queue_avail_num_==0` 才整体失败(:159)。
- ack 批量: `(ack_event_num_ += events) >= GET_PER_ACK(32)`(:259)。
- `RearmRxInterrupt`: `UMQ_TP_TYPE == POOL` 直接 OK(:387);`ret < 0` 才 Convert(:394)。
- `FlushRx`: 超时熔断 `SocketConnHelper::IsTimeout`(:435);循环条件 `Type() != SOCK_TYPE_COUNT && poll_total_cnt < rx_queue_avail_num_`(:465);泄漏计数 `(rx_queue_avail_num_ -= poll_total_cnt) > 0`(:467-468)。
- `HandleBadQBuf` 的 `while (cur_qbuf != bad_qbuf)` + `rest_size` 递减(:221-231)。

**现状覆盖**: .cpp 100.0% 行 / 100% 函数 / 62.1% 分支(LF=290,来源: `umq_data_rx_ops_test` 58 用例,commit `62bf268b`,协调页 done)。**已达标**;h 为纯别名,无可执行行(上版"h 8 行可执行 87.5%"已不成立)。

### 4.5 `umq_data_tx_ops.cpp` / `umq_data_tx_ops.h`

**职责**: TX 数据通路。`PostSend` 把 brpc iov/buffer 转成 umq WR 链(seq/solicited/unsignaled 标记),`umq_post` 失败按 EAGAIN/ETIMEDOUT/EFLOWCTL/EMLINK/ENOBUFS 分流;`PollTx`/`DoUmqTxPoll` 回收 TX CQE;`FlushTx` 关闭排空;`AllocTxBuf` 分配。(30dfed7f 重构后本文件整体位移 +14~21 行,并新增 `OwnerFd`/`IOBufSize` 访问器。)

**变化(2026-08-28)**: 类名已由 `UmqTxOps` 改为 `DataTxOps`(30dfed7f,上版文档未同步);`umq_data_tx_ops.h` 已是纯类型别名(`using UmqTxOps = ::ock::ubs::DataTxOps`)。

**关键函数**: `OwnerFd`(:32-35,新增)、`OwnerUmqh`(:37-40,补录)、`AllocTxBuf`(:45-59)、`ProcessTracePacket`(:61-173)、`PostSend`(:175-499)、`PollTx`(:501-552)、`GetAndAckEvent`(:554-584)、`PollUmqTx`(:586-601)、`PollUmqTxOnce`(:603-610)、`ForceDrainTx`(:612-618)、`QuickPollTx`(:620-627)、`WakeUpTx`(:629-637)、`Writable`(:639-659)、`DoUmqTxPoll`(:661-704)、`DpRearmTxInterrupt`(:706-725)、`DataToBlock`(:727-737)、`IOBufSize`(:739-742,新增)、`HandleBadQBuf`(:744-788)、`FlushTx`(:790-885)。

**UMQ API 依赖**: `umq_buf_alloc`(:48)、`umq_post`(:295)、`umq_poll`(经 `UmqTxHelper::PollUmqTx`,:679)、`umq_get_cq_event`(:559)、`umq_ack_interrupt`(:575)、`umq_rearm_interrupt`(:580/710)、`umq_data_to_head`(:729)、`umq_buf_free`。进程内依赖: `TxCqePoller`(MarkActive/NotifyInflight/NotifyPosted,:300/317/318/469/470/772)、`UmqTpWaitQueue::Enqueue`(:376)、`PortCooldownManager::MarkPortInCooldown`(:699)、`LibcApi::shutdown`(:671/683)、`UmqTxHelper`(PollArgs/PollUmqTx,:679)。

**errno 处理**: `Convert(UmqOperation::WRITEV, ...)`(:324/485/718);`GetAndAckEvent` 用 WRITEV(:566)。TX 数据通路统一 WRITEV。

**边界敏感判定点**(§边界清单新候选):
- WR 分组: `++sge_idx >= TX_SGE_MAX(1) || moved_total_len >= io_buf_size(4064)` 切分 WR(:236);`moved_total_len == 0` 跳过空 WR(:241-245)。
- solicited 阈值: `tx_queue_avail_num_ == 1 || i+1 == batch`(:260)、`unsolicited_wr_num_ > TX_REPORT_THRESHOLD(1) || unsolicited_bytes_ > TX_UNSOLICITED_BYTES_MAX(1MB)`(:263)。
- unsignaled 批量: `++unsignaled_wr_num_ >= TX_REPORT_THRESHOLD(1)` → `complete_enable=1`(:277-281)。
- `umq_post` 错误分流(:318-399): `bad_qbuf==nullptr` 分支(:481-499)、EAGAIN 全败/部分(:321-356,`SetNotWritableReadyIfUnchanged` CAS 语义,:341/375/391)、ETIMEDOUT → EIO(:358-361)、`ret==-UMQ_ERR_EFLOWCTL*` → EIO(:363-366)、EMLINK → 入等待队列 + EAGAIN(:367-378,`UmqTpWaitQueue::Enqueue` :376)、ENOBUFS(:379-395)、其余 → EIO(:396-399)。
- 全败恢复: `bad_qbuf == tx_buf_list` 时恢复 unsolicited/unsignaled 计数与 head/tail(:414-432)。
- `PollTx` 的 `compare_exchange_strong(expect_epoll_event_num_, 0)` 循环(:519-520)。
- `PollUmqTx` 循环条件 `(poll_total_cnt < TX_RETRIEVE_THRESHOLD(32) || poll_to_empty)`(:599)。
- `DpRearmTxInterrupt` 成功路径不走 Convert: `ret==0` → 直接 `errno=EAGAIN; return -1`(:710-716,AGENTS.md 已知陷阱)。
- `Writable`: RNR 反压 `IsRnrBlocked()`(:646)与 jetty `WAITING` 状态(:654)。
- `DoUmqTxPoll`: `TryRnrBlockFatal()` 断链(:669-674);CLOS 拓扑下 status ∈ {LOC_LEN, LOC_ACCESS, ACK_TIMEOUT, FC_ERR, FC_ERR_FATAL} → 全部 port 冷却(:691-702)。
- `FlushTx`: 超时熔断(:803)、PollerYield 周期(:822)、unsignaled 缓存 WR 释放(`cached_wr_cnt < left_wr_num` 循环,:835-875)、泄漏日志(:878)。

**现状覆盖**: .cpp 94.8% 行 / 100% 函数 / 61.0% 分支(LF=496,来源: `umq_data_tx_ops_test` 81 用例,commit `ae60b08e`,协调页 done;上版 6.5% 的"顺带覆盖"表述作废)。**已达标**;h 为纯别名,无可执行行。

### 4.6 `umq_errno_converter.cpp` / `umq_errno_converter.h`(冻结)

**职责**: UMQ 错误码 → Linux errno 映射(冻结文件,只读不写——AGENTS.md 红线)。

**关键 API**: `Convert`(`.cpp:17-37`)、`ConvertBufStatus`(:39-64)、`ConvertHandleResult`(:119-135)、`GetErrorDescription`(:66-86)、`GetBufStatusDescription`(:88-105);`UmqOperation` 枚举(`.h:44-53`: CONNECT/ACCEPT/WRITEV/READV/CREATE/BIND_INFO_GET/GET_STATE);三张映射表 `kCommonErrnoMappings`/`kCommonConnectAcceptBufStatusMappings`/`kWritevBufStatusMappings`/`kReadvBufStatusMappings`(`.h:151-248`)。

**errno 处理**(即本体): `Convert` 的 override 规则(`.cpp:107-117`: `UMQ_FAIL`+savedErrno∈{EINVAL,ENODEV,ENOMEM,ENOEXEC,EIO} → 返 savedErrno;`UMQ_ERR_ENODEV`+{EINVAL,EIO} → 返 savedErrno);GET_STATE 特殊路径(:23-28,不查表);未命中回退 savedErrno>0,否则 EIO。

**边界敏感判定点**: 已登记于 `ut-gen/modules/umq.md` §边界清单(3 条,全部冻结文件),无新增。

**现状覆盖**: .cpp 83.6% 行 / 100% 函数 / 89.7% 分支(LF=73);h 91.7% 行 / 90% 分支。**已达标且测试存在(`umq_errno_converter_test`,546 行用例覆盖全部操作枚举与映射表),本轮不需要也不应该动它**。

### 4.7 `umq_setting.cpp` / `umq_setting.h`

**职责**: UMQ 配置。env 解析(`LoadEnv`,22 个 `UBSOCKET_*` 环境变量)、规则注册(`AddRules`)、校验(`VerifySetting`)、纯函数(尺寸/分类/合并)。

**关键函数**: `AddRules`(:97-136)、`LoadEnv`(:138-263)、`VerifySetting`(:265-305)、`Init`(:307-328,private,friend `UmqBackend`)、`BlockSizeToBytes`(:330-354)、`GetSizeClassCount`(:356-358,补录)、`GetIOBufSizeByClass`(:361-367)、`GetIOBufSize`(:369-372)、`GetRXBufCountsByClass`(:374-380)、`CountRXBufByClass`(:382-405)、`MergeBufLists`(:407-425)、`FloorMask`(:427-430)、`DefaultBlockTypeCheck`(:432-435,补录)、`BlockTypeFromStr`(:437-443)、`TinyBlockSizeFromStr`(:445-459)、`SchedulePolicyFromStr`(:462-473)。

**UMQ API 依赖**: 无。进程内依赖: `GlobalSetting::GetEnvAndValidate`、`Validator::Instance()`(加规则/校验)。

**errno 处理**: 无 Convert;`VerifySetting` 失败直接 `errno = EINVAL`(:319)。

**边界敏感判定点**(§边界清单新候选):
- `AddRules` 区间(:100-115): credit 1..1024、`UBSOCKET_POOL_MAX_SIZE` 1..6144、`UBSOCKET_LINK_PRIORITY` -1..15、`UBSOCKET_JETTY_POOL_SIZE` 1..1000、O3 超时 ≥2、各 pool depth 0..15360 等;枚举串(block type/tiny size/schedule policy/trans mode/tp type)。
- `VerifySetting`: `UMQ_MIDDLE_POOL_BLOCK_SIZE` ∈ [8K, 1M] 且 4K 对齐且 2 的幂(:274-286);`UMQ_MEM_POOL_MAX_SIZE_MB` 过 Validator(:269)。
- `GetIOBufSizeByClass`: `sc >= UMQ_SIZE_CLASS_COUNT` 回退到末位 class(:363-366);`UMQ_EXPLICIT_BLOCK_SIZES` 与 `IOBUF_DIFF` 截断。
- `CountRXBufByClass`: `data_size + headroom_size` 与 block size 比较分类,超界归入最大 class(:391-404)。
- 字符串解析回退: `TinyBlockSizeFromStr` 默认 1K(:445-459)、`SchedulePolicyFromStr` 默认 affinity_priority(:462-473)、`BlockTypeFromStr` 默认 4K(:437-443)。

**现状覆盖**: .cpp 97.6% 行 / 100% 函数 / 97.9% 分支(分支较上轮 82.6% 明显上升;来源: `umq_setting_test`,57 用例,commit `b43c9281`,协调页 done;209 可执行行,5 行未命中)。`AddRules`/`LoadEnv`/`VerifySetting`/`Init`(private,friend `UmqBackend`)与字符串解析函数均已覆盖,边界清单 5 条全过。**已达标**。旧的 `umq_setting_multi_level_test` 仅覆盖公共纯函数的历史已成过去式。

### 4.8 `umq_tp_wait_queue.cpp` / `umq_tp_wait_queue.h`

**职责**: jetty 资源等待队列(EMLINK 时挂起 socket,FC-return 时挂起 handle),`WakeUp` 按释放数唤醒。

**关键函数**: `Enqueue(SocketPtr)`(:19-37)、`Enqueue(uint64_t)`(:103-116)、`TryWakeupOne`(:39-70)、`WakeUp`(:72-101)。元素类 `UmqTpWaitQueueElement`(h:31-63)。

**UMQ API 依赖**: 无直接调用。进程内依赖: `UmqSocket`(`TryAcquireForWaiting`/`ResetToIdle`/`RxQueueEmpty`/`NotifyReadable`/`NotifyWritable`,可 mockcpp mock 类方法)、`UmqTxHelper::PollUmqTxForFcReturn`(:64/94)。

**errno 处理**: 无 Convert。

**边界敏感判定点**(§边界清单新候选):
- `WakeUp(wakeUpNum)`: `wakeUpNum > UMQ_TP_POOL_SIZE(800)` → 直接返回 0(:74);`MultiPop` 实际弹出数 `count`。
- `Enqueue(sock)`: null 检查(:21-24);`TryAcquireForWaiting()` 失败 → 不排队返回 OK(:26-36);`Push` 失败 → `ResetToIdle` + UBS_ERROR(:31-33)。
- `Enqueue(handle)`: `UMQ_INVALID_HANDLE` → UBS_ERROR(:105-113)。
- 元素类型三分支: `UMQ_SOCKET`/`UMQ_HANDLE`/other(:51-68/81-98,other 只打日志)。
- 队列容量: `MPSCRingQueue` 容量 = `next_pow2(UBS_RX_DEPTH)`(h:84-92,`cap==0` 回退 `DEFAULT_CAPACITY=1024`,h:71)。

**现状覆盖**: .cpp 97.0% 行 / 100% 函数 / 78.8% 分支(分支较上轮 58.7% 上升;来源: `umq_tp_wait_queue_test`,19 用例,commit `0360a45c`,协调页 done);h 14 行 92.9% 行 / 50.0% 分支。**已达标**。

### 4.9 `umq_transport_pool.cpp` / `umq_transport_pool.h`

**职责**: transport pool(jetty 池)预热与维护。`WarmUp` 创建池 + 注册 poller/timer/epoll 事件;`CreateOneTp` 按 affinity/round-robin 选 port 创建 TP;`RebuildTp` 故障重建。

**关键函数**: `WarmUp`(:29-95)、`CreatePool`(:97-110)、`Clean`(:112-136)、`CreateOneTp`(:138-256)、`RebuildTp`(:258-290)、`PoolSize`(:292-300)、`AddTimerEvent`(:302-360)、`AddTransportEpollEvent`(:362-386)。

**UMQ API 依赖**: `umq_transport_pool_resource_create`(:238)、`_modify`(:124/276)、`_destroy`(:129/281)、`umq_interrupt_fd_get`(:248)、`umq_transport_pool_eventfd_get`(:370)。进程内依赖: `EpollRunnerFactory`(TRANSPORT_POOL_TX/EVENT_RUNNER,:305/365/286)、`UmqConnHelper::GetRouteList`(:60)、`TxCqePoller::RegisterOrphanSweep`(:77)、`LibcApi::close`(:313)、`timerfd_create/timerfd_settime`(libc,可直接 mockcpp)。

**errno 处理**: 无 Convert(include 了 `umq_errno_converter.h` 但未使用,仅 errno 直传日志)。

**边界敏感判定点**(§边界清单新候选):
- `CreatePool`: `pool_size` 循环,成功计数(:97-110);`WarmUp` 已预热去重 `umq_tp_pool.count(main_umqh)`(:54)。
- `WarmUp`: 非 RM 模式或非 POOL 提前返回(:50-52);`unifiedEnabled` 走 `RegisterOrphanSweep` 否则 `AddTimerEvent`(:72-85);任一失败 → `Clean()`。
- `CreateOneTp`: affinity 分支 `aff_ports.empty()` → 错误(:174-177);`all_ports.empty()` → 错误(:190-193);`aff_rr_num_ %= aff_ports.size()` / `rr_num_ %= all_ports.size()` 轮转(:179/195);port 去重保序(:209-219);`tp_idx == UINT32_MAX` → 错误(:239);`fd < UBS_OK` → 错误(:249)。
- `RebuildTp`: 池空/umq 不存在/tp_idx 不存在三查(:261-275);modify/destroy 返回负 → 错误。
- `AddTimerEvent`: `timerfd_create < 0`(:308)、`tx_epoll_event` 分配失败(:320-323)、`timerfd_settime < 0`(:343-346)、`AddEpollEvent < 0`(:352);ScopeExit 双重清理语义(:312-315/324-327/357-358)。
- `Clean`: 池空直接 OK(:114-117);`resource_modify < 0` continue、`resource_destroy < 0` 记日志(:124-132)。

**现状覆盖**: .cpp 97.4% 行 / 100% 函数 / 69.3% 分支(LF=229,来源: `umq_transport_pool_test` 39 用例,commit `85786f21`,协调页 done;审查修复 mock 漏挂载、死代码豁免 3 处);h 7 行 100.0% 行 / 50.0% 分支。**已达标**。

### 4.10 `umq_tx_helper.cpp` / `umq_tx_helper.h`

**职责**: TX CQE 轮询通用逻辑(被 `DataTxOps::DoUmqTxPoll`、FC-return 轮询复用)。`PollUmqTxInternal` 分发 CQE: bigdata 完成 / RNR 软反压 / 错误 / 探测包 / 正常 WR;`ProcessTxCqe` 沿 qbuf 链 DecRef + 释放;`PollUmqTxForFcReturn` 无 socket 上下文轮询。

**变化(2026-08-28)**: b87d6d4e(2026-08-26 17:52,"适配URMA交换RNR 错误码99和10的语义")改动本文件,RNR 软反压判定宏由 `UMQ_BUF_RNR_RETRY_CNT_EXC_ERR` 改为 `UMQ_BUF_RNR_RETRY_CNT_EXC`(URMA 透传 status=99),并从 `ProcessErrorTxCqe`(:402)起行号 +1、总行数 541→542。`LogTxCqeErrorMsg` 中新增 status=99 软反压日志分支。

**关键函数**: `PollUmqTxInternal`(:27-175)、`ProcessTxCqe`(:177-235)、`HandleTxCqeError`(:237-248)、`HandleProbePacket`(:250-260)、`LogTxCqeErrorMsg`(:262-399)、`ProcessErrorTxCqe`(:402-434)、`HandleRnrNotify`(:436-470)、`DataToBlock`(:472-479)、`PollUmqTxForFcReturn`(:481-538);模板 `PollUmqTx`(h:57-70)、`PollArgs`(h:39-53)。

**UMQ API 依赖**: `umq_poll`(:37)、`umq_buf_free`、`umq_data_to_head`(:474)。进程内依赖: `UbsBigdata::HandleTxCompletion`(:78)、`UmqTpWaitQueue::WakeUp`(:167)、`ArraySet<Socket>`(:134/367/491)、`UmqSocket` 的 RNR 状态机(`IsRnrBlocked`/`SetRnrBlocked`/`OnRnrRecover`/`OnRnrEnter`/`OnRnrTimeout`/`ReadyAndExchange`)、`PortCooldownManager`(:516)、`LibcApi::shutdown`(:502)。

**errno 处理**: `Convert(UmqOperation::WRITEV, ...)`(:47/523);`ConvertBufStatus(UmqOperation::WRITEV, ...)`(:265)。TX 通路统一 WRITEV。

**边界敏感判定点**(§边界清单新候选):
- `PollUmqTxInternal` 无效 CQE 判定: `buf[i]==nullptr || status != 0 || qbuf_ext==nullptr || user_ctx==nullptr`(:92-93);status!=0 → `error_cb.invoke` + `HandleTxCqeError`(:102-105)。
- 静默轮询: `silent_poll_err && poll_num < 0` → Convert 后 `errno == EMLINK` 分支只打 DEBUG(:45-59)。
- RNR 软反压: `status == UMQ_BUF_RNR_RETRY_CNT_EXC`(=99,URMA 透传软反压信号)&& `UBS_RNR_BACKPRESSURE_ENABLED`(:87-88);`HandleRnrNotify` 首/重复进入(first_entry,:449-462),**不释放 buf**(:465-470 注释: bonding 重发会 double free)。
- `ProcessTxCqe`: `left_size` 递减循环(:192-211),`is_coalesced_small` 跳过 DecRef(:195-201);`wr_first_buf == nullptr` → -1(:220-223)。
- `LogTxCqeErrorMsg` 的 status 大 switch(:273-297 桶归类 + :306-398 日志分支),含 `UMQ_BUF_RNR_RETRY_CNT_EXC_ERR`(fatal)的 `OnRnrTimeout` 路径(:354-370,:362 调 OnRnrTimeout;上版写 `_ERR_FATAL` 后缀有误,实际无该宏)与新增 `UMQ_BUF_RNR_RETRY_CNT_EXC`(status=99)日志分支(:367-374,仅 `UBS_RNR_BACKPRESSURE_ENABLED=false` 时落此,旧行为断链)。
- `ProcessErrorTxCqe`: `status >= UMQ_FAKE_BUF_FC_UPDATE` → 直接 free 不走链(:404-407)。
- `PollUmqTxForFcReturn`: `silent_poll_err = (UMQ_TP_TYPE == POOL)`(:487);`ret < 0` 时 EMLINK 静默、其余 Convert + UBS_ERROR(:521-536)。

**现状覆盖**: .cpp 97.1% 行 / 100% 函数 / 78.6% 分支(LF=314,来源: `umq_tx_helper_test` 73 用例,commit `121cbb3b`,协调页 done;上版 5.4% 表述作废);h 15 行 100.0% 行 / 50.0% 分支(模板 PollUmqTx 已覆盖)。**已达标**。

## 5. 覆盖率汇总

### 5.1 数据来源

| 来源 | 日期/时间 | 说明 |
|------|----------|------|
| A: `docs/ubsocket/UBSOCKET-COVERAGE-ANALYSIS.ch.md` | 快照日期 2026-08-28(文件 mtime 2026-08-28 10:38) | 唯一权威数字源,§5 逐文件 .cpp 表 + §6 零覆盖 .h 表 |
| B: `src/ubsocket/build/coverage_filtered.info` 实测 | mtime 2026-08-28 10:36(同批 `coverage_summary.txt`/`coverage_detailed.txt`) | 本次读取的实测数据;全局指标与 A 完全一致(行 52.6%(7181/13661)、函数 69.4%(1108/1597)、分支 38.8%(5836/15040)) |
| C: `ctest --test-dir src/ubsocket/build` 实测 | 2026-08-28(本次刷新时实跑) | 47/47 全绿(含 16 个 umq target),总耗时 ~3.4s |

两来源逐文件与全局数字完全一致,下文统一列出并注明"来源 A/B 一致"。本次刷新同时实跑了 ctest 验证(来源 C)。

### 5.2 目标范围逐文件覆盖率

| 文件 | 行% | 分支% | 来源 | 达标(行≥80/分支≥50)? |
|------|-----|-------|------|----------------------|
| `umq_errno_converter.cpp` | 83.6 | 89.7 | A/B 一致 | 达标(冻结,测试已存在) |
| `umq_backend.cpp` | 97.4 | 74.3 | A/B 一致 | 达标(来源: `umq_backend_test` 00298325) |
| `umq_buffer_receive_queue.cpp` | 92.8 | 81.1 | A/B 一致 | 达标(来源: `umq_buffer_receive_queue_test` e4f19f2c) |
| `umq_conn_helper.cpp` | 100.0 | 60.0 | A/B 一致 | 达标(来源: `umq_conn_helper_test` ca65b79c) |
| `umq_data_rx_ops.cpp` | 100.0 | 62.1 | A/B 一致 | 达标(来源: `umq_data_rx_ops_test` 62bf268b) |
| `umq_data_tx_ops.cpp` | 94.8 | 61.0 | A/B 一致 | 达标(来源: `umq_data_tx_ops_test` ae60b08e) |
| `umq_setting.cpp` | 97.6 | 97.9 | A/B 一致 | 达标(来源: `umq_setting_test` b43c9281) |
| `umq_tp_wait_queue.cpp` | 97.0 | 78.8 | A/B 一致 | 达标(来源: `umq_tp_wait_queue_test` 0360a45c) |
| `umq_transport_pool.cpp` | 97.4 | 69.3 | A/B 一致 | 达标(来源: `umq_transport_pool_test` 85786f21) |
| `umq_tx_helper.cpp` | 97.1 | 78.6 | A/B 一致 | 达标(来源: `umq_tx_helper_test` 121cbb3b) |
| `umq_backend.h` | 92.5 | 75.0 | 仅 B(LF=53) | 达标(来源: zcopy 测试 + backend 测试) |
| `umq_bounded_seq.h` | 95.9 | 79.2 | 仅 B(LF=49) | 达标(来源: bitset_lock_seq 测试 + buffer_receive_queue 测试) |
| `umq_buf_converter.h` | 93.3 | 50.0 | 仅 B(LF=30) | **达标**(上版零覆盖,本轮经 ubsocket_static 库内执行顺带覆盖,未见直接 include 的测试文件) |
| `umq_buffer_receive_queue.h` | 100.0 | —(4 行,无分支) | 仅 B | 达标 |
| `umq_data_plane.h` | 100.0 | 83.3 | 仅 B(LF=6) | 达标(上版无 LF 记录;`Live` 现已被实例化并覆盖,经 `umq_socket.*` 相关测试顺带) |
| `umq_data_rx_ops.h` | — | — | 无 LF 记录 | 纯类型别名(`using UmqRxOps = DataRxOps`),无可执行行 |
| `umq_data_tx_ops.h` | — | — | 无 LF 记录 | 纯类型别名(`using UmqTxOps = DataTxOps`),无可执行行 |
| `umq_eid_table.h` | 85.7 | 53.4 | 仅 B(LF=98) | **达标**(上版零覆盖;`umq_socket_connector_test.cpp`/`umq_backend_test.cpp` 直接 include,顺带覆盖) |
| `umq_epoll_ops.h` | — | — | 无可执行行记录 | 死代码(见 §6.3) |
| `umq_errno_converter.h` | 91.7 | 90.0 | 仅 B(LF=24) | 达标(冻结) |
| `umq_intrusive_buf_queue.h` | 100.0 | 93.8 | 仅 B(LF=47) | 达标(来源: buffer_receive_queue 测试,含 2 个直接用例) |
| `umq_qbuf_list.h` | — | — | 纯宏,无 LF 记录 | 无需 UT |
| `umq_setting.h` | — | — | 无 LF 记录(纯声明+constexpr) | — |
| `umq_tp_wait_queue.h` | 92.9 | 50.0 | 仅 B(LF=14) | 达标 |
| `umq_transport_pool.h` | 100.0 | 50.0 | 仅 B(LF=7) | 达标(上版零覆盖,随 `umq_transport_pool_test` 覆盖) |
| `umq_tx_helper.h` | 100.0 | 50.0 | 仅 B(LF=15) | 达标(上版 80.0%,模板 PollUmqTx 现已被 `umq_tx_helper_test` 覆盖) |

**结论**: 目标范围内 **10 个 .cpp 全部达标**(含冻结且达标的 `umq_errno_converter.cpp`),剩余待补为零;目标范围内纯头文件也全部达标(`umq_buf_converter.h`/`umq_eid_table.h` 已由零覆盖转达标,`umq_transport_pool.h`/`umq_tx_helper.h`/`umq_data_plane.h` 同步转达标),**本调研范围已 100% 收官**。下一步工作量转向范围外文件(`umq_socket.cpp` 650 行 12.6%、`umq_socket_connector.cpp` 28.4%、`umq_socket_acceptor.cpp` 18.2%、`umq_share_jfr_epoll_runner_ops.cpp` 27.6%,认领表已列)。

## 6. 补 UT 优先级建议

### 6.1 优先级排序(建议认领顺序)

> 2026-08-28 刷新: 目标范围 9 个可认领 .cpp 已全部 `done`(认领状态与 commit 以 `UBSOCKET-CLAIMING.md` 为准),本表转为完成记录表;不再产生新任务。

| 优先级 | 文件 | 现状与理由 | 主要 mock 点 | 已登记的边界清单(ut-gen §边界清单) |
|--------|------|-----------|-------------|-----------------------------------|
| 已完成 | `umq_buffer_receive_queue.cpp` | 92.8%/81.1% 达标;`umq_buffer_receive_queue_test`(50 用例,e4f19f2c);协调页 done | — | 已登记(2 的幂深度、O3 截断、队列满/熔断) |
| 已完成 | `umq_intrusive_buf_queue.h` | 100%/93.8% 达标;随 buffer_receive_queue 测试覆盖(含 2 个直接用例) | — | — |
| 已完成 | `umq_setting.cpp` | 97.6%/97.9% 达标;`umq_setting_test`(57 用例,b43c9281);协调页 done | — | 已登记(AddRules 区间、8K..1M/4K 对齐、越界回退,5 条全过) |
| 已完成 | `umq_conn_helper.cpp` | 100%/60.0% 达标;`umq_conn_helper_test`(34 用例,ca65b79c);协调页 done | — | 已登记(7 条全过) |
| 已完成 | `umq_tp_wait_queue.cpp` | 97.0%/78.8% 达标;`umq_tp_wait_queue_test`(19 用例,0360a45c);协调页 done | — | 已登记(5 条全过) |
| 已完成 | `umq_tx_helper.cpp` | 97.1%/78.6% 达标;`umq_tx_helper_test`(73 用例实测,121cbb3b);协调页 done | — | 已登记(7 条全过) |
| 已完成 | `umq_data_rx_ops.cpp` | 100.0%/62.1% 达标;`umq_data_rx_ops_test`(58 用例,62bf268b);协调页 done | — | 已登记(7 条全过) |
| 已完成 | `umq_data_tx_ops.cpp` | 94.8%/61.0% 达标;`umq_data_tx_ops_test`(81 用例,ae60b08e);协调页 done | — | 已登记(10 条全过,含审查修复 wr_num 阈值归因 batch=3;陷阱已回流 umq.md #14) |
| 已完成 | `umq_transport_pool.cpp` | 97.4%/69.3% 达标;`umq_transport_pool_test`(39 用例,85786f21);协调页 done | — | 已登记(5 条全过;死代码豁免 3 处;陷阱已回流 umq.md #15) |
| 已完成 | `umq_backend.cpp` | 97.4%/74.3%/函数 100% 达标;`umq_backend_test`(61 用例,00298325);协调页 2026-08-28 流转 done | — | 已登记(7 条全过;未覆盖分支仅剩豁免 2 处) |
| 冻结 | `umq_errno_converter.*` | 已达标(83.6%/89.7%),测试已存在,不动 | — | 已登记(3 条) |

**范围外提醒**: 协调页认领表下一步是 `umq_socket.cpp`(P1,650 行,起点 12.6%/6.7%,依赖最重,先读 `UBSOCKET-ARCHITECTURE.ch.md`)。

### 6.2 头文件补测说明

- `umq_buf_converter.h`(30 行): 已由零覆盖转 93.3%/50.0%,**达标**;`UmqIovConverter::MemCopy` 的 iov 边界与 `UmqBufferConverter::MemCopy` 的长度截断等用例不再需要专门补(协调页附注"需专项"已过时,以 §5.2 数据源为准)。
- `umq_eid_table.h`(98 行): 已由零覆盖转 85.7%/53.4%,**达标**(`UmqEidTable`/`EidRegistry`/`RouteListRegistry`/`MainUmqState` 均已被覆盖;`EidRegistry` 是 LeakySingleton,用例间 `UnregisterEid()` 清理的既有约定不变)。
- `umq_intrusive_buf_queue.h`(47 行)已达标(100%/93.8%,随 `umq_buffer_receive_queue_test` 覆盖),不再产生任务。
- `umq_tp_wait_queue.h`(14 行,92.9%)/`umq_buffer_receive_queue.h`(4 行,100%)/`umq_transport_pool.h`(7 行,100%)已随各自 .cpp 测试覆盖;`umq_tx_helper.h`(15 行,100%)已随 `umq_tx_helper_test` 覆盖;`umq_data_plane.h`(6 行,100%)已随范围外 `umq_socket.*` 相关测试顺带覆盖。
- `umq_qbuf_list.h` 纯宏无需 UT;`umq_epoll_ops.h` 是死代码(见 §6.3),**不列入补测范围**;`umq_data_rx_ops.h`/`umq_data_tx_ops.h` 为纯类型别名,无可测内容。

### 6.3 其他要点

- **UmqOperation 选择**: 补测新增用例时,建链/初始化上下文(backend/conn helper)用 `CONNECT`;TX 数据通路用 `WRITEV`;RX 数据通路用 `READV`(与现有调用点一致,`ut-gen/modules/umq.md` §op 标注规则)。
- **`UBS_ENABLE_SHARE_JFR` 默认 true**: 测 RX/TX 路径时 SetUp 需显式置 false,否则走 share-JFR 分支(`ut-gen/modules/umq.md` 陷阱 2)。
- **单用例 ≤1s 约束**: `FlushRx`/`FlushTx`/`WarmUp` 含轮询/定时器逻辑,测试用 mock 返回 + 短超时,不要真等。
- **`umq_epoll_ops.h` 现状**: 声明 `UmqEventPollOps : EventPollOps`,但 `EventPollOps` 基类在当前源码树中不存在(全树 grep 无声明,2026-08-28 复核),文件无任何引用方(上次改动 commit `af71118e`,2026-05-30 同步重构分支)。判断为重构遗留死代码,UT 规划中应排除(或单独向维护者确认是否删除)。
- **重复注册**: 新建测试 target 只挂在 `unit_test/umq/CMakeLists.txt`,不要复刻 `ubsocket_umq_setting_multi_level_test` 的顶层重复注册模式(顶层 `unit_test/CMakeLists.txt:169-201`)。
- **冻结红线**: `umq_errno_converter.*` 永不修改(AGENTS.md),测试已存在且达标,不产生任务。
- **RNR 宏名注意**: b87d6d4e 后软反压判定用 `UMQ_BUF_RNR_RETRY_CNT_EXC`(=99),fatal 分支仍是 `UMQ_BUF_RNR_RETRY_CNT_EXC_ERR`(无 `_FATAL` 后缀宏);写引用时不要用上版文档的 `_ERR_FATAL` 写法。
