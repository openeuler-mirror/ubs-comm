# ProbeManager 端到端 RTT 探测能力设计

## 一、概述

### 1.1 问题定义

ProbeManager 解决的是**端到端往返延迟 (RTT) 的分段测量**问题。

两个节点 (Client / Server) 各分三层：从下到上依次为 **硬件层**、**UMQ 层**、**ubsocket 应用层**。探针包在这两层节点间往返，每经过一个层边界就打一个时间戳：

```
    Client 节点                              Server 节点
  ┌─────────────────┐                      ┌─────────────────┐
  │  ubsocket 应用层 │                      │  ubsocket 应用层 │
  │                 │     请求方向 ──▶      │                 │
  │  ① 发送探测包    │─────────────────────▶│  ④ 应用层接收    │
  │                 │                      │  ⑤ 应用层回复    │
  │  ⑧ 收到响应      │◀─────────────────────│                 │
  └────────┬────────┘     响应方向 ◀──      └────────┬────────┘
           │                                        │
  ┌────────┴────────┐                      ┌────────┴────────┐
  │     UMQ 层       │                      │     UMQ 层       │
  │                 │                      │                 │
  │  ② UMQ post     │══════════════════════│  ③ UMQ poll rx  │
  │  ⑦ UMQ poll rx  │◀═════════════════════│  ⑥ UMQ post     │
  └────────┬────────┘                      └────────┬────────┘
           │                                        │
  ┌────────┴────────┐                      ┌────────┴────────┐
  │     硬件层       │                      │     硬件层       │
  │  网卡/DMA/RDMA   │◀────────────────────▶│  网卡/DMA/RDMA   │
  │                 │    物理网络链路        │                 │
  └─────────────────┘                      └─────────────────┘
```

8 个时间戳采集点 (①~⑧) 分布在三层边界上：

| 编号 | 位置 | 含义 | 延迟段 |
|------|------|------|--------|
| ① | Client ubsocket → UMQ 边界 | 应用层发起探测 | — |
| ② | Client UMQ → 硬件 边界 | UMQ 提交发送 | ①→② 应用层→UMQ 提交开销 |
| ③ | Server 硬件 → UMQ 边界 | UMQ 接收完成 | ②→③ UMQ 网络传输延迟 |
| ④ | Server UMQ → ubsocket 边界 | 应用层处理接收 | ③→④ UMQ→应用层开销 |
| ⑤ | Server ubsocket → UMQ 边界 | 应用层发起回复 | ④→⑤ Server 处理时间 |
| ⑥ | Server UMQ → 硬件 边界 | UMQ 提交回复 | ⑤→⑥ 应用层→UMQ 提交开销 |
| ⑦ | Client 硬件 → UMQ 边界 | UMQ 接收完成 | ⑥→⑦ UMQ 网络传输延迟 |
| ⑧ | Client UMQ → ubsocket 边界 | 应用层收到响应 | ⑦→⑧ UMQ→应用层开销 |

**总 RTT** = ⑧ - ① = 应用层开销 (4 段) + UMQ 传输层开销 (4 段)。

ProbeManager 通过在 UMQ 数据包中嵌入 `ProbeTimeInfo` 结构体，在上述 8 个层边界采集时间戳，实现全链路延迟分解。

### 1.2 设计目标

- **零侵入业务代码**: 探针包复用 UMQ 数据通道，通过 `imm.user_data` 标识，不干扰正常流量
- **永远编译、运行时开关**: probe 代码始终编译进二进制，通过环境变量 `UBSOCKET_PROBE_ENABLE` 控制是否启动探测线程，无需重新编译
- **8 段延迟分解**: 应用层 4 段 + UMQ 传输层 4 段，精确定位瓶颈位置
- **低开销**: 探测频率可配（默认 30 秒），热路径仅靠 `opcode` + `user_data` 整数比较短路（非探针包第一条即跳走），probe 未启动时无额外开销
- **CLI 可查询与动态控制**: 通过 UDS 暴露探测数据给外部 CLI 工具，支持通过 `ubstat` 动态开启/关闭 probe 功能

### 1.3 开关控制

probe 代码**始终编译进二进制**，无 `#ifdef` 条件编译开关。支持两种控制方式：**环境变量（静态）** 和 **ubstat CLI（动态）**。

#### 1.3.1 环境变量控制（静态）

外部模块通过 3 个环境变量在进程启动时控制 probe 功能，均在 `ubsocket_init()` → `GlobalSetting::Load()` 时读取（`ubsocket_global_setting.cpp`）：

| 环境变量 | 默认值 | 合法值 / 范围 | 作用 |
|----------|--------|--------------|------|
| `UBSOCKET_PROBE_ENABLE` | `false` | `true\|false` | 总开关。`true` 时 `ubsocket_init` 调用 `ProbeManager::Start()`，`ubsocket_uninit` 调用 `Stop()`；`false` 时不启动探测线程，热路径 `opcode` + `user_data` 检查自然不命中，无额外开销 |
| `UBSOCKET_PROBE_INTERVAL_MS` | `30000` | `1 ~ 360000` (ms) | 探测间隔。控制工作线程 `PeriodicProbe` 的 `sem_timedwait` 超时周期，同时作为 dump 线程的周期 |
| `UBSOCKET_PROBE_BATCH_SIZE` | `50` | `1 ~ 500` | 每轮探测的最大 socket 数。`ProcessClientProbing` 每轮最多向 `mProbeBatch` 个 client socket 发送探针包 |
| `UBSOCKET_PROBE_DUMP_FILE_PATH` | `/tmp/ubsocket/probe` | 非空字符串，最大 512 字符 | dump 文件目录路径。dump 线程将探测数据写入此目录下的 `ubsocket_probe_<pid>.log` 文件 |

参数校验规则（`ubsocket_global_setting.cpp` `SettingValidator`）：
- 字符串类型 (`UBSOCKET_PROBE_ENABLE`)：值必须匹配 `true|false`，否则保留默认值
- 数值类型 (`UBSOCKET_PROBE_INTERVAL_MS`、`UBSOCKET_PROBE_BATCH_SIZE`)：值必须在范围内，否则保留默认值

生命周期挂载点：
- **启动**: `ubsocket.cpp` — `if (UBS_PROBE_ENABLED) ProbeManager::Start(...)`
- **停止**: `ubsocket.cpp` — `if (UBS_PROBE_ENABLED) ProbeManager::Stop()`

#### 1.3.2 ubstat CLI 动态控制

在进程运行期间，可通过 `ubstat` 工具动态开启/关闭 probe 功能，无需重启进程：

```
ubstat -p <pid> probe -t enable     # 动态开启 probe（使用环境变量配置的 interval/batch）
ubstat -p <pid> probe -t disable    # 动态关闭 probe
ubstat -p <pid> probe [-t query]    # 查询探测数据（默认行为，-t query 可省略）
ubstat -p <pid> probe -t dumppath -v <path>  # 设置 dump 文件目录路径
ubstat -p <pid> probe -w            # 实时监控模式（每秒刷新）
```

