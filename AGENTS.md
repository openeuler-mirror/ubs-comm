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
- **Comments**: follow the style of existing code in the same file/module
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
- UBSocket test binaries: `umq_errno_converter_test`, `umq_ops_errno_test`, `mock_infrastructure_test`, `profiling_test`

### UT约束

- **stub默认不启用**: 新增UT时优先使用mockcpp(`MOCKER_CPP`/`MOCKER`)mock C API和系统调用，**默认不使用stub方式**(fake_epoll_static/AllocMockBufWithBlock/SocketTestHelper等)。只有用户明确指定需要stub实现时才启用。现有stub基础设施和mock_infrastructure_test保留不动(验证stub自身正确性)，但新增业务UT不应依赖stub。
- **单用例执行≤1s**: 每个`TEST_F`用例的执行时间不超过1秒。禁止在测试中使用长时间sleep、阻塞等待、密集计算循环等。如需等待异步事件，使用短超时(≤100ms)+轮询。

### Skill系统

opencode skill系统位于`.opencode/skills/`，指导AI生成符合项目约定的UT代码。详见`.opencode/README.md`。

| Skill | 触发关键词 | 覆盖模块 |
|-------|-----------|---------|
| `ut-gen` | UT, 单元测试, mockcpp, gtest | 通用UT模式(root skill) |
| `ut-gen-umq` | UMQ, umq, Share-JFR | `csrc/core/umq/` |
| `ut-gen-core` | epoll, SocketConnector, SocketAcceptor | `csrc/core/`(不含umq/) |
| `ut-gen-common` | GlobalSetting, LeakySingleton, ThreadPool | `csrc/common/` |
| `ut-gen-under-api` | DlApi, dlopen, dlsym | `csrc/under_api/` |
| `ut-gen-profiling` | ProfTracer, profiling | `csrc/profiling/` |
| `ut-coverage-coord` | coverage, 进度, 认领 | 协调层 |

写模块UT时需同时加载`ut-gen`+对应子skill。发现新模式/陷阱时按skill"知识回流"规则更新。

## Architecture & Package Boundaries

| Directory | Component | Key Info |
|-----------|-----------|----------|
| `src/hcom/` | HCOM core library | Main product. `ock::hcom` namespace. Entry: `hcom.h` |
| `src/hcom/transport/` | Transport implementations | `rdma/`, `sock/`, `shm/`, `ub/` — conditional compilation |
| `src/hcom/umq/` | UMQ messaging queue | Standalone CMake build. Outputs `libumq.so` |
| `src/ubsocket/` | UBSocket adapter | Depends on UMQ. Outputs `libubsocket.so` |
| `src/ubsocket/csrc/core/umq/` | UMQ socket adapter ops | `umq_errno_converter.h` (frozen — do NOT modify) |
| `src/ubsocket/csrc/core/` | 通用抽象层 | `ubsocket_*`文件，不应引用`umq/`头文件 |
| `test/hcom/` | HCOM tests | `unit_test/`, `llt/`, `stub/`, `opensslcrt/` |
| `src/ubsocket/unit_test/` | UBSocket tests | gtest+mockcpp, ctest runner |
| `doc/` | Documentation | Design docs in `doc/ubsocket/` and `doc/hcom/` |

### 依赖方向规则

`core/ubsocket_*`是通用抽象层，当前支持umq通信方式，后续新增通信方式只需在`core/`下新增子目录即可，不需要改动通用层代码：

- **通用层**(`core/ubsocket_*.cpp/.h`)只操作虚接口(`AcceptorOps/ConnectorOps/DataTxOps/DataRxOps`等)，**禁止引用任何具体实现子目录的头文件**（当前为`umq/`，后续新增如`urma/`、`posix_shm/`等同理禁止）
- **工厂方法**(`ubsocket_socket.cpp`)是唯一允许引用具体实现子目录头文件的通用层代码——它根据`SocketType`创建具体实现对象，新增通信方式时只需在此文件新增`case`分支
- **具体实现层**(`core/umq/*.cpp/.h`等)可以引用通用层头文件

