# UMQ 模块测试附录 (csrc/core/umq/)

与 `ut-gen/SKILL.md` 配合使用。本模块是 UBSocket 的主传输适配层,errno 映射逻辑的核心。

## 模块范围(2026-08-24 实测)

`.h`(22): `umq_backend.h`、`umq_bounded_seq.h`、`umq_buf_converter.h`、`umq_buffer_receive_queue.h`、`umq_conn_helper.h`、`umq_data_rx_ops.h`、`umq_data_tx_ops.h`、`umq_eid_table.h`、`umq_epoll_ops.h`、`umq_errno_converter.h`、`umq_intrusive_buf_queue.h`、`umq_qbuf_list.h`、`umq_setting.h`、`umq_share_jfr_epoll_runner_ops.h`、`umq_socket.h`、`umq_socket_acceptor.h`、`umq_socket_connector.h`、`umq_tp_event_epoll_runner_ops.h`、`umq_tp_tx_epoll_runner_ops.h`、`umq_tp_wait_queue.h`、`umq_transport_pool.h`、`umq_tx_helper.h`

`.cpp`(16): `umq_backend.cpp`、`umq_buffer_receive_queue.cpp`、`umq_conn_helper.cpp`、`umq_data_rx_ops.cpp`、`umq_data_tx_ops.cpp`、`umq_errno_converter.cpp`、`umq_setting.cpp`、`umq_share_jfr_epoll_runner_ops.cpp`、`umq_socket.cpp`、`umq_socket_acceptor.cpp`、`umq_socket_connector.cpp`、`umq_tp_event_epoll_runner_ops.cpp`、`umq_tp_tx_epoll_runner_ops.cpp`、`umq_tp_wait_queue.cpp`、`umq_transport_pool.cpp`、`umq_tx_helper.cpp`

注意: transport pool 重构产物(`umq_tp_*`、`umq_transport_pool.*`、`umq_tx_helper.*`、`umq_buffer_receive_queue.*`、`umq_conn_helper.*`)是当前主力文件,旧文档中的 `umq_epoll_runner_ops.*`、`umq_share_jfr_epoll_runner_ops.*` 描述已过时。

## 现有测试

| 测试文件 | target | 形态 |
|---------|--------|------|
| `unit_test/umq/umq_errno_converter_test.cpp` | `umq_errno_converter_test` | converter-only(仅 gtest_main,直接编入 converter .cpp) |
| `unit_test/umq/umq_socket_connector_test.cpp` | `umq_socket_connector_test` | ops 级(mockcpp) |
| `unit_test/umq/umq_setting_multi_level_test.cpp` | `umq_setting_multi_level_test` | ops 级 |
| `unit_test/umq/umq_setting_test.cpp` | `umq_setting_test` | ops 级(纯逻辑,真实路径,无 mock) |
| `unit_test/umq/umq_tx_helper_multi_level_test.cpp` | `umq_tx_helper_multi_level_test` | ops 级 |
| `unit_test/umq/umq_zcopy_allocator_multi_level_test.cpp` | `umq_zcopy_allocator_multi_level_test` | ops 级 |
| `unit_test/umq/umq_buffer_receive_queue_test.cpp` | `umq_buffer_receive_queue_test` | ops 级(mockcpp;`umq_intrusive_buf_queue.h` 纯头文件直接用例 2 个) |
| `unit_test/umq/umq_tp_wait_queue_test.cpp` | `umq_tp_wait_queue_test` | ops 级(mockcpp;公开 `Pop` 清空 / 公开 `Push` 填满环隔离 LeakySingleton,容量用例走行为断言) |
| `unit_test/umq/umq_tx_helper_test.cpp` | `umq_tx_helper_test` | ops 级(mockcpp) |
| `unit_test/umq/umq_data_rx_ops_test.cpp` | `umq_data_rx_ops_test` | ops 级(mockcpp;mock 全部 `::umq_poll/::umq_post/::umq_buf_alloc/::umq_get_cq_event` C API,`PollRx` 状态机全覆盖) |
| `unit_test/umq/umq_conn_helper_test.cpp` | `umq_conn_helper_test` | ops 级(mockcpp;`MOCKER_CPP_VIRTUAL` 拦截纯虚 EpollRunner) |
| `unit_test/umq/umq_tp_tx_epoll_runner_ops_test.cpp` | `umq_tp_tx_epoll_runner_ops_test` | ops 级(mockcpp) |
| `unit_test/umq/umq_tp_event_epoll_runner_ops_test.cpp` | `umq_tp_event_epoll_runner_ops_test` | ops 级(mockcpp) |
| `unit_test/umq/umq_backend_test.cpp` | `umq_backend_test` | ops 级(mockcpp;`MOCKER` 挂 SocketConnHelper 静态方法、挂载 helper 提取、`umq_eid_table.h` 顺带覆盖) |
| `unit_test/umq/umq_socket_test.cpp` | `umq_socket_test` | ops 级(mockcpp;`MOCKER_CPP` 挂全局 C API + `MOCKER_CPP_VIRTUAL` 拦截 TxRunner 虚方法,`ArraySet<Socket>` 单例清理) |

另: `umq_setting_multi_level_test.cpp` 在顶层 CMakeLists 被重复注册为 `ubsocket_umq_setting_multi_level_test`(历史遗留,新测试不要学)。

## Errno 映射要点

### UmqOperation 枚举(唯一合法操作集,冻结)

`umq_errno_converter.h:44-53`: `CONNECT` / `ACCEPT` / `WRITEV` / `READV` / `CREATE` / `BIND_INFO_GET` / `GET_STATE`

没有专门对应 `interrupt_fd_get`/`dev_add`/`poll`/`post`/`init` 的枚举值。**选择最近语义**:
- TX 数据通路 → `WRITEV`;RX 数据通路 → `READV`
- connector 上下文/backend 初始化/建立阶段 interrupt_fd_get/rearm → `CONNECT`;acceptor 上下文 → `ACCEPT`
- `umq_create` → `CREATE`(ConvertHandleResult,不走统一表);`umq_state_get` → `GET_STATE`(特殊路径)

### 三个 Convert API(冻结,声明在 `umq_errno_converter.h:89/105/126`)

| API | 使用场景 | 返回值 |
|-----|---------|--------|
| `Convert(op, umqRet, savedErrno)` | UMQ API 返回 `int`(负值=错误) | Linux errno(正值) |
| `ConvertBufStatus(op, bufStatus, savedErrno)` | CQE `buf->status` 字段 | Linux errno(正值) |
| `ConvertHandleResult(op, savedErrno)` | UMQ API 返回 handle/size(0=失败) | Linux errno(正值) |