**动态控制流程**：

```
ubstat -p <pid> probe -t enable
  │
  ├── CLIClient::ProcessProbe(sockfd, response, args)
  │     └── header.mCmdId = PROBE, header.mType = PROBE_OP_ENABLE
  │         → UDS → Server
  │
  ├── [Server 端] Listener::Process()
  │     └── header.mCmdId == PROBE && header.mType == PROBE_OP_ENABLE
  │         └── ProcessProbeControl(fd, header, enable=true)
  │             ├── GlobalSetting::UBS_PROBE_ENABLED = true
  │             ├── ProbeManager::Start(UBS_PROBE_MS, UBS_PROBE_BATCH, -1)
  │             │   ├── mRunning.exchange(true) — 幂等，已运行则直接返回
  │             │   ├── sem_init(&mSem, 0, 0)
  │             │   ├── 启动 PeriodicProbe 工作线程
  │             │   └── RegisterUmqCallbacks() — 注册 UMQ perf 回调
  │             └── 发送 CLIControlHeader{mErrorCode=OK, mDataSize=0} → CLI
  │
  └── [CLI 端]
        └── "Probe enable successfully"
```

**动态关闭流程**：

```
ubstat -p <pid> probe -t disable
  │
  ├── [Server 端] ProcessProbeControl(fd, header, enable=false)
  │     ├── ProbeManager::Stop()
  │     │   ├── mRunning.exchange(false) — 幂等，未运行则直接返回
  │     │   ├── sem_post(&mSem) — 唤醒工作线程
  │     │   ├── join 工作线程 (PeriodicProbe) — 停止发送探针包
  │     │   ├── sem_destroy(&mSem)
  │     │   └── mRecords.clear() — 清空探测记录
  │     ├── GlobalSetting::UBS_PROBE_ENABLED = false
  │     └── 发送 CLIControlHeader{mErrorCode=OK, mDataSize=0} → CLI
  │
  └── [CLI 端]
        └── "Probe disable successfully"
```

**动态关闭后的收发包行为**：

`Stop()` 执行后，probe 相关的收发包活动全部停止：

| 路径 | 关闭后行为 | 机制 |
|------|-----------|------|
| **发送探针包** (Client → Server) | 停止 | `PeriodicProbe` 工作线程退出，不再调用 `ProcessClientProbing` → `SendProbePacket` |
| **UMQ 传输层时间戳** | 继续（无探针包触发） | UMQ 回调仍注册，但 `UmqPerfCallback` 中 `imm.user_data` 检查不命中（无探针包发出） |
| **接收探针包** (Server RX) | 丢弃 | `HandleReceivedPacket` 检查 `mRunning.load()`，返回 `false` 时打印告警并返回，不写入 `mRecvQueue`；探针 buf 由调用方正常 `umq_buf_free` 释放 |
| **接收探针响应** (Client RX) | 丢弃 | 同上，`HandleReceivedPacket` 提前返回，不更新 `mRecords` |
| **后台自动 dump** | 停止 | `DumpStop()` 在 `Stop()` 中首先执行，dump 线程退出并关闭文件 |

**动态设置 dump 路径流程**：

```
ubstat -p <pid> probe -t dumppath -v /tmp/custom/probe
  │
  ├── CLIClient::ProcessProbe(sockfd, response, args)
  │     └── header.mCmdId = PROBE, header.mType = PROBE_OP_SET_DUMP_PATH
  │         header.mDataSize = pathLen
  │         → UDS → Server (header + path payload)
  │
  ├── [Server 端] Listener::Process()
  │     └── header.mCmdId == PROBE && header.mType == PROBE_OP_SET_DUMP_PATH
  │         └── ProcessProbeDumpPath(fd, header)
  │             ├── 接收 path payload (header.mDataSize 字节)
  │             ├── lock(ProbeDumpMutex); UBS_PROBE_DUMP_PATH = pathPayload
  │             └── 发送 CLIControlHeader{mErrorCode=OK, mDataSize=0} → CLI
  │
  └── [CLI 端]
        └── "Probe dump path set to /tmp/custom/probe successfully"
```

dump 线程在下一个 dump 周期自动检测到路径变化（`currentPath != mDumpLastFilePath`），关闭当前文件、在新路径下创建目录并打开新文件，无需重启线程。

**设计要点**：
- `Start()`/`Stop()` 均为**幂等操作**：`mRunning.exchange()` 保证重复调用安全
- 动态开启时使用 `GlobalSetting::UBS_PROBE_MS` / `UBS_PROBE_BATCH` 的当前值作为参数（环境变量在 `ubsocket_init` 时已加载，即使 probe 未启动也有默认值）
- `ubsocket_uninit()` 仍受 `UBS_PROBE_ENABLED` 控制：若动态关闭后 `UBS_PROBE_ENABLED=false`，则 `ubsocket_uninit` 不会重复调用 `Stop()`

### 1.3.3 后台自动 dump

probe 代码**始终编译进二进制**，当 probe 启用时会自动启动后台 dump 线程，将探测数据周期性地写入文件，无需手动查询。

**仅在 Client 端 dump**：与 probe 只在 Client 端发起的规格一致，`DumpData()` 在 `GetCLIProbeData()` 返回空列表时提前返回，不写文件。Server 端 `mRecords` 始终为空（Server 只接收探针包并回响应，不记录探测数据），因此不会产生 dump 文件。

**启动与停止**：

- **启动**: `ProbeManager::Start()` 中调用 `DumpStart()`，启动 dump 工作线程
- **停止**: `ProbeManager::Stop()` 中首先调用 `DumpStop()`（在工作线程 join 之前），确保 dump 线程不会在 `mRecords` 被清空后仍尝试访问

**dump 周期**：

复用 `UBSOCKET_PROBE_INTERVAL_MS`（即 `mIntervalMs`）。dump 线程以 10ms 粒度分片睡眠，便于快速响应停止信号。线程退出前执行一次 final drain，保证最后一批数据落盘。

**dump 内容**：

与 `ubstat probe` 查询结果完全相同，包含：
- 时间戳头部 (`timeStamp: YYYY-MM-DD HH:MM:SS`)
- `=== Probe Statistics ===` 标题 + socket 总数
- 列头: `FD | UBS RTT(ns) | CliΔ(ns) | SrvΔ(ns) | UMQ RTT(ns) | UMQ CliΔ(ns) | UMQ SrvΔ(ns)`
- 每个 socket 的概览行 + 8 个时间戳明细

**日志绕接规则**（参考 PROF 统计聚合层 dump）：

