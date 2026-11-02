# UBSocket 统计值体系设计分析 — 跨节点问题定位视角

## 1. 整体架构

```
Node A (进程)                              Node B (进程)
+------------------------------------+     +------------------------------------+
| CLI 诊断工具 (外部进程)              |     | CLI 诊断工具 (外部进程)              |
| ubstat <pid> stat|txstat|rxstat|   |     | ubstat <pid> stat|txstat|rxstat|   |
|             qbufstats              |     |             qbufstats              |
+--+---------------------------------+     +--+---------------------------------+
   | UDS (ubstat-<pid>)                      | UDS (ubstat-<pid>)
   v                                         v
+------------------------------------+     +------------------------------------+
| GlobalStatsMgr (进程内 Listener)    |     | GlobalStatsMgr (进程内 Listener)    |
| epoll 事件循环线程, CLI 命令响应    |     | epoll 事件循环线程, CLI 命令响应    |
+------------------------------------+     +------------------------------------+
| Layer 1: StatsMgr       — 流量计数 |     | Layer 1: StatsMgr       — 流量计数 |
| Layer 2: TxStatReporter — TX 定界  |     | Layer 2: TxStatReporter — TX 定界  |
| Layer 3: RxStatReporter — RX 定界  |     | Layer 3: RxStatReporter — RX 定界  |
| Layer 4: QbufPoolStats  — 池水位   |     | Layer 4: QbufPoolStats  — 池水位   |
+------------------------------------+     +------------------------------------+
        ^                                         ^
        | 跨节点 UB / RDMA 链路                    |
        +-----------------------------------------+
```

跨节点问题定位的核心思路: 两端各看 `send` vs `recv`、对比 Probe RTT 分解、检查 TX/RX 错误桶在每条链路上的分布、观察两端 qbuf 池水位随时间的变化，定位问题在哪一端、哪条链路。

### 统计开关

四种统计功能默认**开启**。前三种 (StatsMgr / TxStatReporter / RxStatReporter) 为 per-socket 统计, 在 socket 创建时分配 per-socket 计数器; 第四种 (QbufPoolStats) 为全局池级统计, 无 per-socket 计数器, 由后台线程周期取数。通过环境变量 `UBSOCKET_MONITOR_ENABLE=false` 可关闭, 关闭后 per-socket 计数器为 `nullptr`, 热路径宏通过 `nullptr` 判断短路返回 (no-op), 后台线程不启动。CLI 命令在关闭时返回空结果。

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| `UBSOCKET_MONITOR_ENABLE` | 统计总开关, `true` 开启, `false` 关闭 | 开启 |

> 关闭后, 已分配的计数器不释放 (避免热路径加锁), 仅停止计数。再次开启后, 既有 socket 的计数器在下次 CLI 查询时按需补分配; 新建 socket 在构造时分配。

### 1.1 StatsMgr — 基础流量计数

StatsMgr 提供进程级和链路级两个层面的流量计数。全局指标反映进程整体连接与收发概况, per-socket 指标反映每条链路的收发明细, 两端对比可定位丢包方向和链路质量问题。

#### 1.1.1 CLI 命令行展示

```bash
ubstat stat -p <pid>        # 单次查询
ubstat stat -p <pid> -w     # 持续刷新 (每秒)
```

**全局指标** (输出在表格头部):

| 指标 | 说明 |
|------|------|
| Total Sockets | UMQ socket 数量 (排除纯 TCP / listen fd) |
| Connect Calls | 进程生命周期内累计连接数 (含 accept + connect) |
| Active Conns | 其中 client 主动发起的连接数 |
| ReTx Count | UMQ 底层重传计数, 反映跨节点链路质量 |
| Pool Total Capacity | UMQ transport qbuf 池总容量 |
| Pool Available Count | qbuf 池可用数 |
| Pool In Use Count | qbuf 池在用数 |

**per-socket 指标** (每行一条链路):

| 指标 | 说明 |
|------|------|
| Recv Packets | 该 socket 累计收包数 |
| Send Packets | 该 socket 累计发包数 |
| Recv Bytes | 该 socket 累计收字节数 (人类可读: B/K/M/G/T) |
| Send Bytes | 该 socket 累计发字节数 (人类可读) |
| Bigdata CtrlRcv | 该 socket 累计收到的 bigdata ctrl 报文数 (READ_OFFER + READ_DONE + READ_ABORT) |
| Bigdata Read | 该 socket 基于 READ_OFFER 成功发起的 RDMA READ 次数 |
| Bigdata CtrlSnd | 该 socket 累计发送的 bigdata ctrl 报文数 (READ_OFFER + READ_DONE + READ_ABORT) |

> 此外, per-socket 输出中还附带 socket 标识信息 (fd、建链时间、对端 IP、本端/对端 EID), 这些不是统计指标, 仅用于标识和区分链路。

全局指标输出:

```
CLI STATISTICS MONITOR

Total Sockets       : 3
Connect Calls       : 100
Active Conns        : 50
ReTx Count          : 3
Pool Total Capacity : 1024
Pool Available Count: 800
Pool In Use Count   : 224
```

per-socket 指标输出 (每行一条链路, 前几列为标识信息, 后面为统计指标):

```
SocketFd | Creation Time        | Remote Ip          | Local Eid                          | Remote Eid                         | Recv Packets | Send Packets | Recv Bytes | Send Bytes | Error Packets | Lost Packets | Bigdata CtrlRcv | Bigdata Read | Bigdata CtrlSnd
5        | 2026-08-19 10:00:00  | fe80::1234:5678    | 0000:0000:0000:0000:0000:0000:0001 | 0000:0000:0000:0000:0000:0000:0002 | 500000       | 500002       | 52.42M     | 52.42M     | 0             | 0            | 128             | 126          | 127
8        | 2026-08-19 10:01:30  | fe80::abcd:ef01    | 0000:0000:0000:0000:0000:0000:0003 | 0000:0000:0000:0000:0000:0000:0004 | 0            | 0            | 0.00B      | 0.00B      | 0             | 0            | 0            | 0            | 0
12       | 2026-08-19 10:02:00  | fe80::5678:9abc    | 0000:0000:0000:0000:0000:0000:0005 | 0000:0000:0000:0000:0000:0000:0006 | 1            | 1            | 1.02K      | 1.02K      | 0             | 0            | 0            | 0            | 0
```

> 值为 0 的列显示为灰色; 收发包绿色, 字节数黄色, bigdata ctrlRcv/read/ctrlSnd 青色。

### 1.2 TxStatReporter — TX 方向流控定界统计

TxStatReporter 专门定位发送方向的失败原因, 将失败分为**提交侧** (umq_post 失败) 和**完成侧** (TX CQE 异常) 两类, 共 15 个错误桶。所有指标均为 per-socket, 可通过 CLI 按链路查询。

#### 1.2.1 提交侧指标 (umq_post 失败, 9 桶)

| 指标 | 说明 |
|------|------|
| post.eagain_all | 流控 credit 不足, 全部失败 — 对端未及时回复 credit |
| post.eagain_part | 流控 credit 不足, 部分失败 — 对端 credit 跟不上 |
| post.enobufs_all | qbuf 池耗尽, 全部失败 — 本端问题 |
| post.enobufs_part | qbuf 池耗尽, 部分失败 — 本端问题 |
| post.emlink | 无可用 jetty, 已入等待队列 — 本端连接问题 |
| post.timeout | 等对端授信回复超时 (默认 1s) — 对端无响应 |
| post.eflowctl | 流控失败 (FATAL/EAGAIN) — 跨节点流控 |
| post.no_badqbuf | umq_post 返回错误但无 bad_qbuf — 本端异常分支 |
| post.other | 其他错误 |

#### 1.2.2 完成侧指标 (TX CQE 异常, 6 桶)

| 指标 | 说明 |
|------|------|
| cqe.rnr | 对端 RQ 不足, 接收能力不够 |
| cqe.ack_timeout | 对端未回 ACK — 跨节点网络丢包或对端宕机 |
| cqe.fc | 流控失败 |
| cqe.remote | 对端协议错误 (resp_len / unsupported / operation / access_abort) |
| cqe.local | 本端错误 (len / operation / access) |
| cqe.other | 其他完成异常 (unsupported opcode / flush / suspend / poison) |

#### 1.2.3 命令行展示

```bash
ubstat txstat -p <pid>        # 单次查询
ubstat txstat -p <pid> -w     # 持续刷新 (每秒)
```

per-socket 指标输出 (每行一条链路, 前几列为标识信息, 后面为统计指标):

```
CLI TX STATISTICS MONITOR

SocketFd | eagain_all | eagain_part | enobufs_all | enobufs_part | emlink | timeout | eflowctl | no_badqbuf | other | cqe_rnr | cqe_ack_timeout | cqe_fc | cqe_remote | cqe_local | cqe_other
5        | 0          | 0           | 0           | 0            | 0      | 0       | 0        | 0          | 0     | 0       | 1               | 0      | 0          | 0         | 0
8        | 0          | 0           | 0           | 0            | 0      | 0       | 0        | 0          | 0     | 0       | 0               | 0      | 0          | 0         | 0
12       | 3          | 1           | 0           | 0            | 0      | 1       | 0        | 0          | 0     | 0       | 0               | 0      | 0          | 0         | 0
```

> 值为 0 的列显示为灰色; 错误桶非 0 时显示红色。

### 1.3 RxStatReporter — RX 方向流控定界统计

RxStatReporter 定位接收方向的失败原因。接收流程分为 Poll (从 UMQ 拉取数据)、CQE 处理 (处理 buf 异常状态)、数据提取 (从 block cache 取数据) 三个阶段, 各阶段均可产生失败。所有指标均为 per-socket, 可通过 CLI 按链路查询。

#### 1.3.1 接收流程与失败点

```
应用层 readv()
  |
  |-- Poll 阶段 -- PollRx()
  |     |-- GetAndAckEvent() -- 获取并确认 CQ 事件           [可失败: get_event_fail]
  |     |-- GetQbuf() -> umq_poll() -- 从 UMQ 拉取 buf        [可失败: poll_fail]
  |     |     +-- UmqPollAndRefillRx()
  |     |           |-- umq_buf_alloc() -- 分配 refill buf    [可失败: refill_alloc_fail]
  |     |           +-- umq_post() -- 回填 RX 缓冲            [可失败: refill_post_fail]
  |     +-- 遍历 buf[], 按 status 分发:
  |           |-- status == 0 -> 正常数据
  |           |-- status >= UMQ_FAKE_BUF_FC_UPDATE -> 流控消息 [见 CQE 指标]
  |           +-- status != 0 (其他错误)                      [见 CQE 指标]
  |
  |-- 数据提取阶段 -- RxDataSet()
  |     |-- DataToBlock() -> nullptr -- 定位 block 失败        [可失败: dataset_no_block]
  |     |-- CutAndInsertAfter() == 0 -- 无数据可取
  |     |     |-- flow_control_failed_ 置位 -> EIO            [可失败: flow_ctrl_failed]
  |     |     |-- RearmRxInterrupt() 失败 -> EIO              [可失败: rearm_fail]
  |     |     +-- recv(MSG_PEEK) == 0 -> 对端 TCP 关闭        [可失败: peer_closed]
  |     +-- 正常返回
  |
  +-- 重武装阶段 -- RearmRxInterrupt() (非 POOL 模式)
        +-- umq_rearm_interrupt() -- 重新注册 RX 中断          [可失败: rearm_fail]
```

