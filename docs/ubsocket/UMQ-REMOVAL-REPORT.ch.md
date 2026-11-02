# ubsocket 与 umq 解耦专项分析报告

> 分支：`br_urma_remove_umq_20261020`（基于 `br_urma_rw_0728`，base = `0a77d92a`）
> 日期：2026-10-20
> 执行环境：Windows 本地（代码清理）+ 远端 Linux aarch64 编译机 `192.168.41.89:/home/lhj/ubs-comm`（全量验证）

## 1. 操作背景与边界说明

### 1.1 背景

`src/hcom/umq/`（libumq.so）是基于底层 `liburma.so` 封装的高层消息队列库，属 **hcom 模块**；`src/ubsocket/csrc/core/umq/` 是 ubsocket 对 umq 的传输适配层。urma 能力为 umq 能力超集。此前已完成 umq 与 urma 的编译期解耦（`#ifdef UMQ_BACKEND_ENABLED` 宏隔离，记录见历史文档 `URMA-DECOUPLE-PLAN.ch.md`）。

本次任务在解耦基础上，将 **ubsocket 内部的 umq 适配层整体移除**，ubsocket 收敛为 **urma-only 后端**。

### 1.2 任务边界（经确认）

| 范围 | 处理 |
|------|------|
| `src/ubsocket/`（含 `csrc/core/umq/`、`under_api/dl_umq_api.*`、umq UT 等） | umq 全部移除，urma-only 收敛 |
| `build/`（umq+ubsocket 联合构建脚本） | 移除（ubsocket 不再与 umq 联合构建） |
| `.opencode/skills/ut-gen-umq/` | 移除（ubsocket 侧 umq UT skill） |
| **hcom 模块**（`src/hcom/`、`doc/hcom/`、`test/hcom/`、`hcom.spec`、`.pre-commit-config.yaml`、`WORKSPACE`） | **保持基线 `0a77d92a` 原样，不做任何改动**（`src/hcom/umq/` 作为 hcom 子模块继续存在） |

## 2. ubsocket 侧 umq 资源清单与删除内容

### 2.1 物理删除的目录/文件

| 路径 | 内容 |
|------|------|
| `src/ubsocket/csrc/core/umq/` | ubsocket UMQ 适配层（backend/socket/connector/acceptor/data ops/epoll ops/errno converter/setting 等 37 文件） |
| `src/ubsocket/csrc/under_api/dl_umq_api.h/.cpp`、`umq_api.h` | libumq dlopen 加载器 |
| `src/ubsocket/csrc/common/ubsocket_port_cooldown.h/.cpp` | 仅 core/umq 引用的端口冷却（grep 复核后删） |
| `src/ubsocket/csrc/profiling/probe/probe_manager.h` | umq 专属 probe 探测 |
| `src/ubsocket/3rdparty/umq/` | 孤儿 INTERFACE 库定义（全仓无 add_subdirectory 引用） |
| `src/ubsocket/unit_test/core/umq/` | umq UT（2 测试 + CMakeLists） |
| `build/build_umq_and_ubsocket.sh`、`build/sync_umq.sh` | umq+ubsocket 联合构建入口与同步工具 |
| `.opencode/skills/ut-gen-umq/` | UMQ 专属 UT skill |

### 2.2 源码残留清理（urma-only 收敛）

| 类别 | 文件与改动 |
|------|-----------|
| 动态加载收敛 | `under_api/dl_api.h` 删 `LOAD_UMQ`；`dl_api.cpp` 删 umq 加载/卸载分支，仅保留 libc + urma |
| 锁注册收敛 | `common/ubsocket_lock.cpp` 删 umq 头引用与 umq 外部锁注册块 |
| CLI 收敛 | `cli/cli_args_parser.*`、`cli_client.*`、`cli_main.cpp`、`cli_terminal_display.*` 仅保留 stat/delay 命令，删除 umqinfo/umq/topo/flowcontrol/qbuf/io/probe；`profiling/statistics/cli_message.h` 删 umq 命令枚举与结构体 |
| probe 清除 | `ubsocket.cpp` 删 probe_manager 引用；`common/ubsocket_global_setting.*` 删 `UBS_PROBE_*` 环境变量 |
| 类型/常量收敛 | `umq_trans_mode_t`→`UbsTransMode`（`ubsocket_defines.h` 新增，`namespace ock::ubs`）；`UMQ_INVALID_HANDLE`→`UBS_INVALID_HANDLE`；`SOCK_TYPE_UMQ` 枚举删除；`ubsocket_core_types.cpp` 字符串表收敛为 `{"TCP","SHM","URMA"}` |
| 错误码/打点/统计 | `ubsocket_errno.h` 删 `UBS_UMQ_*`/`UBS_NO_MAIN_UMQ`/`UBS_UMQ_ERROR`；`ubsocket_prof.h` 删 `CORE_WRITE_UMQ_*`/`UMQ_BIND_*` 全部 umq 打点；`ubsocket_obj_statistics.h` 删 `UBS_UMQ_SOCKET` |
| epoll/socket 收敛 | `ubsocket_event_epoll.h/.cpp`、`ubsocket_epoll.cpp`、`ubsocket_socket.cpp/.h`、`ubsocket_socket_acceptor.*`、`ubsocket_socket_connector.h` 删 umq 分支/守卫死代码 |
| statistics 收敛 | `statistics.h/.cpp`、`statistics_statsmgr.h`、`ubsocket_print_stats_mgr.h` 删 umq 头引用与类型替换 |

