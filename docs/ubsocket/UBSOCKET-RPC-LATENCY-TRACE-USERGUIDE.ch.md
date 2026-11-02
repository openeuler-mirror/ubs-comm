# RPC 全链路时延打点工具使用手册

> 适用版本：brpc + ubs-comm（UB-native 路径，`BRPC_WITH_URMA`）
> 设计文档：`UBSOCKET-RPC-LATENCY-TRACE-DESIGN.ch.md`
> 手册目标：指导用户完成「开启打点 → 采集日志 → 离线分析 → 解读结果」全流程。

---

## 一、工具能回答什么问题

| 层级 | 问题 | 指标 |
|------|------|------|
| 逐 RPC | 这个 RPC 的通信栈总耗时多少？ | `link_time`（时钟偏移无关，无需对时） |
| 逐 RPC | 服务端处理（解析+业务+序列化）占多少？ | `server_prc` |
| 逐包分段 | 慢 RPC 的请求/响应各由哪几个 UMQ SN 承载？ | req/rsp SN 列表 |
| 逐包分段 | 段交付散布多大（wire/排队）？ | `req_span` / `rsp_span` |
| 逐包分段 | ubs_poll 交付 → brpc 切包之间耗多少？ | `req_tail` / `rsp_tail` |
| 逐包分段 | 扣除尾部后纯链路残差？ | `wire_resid` |
| 阶段事件 | ubsocket/UMQ 各阶段逐事件耗时分布？ | Stage 聚合表（TrySenderPost→…→delivered） |
| 阶段事件 | 单次 RDMA READ fabric 时延多少？ | `UmqPostRead->ReadCqe`（同端同钟，精确） |
| 阶段事件 | 慢 RPC 具体慢在哪个阶段？ | `--top N` 逐 RPC 四方向瀑布图 |

**四点差值法原理**：记录 client 侧 T1（req 发送入口）/ T2（rsp 到达切包）、server 侧 T3（req 到达切包）/ T4（rsp 发送入口），则

```
e2e        = T2 - T1              (client 时钟)
server_prc = T4 - T3              (server 时钟)
link_time  = e2e - server_prc     (时钟偏移 theta 自动抵消，双端无需时钟同步)
```

**字节偏移桥梁**：brpc 与 ubsocket 对同一条 UB 连接计数的是同一字节流（双方游标均自连接建立从 0 累计），因此按 `fd + byte_cursor` 即可把 cid 映射到承载它的 SN 集合，实现逐包归因，wire 协议零改动。

---

## 二、组件与产物文件

| 组件 | 位置 | 产物 |
|------|------|------|
| brpc 四点打点 | `brpc/src/brpc/rpc_link_trace.{h,cpp}` + 4 个打点点 | `/tmp/brpc/rpc_trace_<pid>.log` |
| ubsocket 逐包打点 | `ubscomm/src/ubsocket/csrc/profiling/trace/ubs_pkt_trace.{h,cpp}` | `/tmp/ubsocket/ubs_pkt_trace_<pid>.log` |
| 离线 join 工具 | `ubscomm/tools/trace/join_rpc_trace.py` | 终端报告 + 可选 CSV |

注意：client 进程的 `rpc_trace_<pid>.log` 和 `ubs_pkt_trace_<pid>.log` 来自**同一 pid**（ubsocket 是 brpc 的传输层，同进程），server 端同理。fd 仅在同进程日志间作 join 键。

---

## 三、编译前提

1. brpc 需以 `BRPC_WITH_URMA` 构建（四点打点与字节游标均在此宏内）。
2. ubsocket 无特殊构建开关；aarch64 下 `ENABLE_CPU_HARDWARE_ACCELERATION` 开/关均可（逐包时间戳恒用 `CLOCK_MONOTONIC`，与 brpc 时钟域一致）。
3. join 工具为纯 Python 3 脚本，无第三方依赖。

---

## 四、快速开始

### 1. 双端开启打点并启动应用

```bash
# client 与 server 进程都要设置
mkdir -p /tmp/brpc                       # brpc 侧不会自动建目录
export UBS_PKT_TRACE_ENABLE=1            # ubsocket 逐包打点（默认关）

# 应用启动参数（gflags）
./server -rpc_link_trace_enable -rpc_link_trace_dir=/tmp/brpc &
./client -rpc_link_trace_enable -rpc_link_trace_dir=/tmp/brpc &
```

### 2. 跑流量

