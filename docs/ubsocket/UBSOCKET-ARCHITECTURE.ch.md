# UBSocket 内部架构与调用链

## 1. 定位与总览

UBSocket 是**用户态无感加速库**：北向提供与 POSIX socket/epoll 完全一致的 C API (`ubsocket_<func>`)，南向通过 URMA 传输数据。应用只需将 `socket()` → `ubsocket_socket()` 即可完成 TCP→UB 的劫持替换。

### 1.1 层次架构总图

```mermaid
graph TD
    subgraph APP["用户应用（bRPC等）"]
        APP_API["socket / connect / writev / readv / epoll_wait ..."]
    end

    subgraph PUBLIC["Public API Layer — include/ubsocket*.h"]
        PUB_H["ubsocket.h — 生命周期/日志"]
        PUB_SOCK["ubsocket_sock.h — 22个socket POSIX API"]
        PUB_EPOLL["ubsocket_epoll.h — 5个epoll POSIX API"]
        PUB_CTNL["ubsocket_ctnl.h — fcntl/ioctl封装"]
    end

    subgraph ENTRY["Entry Layer — csrc/*.cpp（API劫持入口）"]
        E_SOCK["ubsocket_sock.cpp"]
        E_EPOLL["ubsocket_epoll.cpp"]
        E_CTNL["ubsocket_cntl.cpp"]
        E_INIT["ubsocket.cpp"]
    end

    subgraph CORE["Core Layer — csrc/core/"]
        subgraph ABSTRACT["通用抽象层（禁止引用urma/）"]
            SOCKET_BASE["Socket / SocketBase"]
            DATA_TX["DataTx / DataTxOps"]
            DATA_RX["DataRx / DataRxOps"]
            CONNECTOR["Connector / ConnectorOps"]
            ACCEPTOR["Acceptor / AcceptorOps"]
            EVENT_EPOLL["EventPoll"]
            TX_CQE["TxCqePoller"]
            WAKEUP["UbsocketWakeupEvent"]
        end
        subgraph URMA["URMA实现 — csrc/core/urma/"]
            URMA_SOCKET["UrmaSocket / UrmaWrapper"]
            URMA_OPS["UrmaTxOps / UrmaRxOps"]
            URMA_CONN["UrmaConnectorOps / UrmaAcceptorOps"]
            URMA_BACKEND["UrmaBackend"]
            URMA_POLLER["UrmaPoller / UrmaEventPoll"]
            URMA_ERRNO["UrmaErrnoConverter"]
        end
    end

    subgraph COMMON["Common Layer — csrc/common/"]
        C_SETTING["GlobalSetting / SettingValidator / Version"]
        C_SET["SocketSet / ArraySet"]
        C_LOCK["Lock / LockRegistry"]
        C_RING["SPSCRingQueue / QbufQueue"]
        C_THREAD["ThreadPool / ExecutorService"]
    end

    subgraph UNDER["Under API Layer — csrc/under_api/"]
        DL["DlApi → dlopen liburma.so + libc.so.6"]
        LIBC["LibcApi / UrmaApi"]
    end

    subgraph PROF["Profiling — csrc/profiling/"]
        PROF_TRACER["ProfTracer / Tracepoint / StatsMgr / ProbeManager"]
    end

    subgraph TRANSPORT["Transport — liburma.so → UB内核驱动 → RDMA-like硬件"]
    end

    APP_API -->|UB_API_WRAP劫持| ENTRY
    ENTRY -->|Guard: UBS_NATIVE_TCP_MODE| LIBC
    ENTRY -->|Guard: domain!=AF_SMC| LIBC
    ENTRY --> CORE
    ABSTRACT --> URMA
    URMA --> UNDER
    UNDER --> TRANSPORT
```

### 1.2 公共API覆盖

| 头文件 | 函数数 | 类别 |
|--------|--------|------|
| `ubsocket.h` | 8 | 库生命周期 + 日志 + IO缓冲区 |
| `ubsocket_sock.h` | 22 | socket / close / shutdown / readv / writev / send / recv 等 |
| `ubsocket_epoll.h` | 5 | epoll_create / epoll_create1 / epoll_ctl / epoll_wait / epoll_pwait |
| `ubsocket_ctnl.h` | 3 | fcntl / fcntl64 / ioctl |