| 属性 | 值 |
|------|------|
| 单文件上限 | 10 MB (`PROBE_DUMP_FILE_MAX_SIZE`) |
| 最大归档数 | 3 个 `.gz` 文件 (`PROBE_DUMP_MAX_ARCHIVES`) |
| 归档格式 | gzip 压缩 (`.gz`) |
| 活跃文件名 | `<dump_path>/ubsocket_probe_<pid>.log` |
| 归档文件名 | `<dump_path>/ubsocket_probe_<pid>.log.gz` |
| 归档裁剪策略 | 按 mtime 升序排序，删除最旧的 |
| 打开模式 | 追加 (`std::ios::app`) |
| 目录权限 | `0750` |
| 路径变更处理 | 关闭当前文件、重置目录创建标志、清空文件名——下次写入时在新路径下重新创建目录并打开文件 |

**文件格式示例**：

```
timeStamp: 2026-09-04 12:34:56
=== Probe Statistics ===
Total Probes (SocketNum): 1

FD       | UBS RTT(ns) | CliΔ(ns)   | SrvΔ(ns)   | UMQ RTT(ns)  | UMQ CliΔ(ns) | UMQ SrvΔ(ns)
---------------------------------------------------------------------------------------------------
549      | 90430.000   | 92370.000  | 1940.000   | 66800.000    | 78750.000    | 11950.000
  +-- [Client] ubsocket_client_send(ns): 721659634534780 | ubsocket_client_recv(ns): 721659634627150
  |            umq_post(ns): 721659634540400 | umq_recv(ns): 721659634619150
  +-- [Server] ubsocket_server_recv(ns): 721660422425858 | ubsocket_server_rsp(ns): 721660422427798
  |            umq_recv(ns): 721660422418348 | umq_rsp(ns): 721660422430298
```

**CLI 动态设置 dump 路径**：

```
ubstat -p <pid> probe -t dumppath -v /tmp/custom/probe
```

dump 线程在下一个周期自动检测路径变更并切换，无需重启线程。路径变更通过 `ProbeDumpMutex` 保护，与 dump 线程的读取无竞争。

### 1.4 CLI 查询与展示

用户通过 `ubstat -p <pid> probe [-w]` 命令查询探测数据，`-w` 启用每秒刷新的实时监控模式。数据从 ProbeManager 到终端的完整路径如下：

```
ubstat -p <pid> probe [-w]
  │
  ├── CLIClient::ProcessProbe(sockfd, response, args)
  │     └── 发送 CLIControlHeader{mCmdId=PROBE, mType=PROBE_OP_QUERY} → UDS → 等待响应
  │
  ├── [Server 端] Listener::Process()
  │     └── header.mCmdId == PROBE && mType != ENABLE/DISABLE
  │         └── ProcessProbeRequest(fd, msg, header)
  │             ├── GetAllProbeData(probeDataList)
  │             │     └── ProbeManager::GetInstance().GetCLIProbeData(outDataVec)
  │             │         └── std::lock_guard<std::mutex> lock(mMutex)
  │             │             遍历 mRecords, 拷贝 ProbeTimeInfo + mLastRttNs → CLIProbeData
  │             ├── 填充 CLIProbeHeader{socketNum=N, probeNum=0, reserved=0}
  │             └── memcpy CLIProbeData[] → 发送给 CLI
  │
  └── [CLI 端] TerminalDisplay::DisplayProbeInfo(data, dataLen)
        ├── 解析 CLIProbeHeader{socketNum} + CLIProbeData[]
        ├── PrintProbeHeader()  — 打印表头
        ├── for each probeData:
        │     ├── PrintProbeRow(&probeData)   — 概览行
        │     │     UBS RTT   = probeData->rtt (Server 端计算)
        │     │     CliΔ      = client_recv_rsp - client_send
        │     │     SrvΔ      = server_rsp - server_recv
        │     │     UMQ RTT   = umqClientΔ - umqServerΔ
        │     │     UMQ CliΔ  = umq_client_recv - umq_client_post
        │     │     UMQ SrvΔ  = umq_server_rsp - umq_server_recv
        │     └── PrintProbeDetails(&probeData)  — 8 个时间戳明细
        └── if watch: sleep(1) → 重新查询
```

终端输出示例：

```
=== Probe Statistics ===
Total Probes (SocketNum): 1

FD       | UBS RTT(ns) | CliΔ(ns)   | SrvΔ(ns)   | UMQ RTT(ns)  | UMQ CliΔ(ns) | UMQ SrvΔ(ns)
---------------------------------------------------------------------------------------------------
549      | 90430.000   | 92370.000  | 1940.000   | 66800.000    | 78750.000    | 11950.000
  +-- [Client] ubsocket_client_send(ns): 721659634534780 | ubsocket_client_recv(ns): 721659634627150
  |            umq_post(ns): 721659634540400 | umq_recv(ns): 721659634619150
  +-- [Server] ubsocket_server_recv(ns): 721660422425858 | ubsocket_server_rsp(ns): 721660422427798
  |            umq_recv(ns): 721660422418348 | umq_rsp(ns): 721660422430298
```

#### 1.4.1 Probe 功能关闭时的查询结果

当 probe 功能未启动（环境变量 `UBSOCKET_PROBE_ENABLE=false` 且未动态开启）时，`mRecords` 为空（`Stop()` 时 `mRecords.clear()`）。`GetCLIProbeData` 遍历空 map，返回空列表。CLI 端收到的 `CLIProbeHeader.socketNum=0`，终端输出仅包含表头，无任何数据行：

```
=== Probe Statistics ===
Total Probes (SocketNum): 0

FD       | UBS RTT(ns) | CliΔ(ns)  | SrvΔ(ns)  | UMQ RTT(ns)  | UMQ CliΔ(ns) | UMQ SrvΔ(ns)
---------------------------------------------------------------------------------------------------
```

动态关闭 (`ubstat -p <pid> probe -t disable`) 后的查询结果与此相同 — `Stop()` 中的 `mRecords.clear()` 确保 probe 关闭后不返回旧数据。

---

## 二、核心数据结构

### 2.1 `ProbeType` 与 `ProbeUpdateMask`

```cpp
// probe_manager.h
enum ProbeType
{
    PROBE_TYPE_REQUEST = 1,  // 请求包 (Client → Server)
    PROBE_TYPE_RESPONSE = 2  // 响应包 (Server → Client)
};

enum ProbeUpdateMask
{
    MASK_NONE = 0x00,
    MASK_CLIENT_SEND = 0x01,
    MASK_CLIENT_RSP = 0x02,
    MASK_SERVER_RECV = 0x04,
    MASK_SERVER_RSP = 0x08,
    MASK_UMQ_CLIENT_POST = 0x10,
    MASK_UMQ_CLIENT_RECV = 0x20,
    MASK_UMQ_SERVER_RECV = 0x40,
    MASK_UMQ_SERVER_RSP = 0x80
};
```

### 2.2 `ProbeTimeInfo` — 探针包 Payload（随网络传输，80B）