#### 1.3.2 Poll 阶段指标 (umq_poll / refill 失败, 5 桶)

| 指标 | 说明 |
|------|------|
| rx.get_event_fail | `umq_get_cq_event` 失败 — 本端 CQ 事件获取异常 |
| rx.poll_fail | `umq_poll` 返回负值 — 本端从 UMQ 拉取数据失败 |
| rx.refill_alloc_fail | `umq_buf_alloc` 返回 nullptr — refill 缓冲分配失败, 本端内存不足 |
| rx.refill_post_fail | `umq_post` (refill) 失败 — 回填 RX 缓冲到 UMQ 失败, 本端或对端问题 |
| rx.qbuf_pop_fail | 共享 JFR 模式下 `GetAndPopQbuf` 失败 — 本端 RX 队列异常 |

#### 1.3.3 CQE 处理阶段指标 (RX buf status 异常, 6 桶)

| 指标 | 说明 |
|------|------|
| rxe.fc | 流控错误 (`UMQ_FAKE_BUF_FC_ERR` / `FC_ERR_FATAL` / `FC_UPDATE`) — 跨节点流控失败或对端流控回复, 触发异步关闭 |
| rxe.remote | 对端错误 (`REM_RESP_LEN` / `UNSUPPORTED_REQ` / `OPERATION` / `ACCESS_ABORT`) — 对端协议错误 |
| rxe.local | 本端错误 (`LOC_LEN` / `OPERATION` / `ACCESS`) — 本端接收异常 |
| rxe.ack_timeout | 对端未回 ACK (`UMQ_BUF_ACK_TIMEOUT_ERR`) — 跨节点网络丢包或对端宕机 |
| rxe.rnr | 对端 RQ 不足 (`UMQ_BUF_RNR_RETRY_CNT_EXC_ERR`) — 对端接收能力不够 |
| rxe.other | 其他 (`UNSUPPORTED_OPCODE` / `FLUSH` / `SUSPEND` / `POISON` / 未知 status) |

#### 1.3.4 数据提取阶段指标 (RxDataSet 失败, 4 桶)

| 指标 | 说明 |
|------|------|
| rx.dataset_no_block | `DataToBlock` 返回 nullptr — buf_data 定位 block 失败, 本端异常 |
| rx.flow_ctrl_failed | `flow_control_failed_` 标记置位 — 前序 CQE 流控错误导致的 EIO |
| rx.rearm_fail | `RearmRxInterrupt` 失败 — `umq_rearm_interrupt` 返回负值, 本端中断注册异常 |
| rx.peer_closed | `recv(MSG_PEEK)` 返回 0 — 对端 TCP 连接已关闭, UB 链路无数据 |

#### 1.3.5 命令行展示

```bash
ubstat rxstat -p <pid>        # 单次查询
ubstat rxstat -p <pid> -w     # 持续刷新 (每秒)
```

per-socket 指标输出 (每行一条链路, 前几列为标识信息, 后面为统计指标):

```
CLI RX STATISTICS MONITOR

SocketFd | get_event_fail | poll_fail | refill_alloc_fail | refill_post_fail | qbuf_pop_fail | rxe_fc | rxe_remote | rxe_local | rxe_ack_timeout | rxe_rnr | rxe_other | dataset_no_block | flow_ctrl_failed | rearm_fail | peer_closed
5        | 0              | 0         | 0                 | 0                | 0             | 1      | 0          | 0         | 0               | 0       | 0         | 0                | 0                | 0          | 1
8        | 0              | 0         | 0                 | 0                | 0             | 0      | 0          | 0         | 0               | 0       | 0         | 0                | 0                | 0          | 0
12       | 0              | 0         | 0                 | 0                | 0             | 0      | 2          | 0         | 1               | 0       | 0         | 0                | 0                | 0          | 0
```

> 值为 0 的列显示为灰色; 错误桶非 0 时显示红色。

### 1.4 QbufPoolStats — UMQ qbuf 池统计

QbufPoolStats 定位共享内存池的水位与泄漏类问题, 观察池水位随时间的变化、定位池耗尽 (`enobufs`) 与内存泄漏。与前三种 per-socket 统计不同, qbuf 池的**统计工作全部在 UMQ 侧完成** (`umq_qbuf_pool.c` / `umq_tiny_qbuf_pool.c`), ubsocket 统计侧只负责**取数、记录**: 周期性落盘 (30s) + 响应 CLI 单次查询, 不做任何统计计算。

#### 1.4.1 统计项定义 (数据来源)

直接调用 UMQ 池级 DFX 接口 (公共声明在 `umq_dfx_api.h`, 实现在 UMQ 库内):

| 接口 | 采集内容 | 池未初始化时行为 |
|------|---------|----------------|
| `umq_qbuf_pool_info_get` | normal 池 (`UMQ_QBUF_POOL_TYPE_SMALL`): 容量/水位/分配计数 + per-SC (size-class) 明细 + 池配置 | 返回错误 (本轮跳过) |
| `umq_tiny_qbuf_pool_info_get` | tiny 池 (`UMQ_QBUF_POOL_TYPE_TINY`): 同上 | 返回成功, 不填数据 |
| `umq_qbuf_pool_stats_to_str` | 将上述结构格式化为多级文本 (池配置 + per-SC + per-thread TLS 分解) | - |

> 只采集 **normal + tiny** 两池, 不含 huge 池; 也不走 `umq_stats_qbuf_pool_get(UMQ_INVALID_HANDLE)` 全局聚合封装 (该封装额外包含 huge 池)。两池条目追加填充在 `poolStats.qbuf_pool_info[]` 数组中 (`num` 计数)。

#### 1.4.2 命令行展示

```bash
ubstat qbufstats -p <pid>        # 单次查询
```

> 仅支持单次查询, **不支持 `-w` 持续刷新** — 单次输出的数据量较大 (含 per-SC / per-thread TLS 全量分解), 每秒刷新代价大; 时间序列观察走 30s 周期落盘 `ubsocket_qbuf.txt` (见 §1.5.4)。

终端输出 (与 30s 落盘 `ubsocket_qbuf.txt` 同一格式化输出, 仅正常 CLI 输出含 ANSI 颜色码):

```
CLI GLOBAL QBUF POOL (NORMAL + TINY) STATISTICS

<umq_qbuf_pool_stats_to_str 全量输出: 池配置 + Per-SizeClass 状态 + Per-Thread TLS 缓存分解>
```

> `retCode != 0` 时 (normal 池未初始化, 进程尚无 UMQ 活动) 红色提示 `Failed to get qbuf pool info, retCode: <code>`。

与既有 `QBUF_POOL` 命令的区别:

| 命令 | 枚举值 | 语义 | 数据来源 |
|------|--------|------|---------|
| `QBUF_POOL` | 5 | per-socket, 按各 socket 的 umq handle 查 transport 层池 | `umq_stats_qbuf_pool_get(umqh)` → transport ops |
| `QBUF_POOL_STATS` | 13 | 全局, normal + tiny 共享池 | `umq_qbuf_pool_info_get` + `umq_tiny_qbuf_pool_info_get` (直接调用) |

> 详细实现 (取数方式 / CLI 响应结构 / 周期落盘 / 诊断场景) 见 §2.4。

### 1.5 后台自动导出功能

后台自动导出功能由 `PrintStatsMgr` 单线程驱动, 在 `ubsocket_init()` 时启动, 每个周期同时输出两类文件: KPI JSON (进程级流量指标) 和统计快照 (前三种统计的完整 per-socket 明细)。此外, 同一线程还以**固定 30 秒**的独立节拍输出第四种统计 — qbuf 池统计快照 (详见 §1.5.4)。

#### 1.5.1 自动导出功能的开关控制

后台导出功能与统计开关共用同一个开关 `UBSOCKET_MONITOR_ENABLE`。开关关闭时, 后台线程不启动, KPI JSON、统计快照和 qbuf 池统计均不落盘。

| 配置项 | 环境变量 | C++ 成员 | 默认值 | 说明 |
|--------|---------|---------|--------|------|
| 统计开关 | `UBSOCKET_MONITOR_ENABLE` | `UBS_MONITOR_ENABLE` | true | 是否启用后台导出及 per-socket 统计 |
| 导出周期 | `UBSOCKET_MONITOR_INTERVAL` | `UBS_MONITOR_INTERVAL` | 10 (秒) | 后台线程每轮间隔 |
| 导出路径 | `UBSOCKET_MONITOR_FILE_PATH` | `UBS_MONITOR_FILE_PATH` | `/tmp/ubsocket/log` | 导出文件目录 |
| 磁盘总上限 | `UBSOCKET_MONITOR_FILE_SIZE` | `UBS_MONITOR_FILE_SIZE_MB` | 40 (MB) | 每类文件的总磁盘占用上限 |

> 每类文件 (KPI JSON / 统计快照 / qbuf 池统计) 独立轮转, 各自最多 2 个文件 (1 活动 + 1 归档), 单文件阈值 = `UBSOCKET_MONITOR_FILE_SIZE / 2` (默认 20MB)。三类文件合计磁盘占用上限 = 3 × `UBSOCKET_MONITOR_FILE_SIZE` (默认 120MB)。

#### 1.5.2 KPI JSON 落盘

> **对外接口约束**: KPI JSON 的文件格式、字段名、字段顺序、JSON 结构以及默认导出路径 (`/tmp/ubsocket/log/ubsocket_kpi.json`) 是对外约定的稳定接口, 下游工具 (如监控采集、日志分析) 依赖此格式解析。**不得随意变动** — 如需新增字段, 只能追加到 `trafficRecords` 对象末尾, 不得修改或删除已有字段名、不得改变字段顺序、不得改变文件命名和默认路径。

`UBSOCKET_MONITOR_ENABLE=true` (默认) 时, `PrintStatsMgr` 后台线程以 `UBSOCKET_MONITOR_INTERVAL` (默认 10 秒) 为周期, 将进程级流量指标以 JSON 格式自动落盘。

每行一个完整 JSON 对象, 追加写入活动文件:

```json
{"timeStamp":"2026-08-19 10:30:00","pid":"12345","trafficRecords":{"totalConnections":100,"activeConnections":50,"reTxCount":3,"sendPackets":500000,"receivePackets":499998,"sendBytes":52420000,"receiveBytes":52399000,"errorPackets":0,"lostPackets":0}}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| timeStamp | string | 落盘时刻 (`%Y-%m-%d %H:%M:%S` 格式) |
| pid | string | 进程 PID |
| totalConnections | uint32 | 进程累计连接数 (全局 atomic) |
| activeConnections | uint32 | 主动发起连接数 (全局 atomic) |
| reTxCount | uint32 | UMQ 底层重传计数 (后台线程从 UMQ tp perf 查询) |
| sendPackets | uint64 | 进程累计发包数 (Σ per-socket Recorder 聚合) |
| receivePackets | uint64 | 进程累计收包数 (Σ per-socket Recorder 聚合) |
| sendBytes | uint64 | 进程累计发字节数 (Σ per-socket Recorder 聚合) |
| receiveBytes | uint64 | 进程累计收字节数 (Σ per-socket Recorder 聚合) |
| errorPackets | uint64 | 发送错误包数 (Σ per-socket Recorder 聚合) |
| lostPackets | uint64 | 丢失包数 (Σ per-socket Recorder 聚合) |

> 收发数据通过 `AggregatePerSocketStats` 遍历 `ArraySet<Socket>` 聚合 per-socket Recorder 获得 (非全局 atomic), 聚合在后台线程执行, 不影响热路径。详细实现见 §2.1.3。

**文件命名与轮转** — 覆盖式轮转, 最多 2 个文件:

- 活动文件: `ubsocket_kpi.json` (固定名, 追加写)
- 归档文件: `ubsocket_kpi_<pid>.1.json` (轮转时 rename 生成, 只读 0440)
- 单文件阈值: `UBSOCKET_MONITOR_FILE_SIZE / 2` (默认 20MB), 超过即 rename 归档
- 磁盘总占用恒 ≤ `UBSOCKET_MONITOR_FILE_SIZE` (峰值 ≤ 2 × 单文件阈值)

> 详细轮转逻辑见 §2.1.3。

#### 1.5.3 统计快照落盘 (前三种统计)

每个导出周期, 将前三种统计 (StatsMgr / TxStatReporter / RxStatReporter) 的完整快照输出到**同一个文件**中。文件格式与 CLI 命令行输出一致 (纯文本表格, 无 ANSI 颜色码), 但每个周期的输出以时间戳分隔行开头 (格式 `==================== <timeStamp> ====================`, 便于在连续追加的文件中区分不同周期)。第四种统计 (QbufPoolStats) 为全局池级数据, 单独落盘, 见 §1.5.4。

**文件命名与轮转** — 与 KPI JSON 相同的覆盖式轮转策略:

- 活动文件: `ubsocket_stats.txt` (固定名, 追加写)
- 归档文件: `ubsocket_stats_<pid>.1.txt` (轮转时 rename 生成, 只读 0440)
- 单文件阈值: `UBSOCKET_MONITOR_FILE_SIZE / 2` (默认 20MB), 超过即 rename 归档

**文件内容示例** (每个周期输出如下结构, 追加到活动文件):

```
==================== 2026-08-19 10:30:00 ====================
CLI STATISTICS MONITOR
Total Sockets       : 3
Connect Calls       : 100
Active Conns        : 50
ReTx Count          : 3
Pool Total Capacity : 1024
Pool Available Count: 800
Pool In Use Count   : 224

 SocketFd  | Creation Time       | Remote Ip         | Local Eid                                        | Remote Eid                                       | Recv Packets | Send Packets | Recv Bytes | Send Bytes | Error Packets | Lost Packets | Bigdata CtrlRcv | Bigdata Read | Bigdata CtrlSnd
 5         | 2026-08-19 10:00:00 | fe80::1234:5678   | 00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:01  | 00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:02  | 500000       | 500002       | 52.42M     | 52.42M     | 0             | 0            | 128             | 126          | 127
 8         | 2026-08-19 10:01:30 | fe80::abcd:ef01   | 00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:03  | 00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:04  | 0            | 0            | 0.00B      | 0.00B      | 0             | 0            | 0               | 0            | 0

CLI TX STATISTICS MONITOR
 SocketFd  | eagain_all   | eagain_part   | enobufs_all   | enobufs_part   | emlink  | timeout  | eflowctl  | no_badqbuf  | other  | cqe_rnr  | cqe_ack_timeout  | cqe_fc  | cqe_remote | cqe_local | cqe_other
 5         | 0            | 0             | 0             | 0              | 0       | 0        | 0         | 0           | 0      | 0        | 1                | 0       | 0          | 0         | 0
 8         | 0            | 0             | 0             | 0              | 0       | 0        | 0         | 0           | 0      | 0        | 0                | 0       | 0          | 0         | 0

CLI RX STATISTICS MONITOR
 SocketFd  | get_event_fail | poll_fail   | refill_alloc_fail | refill_post_fail | qbuf_pop_fail | rxe_fc  | rxe_remote | rxe_local  | rxe_ack_timeout | rxe_rnr  | rxe_other | dataset_no_block | flow_ctrl_failed | rearm_fail  | peer_closed
 5         | 0              | 0           | 0                 | 0                | 0             | 1       | 0          | 0          | 0               | 0        | 0         | 0                | 0                | 0           | 1
 8         | 0              | 0           | 0                 | 0                | 0             | 0       | 0          | 0          | 0               | 0        | 0         | 0                | 0                | 0           | 0