**总计38个公共C API函数**，全部声明为 `extern "C"`。

### 1.3 目录结构速查

```
src/ubsocket/
├── include/                     # 公共C API头文件（5个）
├── UBSOCKET_VERSION             # 全仓统一版本号（当前：1.0.0）
├── csrc/
│   ├── ubsocket.cpp             # 库初始化/uninit/日志/版本
│   ├── ubsocket_sock.cpp        # socket/close/shutdown/readv/writev等劫持
│   ├── ubsocket_epoll.cpp       # epoll_create/ctl/wait劫持
│   ├── ubsocket_cntl.cpp        # fcntl/ioctl劫持
│   ├── common/                  # 基础设施（28个文件）
│   ├── core/                    # 核心层
│   │   ├── ubsocket_*.h/cpp     # 通用抽象层（14个文件）
│   │   └── urma/                # URMA实现（44个文件）
│   ├── under_api/               # 动态库加载封装（7个文件）
│   ├── iobuf/                   # 零拷贝Block/BlockCache（3个文件）
│   ├── profiling/               # 维测基础设施（19个文件）
│   └── cli/                     # CLI诊断工具（7个文件）
├── unit_test/                   # 测试（gtest + mockcpp）
└── tools/                       # 辅助工具
```

---

## 2. 对象模型

### 2.1 Socket继承层次

```mermaid
classDiagram
    class Socket {
        -int raw_socket_
        -int event_fd_
        -SocketState state_
        -SocketType type_
        +State() SocketState
        +Fd() int
        +GetTxFd()* int
        +IsBindRemote()* bool
        +AddTxEvent()* Result
        +ProcessEpollEvent()* Result
    }

    class SocketBase {
        -DataTx tx_
        -DataRx rx_
        -Acceptor* acceptor_
        -Connector* connector_
        -EventPoll* added_epoll_fd_
        +Create(fd, type) static Result
        +GenerateSocketCommOps(sock) static Result
        +Accept() int
        +Connect() int
        +WriteV() int
        +ReadV() int
        +NotifyReadable() int
    }

    class UrmaSocket {
        空壳 — 无成员/方法覆盖
    }

    class DataTxOps {
        <<interface>>
        +BuildIovConverter()* ConverterPtr
        +AllocTxBuf()* uintptr_t
        +PostSend()* int
        +PollTx()* int
        +FlushTx()*
    }
    class DataRxOps {
        <<interface>>
        +PollRx()* int
        +RearmRxInterrupt()* int
        -block_cache_ BlockCache
    }
    class ConnectorOps {
        <<interface>>
        +PrepareConnect()* Result
        +Negotiate()* Result
        +CreateSocketResources()* Result
        +DestroySocketResources()*
    }
    class AcceptorOps {
        <<interface>>
        +PrepareConnect()* Result
        +Negotiate()* Result
        +CreateSocketResources()* Result
        +DestroySocketResources()*
        +ValidateProtocol()* int
    }

    Socket <|-- SocketBase
    SocketBase <|-- UrmaSocket
    SocketBase o-- DataTx
    SocketBase o-- DataRx
    SocketBase o-- Acceptor
    SocketBase o-- Connector
    DataTx o-- DataTxOps
    DataRx o-- DataRxOps
    Connector o-- ConnectorOps
    Acceptor o-- AcceptorOps
```

### 2.2 Socket 状态机

```mermaid
stateDiagram-v2
    [*] --> INIT : socket()
    INIT --> RAW_ESTABLISHED : connect()/accept() TCP完成
    RAW_ESTABLISHED --> ESTABLISHED : Negotiate + CreateUrmaResources + 段信息交换
    ESTABLISHED --> SHUTDOWN : shutdown()
    SHUTDOWN --> CLOSE : close()
    INIT --> CLOSE : close()（未建立时）
    RAW_ESTABLISHED --> CLOSE : close()

    note right of RAW_ESTABLISHED : TCP已连但UB通道未建立\n数据走TCP fallback
    note right of ESTABLISHED : URMA通道就绪\n数据走RDMA通道
```

---

## 3. 版本协商协议

### 3.1 版本号编码

