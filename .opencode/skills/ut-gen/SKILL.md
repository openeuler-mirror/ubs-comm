---
name: ut-gen
description: Use when generating unit tests for ubsocket module csrc code. Trigger on keywords: UT, unit test, 单元测试, test generation, 测试生成, mockcpp, gtest, CMakeLists test target, umq_ops_test, umq_errno_test, securec, AllocMockBuf. Use ONLY when the task involves writing or modifying C++ unit test code for the ubsocket component of ubs-comm.
---

# UBSocket UT 生成 Skill

## 覆盖范围

为 `src/ubsocket/csrc/` 下的源文件生成C++单元测试，包括：
- `csrc/core/umq/` — UMQ传输适配器(主要焦点，经验最多)
- `csrc/core/urma/` — URMA传输适配器
- `csrc/core/` — 共享 socket/epoll/data ops
- `csrc/common/` — 全局设置、锁、日志、errno
- `csrc/iobuf/` — 零拷贝适配器
- `csrc/under_api/` — UmqApi/UurmaApi 包装器(dlopen vs adapter 后端)
- `csrc/profiling/` — 性能追踪器
- `csrc/ubsocket*.cpp` — 顶层ubsocket入口文件

模块特定深度模式，需加载对应子skill：
- `ut-gen-umq` — 测试 `csrc/core/umq/` 时加载
- `ut-gen-core` — 测试 `csrc/core/` 时加载(epoll/socket/data ops)
- `ut-gen-common` — 测试 `csrc/common/` 时加载
- `ut-gen-profiling` — 测试 `csrc/profiling/` 时加载
- `ut-gen-under-api` — 测试 `csrc/under_api/` 时加载

## 覆盖率目标

| 指标 | 基线 (2026-05-27) | 目标 | 缺口 |
|------|-------------------|------|------|
| 行覆盖率 | 11.1% (623/5637) | ≥ 80% | +3886行 |
| 分支覆盖率 | 5.3% (361/6861) | ≥ 50% | +3068分支 |
| 函数覆盖率 | 19.0% (117/615) | 100%(可行时) | +498函数 |

> **权威数据源**: `doc/ubsocket/UBSOCKET-COVERAGE-ANALYSIS.ch.md` — 模块级明细、零覆盖率文件优先级、Mock依赖分析、已知陷阱。
> **协调跟踪**: `.opencode/skills/ut-coverage-coord/SKILL.md` — 文件认领、进度跟踪、里程碑。

## 构建与运行

### 构建UT+覆盖率
```bash
UMQ_BUILD=on UBSOCKET_UT=on UBSOCKET_COVERAGE=on bash build/build_umq_and_ubsocket.sh
```

### 仅构建UT(不含覆盖率)
```bash
UMQ_BUILD=on UBSOCKET_UT=on bash build/build_umq_and_ubsocket.sh
```

### 通过ctest运行UT
```bash
ctest --test-dir src/ubsocket/build --output-on-failure
```

### 直接运行单个test binary
```bash
cd src/ubsocket/build
./umq_errno_converter_test
./umq_ops_errno_test
```

### UMQ前置依赖
UBSocket依赖 `libumq.so`，UMQ必须先构建。构建脚本自动处理此依赖。

## 测试框架栈

| 层级 | 工具 | 说明 |
|------|------|------|
| Runner | GoogleTest 1.12.1 | `TEST_F`, `EXPECT_EQ`, `ASSERT_NE` 等 |
| Mock | mockcpp v2.7 | `MOCKER_CPP`, `.stubs()`, `.will()`, `returnValue()`, `ignore()` |
| Coverage | `-fno-access-control` | 测试代码可直接访问private成员 |
| Safe mem | `securec` | 使用 `memset_s`, `memcpy_s` 替代 `memset`, `memcpy` |
| Verbosity flag | `-DMOCK_VERBS` | 启用fake ibverbs stub(HCOM侧，非ubsocket) |
| Link | `ubsocket_static` | UT链接静态库以获取符号访问 |
| Link | `boundscheck` | 安全C库，提供 `memset_s`/`memcpy_s` |
| Link | `mockcpp` + `pthread` | mock框架必需 |
| Link | `GTest::gtest_main` | 提供 `main()` 入口 |

## mockcpp 模式 (关键)

### 全局C函数mock (ADAPTER后端 — 默认)

Adapter后端(`UMQ_ADAPTER_BACKEND_ENABLED`, 默认ON)通过 `UmqApi::umq_xxx()` 静态包装直接调用 `::umq_*` 全局C函数。**此后端路径没有 `_ptr` 成员变量**。

**正确的UmqApi mock模式:**
```cpp
MOCKER_CPP(::umq_rearm_interrupt)
    .stubs()
    .will(returnValue(-UMQ_ERR_EPERM));
```

