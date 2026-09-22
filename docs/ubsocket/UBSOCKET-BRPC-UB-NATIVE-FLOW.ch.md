# bRPC use_ub_native 模式请求/响应流程分析

> 适用版本：brpc-ybx + ubs-comm-ybx（含归一 IO 轮询设计 v2.4）
> 分析范围：客户端发送 → 服务端接收 → 服务端响应 → 客户端接收

---

## 前置条件与路由阈值

### 启用条件

`use_ub_native` 需满足以下全部条件：

| 条件 | 来源 |
|------|------|
| `use_ub = true` | `ChannelOptions` / `ServerOptions` |
| `SSL_OFF` | `socket.cpp:811` 校验 |
| `RDMA_OFF` | `socket.cpp:813` 校验 |
| 编译时 `BRPC_WITH_URMA` | CMake 条件编译 |

### 路由阈值（核心分叉点）

```
UBS_SMALL_DATA_MAX = 4064 字节     （ubsocket_proto.h:41）
```

- **≤ 4064B** → SMALL_DATA SEND（inline 推送，数据随控制帧一起发出）
- **> 4064B** → READ_OFFER rendezvous（发送方暴露内存，接收方主动 RDMA READ 拉取）

阈值的设计依据：一个 UB 控制帧的最大 wire 长度即 4064B，inline 数据在此范围内可塞进单帧发出；超出此值则切换为 RDMA READ 语义，数据本体不经网络帧搬运，零拷贝由硬件完成。

### brpc 写入入口（两条路径共用）

```
Channel::CallMethod
  └─ 序列化 request → IOBuf（baidu_std: [4B magic][header PB][body PB]）
  └─ Socket::Write(req)
       ├─ 无积压（单次写，socket.cpp:1831）→ DoUbsNativeWrite(data_arr[1], 1)
       └─ 有积压（KeepWrite，socket.cpp:2026）→ DoUbsNativeWrite(data_list, ndata)

DoUbsNativeWrite:
  IOBuf*[] → ubs_data_list_t（{iov_base, iov_len} 数组）
  → UbsBigdata::TrySenderPost(fd, &data_list, &out)
       ├─ 返回 true  → UB-native 路径已处理，使用 out 作为返回字节数
       └─ 返回 false → 回退普通 writev（非 UMQ socket 时）
```

---

## 第一章：1K 路径（≤ 4064B，SMALL_DATA SEND）

### 1.1 概述

1K 路径是纯推送语义。发送方将 IOBuf Block 的物理地址直接交给 UMQ，NIC 读取后 inline 在网络帧中发出；接收方 RX CQE 到达时数据已经在本端内存中，直接解帧交给 brpc，全程无额外 RDMA READ。

---

### 1.2 发送侧流程

#### TrySenderPost 路由决策

```
TrySenderPost(fd, data_list, out):
  for each segment s（s.len ≤ 4064B）:
    HandleSmallSegment(ctx, s)

HandleSmallSegment(ctx, seg):
  BuildSmallData(seg):
    umq_buf_alloc(without_data=true)
    │   ↑ 分配 qbuf 元数据，不额外分配 data buffer
    qbuf->buf_data  = seg.iov_base      ← 零拷贝：直接指向 IOBuf Block 的 UMQ 注册内存
    block->IncRef()                      ← Pin Block，SEND 完成前禁止释放
    qbuf->pro->opcode   = UMQ_OPC_SEND_IMM
    qbuf->pro->imm_data = small_data_marker
    qbuf->data_size     = seg.len        ← ≤4064B，inline
```

#### umq_post 提交与唤醒

```
umq_post(umqh, qbuf, &opt, &bad)
  → SEND WR 写入 SQ（Send Queue）

NotifyPosted():
  sleeping_.load(acquire) == true
    → eventfd_write(wake_fd_, 1)        ← 仅深睡时写，普通情况为一次原子 load
```

#### 硬件执行

```
本端 UB NIC 读取 qbuf->buf_data（IOBuf Block 物理内存）
  → 数据 inline 在 UB 网络帧中发出
  → 对端 NIC 将数据 DMA 写入其 RX buffer
  → 对端 RX CQ 写入 RECV CQE
```

#### 本端 TX CQE 收割（SEND 完成确认）