`GET_STATE` 特殊路径: 不查表、无 override;`QUEUE_STATE_ERR/MAX` → EIO,`QUEUE_STATE_IDLE/READY` → 0;savedErrno 无关。

### override 语义(用于测试期望)

1. `UMQ_FAIL(=-1)` + savedErrno ∈ {EINVAL, ENODEV, ENOMEM, ENOEXEC, EIO} → 返回 savedErrno
2. `UMQ_ERR_ENODEV` + savedErrno ∈ {EINVAL, EIO} → 返回 savedErrno
3. 否则查表: 命中 → 映射 errno;未命中 → savedErrno(若>0),否则 EIO
4. 例外 `GET_STATE` 绕过以上全部

### op 标注规则

- 共享调用点(同一 UMQ API 多处调用)取枚举最小值;独立调用点取实际流程 op
- 共享调用点实例: `umq_bind`(connector:440 op=CONNECT / acceptor:169 op=ACCEPT)、`umq_dev_add`(backend init op=CONNECT / acceptor op=ACCEPT)、`umq_rearm_interrupt`(建立 CONNECT / TX WRITEV / RX READV)、`umq_post`(PrefillRx CONNECT / TX WRITEV / RX refill READV)

## 边界清单

> 播种 2026-08-25;首批登记 2026-08-25,候选来源 `docs/ubsocket/UBSOCKET-UMQ-CORE-UT-RESEARCH.ch.md` §4,位置行号以源码为准。评审按此清单核对边界值 + 相邻值用例(规则见 `ut-gen/SKILL.md` §2 质量加固目标)。

### 冻结文件(已有)

| 判定点 | 边界值(+相邻值) | 位置 |
|--------|----------------|------|
| Convert 三 API 的 override 边界 | `UMQ_FAIL` + savedErrno ∈ {EINVAL, ENODEV, ENOMEM, ENOEXEC, EIO} → 返 savedErrno;`UMQ_ERR_ENODEV` + {EINVAL, EIO} → 返 savedErrno;未命中: savedErrno>0 → 回退 savedErrno,==0 → EIO | `umq_errno_converter.cpp`(冻结) |
| GET_STATE 特殊路径 | `QUEUE_STATE_ERR`/`QUEUE_STATE_MAX` → EIO;`QUEUE_STATE_IDLE`/`QUEUE_STATE_READY` → 0;savedErrno 无关 | 同上 |
| ConvertHandleResult 返回边界 | ret==0(失败)与 ret>0(成功)分界,`umq_create` 场景 | 同上 |

### umq_buffer_receive_queue.cpp

| 判定点 | 边界值(+相邻值) | 位置 |
|--------|----------------|------|
| 队列深度 2 的幂取整 | `queue_depth <= 1` → 1;否则 `1ULL << (64 - clzll(queue_depth-1))`(`QUEUE_DEPTH_FACTOR=1.2`) | `:36` / `h:100` |
| O3 深度截断 | `o3_queue_depth > queue_depth` → clamp 到队列容量;懒创建 `min(O3_QUEUE_INIT_DEPTH=4, o3_max_depth_)` | `:50-53`、`:66` |
| 队列满/空 | 环 Push 满 → `QUEUE_FULL` + `pending_error_` 置位;乱序堆满 → `QUEUE_FULL`;`DequeueBatch` 在 `pending_error_ != OK` 反复返回错误;链入队时首元素推失败 → 释放整条链;快路径(FC_UPDATE/probe)同样受环容量约束 | `:175-179/127-129`、`:213-220`、`:149-151`、`:98-104` |

**死代码/防御性分支**(实测不可覆盖,勿补用例): `:50-53` O3 clamp(`next_pow2(ceil(1.2*rx)) >= rx` 数学恒不等式,永不触发);`:205-206` `EnsureOooQueue` malloc 失败(`new (std::nothrow)` 防御);`:216-218` 堆满后 `IsEmpty()` 回退(堆满时堆必非空)。
| O3 熔断 | `gap > m_max_ooo_gap` 或 `now - m_ooo_start_time_ns > m_ooo_timeout_ns`(默认 `UMQ_O3_TIMEOUT_MS=60000`)→ 熔断回灌;回灌中环溢出 → 丢弃剩余乱序包 + `QUEUE_FULL` | `:310-312`、`:336-349` |
| 序列号窗口 | `gap > UmqSeqTraits::MAX_WINDOW` → 丢包;`status >= UMQ_FAKE_BUF_FC_UPDATE || raw_sn == UMQ_PROBE_USER_DATA_ID` 快路径 | `:189`、`:174` |
| 参数校验与 shutdown | `DequeueBatch`:`buffers==nullptr || max_count==0 || dequeued_count==nullptr`;`Enqueue(nullptr)`;`is_shutdown_` 拒绝入队/出队 | `:139`、`:114`、`:118-122/144-147` |

### umq_conn_helper.cpp

| 判定点 | 边界值(+相邻值) | 位置 |
|--------|----------------|------|
| `PrefillRx` 批量边界 | `left_post_rx_num == 0` → 错误;`left > UMQ_POST_BATCH_MAX(256)` → 截断为 256;`do-while ((left_post_rx_num -= cur_post_rx_num) > 0)` 循环尾 | `:52`、`:61`、`:102` |
| `trans_mode_str[trans_mode]` 索引 | 枚举直接索引,`trans_mode > RC_CTP` 越界(仅影响日志,先于 GetTpInfo 校验) | `:145-146` |
| `GetTpInfo` 分支 | 四分支 + 默认报错 | `:160-176` |
| `GetRouteList` 空路由 | `route_num == 0` → 错误 | `:205` |
| `GetTargetChipId` 哨兵 | socket_id 未找到 → `UINT32_MAX`;`index >= chip_id_list.size()` → `UINT32_MAX` | `:256-258`、`:260-263` |
| 优先级 flag | `UMQ_LINK_PRIORITY != UBSOCKET_LINK_PRIORITY_NOT_SET` → 置 `UMQ_CREATE_FLAG_PRIORITY` | `:140-143` |
| `RegisterSharedJfrForRead` | `umq_interrupt_fd_get < 0` → 错误;`AddEpollEvent` 非 0 → 错误 | `:222`、`:245` |

### umq_data_rx_ops.cpp