协议版本号采用32位编码：`[Major(6bit)][Minor(12bit)][Patch(14bit)]`。版本号来源：CMake/Bazel从 `UBSOCKET_VERSION` 文件自动生成，全代码仓统一。

| 宏 | 作用 |
|----|------|
| `UBS_MAKE_PROTOCOL_VERSION(major, minor, patch)` | 编码 |
| `UBS_PROTOCOL_VERSION_MAJOR(v)` | 提取Major (6bit, bit26-31) |
| `UBS_PROTOCOL_VERSION_MINOR(v)` | 提取Minor (12bit, bit14-25) |
| `UBS_PROTOCOL_VERSION_PATCH(v)` | 提取Patch (14bit, bit0-13) |
| `UBS_PROTOCOL_VERSION` | 当前版本 constexpr uint32_t |

### 3.2 协商流程

```mermaid
sequenceDiagram
    participant C as Client
    participant S as Server

    C->>S: NegotiateReq (magic + local_version + body_len + body)
    S->>S: NegotiateVersion(local, peer)
    alt Major不匹配
        S-->>C: negotiated_version = UBS_PROTOCOL_VERSION（server版本）
        Note over C: ValidateNegotiatedVersion → Major不一致 → fallback TCP
    else Major一致
        S->>S: 协商：取较低全版本（直接整数比较）
        S-->>C: negotiated_version (4B) + NegotiateRsp body
        C->>C: ValidateNegotiatedVersion() → ok
        Note over S: 随Rsp发送：ret_code + trans_mode + conn_flag + conn_id
    end
```

关键函数 (`ubsocket_version.h`):
- `NegotiateVersion(local, peer)` — Acceptor侧；Major一致→取较低全版本
- `ValidateNegotiatedVersion(local, negotiated)` — Connector侧校验
- Major不一致→降级TCP（不可重试）；Minor/Patch差异→按较低方交互

### 3.3 版本协商跨版本兼容

- `RecvLengthPrefixed` 读body_len后 `min(body_len, sizeof_struct)` 读body，不足零填、超出丢弃
- `NEGOTIATE_REQ_WIRE_SIZE` 自动跟随 `sizeof(NegotiateReq)`，struct变更后不需手动维护常量

---

## 4. 连接建立

### 4.1 握手模式

```mermaid
graph TD
    HANDSHAKE{握手模式?} -->|UB_SOCK_OPT| SOCK_OPT["setsockopt(TCP_UB_SOCKET_HANDSHAKE) + connect()"]
    HANDSHAKE -->|TFO| TFO["sendto(MSG_FASTOPEN) — SYN携带协商数据"]
    HANDSHAKE -->|NORMAL| NORMAL["普通TCP connect()"]
    SOCK_OPT -->|ENOPROTOOPT fallback| TFO
    TFO -->|Cookie不存在| RETRY["新fd重试sendto后dup3"]
    TFO -->|Cookie存在| NEGOTIATE["进入协商阶段"]
    NORMAL --> NEGOTIATE
    RETRY --> NEGOTIATE
```

### 4.2 Connector 建链流程

```mermaid
sequenceDiagram
    participant C as Client(Connector)
    participant TCP as TCP通道
    participant S as Server(Acceptor)
    participant UB as URMA后端

    Note over C: step0 — PrepareConnect
    C->>TCP: TFO/UB_SOCK_OPT/Normal connect
    C->>C: SetTcpNoDelay + SetNonBlocking

    Note over C,S: step1 — Negotiate（版本+连接参数协商）
    C->>TCP: NegotiateReq（magic + version + body）
    S->>S: NegotiateVersion → 校验Major
    S-->>C: negotiated_version (4B) + NegotiateRsp（ret_code + trans_mode + conn_flag + conn_id）
    C->>C: ValidateNegotiatedVersion

    Note over C,S: step2 — CreateSocketResources
    C->>UB: CreateUrmaResources → 创建jetty/JFS/JFR等URMA资源
    C->>C: GenerateSocketCommOps → UrmaTxOps + UrmaRxOps
    C->>TCP: ExchangeSocketResources（TCP交换段信息：buf地址/jetty-id/seg）
    C->>UB: ImportRemoteJetty + 注册对端段
    C->>C: 绑定本地收包seg + 预投空recv WR
    C->>C: state = ESTABLISHED
```