建议覆盖目标场景至少数十秒；ring 缓冲默认每线程 65536 条，brpc 侧 1s 一刷、ubs 侧 10ms 一刷，突发丢最旧记录。

### 3. 收集日志

```bash
/tmp/brpc/rpc_trace_<pid_c>.log          # client 四点记录 (T1/T2)
/tmp/brpc/rpc_trace_<pid_s>.log          # server 四点记录 (T3/T4)
/tmp/ubsocket/ubs_pkt_trace_<pid_c>.log  # client ubs 逐包记录（响应方向）
/tmp/ubsocket/ubs_pkt_trace_<pid_s>.log  # server ubs 逐包记录（请求方向）
```

### 4. 离线 join 分析

```bash
python3 join_rpc_trace.py \
    --client     rpc_trace_<pid_c>.log \
    --server     rpc_trace_<pid_s>.log \
    --client-pkt ubs_pkt_trace_<pid_c>.log \
    --server-pkt ubs_pkt_trace_<pid_s>.log \
    --top 20 --csv result.csv
```

只做四点 join（不带逐包归因）时省略两个 `-pkt` 参数即可。

### 5. 生成时延流图 SVG

`gen_flowchart.py` 自动调用 `join_rpc_trace.py` 解析四点 trace + 逐包 trace 日志，生成全链路时延分解 SVG 流图。脚本位于 `tools/trace/gen_flowchart.py`，与 `join_rpc_trace.py` 配套使用。

#### 基本用法

```bash
python3 tools/trace/gen_flowchart.py \
    --client-rpc  rpc_trace_<pid_c>.log \
    --server-rpc  rpc_trace_<pid_s>.log \
    --client-pkt  ubs_pkt_trace_<pid_c>.log \
    --server-pkt  ubs_pkt_trace_<pid_s>.log \
    --output      rpc_latency_flowchart.svg \
    --title-suffix "1MB"
```

#### 带 PROF dump（可选）

传入 `--prof-server` / `--prof-client` 可填充 brpc 序列化/反序列化等阶段的实测值。需在测试时设 `UBSOCKET_PROF_ENABLE=true`，prof dump 间隔 1 分钟，测试需延长到 65s+。

```bash
python3 tools/trace/gen_flowchart.py \
    --client-rpc  rpc_trace_<pid_c>.log \
    --server-rpc  rpc_trace_<pid_s>.log \
    --client-pkt  ubs_pkt_trace_<pid_c>.log \
    --server-pkt  ubs_pkt_trace_<pid_s>.log \
    --prof-server ubsocket_profiling_<pid_s>.log \
    --prof-client ubsocket_profiling_<pid_c>.log \
    --output      rpc_latency_flowchart.svg
```

#### 自动布局选择

脚本根据 stage 事件种类自动选择布局，无需手动指定：

| 布局 | 触发条件 | 阶段数 | 说明 |
|------|---------|--------|------|
| bigdata | 存在 DoReadOffer/FinalizeIo 等 READ_OFFER 专有 stage | 18 | 四列完整流图，含 READ_OFFER rendezvous 路径 |
| 小包 | 无 READ_OFFER 专有 stage | 12 | 简化流图，无 DoReadOffer/post read/FinalizeIo |

#### SVG 结构

- **4 列阶段流图**：Client 发送 → Server 接收 → Server 发送 → Client 接收，每阶段标注实测时延
- **实测时延分解**：server 处理 (T4-T3) + 通信栈 (link_time)，恒等分解不做比例分摊
- **RPC 四点时延分解**：e2e / server_prc / link_time / wire_resid 的 avg/P50/P99/P999
- **虚线列边框**（bigdata 布局）：4 列各有虚线框，视觉分组

#### 显示语义

| 标注 | 含义 |
|------|------|
| `X.Xus` | 该阶段有实测数据（来自 stage 事件或 PROF dump） |
| `未采集` | 该路径无打点，不做比例分摊伪造数据 |
| `含在上框` | group-last 阶段（如 post send）无独立 outgoing pair，其耗时已包含在上一阶段的实测值中 |

#### 数据来源

| 数据 | 文件 | 用途 |
|------|------|------|
| brpc 四点 trace | `rpc_trace_<pid>.log` | T1/T2/T3/T4 四点时延 |
| ubsocket 逐包 trace | `ubs_pkt_trace_<pid>.log` | packet delivery + S 行 stage 事件，per-endpoint 阶段实测 |
| ubsocket PROF | `ubsocket_profiling_<pid>.log` | 可选，brpc 序列化/反序列化子阶段精度分解 |

