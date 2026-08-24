# ubs-comm Agent Notes

## Project Overview

HCOM — 高性能通信库 (RDMA, TCP, SHM, UB transports), 命名空间 `ock::hcom`。UBSocket — 基于 UMQ 的 POSIX socket 适配子组件。C++ 标准: HCOM 以 C++11 编译, UBSocket 使用编译器默认 (C++17+) — 见 Known Gotchas。主要部署目标: aarch64 (Kunpeng 920/950); x86\_64 通过 `build.sh` 自动检测 (`uname -m`) 支持构建 (注意: E2E 场景仅 aarch64 可用, 见 `README.md:52`)。Mulan PSL v2 license。

## Build Commands

### HCOM (primary product)

```bash
./build.sh                                    # release build (default)
HCOM_BUILD_TYPE=debug ./build.sh              # debug build (required for tests)
HCOM_BUILD_TYPE=debug HCOM_BUILD_TESTS=on ./build.sh  # debug + test build
./build/generate_gtest_report.sh              # run hcom_ut + hcom_test, XML output
./build/generate_lcov_report.sh               # generate coverage report (lcov+genhtml)
```

测试工具需先安装: `./build/install_test_tools.sh` (安装 gtest 1.12.1 + mockcpp v2.7)。完整构建说明见 `README.md`。

### UBSocket (sub-component)

```bash
UMQ_BUILD=on UBSOCKET_BUILD=on ./build/build_umq_and_ubsocket.sh          # build only
UMQ_BUILD=on UBSOCKET_UT=on ./build/build_umq_and_ubsocket.sh             # build + run UT
UMQ_BUILD=on UBSOCKET_BUILD=on UBSOCKET_UT=on ./build/build_umq_and_ubsocket.sh  # full cycle
```

UBSocket UT 用 **ctest** 运行 (不是直接跑 gtest binary)。UMQ 必须先构建 — ubsocket 依赖 `libumq.so`。

ctest 从仓库根执行: `ctest --test-dir src/ubsocket/build --output-on-failure`

**直接运行单个 test binary**: binary 在 `src/ubsocket/build/`。除 `umq_errno_converter_test`、`profiling_test` (仅链 gtest\_main + mockcpp, 运行无需 libumq.so) 外,需设置 `LD_LIBRARY_PATH=src/hcom/umq/build/src:src/hcom/umq/build/src/qbuf`。

**构建选项补充:**

- `USE_URMA_STUB` (optional, default `OFF`): 无完整 URMA SDK 时设为 `ON`, 使用 `src/hcom/umq/stub/urma/` 下的 stub 头文件构建 UMQ。

### Bazel (alternative)

```bash
./build_bazel.sh                              # build with Bazel
HCOM_BUILD_TESTS=on ./build_bazel.sh          # falls back to CMake for UT (no Bazel UT targets)
```

## Test Commands

### 测试框架

- **mockcpp** (not GMock) — patching for ARM64 required; links against `fake_ibv_static` for RDMA mocking
- **GoogleTest** 1.12.1 — test runner
- HCOM tests: `-fno-access-control` 允许访问 private members
- `-DMOCK_VERBS` enables fake ibverbs stub
- Test fixture pattern: `class Test<Module> : public testing::Test`, `TEST_F(Test<Module>, <Scenario>)`
- HCOM test binaries: `hcom_ut` (UT), `hcom_test` (LLT)
- UBSocket test binaries (20): `unit_test/CMakeLists.txt` 注册 17 个 `ubsocket_*_test` (signal_handler/ring_buffer/core_types/setting_validator/ring_queue/queue_heap/scope_exit_ref/bitset_lock_seq/common_utils/leaky_singleton/buf_converter/set/obj_statistics/thread_pool/global_setting/version/port_cooldown) + `profiling/profiling_test` + `umq/umq_errno_converter_test`、`umq/umq_socket_connector_test`。注: `iobuf_block_cache_test` 在 release_0813 (d3e8f5ec, 2026-08-14) 新增, 尚未合入 master_0821
- 已删除: `umq_ops_errno_test`、`mock_infrastructure_test`、`iobuf_zcopy_adapter_test` — commit `61db74c0` 连同 `unit_test/stub/` 基础设施 (含 `fake_epoll_static` 等) 整体删除

### UT 约束

- **一律使用 mockcpp** (`MOCKER_CPP`/`MOCKER`) mock C API 和系统调用 (stub 方式已删除, 见上)
- **单用例执行 ≤1s**: 每个 `TEST_F` 用例的执行时间不超过 1 秒。禁止在测试中使用长时间 sleep、阻塞等待、密集计算循环等。如需等待异步事件, 使用短超时 (≤100ms) + 轮询。

