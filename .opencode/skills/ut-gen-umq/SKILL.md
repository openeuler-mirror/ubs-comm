---
name: ut-gen-umq
description: Module-specific skill for writing UT for files under src/ubsocket/csrc/core/umq/. Load together with ut-gen skill when testing UMQ transport adapter code. Trigger on keywords: UMQ, umq, umq_rx_ops, umq_tx_ops, umq_backend, umq_socket, umq_epoll, umq_connector, umq_acceptor, umq_errno_converter, Share-JFR, PrefillRx.
---

# UMQ模块 UT 生成子skill

> **权威数据源**: `doc/ubsocket/UBSOCKET-COVERAGE-ANALYSIS.ch.md` 四.4.1-4.3 — UMQ模块覆盖率基线12.0%(2148行), 需+1460行达80%。
> **协调跟踪**: `.opencode/skills/ut-coverage-coord/SKILL.md` — 文件认领与进度。

## 模块范围

`src/ubsocket/csrc/core/umq/` 下的文件:
- `umq_data_rx_ops.h/.cpp` — UmqRxOps (RX数据通路)
- `umq_data_tx_ops.h/.cpp` — UmqTxOps (TX数据通路)
- `umq_backend.h/.cpp` — UmqBackend (初始化、dev_add、状态管理)
- `umq_socket.h/.cpp` — UmqSocket (interrupt_fd_get, rearm, prefill)
- `umq_share_jfr_epoll_runner_ops.h/.cpp` — UmqShareJfrEpollRunnerOps (epoll集成)
- `umq_socket_connector.h/.cpp` — UmqSocketConnector (连接路径)
- `umq_socket_acceptor.h/.cpp` — UmqSocketAcceptor (接受路径)
- `umq_errno_converter.h/.cpp` — UmqErrnoConverter (errno映射, API冻结)
- `umq_buf_converter.h/.cpp` — UmqBufConverter (buffer转换)
- `umq_qbuf_list.h/.cpp` — UmqQbufList (队列buffer管理)
- `umq_eid_table.h/.cpp` — UmqEidTable (EID注册表)
- `umq_epoll_ops.h/.cpp` — UmqEpollOps (epoll操作)
- `umq_setting.h/.cpp` — UmqSetting (UMQ特定设置)

## 关键类与构造函数依赖

| 类 | 构造函数签名 | 需mock的关键依赖 |
|----|-------------|-----------------|
| UmqRxOps | `(int fd, uint64_t umqHandle)` | 无直接依赖; 成员 `local_umqh_` 从参数设置 |
| UmqTxOps | `(int fd, uint64_t umqHandle)` | 无直接依赖; 成员 `local_umqh_` 从参数设置 |
| UmqBackend | 默认ctor; `Init()` 执行实际工作 | `::umq_init`, `::umq_dev_add`, `::umq_state_get`, `::umq_interrupt_fd_get`, `::umq_rearm_interrupt`, `::umq_create`, `::umq_bind_info_get` |
| UmqSocket | 从SocketBase的复杂ctor | `::umq_interrupt_fd_get`, `::umq_rearm_interrupt`, LockRegistry, SocketSet |
| UmqSocketConnector | 继承UmqSocket | `::umq_bind` (CONNECT op), OsAPiMgr系统调用 |
| UmqSocketAcceptor | 继承UmqSocket | `::umq_dev_add` (ACCEPT op), `::umq_bind` (ACCEPT op) |
| UmqShareJfrEpollRunnerOps | 依赖epoll基础设施 | OsAPiMgr::epoll_create, epoll_ctl, epoll_wait |
| UmqErrnoConverter | 静态类(无ctor) | 无 — 纯逻辑，无需mock |

## UT约束

- **stub默认不启用**: 新增UT时优先使用mockcpp(`MOCKER_CPP`/`MOCKER`)mock C API和系统调用，**默认不使用stub方式**(fake_epoll_static/AllocMockBufWithBlock/SocketTestHelper等)。只有用户明确指定需要stub实现时才启用。
- **单用例执行≤1s**: 每个`TEST_F`用例的执行时间不超过1秒。禁止在测试中使用长时间sleep、阻塞等待、密集计算循环等。如需等待异步事件，使用短超时(≤100ms)+轮询。

## UMQ API Mock模式

### Adapter后端(默认, UMQ_ADAPTER_BACKEND_ENABLED)

所有 `::umq_*` 函数为全局C函数。用 `MOCKER_CPP(::func_name)` mock:

```cpp
MOCKER_CPP(::umq_poll).stubs().will(returnValue(-UMQ_ERR_EAGAIN));
MOCKER_CPP(::umq_get_cq_event).stubs().will(returnValue(0));
MOCKER_CPP(::umq_rearm_interrupt).stubs().will(returnValue(-UMQ_ERR_EPERM));
MOCKER_CPP(::umq_init).stubs().will(returnValue(0));
MOCKER_CPP(::umq_dev_add).stubs().will(returnValue(0));
MOCKER_CPP(::umq_state_get).stubs().will(returnValue(QUEUE_STATE_READY));
MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(returnValue(TEST_FD));
MOCKER_CPP(::umq_create).stubs().will(returnValue(TEST_UMQ_HANDLE));
MOCKER_CPP(::umq_bind_info_get).stubs().will(returnValue(0));
errno = 0; // 在调用被测函数之前设置
int ret = rxOps.DpPollRx(buf, maxBufSize);
```

