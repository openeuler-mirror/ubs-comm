---
name: ut-gen
description: Use when generating or modifying C++ unit tests for the ubsocket component of ubs-comm. Trigger on keywords: UT, 单元测试, unit test, test generation, 测试生成, mockcpp, gtest, ctest, CMake test target, unit_test. Use ONLY when the task involves writing or modifying C++ unit test code for ubsocket. Module-specific detail: modules/<module>.md (load on demand); coverage data: docs/ubsocket/UBSOCKET-COVERAGE-ANALYSIS.ch.md; claiming/coordination: docs/ubsocket/UBSOCKET-CLAIMING.md.
---

# UBSocket UT 生成 Skill

为 `src/ubsocket/csrc/` 生成与修改 C++ 单元测试的唯一入口。测试框架: GoogleTest 1.12.1 + mockcpp v2.7,`ctest` 运行。

## 1. 适用范围与术语

| 术语 | 定义 |
|------|------|
| 用例 (case) | 一个 `TEST` / `TEST_F` 宏,当前全仓 2177 个(2026-08-29 实测) |
| 测试二进制 (target) | 一个 ctest 单元,当前 59 个,对应一个 `<feature>_test` 可执行文件 |
| mock | 用 mockcpp 在 API 边界模拟依赖——**UT 中唯一允许的依赖替换手段** |
| 桩 (stub) | UT 语境已废弃(旧 `unit_test/stub/` 已于 commit `61db74c0` 删除)。"桩"另有编译桩与行为级 URMA 桩两种含义,与本语境无关,见 `CONTEXT.md` §桩体系 |
| 冻结 (frozen) | `umq_errno_converter.h` 的 API/枚举/映射表——永久不得修改 |
| 验证 (verification) | 质量轴——边界敏感类判定点用**边界值+相邻值**显式断言行为,与覆盖率正交(术语定义见 `CONTEXT.md` §覆盖/验证) |
| 边界敏感类判定点 | 六类强制边界验证的判定点: 长度/容量截断、队列满/空、容量上限、超时/熔断、枚举/映射表、版本/协议协商;实例清单见模块附录"边界清单" |

模块范围的每个文件与测试清单见 `modules/<module>.md`(按目标模块加载,见 §11 指针表)。

## 2. 覆盖率目标与验收

- **模块目标**: 每个模块 行 ≥80% / 分支 ≥50%,**越高越好不设上限**;全局达标靠模块超额兜底(决策依据见 `docs/adr/0001-coverage-policy-per-module-80-50-no-cap.md`)
- **数字唯一权威**: `docs/ubsocket/UBSOCKET-COVERAGE-ANALYSIS.ch.md`——任何覆盖率数字只写在那里,本 skill 与模块附录不复制任何数字
- **强制方式**: 软强制,不设 CI 门槛。每轮 sprint 末协调人重跑 `make coverage` 更新数据源;合入前按模块目标逐文件核对
- **认领与分工**: `docs/ubsocket/UBSOCKET-CLAIMING.md`(协调页:认领表/进度/里程碑,分工由负责人填写)
- **质量加固目标(验证轴)**: 边界敏感类判定点(长度/容量截断、队列满/空、容量上限、超时/熔断、枚举/映射表、版本/协议协商)须用**边界值 + 相邻值**显式断言行为,与覆盖率正交——分支被覆盖 ≠ 边界语义被验证。只产生清单项,**不产生新数字**;实例见各模块附录"边界清单"(首次认领时填写)
- **问题记录**: 写测试发现 **csrc 生产缺陷** → 发现即记,经 `/record-issue` 进 `.scratch/<feature-slug>/`(状态 `needs-triage`,记录即放行);**测试模式/陷阱** → §11 知识回流;阻塞类(测试无法编写/需设计决策)必须解决或协调人接受后才过 DoD
- **完成定义 (DoD)**: 目标文件达到模块目标(80/50)→ 全量 `ctest` 通过 → 用例全部 mockcpp(无桩)→ 边界清单逐项有对应用例 → 发现的问题已按"问题记录"条款处理 → 过 §9 评审清单