| 判定点 | 边界值(+相邻值) | 位置 |
|--------|----------------|------|
| 探测包判定 | `opcode == UMQ_OPC_SEND_IMM && imm.user_data == UMQ_PROBE_USER_DATA_ID(0xFFFFFF)` | `:59` |
| CQE 状态分支 | `status != 0` 且 `>= UMQ_FAKE_BUF_FC_UPDATE` 三种细分;`HandleErrorRxCqe` ~20 个 status case | `:72-93`、`:260-357` |
| refill 窗口阈值 | `(UBS_RX_DEPTH - rx_queue_avail_num_) > TX_REFILL_THRESHOLD(32)` 触发 refill;refill 失败不致命,`poll_num==0 && rx_queue_avail_num_==0` 才整体失败 | `:157`、`:143` |
| ack 批量 | `(ack_event_num_ += events) >= GET_PER_ACK(32)` 触发 ack | `:243` |
| `RearmRxInterrupt` | `UMQ_TP_TYPE == POOL` 直接 OK;`ret < 0` 才走 Convert | `:371`、`:378` |
| `FlushRx` 超时与循环 | `SocketConnHelper::IsTimeout` 熔断;循环条件 `Type() != SOCK_TYPE_COUNT && poll_total_cnt < rx_queue_avail_num_`;泄漏计数 `(avail -= total) > 0` | `:419`、`:449`、`:451` |
| `HandleBadQBuf` | `while (cur_qbuf != bad_qbuf)` + `rest_size` 递减到 0 | `:205-215` |

### umq_data_tx_ops.cpp

| 判定点 | 边界值(+相邻值) | 位置 |
|--------|----------------|------|
| WR 切分 | `++sge_idx >= TX_SGE_MAX(1) || moved_total_len >= io_buf_size(4064)` 切分 WR;`moved_total_len == 0` 跳过空 WR | `:222`、`:227-231` |
| solicited/unsignaled 阈值 | `tx_queue_avail_num_ == 1 || i+1 == batch`;`unsolicited_wr_num_ > TX_REPORT_THRESHOLD(1) || unsolicited_bytes_ > TX_UNSOLICITED_BYTES_MAX(1MB)`;`++unsignaled_wr_num_ >= TX_REPORT_THRESHOLD(1)` → `complete_enable=1` | `:246`、`:249`、`:263-268` |
| `umq_post` 五路错误分流 | EAGAIN 全败/部分(`SetNotWritableReadyIfUnchanged` CAS);ETIMEDOUT → EIO;EFLOWCTL → EIO;EMLINK → 入等待队列 + EAGAIN;ENOBUFS;其余 → EIO;`bad_qbuf==nullptr` 分支 | `:304-387`、`:463-478` |
| 全败恢复 | `bad_qbuf == tx_buf_list` → 恢复 unsolicited/unsignaled 计数与 head/tail | `:399-418` |
| `PollTx` CAS 循环 | `compare_exchange_strong(expect_epoll_event_num_, 0)` 循环 | `:502-503` |
| `PollUmqTx` 循环 | `(poll_total_cnt < TX_RETRIEVE_THRESHOLD(32) || poll_to_empty)` | `:581` |
| `DpRearmTxInterrupt` 成功路径 | `ret==0` → 直接 `errno=EAGAIN; return -1`,不走 Convert(AGENTS.md 已知陷阱) | `:693-697` |
| `Writable` 反压 | `IsRnrBlocked()`;jetty `WAITING` 状态 | `:628`、`:636` |
| `DoUmqTxPoll` 断链与冷却 | `TryRnrBlockFatal()` 断链;CLOS 拓扑 status ∈ {LOC_LEN, LOC_ACCESS, ACK_TIMEOUT, FC_ERR, FC_ERR_FATAL} → 全部 port 冷却 | `:651-656`、`:673-684` |
| `FlushTx` 熔断与缓存释放 | 超时熔断;`cached_wr_cnt < left_wr_num` 循环释放 unsignaled 缓存 WR | `:782`、`:810-854` |

> **状态**: 已合入(协调页 done,commit `ae60b08e`);实测陷阱见 §特有陷阱 #14。`-EFLOWCTL_FATAL`/`-EFLOWCTL_EAGAIN` 是 Convert 表独立表项,删其用例会掉分支——勿并入 EFLOWCTL 主用例。

### umq_socket.cpp(已合入)

> 研究文档原范围不含 `umq_socket*`(见 UBSOCKET-UMQ-CORE-UT-RESEARCH.ch.md 头注),边界清单由 `umq_socket_test`(124 用例)2026-08-29 建立。依赖: socket 状态机、`ArraySet<Socket>` 全局注册、`DataPlaneTable::Live`(umq_socket.cpp:957-1013 实现体)、`TxRunner` 虚方法、`::umq_create` 等全局 C API。

| 判定点 | 边界值(+相邻值) | 位置 |
|--------|----------------|------|
| `ShouldRegisterTxEvent` | SINGLE 有效 handle → true;无效 handle → false;POOL → 恒 false | `ShouldRegisterTxEvent` |
| `CreateLocalUmq` | 已创建 → 错误;DevIp → degradable 错误;DevName 非 bonding/bonding backup;无 Dev → 默认 bonding dev0;`::umq_create` 失败;used_ports 非 0 存储;SingleTp interrupt_fd_get/rearm 失败;`RegisterFcTxEvent` 失败;bonding 无 DevName → ConnEid;`::umq_dev_add` 失败 | `CreateLocalUmq` |
| `GetOrCreateCold/GetCold/GetUsedPorts` | 无 ext → cold 空/get null/ports 0;二次调用同实例 | `GetOrCreateCold` |
| `UnbindAndFlushRemoteUmq` | 未 bind → 早退;ack_event_num_ 正/负;`::umq_unbind` 失败;NoOps 跳过 ops 清理 | `UnbindAndFlushRemoteUmq` |
| `DestroyLocalUmq` | 无效 handle → 早退;`::umq_destroy` 重试后成功;重试全败 → 重置 handle | `DestroyLocalUmq` |
| `AddTxEvent/DelTxEvent/GetTxFd` | interrupt_fd_get 失败 → -1;epoll_ctl 失败 → -1;DelTxEvent 无效 handle → 0;uninit 时 epoll_ctl 失败 | `AddTxEvent` |
| RNR 状态机 | `OnRnrEnter` 创建 cold 并记 count/start;`OnRnrRecover` 无 cold 早退/更新统计/start==0 无 latency/超阈值重置;`OnRnrTimeout` 无 cold 早退/递增;`IsRnrBlockFatal` 未 block/超时 0/无 cold/start 0/elapsed 分界(超时→true);`TryRnrBlockFatal` 致命成功清冷+超时;`CanNotifyWritable` RnrBlocked→false | `OnRnrEnter` |
| `AddQbuf/GetAndPopQbuf/FlushRxQueue/RxQueueEmpty` | 无 RxQueue → 释放/返回 -1/早退;空队 → 0/true;shutdown → error;share_jfr 使能入队;非空队 → false | `AddQbuf` |
| `CheckDevAdd` | 已注册 → OK;dev_add OK/Eexist → 注册;失败 → error | `CheckDevAdd` |
| `ReinitTxOps/ReinitRxOps/GetUmqTxOps` | 新条目/本对象所有 → 重建;他对象所有 → null;无条目 → null | `ReinitTxOps` |
| `RegisterFcTxEvent/UnregisterFcTxEvent` | flow control off → 0;无效 handle → 0;interrupt_fd_get 失败;`TxRunner::AddEpollEvent` 失败;成功 set fd;Del 失败打日志 | `RegisterFcTxEvent` |
| `RetireStep/UnInitialize` | 无效 handle;NoOps 置 retiring;首调 bound 单/pool drain 成功/未完成;二调 already retiring 跳过首块;not retiring 全 teardown;retiring 未 drain 跳 flush 仍 destroy;drain 后仅 destroy | `RetireStep` |
| `SetAddedEpollFd` | null fd/无 queue 不 crash;pending queue → 通知 dropped | `SetAddedEpollFd` |