### 4.3 Acceptor 建链流程

```mermaid
sequenceDiagram
    participant L as Listen fd
    participant A as Acceptor
    participant TCP as TCP通道
    participant SOCK as SocketBase
    participant UB as URMA后端
    participant TP as ThreadPool

    A->>L: LibcApi::accept()
    alt 同步Accept
        L-->>A: new_fd
        A->>A: 非TFO连接→直接返回fd（普通TCP）
        A->>A: TFO连接→ProcessUBConnection
    else 异步Accept
        A->>A: TryPopAsyncReadyFd → ready_queue取
        L-->>A: fd（异步任务完成入队的）
    end

    A->>A: ValidateProtocol → 校验magic
    A->>SOCK: SocketBase::Create → UrmaSocket
    A->>TCP: AcceptNegotiate → 收Req + 发Rsp
    A->>UB: ExchangeSocketResources（CreateUrmaResources + 段交换 + 预投recv WR）
    A->>SOCK: SocketSet::OverrideItem(new_fd, socket_obj)
```

### 4.4 异步Accept流程

```mermaid
sequenceDiagram
    participant APP as 用户epoll_wait
    participant AEP as AsyncEventPoll
    participant A as Acceptor
    participant TP as ThreadPool
    participant WK as UbsocketWakeupEvent

    Note over A: 懒初始化（首次Accept时）
    A->>TP: ExecutorService::Start
    A->>WK: Initialize(epoll_fd) + SetListenFd
    A->>AEP: SetWakeupCallback(ready_event, ProcessReadyEvents)

    APP->>A: accept()
    A->>A: LibcApi::accept → new_fd
    A->>TP: Execute(ProcessUBConnection)
    A-->>APP: errno=EAGAIN, return -1

    Note over TP: 异步建链完成
    TP->>WK: WakeUpReadyEventFd(listen_fd)
    WK->>WK: eventfd_write触发epoll事件

    APP->>AEP: epoll_wait返回
    AEP->>WK: ProcessReadyEvents → 注入EPOLLIN
    APP->>A: 再次accept()
    A->>A: TryPopAsyncReadyFd → ready_queue返回已建链fd
    A-->>APP: return new_fd（UB通道已就绪）
```

### 4.5 建链重试/降级状态机

```mermaid
stateDiagram-v2
    [*] --> kSTART
    kSTART --> kOK : ack OK && peer OK
    kSTART --> kRETRY : bonding && (ack可重试 || peer可重试)
    kSTART --> kDEGRADE : degradable && 非bonding/跨芯片失败
    kSTART --> kFAILED : 其他失败

    kRETRY --> kOK : DoUbConnectRetry成功
    kRETRY --> kRETRY_FAILED_CHECK_OTHER_ROUTE : 无可选备路
    kRETRY --> kDEGRADE : degradable

    kRETRY_FAILED_CHECK_OTHER_ROUTE --> kDEGRADE : degradable
    kRETRY_FAILED_CHECK_OTHER_ROUTE --> kFAILED : 不degradable

    kDEGRADE --> [*] : 降级到纯TCP
    kFAILED --> [*] : 建链失败
    kOK --> [*] : URMA通道就绪，state=ESTABLISHED
```

---

## 5. 数据面

### 5.1 发送流程（WriteV）

```mermaid
sequenceDiagram
    participant APP as 用户
    participant SOCK as SocketBase
    participant TX as DataTx
    participant TXOPS as UrmaTxOps
    participant URMA as URMA后端

    APP->>SOCK: ubsocket_writev(fd, iov, iovcnt)
    SOCK->>SOCK: UBS_NATIVE_TCP_MODE/RAW_ESTABLISHED → TCP fallback
    SOCK->>TX: WriteV(sock, iov, iovcnt)

    TX->>TXOPS: WriteV(sock, iov, iovcnt, flags)
    activate TXOPS
    alt total ≤ URMA_INLINE_DATA_THRESHOLD
        TXOPS->>TXOPS: WriteInline → 控制头+用户数据拷入send_buf
        TXOPS->>URMA: 下发RDMA WRITE_IMM（WRITE_IN_BAND）
    else 携带UBS_IO_FLAG_USE_BRPC_BLOCK（零拷贝）
        TXOPS->>TXOPS: WriteZeroCopy → 构建BlockRef描述符
        TXOPS->>URMA: 下发RDMA WRITE_IMM（PRE_WRITE）+ Block::IncRef
    else 大IO非零拷贝
        TXOPS->>TXOPS: WriteLargeIO → 分配Block并拷贝用户数据
        TXOPS->>TXOPS: 复用WriteZeroCopy路径下发
    end
    Note over TXOPS: 失败→UrmaErrnoConverter::Convert(WRITEV)
    deactivate TXOPS
    TX-->>APP: return 已发送字节数（inline路径可能短写）
```

