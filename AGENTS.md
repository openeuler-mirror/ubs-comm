# ubs-comm Agent Notes

## Product

HCOM — high-performance communication library (RDMA, TCP, SHM, UB transports). Namespace: `ock::hcom`. C++11 compiled, aarch64 (Kunpeng) target. Mulan PSL v2 license.

## Build Commands

### HCOM (primary product)
```bash
./build.sh                                    # release build (default)
HCOM_BUILD_TYPE=debug ./build.sh              # debug build (required for tests)
HCOM_BUILD_TYPE=debug HCOM_BUILD_TESTS=on ./build.sh  # debug + test build
./build/generate_gtest_report.sh              # run hcom_ut + hcom_test, XML output
./build/generate_lcov_report.sh               # generate coverage report (lcov+genhtml)
```

测试工具需先安装: `./build/install_test_tools.sh` (安装 gtest 1.12.1 + mockcpp v2.7)。

### UBSocket (sub-component)
```bash
UMQ_BUILD=on UBSOCKET_BUILD=on ./build/build_umq_and_ubsocket.sh          # build only
UMQ_BUILD=on UBSOCKET_UT=on ./build/build_umq_and_ubsocket.sh             # build + run UT
UMQ_BUILD=on UBSOCKET_BUILD=on UBSOCKET_UT=on ./build/build_umq_and_ubsocket.sh  # full cycle
```

UBSocket UT使用**ctest**运行(不是直接跑gtest binary)。UMQ必须先构建——ubsocket依赖`libumq.so`。

### Bazel (alternative)
```bash
./build_bazel.sh                              # build with Bazel
HCOM_BUILD_TESTS=on ./build_bazel.sh          # falls back to CMake for UT (no Bazel UT targets)
```

### 直接运行单个ubsocket UT binary
构建时加`UBSOCKET_BUILD_TESTS=ON`后，binary在`src/ubsocket/build/`。直接运行：
```bash
cd src/ubsocket/build
./umq_errno_converter_test
./umq_ops_errno_test
```
也可用ctest: `ctest --test-dir build --output-on-failure`

## Documentation Language Convention

**中英混写规则** — 适用于所有项目文档(skill文件、coverage分析、进度跟踪、AGENTS.md等):

| 内容类型 | 语言 | 示例 |
|----------|------|------|
| 函数名、变量名、类名、宏 | 英文(保持代码原样) | `UmqErrnoConverter::Convert`, `umq_handle_`, `UMQ_ERR_EPERM` |
| 技术术语/工具名 | 英文(不翻译) | mockcpp, gtest, epoll, UmqOperation, CMake, lcov |
| 表格中技术列 | 英文 | UmqOperation列、Converter API列、Expression列 |
| 代码块、CMake语法 | 英文(代码环境) | `target_link_libraries(...)`、`MOCKER_CPP(::umq_poll)` |
| 描述性语句(说明、解释、警告) | 中文 | "构造函数中调用OsAPiMgr静态方法，**必须在对象创建之前mock**" |
| 章节标题 | 中文 | "常见陷阱"、"高级模式"、"构建与运行" |
| 代码块内注释 | 英文(不改) | 代码本身英文环境，注释改中文造成割裂 |
| 列表项的描述部分 | 中文 | "- **必须mock `close`** — 析构函数调用close导致crash" |
| 列表项的代码/技术部分 | 英文 | "`OsAPiMgr::close(m_fd)`" |

**一句话原则: 看到代码符号就英文，看到人话就中文。**