**永远不要使用 `_ptr` 成员赋值** — adapter后端没有 `_ptr` 成员。

### Ops级测试的常用UMQ mock设置

```cpp
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
```

## UMQ模块中的ALWAYS_INLINE函数

`GetAndAckEvent` 是 ALWAYS_INLINE — 无法直接mock。应mock其内部依赖:

```cpp
// GetAndAckEvent内部调用 ::umq_get_cq_event
MOCKER_CPP(::umq_get_cq_event).stubs().will(returnValue(0));
// GetAndAckEvent通过真实inline代码执行，但umq_get_cq_event被mock
```

## Share-JFR 双Handle陷阱 (关键)

`UBS_ENABLE_SHARE_JFR` 默认为 `true`。`true`时 `PrefillRx` 使用主UMQ handle(`share_umq_handle_`); `false`时使用子UMQ handle(`umq_handle_`)。

**等ready逻辑必须始终检查 `umq_handle_`(子UMQ)**，因为子UMQ是新创建的、需IDLE→READY转换。主UMQ早已ready。

**陷阱:** 提取子函数如 `WaitUntilReady` 时，传 `umq_handle_`(成员变量)而非本地 `umq_handle` 变量。本地 `umq_handle` 在share-JFR模式下解析为 `share_umq_handle_` — 检查那个handle会错误地等待已经ready的主UMQ。

**测试设置:**
```cpp
// Share-JFR模式测试:
GlobalSetting::UBS_ENABLE_SHARE_JFR = true;
// 任何涉及PrefillRx或双handle逻辑的重构必须在两种模式下都验证
```

## 各调用点的errno映射

| 方法 | 调用的UMQ API | UmqOperation | Converter API |
|------|---------------|--------------|---------------|
| UmqRxOps::DpPollRx | ::umq_poll | READV | Convert |
| UmqRxOps::DpGetAndAckEventRx | ::umq_get_cq_event | READV | Convert |
| UmqRxOps::DpRearmRxInterrupt | ::umq_rearm_interrupt | READV | Convert |
| UmqRxOps::RefillBuf | ::umq_post | READV | Convert |
| UmqTxOps::DpPollTx | ::umq_poll | WRITEV | Convert |
| UmqTxOps::DpGetAndAckEventTx | ::umq_get_cq_event | WRITEV | Convert |
| UmqTxOps::DpRearmTxInterrupt | ::umq_rearm_interrupt (失败路径) | WRITEV | Convert |
| UmqTxOps::DpRearmTxInterrupt (成功) | ::umq_rearm_interrupt ret==0 | WRITEV | errno=EAGAIN, return -1 (不走Convert) |
| UmqBackend::Init/AddUbDev | ::umq_init, ::umq_dev_add, ::umq_state_get, ::umq_interrupt_fd_get, ::umq_rearm_interrupt | CONNECT | Convert/ConvertHandleResult |
| UmqBackend::GetState | ::umq_state_get | GET_STATE | Convert (特殊路径: ERR/MAX→EIO, else→0) |
| UmqSocket::PrefillRx | ::umq_post (prefill) | CONNECT | Convert |
| UmqSocketConnector::Connect | ::umq_bind | CONNECT | Convert |
| UmqSocketAcceptor::Accept | ::umq_dev_add, ::umq_bind | ACCEPT | Convert |
| UmqSocket::InterruptFdGet (建立阶段) | ::umq_interrupt_fd_get | CONNECT | Convert |

**特殊情况 — DpRearmTxInterrupt:**
- `ret == 0`(成功): 设 `errno = EAGAIN`, 返回 `-1`。**不调用Convert。**
- `ret != 0`(失败): 调用 `Convert(WRITEV, ret, savedErrno)`。

## UmqErrnoConverter 测试 (API冻结)

`umq_errno_converter.h` API为冻结/final — **永远不要提议修改它**。

### op标注规则 (UmqOperation赋值)

当同一UMQ API被多个流程上下文调用(如 `umq_bind` 在connector和acceptor中都被调用)时，`UmqOperation` 枚举值须谨慎选择:
- **共享代码**(无法区分实际流程): 取枚举最小值 → 避免歧义 (如 `umq_post` 在PrefillRx + RefillRx + PostSend中都使用READV)
- **独立调用点**(可明确归属流程): 取实际流程op → 精确映射 (如acceptor的 `umq_bind` 使用ACCEPT)

### Converter-only测试(无需mockcpp)
```cmake
target_link_libraries(<test_name> PRIVATE GTest::gtest_main)
target_sources(<test_name> PRIVATE <test_name>.cpp ${UBSOCKET_BASE_DIR}/csrc/core/umq/umq_errno_converter.cpp)
```