```
统一 IO 线程 TxSweepOnce():
  PollUmqTxInternal():
    umq_poll TX CQ → SEND 完成 CQE
    global_inflight_.fetch_sub(poll_num)     ← 在途计数按 CQE 数递减

  ProcessTxCqe(sock, qbuf):
    HandleTxCompletion → Branch 3（常规 SEND，非 big_ctrl）:
      DrainDeferredControls(state)
      RetryPendingReads(state, umqh, umq_sock)
      return false                            ← 交上层继续处理

    后续：
      block->DecRef()    ← 引用归零则释放 IOBuf Block
      umq_buf_free(qbuf)
```

---

### 1.3 接收侧流程

```
对端 JFCE（Joint Fabric Completion Event）到达
  → SHARE_JFR_RX_RUNNER 线程 epoll_wait 返回
  → ProcessOneEvent → SHARE_JFR 分支
  → ProcessMainUmqRearm()    ← rearm JFC，维持 armed 状态
  → RunUnifiedActiveLoop():
      RxPollQuantum():
        umq_poll RX CQ，得到 RECV CQE
        qbuf->pro->imm_data == small_data_marker（非 big_ctrl）
        → SiftSocketEventsWithUmqBuffers()
        → bthread_start_background(ProcessInputMessage)

brpc InputMessenger::OnNewMessages:
  CutInputMessage → 解析 baidu_std 帧
  → Handler::HandleRequest(controller, request, response, done)
  → 业务逻辑处理
```

1K 路径接收侧无 TX WR 提交、无 TX CQE 等待，RX CQE 到达即完成数据交付。

---

### 1.4 响应回复（服务端 → 客户端）

响应路径与请求路径完全对称：

```
done->Run()
  → SendRpcResponse → Socket::Write → DoUbsNativeWrite → TrySenderPost
  → 整个 response IOBuf ≤ 4064B → SMALL_DATA SEND
  → 客户端 RxPollQuantum 收到 RECV CQE，直接解帧回调
```

---

### 1.5 1K 路径时序总览

```
客户端发送线程          UB Fabric          服务端 IO 线程
───────────────────────────────────────────────────────
TrySenderPost
umq_post(SMALL_DATA)
NotifyPosted()
                  ──SEND──►   JFCE
                              RxPollQuantum
                              ProcessInputMessage
                              HandleRequest
                              done->Run()
                              umq_post(SMALL_DATA resp)
                  ◄──SEND──   对端 JFCE
RxPollQuantum
CutInputMessage
回调 done
SEND CQE → 本端 TX CQ                   SEND CQE → 服务端 TX CQ
TxSweepOnce: DecRef Block               TxSweepOnce: DecRef Block
```

---

### 1.6 EAGAIN 处理

```
TrySenderPost 提交失败（EAGAIN / ENOBUFS / ENOMEM）:
  return true, *out = -1, errno = EAGAIN
  → 上层 KeepWrite 自动重试（Socket 层标准回退路径）
```

---

## 第二章：100KB 路径（> 4064B，READ_OFFER rendezvous）

### 2.1 概述

100KB 路径采用 rendezvous 语义。发送方不直接传输数据，而是发出一个携带本端内存描述符（地址 + 长度 + lkey）的控制帧（READ_OFFER）；接收方收到控制帧后，向 UMQ 提交 READ WR，由本端 NIC 主动通过 RDMA READ 从发送方内存拉取数据。数据本体在整个过程中未经网络帧封装，零拷贝由硬件完成。

---

### 2.2 发送侧：READ_OFFER 控制帧构造与发送

#### TrySenderPost 路由决策

```
TrySenderPost(fd, data_list, out):
  for each segment s（s.len > 4064B）:
    HandleLargeSegment(ctx, s, i)

  FlushPendingOffer(ctx)    ← 封闭最后一批未满的 READ_OFFER
```

#### HandleLargeSegment：追加内存描述符

```
HandleLargeSegment(ctx, seg, i):
  在 ctx.pending_offer 中追加 ub_mempool_info_t:
    .addr   = seg.iov_base    ← 本端物理内存地址（已向 UMQ 注册）
    .length = seg.len
    .lkey   = <UMQ 分配的本端 lkey>

  封闭条件（满足任一即触发 FlushPendingOffer）:
    nsegs == UBS_SEG_MAX（32）
    wire_size > UBS_CTRL_BODY_MAX（4064B）
```

#### FlushPendingOffer：封闭并序列化控制帧