---

## 五、开关参数参考

### brpc（gflags）

| 参数 | 默认 | 说明 |
|------|------|------|
| `-rpc_link_trace_enable` | false | 总开关，未开时零开销 |
| `-rpc_link_trace_dir` | `/tmp/brpc` | 日志目录（需预先创建） |
| `-rpc_link_trace_flush_ms` | 1000 | ring → 磁盘刷盘周期 |
| `-rpc_link_trace_ring_size` | 65536 | 每线程 ring 容量（2 的幂；溢出丢最旧） |

### ubsocket（环境变量）

| 变量 | 默认 | 说明 |
|------|------|------|
| `UBS_PKT_TRACE_ENABLE` | 关 | `1` 或 `true` 开启；进程内首次读取后缓存，运行中不可切换 |

ubs 侧固定参数：目录 `/tmp/ubsocket`（自动创建），刷盘周期 10ms，ring 65536/线程。

---

## 六、日志格式

### rpc_trace_<pid>.log（TSV，6 列）

```
point  cid  port  fd  ts_ns  byte_cursor
```

| 列 | 说明 |
|----|------|
| point | 1=T1 client 发送入口；2=T2 client 响应到达；3=T3 server 请求到达；4=T4 server 发送入口 |
| cid | correlation_id（双端 join 键） |
| port | 本地端口（client 临时端口可区分连接；server 为监听端口） |
| fd | socket fd（与 ubs_pkt_trace 同进程 join 键） |
| ts_ns | 时间戳，`CLOCK_MONOTONIC` 纳秒 |
| byte_cursor | 连接级 RX 累计字节（仅 T2/T3 有效，T1/T4 为 0） |

### ubs_pkt_trace_<pid>.log（TSV，P 行 4 列 + S 行 5/6 列，同文件混排）

```
fd  sn  byte_cursor  ts_ns                # P 行：ubs_poll 逐段交付记录
S   stage_id  fd  first_sn  ts_ns          # S 行：单 SN 阶段事件
S   stage_id  fd  first_sn  ts_ns  sn_count  # S 行（6 列）：range 事件，覆盖 [first_sn, first_sn+sn_count)
```

P 行列说明：

| 列 | 说明 |
|----|------|
| fd | 与 rpc_trace 同进程 join 键 |
| sn | 连接级 UMQ SN（imm.user_data；bigdata 同 offer 所有 fragment 共享 first_sn） |
| byte_cursor | 连接级累计交付字节（含本段） |
| ts_ns | 交付时间戳，`CLOCK_MONOTONIC` 纳秒 |

S 行列说明（bigdata 路径 10 个阶段入口 + 小包路径补齐）：

| 列 | 说明 |
|----|------|
| stage_id | 1=TrySenderPost 2=FlushPendingOffer 3=UmqPostSend 4=ReadCqe 5=HandleRxCtrl 6=DoReadOffer 7=UmqPostRead 8=FinalizeIo 9=DeliverToRxQ 10=SendSimpleCtrl 11=RxCqeData（SMALL_DATA 的 RX CQE 收割点） |
| fd / first_sn | 组键 `(fd, first_sn)`；first_sn 为 per-offer 首 SN（TX 侧为 `LoadSeqNum()` 预读）；RxCqeData 的 key 为段 SN |
| ts_ns | 阶段入口时间戳，`CLOCK_MONOTONIC` 纳秒 |
| sn_count | 仅 6 列行：range 事件覆盖 `[first_sn, first_sn+sn_count)` 全部 SN（当前仅 UmqPostSend：KeepWrite 合批一批一条记录，批内每个 buf 严格 1 个 SN 连续分配）；缺省/0 = 单 SN 事件 |

join 匹配规则：`first_sn ∈ rpc.sn_set`（成员判定；一个 RPC 可跨多个 offer）；**exact 事件按 stage id 方向过滤**（TX 组仅收 1-3，RX 组仅收 4-11——同 fd 双向 SN 空间数值必然重叠，不过滤会伪造跨方向段），range 事件按区间成员匹配（`sn ∈ [first_sn, end_sn]`，双 bisect O(log n)），**仅 TX 组参与**——RX 组不吸收 range 事件。req 方向阶段事件经 fd 桥双日志配对：TX 侧（1/2/3）在 client 日志、RX 侧（4-11）在 server 日志，rsp 方向对称；SN 空间按方向独立，绝不跨方向匹配。段时长 = 同 `(fd, first_sn)` 组内按 ts 排序后相邻事件差；RX 组末尾由同 key P 行交付时刻收尾（`->delivered` 唤醒段）。批内非批首 SN 的 TX 组只有 range 单事件、无相邻对，不产段时长（`TrySenderPost->UmqPostSend` 段归批首 offer，不重复计数）。