**错误模式(仅dlopen后端有，默认不可用):**
```cpp
// 不要使用这些 — adapter后端中不存在:
UmqApi::umq_rearm_interrupt_ptr = ...;  // _ptr 成员仅在 UMQ_DLOPEN_BACKEND_ENABLED 下存在
```

### Mock调用顺序

1. **在调用被测函数之前设置errno** — 生产代码在UMQ API调用后立即读取 `errno` 保存为 `savedErrno` 供 `UmqErrnoConverter::Convert()` 使用
2. **调用被测函数**
3. **在每个使用mock的test case结束后调用 `GlobalMockObject::verify()`** — 重置mock状态。同时在 `TearDown()` 中也调用以确保安全

```cpp
TEST_F(MyTest, MyScenario)
{
    UmqRxOps rxOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_rearm_interrupt)
        .stubs()
        .will(returnValue(-UMQ_ERR_EPERM));
    errno = EINVAL;  // 模拟内核会设置的errno
    int ret = rxOps.RearmRxInterrupt();
    EXPECT_EQ(ret, -UMQ_ERR_EPERM);
    EXPECT_EQ(errno, EINVAL);  // converter应保留EINVAL
    GlobalMockObject::verify();  // 重置mock状态
}
```

### mockcpp API参考

| 表达式 | 含义 |
|--------|------|
| `MOCKER_CPP(::func_name)` | 开始mock全局C函数 |
| `.stubs()` | 匹配任意调用(无约束) |
| `.will(returnValue(X))` | 匹配调用时返回值X |
| `.will(ignore())` | 不返回(void函数 — 但void不能用returnValue mock) |
| `.expects(exactly(N))` | 期望恰好N次调用 |
| `.expects(once())` | 期望恰好1次调用 |
| `GlobalMockObject::verify()` | 重置所有mock对象，验证期望 |

**注意:** mockcpp头文件是 `<mockcpp/mockcpp.hpp>` (不是 `.h`)。

### 返回void的函数 — 无法mock返回值

`umq_ack_interrupt()` 返回 `void`，没有返回值可检查。生产代码也不检查其返回值。**不要尝试mock void返回的UMQ API调用来做错误路径测试。** 仅用于调用次数验证时，mockcpp仍可stub void函数，但很少有用。

### 返回数组的函数 — 无法直接mock

`umq_dev_info_t[1]` 数组返回类型：mockcpp无法mock返回数组的函数。绕过方式：在测试路径使用指针返回类型(例如通过指针调用测试 `ConvertHandleResult`，而非数组调用)。

### GlobalMockObject::verify() 调用位置

必须在**每个使用mock的test case之后**调用(不仅是TearDown)。这重置mock状态。如果在SetUp()中mock或跨子测试复用mock，需在每个TEST_F体末尾也调用 `verify()`，同时TearDown中也调用。

### -fno-access-control 效果

测试构建使用 `-fno-access-control`，允许直接读写private成员(如 `sock.umq_handle_`, `rxOps.local_umqh_`, `UmqBackend::UMQ_INITED`)。此选项仅对test target启用——生产代码不可用。

## Test Fixture 模式

```cpp
class MyFeatureTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
        SocketSet::Instance().Init();

        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        GlobalSetting::UBS_RX_DEPTH = TEST_DEPTH;
        GlobalSetting::UBS_TX_DEPTH = TEST_DEPTH;
        GlobalSetting::UBS_TRACE_ENABLED = false;
    }

    void TearDown() override
    {
        errno = 0;
        GlobalMockObject::verify();
        SocketSet::Instance().ReleaseAll();
    }
};
```

**注意:** 构造依赖lock/socket基础设施的对象(UmqRxOps, UmqTxOps, UmqSocket等)时，必须先调用 `LockRegistry::RegisterDefaultOps()` 和 `SocketSet::Instance().Init()`。纯converter/utility测试不需要。

## UmqErrnoConverter 集成

`UmqErrnoConverter` 是核心errno映射机制，声明在 `umq_errno_converter.h` 中——其 **API、枚举值、映射数据数组均为冻结/final**。Doxygen注释可更新以反映实现变更。

### 三个转换API

| API | 使用场景 | 返回值 |
|-----|----------|--------|
| `Convert(op, umqRet, savedErrno)` | UMQ API返回 `int`(负值=错误) | Linux errno(正值)。`GET_STATE`时: umqRet是 `umq_state_t`, ERR/MAX→EIO, else→0 |
| `ConvertBufStatus(op, bufStatus, savedErrno)` | CQE `buf->status` 字段 | Linux errno(正值) |
| `ConvertHandleResult(op, savedErrno)` | UMQ API返回handle/size(0=失败) | Linux errno(正值) |

