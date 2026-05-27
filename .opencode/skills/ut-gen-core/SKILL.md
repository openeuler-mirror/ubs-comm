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

| 类 | 构造函数依赖 | mock策略 |
|----|-------------|----------|
| EventEpoll | `OsAPiMgr::epoll_create`, `OsAPiMgr::close`(析构) | 构造前mock epoll_create + close |
| SocketFd | `OsAPiMgr::socket`, `OsAPiMgr::bind`, `OsAPiMgr::listen`, `OsAPiMgr::close`(析构), `OsAPiMgr::setsockopt` | 构造前mock所有系统调用 |
| SocketConnector | 继承SocketFd + `OsAPiMgr::connect`, `OsAPiMgr::getsockopt` | mock connect + 所有SocketFd系统调用 |
| SocketAcceptor | 继承SocketFd + `OsAPiMgr::accept` | mock accept + 所有SocketFd系统调用 |
| SocketSet | 单例模式, `Init()` / `ReleaseAll()` | SetUp/TearDown中调用Init/ReleaseAll |
| DataTx | 依赖SocketFd, TCP写路径 | `OsAPiMgr::send` (函数指针 — 无法mock!) |
| DataRx | 依赖SocketFd, TCP读路径 | mock `OsAPiMgr::recv` 或用invoke |
| SPSCRingQueue | 模板类, 无系统调用 | 直接测试，无需mock |
| RingBuffer | 依赖内存分配 | 直接测试，必要时mock分配 |
| WakeupEvent | `OsAPiMgr::eventfd`(C函数), `OsAPiMgr::write`, `OsAPiMgr::read` | C函数用 `MOCKER(eventfd)` |

## OsAPiMgr Mock策略 (core模块关键)

多数core类在构造函数中调用OsAPiMgr静态方法。**必须在对象创建之前mock:**

```cpp
void SetUp() override
{
    errno = 0;
    LockRegistry::RegisterDefaultOps();

    // mock构造函数将调用的所有系统调用
    MOCKER_CPP(&OsAPiMgr::socket).stubs().will(returnValue(TEST_FD));
    MOCKER_CPP(&OsAPiMgr::bind).stubs().will(returnValue(0));
    MOCKER_CPP(&OsAPiMgr::listen).stubs().will(returnValue(0));
    MOCKER_CPP(&OsAPiMgr::setsockopt).stubs().will(returnValue(0));
    MOCKER_CPP(&OsAPiMgr::close).stubs().will(returnValue(0)); // 必须 — 析构函数调用close
}
```

**必须mock `close`** — 析构函数调用 `OsAPiMgr::close(m_fd)` 在mock fd上。缺少close mock导致crash或状态污染。

### SocketFd派生类的构造函数调用顺序

SocketConnector构造函数顺序:
1. `OsAPiMgr::socket()` → 获取fd
2. `OsAPiMgr::setsockopt()` → 设置socket选项(可能多次调用)
3. `OsAPiMgr::connect()` → 建立连接
4. 若connect失败: `OsAPiMgr::close()` → 清理

SocketAcceptor构造函数顺序:
1. `OsAPiMgr::socket()` → 获取fd
2. `OsAPiMgr::setsockopt()` → 设置选项
3. `OsAPiMgr::bind()` → 绑定地址
4. `OsAPiMgr::listen()` → 开始监听
5. `OsAPiMgr::accept()` → 接受连接(在Process循环中)

### .will().then() 处理多次调用场景

```cpp
// 构造函数中setsockopt被调用两次
MOCKER_CPP(&OsAPiMgr::setsockopt).stubs()
    .will(returnValue(0))      // 第1次调用成功
    .then(returnValue(-1));    // 第2次调用失败
```

## EventEpoll 特定模式

### epoll生命周期mock

```cpp
MOCKER_CPP(&OsAPiMgr::epoll_create).stubs().will(returnValue(TEST_EPOLL_FD));
MOCKER_CPP(&OsAPiMgr::epoll_ctl).stubs().will(returnValue(0));
MOCKER_CPP(&OsAPiMgr::epoll_wait).stubs().will(returnValue(0));
MOCKER_CPP(&OsAPiMgr::close).stubs().will(returnValue(0));

EventEpoll epoll;  // 构造函数调用epoll_create
```

### epoll_wait返回事件

用 `invoke` + static函数填充epoll_event数组:

```cpp
static int MockEpollWaitReturnEvents(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    if (maxevents > 0 && events != nullptr) {
        events[0].events = EPOLLIN;
        events[0].data.fd = TEST_FD;
    }
    return 1;  // 1个事件就绪
}

MOCKER_CPP(&OsAPiMgr::epoll_wait).stubs().will(invoke(MockEpollWaitReturnEvents));
```

## WakeupEvent 特定模式

WakeupEvent使用 `eventfd`(C全局函数，非OsAPiMgr方法):

```cpp
MOCKER(eventfd).stubs().will(returnValue(TEST_EVENTFD));
MOCKER_CPP(&OsAPiMgr::write).stubs().will(returnValue(8));
MOCKER_CPP(&OsAPiMgr::read).stubs().will(returnValue(8));
MOCKER_CPP(&OsAPiMgr::close).stubs().will(returnValue(0));
```

## SocketConnector Connect状态机

Connector有多步Connect流程与状态转换:
- Connect → PollConnectResult → OnConnectSuccess / OnConnectFail

**测试部分状态:** 通过 `-fno-access-control` 直接设置成员变量:

```cpp
connector.m_connect_state = CONNECT_STATE_POLLING;
// 或用Testable继承暴露protected成员
```

## SocketAcceptor 多路径Accept

Acceptor有:
1. UDS(Unix Domain Socket) accept路径
2. TCP accept路径
3. UBS(UB Socket) accept路径

**分别测试每个路径** — 通过mock不同的accept返回值和设置合适的成员状态。

## DataTx/DataRx — 函数指针限制

`OsAPiMgr::send` 和 `OsAPiMgr::recv` 通过内部函数指针调用实现。**mockcpp无法mock这些**，因为函数指针在ALWAYS_INLINE `SendSocketData`/`RecvSocketData` 内部运行时解析。

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
        LockRegistry::RegisterDefaultOps();
        SocketSet::Instance().Init();

        // mock构造函数需要的系统调用
        MOCKER_CPP(&OsAPiMgr::socket).stubs().will(returnValue(TEST_FD));
        MOCKER_CPP(&OsAPiMgr::close).stubs().will(returnValue(0));
    }

    void TearDown() override
    {
        errno = 0;
        GlobalMockObject::verify();
        SocketSet::Instance().ReleaseAll();
    }
};
```

## 参考文件

| 文件 | 用途 |
|------|------|
| `src/ubsocket/unit_test/umq_ops_errno_test.cpp` | OsAPiMgr mock模式参考 |
| `src/ubsocket/csrc/core/ubsocket_event_epoll.h/.cpp` | EventEpoll源码 |
| `src/ubsocket/csrc/core/ubsocket_socket.h/.cpp` | Socket基类源码 |
| `src/ubsocket/csrc/core/ubsocket_socket_connector.h/.cpp` | Connector源码 |
| `src/ubsocket/csrc/core/ubsocket_socket_acceptor.h/.cpp` | Acceptor源码 |