```
FlushPendingOffer(ctx):
  构造 UbsCtrlHdr（type=READ_OFFER, nsegs, seq, first_sn）
  序列化 nsegs 个 ub_mempool_info_t 到 qbuf->buf_data
  qbuf->pro->opcode   = UMQ_OPC_SEND_IMM
  qbuf->pro->imm_data = big_ctrl_marker    ← is_big_ctrl() == true
  加入 ctx.batch
```

#### umq_post 批量提交

```
所有 batch 成员链成 qbuf_next 链，一次 umq_post 提交:
  umq_post(umqh, head, &opt, &bad)
  → 仅发送控制帧（每帧 ≤4064B），100KB 数据本体留在发送方内存
  → NotifyPosted()
```

#### 本端 TX CQE 收割（控制帧 SEND 完成）

```
HandleTxCompletion → Branch 2（SEND_IMM + is_big_ctrl）:
  umq_buf_free(qbuf)        ← 释放控制帧 qbuf
  DrainDeferredControls(state)
  RetryPendingReads(state, umqh, umq_sock)
  return true               ← 不走 ProcessTxCqe 后续路径
```

控制帧 SEND 完成时，100KB 数据本体仍 pin 在发送方内存，等待接收方发起 RDMA READ。

---

### 2.3 接收侧第一阶段：READ_OFFER 处理与 READ WR 提交

#### RX 事件驱动唤醒

```
对端 JFCE 到达 → RxPollQuantum():
  umq_poll RX CQ → RECV CQE
  qbuf->pro->imm_data == big_ctrl_marker
  → UbsBigdata::HandleRxControl(sock, qbuf)
  → DoReadOffer(sock, umqh, qbuf)
```

#### DoReadOffer 详细流程

```
1. ParseReadOffer(qbuf, &ctrl_hdr, segs[]):
     解析 UbsCtrlHdr（seq, first_sn, nsegs）
     解析 nsegs 个 ub_mempool_info_t

2. 注册对端内存：
   for each seg:
     umq_remote_mempool_state_get(umqh, &seg.mempool_info, &state)
     match state:
       REUSE                    → 跳过（已注册，直接复用）
       NEED_IMPORT / NEED_REIMPORT:
         umq_mempool_info_set(umqh, &seg.mempool_info)
                                → 将对端内存注册到本端 UMQ，授权 DMA 直接访问
       ERR / VERSION_ROLLBACK:
         SendReadAbort(sock, seq); return

3. 分配 UbsBigIoCtx:
     ctx->offer_rx_buf = qbuf   ← 转移所有权，READ 完成前 pin 住
     ctx->wr_total = nsegs

4. 分配本地接收 buffer 并构造 READ WR：
   for each seg i:
     umq_buf_alloc(seg.length, 1, umqh, &dest_option)
                                → headroom = sizeof(Block)，placement new Block
     qbuf_wr->pro->opcode        = UMQ_OPC_READ
     qbuf_wr->pro->remote_addr   = seg.addr
     qbuf_wr->pro->remote_length = seg.len
     qbuf_wr->pro->remote_lkey   = seg.lkey
     wr_slots[i] = qbuf_wr

5. ConfigureOrderedReadCompletions(ctx):
     按 SN 设置 completion_span，保证乱序完成仍按序交付

6. ctx 加入 state->active_io

7. 链成 qbuf_next 链，提交 READ WR 链：
   umq_post(umqh, head, &opt, &bad)
   tx_ops->tx_queue_avail_num_.fetch_sub(nsegs)
   TxCqePoller::Instance().NotifyInflight()
   （在 RX 线程内执行，线程非深睡 → NotifyPosted 仅原子 load，不写 eventfd）
```

#### READ WR 提交时的 EAGAIN 处理

```
完全失败（nothing_submitted）:
  ctx 入 state->pending_reads 队列（上限 64 个）
  等下次 TX CQE 触发 RetryPendingReads

部分提交（bad != head && bad != nullptr）:
  ctx->next_post = submitted_wrs
  FinishPosting(ctx, partial=true)

RetryPendingReads 触发时机：
  HandleTxCompletion 的三个 branch 结束后均调用
  → 每次 CQE 收割后自动尝试排空 pending_reads 队列
```

---

### 2.4 硬件执行：RDMA READ

