---
name: ut-gen-core
description: Module-specific skill for writing UT for files under src/ubsocket/csrc/core/ (excluding umq/ subdirectory). Load together with ut-gen skill when testing epoll, socket, connector, acceptor, data ops, ring buffer code. Trigger on keywords: epoll, EventEpoll, SocketConnector, SocketAcceptor, SocketSet, SocketFd, SPSCRingQueue, RingBuffer, DataTx, DataRx, BufConverter.
---

# Core模块 UT 生成子skill

> **权威数据源**: `doc/ubsocket/UBSOCKET-COVERAGE-ANALYSIS.ch.md` 四.4.1-4.3 — Core/socket模块覆盖率基线2.4%(1452行), 需+1126行达80%。
> **协调跟踪**: `.opencode/skills/ut-coverage-coord/SKILL.md` — 文件认领与进度。

## 模块范围

`src/ubsocket/csrc/core/` 下的文件(不含 `umq/`):
- `ubsocket_event_epoll.h/.cpp` — EventEpoll (epoll包装器)
- `ubsocket_socket.h/.cpp` — Socket / SocketFd (基础socket类)
- `ubsocket_socket_set.h/.cpp` — SocketSet (socket注册表, 类单例)
- `ubsocket_socket_connector.h/.cpp` — SocketConnector (TCP连接器)
- `ubsocket_socket_acceptor.h/.cpp` — SocketAcceptor (TCP接受器)
- `ubsocket_socket_helper.h/.cpp` — SocketHelper (工具函数)
- `ubsocket_data_tx.h/.cpp` — DataTx (TCP TX数据通路)
- `ubsocket_data_rx.h/.cpp` — DataRx (TCP RX数据通路)
- `ubsocket_buf_converter.h/.cpp` — BufConverter (buffer转换)
- `ubsocket_spsc_ring_queue.h/.cpp` — SPSCRingQueue (单生产者单消费者环形队列)
- `ubsocket_ring_buffer.h/.cpp` — RingBuffer (环形buffer实现)
- `ubsocket_qbuf_queue.h/.cpp` — QbufQueue (队列buffer队列)
- `ubsocket_core_types.h` — 类型定义
- `ubsocket_wakeup_event.h/.cpp` — WakeupEvent (eventfd唤醒机制)

## 关键类与构造函数依赖

| 类 | 构造函数依赖 | 默认mock策略(mockcpp) | stub可选(fake_epoll_static) |
|----|-------------|------------------------|----------------------------|
| EpollRunner | `epoll_create1`, `eventfd`, `close` | `MOCKER_CPP(::epoll_create1)`等 | 链接`fake_epoll_static` |
| AsyncEventPoll | `epoll_ctl`, `close` | `MOCKER_CPP(::epoll_ctl)`等 | 链接`fake_epoll_static` |
| WakeupEvent | `eventfd`, `epoll_ctl`, `close`, `eventfd_write` | `MOCKER_CPP(::eventfd)`等 | 链接`fake_epoll_static` |
| SocketSet | 单例模式, `Init()` / `ReleaseAll()` | SetUp/TearDown中调用Init/ReleaseAll | — |
| SPSCRingQueue | 模板类, 无系统调用 | 直接测试，无需mock | — |
| RingBuffer | 依赖内存分配 | 直接测试，必要时mock分配 | — |

**重要**: 默认使用mockcpp mock系统调用。Core模块的epoll/eventfd/close调用是直接C全局函数调用，可通过`MOCKER_CPP(::epoll_create1)`等mock。`LibcApi::*`路径(如socket/bind/connect等)通过mockcpp mock函数指针或lambda赋值。

## UT约束