### UmqOperation枚举值(唯一合法操作)

```cpp
enum class UmqOperation {
    CONNECT,       // umq_init, umq_dev_add(backend), umq_bind(connector), umq_get_route_list,
                   // umq_interrupt_fd_get(建立阶段), umq_rearm_interrupt(建立阶段), umq_dev_info_get,
                   // umq_post(RX, prefill/PrefillRx)
    ACCEPT,        // umq_dev_add(acceptor), umq_bind(acceptor)
    WRITEV,        // umq_post(TX), umq_poll(TX), umq_get_cq_event(TX), umq_rearm_interrupt(TX, data/数据通路),
                   // umq_buf_alloc(TX AllocTxBuf)失败时不走Convert
    READV,         // umq_poll(RX), umq_get_cq_event(RX), umq_rearm_interrupt(RX, data/数据通路),
                   // umq_post(RX, refill), umq_buf_alloc(RX)失败时不走Convert
    CREATE,        // umq_create — 仅ConvertHandleResult，不走Convert统一表
    BIND_INFO_GET, // umq_bind_info_get, umq_dev_info_list_get — 仅ConvertHandleResult
    GET_STATE,     // umq_state_get — 特殊路径: 无表查找，无override, ERR/MAX→EIO, else→0
};
```

**没有专门对应 `interrupt_fd_get`, `dev_add`, `poll`, `post`, `init` 的枚举值。** 选择最近的语义匹配:
- TX方向数据通路API → `WRITEV`
- RX方向数据通路API → `READV`
- Connector上下文/backend初始化/建立阶段interrupt_fd_get/rearm → `CONNECT`
- Acceptor上下文 → `ACCEPT`
- `umq_create` → `CREATE` (ConvertHandleResult，非Convert统一表)
- `umq_state_get` → `GET_STATE` (特殊路径，见下文)

**GET_STATE是特殊Convert路径:** `umq_state_get` 返回 `umq_state_t` 枚举(0–3)，不是UMQ_ERR_*负值。converter **不查找映射表，不应用 `ShouldOverrideWithSavedErrno`**:
- `QUEUE_STATE_ERR` 或 `QUEUE_STATE_MAX` → `EIO`
- `QUEUE_STATE_IDLE` 或 `QUEUE_STATE_READY` → `0`

**共享代码vs独立调用点:** 某些UMQ API出现在多个上下文:
- `umq_bind`: 两个独立调用点 — `connector.cpp:440` (op=CONNECT) 和 `acceptor.cpp:169` (op=ACCEPT)。这不是共享代码；每个是独立路径。
- `umq_dev_add`: 出现在backend init(op=CONNECT，不是CREATE)和acceptor上下文(op=ACCEPT)。两者都跳过-EEXIST。独立调用点。
- `umq_interrupt_fd_get`: 建立阶段(op=CONNECT)，非WRITEV/READV。
- `umq_rearm_interrupt`: 建立阶段(op=CONNECT), 数据通路TX(op=WRITEV), 数据通路RX(op=READV)。
- `umq_post`: PrefillRx建立阶段(op=CONNECT), TX数据通路(op=WRITEV), RX refill数据通路(op=READV)。
- 对Convert()而言，`op` 对errno映射结果无影响(都使用kCommonErrnoMappings)，但它标识哪个代码路径产生了错误。对ConvertBufStatus()而言，`op` 决定使用哪个映射表。
- **umq_rearm_interrupt(TX, data)**: DpRearmTxInterrupt() — ret==0成功时直接设errno=EAGAIN返回-1,不经过Convert; 仅ret≠0(失败)走Convert(WRITEV)。

### 生产代码errno映射模式(csrc如何使用converter)

```cpp
int ret = UmqApi::umq_poll(local_umqh_, UMQ_IO_RX, buf, max_buf_size);
if (ret < 0) {
    int savedErrno = errno;  // 必须立即保存errno
    errno = UmqErrnoConverter::Convert(UmqOperation::READV, ret, savedErrno);
    UBS_VLOG_ERR("umq_poll() failed, ret: %d, mapped errno: %d(%s), original errno: %d\n",
                 ret, errno,
                 UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, ret),
                 savedErrno);
}
```

### Converter override语义(用于测试期望)

`Convert()` 函数有override逻辑:
1. `UMQ_FAIL(=-1)` + `savedErrno` ∈ {EINVAL, ENODEV, ENOMEM, ENOEXEC, EIO} → 返回 `savedErrno`
2. `UMQ_ERR_ENODEV` + `savedErrno` ∈ {EINVAL, EIO} → 返回 `savedErrno`
3. 否则: 查表 → 命中返回映射errno → 未命中返回 `savedErrno`(若>0)，否则EIO
4. **例外: GET_STATE** — 绕过上述所有逻辑。无表查找，无override。`QUEUE_STATE_ERR`/`QUEUE_STATE_MAX` → EIO; `QUEUE_STATE_IDLE`/`QUEUE_STATE_READY` → 0。