```cpp
// probe_manager.h
struct __attribute__((packed)) ProbeTimeInfo {
    uint32_t type;   // 探测类型 (PROBE_TYPE_REQUEST / PROBE_TYPE_RESPONSE)
    uint32_t seq_id; // 序列号

    // --- Client Side Timestamps ---
    uint64_t client_send_time_ns;
    uint64_t client_recv_rsp_time_ns;
    uint64_t umq_client_post_time_ns;
    uint64_t umq_client_recv_time_ns;

    // --- Server Side Timestamps ---
    uint64_t server_recv_time_ns;
    uint64_t server_rsp_time_ns;
    uint64_t umq_server_recv_time_ns;
    uint64_t umq_server_rsp_time_ns;
};
```

此结构体嵌入在 `umq_buf_t->buf_data` 中，通过 `imm.user_data = PROBE_USER_DATA_ID (0xFFFFFF)` 标识为探针包。

**设计要点**：
- `__attribute__((packed))`：紧凑布局，无 padding
- `seq_id`：每次 `SendProbePacket` 时递增，用于匹配请求与响应（当前实现未严格校验 seq_id）
- 时间戳通过 `UpdateBuffer` 填充

### 2.3 `ProbeRecord` — 全局 map 中的记录

```cpp
// probe_manager.h
struct ProbeRecord {
    uint32_t mSockFd;
    ProbeTimeInfo mProbeInfo;  // 最近一次收到的完整探测报文
    uint64_t mLastRttNs;       // 最终计算出的 RTT
    bool mIsCompleted;         // 是否已收到完整响应

    ProbeRecord() : mSockFd(0), mLastRttNs(0), mIsCompleted(false)
    {
        memset(&mProbeInfo, 0, sizeof(ProbeTimeInfo));
    }
};
```

存储在 `ProbeManager` 的全局 `mRecords`（`std::unordered_map<uint32_t, ProbeRecord>`）中，以 fd 为键。所有访问通过 `mMutex` 保护。

### 2.4 `CLIProbeHeader` 与 `CLIProbeData` — CLI 传输结构

```cpp
// cli_message.h
struct __attribute__((packed)) CLIProbeHeader {
    uint32_t socketNum;
    uint32_t probeNum;
    uint32_t reserved;
};

struct __attribute__((packed)) CLIProbeData {
    int32_t fd;
    uint64_t client_send_time_ns;
    uint64_t client_recv_rsp_time_ns;
    uint64_t umq_client_post_time_ns;
    uint64_t umq_client_recv_time_ns;
    uint64_t server_recv_time_ns;
    uint64_t server_rsp_time_ns;
    uint64_t umq_server_recv_time_ns;
    uint64_t umq_server_rsp_time_ns;
    uint64_t rtt;  // Server 端计算出的 RTT
};
```

`CLIProbeData` 携带 8 个绝对时间戳和 `rtt` 字段。RTT 在 Server 端 `HandleReceivedPacket` 中计算（`client_recv_rsp_time_ns - client_send_time_ns`），CLI 端直接读取 `rtt` 字段展示。

### 2.5 `ProbeManager` — 单例管理器

```cpp
// probe_manager.h
class ProbeManager {
    std::thread mWorkerThread;
    std::atomic<bool> mRunning;
    uint32_t mIntervalMs;
    uint32_t mProbeBatch;
    std::atomic<uint32_t> mCurrentCursor;
    int mCoreId;

    std::unordered_map<uint32_t, ProbeRecord> mRecords;  // fd → record
    std::mutex mMutex;                                    // 保护 mRecords + mRecvQueue

    sem_t mSem;                                           // 唤醒工作线程处理 Server 回包
    ProbeRecord mRecvQueue[RPC_ADPT_FD_MAX];              // Server 回包队列（环形）
    uint32_t mQueueSt;
    uint32_t mQueueEd;

    // --- dump 相关 ---
    std::thread mDumpThread;                              // dump 工作线程
    std::atomic<bool> mDumpRunning;                       // dump 线程运行标志
    std::mutex mDumpStartMutex;                           // 保护 DumpStart/DumpStop
    std::string mDumpLastFilePath;                        // 上次写入路径（检测变更）
    bool mDumpDirCreated;                                 // 目录已创建标志
    std::string mDumpFileName;                            // 当前 dump 文件名
    std::ofstream mDumpFile;                              // dump 文件句柄
};
```

**设计特点**：
- 全局 `mRecords` map + `mMutex` 保护所有记录的读写
- Server 回包通过 `mRecvQueue` 环形队列 + `sem_t` 信号量唤醒工作线程处理
- `PeriodicProbe` 用 `sem_timedwait` 同时等待信号量（Server 回包）和超时（Client 探测）
- 独立 dump 线程随 `Start/Stop` 启停，周期复用 `mIntervalMs`，日志绕接规则与 PROF 一致（10MB + gzip + 保留 3 个归档）

---

## 三、工作原理

### 3.1 整体流程