- **stub默认不启用**: 新增UT时优先使用mockcpp(`MOCKER_CPP`/`MOCKER`)mock C API和系统调用，**默认不使用stub方式**(fake_epoll_static/AllocMockBufWithBlock/SocketTestHelper等)。只有用户明确指定需要stub实现时才启用。
- **单用例执行≤1s**: 每个`TEST_F`用例的执行时间不超过1秒。禁止在测试中使用长时间sleep、阻塞等待、密集计算循环等。如需等待异步事件，使用短超时(≤100ms)+轮询。

## mockcpp Mock策略 (core模块默认)

Core模块的系统调用(epoll/eventfd/close)可通过`MOCKER_CPP`直接mock:

```cpp
// 默认方式 — mockcpp mock系统调用
MOCKER_CPP(::epoll_create1).stubs().will(returnValue(TEST_EPOLL_FD));
MOCKER_CPP(::eventfd).stubs().will(returnValue(TEST_EVENT_FD));
MOCKER_CPP(::epoll_ctl).stubs().will(returnValue(0));
MOCKER_CPP(::close).stubs().will(returnValue(0));
MOCKER_CPP(::eventfd_write).stubs().will(returnValue(0));
```

**Fixture模板(默认):**

```cpp
class CoreFeatureTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
    }

    void TearDown() override
    {
        errno = 0;
        GlobalMockObject::verify();
    }
};
```

**WakeupEvent示例(mockcpp方式):**

```cpp
TEST_F(CoreFeatureTest, WakeupEventInitializeSuccess)
{
    MOCKER_CPP(::eventfd).stubs().will(returnValue(TEST_EVENT_FD));
    MOCKER_CPP(::epoll_ctl).stubs().will(returnValue(0));

    UbsocketWakeupEvent wakeup;
    EXPECT_EQ(wakeup.Initialize(TEST_EPOLL_FD), 0);
    wakeup.CleanUp();
    GlobalMockObject::verify();
}
```

## fake_epoll_static Mock策略 (stub可选方式 — 用户明确指定时启用)

链接`fake_epoll_static`后，所有直接调用的`epoll_create1()/epoll_ctl()/epoll_wait()/eventfd()/eventfd_write()/close()`自动被fake实现拦截。

**测试控制API** (`ock::ubs::test::FakeEpollCtl`):
- `Reset()` — 每个SetUp必须调用，清空所有fake状态
- `SetNextEpollCreateReturn(fd)` — 让`epoll_create1()`返回指定fd
- `SetNextEpollWaitEvents(events)` — 让`epoll_wait()`返回指定事件列表
- `SetNextEventfdReturn(fd)` — 让`eventfd()`返回指定fd
- `IsFakeFd(fd)` — 判断fd是否是fake分配的
- `AllocFakeFd()` / `ReleaseFakeFd(fd)` — 手动分配/释放fake fd

**Fixture模板(stub方式):**

```cpp
class CoreStubTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        FakeEpollCtl::Reset();
        LockRegistry::RegisterDefaultOps();
    }

    void TearDown() override
    {
        errno = 0;
        FakeEpollCtl::Reset();
    }
};
```

**WakeupEvent示例(stub方式):**

```cpp
TEST_F(CoreStubTest, WakeupEventInitializeSuccess)
{
    FakeEpollCtl::SetNextEpollCreateReturn(TEST_EPOLL_FD);
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_EQ(epfd, TEST_EPOLL_FD);

    UbsocketWakeupEvent wakeup;
    EXPECT_EQ(wakeup.Initialize(epfd), 0);
    wakeup.CleanUp();
    EXPECT_EQ(close(epfd), 0);
}
```

### LibcApi路径 mock (socket/bind/connect等)

`LibcApi`是static类，所有方法委托给static函数指针(`_ptr`成员)。可通过自定义函数赋值或mockcpp mock:

```cpp
// 自定义函数方式(最简单, 兼variadic)
static int MockOpenFail(const char *file, int oflag, ...) { return -1; }
LibcApi::open_ptr = MockOpenFail;
LibcApi::close_ptr = [](int fd) -> int { return 0; };

// mockcpp方式(MOCKER_CPP) — 仅非variadic可用
MOCKER_CPP(LibcApi::close_ptr).stubs().will(returnValue(0));
```