## 3. 构建与运行

命令权威源: `AGENTS.md` §Build Commands(本表只列高频三条)。

| 目的 | 命令 |
|------|------|
| 构建 UT+覆盖率 | `USE_URMA_STUB=on UMQ_BUILD=on UBSOCKET_UT=on UBSOCKET_COVERAGE=on bash build/build_umq_and_ubsocket.sh`(本地缺 3rdparty urma 头必须 `USE_URMA_STUB=on`;勿裸跑 `cmake ..` 覆盖缓存,详见 `AGENTS.md` §构建选项补充) |
| 跑全量 UT | `ctest --test-dir src/ubsocket/build --output-on-failure` |
| 跑单个 target | `cd src/ubsocket/build && ./<test_name>`(链了 `ubsocket_static` 的 target 需 `LD_LIBRARY_PATH=src/hcom/umq/build/src:src/hcom/umq/build/src/qbuf`) |

单文件覆盖率自检(认领前看起点、合入前看增量): 用 lcov `--extract` 提取目标文件,`--initial --capture` 两步骤必须都做才能纳入零覆盖率文件;lcov 1.16 只用 `--ignore-errors gcov`(详见 `AGENTS.md` §关键陷阱)。

## 4. 测试文件组织与命名

### 位置

```
src/ubsocket/unit_test/            ← 通用/顶层测试(38 个 target)
src/ubsocket/unit_test/umq/        ← UMQ 模块测试(19 个 target)
src/ubsocket/unit_test/profiling/  ← profiling 模块测试(10 个 target)
```

新测试文件**必须注册 CMake target**,否则 ctest 不跑。反例: `ubsocket_bigdata_handler_test.cpp` 是孤儿文件(无任何 CMakeLists 引用)。同一源文件**不要**注册到两个 CMakeLists(历史遗留: `umq_setting_multi_level_test.cpp` 被顶层与 `umq/` 各注册了一次)。

### 命名约定

| 对象 | 约定 | 示例 |
|------|------|------|
| 测试二进制 | `<feature>_test` | `umq_socket_connector_test` |
| fixture 类 | `<Feature>Test` | `UmqConnectorOpsTest` |
| 用例 | `<Method>_<Scenario>_<ExpectedResult>` | `PrepareConnect_HandshakeOpt_SetsockoptSuccess_ConnectSuccess` |

### Fixture 模板(ops 级测试)

```cpp
class UmqConnectorOpsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        SetLibcApiPtrsToNull();   // 本测试用到的 LibcApi::xxx_ptr 全部恢复 nullptr
        ArraySet<Socket>::GetInstance().ReleaseAll();
    }
};
```

构造依赖 lock/socket 基础设施的对象(`UmqRxOps`/`UmqTxOps`/connector 等)前,必须先 `LockRegistry::RegisterDefaultOps()` + `ArraySet<Socket>::GetInstance().Init()`;纯逻辑测试(converter、validator、纯 RAII)不需要。**完成标准**: 每个用例结尾与 `TearDown()` 都调用 `GlobalMockObject::verify()`。

## 5. mock 策略

**一律使用 mockcpp**——不重建、不引用任何桩设施。依赖在 API 边界上 mock,分四类语法:

| 目标 | 语法 | 实测示例 |
|------|------|---------|
| 全局 C 函数(含 libc、`::umq_*`) | `MOCKER_CPP(::func)` | `MOCKER_CPP(::umq_data_to_head).stubs().will(returnValue(&m_qbuf))` |
| 类成员函数 | `MOCKER_CPP(&Class::method)` | `MOCKER_CPP(&UmqTxOps::DoUmqTxPoll).expects(exactly(2)).will(returnValue(1)).then(returnValue(0))` |
| 类静态方法 | `MOCKER(&Class::method)` | `MOCKER(&LibcApi::recv).stubs().will(invoke(&FakeRecv))` |
| 类虚成员函数 | `MOCKER_CPP_VIRTUAL(实例, &Class::method)` | `MOCKER_CPP_VIRTUAL(ops, &UmqConnectorOps::PrepareConnect).expects(exactly(1)).will(returnValue(UBS_OK))` |