### 5.2 接收流程（ReadV + UrmaPoller）

#### CQE 派发（UrmaPoller后台线程）

```mermaid
sequenceDiagram
    participant POLLER as UrmaPoller(后台线程)
    participant URMA as URMA后端(JFC)
    participant BACKEND as UrmaBackend
    participant RX as UrmaRxOps
    participant APP as 用户

    loop
        POLLER->>URMA: EventPolling/BusyPolling → poll JFC CQE
        URMA-->>BACKEND: CQE（WRITE_IMM / RDMA READ完成）
        BACKEND->>RX: HandleCqe → DataReached(seq_no, write_imm)
        RX->>RX: WRITE_IN_BAND→slot置DATA_READY<br/>PRE_WRITE→发起RDMA READ（slot置READING）
        RX->>APP: eventfd唤醒 / NotifyReadable内联记录
    end

    APP->>RX: ubsocket_readv(fd, iov)
    RX->>RX: ReadV → 按seq_no顺序消费slot
    alt WRITE_IN_BAND
        RX->>RX: ConsumeInlineData（从recv_buf拷入iov）+ 回WRITE_IN_BAND_ACK
    else PRE_WRITE零拷贝
        RX->>RX: ZeroCopyRead → block_cache_.CutAndInsertAfter 链入brpc Block
    else RDMA READ完成
        RX->>RX: ConsumeBlockData（Block数据拷入iov）+ 回POST_WRITE
    end
    RX-->>APP: return rx_total_len
```

说明：
> - CQE 事件有两种派发策略：Inline 模式由 poller 线程立即处理；Dispatcher 模式事件入队（ReadableEventQueue），由 readv 排空时推进。
> - DISPATCHER 模式下，JFCE 中断 fd 注册进用户 epoll（UrmaEventPoll），JFC 轮询与派发在 epoll_wait 内联完成。

---

## 6. Epoll 多层架构

### 6.1 总体架构

```mermaid
graph TD
    subgraph USER["用户层 (User)"]
        AEP["UrmaEventPoll — 用户epoll_fd"]
        KERNEL["内核epoll: raw_socket_fd + event_fd + JFCE中断fd"]
        QUEUE["ReadableEventQueue — 可读事件缓冲队列"]
        AEP -->|epoll_wait| KERNEL
        AEP -->|DrainPendingReadable| QUEUE
    end

    subgraph INTERNAL["内部层 (Background Threads)"]
        POLLER["UrmaPollerSet — UrmaPoller后台线程<br/>EventPolling/BusyPolling poll JFC CQE<br/>经HandleCqe按连接派发"]
    end

    POLLER -->|eventfd write / inline记录| QUEUE
```

### 6.2 UrmaEventPoll 内部结构

```mermaid
classDiagram
    class EventPoll {
        <<abstract>>
        -int epoll_fd_
        -u_mutex_t* ctl_mutex_
        +EpollCtl(op, fd, event)* int
        +EpollWait(events, max, timeout)* int
    }

    class UrmaEventPoll {
        -unordered_map jfce_handlers_（JFCE中断fd→注册事件）
        -unordered_map socket_data_（fd→EpollEvent*）
        -EpollEvent* removed_head_（延迟释放链表）
        -ReadableEventQueue readable_events_（可读事件缓冲）
        +EpollCtl() 按fd类型分派: raw_socket / urma_socket / JFCE
        +EpollWait() 合并内核事件+pending可读事件
        +NotifyReadable(sock) 内联记录可读事件
    }

    EventPoll <|-- UrmaEventPoll
```