**关键**: `LibcApi::open`是variadic(`int open(const char*, int, ...)`), mockcpp无法mock variadic函数。必须使用自定义函数赋值方式。`LibcApi::close/read`等非variadic方法两种方式均可。

**注意**: `LibcApi::Load()`通过`dlopen("libc.so.6")`+`dlsym`加载真实函数指针。链接`fake_epoll_static`后，`dlsym`仍解析到libc真实实现(因为`dlsym`查询dynamic linker，不查static link table)。因此**两种机制互补**: 链接时fake拦截直接调用，`LibcApi::_ptr` override拦截dlopen路径。

**`_ptr`初始为nullptr** — `DL_API_DEFINE(LibcApi, open)`展开为`open_api LibcApi::open_ptr = nullptr;`。若未调用`LibcApi::Load()`，所有`_ptr`为nullptr。通过nullptr函数指针调用→segfault。测试中若需调用任何`LibcApi::xxx()`路径，必须在SetUp中设置`_ptr`为mock函数或调用`LibcApi::Load()`。TearDown中恢复为nullptr。

## SocketConnector Connect状态机

Connector有多步Connect流程与状态转换:
- Connect → PollConnectResult → OnConnectSuccess / OnConnectFail

**测试部分状态:** 通过 `-fno-access-control` 直接设置成员变量:

```cpp
connector.m_connect_state = CONNECT_STATE_POLLING;
// 或用Testable继承暴露protected成员
```

Connector依赖`LibcApi::socket/bind/connect/setsockopt/close`等，通过函数指针mock(lambda赋值或mockcpp)。

## SocketAcceptor 多路径Accept

Acceptor有:
1. UDS(Unix Domain Socket) accept路径
2. TCP accept路径
3. UBS(UB Socket) accept路径

**分别测试每个路径** — 通过mock不同的accept返回值和设置合适的成员状态。

Acceptor依赖`LibcApi::socket/bind/listen/accept/close`等，通过函数指针mock。

## DataTx/DataRx — 函数指针限制

`LibcApi::send` 和 `LibcApi::recv` 通过函数指针调用(`_ptr`成员)。可通过lambda赋值`LibcApi::send_ptr = my_fake_send`，或mockcpp `MOCKER_CPP(LibcApi::send_ptr)`。

**策略:**
- mock更低层的write/read来测试TCP数据路径
- 或通过参数验证(nullptr buffer, closed状态)测试错误路径
- 实际send/receive成功路径需要集成测试

## SPSCRingQueue / RingBuffer — 直接测试

这些是模板/容器类，无系统调用依赖。直接测试:

```cpp
SPSCRingQueue<int> queue(16);
EXPECT_TRUE(queue.Push(42));
int val;
EXPECT_TRUE(queue.Pop(val));
EXPECT_EQ(val, 42);
```

无需mock。重点测试:
- 满/空边界条件
- 并发push/pop(多线程测试如需)
- Resize/realloc路径

## 优先覆盖的新文件

| 文件 | 行数 | 复杂度 | 优先级 |
|------|------|--------|--------|
| `ubsocket_event_epoll.cpp` | ~200 | 中 | P1 — 多错误路径 |
| `ubsocket_socket_connector.cpp` | ~300 | 高 | P1 — Connect状态机 |
| `ubsocket_socket_acceptor.cpp` | ~250 | 高 | P1 — 多路径accept |
| `ubsocket_socket.cpp` | ~200 | 中 | P2 — 基础socket操作 |
| `ubsocket_socket_set.cpp` | ~150 | 低 | P2 — 注册表管理 |
| `ubsocket_wakeup_event.cpp` | ~100 | 低 | P2 — eventfd包装器 |
| `ubsocket_spsc_ring_queue.cpp` | ~80 | 低 | P3 — 直接测试 |
| `ubsocket_ring_buffer.cpp` | ~80 | 低 | P3 — 直接测试 |