虚成员函数不能用 `MOCKER_CPP(&Class::method)`(member-pointer 编码成 vtable 偏移,被当代码地址打桩即 SEGV);必须用 `MOCKER_CPP_VIRTUAL` 从实例 vtable 槽位读出真实地址后再函数级打桩,且实例须在注册打桩到 `GlobalMockObject::verify()` 期间存活(实测示例: `ubsocket_socket_connector_test.cpp`、`umq_conn_helper_test.cpp`)。

include 一律 `<mockcpp/mockcpp.hpp>`(不是 `.h`)。

### LibcApi: 逐函数 `xxx_ptr` 替换

`LibcApi` 每个函数有独立函数指针成员(`setsockopt_ptr`/`connect_ptr`/`open_ptr` 等,`DL_API_DECLARE` 生成,初始 `nullptr`)。mock 方式:**直接赋值自定义 static 函数,用后恢复 `nullptr`**:

```cpp
static int MockSetsockoptSuccess(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    g_mockSetsockoptCallCount++;
    return 0;
}

TEST_F(UmqConnectorOpsTest, SetSockOpt_FailEnoprotoopt_ReturnMinusOne)
{
    LibcApi::setsockopt_ptr = MockSetsockoptSuccess;
    // ... 被测代码 ...
    LibcApi::setsockopt_ptr = nullptr;
}
```

- variadic 函数(`LibcApi::open`,签名 `int open(const char*, int, ...)`)mockcpp **无法** mock——必须用 `xxx_ptr` 替换
- `_ptr` 未设置时经 `nullptr` 函数指针调用 → segfault;`TearDown` 必须恢复
- 现无任何测试调用 `LibcApi::Load()`——全部走 `xxx_ptr` 替换
- **`UmqApi` 例外**: adapter 后端(默认)没有 `_ptr` 成员,只能 `MOCKER_CPP(::umq_xxx)`,**绝不**给 `UmqApi::xxx_ptr` 赋值(dlopen 后端专属,未启用)

### errno 先设后测

生产代码在 UMQ API 调用后立即 `int savedErrno = errno;` 保存再交给 `UmqErrnoConverter`。测试**必须在调用被测函数之前设置 `errno`**,断言的是映射后的最终 `errno`,不是原始 UMQ 错误码。示例:

```cpp
TEST_F(UmqRxOpsTest, RearmRxInterrupt_FailEpermSavedEinval_MapsEinval)
{
    UmqRxOps rxOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_rearm_interrupt).stubs().will(returnValue(-UMQ_ERR_EPERM));
    errno = EINVAL;
    int ret = rxOps.RearmRxInterrupt();
    EXPECT_EQ(ret, -UMQ_ERR_EPERM);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}
```

### 不可 mock 的情形

| 情形 | 处理 |
|------|------|
| 返回 `void` 的 UMQ API(如 `umq_ack_interrupt`) | 无返回值可验证,不做错误路径测试 |
| 返回数组的 API(如 `umq_dev_info_t[1]`) | mockcpp 无法 mock 数组返回,测试走指针返回类型路径 |
| `ALWAYS_INLINE` 函数 | 编译器内联,无符号可拦截——mock 其**内部依赖**而非函数本身 |
| 顺序返回值 | `.will(returnValue(0)).then(returnValue(-1))` |
| 参数依赖返回值 | 自定义 static 函数 + `invoke`,签名必须与真实函数完全匹配(含类型,如 `FILE*` vs `_IO_FILE*`) |
| lambda 作为 invoke 参数 | **禁止**——invoke 只接受 static 函数,lambda 编译报错 |

### 其他约束