**这意味着:** 测试时必须在调用被测函数**之前**设置 `errno`，因为生产代码将其读取为 `savedErrno`。测试应验证最终的 `errno` 符合预期映射值(而非原始UMQ错误码)。

## 测试文件结构

### 位置
```
src/ubsocket/unit_test/<test_name>.cpp
```

### 命名约定
- 测试binary: `<feature>_test` (如 `umq_errno_converter_test`, `umq_ops_errno_test`)
- 测试fixture类: `<Feature>Test` (如 `UmqOpsErrnoTest`, `UmqErrnoConverterTest`)
- 测试case: `TEST_F(<Fixture>, <MethodName>_<Scenario>_<ExpectedResult>)`
  - 如 `RearmRxInterrupt_FailEpermSavedEinval_MapsEinval`
  - 如 `DpRearmTxInterrupt_FailEagain_MapsEagain`

### 辅助函数: AllocMockBuf

测试CQE/buffer错误处理时，使用此模式创建mock `umq_buf_t`:

```cpp
umq_buf_t *AllocMockBuf(uint32_t size, umq_buf_status_t status = UMQ_BUF_SUCCESS)
{
    static uint8_t bufData[TEST_BUF_DATA_SIZE];
    static umq_buf_pro_t bufPro;
    static umq_buf_t mockBuf;

    memset_s(&bufPro, sizeof(umq_buf_pro_t), 0, sizeof(umq_buf_pro_t));
    bufPro.opcode = UMQ_OPC_SEND;

    memset_s(&mockBuf, sizeof(umq_buf_t), 0, sizeof(umq_buf_t));
    mockBuf.buf_data = reinterpret_cast<char *>(bufData);
    mockBuf.data_size = size;
    mockBuf.total_data_size = size;
    mockBuf.status = status;
    memcpy_s(mockBuf.qbuf_ext, sizeof(mockBuf.qbuf_ext), &bufPro, sizeof(umq_buf_pro_t));
    mockBuf.qbuf_next = nullptr;
    mockBuf.io_direction = UMQ_IO_RX;

    return &mockBuf;
}
```

**注意:** `AllocMockBuf` 使用 `static` 局部变量——每次调用返回相同指针。如果单个测试需要多个不同buffer，需分配多个static数组或使用动态分配。大多数errno映射测试只需一个buffer。

## CMakeLists.txt 注册

添加到 `src/ubsocket/unit_test/CMakeLists.txt`:

```cmake
# 1. 声明可执行文件和ctest入口
add_executable(<test_name> "")
add_test(NAME <test_name> COMMAND <test_name>)
set_target_properties(<test_name> PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

# 2. C++标准(项目测试编译用c++17, 生产用c++11)
target_compile_features(<test_name> PRIVATE cxx_std_17)

# 3. 编译定义
target_compile_definitions(<test_name> PRIVATE UBSOCKET_UNIT_TEST)

# 4. -fno-access-control(允许访问private成员)
target_compile_options(<test_name> PRIVATE -fno-access-control)

# 5. 抑制警告(已在全局设置: set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w"))

# 6. Include目录 — 取决于测试需要哪些头文件
# Converter-only测试:
target_include_directories(<test_name>
    PRIVATE
        ${UBSOCKET_BASE_DIR}/csrc/core/umq
        ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
)
# Ops级测试(需要更多includes):
target_include_directories(<test_name>
    PRIVATE
        ${UBSOCKET_BASE_DIR}/csrc/core/umq
        ${UBSOCKET_BASE_DIR}/csrc
        ${UBSOCKET_BASE_DIR}/csrc/core
        ${UBSOCKET_BASE_DIR}/csrc/common
        ${UBSOCKET_BASE_DIR}/csrc/iobuf
        ${UBSOCKET_BASE_DIR}/csrc/under_api
        ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
        ${UBSOCKET_BASE_DIR}/include
)

# 7. 链接库
# Converter-only测试(无mockcpp, 无ubsocket_static):
target_link_libraries(<test_name> PRIVATE GTest::gtest_main)
# Ops级测试(需要mockcpp + ubsocket_static):
target_link_libraries(<test_name>
    PRIVATE
        ubsocket_static
        boundscheck
        mockcpp
        GTest::gtest_main
        pthread
)

# 8. 源文件
# Converter-only测试(直接编译.cpp):
target_sources(<test_name>
    PRIVATE
        <test_name>.cpp
        ${UBSOCKET_BASE_DIR}/csrc/core/umq/<source_file>.cpp
)
# Ops级测试(链接ubsocket_static, 无需包含源文件):
target_sources(<test_name>
    PRIVATE
        <test_name>.cpp
)
```