### Coverage

```bash
UMQ_BUILD=on UBSOCKET_UT=on UBSOCKET_COVERAGE=on bash build/build_umq_and_ubsocket.sh
```

报告位置: `src/ubsocket/build/coverage_report/`, `coverage_summary.txt`, `coverage_detailed.txt`。覆盖度基线见下方 "Coverage Baseline (csrc UT)"。

### Skill 系统

opencode skill 系统位于 `.opencode/skills/`,指导 AI 生成符合项目约定的 UT 代码。详见 `.opencode/README.md`。

| Skill               | 触发关键词                                     | 覆盖模块                 |
| ------------------- | ----------------------------------------- | -------------------- |
| `ut-gen`            | UT, 单元测试, mockcpp, gtest                  | 通用UT模式(root skill)   |
| `ut-gen-umq`        | UMQ, umq, Share-JFR                       | `csrc/core/umq/`     |
| `ut-gen-core`       | epoll, SocketConnector, SocketAcceptor    | `csrc/core/`(不含umq/) |
| `ut-gen-common`     | GlobalSetting, LeakySingleton, ThreadPool | `csrc/common/`       |
| `ut-gen-under-api`  | DlApi, dlopen, dlsym                      | `csrc/under_api/`    |
| `ut-gen-profiling`  | ProfTracer, profiling                     | `csrc/profiling/`    |
| `ut-coverage-coord` | coverage, 进度, 认领                          | 协调层                  |

写模块 UT 时需同时加载 `ut-gen` + 对应子 skill。另有 `add-tracepoint` (SplitTrace 打点专用, 非 UT skill)。发现新模式/陷阱时按 skill "知识回流" 规则更新。

## Code Style

### 中英混写规则

**中英混写规则** — 适用于所有项目文档 (skill 文件、coverage 分析、进度跟踪、AGENTS.md 等):

| 内容类型            | 语言         | 示例                                                           |
| --------------- | ---------- | ------------------------------------------------------------ |
| 函数名、变量名、类名、宏    | 英文(保持代码原样) | `UmqErrnoConverter::Convert`, `umq_handle_`, `UMQ_ERR_EPERM` |
| 技术术语/工具名        | 英文(不翻译)    | mockcpp, gtest, epoll, UmqOperation, CMake, lcov             |
| 表格中技术列          | 英文         | UmqOperation列、Converter API列、Expression列                     |
| 代码块、CMake语法     | 英文(代码环境)   | `target_link_libraries(...)`、`MOCKER_CPP(::umq_poll)`        |
| 描述性语句(说明、解释、警告) | 中文         | "构造函数中调用OsAPiMgr静态方法,**必须在对象创建之前mock**"                      |
| 章节标题            | 中文         | "常见陷阱"、"高级模式"、"构建与运行"                                        |
| 代码块内注释          | 英文(不改)     | 代码本身英文环境,注释改中文造成割裂                                           |
| 列表项的描述部分        | 中文         | "- **必须mock** **`close`** — 析构函数调用close导致crash"              |
| 列表项的代码/技术部分     | 英文         | "`OsAPiMgr::close(m_fd)`"                                    |

**一句话原则: 看到代码符号就英文, 看到人话就中文。**

### C++ 风格

- **Indent**: 4 spaces (Google default is 2)
- **Line width**: 120 chars max (enforced by clang-format, `.clang-format` `ColumnLimit: 120`)
- **Braces**: function definitions Allman (newline), control statements K\&R
- **Pointer alignment**: right (`Type *name`)
- **No C-style casts** — use `static_cast` etc. (enforced, severity: critical)
- **`reinterpret_cast` 被 clang-tidy 白名单禁用** (部分历史代码仍在使用, 不在本次改动范围)
- **camelBack** for parameters and local variables; **CamelCase** for classes/methods; **UPPER\_CASE** for macros/enum constants; **g\_** prefix for global variables
- **Comments**: follow the style of existing code in the same file/module
- **License header**: every source file must have the Mulan PSL v2 header (see existing files for exact format, includes blank line after Copyright line)
- **Include ordering**: corresponding header first, then standard library, then project headers, then 3rdparty. `IncludeBlocks: Preserve` in clang-format — do NOT rely on auto-sort; manual ordering must respect dependency chains
- **Build flags**: release mode adds `-fvisibility=hidden -Werror -fstack-protector-strong`; debug/test adds `-rdynamic -fPIC`; test adds `-fno-access-control -fno-inline --coverage -DMOCK_VERBS`