```
接收方 UB NIC 读取 wr_slots 中的 READ WR 描述符
  （remote_addr / remote_length / remote_lkey）

→ 通过 UB Fabric 向发送方 NIC 发出 RDMA READ 请求
→ 发送方 NIC 将本端物理内存 DMA 读出（零拷贝，CPU 零参与）
→ 数据直接写入接收方 qbuf_wr->buf_data
→ 接收方 TX CQ 写入 READ 完成 CQE
  （每 WR 一个 CQE，TX_REPORT_THRESHOLD=1，每 WR 独立完成）
```

---

### 2.5 接收侧第二阶段：READ CQE 收割与数据交付

```
统一 IO 线程 TxSweepOnce() → PollUmqTxInternal():
  umq_poll TX CQ → READ 完成 CQE
  global_inflight_.fetch_sub(poll_num)     ← 在途计数按 CQE 数递减

  ProcessTxCqe(sock, qbuf):
    HandleTxCompletion → Branch 1（UMQ_OPC_READ）:
      slot = qbuf->pro->user_ctx          → UbsBigQbufSlot*
      ctx  = slot->owner                  → UbsBigIoCtx*

      if qbuf->status != 0:
        ctx->failed = true

      span = slot->completion_span（有序完成跨度，≥1）

      MarkDataWrComplete(ctx, span):
        ctx->wr_state.fetch_sub(span)
        if all WRs done → FinalizeIo(ctx):
            拼装 wr_slots 所有 qbuf_wr->buf_data 为完整 IOBuf
            umq_buf_free(ctx->offer_rx_buf)   ← 释放控制帧 qbuf
            state->active_io.erase(ctx)
            → 交付给 brpc InputMessenger → CutInputMessage → HandleRequest

      DrainDeferredControls(state)
      RetryPendingReads(state, umqh, umq_sock)
      return true
```

---

### 2.6 响应回复（服务端 → 客户端）

响应路径与请求路径完全对称，角色互换：

```
done->Run()
  → SendRpcResponse → Socket::Write → DoUbsNativeWrite → TrySenderPost
  → response body > 4064B → READ_OFFER

服务端发出 READ_OFFER（携带 response 内存描述符）
客户端 RxPollQuantum 收到控制帧 → DoReadOffer → 提交 READ WR
客户端 NIC 通过 RDMA READ 从服务端内存拉取 response body
客户端 TxSweepOnce 收割 READ CQE → FinalizeIo
  → IOBuf 交 brpc CallMethod 回调
```

---

### 2.7 100KB 路径完整时序

```
发送方应用线程         发送方 IO 线程          UB Fabric          接收方 IO 线程
───────────────────────────────────────────────────────────────────────────────

TrySenderPost
  umq_post(READ_OFFER)
  NotifyPosted()
                                                           ←── JFCE（控制帧到达）
                                                               RxPollQuantum
                                                               DoReadOffer
                                                               umq_post(READ WRs)
                                              ◄── RDMA READ ── NIC 发起 READ 请求
发送方 NIC DMA 读出数据 ──────────────────────────────────────► 数据写入接收方 buffer
                                                               TX CQ 写 READ 完成 CQE
SEND CQE → 发送方 TX CQ                                        TxSweepOnce
TxSweepOnce                                                    HandleTxCompletion(B1)
HandleTxCompletion(B2)                                           FinalizeIo
  umq_buf_free(ctrl_qbuf)                                          → IOBuf 交 brpc
```

两侧收割完全独立：发送方的 SEND CQE（控制帧发出即触发）和接收方的 READ CQE（数据真正落地才触发）并行进行，互不阻塞。发送方 CPU 在 RDMA READ 执行期间零参与。

---

## 第三章：TX CQE 收割机制对比

### 现状（1ms 定时器路径）

```
TxCqePoller 独立线程
  1ms timerfd epoll → PollAllSockets()
    逐 socket 空 poll 50 次（O(50×N) 全量扫描）

时延三层叠加：
  ① 定时器相位噪声:    0~1ms
  ② RX 无界循环排队:  timerfd 在就绪队列中等待 RX do-while 结束
  ③ 全量空转扫描:     O(50×N)
总计 P99 时延 2ms+
```

### 归一方案（UBS_TX_UNIFIED_POLL_ENABLED=true）