- **Indent**: 4 spaces (Google default is 2)
- **Line width**: 120 chars max (enforced by clang-format and clang-tidy G.FMT.05-CPP)
- **Braces**: function definitions Allman (newline), control statements K&R
- **Pointer alignment**: right (`Type *name`)
- **No C-style casts** — use `static_cast` etc. (enforced, severity: critical)
- **No `reinterpret_cast`** in clang-tidy whitelist (but used in some legacy code)
- **camelBack** for parameters and local variables; **CamelCase** for classes/methods; **UPPER_CASE** for macros/enum constants; **g_** prefix for global variables
- **Comments**: do not add any unless explicitly requested
- **License header**: every source file must have the Mulan PSL v2 header (see existing files for exact format, includes blank line after Copyright line)
- **Include ordering**: corresponding header first, then standard library, then project headers, then 3rdparty. `IncludeBlocks: Preserve` in clang-format — do NOT rely on auto-sort; manual ordering must respect dependency chains
- **Build flags**: release mode adds `-fvisibility=hidden -Werror -fstack-protector-strong`; debug/test adds `-rdynamic -fPIC`; test adds `-fno-access-control -fno-inline --coverage -DMOCK_VERBS`

## Testing Framework

- **mockcpp** (not GMock) — patching for ARM64 required; links against `fake_ibv_static` for RDMA mocking
- **GoogleTest** 1.12.1 — test runner
- HCOM tests: `-fno-access-control` allows accessing private members
- `-DMOCK_VERBS` enables fake ibverbs stub
- Test fixture pattern: `class Test<Module> : public testing::Test`, `TEST_F(Test<Module>, <Scenario>)`
- HCOM test binaries: `hcom_ut` (UT), `hcom_test` (LLT)
- UBSocket test binaries: `umq_errno_converter_test`, `umq_ops_errno_test`, etc.

## Architecture & Package Boundaries

| Directory | Component | Key Info |
|-----------|-----------|----------|
| `src/hcom/` | HCOM core library | Main product. `ock::hcom` namespace. Entry: `hcom.h` |
| `src/hcom/transport/` | Transport implementations | `rdma/`, `sock/`, `shm/`, `ub/` — conditional compilation |
| `src/hcom/umq/` | UMQ messaging queue | Standalone CMake build. Outputs `libumq.so` |
| `src/ubsocket/` | UBSocket adapter | Depends on UMQ. Outputs `librpc_adapter_brpc.so` |
| `src/ubsocket/csrc/core/umq/` | UMQ socket adapter ops | `umq_errno_converter.h` (frozen — do NOT modify) |
| `test/hcom/` | HCOM tests | `unit_test/`, `llt/`, `stub/`, `opensslcrt/` |
| `src/ubsocket/unit_test/` | UBSocket tests | gtest+mockcpp, ctest runner |
| `doc/` | Documentation | Design docs in `doc/ubsocket/` and `doc/hcom/` |

## Pre-commit Hooks

`.pre-commit-config.yaml`在每次commit时运行: **全量构建** + clang-format + clang-tidy。非常耗时——这些是本地hooks，不是轻量检查。clang-tidy对源文件使用release构建目录(`-p=cmake-build-release`)，对测试文件使用debug构建目录(`-p=cmake-build-debug`)。

## Errno Mapping 工作进度 (ubsocket)

> **详细成果固化**: `doc/ubsocket/UBSOCKET-ERRNO-UT-PROGRESS.ch.md`

### 代码修复
- `umq_errno_converter.cpp:24` — Convert(GET_STATE): ERR||MAX→EIO, else→0
- `umq_backend.cpp:199` — printf `%d→%zu`; `umq_socket.cpp:220` — `%u→%d`; `umq_socket.cpp:331` — `%d→%llu+cast`

### UT覆盖率 (142 total, 2 binaries)
| Binary | Count | Type |
|--------|-------|------|
| `umq_errno_converter_test` | 91 | converter级 (全部映射逻辑+override+BufStatus+HandleResult+GET_STATE) |
| `umq_ops_errno_test` | 51 | ops级 (RX/TX poll+rearm+cqe+handleError+AddUbDev+FindDevName+GetTxFd+GetDevEid+CheckDevAdd) |