```
┌─────────────────────────────────────────────────────────────────┐
│  ProbeManager::PeriodicProbe() (独立线程)                        │
│                                                                  │
│  while (mRunning) {                                              │
│      ret = sem_timedwait(&mSem, &timeout);                       │
│      if (!mRunning) break;                                       │
│      sentCount = 0;                                              │
│      if (ret == 0)                                               │
│          ProcessServerQueue(sentCount);  // Server 回包处理       │
│      else if (errno == ETIMEDOUT)                                │
│          ProcessClientProbing(sentCount); // Client 探测          │
│  }                                                               │
│                                                                  │
│  ProcessClientProbing():                                         │
│    └─ 遍历 ArraySet<Socket>, 找 IsClient() 的 socket             │
│        └─ SendProbePacket(sockObj)                               │
│            ├─ umq_buf_alloc                                      │
│            ├─ buf_pro->imm.user_data = PROBE_USER_DATA_ID        │
│            ├─ lock(mMutex); record = mRecords[fd];               │
│            ├─ UpdateBuffer(MASK_CLIENT_SEND)                     │
│            └─ umq_post() → 探针包进入 UMQ 发送队列                │
│                                                                  │
│  ProcessServerQueue():                                           │
│    └─ 从 mRecvQueue 取出 REQUEST 探针信息                         │
│        └─ SendResponsePacket(sockObj, &info)                     │
│            ├─ umq_buf_alloc + 拷贝 ProbeTimeInfo                  │
│            ├─ UpdateBuffer(MASK_SERVER_RSP)                      │
│            └─ umq_post()                                         │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│  ProbeManager::DumpLoop() (dump 线程, 随 Start/Stop 启停)        │
│                                                                  │
│  while (mDumpRunning) {                                          │
│      sleep(mIntervalMs, 10ms chunks);                            │
│      if (!mDumpRunning) break;                                   │
│      DumpData();                                                 │
│  }                                                               │
│  DumpData();  // final drain                                     │
│                                                                  │
│  DumpData():                                                     │
│    ├─ GetCLIProbeData(probeDataList)  // 加锁拷贝 mRecords 快照   │
│    ├─ WriteDumpTitle(oss, sockNum)    // 时间戳 + 列头            │
│    ├─ for each probeData:                                        │
│    │     WriteProbeRow(oss, &probeData)     // 概览行             │
│    │     WriteProbeDetails(oss, &probeData) // 8 个时间戳明细     │
│    └─ WriteDumpData(oss)               // 写文件 + 轮转           │
│        ├─ 读取 GlobalSetting::UBS_PROBE_DUMP_PATH (ProbeDumpMutex)│
│        ├─ 路径变更检测 → 关闭旧文件 + 重置状态                    │
│        ├─ CreateDirectory(path)                                  │
│        ├─ 打开 <path>/ubsocket_probe_<pid>.log (append)          │
│        ├─ 写入 oss.str() + flush                                 │
│        └─ RotateDumpFile()                                       │
│            ├─ stat 检查文件大小 < 10MB → 跳过                     │
│            ├─ 关闭文件 → gzip 压缩 → .gz 归档                     │
│            ├─ 按 mtime 排序, 删除最旧归档 (保留 3 个)             │
│            └─ 清空 mDumpFileName → 下次写新文件                   │
└─────────────────────────────────────────────────────────────────┘

UMQ 回调路径 (umq_io_perf_callback_register):
  UmqPerfCallback(record_type, qbuf):
    ├─ 检查 imm.user_data == PROBE_USER_DATA_ID? 否则跳过
    ├─ POST_SEND:
    │   ├─ REQUEST → UpdateBuffer(MASK_UMQ_CLIENT_POST)
    │   └─ RESPONSE → UpdateBuffer(MASK_UMQ_SERVER_RSP)
    └─ POLL_RX:
        ├─ REQUEST → UpdateBuffer(MASK_UMQ_SERVER_RECV)
        └─ RESPONSE → UpdateBuffer(MASK_UMQ_CLIENT_RECV)

Share-JFR RX 路径 (SiftSocketEventsWithUmqBuffers 内联处理):
  探针包在此直接处理, 不经过 AddQbuf → PollRx 路径
  ├─ 检查 opcode == UMQ_OPC_SEND_IMM && imm.user_data == PROBE_USER_DATA_ID
  ├─ ProbeManager::HandleReceivedPacket(OwnerFd(), buf)
  │   ├─ REQUEST:
  │   │   ├─ UpdateBuffer(MASK_SERVER_RECV)
  │   │   ├─ lock(mMutex); mRecvQueue[mQueueEd] = {fd, *probeInfo}; mQueueEd++
  │   │   └─ sem_post(&mSem)  — 唤醒工作线程处理回包
  │   └─ RESPONSE:
  │       ├─ UpdateBuffer(MASK_CLIENT_RSP)
  │       ├─ lock(mMutex); record = mRecords[fd]; record.mProbeInfo = *probeInfo
  │       ├─ record.mLastRttNs = recv_rsp - send
  │       └─ record.mIsCompleted = true
  └─ umq_buf_free(buf)

非 Share-JFR RX 路径 (UmqRxOps::PollRx):
  └─ 检查 opcode + imm.user_data, 调用 HandleReceivedPacket (同上逻辑)
```

### 3.2 Share-JFR 模式下的内联处理

Share-JFR 模式下，RX 缓冲区由 `SiftSocketEventsWithUmqBuffers` 从共享 JFR 的 `umq_poll` 批量获取。探针包**必须在此处内联处理**，原因有二：

1. **brpc 不驱动 PollRx**: 探针包不携带业务数据，不会触发 brpc 的 `readv` → `PollRx` 路径。如果仅 `AddQbuf` 到 per-socket 接收队列，探针包将永远驻留队列中无人消费

2. **bigdata 拦截风险**: `UMQ_PROBE_USER_DATA_ID = 0xFFFFFF` 的 `imm_data` 在某些情况下可能被 `is_big_ctrl()` 匹配（取决于 `rsvd0` 字段是否残留垃圾值），导致探针包被 bigdata 引擎错误拦截

因此在 `SiftSocketEventsWithUmqBuffers` 中，探针包检查位于 bigdata 检查**之前**：

```cpp
// umq_share_jfr_epoll_runner_ops.cpp — SiftSocketEventsWithUmqBuffers()
if (buf[i]->status == 0 && buf[i]->qbuf_ext != nullptr) {
    auto *rx_pro = reinterpret_cast<umq_buf_pro_t *>(buf[i]->qbuf_ext);
    if (rx_pro->opcode == UMQ_OPC_SEND_IMM &&
        rx_pro->imm.user_data == UmqSetting::UMQ_PROBE_USER_DATA_ID) {
        // 内联处理探针包, 不经过 AddQbuf
        ProbeManager::GetInstance().HandleReceivedPacket(OwnerFd(), buf[i]);
        umq_buf_free(buf[i]);
        continue;
    }
    // bigdata 检查在探针检查之后
    if (proto::is_big_ctrl(rx_pro->imm_data) && ...) { ... }
}
```

非 Share-JFR 模式下，`UmqRxOps::PollRx()` 中也有同样的探针包检查，在 `AddQbuf` 之前拦截并处理。

### 3.3 `imm.user_data` 与 `is_big_ctrl` 的正交性分析

`umq_buf_pro_t` 的 `imm` 联合体位域布局（aarch64 little-endian）：

```
union {
    uint64_t imm_data;              // 64-bit 整体视图
    struct {
        uint64_t rsvd0    : 40;     // bits 0-39  (低位)
        uint64_t user_data: 24;     // bits 40-63 (高位)
    } imm;
};
```

- `UBS_IMM_BIG_CTRL_BIT = 1ULL << 20` → 位于 **rsvd0** (bits 0-39)，`is_big_ctrl()` 检查此位
- `imm.user_data` → 位于 **bits 40-63**，与 bit 20 完全正交

各类报文的 `imm` 字段值：

| 报文类型 | `rsvd0` bit 20 | `user_data` (bits 40-63) | 说明 |
|----------|:--------------:|:------------------------:|------|
| Normal SMALL_DATA | 0 | SN (0 ~ 0xFFFFFE) | `FetchAddSeqNum` 分配 |
| Bigdata CTRL (OFFER) | 1 | SN (0 ~ 0xFFFFFE) | `mark_big_ctrl` + SN |
| Bigdata CTRL (DONE/ABORT) | 1 | 0 (无 SN) | `mark_big_ctrl` only |
| **Probe** | **0** | **0xFFFFFF** | `PROBE_USER_DATA_ID` |

**SN 不可能等于 0xFFFFFF**：SN 空间的 `MODULUS = 0xFFFFFF`，`Normalize(0xFFFFFF)` 回绕为 0。SN 取值范围是 `0..0xFFFFFE`，`0xFFFFFF` 是保留值。