### 2.3 构建系统清理

| 文件 | 改动 |
|------|------|
| `src/ubsocket/CMakeLists.txt` | 删 `BUILD_UMQ_*` option 与互斥校验、umq 宏定义、adapter 自动构建块、hcom include 路径；`BUILD_URMA_DLOPEN_BACKEND` 强制 ON（否则 FATAL_ERROR） |
| `src/ubsocket/csrc/CMakeLists.txt` | 删 core/umq GLOB 与 umq 链接依赖 |
| `build.sh` | 保留 hcom 主构建（hcom 模块含 umq，未动）；ubsocket 附加构建改为 urma-only 直连 cmake，保持 `UBSOCKET_BUILD/UT/COVERAGE` 环境变量兼容 |
| `src/BUILD.bazel` | 删 `libumq`/`libumq.so`/`ubsocketall` target 与 `ubsocket`/`ubstat`/`release_package` 对 umq 的 deps/includes（ubsocket 不再依赖 `//src/hcom/umq:*`） |
| `unit_test` 各 CMakeLists（4 个）与 `test/ubsocket/ut/CMakeLists.txt` | 删 `add_subdirectory(core/umq)` 与 `${CMAKE_SOURCE_DIR}/src/hcom/umq/include/umq` include 路径 |

### 2.4 文档与 skills 清理

- **重度改写（urma-only）**：`AGENTS.md`、`src/ubsocket/README_EN.md`、`UBSOCKET-ARCHITECTURE.ch.md`、`UBSOCKET-USER-GUIDE.md`、`UBSOCKET-CLAIMING.md`、`UBSOCKET-COVERAGE-ANALYSIS.ch.md`、`UBSOCKET-BRPC-ERRNO-MAPPING.ch.md`（改写为 `UrmaErrnoConverter` 实际映射表）、`UBSOCKET-CLIENT/SERVER-ASYNC-CONN-DESIGN.ch.md`、`URMA-BUILD-GUIDE.md`、`UBSOCKET-FLOW-CONTROL.md`
- **架构图**：`figures/ubsocket arch.drawio` 删除 2 个 UMQ 节点
- **.opencode skills**：`README.md`、`ut-gen`、`ut-gen-under-api`（改写为 DlApi/LibcApi/UrmaApi 版）、`ut-gen-core`、`ut-coverage-coord`、`add-tracepoint`（示例对齐现行 urma 打点）
- **历史文档加注**：`URMA-DECOUPLE-PLAN.ch.md`、`URMA-DEV-STATUS.ch.md` 顶部注明"ubsocket 侧 umq 适配层已移除，本文为历史记录"，正文不动

### 2.5 ubsocket 侧 umq 引用终态

`git grep -E "umq_|Umq|UMQ_" -- src/ubsocket test/ubsocket` 仅剩白名单：

| 文件 | 原因 |
|------|------|
| `src/ubsocket/tools/extract_timesstamps.py` | 历史 SplitTrace 日志离线分析工具（针对已删除的 ubsocket umq 打点日志格式），独立工具不影响构建/运行/符号，可解析存量历史日志，予以保留 |
| 本报告、`URMA-DECOUPLE-PLAN.ch.md`、`URMA-DEV-STATUS.ch.md` | 记录本身与历史文档注记 |

hcom 模块（`src/hcom/umq/`、`doc/hcom/umq/`、`hcom.spec` 等）按边界保持原样，其 umq 引用不在本次清理范围。

## 3. 执行步骤与提交说明

本次变更为单个 commit（`0393345f`，115 文件，+1228/-18175），按以下步骤执行后整理归并：

