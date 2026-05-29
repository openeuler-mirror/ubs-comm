# UBSocket 内部架构与调用链

## 一句话定位

UBSocket 是**用户态无感加速库**：北向提供与 POSIX socket/epoll 完全一致的 C API (`ubsocket_<func>`)，南向通过 UMQ(RDMA-like) 传输数据。应用只需将 `socket()` → `ubsocket_socket()` 即可完成 TCP→UB 的劫持替换。

---

## 层次架构总图

```
┌─────────────────────────────────────────────────────────────────────┐
│                        用户应用 (bRPC等)                              │
│   socket() / connect() / writev() / readv() / epoll_wait() ...     │
└────────────────────────────┬────────────────────────────────────────┘
                             │ UB_API_WRAP() 劫持
                             ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Public API Layer  (include/ubsocket*.h)  — 纯C, extern "C"        │
│  ubsocket_init → ubsocket_socket → ubsocket_connect → ...          │
└────────────────────────────┬────────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Entry Layer  (csrc/ubsocket_sock.cpp, ubsocket_epoll.cpp等)       │
│  Guard: UBS_NATIVE_TCP_MODE → LibcApi::xxx() 直通                   │
│  Guard: domain ≠ AF_SMC → LibcApi::socket() 普通TCP                 │
│  否则 → SocketSet::GetSocket(fd) → SocketBase::XXX()               │
└────────────────────────────┬────────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Core Layer  (csrc/core/)                                           │
│                                                                     │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────┐  ┌───────────┐  │
│  │ SocketBase   │  │ DataTx/DataRx│  │ Connector │  │ Acceptor  │  │
│  │  (生命周期)   │  │  (数据I/O)    │  │ (客户端)   │  │ (服务端)   │  │
│  └──────────────┘  └──────────────┘  └──────────┘  └───────────┘  │
│        ▲                  ▲                 ▲              ▲         │
│        │                  │                 │              │         │
│  ┌─────┴──────────────────┴─────────────────┴──────────────┴─────┐ │
│  │              Strategy Ops接口 (可插拔)                          │ │
│  │  DataTxOps / DataRxOps / ConnectorOps / AcceptorOps          │ │
│  └──────────────────────────┬────────────────────────────────────┘ │
│                             │                                       │
│  ┌──────────────────────────┴──────────────────────────────────┐   │
│  │                UmqSocket (UMQ具体实现)                        │   │
│  │  umq_handle_ (子UMQ) / share_umq_handle_ (主UMQ)            │   │
│  │  UmqTxOps / UmqRxOps / UmqConnectorOps / UmqAcceptorOps    │   │
│  └──────────────────────────┬──────────────────────────────────┘   │
└─────────────────────────────┼──────────────────────────────────────┘
                              │ UmqApi::umq_xxx()
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Under API Layer  (csrc/under_api/)                                 │
│  UmqApi — dlopen("libumq.so") 或 直接链接                          │
│  LibcApi — dlopen("libc.so.6") 原生POSIX调用                       │
│  DlApi — 加载协调器                                                 │
└────────────────────────────┬────────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Transport  (libumq.so → UB内核驱动 → RDMA-like硬件)               │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Socket 对象层次

```
Socket (抽象基类)
  ├─ raw_socket_ : int         // POSIX fd
  ├─ event_fd_ : int           // eventfd 通知通道
  ├─ state_ : SocketState      // 状态机
  ├─ type_ : SocketType        // SOCK_TYPE_TCP / SOCK_TYPE_UMQ / SOCK_TYPE_SHM
  ├─ intrusive ref-counting    // DECLARE_REF_COUNT_VARIABLE
  │
SocketBase : Socket
  ├─ tx_ : DataTx              // 发送路径
  ├─ rx_ : DataRx              // 接收路径
  ├─ acceptor_ : Acceptor*     // 服务端逻辑
  ├─ connector_ : Connector*   // 客户端逻辑
  ├─ Create(fd, type, sockPtr) : static  // 工厂方法
  ├─ GenerateSocketCommOps() : static     // 连接成功后创建TX/RX ops
  │