**EpollCtl分派逻辑**:
- `raw_socket_fd` → 透传到内核epoll
- urma socket fd → 内部托管（`AddUrmaSocketEvent`），不透传内核
- JFCE中断fd → 注册到`jfce_handlers_`，事件到来时内联poll CQE

---

## 7. 零拷贝IOBuf机制

### 7.1 Block结构（8K对齐）

```
 ┌──────────────────────8K对齐边界──────────────────────┐
 │  Block（2×8B + atomic 4B + …）│      buf_data       │
 └──────────────────────────────────────────────────────┘
                                  ▲
                                  │ PtrFloorToBoundary(buf_data)
                                  │ = (char*)buf_data - sizeof(Block)
```

`blockmem_allocate_zero_copy(size)` 分配8K对齐内存，在起始处placement new `Block`。通过8K对齐特性可回溯到`Block`。

### 7.2 Block / BlockCache

```mermaid
classDiagram
    class Block {
        +atomic int nshared
        +uint16_t flags（IOBUF_BLOCK_FLAGS_UB）
        +uint32_t size / cap
        +char* data
        +Block* portal_next
        +IncRef() / DecRef()（nshared→0时deallocate）
        +Full() bool / LeftSpace() size_t
    }
    class BlockRef {
        +uint32_t offset / length
        +Block* block
        +Reset()
    }
    class BlockCache {
        -BlockRef partial_block_
        -Block* head_block_ / tail_block_
        -uint64_t cache_len_
        +Insert(data, size)
        +CutAndInsertAfter(cut_size, block) ssize_t
        +Flush()
    }
    BlockCache *-- BlockRef
    BlockRef o-- Block
    Block --> Block : portal_next链表
```

### 7.3 数据交付给brpc

```mermaid
sequenceDiagram
    participant RX as UrmaRxOps::ReadV
    participant CACHE as BlockCache
    participant BRPC as brpc Block链

    RX->>RX: ZeroCopyRead → 就绪Block链
    RX->>CACHE: 接收侧data_block链移入block_cache_

    APP->>RX: ubsocket_readv → CutAndInsertAfter(cut_size, brpc_block)
    Note over CACHE: 切割缓存Block链后插入brpc Block链
    CACHE-->>BRPC: brpc_block → ... → [Block 1] → [Block 2] → ... → original_next
```

---

## 8. 库初始化

### 8.1 初始化流程

```mermaid
flowchart TD
    START[ubsocket_init] --> STEP1["step1: GlobalSetting::AddRules + LoadEnv<br/>+ options覆盖 + VerifySetting"]
    STEP1 --> STEP2["step2: DlApi::Load - dlopen liburma.so + libc.so.6"]
    STEP2 --> STEP3["step3: LockRegistry::RegisterDefaultOps<br/>+ 外部锁/信号量/RPC追踪注入"]
    STEP3 --> STEP4["step4: SocketSet/ArraySet EpollPoll Init"]
    STEP4 --> STEP6["step5: UrmaBackend::Init → urma_init<br/>+ ContextPool/JettyPool/PollerSet/IoBufPool初始化<br/>（注册零拷贝分配器）"]
    STEP6 --> STEP7["step6: signal SIGUSR2 → dump handler"]
    STEP7 --> STEP8{维测开关?}
    STEP8 -->|prof| PROF["Profiling::Init"]
    STEP8 -->|cli| CLI["GlobalStatsMgr"]
    STEP8 -->|probe| PROBE["ProbeManager::Start"]
    STEP8 -->|trace| TRACE["PrintStatsMgr + SplitTrace"]
    PROF --> DONE
    CLI --> DONE
    PROBE --> DONE
    TRACE --> DONE
    DONE["step N: UBS_INITED = true"]
```

### 8.2 初始化选项（`u_init_options_t`）

| 字段 | 默认值 | 说明 |
|------|--------|------|
| `allowed_protocol` | `UBS_PROTOCOL_TCP` | 允许的传输协议 |
| `async_acceptor_thread_count` | 0 | 异步Accept线程数（0=同步） |
| `async_connector_enable` | `false` | Client 全异步建链开关（true=异步，false=同步） |
| `async_epoll_thread_count` | 1 | 后台epoll线程数 |
| `lock_ops` | nullptr | 外部互斥锁注入（brpc butex等） |
| `rw_lock_ops` | nullptr | 外部读写锁注入 |
| `sem_ops` | nullptr | 外部信号量注入 |
| `rpc_id_ops` | nullptr | 外部RPC上下文追踪 |