1. **ubsocket 侧 umq 物理删除**：`csrc/core/umq/`（37 文件适配层）、`under_api/dl_umq_api.*`/`umq_api.h`、仅 umq 引用的 `ubsocket_port_cooldown.*`、umq 专属 `profiling/probe/probe_manager.h`、孤儿 `3rdparty/umq/`、`unit_test/core/umq/`、联合构建脚本 `build/build_umq_and_ubsocket.sh`/`build/sync_umq.sh`、`.opencode/skills/ut-gen-umq/`
2. **源码残留清理（urma-only 收敛）**：见 2.2 节（动态加载/锁注册/CLI/probe/类型常量/错误码/打点/统计/epoll/socket）
3. **构建系统清理**：见 2.3 节（CMake/bazel/build.sh/unit_test），含编译期反馈的 `UbsTransMode` 引入修复
4. **文档与 skills 清理**：见 2.4 节（重度改写 10 篇 + 架构图 + skills ×6 + 历史文档加注 2 篇）
5. **专项报告**：新增本报告，含远端全量验证结果回填

说明：执行过程中曾将删除范围扩大至 hcom 模块（src/hcom/umq 等），经边界确认后已恢复基线，最终提交不包含任何 hcom 模块改动。

## 4. 全量验证测试结果（远端 192.168.41.89，Linux aarch64）

### 4.1 编译验证

| 项 | 命令 | 结果 |
|----|------|------|
| ubsocket urma-only release | `cmake -S src/ubsocket -B build -DBUILD_URMA_DLOPEN_BACKEND=ON` + `cmake --build -j$(nproc)` | **通过**（100% Built target：ubsocket_shared/ubsocket_static/golden/allocator_tool） |
| ubsocket Debug + UT | `-DCMAKE_BUILD_TYPE=Debug -DUBSOCKET_BUILD_TESTS=ON` | **通过**（0 error，22 个 UT binary 全部构建成功） |
| hcom 主链路全量（含恢复后的 hcom/umq） | `bash build.sh` | **通过**（`build.sh succeeds`，含 hcom 主产品编译 + install + 打包） |

编译期发现的 2 处遗漏（均为解耦期类型替换的 include 缺失，已修复并入库）：
1. `statistics_statsmgr.h` 使用 `UbsTransMode` 未引入 `common/ubsocket_defines.h`
2. `namespace Statistics` 内无限定使用 `UbsTransMode`（定义于 `namespace ock::ubs`），在 `statistics_statsmgr.h`、`ubsocket_print_stats_mgr.h` 的 namespace 内补 `using ock::ubs::UbsTransMode;`

> 注：以上 1-3 项验证在"umq 整删"边界下完成；边界修正恢复 hcom 后已重新执行全量验证（见 4.5 节补充记录）。

### 4.2 nm 符号验证（libubsocket.so）

```
$ nm -D csrc/libubsocket.so | grep -i umq   → 0 命中
$ nm    csrc/libubsocket.so | grep -i umq   → 0 命中
```

### 4.3 单元测试（ctest，22 个 binary）

```
95% tests passed, 1 tests failed out of 22
```

| # | 测试 | 结果 |
|---|------|------|
| 1-7 | ubsocket_signal_handler / epoll / fd_table / send_recv / conn_buffer / conn_ops_base / conn_reactor | Passed |
| 8-17 | urma_errno_converter / readable_event_queue / socket / acceptor / connector / alive_manager / conn_ops / exchange / data_rx / data_tx | Passed |
| 18 | urma_event_epoll_test | **Failed（2 用例，基线既有）** |
| 19-22 | urma_socket_buf / allocator / block_cache / profiling | Passed |

**失败用例基线对比**：在远端以 `git worktree` 检出基线 `0a77d92a`，构建并运行同一 UT，失败用例完全一致：
- `UrmaEventPollTest.EpollWait_LtMode_ClosedSocket_NoEventfdRewrite`
- `UrmaEventPollDispatcherTest.Jfc_Destroy_DrainsAndAcksResidualEvents`（期望 JFC 排空 ack/poll/delete mock 调用，实际 0 次调用）

结论：该 2 个用例为 `br_urma_rw_0728` 基线既有失败（与 JFC 销毁排空逻辑的已知缺陷相关），**非本次变更引入**，本次变更前后 UT 结果一致。

### 4.4 urma 核心业务场景回归

按确认的回归口径：以 UT 集合作为 urma 回归标准。urma 核心链路 UT 全部通过——
`urma_socket_test`（建链/收发主链路）、`urma_acceptor_test`、`urma_connector_test`、`urma_data_tx_test`、`urma_data_rx_test`、`urma_conn_ops_test`、`urma_exchange_test`、`urma_alive_manager_test`、`urma_errno_converter_test`、`urma_readable_event_queue_test`、`urma_socket_buf_test` 均 Passed。