UmqSocket : SocketBase
  ├─ umq_handle_ : uint64_t      // 子UMQ (实际数据队列)
  ├─ share_umq_handle_ : uint64_t  // 主UMQ (share-JFR共享接收)
  ├─ trans_mode_ : ub_trans_mode  // RC_TP/RM_TP/RM_CTP/RC_CTP
  ├─ rxQueue : QbufQueue<umq_buf_t*>*  // share-JFR RX缓冲队列
  ├─ CreateLocalUmq()            // 创建UMQ队列
  ├─ PrefillRx()                 // 预投递RX缓冲
  ├─ UnbindAndFlushRemoteUmq()   // 断连清理
```

---

## Socket 状态机

```
  INIT ──► RAW_ESTABLISHED ──► ESTABLISHED ──► SHUTDOWN ──► CLOSE
   │            │ (TCP已连)         │ (UMQ已绑+已填充)     │            │
   │            │                   │                       │            │
  socket()    connect()/accept()  CreateLocalUmq+bind     shutdown()   close()
```

- **RAW_ESTABLISHED**: TCP连接完成但UMQ尚未建立 → 数据走TCP fallback
- **ESTABLISHED**: UMQ通道就绪 → 数据走UMQ RDMA通道

---

## 连接建立流程 (两端对称)

### 客户端 connect

```
ubsocket_connect(fd, addr)
  │
  ├─ Connector::Connect(sock)
  │   ├─ Step1: PrepareConnect ── TCP connect (可能TFO快速握手)
  │   ├─ Step2: Negotiate ────── TCP上交换 EID/trans_mode/route
  │   │   ├─ 发送 NegotiateReq (本端支持的trans_mode)
  │   │   ├─ 接收 NegotiateRsp (对端选择的trans_mode)
  │   │   ├─ DoRoute() → umq_get_route_list → 选最优路径
  │   │   ├─ 发送 NegotiateRoute (选定路径告知对端)
  │   │
  │   ├─ Step3: CreateSocketResources ── 创建UMQ通道
  │   │   ├─ CreateLocalUmq() → umq_create (子UMQ + 主UMQ)
  │   │   ├─ GenerateSocketCommOps() → 创建 UmqTxOps + UmqRxOps
  │   │   ├─ TCP上 send/recv bind_info (CpMsg)
  │   │   ├─ umq_bind(handle, remote_info) → 远端绑定
  │   │   ├─ PrefillRx() → umq_buf_alloc + umq_post → 投递RX缓冲
  │   │   └─ WaitUntilReady() → umq_state_get 直到 READY
  │
  └─ state = ESTABLISHED
```

### 服务端 accept

```
ubsocket_accept(listen_fd)
  │
  ├─ Acceptor::Accept(sock)
  │   ├─ LibcApi::accept() → 拿到TCP fd
  │   ├─ ValidateProtocol → TCP上收 CONTROL_PLANE_PROTOCOL_NEGOTIATION
  │   ├─ Step1: Negotiate ── TCP上回复 EID/trans_mode/route (与connect对称)
  │   ├─ Step2: CreateSocketResources ── 创建UMQ通道 (与connect对称)
  │   │   ├─ CreateLocalUmq + GenerateSocketCommOps + bind + PrefillRx
  │   │
  │   ├─ SocketSet::OverrideSocket(new_fd, new_socket_obj)
  │
  └─ return new_fd  (用户拿到的fd已绑好UMQ)