```

**三个分区说明**:

| 分区 | 标题行 | 数据来源 | 格式 |
|------|--------|---------|------|
| StatsMgr | `CLI STATISTICS MONITOR` | 全局 atomic + per-socket `GetSocketCLIData()` | 全局指标键值对 + per-socket 表格行 |
| TxStatReporter | `CLI TX STATISTICS MONITOR` | per-socket `UmqTxOps::GetTxStatCounters()` | per-socket 错误桶表格行 (9 post + 6 cqe) |
| RxStatReporter | `CLI RX STATISTICS MONITOR` | per-socket `UmqRxOps::GetRxStatCounters()` | per-socket 错误桶表格行 (5 poll + 6 cqe + 4 dataset) |

> **与 CLI 的区别**: CLI 输出带 ANSI 颜色码、每秒可刷新; 落盘输出为纯文本、每个周期 (默认 10s) 追加一次, 以 `==================== <timeStamp> ====================` 分隔行开头便于离线时序分析。数据采集调用与 CLI 相同的函数, 保证一致性。详细实现见 §2.1.3。

#### 1.5.4 qbuf 池统计落盘 (30 秒周期)

挂在 `PrintStatsMgr` 后台线程 (与 KPI JSON / 统计快照同一线程), 由 `ExportQbufPoolStatsTick()` 以 `steady_clock` 节流, **固定 30 秒**周期, 独立于 `UBSOCKET_MONITOR_INTERVAL`:

| 项 | 值 |
|----|-----|
| 活动文件 | `ubsocket_qbuf.txt` (固定名, 追加写) |
| 归档文件 | `ubsocket_qbuf_<pid>.1.txt` (轮转时 rename 生成, 只读 0440) |
| 路径 | 与 KPI JSON / 统计快照同目录 (`UBSOCKET_MONITOR_FILE_PATH`, 默认 `/tmp/ubsocket/log`) |
| 开关 | `UBSOCKET_MONITOR_ENABLE` (与统计总开关共用, 关闭时后台线程不启动) |
| 轮转阈值 | 单文件 = `UBSOCKET_MONITOR_FILE_SIZE / 2` (默认 20MB), 覆盖式轮转, 最多 2 个文件 |
| 周期 | 固定 30s; 实际落盘点 = 每满 30s 后的第一个监控 tick (默认监控周期 10s 时精确 30s; 若 `UBSOCKET_MONITOR_INTERVAL > 30s`, 落盘间隔随之变大) |

文件内容示例 (每 30s 追加一段):

```
==================== 2026-08-19 10:30:30 ====================
<umq_qbuf_pool_stats_to_str 全量输出: 池配置 + Per-SizeClass 状态 + Per-Thread TLS 缓存分解>
```

- normal 池未初始化时 (进程启动初期 / 尚无 UMQ 活动) 跳过该轮, 仅打 debug 日志, 不产生空段
- 每轮独立取数, 文件中各时间段即池状态的 30s 间隔时间序列, 便于离线水位分析
- 与 CLI `qbufstats` 命令**数据同源** (同一对池级接口直接调用, 见 §1.4.1), 终端输出与落盘内容格式一致 (仅 CLI 含 ANSI 颜色码)

---

## 2. 详细分析

### 2.1 StatsMgr — 基础流量计数

**核心文件**: `profiling/statistics/statistics_statsmgr.h:108-323`

> 注: 行号为设计阶段基线, 代码变更后可能偏移。

#### 2.1.1 统计项定义

StatsMgr 维护两类计数: **全局静态计数** (进程级) 和 **per-socket Recorder 数组** (链路级)。

**全局静态计数** (`statistics_statsmgr.h`):

```cpp
inline static std::atomic<uint32_t> mConnCount{0};           // 总连接数
inline static std::atomic<uint32_t> mActiveConnCount{0};     // 主动连接数 (client)
inline static std::atomic<uint32_t> mReTxCount{0};           // 重传计数 (来自 UMQ tp perf)
```

> 连接数上限为 `RPC_ADPT_FD_MAX=8192`, 重传计数同理, uint32 (上限 42 亿) 足够。与 `CLIDataHeader` 中 `connNum` / `activeConn` / `reTxCount` 的 `uint32_t` 类型匹配, 消除隐式截断。

> 原有 `mRxPacketCount` / `mTxPacketCount` / `mRxByteCount` / `mTxByteCount` / `mTxErrorPacketCount` / `mTxLostPacketCount` 6 个全局 atomic 已删除 — 它们仅在 JSON 落盘 (`OutputAllStats`) 中使用, 不在 CLI 输出中。per-socket 的收发数据由 Recorder 提供, 不依赖这些全局变量。

**Per-socket Recorder** (`statistics_statsmgr.h`):

```cpp
// 指针惰性分配: UBSOCKET_MONITOR_ENABLE=on (默认) 时在 socket 构造时 new, 否则 nullptr
std::unique_ptr<std::array<Recorder, TRACE_STATE_TYPE_MAX>> m_recorder_vec;
```

每个 `SocketBase` 对象内嵌一个 `StatsMgr stats_mgr_` (`ubsocket_socket.h`)。统计未开启时 `m_recorder_vec` 为 `nullptr`, **不占用 per-socket 内存**; 开启时分配 9 槽 `Recorder` 数组 (堆分配, 每槽 1 个 `uint64_t m_cnt`)。

```cpp
enum trace_stats_type {
    CONN_COUNT,
    ACTIVE_OPEN_COUNT,
    RX_PACKET_COUNT,
    TX_PACKET_COUNT,
    RX_BYTE_COUNT,
    TX_BYTE_COUNT,
    BIGDATA_CTRL_RECV_COUNT,   // bigdata ctrl 报文接收计数
    BIGDATA_READ_COUNT,        // 基于 READ_OFFER 成功发起 RDMA READ 计数
    BIGDATA_CTRL_SEND_COUNT,   // bigdata ctrl 报文发送计数
    TRACE_STATE_TYPE_MAX
};
```

`UpdateTraceStats` 更新 per-socket Recorder:

```cpp
ALWAYS_INLINE void UpdateTraceStats(enum trace_stats_type type, uint32_t value)
{
    if (m_recorder_vec == nullptr) return;   // 统计未开启, no-op
    switch (type) {
        case RX_PACKET_COUNT:
            (*m_recorder_vec)[type].Update(value);   // per-socket
            break;
        case CONN_COUNT:
            mConnCount.fetch_add(value, std::memory_order_relaxed);  // 全局 (仅连接计数)
            break;
        // ... 其他 case 同理
    }
}
```

> 连接计数 (`CONN_COUNT` / `ACTIVE_OPEN_COUNT`) 同时更新全局 atomic 和 per-socket Recorder; 收发计数 (`RX_PACKET_COUNT` / `TX_PACKET_COUNT` / `RX_BYTE_COUNT` / `TX_BYTE_COUNT`) 仅更新 per-socket Recorder, 不再更新全局 atomic。bigdata 计数 (`BIGDATA_CTRL_RECV_COUNT` / `BIGDATA_READ_COUNT` / `BIGDATA_CTRL_SEND_COUNT`) 同样仅更新 per-socket Recorder。

#### 2.1.2 统计采集位置 (全量调用点)

以下按数据流方向列出所有采集位置:

##### 连接生命周期

| 采集位置 | 文件:行 | 统计项 | 触发时机 |
|---------|---------|--------|---------|
| `Connector::Connect` 成功后 | `ubsocket_socket_connector.cpp:75-76` | `CONN_COUNT +1`, `ACTIVE_OPEN_COUNT +1` | client 建链成功 |
| `Acceptor::Accept` 成功后 | `ubsocket_socket_acceptor.cpp:328` | `CONN_COUNT +1` | server 接受连接 |
| `SocketBase::~SocketBase` | `ubsocket_socket.h:47-49` | `SubMConnCount`, `SubMActiveConnCount` | socket 析构 |
| `Connector::~Connector` | `ubsocket_socket_connector.cpp:89-90` | `SubMConnCount`, `SubMActiveConnCount` | connector 析构 |
| `Acceptor` 释放 socket | `ubsocket_socket_acceptor.cpp:388` | `SubMConnCount` | acceptor 回收 |

##### TX 发送方向

| 采集位置 | 文件:行 | 统计项 | 触发时机 |
|---------|---------|--------|---------|
| `DataTx::WriteV` 返回后 | `ubsocket_data_tx.cpp:137` | `TX_BYTE_COUNT += tx_total_len` | writev 成功后 |
| `UmqTxOps::PostSend` 全部成功 | `umq_data_tx_ops.cpp:160` | `TX_PACKET_COUNT += batch` | umq_post 返回 0 |
| `UmqTxOps::PostSend` 部分成功 | `umq_data_tx_ops.cpp:282` | `TX_PACKET_COUNT += buf_num` | umq_post 部分成功, 按 bad_qbuf 之后的成功数量 |

> **注意**: `TX_BYTE_COUNT` 在 `DataTx::WriteV` (核心层) 采集; `TX_PACKET_COUNT` 在 `UmqTxOps::PostSend` (UMQ 实现层) 采集，两者可能不在同一次调用中累加 (如 PostSend 内 batch > 1 时 packet count 按 batch 累加, byte count 在外层按返回值累加)。

##### RX 接收方向

| 采集位置 | 文件:行 | 统计项 | 触发时机 |
|---------|---------|--------|---------|
| `DataRx::ReadV` 返回后 | `ubsocket_data_rx.cpp:132` | `RX_BYTE_COUNT += rx_total_len` | readv 成功后 |
| `UmqRxOps` 处理每个 buf | `umq_data_rx_ops.cpp:97` | `RX_PACKET_COUNT +1` | poll_rx 返回 buf, status == UMQ_BUF_SUCCESS |

> **RX_PACKET_COUNT 逐包计数**: 在 `umq_data_rx_ops.cpp:97`，遍历 poll_rx 返回的 buf 数组, 每个成功 buf `+1`。流控消息 (status >= UMQ_FAKE_BUF_FC_UPDATE) 和错误 buf 不计数。

##### 重传计数 (UMQ tp perf 查询)

| 采集位置 | 文件:行 | 统计项 | 触发时机 |
|---------|---------|--------|---------|
| `GlobalStatsMgr::ProcessEpollEvents` | `statistics.h:970` | `UpdateReTxCount` | 每个 epoll 周期 |
| `PrintStatsMgr::ProcessStats` | `ubsocket_print_stats_mgr.h:54` | `UpdateReTxCount` | 每个统计周期 (默认 10s) |

`UpdateReTxCount` (`statistics.cpp:56-91`) 通过 `umq_stats_tp_perf_info_get` 从 UMQ 传输层查询 `retry_count` 字符串并解析为数值, 写入 `mReTxCount`。这是**唯一从 UMQ 底层反向获取的统计**，反映跨节点链路质量。

##### Bigdata (大数据 ctrl 报文 / RDMA READ)

| 采集位置 | 文件:行 | 统计项 | 触发时机 |
|---------|---------|--------|---------|
| `UbsBigdata::HandleRxControl` 验证通过后 | `ubsocket_bigdata.cpp` | `BIGDATA_CTRL_RECV_COUNT +1` | 收到合法的 bigdata ctrl 报文 (READ_OFFER / READ_DONE / READ_ABORT), 内容验证通过后、switch 分发前 |
| `DoReadOffer` 中 `umq_post(READ)` 成功后 | `ubsocket_bigdata.cpp` | `BIGDATA_READ_COUNT += read_wr_count` | 基于 READ_OFFER 构建的 READ WR 链 `umq_post` 返回 0 (成功提交到 SQ), 一次 OFFER 中的每个段对应一个 READ WR |
| `FlushPendingOffer` 中 `SealOnly` 成功后 | `ubsocket_bigdata.cpp` | `BIGDATA_CTRL_SEND_COUNT +1` | 发送侧: READ_OFFER ctrl 报文封帧成功 (SealOnly 返回非空), 即将随 batch umq_post 发出 |
| `SendSimpleCtrl` 中 `umq_post` 成功后 | `ubsocket_bigdata.cpp` | `BIGDATA_CTRL_SEND_COUNT +1` | 接收侧: READ_DONE / READ_ABORT ctrl 报文 `umq_post` 返回 0 (成功提交到 SQ) |

> **ctrl recv 计数**: 每次 `HandleRxControl` 验证通过即 `+1`, 覆盖全部三种 ctrl 类型 (READ_OFFER / READ_DONE / READ_ABORT)。不区分类型 — 按类型细分可通过 RX-STAT 的 CQE 桶间接观察, 此处仅看 ctrl 报文总流量。
>
> **read 计数**: 每次 `DoReadOffer` 中 `umq_post` 返回 0 时累加 `read_wr_count` (即 OFFER 中的 `nsegs`, 1~32), 而非固定 +1 — 一个 READ_OFFER ctrl 报文携带 `nsegs` 个段, 每个段对应一个 RDMA READ WR, 所以一次成功的 OFFER 对应 `nsegs` 次 READ。EAGAIN 重试不重复计数 — 仅首次 `umq_post` 成功时累加, 进入 pending_reads 队列的重试不触发计数。
>
> **ctrl send 计数**: 覆盖两条发送路径:
> - **READ_OFFER** (发送侧): `FlushPendingOffer` 中 `SealOnly` 成功即 `+1` — ctrl 报文已封帧并加入 batch, 即将随 `umq_post` 发出。此时 `ctx.umq_sock` 已就绪, 无需额外查找。
> - **READ_DONE / READ_ABORT** (接收侧): `SendSimpleCtrl` 中 `umq_post` 返回 0 即 `+1` — ctrl 报文已成功提交到 SQ。EAGAIN 进入 deferred-ctrl 队列的重试**不重复计数** (仅在首次成功 post 时 `+1`)。
>
> **统计开关**: 所有采集均由 `GlobalSetting::UBS_MONITOR_ENABLE` 守卫, 关闭时零开销。`GetStatsMgr()` 返回 nullptr 时 (UBSOCKET_MONITOR_ENABLE=off) 同样短路。

#### 2.1.3 统计输出

**CLI 查询** (`statistics.h` `ProcessStatRequest`): 遍历 `ArraySet<Socket>`，对每个 UmqSocket 调用 `GetSocketCLIData` (`statistics_statsmgr.h`)，读取 per-socket Recorder 的 `GetCnt()`，填充 `CLISocketData` 结构 (含 `bigdataCtrlRecv` / `bigdataRead` / `bigdataCtrlSend` 字段)。全局计数 (`mConnCount` / `mActiveConnCount` / `mReTxCount`) 填入 `CLIDataHeader`。

**后台导出** (`statistics_statsmgr.h` `OutputAllStats` + `statistics.cpp` `ExportStatsSnapshot` + `ubsocket_print_stats_mgr.h` `PrintStatsMgr`): 由 `PrintStatsMgr` 后台线程周期调用, 每轮同时输出 KPI JSON 文件和统计快照文件。输出格式、配置项、文件命名与轮转策略见 §1.4, 此处仅描述实现细节。

##### ProcessStats 调用链

`ubsocket_init()` → `StartStatsCollection()` 启动后台线程, 每轮执行 `ProcessStats()`:

```
ProcessStats():
    1. UpdateReTxCount()          — 从 UMQ tp perf 查询重传计数
    2. AggregatePerSocketStats()  — 遍历 ArraySet<Socket> 聚合 per-socket Recorder
    3. OutputAllStats()           — 拼装 KPI JSON 字符串
    4. OutputJSON()               — 写入 ubsocket_kpi.json + ArchiveJSON 检查轮转
    5. ExportStatsSnapshot()      — 写入 ubsocket_stats.txt + ArchiveStatsTxt 检查轮转