```
SHARE_JFR_RX_RUNNER 升级为统一 IO poll 线程

ACTIVE 主循环：
  do {
    progress  = RxPollQuantum()       // 有界：≤128 CQE/轮
    progress |= TxSweepOnce()         // 有界：每 target 首空即退
    if (++rounds % 64 == 0)
      DispatchPendingEvents()         // 就地分发，防多 main umq 饿死
  } while (progress || AnyTxInflight())

唤醒路径（优先级）：
  1. RX JFCE 事件（1K 路径 DoReadOffer 天然在 RX 线程，无需额外唤醒）
  2. NotifyPosted() eventfd（深睡时由发送侧写入）
  3. 100ms 兜底 timerfd

在途计数（v2.4 口径）：
  递增: NotifyPosted(n_submitted) → global_inflight_.fetch_add(n)
  递减: PollUmqTxInternal         → fetch_sub(poll_num)（按 CQE 数，覆盖全路径）
  判定: AnyTxInflight()           → global_inflight_.load() > 0（单次原子 load）
```

| 指标 | 现状 | 归一方案 |
|------|------|---------|
| P99 时延（空载）| 2ms+ | < 100us（目标）|
| P99 时延（RX 满载）| 无上界 | ≤ 1 个 RX batch 时长 |
| CPU（空闲）| ~0 | ~0（阻塞 epoll_wait）|
| 线程数 | — | POOL 无流控形态净减 1 |

---

## 第四章：两条路径横向对比

| 维度 | 1K（≤4064B，SMALL_DATA）| 100KB（>4064B，READ_OFFER）|
|------|------------------------|--------------------------|
| **umq_post 内容** | payload inline 在 qbuf | 仅控制帧（内存描述符，几十字节）|
| **数据传输语义** | SEND（推送）| RDMA READ（拉取）|
| **数据移动发起方** | 发送方 NIC 主动推 | 接收方 NIC 主动 READ |
| **接收侧 RX CQE 处理** | 直接解帧，投递 bthread | DoReadOffer → 提交 READ WR |
| **TX CQE 收割侧** | 发送方（SEND 完成）| 接收方（READ 完成）|
| **内存注册开销** | 无额外注册 | umq_mempool_info_set（首次需注册，后续 REUSE 跳过）|
| **发送方 Block 释放时机** | SEND CQE 收割后 DecRef | 接收方 READ 完成；发送方内存隐式解 pin |
| **EAGAIN 处理** | TrySenderPost 返回 EAGAIN，KeepWrite 重试 | READ WR 入 pending_reads 队列，TX CQE 触发重试 |
| **SQ slot 消耗** | 1 slot / 段 | nsegs slots / 批次（1 slot / segment）|
| **有序性保证** | SEND 自然有序 | ConfigureOrderedReadCompletions + completion_span |
| **端到端时延** | 单程 SEND 延迟 | RDMA READ 往返延迟 |
| **发送方 CPU 参与数据搬运** | 有（序列化写入 qbuf）| 无（NIC DMA 直接读取内存）|

---

## 附：已知风险点

**在途计数虚高（最高风险，v2.4 识别）** — 若递减逻辑未在 `PollUmqTxInternal` 公共层统一实现，而仅挂在归一线程的 sweep 路径，`writev` 路径（`PollTx`）收割的 CQE 永不递减，`global_inflight_` 虚高，ACTIVE 循环永久驻留忙转，`Stop()` 挂死。须保证：递减在公共层、按 CQE 数（`poll_num`）、全局聚合与 per-target 同步。

**NotifyPosted 遗漏** — `TrySenderPost` 批量 SEND、`DoReadOffer`、`RetryPendingReads`、`SendSimpleCtrl`、`DrainDeferredCtrl`、`ProbeManager` 探测包请求/响应共 7 处均须覆盖。任意遗漏均导致深睡期该路径的 CQE 退化到 100ms 兜底节奏，对 100KB SEND 吞吐影响尤为显著（坍塌至兜底频率）。

**ExternalPoller backend 形态** — 归一方案的 ACTIVE 驻留要求专职 pthread 线程。`GlobalSetting::UBS_POLLER_OPS != nullptr` 时须在初始化阶段强制关闭该特性，回退 1ms 定时器并打 WARN。

**POOL 模式孤儿 CQE** — socket 关闭后若 `FlushTx` 超时留有在途 WR，节点因 `tx_outstanding != 0` 无法归还池子。归一方案用 100ms 兜底 tick 对 main umq 做带 `FLAG_TP_HANDLE_IDX` 的 round-robin drain 清扫；`umq_destroy` 是否释放 node ownership 须在实施前与 umq 层对齐确认。