## Dev Environment Tips

- **测试工具安装**: 首次构建/测试前运行 `./build/install_test_tools.sh` (gtest 1.12.1 + mockcpp v2.7)
- **HCOM 测试必须 debug 构建**: `HCOM_BUILD_TYPE=debug` — release 构建不带测试所需的 `-fno-access-control` 等标志
- **UBSocket 测试依赖**: UMQ 必须先构建 (`libumq.so`); 直接跑 test binary 需按 Build Commands 设置 `LD_LIBRARY_PATH`
- **clang-tidy 编译数据库**: 位于 `tmp_build_dir` (见 `.pre-commit-config.yaml` 和 `build.sh`)
- **覆盖度构建残留文件**: `brpc/` 目录已从源码树移除, 但 build 目录可能残留旧 `.gcno` 文件 (如 `ubs_mem/ring_buffer.cpp.gcno`) 引用不存在的头文件 (`ub_ring_buffer.h`), 导致 `genhtml` 报错。修复: `rm -rf src/ubsocket/build` 清干净重跑, 或 genhtml 已加 `--ignore-errors source` 容错。

## Architecture & Package Boundaries

| Directory                     | Component                 | Key Info                                                  |
| ----------------------------- | ------------------------- | --------------------------------------------------------- |
| `src/hcom/`                   | HCOM core library         | Main product. `ock::hcom` namespace. Entry: `hcom.h`      |
| `src/hcom/transport/`         | Transport implementations | `rdma/`, `sock/`, `shm/`, `ub/` — conditional compilation |
| `src/hcom/umq/`               | UMQ messaging queue       | Standalone CMake build. Outputs `libumq.so`               |
| `src/ubsocket/`               | UBSocket adapter          | Depends on UMQ. Outputs `libubsocket.so`                  |
| `src/ubsocket/csrc/core/umq/` | UMQ socket adapter ops    | `umq_errno_converter.h` (frozen — do NOT modify)          |
| `src/ubsocket/csrc/core/`     | 通用抽象层                     | `ubsocket_*` 文件, 不引用 `umq/` 头文件 (工厂类例外, 见依赖方向规则)                          |
| `test/hcom/`                  | HCOM tests                | `unit_test/`, `llt/`, `stub/`, `opensslcrt/`              |
| `src/ubsocket/unit_test/`     | UBSocket tests            | gtest+mockcpp, ctest runner                               |
| `doc/`                        | Documentation             | Design docs in `doc/ubsocket/` and `doc/hcom/`            |

### 依赖方向规则

`core/ubsocket_*` 是通用抽象层,当前支持 umq 通信方式,后续新增通信方式只需在 `core/` 下新增子目录即可,不需要改动通用层代码:

- **通用层** (`core/ubsocket_*.cpp/.h`) 只操作虚接口 (`AcceptorOps/ConnectorOps/DataTxOps/DataRxOps` 等), **禁止引用任何具体实现子目录的头文件** (当前为 `umq/`, 后续新增如 `urma/`、`posix_shm/` 等同理禁止)
- **工厂类例外** — `ubsocket_socket.cpp` (SocketType 工厂, 新增通信方式只需新增 `case` 分支) 与 `ubsocket_event_epoll.cpp` (EpollRunner 工厂, `new umq::UmqShareJfrEpollRunnerOps()` 等) 是通用层中仅有的两个允许引用具体实现子目录头文件的文件
- **具体实现层** (`core/umq/*.cpp/.h` 等) 可以引用通用层头文件

### UBSocket 架构参考

> **完整架构文档**: `doc/ubsocket/UBSOCKET-ARCHITECTURE.ch.md` (含 mermaid 图)

#### POSIX API 覆盖状态

**已实现 (14个)**: `socket`, `close`, `shutdown`, `bind`, `listen`, `accept`, `connect`, `readv`, `writev`, `getsockopt`, `setsockopt`, `epoll_create`, `epoll_ctl`, `epoll_wait`

**桩函数返回0 (13个)**: `accept4`, `send`, `recv`, `read`, `write`, `sendto`, `recvfrom`, `sendmsg`, `recvmsg`, `sendfile`, `sendfile64`, `epoll_create1`, `epoll_pwait` — 非 TCP 模式下返回 0 而非 -1+errno, 违反 POSIX 语义

**无 NATIVE\_TCP 守卫的桩函数 (4个)**: `fcntl`, `fcntl64`, `ioctl`, `sendmsg` — 即使 TCP 模式也返回 0 而非透传 libc