**结论**：
- **bigdata 报文不可能被误识别为 probe** — probe 检查要求 `user_data == 0xFFFFFF`，而 bigdata 的 SN 永远 ≠ 0xFFFFFF
- **normal SMALL_DATA 不可能被误识别** — 同理
- **probe 检查在 bigdata 检查之前是安全的** — 两类报文的 `user_data` 值域不重叠，互不窃取

### 3.4 8 个时间戳的采集时机

| 时间戳 | 采集位置 | 掩码 | 说明 |
|--------|---------|------|------|
| `client_send_time_ns` | `SendProbePacket()` 中 `umq_post` 之前 | `MASK_CLIENT_SEND` | Client 应用层发起探测 |
| `umq_client_post_time_ns` | UMQ 回调 `POST_SEND` + REQUEST | `MASK_UMQ_CLIENT_POST` | UMQ 实际提交发送 |
| `umq_server_recv_time_ns` | UMQ 回调 `POLL_RX` + REQUEST | `MASK_UMQ_SERVER_RECV` | Server 端 UMQ 接收完成 |
| `server_recv_time_ns` | `HandleReceivedPacket()` + REQUEST | `MASK_SERVER_RECV` | Server 应用层处理接收 |
| `server_rsp_time_ns` | `SendResponsePacket()` 中 `umq_post` 之前 | `MASK_SERVER_RSP` | Server 应用层发起回复 |
| `umq_server_rsp_time_ns` | UMQ 回调 `POST_SEND` + RESPONSE | `MASK_UMQ_SERVER_RSP` | UMQ 实际提交回复 |
| `umq_client_recv_time_ns` | UMQ 回调 `POLL_RX` + RESPONSE | `MASK_UMQ_CLIENT_RECV` | Client 端 UMQ 接收完成 |
| `client_recv_rsp_time_ns` | `HandleReceivedPacket()` + RESPONSE | `MASK_CLIENT_RSP` | Client 应用层收到响应 |

### 3.5 8 段延迟分解

| 延迟段 | 计算公式 | 含义 |
|--------|---------|------|
| Client 应用 → UMQ | `umq_client_post - client_send` | Client 应用层到 UMQ 提交的开销 |
| UMQ 传输 (Client→Server) | `umq_server_recv - umq_client_post` | UMQ 网络传输延迟 |
| Server UMQ → 应用 | `server_recv - umq_server_recv` | Server 从 UMQ 收到到应用处理的开销 |
| Server 处理 | `server_rsp - server_recv` | Server 应用层处理 + 准备回复 |
| Server 应用 → UMQ | `umq_server_rsp - server_rsp` | Server 应用层到 UMQ 提交的开销 |
| UMQ 传输 (Server→Client) | `umq_client_recv - umq_server_rsp` | UMQ 网络传输延迟 |
| Client UMQ → 应用 | `client_recv_rsp - umq_client_recv` | Client 从 UMQ 收到到应用处理的开销 |
| **总 RTT** | `client_recv_rsp - client_send` | 端到端往返总延迟 |

### 3.6 `UpdateBuffer` 与时间源

```cpp
static void UpdateBuffer(ProbeTimeInfo *info, uint32_t mask)
{
    auto now_clock = std::chrono::system_clock::now();
    uint64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(now_clock.time_since_epoch()).count();

    if (mask & MASK_CLIENT_SEND)    info->client_send_time_ns = now;
    if (mask & MASK_CLIENT_RSP)     info->client_recv_rsp_time_ns = now;
    // ... 其余 6 个
}
```

当前使用 `std::chrono::system_clock`。后续可考虑替换为 `CLOCK_MONOTONIC` / `cntvct_el0`（通过 `ubsocket_get_timeNs()`），避免 `system_clock` 因 NTP 回跳导致 RTT 无效。

### 3.7 工作线程主循环

```cpp
void PeriodicProbe()
{
    struct timespec exceptTime;
    clock_gettime(CLOCK_REALTIME, &exceptTime);
    UpdateTimespec(&exceptTime, mIntervalMs);

    while (mRunning) {
        int ret = sem_timedwait(&mSem, &exceptTime);
        if (!mRunning) break;

        uint32_t sentCount = 0;
        if (ret == 0) {
            ProcessServerQueue(sentCount);       // 被唤醒：处理 Server 回包
        } else if (errno == ETIMEDOUT) {
            ProcessClientProbing(sentCount);     // 超时：Client 探测
            clock_gettime(CLOCK_REALTIME, &exceptTime);
            UpdateTimespec(&exceptTime, mIntervalMs);
        }
    }
}
```

`sem_timedwait` 同时承担两个职责：
- **信号量唤醒**：`HandleReceivedPacket` 收到 REQUEST 时 `sem_post`，工作线程处理 Server 回包（`ProcessServerQueue` → `SendResponsePacket`）
- **超时触发**：超时后执行 Client 探测（`ProcessClientProbing` → `SendProbePacket`）

`Stop()` 通过 `sem_post` 唤醒工作线程使其退出。

---

## 四、完整工作流程

### 4.1 Client 探测（定时触发）

```
ProbeManager 工作线程 (PeriodicProbe):
  sem_timedwait 超时
  → ProcessClientProbing(sentCount)
      → 遍历 ArraySet<Socket>, 轮询 IsClient() 的 socket
          → SendProbePacket(sockObj):
              ├── lock(mMutex); record = mRecords[fd]; record.mIsCompleted = false
              ├── umq_buf_alloc
              ├── buf_pro->imm.user_data = PROBE_USER_DATA_ID
              ├── probeInfo->type = PROBE_TYPE_REQUEST
              ├── UpdateBuffer(probeInfo, MASK_CLIENT_SEND)    (应用层发送)
              └── umq_post(buf)
                  ↓
              UmqPerfCallback(POST_SEND, REQUEST):
                  UpdateBuffer(probeInfo, MASK_UMQ_CLIENT_POST) (UMQ 提交)
```

### 4.2 Server 接收 + 入队 + 工作线程回包

