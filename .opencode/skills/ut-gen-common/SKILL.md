---
name: ut-gen-common
description: Module-specific skill for writing UT for files under src/ubsocket/csrc/common/. Load together with ut-gen skill when testing global settings, lock, logger, errno, singleton, threadpool, scope exit, set, statistics, signal handler, setting validator code. Trigger on keywords: GlobalSetting, LeakySingleton, Lock, LockRegistry, ThreadPool, ScopeExit, UbsocketSet, ObjStatistics, SignalHandler, SettingValidator, UbsocketErrno.
---

# Common模块 UT 生成子skill

> **权威数据源**: `doc/ubsocket/UBSOCKET-COVERAGE-ANALYSIS.ch.md` 四.4.1-4.3 — Common模块覆盖率基线20.1%(642行), 需+384行达80%。
> **协调跟踪**: `.opencode/skills/ut-coverage-coord/SKILL.md` — 文件认领与进度。

## 模块范围

`src/ubsocket/csrc/common/` 下的文件:
- `ubsocket_global_setting.h/.cpp` — GlobalSetting (Meyers单例, 环境变量依赖)
- `ubsocket_lock.h/.cpp` — Lock, LockRegistry (pthread_rwlock包装器)
- `ubsocket_logger.h/.cpp` — Logger (日志宏/实现)
- `ubsocket_errno.h/.cpp` — UbsocketErrno (errno定义)
- `ubsocket_leaky_singleton.h` — LeakySingleton (模板, 故意泄漏)
- `ubsocket_thread_pool.h/.cpp` — ThreadPool (线程池, sleep_for)
- `ubsocket_scope_exit.h` — ScopeExit (RAII scope guard, 仅头文件)
- `ubsocket_set.h/.cpp` — UbsocketSet (集合工具)
- `ubsocket_obj_statistics.h/.cpp` — ObjStatistics (对象统计)
- `ubsocket_signal_handler.h/.cpp` — SignalHandler (SIGINT/SIGTERM处理)
- `ubsocket_setting_validator.h/.cpp` — SettingValidator (设置验证)
- `ubsocket_defines.h` — 宏和常量(IO_SIZE_MB等)
- `ubsocket_functions.h/.cpp` — 工具函数
- `ubsocket_ref.h` — 引用计数
- `ubsocket_version.h` — 版本信息
- `ubsocket_common_includes.h` — 公共include聚合器
- `ubsocket_profiling.h` — 性能宏(链接到profiling模块)

## 单例模式与清理

### GlobalSetting (Meyers单例)

`GlobalSetting` 使用Meyers单例模式(`static GlobalSetting& Instance()`)。所有成员变量为public static字段。

**测试设置:**
```cpp
void SetUp() override
{
    GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
    GlobalSetting::UBS_RX_DEPTH = TEST_DEPTH;
    GlobalSetting::UBS_TX_DEPTH = TEST_DEPTH;
    GlobalSetting::UBS_TRACE_ENABLED = false;
    GlobalSetting::UBS_EPOLL_TIMEOUT_MS = 100;
}
```

**环境变量依赖:** 某些设置在首次访问时从环境变量读取。**环境变量必须在首次Instance()调用之前设置:**

```cpp
void SetUp() override
{
    setenv("UBS_ENABLE_SHARE_JFR", "0", 1);  // 在任何GlobalSetting访问之前
    setenv("UBS_RX_DEPTH", "64", 1);
    // ... 然后访问Instance()
}
```

**清理:** Meyers单例永不销毁——无需清理。但**TearDown中必须重置public字段**以避免跨测试污染。

### LeakySingleton (故意泄漏)

`LeakySingleton<T>` 是模板，**故意不销毁**其实例。用于 `EidRegistry` 等。

**测试清理:**
```cpp
void TearDown() override
{
    EidRegistry::UnregisterEid();  // 显式清理方法, 不是析构函数
}
```

**关键:** LeakySingleton在进程退出时不调用析构函数。测试必须调用显式清理/unregister方法。遗漏清理导致跨测试状态污染。

### SocketSet (类单例)

```cpp
void SetUp() override
{
    SocketSet::Instance().Init();
}

void TearDown() override
{
    SocketSet::Instance().ReleaseAll();
}
```

### ProbeManager (单例)

```cpp
void TearDown() override
{
    ProbeManager::GetInstance().Stop();
}
```

## Lock & LockRegistry