#### URMA 传输层状态

- **UrmaSocket** — 空壳类, 无任何成员/方法覆盖
- **UrmaWrapper** — `UrmaDevice/Context/Jfc/Jfs/Jfr/Jetty` 包装类已完整实现
- **UrmaApi** — 80+ 函数指针的动态加载封装 (`under_api/urma/dl_urma_api.h`)
- **DlApi::Load(LOAD\_URMA)** — 已预留加载标志位
- **EpollRunnerFactory** — 无 URMA 对应 runner case (当前仅 SHARE_JFR_RX / TRANSPORT_POOL_TX / TRANSPORT_POOL_EVENT 三个 UMQ runner)
- **SocketType 枚举** — 已含 `SOCK_TYPE_SHM` (无实现)

#### 关键目录补充

- `csrc/cli/` — CLI 诊断工具 (CLIClient/CLIArgsParser/TerminalDisplay), 通过 Unix socket 通信
- `csrc/iobuf/` — 零拷贝内存管理 (Block/BlockRef/BlockCache + UbsZcopyAdapter)
- `csrc/core/ubsocket_wakeup_event.h` — 异步 Accept 唤醒机制 (UbsocketWakeupEvent)
- `csrc/core/ubsocket_socket_helper.h` — TCP 工具类 (SocketConnHelper)
- `csrc/common/ubsocket_spsc_ring_queue.h` — 无锁 SPSC 环形队列 (AsyncEventPoll 使用)
- `csrc/common/ubsocket_qbuf_queue.h` — 动态扩缩容队列 (扩容 2 倍, 使用率 <=25% 缩容至 50%)

## Security Guidelines

**禁止以下行为 (红线规则):**

1. **禁止提交敏感信息**
   - API 密钥、密码、Token、证书私钥
   - 数据库连接字符串含凭证
   - SSH 私钥、GPG 密钥
2. **禁止硬编码凭证**
   - 用户名/密码
   - Access Key/Secret Key
   - 认证 Token
3. **禁止绕过安全检查**
   - 禁用 SSL/TLS 验证
   - 注释或删除安全相关代码
   - 关闭认证/授权机制
4. **禁止不安全日志**
   - 记录敏感数据 (密码、Token、个人信息)
   - 明文记录凭证

**必须遵守:**

- 使用环境变量或配置文件管理凭证 (配置文件需 `.gitignore`)
- 敏感操作需代码审查
- 定期轮换密钥和凭证
- 报告安全漏洞不公开披露

## Commit Guidelines

### Pre-commit Hooks

`.pre-commit-config.yaml` 在每次 commit 时运行: **全量构建** + clang-format + clang-tidy。非常耗时 — 这些是本地 hooks, 不是轻量检查。clang-tidy 使用 `-p=tmp_build_dir` 编译数据库 (源文件与测试文件相同, 见 `.pre-commit-config.yaml` 和 `build.sh`)。

### Commit 规范

**每次 commit 之前必须执行** `pre-commit run clang-format --all-files`, 确保格式合规后再提交。如 clang-format 修改了文件, 需 `git add` 后再 commit。

```bash
pre-commit run clang-format --all-files   # commit前必跑
git add -A                     # 如有格式修改, 重新暂存
git commit --no-verify -m "..." # 完整hook耗时过长(全量构建+clang-tidy), 仅依赖手动clang-format
```

clang-tidy 已启用白名单检查 (见 `.clang-tidy`, bugprone/cert/clang-analyzer/cppcoreguidelines 等)。`--no-verify` 只跳过完整 hook; 如需跑 clang-tidy, 可只对改动文件: `pre-commit run clang-tidy --files <file>`。

- **Co-Authored-By**: 当 AI 辅助生成代码时, commit 消息必须包含 `Co-Authored-By: <模型名> <模型邮箱>`, 标注实际使用的驱动模型 (如 `Co-Authored-By: GLM-5.1 <noreply@zhipuai.cn>`、`Co-Authored-By: Claude Opus 4 <noreply@anthropic.com>` 等), 根据实际情况填写

## Coverage Baseline (csrc UT)

> **详细分析**: `doc/ubsocket/UBSOCKET-COVERAGE-ANALYSIS.ch.md`

| 指标    | 基线               | 目标   | 缺口      |
| ----- | ---------------- | ---- | ------- |
| 行覆盖率  | 15.1% (850/5637) | ≥80% | +3650行  |
| 分支覆盖率 | 7.0% (479/6861)  | ≥50% | +2953分支 |