```

##### KPI JSON 数据来源

KPI JSON 中 9 个字段的数据来源:

| 字段 | 数据来源 | 采集方式 |
|------|---------|---------|
| totalConnections / activeConnections | `mConnCount` / `mActiveConnCount` 全局 atomic | 热路径 `UpdateTraceStats` 累加, socket 析构时减 |
| reTxCount | `mReTxCount` 全局 atomic | 后台线程 `UpdateReTxCount()` 从 UMQ `umq_stats_tp_perf_info_get` 查询 |
| sendPackets / receivePackets / sendBytes / receiveBytes | Σ per-socket `Recorder[TX/RX_PACKET/BYTE_COUNT]` | `AggregatePerSocketStats` 遍历 `ArraySet<Socket>` 聚合 |
| errorPackets / lostPackets | Σ per-socket (当前硬编码 0) | 同上 (PR-222 删除了全局 atomic, per-socket Recorder 中无对应枚举) |

> **聚合实现** (`statistics.cpp` `AggregatePerSocketStats`): 遍历 `ArraySet<Socket>`, 跳过纯 TCP socket 和 listen fd, 对每个 UmqSocket 调用 `GetSocketCLIData()` 获取 per-socket Recorder 值并累加。聚合在后台线程执行, 不影响热路径。

##### 统计快照数据来源

`ExportStatsSnapshot` (`statistics.cpp`) 遍历 `ArraySet<Socket>`, 采集前三种统计 (per-socket) 的完整数据 (第四种 QbufPoolStats 为全局池级数据, 由 `ExportQbufPoolStats` 单独落盘, 见 §2.4.4):

| 分区 | 采集函数 | 数据结构 | 说明 |
|------|---------|---------|------|
| StatsMgr | `UmqSocket::GetSocketCLIData()` | `CLISocketData` | 全局指标 (连接数/重传/qbuf pool) + per-socket 流量计数 |
| TxStatReporter | `UmqTxOps::GetTxStatCounters()` | `TxStatCounters` | per-socket 15 个错误桶 (9 post + 6 cqe) |
| RxStatReporter | `UmqRxOps::GetRxStatCounters()` | `RxStatCounters` | per-socket 15 个错误桶 (5 poll + 6 cqe + 4 dataset) |

> 每个周期的输出以时间戳分隔行 (`==================== %Y-%m-%d %H:%M:%S ====================`) 开头, 后接三个分区 (格式同 CLI 输出, 纯文本无 ANSI 码)。三个分区写入同一文件 `ubsocket_stats.txt`。三类文件 (KPI JSON / 统计快照 / qbuf 池统计) 各自独立轮转, 策略见 §1.5.2 / §1.5.3 / §1.5.4。

#### 2.1.4 跨节点诊断场景

```
Node A Send Packets(fd=5) = 1000000    Node B Recv Packets(fd=5) = 999998
                        ↕ 差值 = 2, 跨节点丢失
Node A reTxCount = 3                    Node B reTxCount = 0
                        ↕ A 端重传, 链路质量问题在 A→B 方向
```

---

### 2.2 TxStatReporter — TX 方向流控定界统计 (per-socket)

**核心文件**:
- `profiling/statistics/tx_stat_defs.h` — 枚举与桶名
- `profiling/statistics/tx_stat_block.h` — per-socket 计数 + 热路径宏
- `profiling/statistics/tx_stat_block.cpp` — per-socket 变量定义 + 聚合
- `profiling/statistics/tx_stat_reporter.h` / `.cpp` — CLI 查询响应
- `cli/cli_message.h` — `CLITxStatData` 结构体
- `cli/cli_terminal_display.cpp` — `DisplayTxStatInfo()` 输出

#### 2.2.1 设计目标

专门定位**发送方向的失败原因**，回答两个问题:
- **A) 提交侧**: `umq_post` 为什么失败？(信用不足 / 缓冲耗尽 / 超时 / 流控)
- **B) 完成侧**: 发送完成事件 (TX CQE) 为什么异常？(对端 RNR / ACK 超时 / 远端错误)

与原全局统计不同, 改为 **per-socket** 统计 — 每条链路独立维护 15 个错误桶, 通过 CLI `ubstat txstat` 按链路查询。

#### 2.2.2 错误桶定义

**埋点 A — umq_post 失败** (`tx_stat_defs.h:34-49`, 9 桶):

| 枚举值 | 桶名 | 诊断意义 | 跨节点相关 |
|--------|------|---------|-----------|
| `POST_ERR_EAGAIN_ALL` | `eagain_all` | 流控 credit 不足, 全部失败 | 对端未及时回复 credit |
| `POST_ERR_EAGAIN_PART` | `eagain_part` | 流控 credit 不足, 部分失败 | 对端 credit 跟不上 |
| `POST_ERR_ENOBUFS_ALL` | `enobufs_all` | qbuf 池耗尽, 全失败 | 本端问题 |
| `POST_ERR_ENOBUFS_PART` | `enobufs_part` | qbuf 池耗尽, 部分失败 | 本端问题 |
| `POST_ERR_EMLINK` | `emlink` | 无可用 jetty, 已入 TpWaitQueue | 本端连接问题 |
| `POST_ERR_ETIMEDOUT` | `timeout` | **等对端授信回复超时 (默认 1s)** | **跨节点: 对端无响应** |
| `POST_ERR_EFLOWCTL` | `eflowctl` | 流控失败 (FATAL/EAGAIN) | 跨节点流控 |
| `POST_ERR_NO_BADQBUF` | `no_badqbuf` | umq_post 返回错误但无 bad_qbuf | 本端异常分支 |
| `POST_ERR_OTHER` | `other` | 其他 (flagEIO 分支) | — |

**埋点 B — TX CQE 完成异常** (`tx_stat_defs.h:52-63`, 6 桶):

| 枚举值 | 桶名 | 对应 buf->status | 跨节点相关 |
|--------|------|-----------------|-----------|
| `CQE_ERR_RNR` | `rnr` | `UMQ_BUF_RNR_RETRY_CNT_EXC_ERR` | **对端 RQ 不足** |
| `CQE_ERR_ACK_TIMEOUT` | `ack_timeout` | `UMQ_BUF_ACK_TIMEOUT_ERR` | **对端未回 ACK** |
| `CQE_ERR_FC` | `fc` | `UMQ_FAKE_BUF_FC_ERR` | 流控失败 |
| `CQE_ERR_REMOTE` | `remote` | `REM_RESP_LEN / UNSUPPORTED_REQ / OPERATION / ACCESS_ABORT` | **对端协议错误** |
| `CQE_ERR_LOCAL` | `local` | `LOC_LEN / OPERATION / ACCESS` | 本端错误 |
| `CQE_ERR_OTHER` | `other` | unsupported opcode / flush / suspend / poison / 未知 | — |

#### 2.2.3 per-socket 计数与热路径宏

**per-socket 计数器** — 挂在 `UmqTxOps` 上, 指针惰性分配:

```cpp
struct TxStatCounters {
    volatile uint32_t post_err[POST_ERR_MAX];   // 9 个 post 失败桶
    volatile uint32_t cqe_err[CQE_ERR_MAX];     // 6 个 CQE 异常桶
};

// UmqTxOps 成员: 统计未开启时为 nullptr, 不占用 per-socket 内存
std::unique_ptr<TxStatCounters> tx_stat_counters_;
```

> 统计未开启时 `tx_stat_counters_` 为 `nullptr`, **不占用 per-socket 内存**; 开启时在 `UmqTxOps` 构造时 `new` 分配。`volatile` 而非 `std::atomic` — 并发 `++` 在多核下可能丢更新, 但本统计只看**错误趋势**, 不需要精确计数。

**热路径宏** — per-socket, 含 nullptr 守卫:

```cpp
// post 失败: 若 tx_stat_counters_ 非空则 post_err[k]++
#define UMQ_POST_ERR_ADD(sock, k)    // 1 次 nullptr 判断 + 1 次 volatile ++
// CQE 异常: 若 tx_stat_counters_ 非空则 cqe_err[k]++
#define UMQ_CQE_ERR_ADD(sock, k)     // 1 次 nullptr 判断 + 1 次 volatile ++
```

> 统计未开启时, 宏内部 `tx_stat_counters_ == nullptr` 短路返回, **零开销**。原 `UMQ_POST_OK_ADD()` 宏取消 — 不再统计 post 成功数, 只关注错误桶。

#### 2.2.4 统计采集位置 (全量调用点)

##### 埋点 A — umq_post 提交侧

全部位于 `UmqTxOps::PostSend` 函数中 (`umq_data_tx_ops.cpp:148-306`), 在调用 `UmqApi::umq_post()` 之后按返回值分支。`PostSend` 函数签名带 `SocketPtr sock`, 直接可用:

```
umq_data_tx_ops.cpp : PostSend(sock)
    |
    +-- line 151: int ret = UmqApi::umq_post(...)
    |
    +-- ret == UMQ_SUCCESS (line 152-168)
    |   (无计数 — 不统计 post 成功)
    |
    +-- bad_qbuf != nullptr (line 169-306)
    |   |
    |   +-- errno == EAGAIN (line 174-206)
    |   |   +-- all_failed == true
    |   |   |   +-- line 191: UMQ_POST_ERR_ADD(sock, POST_ERR_EAGAIN_ALL)
    |   |   +-- all_failed == false
    |   |       +-- line 194: UMQ_POST_ERR_ADD(sock, POST_ERR_EAGAIN_PART)
    |   |
    |   +-- errno == ETIMEDOUT (line 207-211)
    |   |   +-- line 208: UMQ_POST_ERR_ADD(sock, POST_ERR_ETIMEDOUT)
    |   |
    |   +-- ret == -UMQ_ERR_EFLOWCTL* (line 212-215)
    |   |   +-- line 213: UMQ_POST_ERR_ADD(sock, POST_ERR_EFLOWCTL)
    |   |
    |   +-- errno == EMLINK (line 216-227)
    |   |   +-- line 217: UMQ_POST_ERR_ADD(sock, POST_ERR_EMLINK)
    |   |
    |   +-- errno == ENOBUFS (line 228-244)
    |   |   +-- all_failed == true
    |   |   |   +-- line 234: UMQ_POST_ERR_ADD(sock, POST_ERR_ENOBUFS_ALL)
    |   |   +-- all_failed == false
    |   |       +-- line 243: UMQ_POST_ERR_ADD(sock, POST_ERR_ENOBUFS_PART)
    |   |
    |   +-- else (line 245-252)
    |       +-- line 250: UMQ_POST_ERR_ADD(sock, POST_ERR_OTHER)
    |
    +-- bad_qbuf == nullptr (line 297-306)
        +-- line 300: UMQ_POST_ERR_ADD(sock, POST_ERR_NO_BADQBUF)
```

> **改动**: 原宏无参 `UMQ_POST_ERR_ADD(k)` 改为 `UMQ_POST_ERR_ADD(sock, k)`, 在宏内部通过 `sock` 获取 `TxStatCounters`。原 `UMQ_POST_OK_ADD()` 取消。所有调用点已天然持有 `sock` 参数, 无需额外传参。

##### 埋点 B — TX CQE 完成异常

采集点位于 `UmqTxHelper::LogTxCqeErrorMsg` (`umq_tx_helper.cpp:252-279`)。原函数只收 `umq_buf_t *buf`, 需增加 socket 上下文:

```
umq_tx_helper.cpp : LogTxCqeErrorMsg(buf, sock)   <- 新增 sock 参数
    |
    +-- line 254: bufStatus = buf->status
    |
    +-- line 262: if (bufStatus == UMQ_BUF_SUCCESS) return
    |
    +-- line 266-278: switch (bufStatus) -> 映射到 CqeErr 桶 (不变)
    |   +-- UMQ_BUF_RNR_RETRY_CNT_EXC_ERR  -> CQE_ERR_RNR
    |   +-- UMQ_BUF_ACK_TIMEOUT_ERR        -> CQE_ERR_ACK_TIMEOUT
    |   +-- UMQ_FAKE_BUF_FC_ERR            -> CQE_ERR_FC
    |   +-- UMQ_BUF_REM_*_ERR              -> CQE_ERR_REMOTE
    |   +-- UMQ_BUF_LOC_*_ERR              -> CQE_ERR_LOCAL
    |   +-- default                        -> CQE_ERR_OTHER
    |
    +-- line 279: UMQ_CQE_ERR_ADD(sock, cqe_bucket)   <- 改为 per-socket
