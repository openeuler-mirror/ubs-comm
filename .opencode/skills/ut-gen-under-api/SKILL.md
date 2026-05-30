---
name: ut-gen-under-api
description: Module-specific skill for writing UT for files under src/ubsocket/csrc/under_api/. Load together with ut-gen skill when testing dlopen/dlsym API wrapper code. Trigger on keywords: DlApi, DlUmqApi, UmqApi, UurmaApi, dlopen, dlsym, dl_api, dl_umq_api, dl_libc_api, umq_api, under_api.
---

# Under-API模块 UT 生成子skill

> **权威数据源**: `doc/ubsocket/UBSOCKET-COVERAGE-ANALYSIS.ch.md` 四.4.1-4.3 — Under-api模块5.2%(269行)+urma模块0.0%(267行), 需+414行达80%。
> **协调跟踪**: `.opencode/skills/ut-coverage-coord/SKILL.md` — 文件认领与进度。

## 模块范围

`src/ubsocket/csrc/under_api/` 下的文件:
- `dl_api.h/.cpp` — DlApi (dlopen/dlsym通用包装器)
- `dl_umq_api.h/.cpp` — DlUmqApi (通过dlopen加载UMQ函数)
- `dl_libc_api.h/.cpp` — DlLibcApi (通过dlopen加载libc函数)
- `umq_api.h/.cpp` — UmqApi (静态包装器: adapter后端 vs dlopen后端)

## UT约束

- **stub默认不启用**: 新增UT时优先使用mockcpp(`MOCKER_CPP`/`MOCKER`)mock C API和系统调用，**默认不使用stub方式**(fake_epoll_static/AllocMockBufWithBlock/SocketTestHelper等)。只有用户明确指定需要stub实现时才启用。
- **单用例执行≤1s**: 每个`TEST_F`用例的执行时间不超过1秒。禁止在测试中使用长时间sleep、阻塞等待、密集计算循环等。如需等待异步事件，使用短超时(≤100ms)+轮询。

## 两种后端模式

### Adapter后端(UMQ_ADAPTER_BACKEND_ENABLED, 默认ON)

`UmqApi` 提供静态包装方法，直接调用 `::umq_*` 全局C函数:
```cpp
int UmqApi::umq_poll(uint64_t umq_handle, int io_direction, umq_buf_t **buf, int max_buf_size)
{
    return ::umq_poll(umq_handle, io_direction, buf, max_buf_size);
}
```

**mock模式:** `MOCKER_CPP(::umq_poll).stubs().will(returnValue(...))`

**此后端路径没有 `_ptr` 成员变量。**

### Dlopen后端(UMQ_DLOPEN_BACKEND_ENABLED, 很少使用)

`UmqApi` 使用 `_ptr` 成员变量，通过 `DlUmqApi::LoadSymbols()` 加载:
```cpp
UmqApi::umq_poll_ptr = (umq_poll_func_t)dlsym(handle, "umq_poll");
```

**dlopen后端mock模式:** 直接设置 `_ptr`:
```cpp
UmqApi::umq_poll_ptr = MockUmqPoll;
```

**注意:** 大多数测试使用adapter后端。仅显式需要时才测试dlopen后端。

## DlApi — dlopen/dlsym包装器

`DlApi` 包装 `dlopen` 和 `dlsym` 调用。**这些是C全局函数** — 用 `MOCKER` mock:

```cpp
MOCKER(dlopen).stubs().will(returnValue(&g_mockHandle));
MOCKER(dlsym).stubs().will(invoke(MockDlsymReturnDifferentPtrPerSymbol));
MOCKER(dlclose).stubs().will(returnValue(0));
```

### MockDlsym — 参数依赖返回值

`dlsym` 必须对不同符号名返回不同函数指针。用 `invoke` + static函数:

```cpp
static void *g_mockUmqPollPtr = reinterpret_cast<void *>(0x1000);
static void *g_mockUmqInitPtr = reinterpret_cast<void *>(0x2000);

static void *MockDlsymReturnDifferentPtrPerSymbol(void *handle, const char *symbol)
{
    if (strcmp(symbol, "umq_poll") == 0) return g_mockUmqPollPtr;
    if (strcmp(symbol, "umq_init") == 0) return g_mockUmqInitPtr;
    return nullptr;  // 未知符号 → 加载失败
}

MOCKER(dlsym).stubs().will(invoke(MockDlsymReturnDifferentPtrPerSymbol));
```

**关键:** invoke不能用lambda — 必须用static函数。

## DlUmqApi — UMQ符号加载

`DlUmqApi::LoadSymbols()` 对每个UMQ函数调用 `dlsym`。测试方法:
1. mock `dlopen` 返回有效handle
2. mock `dlsym` 返回函数指针(用上面的invoke模式)
3. 验证所有 `_ptr` 成员正确设置
4. 测试失败: `dlsym` 对某个符号返回nullptr → LoadSymbols失败