> 注: 15.1% 为 `doc/ubsocket/UBSOCKET-COVERAGE-ANALYSIS.ch.md` 中 "iobuf后" 列数值 (该文档口径基线为 11.1%), 即当前基线。

- 构建: `UMQ_BUILD=on UBSOCKET_UT=on UBSOCKET_COVERAGE=on bash build/build_umq_and_ubsocket.sh`
- 报告: `src/ubsocket/build/coverage_report/`, `coverage_summary.txt`, `coverage_detailed.txt`
- 79 个文件已纳入; 24 个未纳入 (urma 条件编译 + 纯声明头文件 + 无可执行行 .cpp)
- profiling 已达标 84.2%; core/umq 和 core/socket 是主力战场 (共 +2586行)

## 关键陷阱 (UT相关)

- `mockcpp::Result` vs `ock::ubs::Result` 类型冲突 → 用 `using namespace ock::ubs;` + 显式限定 `ock::ubs::Result`
- `IO_SIZE_MB` 是 `ubsocket_defines.h` 中的宏 → 测试中不可重定义为 constexpr
- `EidRegistry` 是 LeakySingleton → 测试中需 `UnregisterEid()` 清理
- `UmqBackend::UMQ_INITED` 是 static bool → SetUp 中设为 false 重置
- errno 必须在调用被测函数**之前**设置 (生产代码在 UMQ API 调用后立即保存 errno)
- lcov 1.16: 只用 `--ignore-errors gcov` (不支持 `unused,range`, 那是旧版 lcov 的)
- coverage filter: `'*/_deps/*' '/usr/include/*' '*/3rdparty/*' '*/unit_test/*' '*/tools/*'`
- lcov 必须加 `--initial --capture` 步骤才能纳入零覆盖率文件
- **`LibcApi::open`** **是 variadic**: mockcpp 无法 mock variadic 函数, 改用 `LibcApi::open_ptr` 函数指针直接替换 (`-fno-access-control`)
- **mockcpp** **`.stubs()`** **默认返回 Void**: 非 void 返回函数必须加 `.will(returnValue(...))`
- **`LibcApi::_ptr`** **初始为 nullptr**: 未调用 `LibcApi::Load()` 时通过 nullptr 函数指针调用 → segfault, SetUp 中必须设置 `_ptr` 或调用 `Load()`

## Known Gotchas