join 工具向下兼容 4 列（无 byte_cursor）与 5 列（无 fd）的旧 rpc_trace 日志：4/5 列时逐包归因自动跳过并计数提示。

---

## 七、输出解读

### 汇总区（单位 us；nseg 单位为 segs）

```
e2e        : 端到端 RPC 时延 (T2-T1)
server_prc : 服务端处理 (T4-T3)
link_time  : 通信栈耗时 = e2e - server_prc
req_nseg   : 请求承载段数          req_span : 请求段首→尾交付散布 (server 时钟)
req_tail   : 请求末段交付→T3 切包  (server 时钟)
rsp_nseg/rsp_span/rsp_tail : 响应方向对应值 (client 时钟)
wire_resid : link_time - req_tail - rsp_tail （纯 wire+排队残差，时钟无关）
```

链路时间近似三分解：`link_time ≈ wire_resid + req_tail + rsp_tail`。
定位慢 RPC 时按此归因：tail 大 → ubs 交付到 brpc 切包之间（读缓冲滞留/切包调度）；wire_resid 大 → wire 传输与 UMQ/URMA 排队；span 大 → 段间到达散布（流控/乱序重组）。

### --top N 慢 RPC 明细

按 `link_time` 降序列出 Top N，并附该 RPC 的请求/响应 SN 列表（超过 8 个折叠显示），可直接拿去对照 UMQ/SplitTrace 侧记录。

有阶段事件时每个慢 RPC 追加**四方向瀑布图**（Client Send / Server Recv / Server Send / Client Recv），按 offer 分组逐事件列出相对偏移与段时长，`[READ wire]` 标注实测 RDMA READ fabric 段。

### 阶段聚合表（Stage segments，有 S 行时输出）

逐事件对（per event-pair）统计：每行 = 组内相邻两阶段（或末阶段→delivered）的时长分布（n/avg/p50/p90/p99/p999/max）。特殊行：

- `UmqPostRead->ReadCqe  [READ wire]`：单次 RDMA READ fabric 时延，同端同钟，无跨端时钟问题——**逐包级新产出**。多 WR offer（per-WR CQE）口径：该值仅覆盖**首个 CQE**，后续 CQE 间隔另见 `ReadCqe->ReadCqe` 行（CQE 到达散布）；单 WR offer 不受影响。
- `RxCqeData->delivered`：SMALL_DATA 的 RX CQE 收割 → ubs_poll 交付的唤醒段（rxQueue 等待 + 唤醒 + brpc 读调度），小包路径的 RX 侧主要观测点。
- `SendSimpleCtrl->delivered`：末阶段 → ubs_poll 交付的唤醒段。
- `stage_total`：per-RPC 全部阶段段之和（多 offer 求和）。
- `unattr`：`link_time − stage_total` = 跨节点 OFFER SEND 腿 + brpc 内部间隙（T1→TX 入口等）残差。

### --csv

逐 RPC 一行：`cid,e2e_us,server_prc_us,link_us,req_nseg,req_span_us,req_tail_us,rsp_nseg,rsp_span_us,rsp_tail_us,wire_resid_us,stage_total_us,unattr_us`（无阶段事件时末两列为空）。

---

## 八、读数规则（sanity check）

工具输出内建三条校验，异常计数应接近 0：

1. `link_time < 0` — theta 已抵消，链路时间物理上必为正；持续为负说明打点或 join 有 bug。
2. `req_tail < 0` / `rsp_tail < 0` — 切包时刻必然不早于末段交付时刻；为负说明时钟域混用或日志错配（重点核对 rpc_trace 与 ubs_pkt_trace 是否同 pid）。
3. dropped 计数（incomplete/only_client/only_server）升高 — 多为 ring 溢出丢记录，调大 `-rpc_link_trace_ring_size` 或缩短采集窗口。

---

## 九、开销评估