## Pre-commit Hooks

`.pre-commit-config.yaml`在每次commit时运行: **全量构建** + clang-format + clang-tidy。非常耗时——这些是本地hooks，不是轻量检查。clang-tidy对源文件使用release构建目录(`-p=cmake-build-release`)，对测试文件使用debug构建目录(`-p=cmake-build-debug`)。

### commit前必做检查

**每次commit之前必须执行** `pre-commit run clang-format --all-files`，确保格式合规后再提交。完整pre-commit hook耗时过长（全量构建+clang-tidy），仅clang-format是轻量且可靠的检查。如clang-format修改了文件，需`git add`后再commit。

```bash
pre-commit run clang-format --all-files   # commit前必跑
git add -A                     # 如有格式修改，重新暂存
git commit --no-verify -m "..." # 跳过完整hook，仅依赖手动clang-format
```

clang-tidy当前有预存配置问题(no checks enabled)，hcom release build也有预存编译错误，两者均与ubsocket改动无关，commit时可用`--no-verify`跳过。

## Errno Mapping 工作进度 (ubsocket)

> **详细成果固化**: AGENTS.md §Errno Mapping工作进度 + `doc/ubsocket/UBSOCKET-CLAIMING.md`

### 代码修复
- `umq_errno_converter.cpp:24` — Convert(GET_STATE): ERR||MAX→EIO, else→0
- `umq_backend.cpp:199` — printf `%d→%zu`; `umq_socket.cpp:220` — `%u→%d`; `umq_socket.cpp:331` — `%d→%llu+cast`

### UT覆盖率 (220 total, 5 binaries)
| Binary | Count | Type |
|--------|-------|------|
| `umq_errno_converter_test` | 91 | converter级 (全部映射逻辑+override+BufStatus+HandleResult+GET_STATE) |
| `umq_ops_errno_test` | 51 | ops级 (RX/TX poll+rearm+cqe+handleError+AddUbDev+FindDevName+GetTxFd+GetDevEid+CheckDevAdd) |
| `mock_infrastructure_test` | 32 | mock基础设施验证 (Helper方式2 + Static方式19 + Block方式5 + Socket方式6) |
| `iobuf_zcopy_adapter_test` | 45 | iobuf模块全覆盖 (BlockMem 6 + ZcopyAdapter 15 + DynSymScanner 24) |
| `profiling_test` | 1 | profiling (DumpLoop无sleep) |

- 13/36 调用点已覆盖ops级; 所有7个UmqOperation枚举值均有ops级覆盖
- 23个未覆盖调用点因SocketPtr/epoll/connector/acceptor复杂依赖暂不测试

### fake_epoll_static (链接时系统调用替换)

- **位置**: `src/ubsocket/unit_test/stub/fake_epoll/` — `fake_epoll.h` + `fake_epoll.cpp`
- **原理**: 提供与libc同名C全局函数(`epoll_create1`, `epoll_create`, `epoll_ctl`, `epoll_wait`, `epoll_pwait`, `eventfd`, `eventfd_write`, `eventfd_read`, `close`)，链接器优先解析fake符号；`close()`对非fake fd通过`dlsym(RTLD_NEXT)`转发到真实libc
- **测试控制API** (`ock::ubs::test::FakeEpollCtl`): `Reset()`, `SetNextEpollCreateReturn(fd)`, `SetNextEpollWaitEvents(events)`, `SetNextEpollWaitReturn(ret)`, `SetNextEventfdReturn(fd)`, `IsFakeFd(fd)`, `AllocFakeFd()`, `ReleaseFakeFd(fd)`, `GetFdCount()`
- **fd分配**: 从`100`开始递增(`FAKE_FD_BASE`)，避免与真实fd冲突
- **内部状态**: `FakeEpollState`维护epoll_fds/event_fds/all_fake_fds集合 + registered_events映射(epfd→fd→epoll_event)
- **使用方式**: Ops级测试链接`fake_epoll_static`后，生产代码中直接调用的`epoll_create1()/epoll_ctl()/eventfd()/close()`自动被拦截；通过`FakeEpollCtl` API注入特定返回值