```

> **核心思路**: TCP仅用于**控制面**(EID交换/协商)，一旦UMQ bind完成，**数据面**完全走UMQ(RDMA)。

---

## 数据发送流程 (WriteV)

```
ubsocket_writev(fd, iov, iovcnt)
  │
  ├─ if UBS_NATIVE_TCP_MODE → LibcApi::writev() 直通
  ├─ if state=RAW_ESTABLISHED → LibcApi::writev() TCP fallback
  ├─ if state=CLOSE → errno=EPIPE, return -1
  │
  ├─ DataTx::WriteV(sock, iov, iovcnt)
  │   ├─ PollTx(sock) ──── 处理TX完成事件
  │   │   ├─ GetAndAckEvent() → umq_get_cq_event + umq_ack_interrupt
  │   │   ├─ DoUmqTxPoll() → umq_poll(UMQ_IO_TX) 清理已完成发送
  │   │   └─ DpRearmTxInterrupt() → umq_rearm_interrupt (失败→Convert)
  │   │
  │   ├─ BuildIovConverter(iov) ─── scatter-gather适配器
  │   │
  │   ├─ Loop: 切分用户数据为UMQ buf块 (批量发送)
  │   │   ├─ AllocTxBuf() → umq_buf_alloc 拿到UMQ发送缓冲
  │   │   ├─ MemCopy() → 将iov数据拷贝进umq_buf_t链
  │   │   ├─ Block::IncRef() → brpc Block引用计数
  │   │   ├─ PostSend() → umq_post(UMQ_IO_TX) 提交发送请求
  │   │   │   ├─ 成功: tx_queue_avail_num_ -= batch
  │   │   │   ├─ 失败: UmqErrnoConverter::Convert(WRITEV)
  │   │   │
  │   └─ return tx_total_len
```

---

## 数据接收流程 (ReadV + EpollRunner)

### EpollRunner 后台线程 (share-JFR模式)

```
EpollRunner 后台线程 (持续运行)
  │
  ├─ epoll_wait(runner_epfd) → 等待 share_jfr_fd 事件
  │
  ├─ ProcessShareJfrEvent()
  │   ├─ ProcessMainUmqRearm() → umq_get_cq_event + umq_rearm_interrupt
  │   ├─ umq_poll(main_umq, UMQ_IO_RX) → 从主UMQ拉取RX数据
  │   ├─ umq_buf_alloc + umq_post → 补充RX缓冲(refill)
  │   ├─ SiftSocketEventsWithUmqBuffers()
  │   │   ├─ 按fd(ctx)将buf分发到各UmqSocket的rxQueue
  │   ├─ NewRxEpollIn() + SetReadableEventFd()
  │   │   ├─ 通知 AsyncEventPoll 可读队列
```

### 用户 epoll_wait + readv

```
ubsocket_epoll_wait(epfd, events, timeout)
  │
  ├─ AsyncEventPoll::EpollWait()
  │   ├─ 如果 readable_sockets_event_queue 有数据 → MultiPop 直接返回
  │   ├─ 否则 → kernel epoll_wait 等内核事件
  │   ├─ ArrangeWakeUpEvents() → 合并内核事件 + 内部可读队列事件

ubsocket_readv(fd, iov, iovcnt)
  │
  ├─ if state=RAW_ESTABLISHED → LibcApi::readv() TCP fallback
  │
  ├─ DataRx::ReadV(sock, iov, iovcnt)
  │   ├─ PollRx(sock) ─── 处理RX完成事件
  │   │   ├─ share-JFR: rxQueue->Dequeue() (从EpollRunner预分发队列取)
  │   │   ├─ 非share-JFR: umq_poll(UMQ_IO_RX) 直接拉取
  │   │   ├─ 处理每个buf: 探测包→free / 错误→HandleError / FC更新 / 正常→block_cache
  │   │
  │   ├─ RxDataSet(iov[0].iov_base, size) ─── 将缓存数据对接brpc Block链
  │   │   ├─ block_cache_.CutAndInsertAfter() → 链入brpc Block
  │   │   ├─ RearmRxInterrupt() → umq_rearm_interrupt
  │   │   ├─ 无数据 → errno=EAGAIN (brpc会重试)
  │   │   ├─ 有数据 → 返回rx_total_len
  │   │
  │   └─ return rx_total_len
```

---

## Epoll 双层架构

```
┌─ 用户层 ──────────────────────────────────────────────────┐
│                                                            │
│   AsyncEventPoll (用户epoll_fd)                            │
│   ├─ 内核epoll: raw_socket_fd + event_fd                  │
│   ├─ SPSCRingQueue<epoll_event> ← EpollRunner推送可读通知  │
│   └─ EpollWait = max(内核事件, 内部队列事件)              │
│                                                            │
└─────────────────────────┬──────────────────────────────────┘
                          │ 可读通知 (eventfd写触发)
                          ▼