---

## 9. UrmaErrnoConverter

### 9.1 两种映射路径

```mermaid
flowchart LR
    subgraph INPUT["输入"]
        OP["UrmaOperation枚举<br/>CONNECT/ACCEPT/WRITEV/READV/<br/>CREATE_CTX/CREATE_JETTY/IMPORT_JETTY/BIND"]
        STATUS["urma_status_t"]
        CR_STATUS["urma_cr_status_t"]
        ERRNO["原始errno"]
    end

    subgraph MAP["映射路径"]
        CONV["Convert(op, urmaStatus)<br/>urma_status_t映射表<br/>EAGAIN/ENOMEM/EINVAL<br/>按op兜底：CONNECT/ACCEPT→ECONNREFUSED"]
        CR["ConvertCrStatus(op, crStatus)<br/>建链完成状态映射<br/>RNR重试超限→EAGAIN / ACK超时→ETIMEDOUT"]
    end

    OP --> CONV
    STATUS --> CONV
    ERRNO --> CONV
    OP --> CR
    CR_STATUS --> CR
    ERRNO --> CR

    CONV --> OUTPUT[mapped errno]
    CR --> OUTPUT
```

覆盖8种UrmaOperation，errno在生产代码调用URMA API后**立即保存**（errno必须在调用被测函数之前设置）。

---

## 10. 核心设计模式

| 模式 | 位置 | 作用 |
|------|------|------|
| **函数劫持** | `UB_API_WRAP()` 宏 | 透明替换POSIX调用 |
| **Strategy(Ops)** | 5组虚接口 | 可插拔传输实现 |
| **Factory** | `SocketBase::Create / GenerateSocketCommOps` | 按SocketType创建ops |
| **Template Method** | `Connector::Connect / Acceptor::Accept` | 固定步骤: Prepare→Negotiate→CreateResources |
| **Singleton** | `SocketSet / ArraySet` | 全局注册表 |
| **Leaky Singleton** | `TxCqePoller / UrmaPollerSet` | 进程生命周期，不析构 |
| **Bridge/Adapter** | `UrmaApi / LibcApi / DlApi` | 抽象dlopen vs 直接链接 |
| **Guard/Bypass** | `UBS_NATIVE_TCP_MODE` 检查 | UB不可用时回退TCP |
| **Ref Counting** | `DECLARE_REF_COUNT_VARIABLE` | Socket / EventPoll / Ops生命周期 |
| **State Machine** | `UBHandshakeState` | 建链重试/降级控制 |
| **Scope Exit** | `MakeScopeExit()` | RAII资源恢复 |

---

## 11. 关键概念速查

### Share-JFR（共享接收队列）
URMA支持`URMA_SHARE_JFR`共享接收模式：多个连接可共享JFR（共享接收队列），CQE由UrmaPoller后台统一poll并按连接派发；连接也可使用独占jetty。

### 双层Epoll
**用户层**（UrmaEventPoll）：合并内核事件 + 可读事件缓冲队列（ReadableEventQueue）。**内部层**（UrmaPollerSet）：后台线程poll JFC CQE并按连接派发；Dispatcher模式下JFCE事件在用户epoll_wait内联处理。

### TCP Fallback
`UBS_NATIVE_TCP_MODE`守卫 + `RAW_ESTABLISHED`状态时走TCP。建链失败时通过`IsDegradable()`判断是否可降级到纯TCP。

### 外部锁注入
`u_init_options_t`提供mutex/rwlock/semaphore/rpc_id四个函数指针表，brpc可注入自己的butex/semaphore实现。

### 依赖方向规则
- 通用层（`core/ubsocket_*`）禁止引用任何具体实现子目录头文件
- 工厂方法（`ubsocket_socket.cpp`）是唯一例外——它根据`SocketType`创建具体对象
- 具体实现层（`core/urma/*`）可引用通用层头文件