- HCOM 以 **C++11** (`-std=c++11`) 编译, 但 `.clang-format` 设 `Standard: c++17` — 格式化用 c++17 规则, 但 **HCOM** 代码必须兼容 c++11; UBSocket 生产代码未设 C++ 标准 (编译器默认 C++17+, 不受此约束), 其 UT 显式要求 `cxx_std_17`
- `IncludeBlocks: Preserve` 在 clang-format 中意味着自动排序不会重排 includes; 手动排序必须遵循依赖链 (如 `umq_data_tx_ops.h` 必须在 `umq_buf_converter.h` 之前, 因为后者用到 `umq_buf_t`)
- `SortIncludes: true` 实际上只在 Preserve 块内排序, 不是全局排序
- Bazel 构建没有 UT 目标; `HCOM_BUILD_TESTS=on` with Bazel 会回退到 CMake 跑测试
- **无 URMA SDK 时 UMQ 构建需** **`USE_URMA_STUB=ON`** — `umq_ub` 子模块依赖 `urma_api.h`/`uvs_api.h`, 默认从 `dist/hcom_3rdparty/umdk/urma/include/` 查找。若该目录不存在 (未安装完整 URMA SDK), 必须 `cmake -DUSE_URMA_STUB=ON ..`, 使用 `src/hcom/umq/stub/urma/` 下的 stub 头文件; 否则 `umq_ub` 编译报 `fatal error: urma_api.h: No such file or directory`
- **UMQ CMakeLists 默认** **`OPENSSL_ROOT_DIR`** **指向 Homebrew 路径** — UMQ 的 `CMakeLists.txt:32` 设为 `/usr/local/opt/openssl` (macOS Homebrew 默认), 在 Linux 上需手动覆盖。UMQ 为外部引入代码, 不做修改
- **TLS 测试证书有效期** — `test/hcom/opensslcrt/` 下的 X509 测试证书有有效期, 过期后 OpenSSL 3.0+ 会拒绝加载 (报 "TLS use certification file chain failed")。详见 `test/hcom/opensslcrt/README.md` 的重新生成方法
- Release 构建有 `-Werror`; debug/test 构建没有
- Pre-commit hooks 跑全量构建 — commit 会很慢
- `umq_errno_converter.h` 是冻结/final — 永远不要提议修改它
- **DpRearmTxInterrupt 成功路径不走 Convert**: `umq_rearm_interrupt` ret==0 时直接设 errno=EAGAIN 返回 -1, 不调用 Convert。仅 ret≠0 (失败) 走 Convert 路径
- **Share-JFR handle 变量语义陷阱**: `PrefillRx` 中本地变量 `umq_handle` 在 `UBS_ENABLE_SHARE_JFR=true` 时指向 `share_umq_handle_` (主UMQ), `false` 时指向 `umq_handle_` (子UMQ)。等 ready 逻辑应查 `umq_handle_` (子UMQ) — 因为子 UMQ 是刚创建的、需从 IDLE→READY; 主 UMQ 早已 ready。提取 `WaitUntilReady` 时若错误传入本地 `umq_handle` 而非 `umq_handle_`, share JFR 模式下会导致查错 handle。此陷阱适用于任何涉及主/子 UMQ 双 handle 的函数重构。
- **`UBS_ENABLE_SHARE_JFR`** **默认 true** — 测试环境若未显式关闭, PrefillRx 走主 UMQ 路径。重构时必须在两种模式下都验证。
- **冗余 GetItem 陷阱**: 当函数入参已持有 `SocketPtr`/`SocketBasePtr` 时, 不应再用 `ArraySet<Socket>::GetInstance().GetItem(fd_)` 重新查找 — 入参就是同一个对象的引用, 额外 GetItem 多一次 atomic load + IncreaseRef 且语义冗余。应直接用入参 (`sock`) 或函数内已计算的局部变量 (`sockBase`)。反面: `RefConvert<Socket,SocketBase>(ArraySet<Socket>::GetInstance().GetItem(fd_))`; 正面: `RefConvert<Socket,SocketBase>(sock)` 或复用已有的 `sockBase`。
- **`RPC_ADPT_FD_MAX`** **与** **`ArraySet::Capacity()`** **语义不同**: `RPC_ADPT_FD_MAX=8192` 是 `ubsocket_defines.h` 中编译时常量, 仅用于 `ProbeManager` 固定大小环形队列 (`mRecvQueue[RPC_ADPT_FD_MAX]`) — 不可替换为运行时 `Capacity()`, 因为静态数组大小必须编译时确定。`Capacity()` 是运行时动态值 (取 `min(rlim_cur, 65536)`), 供外部查询 fd 容量上限使用。
- **热路径不加日志**: `Init` 是一次性初始化可加日志; `GetItem/OverrideItem/RemoveItem/ForEach` 是高频热路径, 加日志会显著影响性能, 尤其 `ForEach` 遍历+回调场景。
- **mermaid 渲染陷阱**: 方括号 `[]` 在 `participant as` 别名中被解析为链接语法 (需改为纯文本如 `set_obj_idx`); 泛型尖括号 `<>`、Unicode 圆圈数字①②③④、emoji 如 ❌、特殊数学符号 ≤×、特殊箭头 ←→、HTML 实体 `&lt;&gt;` 均可能导致渲染失败。文档中 mermaid 图应只用纯 ASCII 英文+中文描述。
- **C++11** **`static constexpr`** **ODR-use 陷阱** (严重 — 导致链接 undefined reference): C++11 中模板类的 `static constexpr` 成员变量如果被 **ODR-use** (引用绑定), 需要类外提供定义, 否则链接报 undefined。典型场景: `std::min(const T&, const T&)` 的参数是引用, 直接传 `static constexpr` 变量会绑定引用 → ODR-use → 需要外部定义 → 未提供 → 链接错误。**修复方法**: `static_cast<类型>(CONSTEXPR_VAR)` 创建临时 rvalue, 引用绑定到临时值而非原变量 → 不再 ODR-use → 不需要外部定义 → 链接 OK。反面: `std::min(x, FD_CAPACITY_HARD_LIMIT)`; 正面: `std::min(x, static_cast<uint32_t>(FD_CAPACITY_HARD_LIMIT))`。C++17 的 `inline constexpr` 自动解决此问题 (隐式提供定义), 但 HCOM 以 C++11 编译, **HCOM 代码中所有模板类** **`static constexpr`** **成员传给引用参数函数 (如** **`std::min/max/clamp`、日志函数的** **`%u`** **格式化参数等) 都必须加** **`static_cast`** **避免 ODR-use**。
- **版本协商** **`mismatch_version`** **陷阱**: `AcceptNegotiate` 的 Major mismatch 分支中, `mismatch_version` 用于通知 client 降级。**绝对不能发硬编码的** **`0`** — Major=0 的 client (0.1.0/0.2.0) 会误判为兼容, 继续等待 NegotiateRsp 直到超时。正确做法: 发 `UBS_PROTOCOL_VERSION` (server 自身版本) — 因为 server 只在 `peer_major≠local_major` 时进入此分支, client 收到后 Major 必然不匹配。
- **TFO 数据残留陷阱**: TFO 模式下 SYN 携带 `[magic][version][body_len][body]`。Server 在 Major mismatch 时已消费 magic+version (12B), 剩余 `body_len+body` 仍在 socket 缓冲区。**必须在** **`return`** **前消费这些残留字节**, 否则后续 brpc 接管 fd 后会读到垃圾。修复: 读 wire 上的 `body_len` (4B) 然后精确丢弃 `body_len` 字节。**不要用** **`FlushSocketMsg`** — 非阻塞 fd 无数据时可能死循环 (`while(received>0)` 循环内 `errno==EAGAIN` 时 `continue` 回到 `recv`, 无限重复)。
- **`RecvLengthPrefixed`** **跨版本兼容**: body\_len 来自对端 wire, `min(body_len, obj_size)` 读 body, 不足零填、超出丢弃。保证不同 struct 大小 (如 0.1.0 无 `reserved_version` vs 0.2.0 有) 两端可互通。**`NEGOTIATE_REQ_WIRE_SIZE`** **自动跟随** **`sizeof(NegotiateReq)`**, struct 变更后不需手动维护常量。
- **`UBSVersion::Negotiate`/`ValidateNegotiated`** **纯函数** (旧名 `NegotiateVersion`/`ValidateNegotiatedVersion`): 两函数仅做位运算, 不依赖 `UBS_PROTOCOL_VERSION` 常量 (`local_version` 是入参)。UT 中可构造任意版本组合测试, 无需重编。
- **版本号来源**: `UBSOCKET_VERSION` 文件 (如 `0.1.0`) → CMake `config_version.cmake` 解析 → `configure_file` 生成 `ubsocket_version_defs.h` → `ubsocket_version.h` include 后定义 `UBS_PROTOCOL_VERSION` constexpr。(无 Bazel 等价机制 — 本仓无 genrule, ubsocket 无 Bazel 构建文件)。**全代码仓统一一个版本号文件**。
- **`ubsocket_uninit()`** **EpollRunner 停止顺序陷阱** (严重 — 退出时报 `mempool ... tseg not exist` 或空指针崩溃): `EpollRunner<T>` 是 `LeakySingleton`, 进程退出不会自动析构、后台 poller 线程不会自动 join, 会持续对 umq 做 `umq_poll/umq_post/umq_get_cq_event`。若 `umq_uninit()` (即 `UmqBackend::UnInit()`) 先执行, 释放了 umq/mempool/tseg, runner 线程再 poll 就触发底层 umq/urma 库报错 `mempool <id> tseg not exist` (该串不在本仓, 来自底层库, 是"释放后仍 poll"的特征)。**必须在** **`umq_uninit`** **之前** **`Stop()`** **掉三个 runner** (SHARE\_JFR\_RX\_RUNNER / TRANSPORT\_POOL\_TX\_RUNNER / TRANSPORT\_POOL\_EVENT\_RUNNER)。**但 stop 位置不能放最前**: socket 析构链 `UmqSocket::~UmqSocket→UnInitialize()` (`umq_socket.cpp`) 会调 `EpollRunnerFactory::GetInstance(TRANSPORT_POOL_TX_RUNNER).DelEpollEvent(...)`, 而 `EpollRunner::Stop()` 会置 `ops_=nullptr`/`epoll_fd_=-1` (`ubsocket_event_epoll.cpp`), `DelEpollEvent` 里 `ops_->DelEpollEvent(...)` 会空指针崩溃。**正确顺序**: `TxCqePoller::Stop()` → `ArraySet<Socket>::ReleaseAll()` → `ArraySet<EventPoll>::ReleaseAll()` → 三个 runner `Stop()` → `UmqBackend::UnInit()`。`Stop()` 幂等且对未 Start 的 runner 是安全 no-op (`exit_efd_<0` 直接 return), 可无条件调用。**注意仅覆盖显式调用** **`ubsocket_uninit()`** **的路径**; 用户不调用时 LeakySingleton 线程仍会在 static destruction 阶段以不确定顺序退出 (既有设计权衡)。