### AllocMockBufWithBlock (Block级8K对齐mock) — 快捷方式，非唯一入口

- **位置**: `src/ubsocket/unit_test/stub/umq_api_helper.h` — `AllocMockBufWithBlock()` + `GetBlockFromMockBuf()` + `ResetMockBufWithBlockIndex()`
- **原理**: 分配8K对齐内存(`alignas(8192)`)，在8K边界放置`Block`结构体(placement new)，`buf_data`指向Block之后的区域，`PtrFloorToBoundary(buf_data)`正确回溯到Block地址
- **Block初始nshared=1**: 与生产代码一致; IncRef→2, DecRef→1(存活); DecRef→0时调用`blockmem_deallocate_zero_copy`但g_zcopy_allocator为nullptr所以安全
- **缓冲池**: 8个静态`MockBufWithBlock`数组(每个16384字节)，循环使用；`ResetMockBufWithBlockIndex()`在SetUp中调用重置索引
- **灵活绕过**: 需自定义opcode/nshared/data_size等字段时，直接在test case中手写umq_buf_t构造+8K对齐placement new Block
- **使用方式**: `umq_buf_t *buf = AllocMockBufWithBlock(size); Block *block = GetBlockFromMockBuf(buf); block->IncRef(); ...`

### SocketTestHelper (UmqSocket对象构造) — 快捷方式，非唯一入口

- **位置**: `src/ubsocket/unit_test/stub/socket_test_helper.h` — `MakeTestUmqSocket()` + `DestroyTestSocketOps()`
- **原理**: 使用`MakeRef<UmqSocket>(fd)`创建UmqSocketPtr(绕过SocketBase::Create工厂链)，通过`-fno-access-control`设置`umq_handle_/event_fd_/state_`，创建`UmqTxOps/UmqRxOps`并赋给`tx_.tx_ops_/rx_.rx_ops_`，`RefConvert`转为SocketPtr
- **参数**: `MakeTestUmqSocket(fd, umqHandle, state=ESTABLISHED, shareUmqHandle=INVALID_HANDLE)` — state支持INIT/RAW_ESTABLISHED/ESTABLISHED/SHUTDOWN；shareUmqHandle模拟JFR场景
- **Ref管理**: MakeRef创建ref_count=1，RefConvert临时增到2，函数返回后SocketPtr持有ref_count=1，析构时ref_count→0→delete
- **灵活绕过**: 复杂场景(自定义ops/跳过ops创建/多handle配置)可在test case中手写MakeRef→设private字段→自定义构造
- **cleanup**: `DestroyTestSocketOps(sock)` 删除tx/rx ops + 释放fake event_fd，在SocketPtr析构前调用
- **使用方式**: `SocketPtr sock = MakeTestUmqSocket(TEST_FD, TEST_UMQ_HANDLE); ...; DestroyTestSocketOps(sock);`

### Coverage Baseline (csrc UT)
> **详细分析**: `doc/ubsocket/UBSOCKET-COVERAGE-ANALYSIS.ch.md`

| 指标 | 基线 | 目标 | 缺口 |
|------|------|------|------|
| 行覆盖率 | 15.1% (850/5637) | ≥80% | +3650行 |
| 分支覆盖率 | 7.0% (479/6861) | ≥50% | +2953分支 |

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
- **`extern "C"` + namespace scope**: `extern "C"`块内无法直接使用C++命名空间中的变量(如`g_state`、`next_fd_`)，必须用显式限定(`ock::ubs::test::FakeEpollCtl::next_fd_++`)或提供全局访问器函数(`GetFakeEpollState()`)
- **`dlsym(RTLD_NEXT)` 转发**: `fake_epoll_static`的`close()`对非fake fd必须通过`dlsym(RTLD_NEXT, "close")`转发到真实libc，否则测试代码本身的close调用和LibcApi路径都会返回-1+EBADF
- **helper头文件自包含**: `libc_api_helper.h`使用`sockaddr_in`/`htons`/`INADDR_LOOPBACK`时需包含`<netinet/in.h>`+`<arpa/inet.h>`，否则独立编译报"incomplete type"
- **`LibcApi::open` 是variadic**: mockcpp无法mock variadic函数，改用`LibcApi::open_ptr`函数指针直接替换(`-fno-access-control`)
- **mockcpp `.stubs()` 默认返回Void**: 非void返回函数必须加`.will(returnValue(...))`
- **`LibcApi::_ptr` 初始为nullptr**: 未调用`LibcApi::Load()`时通过nullptr函数指针调用→segfault，SetUp中必须设置`_ptr`或调用`Load()`