┌─ 内部层 ──────────────────────────────────────────────────┐
│                                                            │
│   EpollRunner<SOCK_TYPE_UMQ> (后台daemon线程)              │
│   ├─ runner_epoll_fd                                      │
│   ├─ 监听: share_jfr_fd + sub_umq_rx_fd + exit_fd        │
│   ├─ ProcessShareJfrEvent:                               │
│   │   ├─ poll主UMQ RX → 分发buf到各socket rxQueue         │
│   │   └─ 通知AsyncEventPoll (SetReadableEventFd)         │
│   └─ ProcessSubUmqRxEvent:                               │
│   │   ├─ poll子UMQ RX → 处理错误/流控                    │
│                                                            │
└────────────────────────────────────────────────────────────┘

   (a) socket_fd    (b) tx_fd    (c) readable_event_fd    (d) share_jfr_fd    (e) sub_umq_rx_fd    (f) exit_fd
       │              │              │                        │                    │                    │
       ├──────────────┼──────────────┤                        │                    │                    │
       │  AsyncEventPoll (用户侧)   │                        │                    │                    │
       └──────────────┴──────────────┤                        │                    │                    │
                                     │  ← eventfd write ←───┤                    │                    │
                                     │                        │                    │                    │
                                     │                        ├────────────────────┼────────────────────┤
                                     │                        │  EpollRunner (内部daemon)             │
                                     │                        └────────────────────┴────────────────────┘
```

---

## 库初始化流程

```
ubsocket_init(u_init_options_t*)
  │
  ├─ GlobalSetting::AddRules() + LoadEnv()     // 环境变量 → 静态配置
  ├─ GlobalSetting::VerifySetting()             // 校验参数范围
  ├─ DlApi::Load(LOAD_LIBC | LOAD_UMQ)         // dlopen libumq.so + libc.so.6
  ├─ LockRegistry::RegisterDefaultOps()         // 注册外部锁实现(brpc butex等)
  ├─ SocketSet::Instance().Init()               // fd→SocketPtr映射表 (8192槽)
  ├─ ArraySet<EventPoll>::GetInstance().Init()  // epoll_fd→EventPoll映射表
  ├─ ZeroCopyPrepare()                          // UmqZeroCopyAllocator + brpc hook
  ├─ umq::UmqBackend::Init()
  │   ├─ UmqSetting::Init()                     // UMQ参数初始化
  │   ├─ umq_init(&umq_config)                  // 初始化UMQ库
  │   ├─ AddUbDev()                              // 发现并添加UB设备
  │   │   ├─ FindDevName() → umq_dev_info_list_get
  │   │   ├─ umq_dev_add → 注册设备
  │   │
  ├─ signal(SIGUSR2, handler)                   // 对象统计dump触发
  ├─ Profiling::Init()                          // 维测初始化 (可选)
  └─ GlobalSetting::UBS_INITED = true