### SplitTrace/Profiling tracepoint 陷阱

以下陷阱覆盖 Write 侧、Read 侧、poller 线程、结构变更四种场景, 均源于实际调试经历。

#### 陷阱1: 本地变量名与已有变量冲突

在函数中段新增变量时, 需检查后续是否有同名变量。

反面 (编译错误 `conflicting declaration 'int64_t ret'`):

```cpp
// data_tx.cpp WriteV() 中
int ret = tx_ops_->PollTx(sock);        // 新增的行65
...
int64_t ret = tx_ops_->PostSend(...);   // 原有的行113 — 冲突!
```

正面:

```cpp
int poll_ret = tx_ops_->PollTx(sock);   // 用独立命名避免冲突
```

#### 陷阱2: 用错 buffer (Read vs Write)

Write 路径必须进 `write_bufs_`, Read 路径进 `read_bufs_`。

正面:

```cpp
// Write 侧: trace->AddWriteTrace(...)
// Read 侧:  trace->AddReadTrace(...)
```

#### 陷阱3: UpdateWriteFirstTrace 硬编码相邻位, 多 tracepoint 插入后全部 seq=0

`UpdateWriteFirstTrace` 最初检查 `core_write_pos+1` 是否为 `CORE_WRITE_POLL_TX`。当我们在 `CORE_WRITE` 和 `CORE_WRITE_POLL_TX` 之间插入 4 个新 tracepoint (UMQ\_POLL/DECREF/FREE/POLL\_CQE) 后, `core_write_pos+1` 不是 type=15, 所有新类型 + type=15 的 seq 都是 0。