- `-fno-access-control`: 仅 test target 启用,测试代码可直接读写 private 成员(`sock.umq_handle_`、`poller.mutex_`)。生产代码不可用
- mockcpp `.stubs()` 默认返回 Void: 非 void 返回类型必须加 `.will(returnValue(...))`,否则运行期抛异常
- 资源一致性: 同一路径全 mock 或全真实,不混用
- mock 必须在**对象构造之前**设置(构造函数可能已调用真实依赖);析构调用的 `close` 等必须 mock
- **重复挂载序列提取 helper**(umq_backend_test 实战): 多个用例共用的一组 `MOCKER_CPP` 挂载(如 Init 全链 = umq_init + dev_info_list_get/free + dev_add + socket id)提取为 `MountXxxMocks()` static 函数,用例一行调用;配套的全局状态准备(设备数组/计数/静态成员)提取为 `PrepareXxx()` helper,避免每用例 10+ 行样板且保证各用例状态一致

## 6. CMake 注册模板

权威参考: `src/ubsocket/unit_test/CMakeLists.txt`、`unit_test/umq/CMakeLists.txt`、`unit_test/profiling/CMakeLists.txt`(当前实现即模板)。**已无 stub 链接分支**。

### 顶层通用模板(链 `ubsocket_static`)

```cmake
add_executable(ubsocket_ring_buffer_test "")
add_test(NAME ubsocket_ring_buffer_test COMMAND ubsocket_ring_buffer_test)
set_target_properties(ubsocket_ring_buffer_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_ring_buffer_test PRIVATE cxx_std_17)
target_compile_definitions(ubsocket_ring_buffer_test PRIVATE UBSOCKET_UNIT_TEST)
target_include_directories(ubsocket_ring_buffer_test PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/core
)
target_link_libraries(ubsocket_ring_buffer_test PRIVATE
    # 不链 boundscheck(securec 安全库): csrc 生产代码与测试代码均无 securec 函数调用,
    # 链了会给动态库版本引入多余 DT_NEEDED。实测对照(2026-08-28):
    # ubsocket_socket_helper_test 不链 boundscheck,构建运行正常
    ubsocket_static mockcpp GTest::gtest_main pthread
)
target_sources(ubsocket_ring_buffer_test PRIVATE ubsocket_ring_buffer_test.cpp)
```

### umq 子目录(`add_umq_test` 函数)

```cmake
add_umq_test(<test_name>)                     # 含 -fno-access-control + UMQ include 目录
# converter-only 形态:
target_link_libraries(<test_name> PRIVATE GTest::gtest_main)
target_sources(<test_name> PRIVATE
    <test_name>.cpp
    ${UBSOCKET_BASE_DIR}/csrc/core/umq/umq_errno_converter.cpp
)
# ops 级形态:
target_link_libraries(<test_name> PRIVATE
    ubsocket_static mockcpp GTest::gtest_main pthread   # 不链 boundscheck,理由见 §6 顶层模板注释
)
```

### profiling 子目录(轻量形态)

不链 `ubsocket_static`: 只链 `GTest::gtest_main + mockcpp`,8 个 profiling 实现 `.cpp` + `ubsocket_global_setting.cpp` 直接编入 source(见 `unit_test/profiling/CMakeLists.txt`)。

**完成标准**: 注册后 `ctest -N` 能列出新 target。

## 7. 硬约束

| 约束 | 说明 |
|------|------|
| 一律 mockcpp | 不引入桩、不直接依赖真实硬件/umdk 环境 |
| 单用例执行 ≤1s | 禁止长 sleep、阻塞等待、密集计算;等待异步事件用短超时(≤100ms)+轮询 |
| 测试 C++17 / 生产不受此约束 | test target `cxx_std_17`;生产代码不引入 C++17-only 特性 |
| `umq_errno_converter.h` 冻结 | 枚举值、函数签名、映射数据数组永不修改;Doxygen 注释可更新 |
| 命名 | 全局变量 `g_` 前缀;mock 函数 CamelCase |
| 幻数 | 用命名常量(`TEST_FD_42` 而非 `42`),**含枚举强转注入值**(`static_cast<ub_trans_mode>(100)` → `TEST_TRANS_MODE_INVALID_POSITIVE`,umq_conn_helper/connector 实战);豁免: 0/1/±1、枚举成员、API 返回码常量、纯测试数据集合(`{0, 1, 2}`) |