## Commit规范

- commit前必须执行 `pre-commit run clang-format --all-files`，确保格式合规后再提交
- 完整pre-commit hook耗时过长(全量构建+clang-tidy)，commit时用 `--no-verify` 跳过，仅依赖手动clang-format
- **Co-Authored-By**: 当AI辅助生成代码时，commit消息必须包含 `Co-Authored-By: <模型名> <模型邮箱>`，标注实际使用的驱动模型(如 `Co-Authored-By: GLM-5.1 <noreply@zhipuai.cn>`、`Co-Authored-By: Claude Opus 4 <noreply@anthropic.com>` 等)，根据实际情况填写

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
- **冗余GetItem陷阱**: 当函数入参已持有`SocketPtr`/`SocketBasePtr`时，不应再用`ArraySet<Socket>::GetInstance().GetItem(fd_)`重新查找——入参就是同一个对象的引用，额外GetItem多一次atomic load + IncreaseRef且语义冗余。应直接用入参(`sock`)或函数内已计算的局部变量(`sockBase`)。反面: `RefConvert<Socket,SocketBase>(ArraySet<Socket>::GetInstance().GetItem(fd_))`; 正面: `RefConvert<Socket,SocketBase>(sock)` 或复用已有的 `sockBase`。
- **`RPC_ADPT_FD_MAX`与`ArraySet::Capacity()`语义不同**: `RPC_ADPT_FD_MAX=8192`是`ubsocket_defines.h`中编译时常量，仅用于`ProbeManager`固定大小环形队列(`mRecvQueue[RPC_ADPT_FD_MAX]`)——不可替换为运行时`Capacity()`，因为静态数组大小必须编译时确定。`Capacity()`是运行时动态值(取`min(rlim_cur, 65536)`)，供外部查询fd容量上限使用。
- **热路径不加日志**: `Init`是一次性初始化可加日志；`GetItem/OverrideItem/RemoveItem/ForEach`是高频热路径，加日志会显著影响性能，尤其`ForEach`遍历+回调场景。
- **mermaid渲染陷阱**: 方括号`[]`在`participant as`别名中被解析为链接语法(需改为纯文本如`set_obj_idx`)；泛型尖括号`<>`、Unicode圆圈数字①②③④、emoji如❌、特殊数学符号≤×、特殊箭头←→、HTML实体`&lt;&gt;`均可能导致渲染失败。文档中mermaid图应只用纯ASCII英文+中文描述。
- **C++11 `static constexpr` ODR-use陷阱**(严重 — 导致链接undefined reference): C++11中模板类的`static constexpr`成员变量如果被**ODR-use**(引用绑定)，需要类外提供定义，否则链接报undefined。典型场景：`std::min(const T&, const T&)`的参数是引用，直接传`static constexpr`变量会绑定引用→ODR-use→需要外部定义→未提供→链接错误。**修复方法**：`static_cast<类型>(CONSTEXPR_VAR)`创建临时rvalue，引用绑定到临时值而非原变量→不再ODR-use→不需要外部定义→链接OK。反面: `std::min(x, FD_CAPACITY_HARD_LIMIT)`；正面: `std::min(x, static_cast<uint32_t>(FD_CAPACITY_HARD_LIMIT))`。C++17的`inline constexpr`自动解决此问题(隐式提供定义)，但本项目编译C++11。**所有模板类中`static constexpr`成员传给引用参数函数(如`std::min/max/clamp`、日志函数的`%u`格式化参数等)都必须加`static_cast`避免ODR-use**。