> **状态**: 已合入(协调页 done,2026-08-29);行 87.0%/函数 100%/分支 61.2%(数据源 §5);124 用例,单用例均 ≤100ms;实测陷阱见 §特有陷阱 #18。剩余未覆盖多为 `nothrow` OOM 防御分支与个别日志路径。

### umq_setting.cpp

| 判定点 | 边界值(+相邻值) | 位置 |
|--------|----------------|------|
| `AddRules` 区间边界 | credit 1..1024、`UBSOCKET_POOL_MAX_SIZE` 1..6144、`UBSOCKET_LINK_PRIORITY` -1..15、`UBSOCKET_JETTY_POOL_SIZE` 1..1000、O3 超时 ≥2、pool depth 0..15360;枚举串(block type/tiny size/schedule policy/trans mode/tp type) | `:100-115` |
| `VerifySetting` 约束 | `UMQ_MIDDLE_POOL_BLOCK_SIZE` ∈ [8K, 1M] 且 4K 对齐且 2 的幂;`UMQ_MEM_POOL_MAX_SIZE_MB` 过 Validator | `:274-286`、`:269` |
| `GetIOBufSizeByClass` 越界回退 | `sc >= UMQ_SIZE_CLASS_COUNT` → 回退末位 class;`UMQ_EXPLICIT_BLOCK_SIZES` 与 `IOBUF_DIFF` 截断 | `:363-366` |
| `CountRXBufByClass` 分类超界 | `data_size + headroom_size` 与 block size 比较,超界归入最大 class | `:391-404` |
| 字符串解析默认回退 | `TinyBlockSizeFromStr` → 1K;`SchedulePolicyFromStr` → affinity_priority;`BlockTypeFromStr` → 4K | `:445-459`、`:462-473`、`:437-443` |

**死代码/防御性分支**(实测不可覆盖,勿补用例): `:249-251` `ub_trans_mode` else 分支(枚举规则四值被四个 if 全覆盖,Validator 保证 else 不可达);`:312-313` `Init` 中 LoadEnv 失败分支(`LoadEnv` 恒返回 `UBS_OK`,无失败路径)。

### umq_tp_wait_queue.cpp

| 判定点 | 边界值(+相邻值) | 位置 |
|--------|----------------|------|
| `WakeUp` 早退 | `wakeUpNum > UMQ_TP_POOL_SIZE(800)` → 直接返回 0;`MultiPop` 实际弹出数分界 | `:74` |
| `Enqueue(sock)` 拒绝路径 | null 检查;`TryAcquireForWaiting()` 失败 → 不排队返回 OK;`Push` 失败 → `ResetToIdle` + UBS_ERROR | `:21-24`、`:26-36`、`:31-33` |
| `Enqueue(handle)` | `UMQ_INVALID_HANDLE` → UBS_ERROR | `:105-113` |
| 元素类型三分支 | `UMQ_SOCKET`/`UMQ_HANDLE`/other(仅打日志) | `:51-68`、`:81-98` |
| 队列容量 | `MPSCRingQueue` 容量 = `next_pow2(UBS_RX_DEPTH)`(用例 `GetCapacity_DefaultRxDepth_NextPow2RoundUp` 镜像算法断言 `capacity_`) | `h:84-92` |

**死代码/防御性分支**(实测不可覆盖,勿补用例): `h:88-89` `cap==0` 回退 `DEFAULT_CAPACITY`(`h:87` `(cap <= 1) ? 1 : …` 已包含 0,`GetCapacity` 永不返回 0);`h:71` `DEFAULT_CAPACITY` 常量仅被死代码分支引用。

### umq_transport_pool.cpp

| 判定点 | 边界值(+相邻值) | 位置 |
|--------|----------------|------|
| `WarmUp` 前置返回与分流 | 非 RM 模式或非 POOL → 提前返回;已预热去重 `umq_tp_pool.count(main_umqh)`;`unifiedEnabled` → `RegisterOrphanSweep`,否则 `AddTimerEvent`;任一失败 → `Clean()` | `:50-52`、`:54`、`:72-85` |
| `CreateOneTp` 轮转与哨兵 | `aff_ports.empty()`/`all_ports.empty()` → 错误;`aff_rr_num_ %= aff_ports.size()`、`rr_num_ %= all_ports.size()` 轮转;port 去重保序;`tp_idx == UINT32_MAX` → 错误;`fd < UBS_OK` → 错误 | `:174-177`、`:190-193`、`:179/195`、`:209-219`、`:239`、`:249` |
| `RebuildTp` 三查 | 池空/umq 不存在/tp_idx 不存在;modify/destroy 返回负 → 错误 | `:261-275` |
| `AddTimerEvent` 失败路径 | `timerfd_create < 0`;`tx_epoll_event` 分配失败;`timerfd_settime < 0`;`AddEpollEvent < 0`;ScopeExit 双重清理语义 | `:308`、`:320-323`、`:343-346`、`:352` |
| `Clean` | 池空直接 OK;`resource_modify < 0` continue、`resource_destroy < 0` 记日志 | `:114-117`、`:124-132` |