```
Server UMQ 回调 (POLL_RX, REQUEST):
  UmqPerfCallback(POLL_RX, REQUEST):
      UpdateBuffer(probeInfo, MASK_UMQ_SERVER_RECV)  (Server UMQ 接收)

Server Share-JFR RX 路径 (SiftSocketEventsWithUmqBuffers):
  识别 opcode==SEND_IMM && imm.user_data==PROBE_USER_DATA_ID
  → HandleReceivedPacket(fd, buf):
      ├── probeInfo->type == PROBE_TYPE_REQUEST:
      │   ├── UpdateBuffer(probeInfo, MASK_SERVER_RECV)  (应用层接收)
      │   ├── lock(mMutex); mRecvQueue[mQueueEd] = {fd, *probeInfo}; mQueueEd++
      │   └── sem_post(&mSem)  — 唤醒工作线程
      └── umq_buf_free(buf)

工作线程被唤醒:
  ProcessServerQueue(sentCount):
      ├── 从 mRecvQueue 取出 {fd, probeInfo}
      ├── sockObj = ArraySet<Socket>::GetInstance().GetItem(fd)
      └── SendResponsePacket(sockObj, &probeInfo):
          ├── umq_buf_alloc + 拷贝 ProbeTimeInfo
          ├── rspProbeInfo->type = PROBE_TYPE_RESPONSE
          ├── UpdateBuffer(rspProbeInfo, MASK_SERVER_RSP)  (应用层回复)
          └── umq_post(buf)
              ↓
          UmqPerfCallback(POST_SEND, RESPONSE):
              UpdateBuffer(rspProbeInfo, MASK_UMQ_SERVER_RSP) (UMQ 提交)
```

### 4.3 Client 接收响应

```
Client UMQ 回调 (POLL_RX, RESPONSE):
  UmqPerfCallback(POLL_RX, RESPONSE):
      UpdateBuffer(probeInfo, MASK_UMQ_CLIENT_RECV)  (Client UMQ 接收)

Client Share-JFR RX 路径 (SiftSocketEventsWithUmqBuffers):
  识别 probe 包 → HandleReceivedPacket(fd, buf):
      ├── probeInfo->type == PROBE_TYPE_RESPONSE:
      │   ├── UpdateBuffer(probeInfo, MASK_CLIENT_RSP)  (应用层收到响应)
      │   ├── lock(mMutex); record = mRecords[fd]
      │   ├── rtt = client_recv_rsp - client_send
      │   ├── record.mLastRttNs = rtt
      │   ├── record.mIsCompleted = true
      │   └── record.mProbeInfo = *probeInfo
      └── umq_buf_free(buf)
```

### 4.4 CLI 查询

```
CLI 工具 → UDS → Listener::Process(PROBE)
  → GetAllProbeData():
      lock(mMutex);
      遍历 mRecords:
          item.fd = fd
          item.rtt = record.mLastRttNs
          item.*_time_ns = record.mProbeInfo.*_time_ns  (直接拷贝绝对时间戳)
  → 填充 CLIProbeHeader{socketNum}
  → 发送 CLIProbeHeader + CLIProbeData[]
  → CLI 端: DisplayProbeInfo → PrintProbeRow + PrintProbeDetails
```

---

## 五、数据存储结构

### 5.1 ProbeManager 内部结构

```
ProbeManager (单例)
├── mRecords: std::unordered_map<uint32_t, ProbeRecord>
│   └── [fd=549] → ProbeRecord
│       ├── mSockFd = 549
│       ├── mProbeInfo: ProbeTimeInfo  (最近一次收到的完整探测报文, 80B)
│       │   ├── type = 2 (RESPONSE)
│       │   ├── seq_id = 42
│       │   ├── client_send_time_ns = 721659634534780
│       │   ├── client_recv_rsp_time_ns = 721659634627150
│       │   └── ... (8 个时间戳)
│       ├── mLastRttNs = 92370
│       └── mIsCompleted = true
│
├── mRecvQueue[RPC_ADPT_FD_MAX]: ProbeRecord[]  (Server 回包环形队列)
├── mQueueSt, mQueueEd: uint32_t
├── mSem: sem_t  (唤醒工作线程)
└── mMutex: std::mutex  (保护 mRecords + mRecvQueue)
```

### 5.2 内存开销

| 组件 | 大小 | 说明 |
|------|------|------|
| `ProbeRecord` (per-socket) | ~96B | `mSockFd`(4) + `mProbeInfo`(80) + `mLastRttNs`(8) + `mIsCompleted`(1) + padding |
| `mRecvQueue` | ~768KB | `RPC_ADPT_FD_MAX * sizeof(ProbeRecord)` ≈ 8192 × 96B |
| `mRecords` map | 动态 | 每个 entry ~120B (key + value + hash node) |
| **合计（8192 sockets）** | **~1.7MB** | mRecvQueue 固定 + mRecords 动态 |

### 5.3 `ProbeManager` 自身内存

| 组件 | 大小 | 说明 |
|------|------|------|
| `mWorkerThread` | ~16B | `std::thread` |
| `mRunning` | 1B | `std::atomic<bool>` |
| `mIntervalMs` | 4B | `uint32_t` |
| `mProbeBatch` | 4B | `uint32_t` |
| `mCurrentCursor` | 4B | `std::atomic<uint32_t>` |
| `mCoreId` | 4B | `int` |
| `mMutex` | ~40B | `std::mutex` |
| `mSem` | ~32B | `sem_t` |
| `mRecvQueue` | ~768KB | 固定大小环形队列 |
| `mRecords` | 动态 | unordered_map |
| `mDumpThread` | ~16B | `std::thread` (dump 线程) |
| `mDumpRunning` | 1B | `std::atomic<bool>` |
| `mDumpStartMutex` | ~40B | `std::mutex` |
| `mDumpLastFilePath` | 动态 | `std::string` |
| `mDumpFileName` | 动态 | `std::string` |
| `mDumpFile` | ~256B | `std::ofstream` |

---

## 六、关键设计决策

1. **全局 map + mutex**: `mRecords` 以 fd 为键存储所有 socket 的探测记录，`mMutex` 保护三方访问（探测线程、RX 线程、CLI 线程）。简单直接，但高并发时存在锁争用

2. **Server 回包通过信号量**: `HandleReceivedPacket` 收到 REQUEST 时入 `mRecvQueue` + `sem_post`，工作线程被唤醒后处理回包。避免在 RX 线程直接 `umq_post` 回复

3. **Share-JFR 内联处理**: 探针包在 `SiftSocketEventsWithUmqBuffers` 中直接处理，不经过 `AddQbuf → PollRx` 路径。原因：brpc 不驱动 PollRx 消费探针包，且避免 bigdata `is_big_ctrl` 拦截

4. **`sem_timedwait` 双职责**: 工作线程同时等待信号量（Server 回包）和超时（Client 探测），无需两个定时器

5. **RTT 在 Server 端计算**: `HandleReceivedPacket` 收到 RESPONSE 时计算 `mLastRttNs = client_recv_rsp - client_send`，CLI 端直接读取 `rtt` 字段

6. **`imm.user_data` 值域隔离**: `PROBE_USER_DATA_ID = 0xFFFFFF` 是 SN 空间的保留值（`MODULUS`，`Normalize` 回绕为 0），normal/bigdata 报文的 SN 永远 ≠ 0xFFFFFF，因此 probe 检查不会误识别业务报文

7. **时间源**: 当前使用 `std::chrono::system_clock`。后续可替换为 `CLOCK_MONOTONIC` / `cntvct_el0` 避免 NTP 回跳