## 代码风格要求 (来自AGENTS.md)

### 文档语言规则

所有项目文档(skill文件、coverage分析、进度跟踪、AGENTS.md等)遵循中英混写规则:

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

### 代码风格明细

| 规则 | ID | 要求 |
|------|----|------|
| 缩进 | — | 4空格 |
| 行宽 | G.FMT.05-CPP | 120字符上限 |
| 函数大括号 | — | Allman(换行) |
| 控制语句大括号 | — | K&R(同行) |
| 指针对齐 | — | 右对齐: `Type *name` |
| 禁止C风格转换 | G.EXP.14-CPP | 使用 `static_cast<int>()`, `reinterpret_cast<>()` |
| 命名(参数/局部) | G.NAM.03-CPP | camelBack: `savedErrno`, `maxBufCount` |
| 命名(类/方法) | — | CamelCase: `UmqRxOps`, `RearmRxInterrupt` |
| 命名(宏/枚举) | — | UPPER_CASE: `UMQ_ERR_EPERM`, `TEST_FD` |
| 全局变量前缀 | — | `g_` |
| 注释 | — | **除非明确要求，不添加任何注释** |
| 许可证头 | — | Mulan PSL v2, Copyright行后有空行 |
| Include顺序 | G.INC.07-CPP | 对应头文件优先，然后标准库，然后项目头，然后3rdparty |
| IncludeBlocks | — | clang-format中 `Preserve` — 必须手动排序 |

### UBS_VLOG_ERR 行宽限制

生产代码使用 `UBS_VLOG_ERR()`，格式字符串常超120字符。拆分字符串字面量:

```cpp
// 错误 — 超过120字符:
UBS_VLOG_ERR("umq_rearm_interrupt() failed for RX, local umq: %llu, ret: %d, mapped errno: %d(%s), original errno: %d\n", ...);

// 正确 — 在120字符处拆分:
UBS_VLOG_ERR("umq_rearm_interrupt() failed for RX, local umq: %llu, "
              "ret: %d, mapped errno: %d(%s), original errno: %d\n", ...);
```

**续行缩进规则:** 续行必须与调用位置缩进层级对齐(非列0)。方法体缩进1级(4空格)时，续行从列14开始(4 + `UBS_VLOG_ERR` 10字符)。参考现有代码示例。

## Errno映射测试覆盖矩阵

对每个调用UMQ API并检查返回值的生产代码路径，测试应覆盖以下场景:

### `UmqErrnoConverter::Convert()` 路径(int返回API)

| 测试场景 | 设置 | 期望errno |
|----------|------|-----------|
| 特定映射错误(如EAGAIN) | `MOCKER_CPP(::umq_xxx).stubs().will(returnValue(-UMQ_ERR_EAGAIN))`, `errno=0` | 映射值(EAGAIN) |
| Override savedErrno(UMQ_FAIL + EINVAL) | `returnValue(-1)`, `errno=EINVAL` | EINVAL |
| Override savedErrno(ENODEV + EIO) | `returnValue(-UMQ_ERR_ENODEV)`, `errno=EIO` | EIO |
| 无override，回退EIO | `returnValue(-UMQ_ERR_EPERM)`, `errno=0` | EIO |
| Override阻止不匹配savedErrno | `returnValue(-UMQ_ERR_ENODEV)`, `errno=ENOMEM` | ENODEV(无override，映射优先) |

### `UmqErrnoConverter::ConvertBufStatus()` 路径(CQE buffer status)

| 测试场景 | 设置 | 期望errno |
|----------|------|-----------|
| 特定buf status映射 | `AllocMockBuf(size, UMQ_BUF_REM_OPERATION_ERR)` | EIO(大多数buf错误映射为EIO) |
| 成功status | `AllocMockBuf(size, UMQ_BUF_SUCCESS)` | 0 |
| 流控更新 | `AllocMockBuf(size, UMQ_BUF_FLOW_CONTROL_UPDATE)` | 0 |

### `UmqErrnoConverter::Convert(GET_STATE)` 路径(umq_state_get)

| 测试场景 | 设置 | 期望errno |
|----------|------|-----------|
| QUEUE_STATE_ERR | `Convert(GET_STATE, QUEUE_STATE_ERR, errno)` | EIO |
| QUEUE_STATE_MAX | `Convert(GET_STATE, QUEUE_STATE_MAX, errno)` | EIO |
| QUEUE_STATE_IDLE | `Convert(GET_STATE, QUEUE_STATE_IDLE, errno)` | 0 |
| QUEUE_STATE_READY | `Convert(GET_STATE, QUEUE_STATE_READY, errno)` | 0 |