> **状态**: 完成(39 用例,达标 80/50,ctest 全绿,数字见数据源);死代码豁免 3 处(实测不可覆盖,勿补用例): `WarmUp:66-69`(CreatePool 恒返回 UBS_OK,内部失败只记日志)、`CreateOneTp:174` `aff_ports.empty()`(targetChipId 必来自 routes chip 集合,不可能空)、`AddTimerEvent:320` `new(std::nothrow)` 失败(固定 struct 无法注入 OOM);新陷阱见 §特有陷阱 #15。

### umq_tx_helper.cpp

| 判定点 | 边界值(+相邻值) | 位置 |
|--------|----------------|------|
| 无效 CQE 判定 | `buf[i]==nullptr || status != 0 || qbuf_ext==nullptr || user_ctx==nullptr`;status!=0 → `error_cb.invoke` + `HandleTxCqeError` | `:92-93`、`:102-105` |
| 静默轮询 | `silent_poll_err && poll_num < 0` → Convert 后 `errno == EMLINK` 只打 DEBUG | `:45-59` |
| RNR 软反压 | `status == UMQ_BUF_RNR_RETRY_CNT_EXC_ERR && UBS_RNR_BACKPRESSURE_ENABLED`;`HandleRnrNotify` 首/重复进入,不释放 buf(bonding 重发 double free 注释) | `:87-91`、`:448-468` |
| `ProcessTxCqe` 链遍历 | `left_size` 递减循环;`is_coalesced_small` 跳过 DecRef;`wr_first_buf == nullptr` → -1 | `:192-211`、`:195-201`、`:220-223` |
| `LogTxCqeErrorMsg` status 桶 | 桶归类 + 日志分支;`UMQ_BUF_RNR_RETRY_CNT_EXC_ERR_FATAL` → `OnRnrTimeout` 路径 | `:273-297`、`:306-398`、`:358-372` |
| `ProcessErrorTxCqe` | `status >= UMQ_FAKE_BUF_FC_UPDATE` → 直接 free 不走链 | `:403-406` |
| `PollUmqTxForFcReturn` | `silent_poll_err = (UMQ_TP_TYPE == POOL)`;`ret < 0` 时 EMLINK 静默、其余 Convert + UBS_ERROR | `:486`、`:520-535` |

> **死代码豁免(umq_tx_helper_test 实战,不可覆盖已登记)**: 
> - 无效 CQE 判定中 `qbuf_ext==nullptr` 子条件(`:92-93`): `qbuf_ext` 是 `uint64_t[8]` 定长数组(umq_types.h:433)非指针,强转 `(umq_buf_pro_t *)qbuf_ext` 后恒非空——该子条件不可达,剩余 3 个子条件(`buf[i]==nullptr`/`status!=0`/`user_ctx==nullptr`)均有独立用例
> - `ProcessTxCqe` 返回 <0 → FATAL_ERROR 早退(`:120-121`): 仅当 `wr_first_buf==nullptr` 时返回 -1,而该路径本身不可达(见下),单线程 UT 无法触发
> - `wr_first_buf == nullptr` 防御(`:221-222`): do-while 首迭代必有 first_qbuf,防御性代码,不可达
> - 第二 switch `case UMQ_BUF_SUCCESS`(`:307-308`): 被函数开头 `if (bufStatus == UMQ_BUF_SUCCESS) return;`(`:269-271`)早退拦截,不可达

### umq_backend.cpp

| 判定点 | 边界值(+相邻值) | 位置 |
|--------|----------------|------|
| 重复初始化守卫 | `UMQ_INITED` static bool: 首次 Init 成功、再次 Init 直接返回(测试 SetUp 需置 false) | `:40` |
| LINK_SELECTION_POLICY 三目 | bonding+backup → `BONDING_BACKUP`;bonding 非 backup → `BONDING_ROUTE`;RAW → `RAW_DEVICE`(BONDING_ROUTE 分支已补用例,`:118` 全覆盖) | `:116-119` |
| 设备名长度上限 | `UMQ_DEV_NAME.length() >= DEV_NAME_STR_LEN_MAX(64)` → 失败;`>= UMQ_DEV_NAME_SIZE` → 失败;sprintf 返回值 `ret < 0 || ret >= UMQ_DEV_NAME_SIZE` 截断判定 | `:253`、`:325`、`:263-264` |
| 设备名匹配分支 | `strncmp(name, "udma", 4) == 0`;`strstr(name, "bonding_dev")`;`strcmp(name, "bonding_dev_0")` | `:246`、`:269`、`:311` |
| 设备枚举数量边界 | `devCount <= 0` → 失败;`bondingIndex == -1 || bondingIndex > devCount || eid_cnt == 0` → 失败 | `:297/341`、`:319` |
| trans mode 三态 | `UMQ_TRANS_MODE_IB` / `UMQ_TRANS_MODE_UB` / default | `:99-110` |
| 路由去重与哨兵 | `route_num` 循环、`unique_chip_ids` 去重、`targetChipId = UINT32_MAX` 哨兵;`used_ports` 按 chip/die/port 排序 + 去重 | `:390-397`、`:410-437` |
| `strncpy` 检查 | `strncpy(...) == nullptr` → 失败 | `:450/456` |

### umq_buf_converter.h

| 判定点 | 边界值(+相邻值) | 位置 |
|--------|----------------|------|
| `UmqIovConverter::MemCopy` iov 边界 | iov 耗尽、零长度 iov 跳过、`iov_offset_ + len >= iov_[iov_idx_].iov_len` 跨段回退 | `umq_buf_converter.h:21-48` |
| `UmqBufferConverter::MemCopy` 截断 | `m_offset + len >= m_size` → `buf_len = m_size - m_offset`;末尾 `m_offset >= m_size` 返回 | `umq_buf_converter.h:62-67` |

### umq_intrusive_buf_queue.h

| 判定点 | 边界值(+相邻值) | 位置 |
|--------|----------------|------|
| SPSC 容量上限 | `enq_local_ - deq >= capacity_` → 入队返回 false | `umq_intrusive_buf_queue.h:58` |
| Push(nullptr) 校验 | `buf == nullptr` → 返回 false,不提交计数 | `umq_intrusive_buf_queue.h:54` |

> **覆盖方式**: 模板经 `umq_buffer_receive_queue.cpp` 间接实例化已覆盖大部分成员;`Push(nullptr)` 与 `Capacity()` 无间接调用点,需直接实例化 `UmqIntrusiveBufQueue` 补用例(命名空间 `ock::ubs::umq`,已 `using`)。行 74 CAS 失败重试分支需消费者并发摘链,单线程 UT 不可达,豁免。
>
> 维护: 认领时发现清单外的新边界敏感判定点,先在此登记再写用例;新发现经 §11 回流通道同步本清单。行号若因重构漂移,以源码为准更新本清单。