8. **后台自动 dump**: dump 线程随 `ProbeManager::Start()` 启动、随 `Stop()` 停止，dump 周期复用 `mIntervalMs`。仅在 Client 端产生 dump 文件（`DumpData()` 在无探测记录时提前返回，Server 端 `mRecords` 为空）。日志绕接规则与 PROF 统计聚合层一致（10MB 轮转 + gzip 压缩 + 保留 3 个归档）。dump 路径可通过环境变量 `UBSOCKET_PROBE_DUMP_FILE_PATH` 静态配置或 CLI `-t dumppath -v <path>` 动态修改，dump 线程在下一个周期自动切换

9. **dump 停止顺序**: `Stop()` 中先 `DumpStop()` 再 join 工作线程，确保 dump 线程不会在 `mRecords.clear()` 之后仍尝试加锁访问

---

## 七、涉及文件

| 文件 | 角色 |
|------|------|
| `profiling/probe/probe_manager.h` | `ProbeManager` 单例: 数据结构定义 + `Start/Stop/SendProbePacket/SendResponsePacket/HandleReceivedPacket/GetCLIProbeData/UmqPerfCallback/PeriodicProbe/ProcessServerQueue/ProcessClientProbing` + dump 线程 (`DumpStart/DumpStop/DumpLoop/DumpData/WriteDumpData/RotateDumpFile`) |
| `core/umq/umq_setting.h` | `UMQ_PROBE_USER_DATA_ID = 0xFFFFFF` 常量定义 |
| `core/umq/umq_share_jfr_epoll_runner_ops.cpp` | Share-JFR RX 路径: 探针包内联处理 (bigdata 检查之前) |
| `core/umq/umq_data_rx_ops.cpp` | 非 Share-JFR RX 路径: `PollRx` 中探针包检查 |
| `profiling/statistics/cli_message.h` | `CLIProbeHeader`, `CLIProbeData` (含 `rtt` 字段), `CLITypeParam` (含 `PROBE_OP_QUERY/ENABLE/DISABLE/SET_DUMP_PATH`) |
| `profiling/statistics/statistics.h` | `ProcessProbeRequest`: 填充 `CLIProbeHeader` + 发送 CLI 响应; `ProcessProbeControl`: 动态开启/关闭 probe; `ProcessProbeDumpPath`: 动态设置 dump 路径 |
| `cli/cli_client.h` / `cli/cli_client.cpp` | `ProcessProbe`: 发送 probe query/enable/disable/dumppath 请求并接收响应 |
| `cli/cli_main.cpp` | probe 命令分发: query → `DisplayProbeInfo`, enable/disable → 打印结果, dumppath → 打印结果 |
| `cli/cli_args_parser.cpp` | probe 命令帮助文本与 `-t` / `-v` 参数说明 |
| `cli/cli_terminal_display.cpp` | `PrintProbeRow/PrintProbeDetails`: 计算 UBS RTT / UMQ RTT 并展示 |
| `ubsocket.cpp` | `ubsocket_init`: `ProbeManager::Start()` (内含 `DumpStart()`); `ubsocket_uninit`: `ProbeManager::Stop()` (内含 `DumpStop()`) |
| `common/ubsocket_global_setting.cpp` | 环境变量读取: `UBSOCKET_PROBE_ENABLE/INTERVAL_MS/BATCH_SIZE/DUMP_FILE_PATH` |

---

## 八、注意事项

1. **`mMutex` 保护范围**: `mMutex` 保护 `mRecords`（读写）和 `mRecvQueue`（入队/出队）。所有路径（`SendProbePacket`、`HandleReceivedPacket`、`GetCLIProbeData`、`ProcessServerQueue`）访问前均需持锁

2. **`mRecvQueue` 环形队列溢出**: `mRecvQueue` 大小为 `RPC_ADPT_FD_MAX`（8192）。当 Server 收到探针包的速度超过工作线程处理速度时，队列会回绕覆盖旧条目。正常情况下 probe 频率低（30 秒一次），不会溢出

3. **`sem_timedwait` 与 `Stop()` 的竞争**: `Stop()` 先 `mRunning.exchange(false)` 再 `sem_post(&mSem)`。工作线程被唤醒后检查 `mRunning` 为 false 即退出。`sem_timedwait` 超时时也检查 `mRunning`，保证不会在 `Stop()` 后继续探测

4. **Share-JFR 内联处理必须早于 bigdata 检查**: `UMQ_PROBE_USER_DATA_ID = 0xFFFFFF` 的 bit 20 位于 `imm.rsvd0`（bits 0-39），与 `imm.user_data`（bits 40-63）正交。但如果 probe 代码未显式清零 `rsvd0`（`umq_buf_alloc` 可能残留垃圾值），`is_big_ctrl()` 可能返回 true。将 probe 检查放在 bigdata 之前可完全避免此问题

5. **`PROBE_USER_DATA_ID` 与 SN 空间**: `0xFFFFFF` 是 SN 空间的 `MODULUS` 值，`Normalize(0xFFFFFF) = 0`。业务报文的 SN 取值范围是 `0..0xFFFFFE`，永远不会等于 `0xFFFFFF`，因此 probe 检查不会误识别业务报文

6. **CLI PID 检测**: 当目标进程通过 `numactl` 启动时，`numactl` fork 出的子进程 PID 与父进程 PID 相差 1。CLI 工具需通过 `/proc/net/unix` 查找 `ubscli-<pid>` UDS socket 来获取正确的子进程 PID

8. **后台 dump 线程停止顺序**: `Stop()` 中 `DumpStop()` 必须在工作线程 `join` 之前执行。dump 线程在 `DumpData()` 中调用 `GetCLIProbeData()` 获取数据快照（持有 `mMutex`），若工作线程先 join 完毕再停 dump，dump 线程可能在 `mRecords` 被清空后仍尝试访问（虽然 `GetCLIProbeData` 对空 map 是安全的，但 `Stop()` 中的 `mRecords.clear()` 持有 `mMutex`，与 dump 线程的 `GetCLIProbeData` 竞争锁，可能导致 `Stop()` 中的 `lock_guard` 阻塞等待 dump 线程释放锁）。先停 dump 线程避免此竞争

9. **dump 路径热替换**: dump 线程在每次 `WriteDumpData()` 中通过 `ProbeDumpMutex` 读取 `UBS_PROBE_DUMP_PATH`，与 CLI 的 `ProcessProbeDumpPath` 写入端互斥。路径变更后，dump 线程关闭当前文件、重置目录创建标志，在新路径下重新创建目录并打开文件，无需重启线程

10. **dump 线程与 `mIntervalMs`**: dump 线程的睡眠周期直接使用 `mIntervalMs`（`Start()` 时从 `GlobalSetting::UBS_PROBE_MS` 传入），不会在运行时动态读取 `GlobalSetting::UBS_PROBE_MS`。若需修改 dump 周期，需要重新 `Stop()` + `Start()` probe