```

**调用链与 socket 上下文传递**:

```
TxCqePoller::Run()
  -> PollUmqTxInternal(poll_args)
       |-- poll_args.sock 非空时: 直接传入
       |-- poll_args.sock 为空时: 从 buf_pro->umq_ctx 提取 fd
       |    -> ArraySet<Socket>::GetInstance().GetItem(fd) 反查 sock
       |    (已有代码在 umq_tx_helper.cpp:118 做了同样的 fd 提取)
       |
       +-- buf[i]->status != 0 时:
            -> HandleTxCqeError(buf, sock)   <- 新增 sock 参数
                 -> LogTxCqeErrorMsg(buf, sock)
                      -> UMQ_CQE_ERR_ADD(sock, cqe_bucket)
```

> **边界处理**: 若 `qbuf_ext == nullptr` 无法提取 fd, 或 `ArraySet::GetItem(fd)` 返回空 (socket 已释放), 则跳过 per-socket 计数, 不影响其他逻辑。

#### 2.2.5 CLI 查询响应

**CLI 命令**: `ubstat txstat -p <pid>`

**响应流程**: `GlobalStatsMgr::ProcessTxStatRequest()` 遍历 `ArraySet<Socket>`, 对每个 UmqSocket 读取其 `UmqTxOps::tx_stat_counters_`, 填充 `CLITxStatData` 结构:

```cpp
struct CLITxStatData {
    uint64_t socketId;
    uint32_t post_err[POST_ERR_MAX];         // 9 个 post 失败桶
    uint32_t cqe_err[CQE_ERR_MAX];           // 6 个 CQE 异常桶
};
```

**终端输出**: `TerminalDisplay::DisplayTxStatInfo()` 按 per-socket 行输出, 字段对齐 + 着色规则同 stat 命令。

#### 2.2.6 跨节点诊断场景

| 症状 | 检查 | 结论 |
|------|------|------|
| A→B 延迟高 | A 端 fd=5 的 `cqe_ack_timeout > 0` | 跨节点网络丢包或 B 端宕机, 问题在 fd=5 这条链路 |
| A→B 吞吐低 | A 端 fd=5 的 `eagain_all` 持续增长 | B 端 credit 回复跟不上, 仅影响 fd=5 |
| A→B 偶发失败 | A 端 fd=5 的 `timeout > 0` | B 端 credit 回复超时 (1s), B 端负载高 |
| A→B 连接异常 | A 端 fd=5 的 `cqe_rnr` 持续增长 | B 端 RQ 不足, 接收能力不够 |
| A→B 协议错误 | A 端 fd=5 的 `cqe_remote > 0` | B 端协议错误 (resp_len / unsupported / access_abort) |
| 本端问题 | A 端 fd=5 的 `cqe_local > 0` 或 `enobufs_all > 0` | 本端 buffer / 长度 / 权限问题 |

**两端对比**: A 端 TX-STAT 看每条链路发送方向失败, B 端 TX-STAT 看其向 A 发送的失败, 按 fd 对比可定位是哪条链路、哪个方向的问题。

---

### 2.3 RxStatReporter — RX 方向流控定界统计 (per-socket)

**核心文件**:
- `profiling/statistics/rx_stat_defs.h` — 枚举与桶名
- `profiling/statistics/rx_stat_block.h` — per-socket 计数 + 热路径宏
- `profiling/statistics/rx_stat_block.cpp` — per-socket 变量定义 + 聚合
- `profiling/statistics/rx_stat_reporter.h` / `.cpp` — CLI 查询响应
- `cli/cli_message.h` — `CLIRxStatData` 结构体
- `cli/cli_terminal_display.cpp` — `DisplayRxStatInfo()` 输出

#### 2.3.1 设计目标

定位**接收方向的失败原因**, 覆盖 Poll / CQE 处理 / 数据提取三个阶段, 共 15 个错误桶。与 TxStatReporter 对称, 同为 **per-socket** 统计, 通过 CLI `ubstat rxstat` 按链路查询。

#### 2.3.2 错误桶定义

**Poll 阶段** (`rx_stat_defs.h`, 5 桶):

| 枚举值 | 桶名 | 诊断意义 | 跨节点相关 |
|--------|------|---------|-----------|
| `RX_POLL_GET_EVENT_FAIL` | `get_event_fail` | `umq_get_cq_event` 失败 | 本端 CQ 异常 |
| `RX_POLL_FAIL` | `poll_fail` | `umq_poll` 返回负值 | 本端拉取失败 |
| `RX_REFILL_ALLOC_FAIL` | `refill_alloc_fail` | `umq_buf_alloc` 返回 nullptr | 本端内存不足 |
| `RX_REFILL_POST_FAIL` | `refill_post_fail` | `umq_post` (refill) 失败 | 本端或对端 |
| `RX_QBUF_POP_FAIL` | `qbuf_pop_fail` | `GetAndPopQbuf` 失败 (共享 JFR) | 本端 RX 队列异常 |

**CQE 处理阶段** (`rx_stat_defs.h`, 6 桶):

| 枚举值 | 桶名 | 对应 buf->status | 跨节点相关 |
|--------|------|-----------------|-----------|
| `RXCQE_FC` | `fc` | `UMQ_FAKE_BUF_FC_ERR` / `FC_ERR_FATAL` / `FC_UPDATE` | **跨节点流控失败** |
| `RXCQE_REMOTE` | `remote` | `REM_RESP_LEN / UNSUPPORTED_REQ / OPERATION / ACCESS_ABORT` | **对端协议错误** |
| `RXCQE_LOCAL` | `local` | `LOC_LEN / OPERATION / ACCESS` | 本端错误 |
| `RXCQE_ACK_TIMEOUT` | `ack_timeout` | `UMQ_BUF_ACK_TIMEOUT_ERR` | **对端未回 ACK** |
| `RXCQE_RNR` | `rnr` | `UMQ_BUF_RNR_RETRY_CNT_EXC_ERR` | **对端 RQ 不足** |
| `RXCQE_OTHER` | `other` | unsupported opcode / flush / suspend / poison / 未知 | — |

**数据提取阶段** (`rx_stat_defs.h`, 4 桶):

| 枚举值 | 桶名 | 诊断意义 | 跨节点相关 |
|--------|------|---------|-----------|
| `RX_DATASET_NO_BLOCK` | `dataset_no_block` | `DataToBlock` 返回 nullptr | 本端异常 |
| `RX_FLOW_CTRL_FAILED` | `flow_ctrl_failed` | `flow_control_failed_` 置位 | 跨节点流控 |
| `RX_REARM_FAIL` | `rearm_fail` | `RearmRxInterrupt` 失败 | 本端中断异常 |
| `RX_PEER_CLOSED` | `peer_closed` | `recv(MSG_PEEK)` 返回 0 | **对端 TCP 关闭** |

#### 2.3.3 per-socket 计数与热路径宏

**per-socket 计数器** — 挂在 `UmqRxOps` 上, 指针惰性分配:

```cpp
struct RxStatCounters {
    volatile uint32_t poll_err[RX_POLL_ERR_MAX];            // 5 个 poll 失败桶
    volatile uint32_t cqe_err[RXCQE_ERR_MAX];               // 6 个 CQE 异常桶
    volatile uint32_t dataset_err[RX_DATASET_ERR_MAX];      // 4 个数据提取失败桶
};

// UmqRxOps 成员: 统计未开启时为 nullptr, 不占用 per-socket 内存
std::unique_ptr<RxStatCounters> rx_stat_counters_;
```

> 统计未开启时 `rx_stat_counters_` 为 `nullptr`, **不占用 per-socket 内存**; 开启时在 `UmqRxOps` 构造时 `new` 分配。

**热路径宏** — 含 nullptr 守卫:

```cpp
#define RX_POLL_ERR_ADD(sock, k)      // 1 次 nullptr 判断 + 1 次 volatile ++
#define RX_CQE_ERR_ADD(sock, k)       // 1 次 nullptr 判断 + 1 次 volatile ++
#define RX_DATASET_ERR_ADD(sock, k)   // 1 次 nullptr 判断 + 1 次 volatile ++
```

> 统计未开启时, 宏内部 `rx_stat_counters_ == nullptr` 短路返回, **零开销**。不统计 poll_total/poll_ok/recv_total/recv_ok — 只关注错误桶, 与 TxStatReporter 对称。

#### 2.3.4 统计采集位置 (全量调用点)

##### 埋点 A — Poll 阶段

全部位于 `UmqRxOps::PollRx` (`umq_data_rx_ops.cpp:22-107`) 及其内部函数。`PollRx` 函数签名带 `SocketPtr sock`, `UmqRxOps` 的 `fd_` / `local_umqh_` 成员也可用:

```
umq_data_rx_ops.cpp : PollRx(sock)
    |
    +-- GetAndAckEvent() 失败 (line 26-31)
    |   +-- RX_POLL_ERR_ADD(sock, RX_POLL_GET_EVENT_FAIL)
    |
    +-- GetQbuf(sock, buf, max) 失败 (line 39-49)
    |   +-- 共享 JFR: GetAndPopQbuf < 0
    |   |   +-- RX_POLL_ERR_ADD(sock, RX_QBUF_POP_FAIL)     [umq_data_rx_ops.cpp:125-126]
    |   +-- 非共享 JFR: UmqPollAndRefillRx < 0
    |       +-- umq_poll < 0
    |       |   +-- RX_POLL_ERR_ADD(sock, RX_POLL_FAIL)      [umq_data_rx_ops.cpp:140-147]
    |       +-- umq_buf_alloc == nullptr (refill)
    |       |   +-- RX_POLL_ERR_ADD(sock, RX_REFILL_ALLOC_FAIL) [umq_data_rx_ops.cpp:183-184]
    |       +-- umq_post (refill) 失败
    |           +-- RX_POLL_ERR_ADD(sock, RX_REFILL_POST_FAIL)  [umq_data_rx_ops.cpp:175-182]
    |
    +-- 遍历 buf[], 按 status 分发 (line 55-104)
        +-- status == 0: (无计数 — 不统计正常接收)
        +-- status >= UMQ_FAKE_BUF_FC_UPDATE:
        |   +-- FC_UPDATE: RX_CQE_ERR_ADD(sock, RXCQE_FC)
        |   +-- FC_ERR / FC_ERR_FATAL: HandleErrorRxCqe(buf) 内部按 status 映射 RXCQE_FC
        |   +-- 其他: RX_CQE_ERR_ADD(sock, RXCQE_OTHER)
        +-- status != 0 (其他错误):
            +-- HandleErrorRxCqe(buf) 内部按 status 映射:
                +-- REM_*  -> RXCQE_REMOTE
                +-- LOC_*  -> RXCQE_LOCAL
                +-- ACK_TIMEOUT -> RXCQE_ACK_TIMEOUT
                +-- RNR    -> RXCQE_RNR
                +-- 其他   -> RXCQE_OTHER