```

---

## 核心设计模式

| 模式 | 位置 | 作用 |
|------|------|------|
| **函数劫持** | `UB_API_WRAP()` | 透明替换POSIX调用 |
| **Strategy(Ops)** | `DataTxOps/DataRxOps/ConnectorOps/AcceptorOps` | 可插拔传输实现 |
| **Factory** | `SocketBase::Create()`, `GenerateSocketCommOps()` | 按SocketType创建正确ops |
| **Template Method** | `Connector::Connect()` / `Acceptor::Accept()` | 固定步骤: Prepare→Negotiate→CreateResources |
| **Singleton** | `SocketSet`, `ArraySet<T>`, `UmqEidTable`, `EpollRunner<T>` | 全局注册表 |
| **Leaky Singleton** | `EidRegistry`, `RouteListRegistry` | 进程生命周期，不析构 |
| **Bridge/Adapter** | `UmqApi` / `LibcApi` / `DlApi` | 抽象dlopen vs 直接链接 |
| **Guard/Bypass** | `UBS_NATIVE_TCP_MODE` 检查 | UB不可用时回退TCP |
| **Ref Counting** | Socket, EventPoll, Ops | 引用计数管理生命周期 |
| **SPSC Ring Queue** | `AsyncEventPoll` | EpollRunner→用户epoll无锁通知 |

---

## UMQ API 使用汇总

| UMQ API | 作用 | 调用方 |
|---------|------|--------|
| `umq_init` | 全局初始化 | `UmqBackend::Init()` |
| `umq_create` | 创建UMQ队列 | `UmqSocket::CreateLocalUmq()` |
| `umq_bind` | 远端绑定 | `ConnectorOps/AcceptorOps` |
| `umq_bind_info_get` | 获取序列化绑定信息 | `ConnectorOps/AcceptorOps` |
| `umq_post` | 提交发送/接收请求 | `UmqTxOps::PostSend`, `PrefillRx` |
| `umq_poll` | 获取完成缓冲 | `UmqTxOps`, `UmqRxOps`, `EpollRunnerOps` |
| `umq_buf_alloc/free` | 缓冲管理 | `UmqTxOps`, `PrefillRx` |
| `umq_rearm_interrupt` | 重触发中断通知 | `UmqTxOps`, `UmqRxOps`, `EpollRunnerOps` |
| `umq_interrupt_fd_get` | 获取中断fd | `UmqSocket::AddTxEvent/GetTxFd/AddRxEventToRunner` |
| `umq_get_cq_event` | 获取CQE | `UmqTxOps/UmqRxOps`, `EpollRunnerOps` |
| `umq_ack_interrupt` | 确认中断 | 同上 |
| `umq_state_get` | 查询队列状态 | `UmqSocket::WaitUntilReady` |
| `umq_dev_add` | 注册UB设备 | `UmqBackend::AddUbDev` |
| `umq_dev_info_list_get` | 发现设备 | `UmqBackend::FindDevName` |
| `umq_get_route_list` | 获取路由拓扑 | `UmqConnectorOps::GetDevRouteList` |

---

## 关键概念

### Share-JFR (共享接收队列)

默认模式 (`UBS_ENABLE_SHARE_JFR=true`)。同一EID下多个子UMQ共享一个主UMQ的接收队列:
- **主UMQ**: EpollRunner后台线程持续poll，拿到RX数据后按fd(ctx)分发到各socket的`rxQueue`
- **子UMQ**: 只负责TX，RX数据从`rxQueue`取
- **优势**: 减少epoll fd数量，EpollRunner只需监听一个share_jfr_fd

### 双层Epoll

- **用户层** (`AsyncEventPoll`): 用户调用`ubsocket_epoll_wait` → 返回合并事件(内核+内部队列)
- **内部层** (`EpollRunner`): 后台线程poll主UMQ → 发现可读 → 通过eventfd通知用户层

### TCP Fallback

每个API入口都有`UBS_NATIVE_TCP_MODE`守卫。socket在`RAW_ESTABLISHED`状态时数据走TCP。连接建立阶段TCP作为控制面，UMQ就绪后切换到RDMA数据面。

### UmqErrnoConverter

UMQ返回值 → Linux errno 映射器。三种映射路径:
1. `Convert(op, umqRet, errno)` — 通用int返回值映射表
2. `ConvertBufStatus(op, bufStatus, errno)` — 缓冲状态方向映射 (WRITEV→EPIPE, READV→ECONNRESET)
3. `ConvertHandleResult(op, errno)` — handle返回值有限映射 (CREATE→EINVAL/EPERM)

### 外部锁注入

`u_init_options_t`接受三个函数指针表 (mutex/rwlock/semaphore ops)，允许brpc注入自己的butex/semaphore实现，替代pthread原语。

---

## 目录结构速查

```
src/ubsocket/
  ├─ include/              # 公共C API头文件 (ubsocket.h等5个)
  ├─ csrc/
  │   ├─ entry/            # API劫持入口 (ubsocket_sock.cpp等)
  │   ├─ common/           # 基础设施 (GlobalSetting, SocketSet, BlockCache等)
  │   ├─ core/
  │   │   ├─ socket/       # SocketBase, DataTx/Rx, Acceptor/Connector
  │   │   └─ umq/          # UmqSocket, UmqTxOps/RxOps/ConnectorOps/AcceptorOps
  │   ├─ under_api/        # UmqApi/LibcApi/DlApi (dlopen抽象)
  │   ├─ profiling/        # 维测tracepoint
  │   └─ under_api/urma/   # URMA后端 (条件编译，从覆盖率排除)
  ├─ unit_test/            # 测试 (gtest + mockcpp)
```