## 8. 常见陷阱(通用)

模块特有陷阱见对应 `modules/<module>.md`;构建系统陷阱见 `AGENTS.md` §关键陷阱。

1. **UmqApi 无 `_ptr`** — adapter 后端(默认)只有 `MOCKER_CPP(::umq_xxx)` 一条路;`_ptr` 赋值仅 dlopen 后端(未启用)存在
2. **LibcApi `xxx_ptr` 初始 `nullptr`** — 未设置即调用 → segfault;`TearDown` 必须恢复;`open` 是 variadic,mockcpp 无法 mock,只能 `open_ptr` 替换
3. **errno 必须调用前设置** — 不设置则 converter 拿到 0,可能回退 EIO 而非保留"真实"errno
4. **`.stubs()` 默认返回 Void** — 非 void 返回类型必须 `.will(returnValue(...))`
5. **invoke 必须 static 函数且签名完全匹配** — 含参数顺序与类型;glibc 内部类型冲突(如 `fgets` 的 `FILE*` vs `_IO_FILE*`)避免 `invoke`,用 `.will(returnValue(...))`
6. **`buf->status` 是 `uint64_t : 32` bitfield** — 传 `ConvertBufStatus` 前 `static_cast<umq_buf_status_t>`;aarch64 上 `%d` 打印 `uint64_t` 截断值
7. **`umq_handle_` 是 `uint64_t`** — 日志打印用 `%llu` + `static_cast<unsigned long long>`
8. **`mockcpp::Result` vs `ock::ubs::Result` 冲突** — `using namespace ock::ubs;` + 显式限定 `ock::ubs::Result`
9. **`IO_SIZE_MB` 是宏** — `ubsocket_defines.h` 中定义,测试不可重定义为 `constexpr`
10. **单例测试必须清理** — LeakySingleton 显式 unregister;类单例 `ReleaseAll()`;`ProbeManager::GetInstance().Stop()`(详见 `modules/common.md` §单例清理三件套)
11. **`UBS_VLOG_ERR` 行宽 120** — 超宽字符串拆分为相邻字面量,续行与调用位置缩进对齐,非列 0
12. **mock 漏挂载 = 静默打真实库(假阳性)** — 想覆盖某 API 失败路径却忘挂 `MOCKER_CPP`,adapter 后端会直接调真实 `::umq_xxx`;若真实库对该输入恰好也返回失败(如非法句柄),用例"通过"但断言依赖库副作用(如未初始化句柄的 errno),`g_xxxRet` 注入值成死代码,且库行为一变就挂。**失败路径用例写完必须自查: 注入的 mock 全局是否有对应的 `MOCKER_CPP` 挂载**(umq_transport_pool_test 实战,审查硬违规)
13. **死 mock 函数 = 陷阱 #12 反向形态(假阴性)** — 定义了 `MockXxx` static 函数 + `g_xxxRet` 注入变量,但**从未**有 `MOCKER_CPP(...).will(invoke(&MockXxx))` 挂载: 注入变量成死代码、用例断言恒为默认值,覆盖率无损失但被测失败分支从未真正触发,代码改错也测不出(umq_backend_test 实战,审查发现 4 个)。自查: mock 函数定义处 `grep` 反向引用——只被"定义 + 测试内引用注入变量"而从未被 `invoke` 引用的函数即死 mock,**删除函数 + 注入变量**(若某用例需 `returnValue` 直挂则保留注入变量,仅删函数;计数仍要 invoke 的用例见 umq.md #16)
14. **全局 static 模式 latch + 配置驱动的双实现分派 C API** — 某 C API 按文件内 static latch(无 getter,由 init 重读全局配置,如 `ubsocket_prof_mode_ext`←`GlobalSetting::UBS_PROF_MODE`)分派到两个实现类。**测非默认分支的 record/combind/reset 类入口前,必须先调 init 把 latch 置到目标值**,否则入口落在默认实现、针对另一实现的 mock 成死代码(假阳性);`SetUp` 显式复位默认配置值 + `TearDown` 调 uninit 复位 latch,防跨用例污染(ubsocket_profiling_test 实战,详见 `modules/profiling.md` #29)
15. **`ubsocket_init()` 依赖链隔离** — 初始化成功 UT 需 mock `DlApi::Load` 与 `umq::UmqBackend::Init`，并在结束调用 `ubsocket_uninit()` 清理 `ArraySet`/runner；`async_epoll_thread_count` 的配置上限为 1，测试注入 2 会被 `VerifySetting()` 拒绝。
16. **mockcpp 返回类型按声明匹配** — `Profiling::Init/Uninit` 的声明返回 `int`，不能直接对其使用 `returnValue(UBS_OK)`(枚举类型会触发运行时类型异常)，应使用 `returnValue(0)`；`Result` 返回值显式写 `static_cast<ock::ubs::Result>`。
17. **后台线程入口不可盲目 mock** — `ProbeManager::Start`、`StatExporter::Init` 等入口可能是 inline 或直接创建线程，若 mock 未命中会启动真实后台线程，造成 UT 挂起。应 mock 可注入的底层依赖，或为可选组件使用独立 fixture；不得保留会挂起的覆盖率用例。
18. **inline formatter 测试不强行制造 branch** — `ubsocket_struct_helper.h` 的 `operator<<` 是纯字段串行输出，没有条件分支；使用 `ubsocket_struct_helper_test.cpp`/`ubsocket_struct_helper_test`，覆盖完整对象和全零对象即可。lcov 显示 `branch: no data found` 属于正确结果。
19. **测试文件名必须对应被测入口** — `csrc/ubsocket.cpp` 使用 `ubsocket_test.cpp` + `ubsocket_test`；功能专项（如 degrade/socket）应保持独立 target，不要把入口文件 UT 混入专项 target，避免覆盖率归属错误。

## 9. 评审清单(提测前逐项勾选)

`/code-review` 的 Standards 轴以此清单为准。

**Mock 正确性**
- [ ] 语法三选正确: 全局 C 函数 `MOCKER_CPP(::func)` / 类成员 `MOCKER_CPP(&Class::method)` / 类静态 `MOCKER(&Class::method)`
- [ ] mock 在对象构造之前设置;析构调用的 `close` 等已 mock
- [ ] invoke 用 static 函数,非 lambda;签名与真实函数完全匹配
- [ ] 资源一致性: 全 mock 或全真实,不混用
- [ ] `LibcApi::xxx_ptr` 赋值后已恢复 `nullptr`;未给 `UmqApi::xxx_ptr` 赋值

**测试隔离**
- [ ] 每个用例结尾与 `TearDown()` 都调 `GlobalMockObject::verify()`
- [ ] `errno = 0` 在 `SetUp`/`TearDown` 重置;errno 场景在调用前设置
- [ ] 单例已清理(`ReleaseAll()`/`UnregisterEid()`/`Stop()`);环境变量在单例首次访问前设置
- [ ] 无重复测试名;单用例 ≤1s

**边界验证与问题记录**
- [ ] 模块附录"边界清单"列出的每个边界敏感判定点有边界值 + 相邻值用例(断言行为,非仅执行)
- [ ] 写测试时发现的 csrc 生产缺陷已 `/record-issue`(状态 `needs-triage`)
- [ ] 阻塞类问题已解决,或协调人明确接受
- [ ] 新的测试模式/陷阱已按 §11 回流(本 skill 或模块附录)

**代码质量**
- [ ] 命名符合 §4 约定(`<Method>_<Scenario>_<ExpectedResult>`)
- [ ] 命名常量无幻数;全局变量 `g_` 前缀;mock 函数 CamelCase
- [ ] 不引用已删除/不存在的类名函数名(新引用一律 grep 验证存在)

## 10. 工作流

| 步骤 | 动作 | 完成标准 |
|------|------|---------|
| 1. 分析源文件 | 读目标 `.cpp`,识别检查返回值的 UMQ/系统调用点、各调用点 `UmqOperation`、用的哪个 Convert API、errno 是否先存(§深度分析 4 步);对照模块附录"边界清单"标记本文件边界敏感判定点 | 列出调用点清单,每个标注 op + Converter API + savedErrno 行为;边界敏感判定点清单 |
| 2. 设计用例 | 每个错误路径 ≥1 case(参照 §5 示例与 `modules/umq.md` §Errno 映射要点);边界清单每个判定点有边界值 + 相邻值用例 | 用例名全部符合 `<Method>_<Scenario>_<ExpectedResult>`;边界用例齐全 |
| 3. 编写测试文件 | §4 fixture 模板 + §5 mock 策略;新引用的类/函数先 grep 验证存在 | 文件自含所有 mock 函数与常量(匿名 namespace),`verify()` 齐 |
| 4. 注册 CMake target | §6 模板,注册到正确 CMakeLists | `ctest -N` 列出新 target;无孤儿文件 |
| 5. 构建运行 | §3 命令 | 单 target 通过 + 全量 `ctest` 59/59 通过 |
| 6. 覆盖率自检 | lcov `--extract` 单文件,记录行/分支 | 达到模块目标(80/50);未达则补 case 回到第 2 步 |
| 7. 评审与回流 | 过 §9 评审清单(含边界与记录组);新陷阱按 §11 回流;发现的生产缺陷已 `/record-issue` | 清单全勾;合入后向协调人汇报该文件数字与记录的问题 |

### 深度分析 4 步

写测试前,系统分析未覆盖代码:

1. **识别条件**: 什么条件触发此代码路径?(前置条件)
2. **识别依赖**: 它调用什么外部函数?(依赖分析)
3. **识别为何未覆盖**: 正常测试路径为何不到达这里?(路径分析)
4. **总结 mock 策略**: 按依赖分析确定 mock 技术——固定值 `.will(returnValue(x))`;顺序 `.will().then()` 链;填充 buffer 用自定义 static 函数 + `invoke`;随参数变化用 `invoke` 内部检查参数。

## 11. 模块附录与知识回流

### 模块指针表(按目标模块加载)

| 目标代码 | 加载文件 |
|---------|---------|
| `csrc/core/umq/` | `modules/umq.md` |
| `csrc/core/`(不含 `umq/`、`urma/`) | `modules/core.md` |
| `csrc/common/` | `modules/common.md` |
| `csrc/under_api/` | `modules/under-api.md` |
| `csrc/profiling/` | `modules/profiling.md` |
| `csrc/iobuf/` | `modules/iobuf.md` |

`csrc/core/urma/` 未启用(`BUILD_URMA_DLOPEN_BACKEND=OFF`,见 `CONTEXT.md`),当前不产生测试任务。

### 知识回流(单一目标,不许二选一)

| 发现类型 | 回流目标 |
|---------|---------|
| mockcpp/fixture/CMake 通用模式 | 本 skill §5/§6 |
| 模块特有陷阱、文件清单变化 | 对应 `modules/<module>.md` |
| 覆盖率数字(任何百分比/行数) | `docs/ubsocket/UBSOCKET-COVERAGE-ANALYSIS.ch.md`(唯一权威,本 skill 不写数字) |
| 构建系统/架构问题 | `AGENTS.md` §关键陷阱 / §Known Gotchas |
| 协调/认领状态 | `UBSOCKET-CLAIMING.md`(协调页) |
| 边界清单实例变化(新增/更正边界敏感判定点) | 对应 `modules/<module>.md` §边界清单 |

回流纪律详见 `.opencode/README.md` §如何更新 Skill。