**最终修复**: forward-scan — 从 `core_write_pos+1` 向前扫描, 对所有 `seq_no==0` 的条目回填 seq\_no/data\_size/offset, 遇到 `seq_no!=0` 则停。

#### 陷阱4: 头文件签名与 .cpp 不一致 (out\_decref\_ns)

添加参数后忘记同步 .cpp 定义 → Bazel/GCC release 构建报 `no declaration matches`。

**规则**: 头文件和 .cpp 每次参数变更后对比签名。

#### 陷阱5: RxDataSet 内部子操作打点需要 trace/fd 透传

`RxDataSet` 内部调用的 `RearmRxInterrupt()` 和 `recv(MSG_PEEK)` 无法直接访问 SplitTrace — 当前分支签名仅 `RxDataSet(void *buf, uint32_t size)` (ubsocket_data_rx.h:36), 打点需扩展签名传参 (trace 透传改动 6ba83957 未合入 master_0821; 枚举 `CORE_READ_RX_DATA_SET_REARM/RECV` 存在但零引用)。

正面 (若需打点): 扩展签名为 `ssize_t RxDataSet(void *buf, uint32_t size, SplitTrace *trace = nullptr, int raw_socket = -1)` (参考未合入的 6ba83957)。

#### 陷阱6: 新增字段到 SplitTraceInfo → 重载歧义 (bitfield)

新增 `poll_num` 字段后在 `AddReadTrace` 4-param 重载加默认参数, 导致与已有 5-param 重载对 bitfield 实参产生歧义。

修复: 对 bitfield 实参 `static_cast<uint32_t>(buf_pro->imm.user_data)` 消歧。

#### 陷阱7: Poller 线程产生 trace 爆炸

`TxCqePoller` 调用 `DoUmqTxPoll → PollUmqTxInternal` 时会走 `sock->split_trace_` 写 SplitTrace, 周期产生海量 trace + clock\_gettime 开销 (轮询周期 **100ms**, timerfd 100'000'000 ns)。

修复: poller 线程通过线程局部 `SplitTrace::SuppressTrace()` 抑制打点 (`ubsocket_tx_cqe_poller.cpp:164`、`umq_tx_helper.cpp:25`)。

#### 陷阱8: ProfilingTPId 枚举新增值必须追加在 UBSOCKET\_PROF\_COUNT 之前

所有新 enum 值放在 `UBSOCKET_PROF_COUNT` 之前, 保持已有类型号不变。

## Agent skills

### Issue tracker

Issues are tracked as local markdown files under `.scratch/<feature-slug>/`. 约定见 `docs/agents/issue-tracker.md` — 本仓尚无 `.scratch/` 用例, 启用时按约定创建。

### Triage labels

Five canonical roles, label strings equal to their names (`needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`). See `docs/agents/triage-labels.md`.

### Domain docs

Single-context layout — one `CONTEXT.md` + `docs/adr/` at repo root (本仓尚未创建; 不存在时按 domain.md 规则静默处理). See `docs/agents/domain.md`.