## 特有陷阱

1. **`GlobalSetting::GetEnv/GetEnvAndValidate` 全链 ALWAYS_INLINE**(`ubsocket_global_setting.h`,带 `always_inline` 属性,`-fno-inline` 也拦不住)→ mockcpp **无法 mock** 任何一层。`umq_setting.cpp` 的 `LoadEnv` 测试只能走**真实路径**: `setenv` + `UmqSetting::AddRules()` 注册规则 → `LoadEnv`,Validator 区间/枚举校验真实执行。`Validator` 单例全 inline、`map::emplace` 幂等,重复 AddRules 安全且无清理接口
2. **`UmqSetting` 静态成员是全局状态** — 测试 SetUp/TearDown 必须恢复默认值(见 `umq_setting_test.cpp` 的 `ResetStatics()`),否则用例间污染;用例内多次 `LoadEnv` 是**累积语义**(被拒绝的 env 保留上一次 LoadEnv 的值,不是默认值)
3. **UmqApi adapter 后端无 `_ptr`** — 只能 `MOCKER_CPP(::umq_xxx)`;`UmqApi::xxx_ptr` 赋值仅 dlopen 后端存在(未启用),写了编译不过
4. **`UBS_ENABLE_SHARE_JFR` 默认 `true`** — 测试 SetUp 必须显式 `GlobalSetting::UBS_ENABLE_SHARE_JFR = false`,否则走 share 路径
5. **Share-JFR 双 handle 语义** — `PrefillRx` 本地变量 `umq_handle` 在 share 模式下解析为 `share_umq_handle_`(主 UMQ);等 ready 逻辑必须查 `umq_handle_`(子 UMQ,新创建需 IDLE→READY)。提取 `WaitUntilReady` 之类子函数时传 `umq_handle_` 成员,不传本地变量
6. **`DpRearmTxInterrupt` 成功路径不走 Convert** — `umq_rearm_interrupt` ret==0 时直接设 errno=EAGAIN 返回 -1;仅 ret≠0 走 Convert(WRITEV)。`DpRearmTxInterrupt` 在 `umq_data_tx_ops.h:96`
7. **`UmqBackend::UMQ_INITED` 是 static bool** — SetUp 重置 false,否则跨测试状态污染
8. **方法名已演进**(旧文档失效): `PrefillRx` 现属 `UmqConnHelper`(umq_conn_helper.h:28);数据路径现为 `PollRx`/`RearmRxInterrupt`/`GetAndAckEvent`/`UmqPollAndRefillRx`/`PollTx`/`PollUmqTx`/`DoUmqTxPoll`。写测试前 grep 验证
9. **`UmqBufferReceiveQueue` 陷阱**(umq_buffer_receive_queue_test 实战发现):
   - **FastHeap 初始容量强制 4**: `MIN_CAPACITY=4`,`m_max_capacity` 仅在扩容路径(`m_size >= m_capacity`)检查 → rx=1 时乱序堆实际容量是 4 而非 o3_max_depth 指定的 1。断言堆满(QUEUE_FULL)前必须按容量 4 前置填充
   - **`m_ooo_timeout_ns` 构造时固化**: `(UMQ_O3_TIMEOUT_MS!=0 ? UMQ_O3_TIMEOUT_MS : 5) * 1e6` 在构造函数里确定 → 超时相关用例必须在**构造对象之前**设置环境变量;测试中拨时间用 `queue.m_ooo_start_time_ns = 1`(`-fno-access-control` 可直写)
   - **重复 SN 触发 stale 分支**: `UmqSeqTraits::Distance(2,1)=MODULUS-1 > MAX_WINDOW` → 判定"在后方"→ 走 free 分支。制造 stale 分支的最短路径是重复 SN,不需要完整窗口回绕
   - **nullptr 入堆安全边界**: 乱序堆 comparator 会对元素调 `GetSn`,`Push(nullptr)` 仅当 nullptr 是**唯一堆元素**时安全;之后任何 Push 都会崩溃
   - **侵入式链表复用 `rsvd1`**(`umq_buf_pro_t::rsvd1`,offset 56)作为 `qbuf_next` 链接字段 → 测试手工构造链时不能碰 `rsvd1`,否则破坏链表