## Core模块 Test Fixture 模板

```cpp
class CoreFeatureTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        FakeEpollCtl::Reset();
        LockRegistry::RegisterDefaultOps();
        SocketSet::Instance().Init();
    }

    void TearDown() override
    {
        errno = 0;
        FakeEpollCtl::Reset();
        GlobalMockObject::verify();
        SocketSet::Instance().ReleaseAll();
    }
};
```

## 如何使用本Skill

### 触发与加载

触发关键词见本skill YAML frontmatter `description`字段。加载规则见`.opencode/README.md` §全局规则——写core/socket模块UT时需与`ut-gen`(root skill)一起加载。

### 工作流程

1. **认领文件** — 从§优先覆盖的新文件中选择目标文件
2. **读源码** — 对照§关键类与构造函数依赖表确定mock策略
3. **选择mock方式** — 默认mockcpp(§mockcpp Mock策略); 用户明确要求时才用fake_epoll_static(§fake_epoll_static Mock策略)
4. **设计case** — 按ut-gen §深度分析方法(4步)设计
5. **编写测试** — 使用本skill的fixture模板(§mockcpp Mock策略或§fake_epoll_static Mock策略)
6. **构建验证** — 命令见ut-gen §构建与运行
7. **报告进度** — 更新ut-coverage-coord进度表+覆盖率增量

## 知识回流

按`.opencode/README.md` §如何更新Skill回流。Core模块特定判断:

| 发现类型 | 判断条件 | 回流目标 |
|----------|---------|----------|
| epoll/eventfd/close mock模式 | 涉及mockcpp直接mock系统调用 | 本skill §mockcpp Mock策略 |
| LibcApi函数指针mock | 涉及`_ptr`赋值或variadic函数 | 本skill §LibcApi路径mock |
| Connector/Acceptor状态机陷阱 | 涉及Connect/Accept流程转换 | 本skill §SocketConnector/§SocketAcceptor |
| fake_epoll_static模式 | 涉及stub链接时替换 | 本skill §fake_epoll_static Mock策略 |
| 跨模块适用 | 不限于core模块 | `ut-gen` §mockcpp模式/§常见陷阱 |

### 回流更新检查清单

- [ ] 新epoll/eventfd mock模式 → 本skill §mockcpp Mock策略
- [ ] 新LibcApi函数指针mock → 本skill §LibcApi路径mock
- [ ] 新Connector/Acceptor状态机陷阱 → 本skill对应章节
- [ ] 新fake_epoll_static模式 → 本skill §fake_epoll_static Mock策略

## 参考文件

| 文件 | 用途 |
|------|------|
| `src/ubsocket/unit_test/mock_infrastructure_test.cpp` | mock基础设施验证参考(Helper+Static两种方式) |
| `src/ubsocket/unit_test/stub/fake_epoll/fake_epoll.h` | FakeEpollCtl API定义 |
| `src/ubsocket/unit_test/stub/fake_epoll/fake_epoll.cpp` | fake epoll实现 |
| `src/ubsocket/csrc/core/ubsocket_event_epoll.h/.cpp` | EventEpoll源码(直接调用epoll_*/eventfd/close) |
| `src/ubsocket/csrc/core/ubsocket_wakeup_event.h/.cpp` | WakeupEvent源码(最简单的fake_epoll_static验证对象) |
| `src/ubsocket/csrc/core/ubsocket_socket.h/.cpp` | Socket基类源码 |
| `src/ubsocket/csrc/core/ubsocket_socket_connector.h/.cpp` | Connector源码 |
| `src/ubsocket/csrc/core/ubsocket_socket_acceptor.h/.cpp` | Acceptor源码 |
| `src/ubsocket/csrc/under_api/dl_libc_api.h` | LibcApi类(static函数指针wrapper) |