```

> **HandleErrorRxCqe 改动**: 原 `HandleErrorRxCqe(umq_buf_t *buf)` 无 sock 参数, 需改为 `HandleErrorRxCqe(umq_buf_t *buf, SocketPtr sock)` (或通过 `UmqRxOps::fd_` + `ArraySet::GetItem` 反查)。`PollRx` 已持有 `sock`, 直接传入。

##### 埋点 B — 数据提取阶段

位于 `DataRxOps::RxDataSet` (`ubsocket_data_rx.cpp:130-192`)。`RxDataSet` 通过 `fd_` 成员可关联回 socket:

```
ubsocket_data_rx.cpp : RxDataSet(buf, size)
    |
    +-- DataToBlock() == nullptr (line 132-137)
    |   +-- RX_DATASET_ERR_ADD(sock, RX_DATASET_NO_BLOCK)
    |
    +-- CutAndInsertAfter() == 0 (line 138-186)
    |   +-- flow_control_failed_ 置位 (line 156-161)
    |   |   +-- RX_DATASET_ERR_ADD(sock, RX_FLOW_CTRL_FAILED)
    |   +-- RearmRxInterrupt() < 0 (line 163-168)
    |   |   +-- RX_DATASET_ERR_ADD(sock, RX_REARM_FAIL)
    |   +-- recv(MSG_PEEK) == 0 (line 173-181)
    |   |   +-- RX_DATASET_ERR_ADD(sock, RX_PEER_CLOSED)
    |
    +-- 正常返回: 无计数
```

> **sock 获取方式**: `RxDataSet` 是 `DataRxOps` 的方法, `DataRxOps::fd_` 成员标识 socket。可通过 `ArraySet<Socket>::GetInstance().GetItem(fd_)` 反查 `SocketPtr`, 或在 `DataRx::ReadV` 调用 `RxDataSet` 时将 `sock` 透传 (推荐, 避免额外 ArraySet 查找)。

#### 2.3.5 CLI 查询响应

**CLI 命令**: `ubstat rxstat -p <pid>`

**响应流程**: `GlobalStatsMgr::ProcessRxStatRequest()` 遍历 `ArraySet<Socket>`, 对每个 UmqSocket 读取其 `UmqRxOps::rx_stat_counters_`, 填充 `CLIRxStatData` 结构:

```cpp
struct CLIRxStatData {
    uint64_t socketId;
    uint32_t poll_err[RX_POLL_ERR_MAX];       // 5 个 poll 失败桶
    uint32_t cqe_err[RXCQE_ERR_MAX];          // 6 个 CQE 异常桶
    uint32_t dataset_err[RX_DATASET_ERR_MAX]; // 4 个数据提取失败桶
};
```

**终端输出**: `TerminalDisplay::DisplayRxStatInfo()` 按 per-socket 行输出, 字段对齐 + 着色规则同 stat 命令。

#### 2.3.6 跨节点诊断场景

| 症状 | 检查 | 结论 |
|------|------|------|
| A→B 数据丢失 | B 端 fd=5 的 `poll_fail > 0` | B 端拉取失败, 本端问题 |
| A→B 对端无响应 | B 端 fd=5 的 `rxe_ack_timeout > 0` | B 端视角: A 端未回 ACK, A 端宕机或网络丢包 |
| A→B 流控频繁 | B 端 fd=5 的 `rxe_fc > 0` | 跨节点流控失败 |
| A→B 对端协议错误 | B 端 fd=5 的 `rxe_remote > 0` | A 端发送的协议格式 B 端不支持 |
| B 端接收异常 | B 端 fd=5 的 `rxe_local > 0` | B 端本端接收错误 |
| 连接断开 | B 端 fd=5 的 `peer_closed > 0` | A 端 TCP 连接已关闭 |

**两端配合**: A 端 TX-STAT 看 A→B 发送失败, B 端 RX-STAT 看对应链路接收失败, 两个视角交叉确认可精确定位问题在 A 端、B 端还是跨节点链路。

---

### 2.4 QbufPoolStats — UMQ qbuf 池统计 (全局池级)

**核心文件**:
- `profiling/statistics/statistics.cpp` — `ExportQbufPoolStats` (取数 + 格式化 + 写盘 + 轮转)
- `profiling/statistics/statistics.h` — `ProcessQbufPoolStatsRequest` (CLI 查询响应) + 命令枚举分发
- `profiling/statistics/ubsocket_print_stats_mgr.h` — `ExportQbufPoolStatsTick` (30s 节流)
- `hcom/umq/include/umq/umq_dfx_api.h` — UMQ 池级 DFX 接口公共声明
- `cli/cli_message.h` — `CLICommand::QBUF_POOL_STATS` 枚举 + `CLIQbufPoolStatsData` 结构体
- `cli/cli_args_parser.cpp` / `cli/cli_client.cpp` / `cli/cli_terminal_display.cpp` — CLI 命令链路

#### 2.4.1 设计目标

定位**共享内存池的水位与泄漏类问题**, 回答两个问题:

- **A) 池耗尽定位**: `umq_post` 返回 ENOBUFS 时 (对应 TX-STAT 的 `enobufs_all` / `enobufs_part` 桶, 见 §2.2), 池水位是否真的到顶? 哪个 size-class 先耗尽?
- **B) 泄漏观察**: 进程长期运行后池在用数是否只增不减? 哪个 per-thread TLS 缓存持有量异常?

与前三种统计的**架构差异**: 统计工作全部在 UMQ 侧完成 (UMQ 库内的池级 DFX 接口), ubsocket 统计侧**不做任何计数**, 只负责取数 (周期 30s 落盘 + CLI 按需查询)、记录。因此无 per-socket 计数器、无热路径埋点、无 `UBSOCKET_MONITOR_ENABLE` 短路逻辑 (开关只控制后台线程是否启动)。

#### 2.4.2 统计项定义 (数据来源)

数据来源为 UMQ 池级 DFX 接口 (见 §1.4.1), 只采集 **normal + tiny** 两池, 不含 huge 池。取数方式:

```cpp
umq_qbuf_pool_stats_t poolStats{};
umq_qbuf_pool_info_get(&poolStats);        /* normal 池 (UMQ_QBUF_POOL_TYPE_SMALL) */
umq_tiny_qbuf_pool_info_get(&poolStats); /* tiny 池 (UMQ_QBUF_POOL_TYPE_TINY), 条目追加 */

char poolBuf[32768]; /* 32KB: stats_to_str 输出含 per-SC / per-thread 分解 */
umq_qbuf_pool_stats_to_str(&poolStats, poolBuf, sizeof(poolBuf));
```

> 不走 `umq_stats_qbuf_pool_get(UMQ_INVALID_HANDLE)` 全局聚合封装 — 该封装额外包含 huge 池。两池条目追加填充在 `poolStats.qbuf_pool_info[]` 数组中 (`num` 计数)。normal 池未初始化时 (进程启动初期 / 尚无 UMQ 活动) `umq_qbuf_pool_info_get` 返回错误, 该轮跳过。

#### 2.4.3 CLI 查询响应

**CLI 命令**: `ubstat qbufstats -p <pid>` (枚举 `CLICommand::QBUF_POOL_STATS = 13`)

**响应流程**: `GlobalStatsMgr::ProcessQbufPoolStatsRequest()` 直接调用同一对池级接口 (与周期落盘数据同源), 填充 `CLIQbufPoolStatsData` 结构 — 服务端返回**原始结构不做格式化**:

```cpp
struct CLIQbufPoolStatsData {
    int32_t retCode;                         /* 池级 info_get 返回值, 0 成功 */
    umq_qbuf_pool_stats_t umqQbufPoolStat;  /* 原始统计结构 (normal + tiny 两池条目) */
};
```

**终端输出**: `TerminalDisplay::DisplayQbufPoolStatsInfo()` 客户端调用 `umq_qbuf_pool_stats_to_str` 渲染 (32KB 缓冲, 与服务端落盘一致); `retCode != 0` 时红色提示, 不打印无效数据。与既有 `QBUF_POOL` 命令的区别见 §1.4.2 的对照表。

> CLI 链路复用现有 UDS 通道 (`ubscli-<pid>`) 与消息机制, 无额外监听资源: `cli_args_parser.cpp` (`qbufstats` 命令注册, 不支持 `-w` 持续刷新) → `cli_client.cpp` `ProcessQbufPoolStats` (复用 `SendSimpleCmd`) → 服务端 `ProcessQbufPoolStatsRequest` → `cli_terminal_display.cpp` `DisplayQbufPoolStatsInfo`。

#### 2.4.4 周期落盘实现

`PrintStatsMgr` 后台线程每轮监控 tick 调用 `ExportQbufPoolStatsTick()`, 以 `steady_clock` 节流 (独立于 `UBSOCKET_MONITOR_INTERVAL`), 每满 30 秒执行一次 `ExportQbufPoolStats()`:

```
PrintStatsMgr::ProcessStats()           — 每个监控 tick (默认 10s)
    +-- ExportQbufPoolStatsTick()       — steady_clock 节流, 距上次 ≥30s 才执行
         +-- ExportQbufPoolStats()      — statistics.cpp
              |-- umq_qbuf_pool_info_get() + umq_tiny_qbuf_pool_info_get()  — 取数
              |-- umq_qbuf_pool_stats_to_str()                             — 32KB 文本格式化
              +-- 写 ubsocket_qbuf.txt + ArchiveQbufTxt 检查轮转            — 落盘