8. **纯虚接口用 `MOCKER_CPP_VIRTUAL` 经 vtable 拦截**(umq_conn_helper_test 实战验证): `EpollRunnerBase::Start/AddEpollEvent` 是纯虚,`MOCKER(&Base::method)` 直接 segfault。正解: 对具体模板实例单例对象 `EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER>::Instance()` 用 `MOCKER_CPP_VIRTUAL(obj, &ShareJfrRunner::Start)` patch 共享 vtable 槽位,被测代码经基类引用调用同样被拦截;`EpollRunnerFactory::GetInstance` 返回 `EpollRunnerBase&`,无法 returnValue 引用,让真实路径走(Start 被 mock 不启动线程)
9. **`GetTpInfo` 默认参数陷阱**: `GetRouteList` 内部调用 `GetTpInfo(tp_mode, tp_type)` **不带 trans_mode 参数**,用的是声明默认值 `UmqSetting::UMQ_UB_TRANS_MODE`(调用时求值)。触发其 GetTpInfo 失败分支必须 `UmqSetting::UMQ_UB_TRANS_MODE = 非法值`,传形参无效。**例外**: `NewBaseUmqCreateOptions` 经 trans_mode_str 越界读修复后已显式传 trans_mode 形参——触发它的失败分支直接传非法形参即可(见 `NewBaseUmqCreateOptions_UnsupportedTransMode_ReturnsError`),无需动全局设置
10. **`DataToBlock` 的 mock 语义(umq_tx_helper_test 实战)**: `UmqTxHelper::DataToBlock(void *data)` 调 `UmqApi::umq_data_to_head(data)` 返回 qbuf,再从**返回 qbuf 的 `buf_data` 字段**提取 Block(`reinterpret_cast<Block *>(qbuf->buf_data)`,umq_tx_helper.cpp:203)。mock `::umq_data_to_head` 时返回值必须是一个 qbuf 且其 `buf_data` 指向**真实 Block**(placement-new 构造,测试用 nshared=2 断言 DecRef 递减);多 qbuf 链需按 data 入参分发(`std::unordered_map<void*, umq_buf_t*>`,不能只回一个固定值)。真实签名 `umq_buf_t *umq_data_to_head(void *data)`(umq_api.h:152)
11. **探测包/正常包用例必须设 `user_ctx` 非空**: 外层分发顺序中无效 CQE 判定(`buf[i]==nullptr || status!=0 || qbuf_ext==nullptr || user_ctx==nullptr` → err_code=NORMAL_ERROR,:92-93)在探测包判定(:113)之前 — user_ctx==nullptr 的探测包会先走无效分支。用例需 `MakeBasePro()->user_ctx = reinterpret_cast<uint64_t>(cqe)`
12. **fd 解析依赖 `umq_ctx`**: 未传 `poll_args.sock` 时 `raw_socket = -1` 回退到 `MakeBasePro()->umq_ctx`(:27-29 一带);不设 `umq_ctx = TEST_FD` 则 fd=0 → `GetItem(0)` 为 null → 跳过 tx_queue_avail 更新与 RNR 解除,用例断言落空
13. **freed_jettys 处理只在 `poll_num > 0` 路径**: `std::exchange(poll_args.poll_option.tp_handle_free_num, 0)` + `UmqTpWaitQueue::Instance().WakeUp(freed_jettys)` 只在正常分发路径尾部(:165-168);`poll_num <= 0` 早退(:43-61)不执行。`PollArgs::poll_option` 是**引用**(`umq_io_option_t &poll_option`)— std::exchange 直接改外部对象,用例可断言调用后外部值被清零。`WakeUp` 是**非虚成员**,mockcpp 不可 mock — 让真实 `Instance()` 空队列运行(安全,返回 0)
14. **`umq_data_tx_ops_test` 实战陷阱**(2026-08-27):
    - **mockcpp 只替换被 `MOCKER_CPP` 引用过的符号**: PostSend/FlushTx 测试若只 mock 了 `::umq_post`,真实 `::umq_data_to_head`/`::umq_buf_free` 仍会执行 → 释放路径的 `g_bufFreeCount` 断言落空。FlushTx 缓存释放用例必须一并 mock `::umq_data_to_head`(DataToBlock 需要)+ `::umq_buf_free`
    - **`LoadSeqNum` 在 `UmqSocketSeq` 上,基类 `Socket` 不可见**;`events_`/`GetVersionedWritableReady`/`SetWritableReady` 在 `SocketBase` 上(继承链 Socket→SocketBase→UmqSocket)。测试用 `static_cast<UmqSocket*>`/`static_cast<SocketBase*>` 下行转换(对象恒为 UmqSocket,安全)
    - **PostSend block-lookup 失败(`DataToBlock` 返回 nullptr → EINVAL)不回退 seq**: 失败发生在 `FetchAddSeqNum` 之后,之前 WR 的 seq 保留
    - **`PollTx` CAS 循环每次迭代都调 `GetAndAckEvent` + `PollUmqTx`**: expect≠epoll 时重试 N 次 = 两者各 N 次调用,不是只调一次
    - **`ProcessTracePacket` pending 组装**: 第二片 WR 的数据必须从 header 偏移 `header_cache_size` 处开始(组装 = 缓存 + 当前数据前 N 字节);`pack_size = val_second + 12 - prev_cache_size`,随后统一 `pack_size -= data_size`。`val_second = ntohl(*(assembled+4))`,魔数不匹配仅置 `is_first=false`(打警告),解析照常 → 断言 pack_size 时按真实计算值写,别想当然
    - **`ProcessTracePacket` 的 `available < 8` 分支会 `memcpy` 到 `buf_data + offset`**: 测试必须给 `buf_data` 真实缓冲(`data_size-1` 的 pack_size 用例最易漏 → 段错误);pack_size==data_size 恰好减到 0 的用例不触碰 buf_data,可不设
    - **`FlushTx` 缓存释放块在轮询循环外**: 超时熔断后仍执行释放;`err_code == FATAL_ERROR` 才跳过。断言 `avail` 回补用 `fetch_add(cached_wr_cnt)`,先 poll 后释放,别在 poll 前断言 avail

15. **`umq_transport_pool_test` 实战陷阱**(2026-08-27):
    - **mock 全局数据喂不进成员**: 直接调用被测类私有方法时(如 `CreateOneTp`),方法读的是成员 `route_list_tp_` 而非 mock 全局(`g_mockRoutes` 只供走 `MockGetRouteList` 的 WarmUp 路径)。漏预填 → route_num=0 → `all_ports.empty()` → 返回 UBS_ERROR,EXPECT_EQ 失败且无 crash(不像 OOM 那样直接暴露)。修复: 设置 helper(`SetRouteList`)同步写 `UmqTransportPool::Instance().route_list_tp_`,与真实 GetRouteList 填充路径语义一致;空 route 用例靠 SetUp 的 `ResetPoolState` 清 route_num=0,顺序无关
    - **`RebuildTp_Success` 内部 CreateOneTp 走真实 route 选择**: 重建成功用例必须预填 route 数据,否则重建阶段返回 UBS_ERROR
    - **`operator[]` 副作用即断言点**: `umq_tp_pool[main_umqh][tp_idx]` 在 `umq_interrupt_fd_get` 失败前已插入空 fd_vec 条目——失败用例断言 `pool.umq_tp_pool[handle].size()==1` 验证"条目已插入但 fd 为空"
16. **`umq_backend_test` 实战陷阱**(2026-08-27):
    - **`LoadEnv` 对非法 env 只警告不阻断**: `GetEnvAndValidate` 失败仅打警告并跳过(不 return),`LoadEnv` 恒返回 `UBS_OK`;`UmqSetting::Init()` 唯一失败路径是 `VerifySetting`(`Validator.Validate` 区间校验失败 → `UBS_INVALID_PARAM` + `errno=EINVAL`)。所以"非法环境变量"用例用 `setenv` 测不到 `UmqBackend::Init` 的 `:47-49` 失败分支,必须直接置静态成员(`UmqSetting::UMQ_MEM_POOL_MAX_SIZE_MB = 0`)触发 VerifySetting 失败
    - **`UmqCleanup` 链路上所有 `::umq_*` 必须挂 mock**: 走到 `CreateShareMainUmq` 成功再失败的用例(Prefill/JfrMonitor/WarmUp 失败)会 `UmqCleanup → DestroyShareMainUmq → umq_destroy`(EidTable 里有 handle);漏挂 `::umq_destroy` 会真实调 libumq.so 对假 handle 解引用 → **SEGV**(不是静默失败)。同理 `::umq_uninit` 必挂
    - **`umq_create` 计数断言必须用 invoke 而非 returnValue**: `returnValue(g_mockCreateRet)` 不经过计数函数,`g_mockUmqCreateCnt` 恒为 0(断言静默失败)。统一 `MOCKER_CPP(::umq_create).stubs().will(invoke(&MockUmqCreate))`,invoke 内 `++cnt` 并返回 `g_mockCreateRet`
    - **`::umq_stats_tp_perf_stop` 返回 int 不是 void**: mock 签名用 `static int`(声明见 umq_dfx_api.h),void 签名会在调用时抛 `mockcpp::Exception` terminate
    - **类静态方法用 `MOCKER` 不用 `MOCKER_CPP`**(见 SKILL.md §5 三选表): `SocketConnHelper::GetCurrentProcessSocketId/GetSocketIdsViaNumaSysfs` 是 static,挂载统一收进 `MountSocketIdMocks()` helper;`returnValue` 直挂静态方法返回值,计数或动态值用 `invoke`(同 `umq_create` 规则)
    - **code-review 修复模式**: 同值 `.will(returnValue(0)).then(returnValue(0))` 链是冗余;断言增强用 `errno=EINVAL` + `EXPECT_EQ(errno, EINVAL)`;删除与已有用例同打同一行的冗余用例(如 `CreateShareMainUmq_Bonding_EmptyRouteList` 与 `GetRouteListFail` 同打 `:385`);死 mock 函数删除见 SKILL.md 陷阱 #13