使用Lock前必须调用 `LockRegistry::RegisterDefaultOps()`，它注册pthread_rwlock的create/destroy/read_lock/write_lock/unlock操作。

**测试设置:**
```cpp
void SetUp() override
{
    LockRegistry::RegisterDefaultOps();
}
```

**直接测试Lock:** Lock包装 `pthread_rwlock_t`。Lock中大多数错误路径不太可能发生(pthread_rwlock很少失败)。重点测试:
- 读锁 / 写锁 / 解锁序列
- Lock销毁(调用pthread_rwlock_destroy)
- 边界情况: 双重解锁, 销毁后加锁

## ThreadPool

ThreadPool创建工作线程，线程调用 `std::this_thread::sleep_for`。**测试难点:**
- 线程创建是真实的(不易mock `pthread_create`)
- `sleep_for` 使测试变慢
- 任务队列是内部的

**策略:**
- 用短任务测试提交和完成
- 用 `std::atomic<int>` 计数器验证任务执行
- 测试stop/shutdown序列
- 错误路径: 用无效线程数(0, 负数)测试构造

```cpp
ThreadPool pool(2);
std::atomic<int> counter{0};
pool.Submit([&counter]() { counter++; });
std::this_thread::sleep_for(std::chrono::milliseconds(100));
EXPECT_EQ(counter.load(), 1);
pool.Stop();
```

## ScopeExit (仅头文件)

ScopeExit是RAII scope guard——无需mock。直接测试:

```cpp
int value = 0;
{
    auto guard = MakeScopeExit([&value]() { value = 42; });
    // guard在scope退出时触发
}
EXPECT_EQ(value, 42);
```

重点测试:
- 正常scope退出(guard触发)
- 提前释放(scope退出前调用Release())
- 异常路径(guard即使异常也触发)

## UbsocketErrno

定义errno常量及可能的映射函数。纯逻辑——无需mock。

## SettingValidator

验证GlobalSetting值。依赖GlobalSetting单例。通过设置GlobalSetting字段后调用validator来测试:

```cpp
GlobalSetting::UBS_RX_DEPTH = 0;  // 无效
EXPECT_FALSE(SettingValidator::ValidateRxDepth());

GlobalSetting::UBS_RX_DEPTH = 64;  // 有效
EXPECT_TRUE(SettingValidator::ValidateRxDepth());
```

## SignalHandler

注册SIGINT/SIGTERM处理器。**测试难点:**
- 测试中不易发送真实信号
- handler通常设置全局标志并调用清理

**策略:**
- 测试handler注册(mock sigaction)
- 直接调用handler函数测试handler逻辑
- 测试清理序列

## ObjStatistics

跟踪对象创建/销毁计数。可能使用static计数器。直接测试:

```cpp
ObjStatistics::RecordCreate("TestClass");
EXPECT_EQ(ObjStatistics::GetCreateCount("TestClass"), 1);
ObjStatistics::RecordDestroy("TestClass");
EXPECT_EQ(ObjStatistics::GetDestroyCount("TestClass"), 1);
```

## 优先覆盖的新文件

| 文件 | 行数 | 复杂度 | 优先级 |
|------|------|--------|--------|
| `ubsocket_global_setting.cpp` | ~150 | 中 | P1 — 单例 + 环境变量 |
| `ubsocket_thread_pool.cpp` | ~120 | 中 | P2 — 线程管理 |
| `ubsocket_lock.cpp` | ~80 | 低 | P2 — pthread包装器 |
| `ubsocket_setting_validator.cpp` | ~60 | 低 | P2 — 验证逻辑 |
| `ubsocket_signal_handler.cpp` | ~80 | 中 | P3 — 信号处理 |
| `ubsocket_scope_exit.h` | ~30 | 低 | P3 — 仅头文件RAII |
| `ubsocket_obj_statistics.cpp` | ~50 | 低 | P3 — 计数器 |

## 参考文件

| 文件 | 用途 |
|------|------|
| `src/ubsocket/csrc/common/ubsocket_global_setting.h/.cpp` | GlobalSetting源码 |
| `src/ubsocket/csrc/common/ubsocket_leaky_singleton.h` | LeakySingleton模板 |
| `src/ubsocket/csrc/common/ubsocket_lock.h/.cpp` | Lock源码 |
| `src/ubsocket/csrc/common/ubsocket_defines.h` | 宏/常量(IO_SIZE_MB) |