```

> 实际落盘点 = 每满 30s 后的第一个监控 tick (默认监控周期 10s 时精确 30s; 若 `UBSOCKET_MONITOR_INTERVAL > 30s`, 落盘间隔随之变大)。文件命名 / 轮转 / 路径等配置见 §1.5.4。

#### 2.4.5 跨节点诊断场景

| 症状 | 检查 | 结论 |
|------|------|------|
| A 端发送失败, TX-STAT `enobufs_all > 0` | A 端 qbuf 池水位到顶的时段 (30s 快照) | 池容量不足, 本端问题; 对比 per-SC 找出先耗尽的 size-class |
| A 端 `enobufs` 与 B 端 `rxe_fc` 同时出现 | 两端 qbuf 池水位 + 流控桶交叉 | 接收端 B 池耗尽 → 回复 credit 慢 → 发送端 A 池积压, 根因在 B 端 |
| 进程内存持续增长 | 池在用数 (in use) 只增不减 | qbuf 泄漏; 按 per-thread TLS 缓存分解定位泄漏线程 |
| 延迟毛刺周期性出现 | tiny 池水位波动 (30s 时间序列) | 小包风暴耗尽 tiny 池, 分配走慢路径 |
| 正常稳态 | 池水位平稳, available 充裕 | 池配置合理, 排除池维度嫌疑 |

**两端配合**: qbuf 池为**节点内共享** (非 per-链路), 单端查询即覆盖该进程全部链路。A 端池耗尽会表现为 A 端 TX-STAT `enobufs` 桶增长, B 端池耗尽会表现为 A 端 TX-STAT `eagain` / `timeout` 桶增长 (B 端回复 credit 慢) — 池水位快照与两端 TX/RX 错误桶交叉可定位池耗尽发生在哪一端。

---

## 3. 影响性分析

### 3.1 内存影响

以下从"统计开启"与"统计关闭"两个角度对比, 以 N=40000 (4 万 socket 连接) 为满载场景。统计默认开启, 关闭时 per-socket 计数器为 `nullptr`, 不申请内存。

#### StatsMgr

| 对比项 | 统计未开启 | 统计开启 | 本次调整变化 |
|--------|-----------|---------|-------------|
| 全局内存 | 12B (连接计数 atomic 始终存在) | 12B | -60B (删除 6 个 `atomic<uint64_t>`) |
| per-socket 内存 | 0 (`m_recorder_vec` 为 nullptr) | 72B (9 槽 Recorder, 堆分配) | +24B (新增 3 槽 bigdata 计数, 6→9 槽) |
| N=40000 总量 | ≈ 12B | ≈ 2.7MB | 未开启时节省 2.7MB; 开启时新增 960KB (bigdata 槽) |

#### TxStatReporter

| 对比项 | 统计未开启 | 统计开启 | 本次调整变化 |
|--------|-----------|---------|-------------|
| 全局内存 | 0 | 0 | -640B (删除原全局变量) |
| per-socket 内存 | 0 (`tx_stat_counters_` 为 nullptr) | 60B (堆分配) | 从内嵌改为指针惰性分配, 未开启时 0 |
| N=40000 总量 | 0 | ≈ 2.3MB | 未开启时 0; 开启时 +2.3MB |

#### RxStatReporter

| 对比项 | 统计未开启 | 统计开启 | 本次调整变化 |
|--------|-----------|---------|-------------|
| 全局内存 | 0 | 0 | 0 |
| per-socket 内存 | 0 (`rx_stat_counters_` 为 nullptr) | 60B (堆分配) | 全新功能, 未开启时 0 |
| N=40000 总量 | 0 | ≈ 2.3MB | 未开启时 0; 开启时 +2.3MB |

#### QbufPoolStats

| 对比项 | 统计未开启 | 统计开启 | 本次调整变化 |
|--------|-----------|---------|-------------|
| 全局内存 | 0 | 0 | 0 (无计数器, 统计在 UMQ 侧) |
| per-socket 内存 | 0 | 0 | 全局池级统计, 无 per-socket 结构 |
| 临时内存 | 无 (后台线程关闭) | 32KB (后台线程栈上格式化缓冲, 用完即弃) | CLI 响应结构 `CLIQbufPoolStatsData` 仅请求期间存在 |
| N=40000 总量 | 0 | ≈ 0 | 与 socket 数量无关 |

#### 四者合计

| 对比项 | 统计未开启 | 统计开启 | 说明 |
|--------|-----------|---------|------|
| 全局内存 | 12B | 12B | 连接计数始终存在 |
| per-socket 内存 | **0** | 192B (72 + 60 + 60) | 未开启时全部 nullptr; StatsMgr 9 槽 + TxStat 60B + RxStat 60B (QbufPoolStats 无 per-socket 结构) |
| N=40000 总量 | **≈ 12B** | ≈ 7.5MB | 未开启时仅 12B, 开启时 7.5MB, 其中 bigdata 新增 960KB |

### 3.2 热路径性能影响

统计默认开启。关闭时 (`UBSOCKET_MONITOR_ENABLE=off`) 所有热路径宏通过 `nullptr` 判断短路返回, **零开销**。

#### StatsMgr

| 对比项 | 统计未开启 | 统计开启 |
|--------|-----------|---------|
| 正常路径 (收发包/字节) | 0 (`m_recorder_vec == nullptr` 短路) | <1ns (per-socket `Recorder::m_cnt +=`, 非 atomic) |
| bigdata ctrl 接收路径 | 0 (`UBS_MONITOR_ENABLE` 短路) | <1ns (1 次 nullptr 判断 + 1 次 `m_cnt += 1`) |
| bigdata read 提交路径 | 0 (同上短路) | <1ns (复用已有 `umq_sock` 变量, 1 次 nullptr 判断 + 1 次 `m_cnt += 1`) |
| bigdata ctrl 发送路径 (READ_OFFER) | 0 (同上短路) | <1ns (复用已有 `ctx.umq_sock`, 1 次 nullptr 判断 + 1 次 `m_cnt += 1`) |
| bigdata ctrl 发送路径 (DONE/ABORT) | 0 (同上短路) | <1ns (复用已有 `umq_sk` 变量, 1 次 nullptr 判断 + 1 次 `m_cnt += 1`) |
| 连接建立/断开 | ~5ns (全局 atomic, 始终生效) | ~5ns (同左, 连接计数不受开关影响) |
| CLI 查询 | 返回空 | 遍历 `ArraySet<Socket>` 读 `Recorder::GetCnt()`, 仅 CLI 请求时触发 |
| KPI JSON 聚合 | 无 (后台线程关闭) | 遍历 `ArraySet<Socket>` 聚合 per-socket Recorder, 仅后台线程周期触发 |
| 统计快照导出 | 无 (后台线程关闭) | 遍历 `ArraySet<Socket>` 采集前三种统计, 仅后台线程周期触发 |
| qbuf 池统计导出 | 无 (后台线程关闭) | 每 30s 取数一次 (QbufPoolStats, 见 §2.4.4), 仅后台线程周期触发 |
| 跨核竞争 | 无 | 无 (收发计数 per-socket, 连接计数低频) |

#### TxStatReporter

| 对比项 | 统计未开启 | 统计开启 |
|--------|-----------|---------|
| 提交侧正常路径 (post 成功) | 0 | 0 (不计数) |
| 提交侧失败路径 (post 失败) | 0 (`tx_stat_counters_ == nullptr` 短路) | ~1ns (1 次 nullptr 判断 + 1 次 volatile ++) |
| 完成侧正常路径 (CQE 成功) | 0 | 0 (不计数) |
| 完成侧失败路径 (CQE 异常, sock 已知) | 0 (同上短路) | ~1ns |
| 完成侧失败路径 (CQE 异常, 全局轮询需反查 sock) | 0 (同上短路) | ~11ns |
| 跨核竞争 | 无 | 无 (per-socket 计数) |

#### RxStatReporter

| 对比项 | 统计未开启 | 统计开启 |
|--------|-----------|---------|
| Poll 阶段正常路径 | 0 | 0 (不计数) |
| Poll 阶段失败路径 | 0 (`rx_stat_counters_ == nullptr` 短路) | ~1ns |
| CQE 处理正常路径 | 0 | 0 (不计数) |
| CQE 处理失败路径 | 0 (同上短路) | ~1ns |
| 数据提取正常路径 | 0 | 0 (不计数) |
| 数据提取失败路径 | 0 (同上短路) | ~1ns |
| 跨核竞争 | 无 | 无 (per-socket 计数) |

#### QbufPoolStats

| 对比项 | 统计未开启 | 统计开启 |
|--------|-----------|---------|
| 热路径 (收发/提交/完成) | 0 (无埋点) | 0 (无埋点, 统计在 UMQ 侧) |
| 后台线程开销 | 无 (后台线程关闭) | 每 30s 一次: 2 次池级 `info_get` (UMQ 侧原子读 + 快照) + 1 次 32KB 文本格式化 + 1 次追加写盘, 微秒~毫秒级 |
| CLI 查询 | 无 | CLI epoll 线程执行, 不触及数据面 |
| 跨核竞争 | 无 | 无 (后台线程单线程取数) |

#### 四者合计

| 对比项 | 统计未开启 | 统计开启 | 增量 |
|--------|-----------|---------|------|
| 正常路径开销 | **0** | <1ns (StatsMgr 收发计数 + bigdata 计数) | <1ns |
| 失败路径开销 | **0** (nullptr 短路) | ~1ns (per-socket volatile ++) | ~1ns |
| 跨核 cache line bounce | 无 | 无 (全部 per-socket) | 消除 |
| CLI 查询对数据面影响 | 无 | 无 (仅 CLI 请求时触发) | 0 |
| 后台线程对数据面影响 | 无 | 无 (QbufPoolStats 取数在后台线程执行, 不触及热路径) | 0 |

### 3.3 性能基准测试

在双节点 UB 环境 (Kunpeng 920, URMA bonding) 下, 使用 brpc UB echo 性能测试工具进行 3 轮 monitor 开/关对比测试, 每轮 30 秒, 单连接 queue_depth=10, req_size=100KB。

**测试环境**:
- Server: 141.62.33.71, 16 threads, UB native data-plane
- Client: 141.62.33.45, 1 thread, queue_depth=10
- 两轮之间等待 10s 释放 UB jetty 资源
- 通过 brpc gflag `--ubsocket_monitor_enable=true/false` 控制 server 和 client 的 monitor 开关

**测试结果 (3 轮)**:

| 轮次 | Mode | QPS | Avg-Latency (us) | p99 (us) | p999 (us) | Throughput (MB/s) | Total-Req | Errors |
|------|------|-----|------------------|----------|-----------|-------------------|-----------|--------|
| R1 | ON  | 999.7 | 87 | 122 | 143 | 97.62 | 30001 | 0 |
| R1 | OFF | 1000.0 | 92 | 137 | 158 | 97.65 | 29999 | 0 |
| R2 | ON  | 999.8 | 91 | 138 | 163 | 97.64 | 30005 | 0 |
| R2 | OFF | 999.8 | 86 | 124 | 150 | 97.63 | 30003 | 0 |
| R3 | ON  | 999.8 | 82 | 116 | 143 | 97.64 | 30005 | 0 |
| R3 | OFF | 999.8 | 83 | 123 | 151 | 97.64 | 30005 | 0 |

**统计分析**:

| 指标 | ON 平均 | OFF 平均 | 差异 |
|------|---------|----------|------|
| QPS | 999.8 | 999.9 | -0.01% (噪声) |
| Avg-Latency | 86.7us | 87.0us | -0.3% (噪声) |
| p99 | 125.3us | 128.0us | -2.1% (噪声) |
| p999 | 149.7us | 153.0us | -2.2% (噪声) |
| Throughput | 97.63MB/s | 97.64MB/s | -0.01% (噪声) |

**结论**: monitor 开启与关闭相比, QPS、平均延迟、p99/p999 延迟、吞吐量均在正常波动范围内, **statistics 统计对 UB native 热路径性能无可观测影响**, 与 §3.2 理论分析一致 (<1ns 增量相对微秒级 UMQ 操作, 占比 <0.1%)。