### 4.5 边界修正后补充验证（hcom 恢复基线 + 3c3aeed6）

| 项 | 结果 |
|----|------|
| `bash build.sh` 全量（hcom 主构建含恢复的 `src/hcom/umq` + ubsocket urma-only） | **通过**（`build.sh succeeds`，install 完成） |
| ubsocket Debug+UT 重建 | **通过**（0 error，22 个 UT binary 全部构建成功） |
| ctest | **21/22 通过（95%）**，唯一失败仍为 `urma_event_epoll_test`（基线既有，见 4.3） |
| nm 符号 | `nm -D` 与 `nm` 全符号 `grep -i umq` 均 **0 命中** |

结论：hcom 模块恢复基线后，全链路（hcom 主构建含 libumq + ubsocket urma-only + UT + 符号）验证结果与边界修正前完全一致。

## 5. 变更影响范围说明

| 影响项 | 说明 |
|--------|------|
| ubsocket umq 后端 | **永久移除**。`BUILD_UMQ_DLOPEN_BACKEND`/`BUILD_UMQ_ADAPTER_BACKEND` 等 ubsocket CMake option 删除；`BUILD_URMA_DLOPEN_BACKEND=OFF` 会触发 FATAL_ERROR |
| hcom 模块 | **零改动**（恢复基线）。`src/hcom/umq/`（libumq.so）继续随 hcom 主构建编译与发布；hcom.spec、WORKSPACE、.pre-commit 均为基线原样 |
| ubsocket CLI 命令 | `ubstat` 仅保留 `stat`/`delay`；`umqinfo`/`umq`/`topo`/`flowcontrol`/`qbuf`/`io`/`probe` 命令移除 |
| ubsocket UT | `umq_errno_converter_test`、`umq_socket_connector_test` 等移除；现存 22 个 binary（urma 系列为主） |
| bazel | `src/BUILD.bazel` 中 ubsocket 对 `//src/hcom/umq:*` 的依赖与 `libumq`/`ubsocketall` target 删除；`src/hcom/BUILD.bazel` 为基线原样。bazel 仅做静态清理（远端无 bazel 环境，未实测） |
| 联合构建脚本 | `build/build_umq_and_ubsocket.sh`、`build/sync_umq.sh` 移除（ubsocket 不再与 umq 联合构建；hcom/umq 自身构建走 hcom 主构建） |
| 对外 API | `libubsocket.so` 符号面无 umq 残留（nm 验证）；对外 C API（`ubsocket_*`）不受影响 |
| 环境变量 | ubsocket 侧 `UBS_PROBE_ENABLED/MS/BATCH`、`LEVEL_UMQ`、`UBSOCKET_UMQ_TINY_POOL_*` 等读取逻辑删除 |
| 文档/skills | ubsocket 侧文档全部收敛 urma-only；历史文档（DECOUPLE-PLAN/DEV-STATUS）加注保留 |

## 6. 后续迭代注意事项

1. **能力补齐待办**（原 ubsocket umq 后端提供的等价能力，urma 侧按需补）：
   - CLI 诊断命令：原 topo/flowcontrol/qbuf/io/probe 展示能力已随 ubsocket umq 后端移除，若需要，可基于 urma 现有统计（stat/delay）扩展
   - probe 探测、port cooldown、transport pool 预热等 umq 专属机制未迁移（urma 架构下无需或另有实现）
2. **基线既有失败**：`urma_event_epoll_test` 的 2 个失败用例（JFC 销毁排空相关）在基线即失败，与 `br_urma_jfc_ack_fix_20261020` 已回退的排空修复相关，建议后续单独跟进修复
3. **bazel 验证**：本次仅静态清理 `src/BUILD.bazel`（ubsocket target），建议在具备 bazel 环境的机器上跑 `bazel build //src:libubsocket.so --define hcom_enable_ub=1` 复核
4. **extract_timesstamps.py**：保留的历史日志分析工具，其 umq 解析分支对应旧日志格式，新打点日志不会产生 umq 条目；后续如不再需要解析存量日志可整体删除
5. **hcom/umq 与 ubsocket 的关系**：`src/hcom/umq`（libumq.so）仍依赖 urma 并保持原有对外能力，但其下游 ubsocket 适配层已移除——hcom/umq 的独立使用方不受影响；若后续需要在 ubsocket 侧恢复 umq 传输，需重新引入适配层
6. **分支合并**：本分支基于 `br_urma_rw_0728`（`0a77d92a`）独立演进，合并回主干前建议先同步 `br_urma_rw_0728` 最新提交做一次 rebase 预演；`br_urma_remote_seg_reimport_fix_20261020`（e072c43b）的修复不在本分支，需另行合入