**注意:** GET_STATE中 `savedErrno` 不相关——函数忽略它。测试只需验证 `umqRet`(即 `umq_state_t`)映射正确。需包含 `"umq_types.h"` 以获取 `QUEUE_STATE_*` 枚举值。

### `UmqErrnoConverter::ConvertHandleResult()` 路径(handle/size返回API)

| 测试场景 | 设置 | 期望errno |
|----------|------|-----------|
| 白名单errno(CREATE时EINVAL) | `errno=EINVAL` | EINVAL |
| 白名单errno(BIND_INFO_GET时ENOMEM) | `errno=ENOMEM` | ENOMEM |
| 非白名单errno | `errno=EBUSY` | EIO |
| errno=0(无信息) | `errno=0` | EIO |

## 常见陷阱

1. **永远不要使用 `_ptr` 赋值** — adapter后端没有 `_ptr` 成员。只用 `MOCKER_CPP(::umq_xxx)`。
2. **永远不要修改 `umq_errno_converter.h` API或映射表** — 枚举值、函数签名、映射数据数组为冻结/final。Doxygen注释可更新以反映实现变更。
3. **`umq_ack_interrupt` 返回void** — 无法检查其返回值做错误映射。
4. **`errno` 必须在调用前设置** — 生产代码在UMQ API调用后立即保存为 `savedErrno`。不设置则converter拿到0，可能回退EIO而非保留"真实"errno。
5. **`-fno-access-control`** — 测试代码可读写private成员如 `sock.umq_handle_`, `rxOps.local_umqh_`。构造测试对象需private状态而无法通过构造函数设置时使用此能力。
6. **`static` AllocMockBuf** — 每次调用返回相同指针。需多个不同buffer的测试应创建多个static数组。
7. **C++11生产, C++17测试** — 生产 `CMakeLists.txt` 用 `-std=c++11`，但test target用 `cxx_std_17`。测试代码可使用C++17特性(如 `constexpr inline`)，但绝不能将C++17-only模式引入生产代码。
8. **Include顺序依赖** — `umq_data_tx_ops.h` 必须在 `umq_buf_converter.h` 之前，因为后者使用 `umq_buf_t`。clang-format `IncludeBlocks: Preserve` 意味着自动排序不重排；手动排序必须遵循依赖链。
9. **UBS_VLOG_ERR 缩进** — 拆分续行必须与调用位置缩进层级对齐，非列0。
10. **`buf->status` 类型是 `uint64_t : 32`** — unsigned bitfield，非 `int`。传入 `ConvertBufStatus` 时cast为 `umq_buf_status_t`(`static_cast<umq_buf_status_t>(buf->status)`)。`%d` 打印时cast为 `int`(`static_cast<int>(buf->status)`)。aarch64上用 `%d` 打印 `uint64_t` 会截断值——这是实际bug。
11. **`umq_handle_` 是 `uint64_t`** — 在 `UBS_VLOG_ERR` 中打印时用 `%llu` + `static_cast<unsigned long long>(umq_handle_)`，非 `%d`。
12. **Share-JFR handle变量语义** — `PrefillRx` 中本地变量 `umq_handle` 在 `UBS_ENABLE_SHARE_JFR=true` 时解析为 `share_umq_handle_`(主UMQ)，`false` 时为 `umq_handle_`(子UMQ)。等ready逻辑必须检查 `umq_handle_`(子UMQ)——子UMQ是新创建的、需IDLE→READY转换；主UMQ早已ready。提取子函数(如 `WaitUntilReady`)时传 `umq_handle_`(成员变量)而非本地 `umq_handle`——传本地变量会导致share-JFR模式检查错误的handle。此陷阱适用于任何涉及主/子UMQ双handle的重构。
13. **`UBS_ENABLE_SHARE_JFR` 默认 `true`** — 测试环境若不显式设为 `false` 则走share路径。重构必须在 `true` 和 `false` 两种模式下都验证。

## 测试工作流

### 步骤1: 分析源文件
- 读取目标 `.cpp` 文件(`csrc/`下)
- 识别所有检查返回值的UMQ API调用
- 识别每个调用对应的 `UmqOperation` 枚举
- 识别使用哪个 `UmqErrnoConverter` API(`Convert`, `ConvertBufStatus`, `ConvertHandleResult`)
- 检查errno是否在转换前保存(应该如此: `int savedErrno = errno;`)

### 步骤2: 设计测试用例
- 对每个错误路径，按上述矩阵创建2-5个测试用例
- 命名: `<Method>_<Scenario>_<ExpectedResult>`
- 确定是否需要 `LockRegistry::RegisterDefaultOps()` 和 `SocketSet::Instance().Init()`