### Ops级测试(需要mockcpp)
```cmake
target_link_libraries(<test_name> PRIVATE ubsocket_static boundscheck mockcpp GTest::gtest_main pthread)
target_sources(<test_name> PRIVATE <test_name>.cpp)
```

## 已有测试覆盖

| Binary | 用例数 | 类型 | 覆盖要点 |
|--------|--------|------|----------|
| `umq_errno_converter_test` | 91 | converter级 | 全映射逻辑, override语义, ConvertBufStatus, ConvertHandleResult, GET_STATE |
| `umq_ops_errno_test` | 51 | ops级 | RX/TX poll/rearm/cqe, handleError, AddUbDev, FindDevName, GetTxFd, GetDevEid, CheckDevAdd |

**13/36调用点已有ops级覆盖。23个因SocketPtr/epoll/connector/acceptor复杂依赖暂未覆盖。**

## 优先覆盖的新文件

| 文件 | 行数 | 复杂度 | 未覆盖调用点 | 优先级 |
|------|------|--------|-------------|--------|
| `umq_socket.cpp` | ~450 | 高 | PrefillRx, InterruptFdGet, RearmRxInterrupt (非errno) | P1 |
| `umq_backend.cpp` | ~350 | 中 | Init错误路径, AddUbDev -EEXIST跳过 | P2 |
| `umq_socket_connector.cpp` | ~300 | 高 | Connect状态机, bind错误路径 | P2 |
| `umq_socket_acceptor.cpp` | ~250 | 高 | Accept多路径, dev_add -EEXIST | P2 |
| `umq_epoll_runner_ops.cpp` | ~200 | 中 | Epoll wait/ctl错误路径 | P3 |
| `umq_buf_converter.cpp` | ~80 | 低 | buffer转换逻辑 | P3 |

## 如何使用本Skill

### 触发与加载

触发关键词见本skill YAML frontmatter `description`字段。加载规则见`.opencode/README.md` §全局规则——写UMQ模块UT时需与`ut-gen`(root skill)一起加载。

### 工作流程

1. **认领文件** — 从§优先覆盖的新文件中选择目标文件
2. **读源码** — 对照§各调用点的errno映射表识别Convert API和UmqOperation
3. **读已有测试** — 参考§已有测试覆盖的fixture模式
4. **设计case** — 按ut-gen §Errno映射测试覆盖矩阵+本skill §Share-JFR双Handle陷阱设计
5. **编写测试** — 使用§UMQ API Mock模式(`MOCKER_CPP(::umq_xxx)`)
6. **构建验证** — 命令见ut-gen §构建与运行
7. **报告进度** — 更新ut-coverage-coord进度表+覆盖率增量

## 知识回流

按`.opencode/README.md` §如何更新Skill回流。UMQ模块特定判断:

| 发现类型 | 判断条件 | 回流目标 |
|----------|---------|----------|
| UMQ API mock模式 | 适配`::umq_*`函数 | 本skill §UMQ API Mock模式 |
| Share-JFR/双handle相关 | 涉及handle变量语义或主/子UMQ切换 | 本skill §Share-JFR双Handle陷阱 |
| 其他UMQ errno路径陷阱 | 不涉及Share-JFR | 本skill新增§常见陷阱条目(格式参考ut-gen §常见陷阱#12) |
| 新errno映射调用点 | 新增调用点未被§各调用点的errno映射表覆盖 | 本skill §各调用点的errno映射表 |
| 跨模块适用 | 不限于umq模块 | `ut-gen` §mockcpp模式/§常见陷阱 |

### 回流更新检查清单

- [ ] 新UMQ API mock模式 → 本skill §UMQ API Mock模式
- [ ] Share-JFR/双handle陷阱 → 本skill §Share-JFR双Handle陷阱
- [ ] 其他UMQ errno路径陷阱 → 本skill新增§常见陷阱条目
- [ ] 新errno映射调用点 → 本skill §各调用点的errno映射表

## 参考文件

| 文件 | 用途 |
|------|------|
| `src/ubsocket/unit_test/umq_errno_converter_test.cpp` | Converter-only测试模式(91 cases) |
| `src/ubsocket/unit_test/umq_ops_errno_test.cpp` | Ops级mockcpp测试模式(51 cases, 3 fixtures) |
| `src/ubsocket/csrc/core/umq/umq_errno_converter.h` | 冻结 — converter API |
| `src/ubsocket/csrc/core/umq/umq_data_rx_ops.h/.cpp` | RX ops源码 |
| `src/ubsocket/csrc/core/umq/umq_data_tx_ops.h/.cpp` | TX ops源码 |
| `src/ubsocket/csrc/core/umq/umq_backend.h/.cpp` | Backend源码 |
| `src/ubsocket/csrc/core/umq/umq_socket.h/.cpp` | Socket源码 |
| `doc/ubsocket/UBSOCKET-CLAIMING.md` | Sprint唯一入口 — 认领表+规则+mock基础设施 |