- 13/36 调用点已覆盖ops级; 所有7个UmqOperation枚举值均有ops级覆盖
- 23个未覆盖调用点因SocketPtr/epoll/connector/acceptor复杂依赖暂不测试

### Coverage Baseline (csrc UT)
> **详细分析**: `doc/ubsocket/UBSOCKET-COVERAGE-ANALYSIS.ch.md`

| 指标 | 基线 | 目标 | 缺口 |
|------|------|------|------|
| 行覆盖率 | 11.1% (623/5637) | ≥80% | +3886行 |
| 分支覆盖率 | 5.3% (361/6861) | ≥50% | +3068分支 |

- 构建: `UMQ_BUILD=on UBSOCKET_UT=on UBSOCKET_COVERAGE=on bash build/build_umq_and_ubsocket.sh`
- 报告: `src/ubsocket/build/coverage_report/`, `coverage_summary.txt`, `coverage_detailed.txt`
- 79个文件已纳入; 24个未纳入(urma条件编译+纯声明头文件+无可执行行.cpp)
- profiling 已达标84.2%; core/umq和core/socket是主力战场(共+2586行)

### 关键陷阱 (UT相关)
- `mockcpp::Result` vs `ock::ubs::Result` 类型冲突 → 用 `using namespace ock::ubs;` + 显式限定 `ock::ubs::Result`
- `IO_SIZE_MB` 是 `ubsocket_defines.h` 中的宏 → 测试中不可重定义为constexpr
- `EidRegistry` 是 LeakySingleton → 测试中需 `UnregisterEid()` 清理
- `UmqBackend::UMQ_INITED` 是 static bool → SetUp中设为false重置
- errno必须在调用被测函数**之前**设置(生产代码在UMQ API调用后立即保存errno)
- lcov 1.16: 只用 `--ignore-errors gcov` (不支持 `unused,range`，那是旧版lcov的)
- coverage filter: `'*/_deps/*' '/usr/include/*' '*/3rdparty/*' '*/unit_test/*' '*/tools/*'`
- lcov必须加 `--initial --capture` 步骤才能纳入零覆盖率文件

## Known Gotchas

- CMake编译 **C++11** (`-std=c++11`) 但 `.clang-format` 设 `Standard: c++17` — 格式化用c++17规则，但代码必须兼容c++11
- `IncludeBlocks: Preserve` 在clang-format中意味着自动排序不会重排includes；手动排序必须遵循依赖链(如 `umq_data_tx_ops.h` 必须在 `umq_buf_converter.h` 之前，因为后者用到 `umq_buf_t`)
- `SortIncludes: true` 实际上只在Preserve块内排序，不是全局排序
- Bazel构建没有UT目标；`HCOM_BUILD_TESTS=on` with Bazel会回退到CMake跑测试
- Release构建有 `-Werror`；debug/test构建没有
- Pre-commit hooks跑全量构建 — commit会很慢
- `umq_errno_converter.h` 是冻结/final — 永远不要提议修改它
- **DpRearmTxInterrupt成功路径不走Convert**: `umq_rearm_interrupt` ret==0时直接设errno=EAGAIN返回-1，不调用Convert。仅ret≠0(失败)走Convert路径
- **Share-JFR handle变量语义陷阱**: `PrefillRx`中本地变量`umq_handle`在`UBS_ENABLE_SHARE_JFR=true`时指向`share_umq_handle_`(主UMQ), `false`时指向`umq_handle_`(子UMQ)。等ready逻辑应查`umq_handle_`(子UMQ)——因为子UMQ是刚创建的、需从IDLE→READY; 主UMQ早已ready。提取`WaitUntilReady`时若错误传入本地`umq_handle`而非`umq_handle_`, share JFR模式下会导致查错handle。此陷阱适用于任何涉及主/子UMQ双handle的函数重构。
- **`UBS_ENABLE_SHARE_JFR`默认true** — 测试环境若未显式关闭, PrefillRx走主UMQ路径。重构时必须在两种模式下都验证。