17. **`umq_socket_connector_test` 实战陷阱**(2026-08-29):
    - **TFO fallback 的 `dup3` 是真实 syscall,不可 mock**: `ConnectViaTfo` 末尾 `dup3(new_fd, fd, O_CLOEXEC)` 无法用 LibcApi 函数指针 mock;测试用 fake fd(200→42)必然 EBADF 失败 → "dup3 成功"的完整成功 fallback 分支不可达。`PrepareConnect_HandshakeOpt_SetsockoptFailEnoprotoopt/Eopnotsupp_FallbackTfo` 因此只断言 `UBS_HAND_SHAKE_MODE` 切到 TFO,不断言返回值
    - **kDEGRADE 触发条件**: 需两端均不可重试且带降级标记。`g_recvPeerRet` 设 `UBS_UMQ_BIND | UBS_DEGRADABLE_MASK`(不是 `UBS_OK | UBS_DEGRADABLE_MASK`——后者被 `IsOk()` 判为成功),再配合 `ack_ret` 非 OK/非可重试才能落 kDEGRADE(降级共识合成见 `UmqConnectorOps::ClientDegradableVerdict`)
    - **code-review 修复模式**: 删除空函数死代码(`SaveAndResetLibcApiPtrs/RestoreLibcApiPtrs`);mock 辅助函数定长拷贝用 `memcpy_s`(需 `#include <securec.h>`);类静态方法统一用 `MOCKER`(同 §16)

18. **`umq_socket_test` 实战陷阱**(2026-08-29):
    - **`ArraySet<Socket>` 全局注册必须清理**: 用例后 `ArraySet<Socket>::GetInstance().ReleaseAll()` 清空,否则跨用例 handle 复用/残留污染 `GetSocket`/EidTable/`GetOrCreateMainUmq` 主表(主表残留会改变后续用例命中路径)
    - **虚成员方法用 `MOCKER_CPP_VIRTUAL`**: `TxRunner::AddEpollEvent/DelEpollEvent` 等实例虚方法必须 `MOCKER_CPP_VIRTUAL(runner, &TxRunner::AddEpollEvent)` 从 vtable 取址打桩,不能 `MOCKER_CPP(&Class::method)`(member-pointer 编码成 vtable 偏移,打桩即 SEGV,同 core.md #6)
    - **全局 C API 用 `MOCKER_CPP` + invoke**: `::umq_create` 等返回 int 的全局 C 函数统一 `MOCKER_CPP(::umq_create).stubs().will(invoke(&MockUmqCreate))`,invoke 内自增计数并返回 mock 值;直接 `returnValue` 会跳过计数,断言静默失败(同 umq_backend #16)
    - **errno 前置**: 在调用被测函数之前设置 errno——生产代码在 UMQ API 返回后立即保存 errno,漏设会导致 Convert 断言落空
    - **`CreateLocalUmq`/`DestroyLocalUmq` 重试循环**: 断言重试边界(首败后成功/全败重置 handle),mock 用 `.will(returnValue(...)).then(returnValue(...))` 链模拟多次调用不同返回值
    - **`UnbindAndFlushRemoteUmq` 依赖 `TxOps` 存在**: 走 ops 清理的用例需先挂好 Tx/Rx ops 实例,NoOps 场景单独覆盖"跳过 ops 清理"分支
    - **code-review 修复模式**: 同值 `.will(returnValue(0)).then(returnValue(0))` 链冗余要删;断言增强用 `errno=EINVAL` + `EXPECT_EQ(errno, EINVAL)`;删与已有用例同打同一行的冗余用例

### umq_backend.cpp 死代码豁免(4 处)

1. `:263-264` AddUbDev 的 sprintf 截断检查 — `dev_info` 由 `strncpy(..., DEV_NAME_STR_LEN_MAX - 1)` 产生 ≤63 字符,sprintf 返回值恒 <64,`ret < 0 || ret >= UMQ_DEV_NAME_SIZE` 不可达
2. `:326-328` FindDevName 的 `UMQ_DEV_NAME.size() >= UMQ_DEV_NAME_SIZE` — 源串由 `strncpy(name, ..., UMQ_DEV_NAME_SIZE - 1)` 封顶,std::string 长度恒 ≤63,无法安全构造 64+ 字符名(char[64] null 终止约束)
3. `:450/456` CreateShareMainUmq 的 `strncpy(...) == nullptr` — C 库 `strncpy` 恒返回 dst,恒非 nullptr
4. `:319` `bondingIndex > devCount` — `bondingIndex` 在 `index < devCount` 循环内赋值,恒 ≤devCount-1,不可达(相邻分支 `==-1` 与 `eid_cnt==0` 已有用例覆盖)

## 测试范例

- `umq_socket_connector_test.cpp` — ops 级完整模板: LibcApi `xxx_ptr` 替换、connector 状态机(`PrepareConnect_*` 用例组)、`SetLibcApiPtrsToNull()` 清理函数
- `umq_tx_helper_multi_level_test.cpp` — 全局 C 函数 mock + fixture 持有被测对象成员
- `umq_socket_test.cpp` — 虚方法 mock(`MOCKER_CPP_VIRTUAL`) + 全局 C API mock(`MOCKER_CPP`+invoke) + `ArraySet<Socket>` 单例清理模板(`GetInstance().ReleaseAll()`)
