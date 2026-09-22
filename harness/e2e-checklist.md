# TX/RX Unified Poller — E2E Acceptance Checklist

> 部署到目标运行时环境（Kunpeng aarch64 + UB 硬件）后的端到端验收清单。
> 对应 Issue 05: integration-latency-cpu-acceptance (HITL)。

## Prerequisites

- [ ] UB 硬件环境就绪（Kunpeng aarch64, URMA/UB transport 可用）
- [ ] UMQ 库已构建: `src/hcom/umq/build/src/libumq.so`
- [ ] UBSocket 已构建: `UMQ_BUILD=on UBSOCKET_BUILD=on bash build/build_umq_and_ubsocket.sh`
- [ ] 基线数据采集完成（`UBS_TX_UNIFIED_POLL_ENABLED` 未设置 = flag=OFF）

## Test Matrix

### 1. 空载 P99 时延（核心目标）

**环境变量:**
```bash
export UBSOCKET_TX_UNIFIED_POLL_ENABLED=true
export UBSOCKET_TX_POLLER_SPIN_ROUNDS=64
export UBSOCKET_TX_POLLER_BACKOFF_MAX_US=200
export UBSOCKET_TX_POLLER_FALLBACK_MS=100
```

**验收标准:** 空载 P99 < 100us（eventfd 唤醒路径）

**测试方法:**
1. 建立 UB 连接（share-JFR 模式）
2. 单方向发送 READ WR，测量 post READ → HandleTxCompletion 时延
3. 采集 10000+ 样本，计算 P50/P95/P99

**结果记录:**
- P50: ____ us
- P95: ____ us
- P99: ____ us (目标 < 100us)
- 对比基线 (flag=OFF): P99 = ____ us

### 2. RX 满载下 TX P99 时延（交织配额核心验证）

**验收标准:** RX 满载下 TX P99 有界，与 RX batch 时长同量级，显著优于现状 2ms+

**测试方法:**
1. 双向流量：RX 满载（持续大量 READ offers）+ TX 发送
2. 测量 TX CQE 收割时延
3. 对比 flag=OFF（100ms timer 兜底）时的 TX P99

**结果记录:**
- flag=ON RX满载 TX P99: ____ us
- flag=OFF RX满载 TX P99: ____ us (预期 2ms+)
- 改善倍数: ____x

### 3. RX batch 时长分布

**测试方法:**
1. 使用 SplitTrace 打点采集 `CORE_PROCESS_JRF_END` 时长
2. 统计 RX batch 处理时长分布

**结果记录:**
- RX batch P50: ____ us
- RX batch P99: ____ us
- TX P99 应与 RX batch P99 同量级

### 4. 空闲 CPU 占用

**验收标准:** 空闲 24h 统一线程 CPU ~0（DEEP_IDLE 阻塞 epoll_wait）

**测试方法:**
1. 建立连接后无流量
2. 运行 24h，监控 SHARE_JFR_RX_RUNNER 线程 CPU 占用
3. `top -H -p <pid>` 或 `/proc/<pid>/task/<tid>/stat`

**结果记录:**
- 24h CPU 占用: ____% (目标 ~0%)
- 无异常日志

### 5. RX 回归：纯 RX 开/关两态持平

**验收标准:** 纯 RX 流量下 flag=ON/OFF 吞吐/时延持平（sweep 稀释 ≤ 3%）

**测试方法:**
1. 纯 RX 流量（单向 READ offers）
2. 分别在 flag=ON 和 flag=OFF 下测量吞吐和时延
3. 计算 sweep 引入的稀释比例

**结果记录:**
- flag=OFF 吞吐: ____ ops/s, P99: ____ us
- flag=ON 吞吐: ____ ops/s, P99: ____ us
- 稀释比例: ____% (目标 ≤ 3%)

### 6. 纯 SEND 吞吐/时延持平

**验收标准:** 纯 SEND 持平（writev 路径未动）

**测试方法:**
1. 纯 SEND 流量（单向 SMALL_DATA 发送）
2. flag=ON/OFF 对比

**结果记录:**
- flag=OFF 吞吐: ____ ops/s, P99: ____ us
- flag=ON 吞吐: ____ ops/s, P99: ____ us
- 差异: ____%

### 7. 三向混跑时延分布

**验收标准:** SEND + bigdata READ + RX 三向混跑时延分布不劣化于基线

**测试方法:**
1. 同时运行 SEND、READ（bigdata）、RX 三方向流量
2. 测量各方向时延分布
3. 对比 flag=OFF 基线

**结果记录:**
- SEND P99: ON=____ us / OFF=____ us
- READ P99: ON=____ us / OFF=____ us
- RX P99: ON=____ us / OFF=____ us

### 8. ubsocket_uninit 高负载压测

**验收标准:** `ubsocket_uninit` 高负载压测无 `mempool tseg not exist`

**测试方法:**
1. 高负载运行中调用 `ubsocket_uninit()`
2. 检查是否有 `mempool ... tseg not exist` 错误日志
3. 验证 Stop 顺序：TxCqePoller::Stop → ArraySet::ReleaseAll → Runner::Stop → UmqBackend::UnInit

**结果记录:**
- 无 `mempool tseg not exist`: [ ] PASS / [ ] FAIL
- 无 crash: [ ] PASS / [ ] FAIL

## SplitTrace Tracepoints

新增枚举值（`ubsocket_prof.h`，在 `UBSOCKET_PROF_COUNT` 之前）：
- `TX_CQE_LATENCY_POST_READ` — post READ (umq_post TX) 时刻
- `TX_CQE_LATENCY_HANDLE_COMPLETION` — HandleTxCompletion 时刻

**验证方法:**
1. `UBSOCKET_PROF_ENABLE=true UBSOCKET_PROF_MODE=ext` 启用 profiling
2. 运行 READ 流量
3. 检查 profiling dump 文件中新增 tracepoint 的记录

## 四开关组合矩阵

| UBS_TX_UNIFIED_POLL_ENABLED | UBS_SHARE_JFR_LOOP_POLL_ENABLED | 预期行为 |
|---|---|---|
| false | false | 现状：1ms timer + ProcessShareJfrEvent do-while 不 spin |
| false | true | 现状：1ms timer + ProcessShareJfrEvent do-while spin |
| true | false | 统一 poller：单轮 quantum + TxSweep 不 spin → DEEP_IDLE |
| true | true | 统一 poller：RunUnifiedActiveLoop spin + backoff |

**验证:** 每种组合下系统行为正确，无 crash、无死锁、无性能异常。

## Sign-off

- [ ] 所有 8 项验收测试通过
- [ ] 验收报告归档（含 RX batch 时长分布数据）
- [ ] 四开关组合矩阵验证完成
- [ ] SplitTrace tracepoint 正确记录时延

Reviewer: ____________  Date: ____________