```cpp
TEST_F(DlUmqApiTest, LoadSymbols_AllSymbolsFound_Success)
{
    MOCKER(dlopen).stubs().will(returnValue(&g_mockHandle));
    MOCKER(dlsym).stubs().will(invoke(MockDlsymReturnAllValid));
    MOCKER(dlclose).stubs().will(returnValue(0));

    EXPECT_TRUE(DlUmqApi::LoadSymbols("libumq.so"));
    EXPECT_NE(UmqApi::umq_poll_ptr, nullptr);
    GlobalMockObject::verify();
}

TEST_F(DlUmqApiTest, LoadSymbols_SomeSymbolMissing_Fail)
{
    MOCKER(dlopen).stubs().will(returnValue(&g_mockHandle));
    MOCKER(dlsym).stubs().will(invoke(MockDlsymReturnNullForPoll));  // umq_poll返回nullptr
    MOCKER(dlclose).stubs().will(returnValue(0));

    EXPECT_FALSE(DlUmqApi::LoadSymbols("libumq.so"));
    GlobalMockObject::verify();
}
```

## UmqApi — Adapter vs Dlopen路径

### Adapter路径测试(默认)

直接用 `MOCKER_CPP(::umq_xxx)`，如ut-gen skill所述。UmqApi是薄包装器，无特殊UmqApi专属测试需求。

### Dlopen路径测试(很少)

测试 `_ptr` 赋值和通过 `_ptr` 调用产生期望结果:

```cpp
// 设置mock函数指针
UmqApi::umq_poll_ptr = MockUmqPollFunc;
int ret = UmqApi::umq_poll(handle, UMQ_IO_RX, buf, max_buf_size);
EXPECT_EQ(ret, expected_value);
```

## 常见陷阱

1. **不要混用adapter和dlopen模式** — adapter用 `MOCKER_CPP(::func)`, dlopen用 `_ptr` 赋值
2. **dlsym mock必须参数依赖** — 不同符号需要不同返回值。用 `invoke(staticFunc)`, 非lambda
3. **必须mock dlclose** — DlApi析构函数调用dlclose
4. **`_ptr` 成员仅在 `UMQ_DLOPEN_BACKEND_ENABLED` 下存在** — adapter后端测试中使用它们是错误的
5. **DlApi通常通过DlUmqApi/DlLibcApi使用** — 测试特定子类，而非泛型DlApi

## 优先覆盖的新文件

| 文件 | 行数 | 复杂度 | 优先级 |
|------|------|--------|--------|
| `dl_api.cpp` | ~80 | 中 | P2 — dlopen/dlsym包装 |
| `dl_umq_api.cpp` | ~100 | 中 | P2 — 符号加载 |
| `dl_libc_api.cpp` | ~60 | 低 | P3 — libc符号加载 |
| `umq_api.cpp` | ~40 | 低 | P3 — 薄静态包装器 |

## 如何使用本Skill

### 触发与加载

触发关键词见本skill YAML frontmatter `description`字段。加载规则见`.opencode/README.md` §全局规则——写under-api模块UT时需与`ut-gen`(root skill)一起加载。

### 工作流程

1. **认领文件** — 从§优先覆盖的新文件中选择目标文件
2. **确定后端模式** — 默认测试adapter后端(§Adapter后端); 仅显式需要时测试dlopen后端(§Dlopen后端)
3. **设计case** — adapter路径用`MOCKER_CPP(::umq_xxx)`; dlopen路径用§MockDlsym的invoke+static函数
4. **编写测试** — dlsym mock必须参数依赖(invoke+static函数，非lambda)
5. **构建验证** — 命令见ut-gen §构建与运行
6. **报告进度** — 更新ut-coverage-coord进度表+覆盖率增量

## 知识回流

按`.opencode/README.md` §如何更新Skill回流。Under-api模块特定判断:

| 发现类型 | 判断条件 | 回流目标 |
|----------|---------|----------|
| dlopen/dlsym mock模式 | 涉及`dlopen`/`dlsym`/`dlclose`调用 | 本skill §DlApi或§MockDlsym |
| 两后端切换陷阱 | 涉及adapter/dlopen路径选择或`_ptr`混用 | 本skill §常见陷阱 |
| 跨模块适用 | 不限于under-api模块 | `ut-gen` §mockcpp模式/§常见陷阱 |

### 回流更新检查清单

- [ ] 新dlsym invoke模式 → 本skill §MockDlsym
- [ ] 新adapter/dlopen切换陷阱 → 本skill §常见陷阱

## 参考文件

| 文件 | 用途 |
|------|------|
| `src/ubsocket/csrc/under_api/dl_api.h/.cpp` | DlApi源码 |
| `src/ubsocket/csrc/under_api/dl_umq_api.h/.cpp` | DlUmqApi源码 |
| `src/ubsocket/csrc/under_api/umq_api.h/.cpp` | UmqApi源码 |