| 位置 | 开销 |
|------|------|
| brpc T1/T4 | 1 次 `CLOCK_MONOTONIC` vDSO 读 + 40B ring 写 / RPC |
| brpc T2/T3 | 复用 `msg->received_us()`，仅 40B ring 写 / RPC |
| ubs 交付点 | 1 次 vDSO 读 + 32B ring 写 / 段 |
| ubs 阶段事件 | 1 次 vDSO 读 + ring 写 / 阶段，每 offer 约 10 次（与逐包打点同 ring、同开关） |

均为 per-thread 无锁 ring + 独立 flush 线程，热路径无磁盘 IO；开关默认关闭，关闭时零开销（一次布尔判断）。

---

## 十、注意事项与已知近似

1. **目录**：brpc 日志目录需预先 `mkdir -p`（默认 `/tmp/brpc`）；ubs 目录自动创建。
2. **运行中不可切换**：两侧开关都在进程内缓存，需重启进程生效。
3. **段跨界近似**：一个段横跨两条消息时整体归因于后一条（对逐包诊断影响可忽略）。
4. **fd 复用**：同一日志文件覆盖 fd 关闭又复用的场景时，桥接可能错配；短采集窗口内概率极低。
5. **降级路径**：连接降级（非 UB-active）后 brpc 侧不再打点，ubs 侧游标与 brpc 游标停止对齐，该连接后续记录不参与归因属预期行为。
6. **bigdata 路径**：READ_OFFER 大数据最终同样经 `ubs_poll` 统一交付，天然覆盖，无需额外打点。
7. **阶段事件覆盖范围**：bigdata（>64KB）路径有完整 10 阶段；小消息（≤64KB）走 SMALL_DATA 内联路径，无 offer，阶段 2/6-10 无事件（属正常）——其 TX 由 `UmqPostSend` range 事件覆盖（见下条），RX 由 `RxCqeData->delivered` 段覆盖。
8. **TX 侧多 offer 批次**：`TrySenderPost`/`FlushPendingOffer` 以批首 offer 的 first_sn 记录，同批后续 offer 仅有 `FlushPendingOffer` 事件；`UmqPostSend` 打 **range 事件**（`sn_count` = 本次实际 post 上链的 SN 数，不含被 `UMQ_BATCH_SIZE`/SQ 额度裁剪回滚的尾部），批内每个 RPC 的 TX 归属精确到 SN，但非批首 SN 只有 `UmqPostSend` 单点、无 1→2→3 段时长（该段归批首 offer，其值 = 整批 post 窗口）。FC backpressure 导致 `umq_post` 部分接受时，该批 `UmqPostSend` 事件整体缺失（与 PROF 口径一致，属降级不属错误）。`RxCqeData` 仅覆盖 shared-JFR 主 umq RX 路径，sub-umq RX 部署下小包 RX 段无此事件。
9. **阶段时长语义**：阶段事件仅入口单时间戳，段时长 = 到下一阶段的间隔，语义为"阶段处理 + 排队"合量。多 WR offer 的 `[READ wire]` 仅覆盖首个 CQE，CQE 散布看 `ReadCqe->ReadCqe` 行。

---

## 十一、故障排查

| 现象 | 排查 |
|------|------|
| `rpc_trace_<pid>.log` 未生成 | 是否带 `-rpc_link_trace_enable`；`/tmp/brpc` 是否已创建；是否 `BRPC_WITH_URMA` 构建且连接为 UB-active |
| `ubs_pkt_trace_<pid>.log` 未生成 | `UBS_PKT_TRACE_ENABLE=1` 是否在进程启动前导出；流量是否走 `ubs_poll` 交付（UB 模式） |
| joined RPCs = 0 | 双端日志是否同一次压测、cid 是否一致；ring 是否溢出 |
| req/rsp 段全为空且 `msgs_without_fd` 大 | rpc_trace 是 5 列旧格式（无 fd），需升级 brpc 侧到 6 列 |
| tail 普遍为负或大得离谱 | rpc_trace 与 ubs_pkt_trace 是否同 pid；ubs 侧是否本补丁版本（恒 `CLOCK_MONOTONIC`） |
| `tail_segs_inflight` 很大 | 采集窗口末尾的在途字节，属正常；持续偏大说明 brpc 消费滞后于交付 |

---

## 十二、自测

无真实环境时可用合成数据验证 join 逻辑（45 项断言，含阶段事件瀑布图、range 事件与方向隔离、旧格式兼容）：

```bash
python3 ubscomm/tools/trace/test_join_rpc_trace.py
```