### 步骤3: 编写测试文件
- 使用本skill的文件结构和fixture模式
- 需buffer测试时添加 `AllocMockBuf` 辅助函数
- 使用 `MOCKER_CPP(::umq_xxx)` mock
- 使用 `securec` 函数(`memset_s`, `memcpy_s`)
- 遵守所有代码风格(120字符、4空格缩进、camelBack等)

### 步骤4: 更新CMakeLists.txt
- 按本skill模式添加新test target
- 确定链接类型: converter-only = 仅 `GTest::gtest_main`; ops级 = `ubsocket_static + mockcpp + boundscheck + pthread`

### 步骤5: 构建并运行
```bash
UMQ_BUILD=on UBSOCKET_UT=on bash build/build_umq_and_ubsocket.sh
cd src/ubsocket/build && ./<test_binary_name>
```

### 步骤6: 验证
- 所有测试通过(无segfault、无意外失败)
- 无cmake错误
- 完整ctest: `ctest --test-dir src/ubsocket/build --output-on-failure`

## 高级模式 (经验积累)

### 深度分析方法 (4步)

写测试前，系统分析未覆盖代码:

1. **识别条件**: 什么条件触发此代码路径?(前置条件)
2. **识别依赖**: 它调用什么外部函数?(依赖分析)
3. **识别为何未覆盖**: 正常测试路径为何不到达这里?(路径分析)
4. **总结mock策略**: 根据依赖分析确定mock技术:

| 需要的返回类型 | mock策略 | 示例 |
|---------------|----------|------|
| 固定值 | `.stubs().will(returnValue(x))` | socket返回-1 |
| 随调用顺序变化 | `.will().then()` 链或invoke+计数器 | setsockopt第1次成功、第2次失败 |
| 需填充buffer | 自定义static函数 + invoke | recv填充CLIControlHeader |
| 随参数变化 | 自定义函数 + invoke, 内部检查参数 | dlsym对不同symbol返回不同ptr |

### MOCKER vs MOCKER_CPP — 函数类型区分

| 函数类型 | mock语法 | 示例 |
|----------|----------|------|
| C++类成员函数 | `MOCKER_CPP(&ClassName::method)` | `MOCKER_CPP(&OsAPiMgr::socket)` |
| C全局函数 | `MOCKER(func_name)` | `MOCKER(eventfd)` |
| UMQ C函数(adapter后端) | `MOCKER_CPP(::func_name)` | `MOCKER_CPP(::umq_poll)` |

**注意:** `MOCKER_CPP(::func_name)` 对UMQ C函数有效，因为mockcpp将它们解析为全局符号。纯C libc函数如 `eventfd` 使用 `MOCKER(eventfd)`。

### OsAPiMgr mock — 时序关键

OsAPiMgr包装系统调用(socket, bind, listen, accept, connect, epoll_create, epoll_ctl, epoll_wait, close, read, write, setsockopt, getsockopt)。这些是C++类方法，可mock。

**关键: 在对象创建之前mock**

```cpp
MOCKER_CPP(&OsAPiMgr::socket).stubs().will(returnValue(42));
MOCKER_CPP(&OsAPiMgr::bind).stubs().will(returnValue(0));
MOCKER_CPP(&OsAPiMgr::listen).stubs().will(returnValue(0));
MOCKER_CPP(&OsAPiMgr::close).stubs().will(returnValue(0));
Listener listener;  // 构造函数使用mock值

// 错误 — 创建后再mock太迟了
Listener listener;  // 构造函数已经调用真实socket/bind/listen
MOCKER_CPP(&OsAPiMgr::socket)...  // 无用
```

**必须也mock close** — 析构函数调用 `close(m_fd)` 在mock fd上。缺少close mock导致:
- 在无效fd(42)上执行真实系统调用 → crash或污染
- 状态污染影响后续测试

### .will().then() — 顺序返回值

```cpp
MOCKER_CPP(&OsAPiMgr::setsockopt).stubs()
    .will(returnValue(0))      // 第1次调用成功
    .then(returnValue(-1));    // 第2次调用失败
```

复杂场景(参数依赖返回值)使用 `invoke(staticFunction)` + static计数器。

### invoke限制 — 不能用lambda

```cpp
// 错误 — invoke用lambda导致编译错误
MOCKER_CPP(&SocketFd::ValidateProtocol).stubs()
    .will(invoke([](int fd, uint64_t& protocol, ssize_t& recvSize) { ... }));

// 正确 — invoke用static函数
static int MockValidateProtocol_Success(int fd, uint64_t& protocol, ssize_t& recvSize)
{
    protocol = 0;
    recvSize = 0;
    return 0;
}
MOCKER_CPP(&SocketFd::ValidateProtocol).stubs().will(invoke(MockValidateProtocol_Success));
```

### ALWAYS_INLINE函数策略

`ALWAYS_INLINE` 函数(static或member)无法mock——编译器内联它们，mockcpp无单独符号可拦截。

**策略: mock内部依赖而非inline函数本身**

| 分支类型 | 测试方法 | 示例 |
|----------|----------|------|
| 参数验证 | 传入无效参数 | `Write(nullptr, 10)` |
| 状态检查 | 设置成员变量 | `m_closed = true` |
| TCP路径切换 | 设 `m_rx_use_tcp = true` | 调用真实write |
| inline函数依赖 | mock其调用的C函数 | `GetAndAckEvent` 内部的 `umq_get_cq_event` |
| 复杂内部状态 | 集成测试 | umq_post成功路径 |

**无法单元测试的函数(需集成测试):**
- `SocketFd::SendSocketData` — static ALWAYS_INLINE + 内部函数指针调用
- `SocketFd::RecvSocketData` — static ALWAYS_INLINE + 内部函数指针调用
- `OsAPiMgr::send` (函数指针调用) — 无法拦截

### Testable继承 — 暴露protected成员

```cpp
class TestableListener : public Listener {
public:
    using Listener::Process;          // 暴露protected方法
    using Listener::m_uds_fd;         // 暴露protected成员
    using Listener::m_epoll_fd;
};
```

有 `-fno-access-control` 时也可直接访问private成员如 `sock.umq_handle_`，无需Testable继承。

### Mock函数组织

所有自定义mock函数和测试常量放在test文件顶部的namespace块中:

```cpp
namespace {
static const int STATS_FD_42 = 42;
static ssize_t MockRecvFillStatCommand(int sockfd, void *buf, size_t len, int flags) { ... }
static int g_setsockoptCallCount = 0;
} // namespace
```

### 快速检查清单

写测试前，验证:

**Mock正确性**:
- [ ] C函数用 `MOCKER`, C++方法用 `MOCKER_CPP`
- [ ] Mock在对象构造之前设置(OsAPiMgr时序)
- [ ] 始终包含 `close` mock(析构函数调用close)
- [ ] invoke用static函数，非lambda
- [ ] mock函数签名与真实函数完全匹配
- [ ] 资源一致性: 全mock或全真实，不混用

**测试隔离**:
- [ ] TearDown中清理单例(ProbeManager::GetInstance().Stop(), EidRegistry::UnregisterEid())
- [ ] TearDown中调用 `GlobalMockObject::verify()`
- [ ] 环境变量在单例首次访问之前设置
- [ ] SetUp/TearDown中 `errno = 0` 重置
- [ ] 无重复测试名

**代码质量**:
- [ ] 命名常量，无幻数(TEST_FD_42，非42)
- [ ] 全局变量用 `g_` 前缀
- [ ] mock函数用CamelCase
- [ ] `memcpy_s` / `memset_s` 替代 `memcpy` / `memset`

## 参考文件

| 文件 | 用途 |
|------|------|
| `src/ubsocket/unit_test/umq_errno_converter_test.cpp` | 纯converter逻辑测试(91 cases) |
| `src/ubsocket/unit_test/umq_ops_errno_test.cpp` | Ops级errno映射测试含mockcpp(51 cases, 3 fixture类) |
| `src/ubsocket/unit_test/CMakeLists.txt` | 测试构建配置(2 targets) |
| `src/ubsocket/csrc/core/umq/umq_errno_converter.h` | 冻结 — converter API, 映射表, 枚举 |
| `src/ubsocket/csrc/core/umq/umq_errno_converter.cpp` | converter实现 |
| `src/ubsocket/csrc/under_api/dl_umq_api.h` | UmqApi类 — adapter vs dlopen后端 |
| `src/ubsocket/csrc/core/umq/umq_data_rx_ops.h/.cpp` | RX ops 含errno映射 |
| `src/ubsocket/csrc/core/umq/umq_data_tx_ops.h/.cpp` | TX ops 含errno映射 |
| `src/ubsocket/csrc/core/umq/umq_backend.h/.cpp` | Backend init 含errno映射 |
| `src/ubsocket/csrc/core/umq/umq_socket.h/.cpp` | Socket 含interrupt_fd_get errno映射 |
| `src/ubsocket/csrc/core/umq/umq_epoll_runner_ops.h/.cpp` | Epoll runner 含errno映射 |
| `src/ubsocket/csrc/core/umq/umq_socket_connector.h/.cpp` | Connector 含errno映射 |
| `src/ubsocket/csrc/core/umq/umq_socket_acceptor.h/.cpp` | Acceptor 含errno映射 |
| `doc/ubsocket/UBSOCKET-BRPC-ERRNO-MAPPING.ch.md` | errno映射设计文档 |