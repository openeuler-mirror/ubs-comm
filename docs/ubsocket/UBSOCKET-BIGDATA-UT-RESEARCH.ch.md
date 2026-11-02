# UBSocket Bigdata UT 前期调研

> 本文件是 `csrc/core/ubsocket_bigdata.cpp` / `ubsocket_bigdata.h` / `ubsocket_bigdata_order.h` 的 UT 补充工作调研报告(协调页 `UBSOCKET-CLAIMING.md` core 表 `ubsocket_bigdata.cpp` 行,当前 `unclaimed`)。
> 全部论断基于源码一级资料,标注 `文件路径:行号`。覆盖率数字只引用 `docs/ubsocket/UBSOCKET-COVERAGE-ANALYSIS.ch.md`(下称"数据源")并标注。
> 政策: 每模块 行 ≥80% / 分支 ≥50%(`docs/adr/0001-coverage-policy-per-module-80-50-no-cap.md`);结构对照 `UBSOCKET-UMQ-CORE-UT-RESEARCH.ch.md`。

## 1. 目标范围清单

| # | 文件 | 总行数 | 内容形态 |
|---|------|--------|---------|
| 1 | `core/ubsocket_bigdata.cpp` | 3019 | 自适应大小/大 I/O 引擎(设计 §5–§7),可执行行 1484(LF,数据源 §5 表) |
| 2 | `core/ubsocket_bigdata.h` | 136 | 纯声明: `UbsBigdata` 类(9 个 static 方法)+ `GenCheckStats` 结构体;无可执行行 |
| 3 | `core/ubsocket_bigdata_order.h` | 192 | **纯头文件**(header-only inline,无独立 .cpp,已核实 `csrc/core/` 无 `ubsocket_bigdata_order.cpp`),7 个 inline 函数 + 1 个 struct,可执行行 51(LF,数据源 §6 零覆盖 .h 表) |

补充说明: `ubsocket_bigdata.cpp` 还**定义了 3 个 `UmqSocket` 成员函数**(`:2062-2097` `GetOrCreateBigdataState`/`GetBigdataState`/`ReleaseBigdataState`),声明在 `core/umq/umq_socket.h`(`:135-137` 构造/析构区附近)——测试驱动它们时链接的是 libubsocket 内本文件实现的符号,行覆盖计入 bigdata.cpp。

## 2. 现有测试与目标文件的对应关系

### 2.1 已注册 target

顶层 `src/ubsocket/unit_test/CMakeLists.txt:713-744` 注册 `ubsocket_bigdata_proto_test`:

| 项 | 值 | 来源 |
|----|-----|------|
| 测试源 | `ubsocket_bigdata_proto_test.cpp`(21 个用例) | CMakeLists:741-744;文件内 `TEST_F` 计数 |
| 链接 | `ubsocket_static` + `boundscheck` + `GTest::gtest_main` + `pthread`(**无 mockcpp**) | CMakeLists:733-739 |
| boundscheck 注 | 现有模板的 boundscheck 是历史遗留——csrc 生产代码与测试代码均无 securec 函数调用,链了只给动态库版本引入多余 DT_NEEDED;新 target 一律不链(§8) | 2026-08-28 实测 `readelf -d`;对照 `ubsocket_socket_helper_test` |
| include | `csrc` + `csrc/core` | CMakeLists:727-731 |
| 测什么 | **仅 `core/ubsocket_proto.h` 纯头 wire 常量/helper**(wire 尺寸、4064B 预算、32 项上限、imm bit 20 分类),不触碰 bigdata.cpp 任何函数 | `ubsocket_bigdata_proto_test.cpp:16-27` 文件头注释 |

结论: `ubsocket_bigdata_proto_test` 对 `ubsocket_bigdata.cpp` 的贡献为零(数据源中该 .cpp 3.6% 行覆盖全部来自其它 target 经 `ubsocket_static` 库内顺带执行,见 §5)。

### 2.2 孤儿 `ubsocket_bigdata_handler_test.cpp` 评估(SKILL.md §4 点名反例)

- **文件**: 392 行,19 个 `TEST_F`,测试 `UbsBigdata::HandleRxControl` 的**内容校验拒绝路径**(`ubsocket_bigdata_handler_test.cpp:159-380`)。
- **测试对象构造**: 手写 `DummySocket : public Socket` 子类(`:34-61`),无任何 `MOCKER_CPP` 挂载。
- **覆盖了什么**: 空指针/短 buffer/全零/随机数据/首字节伪 type/`nsegs` 越界(32)/`nsegs!=nmempool_infos`/`total_len` 与布局公式不匹配等拒绝路径;含 255 个非法 type 值穷举(`:380-390`)。
- **致命缺陷(文件自证)**: 正例路径(合法 READ_DONE/READ_ABORT 被消费)因 `DummySocket` 无法 `RefConvert` 成 `UmqSocket`(`ubsocket_bigdata.cpp:2514-2518` 必返回 false),**永远测不到 `ReleasePinned` + `umq_buf_free` 的真实消费分支**——文件注释 `:371-378` 自己承认了这一局限。另: 未注册 CMake,从不运行。
- **判定**: 不直接救活注册。理由: (1) 违反"一律 mockcpp"依赖替换纪律(SKILL.md §1/§5,手写子类即测试替身,与已删除的 stub 设施同性质);(2) 正例路径结构上无法用该 fixture 完成,即使注册也达不到 80/50;(3) 其拒绝路径用例(约 14 个)可低成本吸收进新测试的"内容校验"分组——改用真实 `UmqSocket` + `MOCKER_CPP(::umq_buf_free)` 断言消费与否,信息量更高。

## 3. 外部依赖与可 mock 点总览

### 3.1 UmqApi(adapter 后端,全局 C 函数,**只能 `MOCKER_CPP(::umq_xxx)`**,无 `_ptr`——SKILL.md §5)

| 函数 | bigdata.cpp 调用点 | 用途 |
|------|-------------------|------|
| `umq_buf_alloc` | `:459/:517/:598/:970/:1563` | SMALL_DATA/coalesced/offer/ctrl/READ dest 分配 |
| `umq_buf_free` | `:298/:2539/:2546/:2597/:2671` | 链回收、DONE/ABORT 消费、READ CQE 回收 |
| `umq_data_to_head` | `:679` | Block data → 归属 qbuf(mempool 信息恢复) |
| `umq_mempool_info_get` | `:691` | 发送侧把 mempool blob 写入 wire 条目 |
| `umq_mempool_info_set` | `:1500` | 接收侧导入 mempool |
| `umq_mempool_info_get_remote_fields` | `:1585` | 提取 mempool_id/token_id/token_value 填 READ sge |
| `umq_remote_mempool_state_check` | `:1484` | REUSE/ERR/NEED_IMPORT/NEED_REIMPORT 状态判定 |
| `umq_post` | `:913/:995/:1644/:1766/:2322` | 五处: deferred ctrl 重发 / SendSimpleCtrl / READ 链 / RetryPendingReads / TrySenderPost 批提交 |

### 3.2 进程内组件(类成员可 `MOCKER_CPP(&Class::method)`,inline 的只能靠真实现或覆盖其依赖)

| 依赖 | 形态 | 要点 |
|------|------|------|
| `ArraySet<Socket>::GetInstance()` | 单例 | `Init()`/`OverrideItem(fd, sock)`/`ReleaseAll()`;`GetItem` 在 `:252/:920/:1006/:1301/:1387` |
| `RefConvert<Socket, UmqSocket>` | 模板 static 转换 | 前提: ArraySet 里必须是**真实 `UmqSocket` 对象**(`MakeRef<UmqSocket>(fd)`,参考 `ubsocket_tx_unified_poller_test.cpp:138-144`);`DummySocket` 走不通(§2.2) |
| `UmqSocket::GetOrCreateBigdataState` 等 3 函数 | 本 .cpp 定义(`:2062-2097`) | 真实实现即可;`SocketExt`/`EnsureExt` 真实 |
| `UmqSocket::FetchAddSeqNum/FetchSubSeqNum` | inline(`core/umq/umq_bounded_seq.h:124/134`) | 真实原子运算,直接可用 |
| `UmqSocket::UmqHandle/GetLocalRpcTimeoutMs/GetPeerRpcTimeoutMs/State/IsRetiring` | `umq_socket.h` | 真实实现;`IsRetiring()` 默认 false |
| `UmqSocket::AddQbuf` | **非 inline**(`umq_socket.cpp:733`) | `MOCKER_CPP(&UmqSocket::AddQbuf)`,`DeliverToRxQueue` 入口(`:1080`) |
| `SocketBase::GetTxOps()` | **inline 经 `DataPlaneTable::Live(raw_socket_)`**(`ubsocket_socket.h:223-227`) | 未物化 DataPlaneEntry 时返回 nullptr → 各 `tx_queue_avail_num_` 记账分支被跳过(见 §7 陷阱) |
| `SocketBase::NotifyReadable()` | `ALWAYS_INLINE`(`ubsocket_socket.h:339-360`) | 不可 mock;未注册 epoll 时打警告返回 -1,不影响断言 |
| `TxCqePoller::Instance()` | LeakySingleton | `MarkActive`/`NotifyInflight`/`NotifyPosted`(`:927/:931/:1014/:1545/:1661/:1901/:2398/:2417`);`UBS_TX_UNIFIED_POLL_ENABLED=false`(默认)时 `Notify*` no-op |
| `Block::IncRef/DecRef` | inline(`iobuf/ubsocket_iobuf.h:55-67`) | `DecRef` 归零时调 `::ubsocket_iobuf_deallocate`(定义 `ubsocket.cpp:376`)→ **必须 `MOCKER_CPP(::ubsocket_iobuf_deallocate)` 或保证引用计数>0**;`Block` 构造简单(`ubsocket_iobuf.h:32-52`,`Block(data, size, init_nshared)`) |
| `LockRegistry::LOCK_OPS.create/destroy` | `ubsocket_lock.h:60-61` | `UbsBigdataSocketState` 构造/析构调用(`:113/:118`);**SetUp 先 `LockRegistry::RegisterDefaultOps()`** |
| `Func::CurrentTimeNs()` | `common` 工具 | 真实实现;超时用例需构造 `now` 相对时间或直接设 `deadline_ns`(见 §6) |
| `GlobalSetting::UBS_*` | 静态变量(`ubsocket_global_setting.cpp:41/49/50/51`) | 默认 `UBS_TX_UNIFIED_POLL_ENABLED=false`、`UBS_READ_GEN_CHECK_ENABLED=false`、`UBS_BIG_PIN_TIMEOUT_MARGIN_MS=1000`、`UBS_GRACE_MS=100`;测试间需恢复默认 |

### 3.3 全局状态(匿名 namespace,测试**不可注入/重置**,只能经公共 API 观察)

- `g_read_gen`(`:82`,初值 1,单调递增)—— TrySenderPost 每批 fetch_add,`GetGenCheckStats()` 无对应读数;**用例断言只能用"非零"或相对值**。
- `g_seq`(`:236`,random_device 种子)—— offer seq 来源,同上。
- 6 个计数原子(`:86-91`)—— 经 `UbsBigdata::GetGenCheckStats()`(`:2961-2971`)读,**可用增减断言**(如 `pin_timeout`/`gen_mismatch`/`gen_fallback`/`rx_ctx_timeout`/`offer_total` 各 +N)。
- `g_stats_dump_registrar`(`:3015`)—— 静态注册器;仅当环境变量 `UBS_GEN_CHECK_STATS_DUMP=1` 时启动 3s/5s 后台打印线程 + atexit。**测试禁止设置该 env**(违背单用例 ≤1s 与隔离);不设置则零副作用。
- `BigdataOrderDebugEnabled()`(`ubsocket_bigdata_order.h:44-52`)—— 首调用读 env 后 static 缓存,**跨用例不可重置**;建议只测默认分支或整体跳过(见 §9 风险)。

### 3.4 公共 API 调用方(顺带覆盖来源与驱动入口)

| 调用方 | 调用的 bigdata API | 来源 |
|--------|-------------------|------|
| `ubsocket_data.cpp:91` | `TrySenderPost` | `ubsocket_static` 内真实调用 |
| `ubsocket_data.cpp:192` | `HandleRxControl` | 同上 |
| `umq_tx_helper.cpp:78` | `HandleTxCompletion` | 同上 |
| `ubsocket_tx_cqe_poller.cpp:267/556-557/598-599/622-623` | `NeedsPollerAttention`/`RetryPendingReadsForSocket`/`SweepExpiredForSocket` | 同上 |
| `umq_socket.cpp:41` | `CleanupSocketState`(UmqSocket 析构链) | 测试销毁 `UmqSocket` 对象时**自动触发**,无需手动调 |

## 4. 逐文件分析

### 4.1 `ubsocket_bigdata.cpp` / `ubsocket_bigdata.h`

**职责**: 自适应大小/大 I/O 引擎(设计 §5–§7)。发送路径按段长路由(≤4064B 走 SMALL_DATA/coalesced SEND,>4064B 走 READ_OFFER + 对端 RDMA READ 拉取);接收侧导入 mempool、发 READ WR、终态 Finalize、SN 有序投递、DONE/ABORT 应答;两级超时释放(read_gen 校验 + grace)、EAGAIN 回压队列(deferred_ctrl/pending_reads)、poller 集成。

**公共 API(9 个,全部 static;`ubsocket_bigdata.h:49-131`)**:

| 方法 | bigdata.cpp 定义 | 行数 | 现状覆盖 |
|------|-----------------|------|---------|
| `TrySenderPost` | `:2103-2433` | 331 | 无专用测试(仅顺带) |
| `HandleRxControl` | `:2437-2554` | 118 | 拒绝路径有孤儿测试(未注册,§2.2);正例路径未覆盖 |
| `HandleTxCompletion` | `:2558-2722` | 165 | 无 |
| `HandleFlowControlUpdate` | `:2724-2737` | 14 | 无 |
| `RetryPendingReadsForSocket` | `:2739-2770` | 32 | 无 |
| `NeedsPollerAttention` | `:2830-2845` | 16 | 无 |
| `SweepExpiredForSocket` | `:2847-2886` | 40 | 无 |
| `CleanupSocketState` | `:2888-2958` | 71 | 无(UmqSocket 析构顺带触发,3.6% 的来源之一) |
| `GetGenCheckStats` | `:2961-2971` | 11 | 无 |

**内部函数(匿名 namespace,~30 个,不可直接测,只能经公共 API 驱动)**: `GetBigdataState`(:239)、`GetUmqSocketLookup`(:250)、`ConfigureControlSend`(:264)、`FreeQbufChain`(:293)、`ReleasePinned`(:305)、`SweepExpiredPinned`(:359)、`BuildSmallData`(:445)、`BuildCoalescedSmallData`(:505)、`OfferBuilder`(:557,含 `CanAppend`/`Alloc`/`Append`/`FillWireSeg`/`FillWireMempoolInfo`/`SealOnly`/`Rollback`)、`ParseReadOffer`(:823)、`DrainDeferredControls`(:893)、`SendSimpleCtrl`(:962)、`DeliverToRxQueue`(:1062)、`DeleteIoUctx`(:1148)、`MarkDataWrComplete`(:1164)、`FinishPosting`(:1172)、`FinalizeIo`(:1185)、`DoReadOffer`(:1409)、`RetryPendingReads`(:1743)、`validateSegs`(:1832)、`FlushPendingOffer`(:1890)、`AllocNewOffer`(:1923)、`HandleSmallSegment`(:1942)、`HandleCoalescedSmallSegments`(:1979)、`HandleLargeSegment`(:2009)、`SweepExpiredCtxs`(:2775)、`DumpGenCheckStatsAtExit`(:2976)、`StatsDumpThread`(:2993)。

> 关键测试形态推论: 内部函数全部在匿名 namespace,`#include` .cpp 会产生与库内副本不同的第二份符号(匿名 namespace 跨 TU 是不同实体),**不可行**;唯一路径是经 9 个公共 API 间接驱动。这也是 §8 单 target 单文件方案的依据。

**errno 处理模式**: 无 `UmqErrnoConverter` 参与(通用层);直接设置 errno——`EINVAL`(`:1838/:1844/:2120`)、`EOVERFLOW`(`:1851`)、`EPIPE`(`:2131`)、`ENOMEM`(`:2218`)、`EAGAIN`(`:600/:2371/:2387`)、兜底 `errno != 0 ? errno : EIO`(`:2285`)。测试遵循 SKILL.md §5"errno 先设后测"。

**未覆盖错误路径(按函数)**: 见 §6 边界清单 + §9 风险;重点缺口: `OfferBuilder::FillWireMempoolInfo` 三个失败分支(`umq_data_to_head` 返回 nullptr/越界 :680、`umq_mempool_info_get` 失败 :691、blob_len 越界 :699)、`DoReadOffer` 的 mempool 状态四态(:1487-1497)、`FinalizeIo` 的 gen mismatch 丢弃(:1314-1348)与 failed 路径(:1279-1308)、`RetryPendingReads` 的失败排空循环(:1800-1828)、`TrySenderPost` 的部分接受/尾部回滚(:2342-2391)。

### 4.2 `ubsocket_bigdata_order.h`(纯头文件)

7 个 inline 函数 + 1 个 struct,当前**零覆盖**(数据源 §6 零覆盖 .h 表,51 行):

| 函数 | 行号 | 边界点 |
|------|------|--------|
| `BigdataOrderDebugEnabled` | :44-52 | env 首调用缓存(不可重置,见 §3.3) |
| `BigdataOrderPrefix` | :54-62 | `data==nullptr || len==0` 短路;`len < 8` 截断采样 |
| `PrepareSyntheticReadQbuf` | :64-77 | `qbuf==nullptr` 短路;字段填充 |
| `FindReadWrTail` | :79-96 | `head==nullptr || total_data_size==0`;单 buf 链/多 buf 链/中途 data_size 越界/总和不足/正好走到尾 |
| `ValidateStandaloneReadQbufChain` | :98-115 | `expected_total` 不匹配;`tail->qbuf_next != nullptr` |
| `ConfigureOrderedReadCompletions` | :125-148 | `slots==nullptr/count==0/count>UINT32_MAX`;`pending==nullptr`;opcode≠READ;`ordered_with_previous` 被 `(void)` 忽略 |
| `LinkReadQbufsInOrder` | :150-174 | 空参/`count==0`;中间槽 `FindReadWrTail==nullptr`;头尾输出 |
| `PrependReadQbuf` | :176-186 | 空参短路;`*tail==nullptr` 时 head/tail 同时指向 prefix |

**测法**: 该头无 .cpp,测试文件直接 include 即实例化;建议并入 bigdata 测试文件的一个"order helpers"分组(纯内存操作,无需 mock,成本极低),随 `ubsocket_static` 覆盖即可转达标。

## 5. 覆盖率现状

### 5.1 数据来源

| 来源 | 内容 |
|------|------|
| `UBSOCKET-COVERAGE-ANALYSIS.ch.md` §5 逐文件 .cpp 表 / §6 零覆盖 .h 表(唯一权威,快照 2026-08-28) | 本节所有数字 |

### 5.2 目标范围覆盖率(全部引自数据源)

| 文件 | 行% | 函数% | 分支% | 可执行行(LF) | 达标? |
|------|-----|-------|-------|-------------|-------|
| `core/ubsocket_bigdata.cpp` | **3.6** | **15.5** | **1.4** | 1484 | 否(起点极低,基本全裸) |
| `core/ubsocket_bigdata_order.h` | **0.0** | — | — | 51 | 否(数据源 §6 零覆盖 .h 表) |

`ubsocket_bigdata.h` 无可执行行(纯声明),不计入。

## 6. 边界敏感判定点清单(候选,按 SKILL.md §2 六类)

> 评审按此清单核对"边界值 + 相邻值"用例;认领时登记到 `ut-gen/modules/core.md` §边界清单。

**A. 长度/容量截断**
- `seg.len` vs `UBS_SMALL_DATA_MAX=4064`(proto.h:41): `s.len > small_max` 走大段(`:2214`)。边界: 4064(小)/4065(大);0(空段,validateSegs 不拒 0,但 `BuildSmallData` 分配 0 字节 qbuf)。
- 小包合并预算: `total + len > small_max` 截止(`:2233`)。边界: 累计恰 4064(合并)/4065(拆出)。
- `OfferBuilder::CanAppend`: `nsegs >= UBS_SEG_MAX=32` 截止(`:582`)+ 4064B wire 预算(`:586`)。边界: 第 31→32 段可塞/第 33 段触发封板开新 offer。
- 批提交上限: `post_count = min(batch.size(), UMQ_BATCH_SIZE=256)`(umq_types.h:156)(`:2310`)。边界: 批 256(全发)/257(尾 1 个回滚)。
- `mempool_info_len` 下界 `UBS_MEMPOOL_INFO_HDR_SIZE(24)+UBS_URMA_SEG_T_SIZE(48)=72`(`:699-700/:860/:2497`)与上界 `UMQ_MEMPOOL_INFO_MAX_SIZE`(`:700`)。边界: 72(过)/71(拒)。
- `ParseReadOffer` 溢出防护: `infos_bytes > qbuf->data_size - infos_off - info_len`(`:865`)。边界: 恰好填满/超 1 字节。
- `HandleRxControl` segs 区域: `segs_region > data_size - UBS_CTRL_HDR_SIZE`(`:2490`)。
- `validateSegs`: `total > SSIZE_MAX` → `EOVERFLOW`(`:1850`);`offset > offset+len` 32 位溢出(`:1843`)。

**B. 队列满/空**
- `deferred_ctrl` 满(`UBS_BIG_DEFERRED_CTRL_MAX=128`,`:77`): `dq->size() >= 128` → 丢最旧(`:1036-1043`)。边界: 127(入队)/128(丢旧)。
- `pending_reads` 满(`UBS_BIG_PENDING_READ_MAX=64`,`:196`): `pr->size() < 64` 才入队(`:1691`),满则落 FinalizeIo-as-failed。边界: 63(入队)/64(丢弃)。
- 空队退出: `DrainDeferredControls` 空队 return(`:904`);`RetryPendingReads` 空队/`destroying` return(`:1754-1758`)。

**C. 容量上限**
- `nsegs`/`nmempool_infos` 协议上限 32:`ctrl_offer_counts_valid`(`:829`)、`HandleRxControl` 的 `> UBS_SEG_MAX` 拒(`:2465`)。
- `data_list->nsegs`(uint16)全量遍历(`:2212`);`segs==nullptr` 拒(`:2119`)。

**D. 超时/熔断**
- `SweepExpiredPinned` 两阶段: stage-1 `now >= deadline_ns`(`:390`)→ 清 headroom gen + 起 grace;stage-2 `now >= grace_deadline_ns`(`:375`)→ DecRef + erase。grace = `UBS_GRACE_MS`(默认 100,`ubsocket_global_setting.cpp:51`)。边界: 到达 deadline 前 1ns/到达;grace 到期前/后。
- `SweepExpiredCtxs`:`pending_reads` ctx 到期直接删(`:2791`);`active_io` ctx 到期只置 failed 等 FinalizeIo(`:2805`)。
- deadline 公式: `now + effective_timeout(本地?本地:peer) + UBS_BIG_PIN_TIMEOUT_MARGIN_MS`(`:2152-2178/:1539-1542`)。`UBS_BIG_PIN_TIMEOUT_MARGIN_MS` 默认 1000(:50)。
- RNR 软反压: `status == UMQ_BUF_RNR_RETRY_CNT_EXC`(99)不累计不释放(`:2589/:2664`)。

**E. 枚举/映射表**
- `UbsCtrlType` 三值(proto.h:65-69): `HandleRxControl` 类型校验(`:2454`)与 switch 分发(`:2527-2553`);`DoReadOffer` 的 `type != UBS_READ_OFFER` 拒(`:1440`)。
- mempool 状态四态(umq 语义): `UMQ_REMOTE_MEMPOOL_STATE_REUSE`(跳过)/`_ERR`(ABORT)/`NEED_IMPORT`/`NEED_REIMPORT`(走 `umq_mempool_info_set`)(`:1487-1507`)。
- errno 映射: 见 §4.1;边界为"调用前设置 errno + 断言最终 errno"模式。

**F. 版本/协议协商**
- 无显式版本协商字段;`read_gen` 的 0/非 0 语义即校验开关(:731 `has_sliced_seg ? 0 : gen`,=0 时 `g_read_gen_fallback_count` 加一)。边界: gen=0 无校验/非 0 有校验;`g_read_gen.fetch_add` 返回 0 的 skip-0 保护(`:2163-2166`)。

## 7. mock 策略与陷阱要点(core 模块 + bigdata 特化)

1. **UmqApi 一律 `MOCKER_CPP(::umq_xxx)`**(SKILL.md §5,adapter 后端无 `_ptr`);`umq_post` 是最高频挂载点,建议按"成功/EAGAIN 族/其他错误/部分接受(bad≠head)"四类返回组织用例。
2. **`UbsBigdataSocketState` 构造调 `LockRegistry::LOCK_OPS.create`**(`:113`)——SetUp 必须先 `LockRegistry::RegisterDefaultOps()` + `ArraySet<Socket>::GetInstance().Init()`(SKILL.md §4 fixture 模板;参考 `ubsocket_tx_unified_poller_test.cpp:43-57`)。
3. **必须真实 `UmqSocket` 对象**: `MakeRef<UmqSocket>(fd)` + `ArraySet::OverrideItem(fd, sock)`;`RefConvert` 失败路径(`HandleRxControl:2515`、`GetBigdataState:242`)用**非 UmqSocket 的 Socket**(如孤儿测试的 DummySocket 思路)或未注册 fd 覆盖,正例路径一律走真实 UmqSocket。
4. **mock 必须对象构造前设置**(SKILL.md §5): 特别是 `::umq_buf_free`——`UmqSocket` 析构触发 `CleanupSocketState`(`umq_socket.cpp:41`)→ `FreeQbufChain` → `::umq_buf_free`,TearDown 释放对象前 mock 须仍在。
5. **`::ubsocket_iobuf_deallocate` 必须挂** `MOCKER_CPP`(或 `expects(exactly(0))` 兜底): `Block::DecRef` 归零即调(`ubsocket_iobuf.h:65`),`ReleasePinned`/`SweepExpiredPinned`/`CleanupSocketState` 都会走到;`Block` 用真实构造(内存自管,`Block(data, size)` 即可)。
6. **`GetTxOps()` 默认返回 nullptr**(inline 经 `DataPlaneTable::Live`,`ubsocket_socket.h:223-227`): 各 `tx_queue_avail_num_.fetch_sub` 记账分支(`:925/:1012/:1658/:1773/:2415`)与 `TxCqePoller::MarkActive(umq_sock)` 的 `umq_sock!=nullptr` 分支默认可达性不同——想覆盖记账分支需 `ReinitTxOps()` + 注入栈上 `UmqTxOps`(参考 `ubsocket_tx_unified_poller_test.cpp:160-167`),否则该批分支列入豁免评估。
7. **`TxCqePoller::Instance()` 单例无需清理**: `UBS_TX_UNIFIED_POLL_ENABLED=false`(默认)时 `NotifyInflight/NotifyPosted` no-op、`MarkActive` 仅改活跃集;不 Start 则不产生线程。
8. **全局原子不可重置**(§3.3): 计数断言用 `GetGenCheckStats()` 读"前后差";`g_read_gen` 单调递增不可断言绝对值。
9. **`GlobalSetting` 四个 bigdata 相关静态变量**(`ubsocket_global_setting.cpp:41/49/50/51`)SetUp 显式设值、TearDown 恢复默认;改 `UBS_READ_GEN_CHECK_ENABLED` 前先想清楚 gen 路径副作用(它同时开启 pin deadline/两阶段 sweep/read_gen 校验三条线,建议分用例组开关)。
10. **精确定数断言**: `umq_post`/`umq_buf_alloc` 用 `.expects(exactly(N))` 验证批次数与回滚次数(SKILL.md §5;core.md 实例)。
11. **mock 漏挂载假阳性**(SKILL.md §8 陷阱 12/13): 错误路径用例(如 `umq_mempool_info_get` 失败)必须核对 `MOCKER_CPP` 已挂,注入变量必须有 `invoke` 引用。
12. **`mockcpp::Result` vs `ock::ubs::Result` 冲突**(AGENTS.md): `using namespace ock::ubs;` + 显式限定。
13. **`errno` 先设后测**: 如 `TrySenderPost` 非 umq socket 路径(`:2131 EPIPE`)、`validateSegs`(`:1838/:1844/:1851`)、兜底 EIO(`:2285`)。
14. **`UBS_DATAPATH_LOG`/`UBS_VLOG_*` 在测试中正常输出**,不阻塞;行宽 120 约束对测试代码同样适用。

## 8. CMake 注册计划

- **位置**: 顶层 `src/ubsocket/unit_test/CMakeLists.txt`(bigdata 属 `csrc/core` 不含 umq,`umq/` 子目录的 `add_umq_test` 带 UMQ include 与约定不适用;参考 `ubsocket_bigdata_proto_test` 块 `:713-744` 与 SKILL.md §6 顶层模板)。
- **模板**(在 proto test 块后追加):
  ```cmake
  add_executable(ubsocket_bigdata_test "")
  add_test(NAME ubsocket_bigdata_test COMMAND ubsocket_bigdata_test)
  set_target_properties(ubsocket_bigdata_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
  target_compile_features(ubsocket_bigdata_test PRIVATE cxx_std_17)
  target_compile_definitions(ubsocket_bigdata_test PRIVATE UBSOCKET_UNIT_TEST)
  target_compile_options(ubsocket_bigdata_test PRIVATE -fno-access-control)
  target_include_directories(ubsocket_bigdata_test PRIVATE
      ${UBSOCKET_BASE_DIR}/csrc
      ${UBSOCKET_BASE_DIR}/csrc/common
      ${UBSOCKET_BASE_DIR}/csrc/core
      ${UBSOCKET_BASE_DIR}/csrc/iobuf
  )
  target_link_libraries(ubsocket_bigdata_test PRIVATE
      # 不链 boundscheck(securec 安全库): csrc 生产代码与测试代码均无 securec 函数调用,
      # 链了会给动态库版本引入多余 DT_NEEDED;实测对照(2026-08-28):
      # ubsocket_socket_helper_test 不链 boundscheck 构建运行正常
      ubsocket_static mockcpp GTest::gtest_main pthread
  )
  target_sources(ubsocket_bigdata_test PRIVATE ubsocket_bigdata_test.cpp)
  ```
- **`-fno-access-control` 按需加**(AGENTS.md: 顶层 target 按需,实例 `ubsocket_tx_unified_poller_test`): 构造/检查 `UbsBigIoCtx`、`wr_slots`、`tx_queue_avail_num_` 等 private 成员时需要;若全部经公共 API + `GetGenCheckStats()` 断言则可不加。
- **禁止重复注册**: 不学 `ubsocket_umq_setting_multi_level_test` 顶层+umq 双注册模式(SKILL.md §4)。
- 链 `ubsocket_static` 的 target 运行时需 `LD_LIBRARY_PATH=src/hcom/umq/build/src:src/hcom/umq/build/src/qbuf`(AGENTS.md)。

## 9. 工作量与风险

### 9.1 规模估算

- `ubsocket_bigdata.cpp` 1484 可执行行 / 约 40 个函数(9 公共 + ~30 内部 + 3 UmqSocket 成员 + 类内方法)。
- 横向参照(umq 系列实测): `umq_tx_helper.cpp` 542 行 → 73 用例;`umq_data_tx_ops.cpp` 885 行 → 81 用例;`umq_backend.cpp` 584 行 → 61 用例。bigdata 行数最大(1484)且状态机复杂,预估 **180-250 用例**。
- **文件划分建议**: **一个 target 一个文件**(`ubsocket_bigdata_test.cpp`)。理由: 内部函数匿名 namespace 不可跨 TU 复用(§4.1),公共 API 是唯一入口,拆分 target 只带来重复的 fixture 基建;单文件超长时按"内容校验组 / 发送路径组 / 接收与完成路径组 / 超时与清理组"分段组织。
- **实际拆分(2026-08-29,已执行)**: §1 `ubsocket_bigdata_order.h` 纯头直测(29 用例)拆为独立 `ubsocket_bigdata_order_test` target。触发理由并非用例数超限(~250),而是**依赖形态差异**: order.h 直测零 mock、零 fixture、零 UMQ 调用,混在链 `ubsocket_static`+mockcpp 的重 target 里每构建付全量链接代价,且覆盖归因与 bigdata.cpp 混淆。新 target 仿 `ubsocket_bigdata_proto_test` 先例但更轻——**仅链 `GTest::gtest_main`**(无 mockcpp、无 ubsocket_static),include 需保留 `${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq`(order.h 依赖 umq_pro_types.h/umq_types.h 纯类型头)。bigdata_test.cpp 保持 118 用例。
- 建议分 4 个测试分组(fixture 共享):
  1. **内容校验组**(吸收孤儿测试拒绝路径,~20 用例): `HandleRxControl` 校验门禁 + `ParseReadOffer` 等价输入构造(经 `HandleRxControl` 驱动)+ `ubsocket_bigdata_order.h` 7 个 inline 函数直接单测。
  2. **发送路径组**(~60 用例): `TrySenderPost` 全部分支(空列表/参数错/validateSegs/4064 边界/coalesce/offer 封板/UMQ_BATCH_SIZE 截断/umq_post 全拒/部分接受回滚/SN 回退/ReleasePinned 回滚)。
  3. **接收与完成路径组**(~70 用例): `DoReadOffer`(经 `HandleRxControl` 正例)+ `FinalizeIo`(经 `HandleTxCompletion` 的 READ CQE 驱动)+ `HandleTxCompletion` 三分支(SEND_IMM+bit20 / READ / 普通 SEND)。
  4. **超时/回压/清理组**(~40 用例): `SweepExpiredForSocket` 两阶段、`SweepExpiredCtxs`、`RetryPendingReadsForSocket`、`DrainDeferredControls`、`HandleFlowControlUpdate`、`NeedsPollerAttention`、`CleanupSocketState`、`GetGenCheckStats` 计数增减。

### 9.2 阻塞点与风险

| 风险 | 等级 | 说明与对策 |
|------|------|-----------|
| 环境搭建重 | 高 | 每个正例用例都要 `RegisterDefaultOps` + `ArraySet.Init` + `MakeRef<UmqSocket>` + `OverrideItem` + 4~8 个 `MOCKER_CPP`;按 SKILL.md §5 提取 `MountXxxMocks()`/`PrepareXxx()` helper(umq_backend_test 实战模式) |
| `FinalizeIo` 经 CQE 驱动 | 中高 | `HandleTxCompletion` 的 READ 分支要求 `pro->user_ctx` 指向真实 `UbsBigQbufSlot`——需 `-fno-access-control` 直接构造 `UbsBigIoCtx`/`wr_slots`,或先走 `DoReadOffer` 全链(先 mock 全部 UMQ API 让 post 成功,再模拟 CQE);两者都成立,选后者信息量更大 |
| 并发竞态路径难测 | 中 | `destroying`/`finalize` 双跑/double-finalize 守卫(`:1192-1197`)、`DecreaseRef` 删除 state 等竞态单线程难触达;建议明确豁免并登记(参照 umq_transport_pool_test 死代码豁免先例) |
| 全局原子不可重置 | 中 | `g_read_gen`/`g_seq` 跨用例漂移;计数断言用 GetGenCheckStats 差值 |
| `GetTxOps()` 记账分支 | 中 | 默认 nullptr 使 `tx_queue_avail_num_` 分支不可达;需 `ReinitTxOps()`+注入 `UmqTxOps` 才能覆盖,工作量另加;或列入豁免评估 |
| `BigdataOrderDebugEnabled` env 缓存 | 低 | static 缓存不可重置;只测默认分支或 `UBS_BIG_ORDER_DEBUG` 未设形态 |
| `StatsDumpThread` | 低 | 测试环境不得设 `UBS_GEN_CHECK_STATS_DUMP=1`(3s 起线程 + atexit) |
| 单用例 ≤1s | 低 | 无轮询/sleep 依赖;超时路径用 `UBS_GRACE_MS`/`UBS_BIG_PIN_TIMEOUT_MARGIN_MS` 设 0 或直接注入 `deadline_ns` 构造(需要 -fno-access-control 或经公共 API 先造有 deadline 的 entry) |

### 9.3 实施批次计划

**一次认领、三批产出**(2026-08-28 定稿;3k+ 行 / 预估 180–250 用例,一次写完构建调试集中爆发且 6 条阻塞风险需边写边验证模式,故分批;原"五刀"覆盖顺序重组织为批次,内容不变):

| 批次 | 内容 | 完成标准 | commit 粒度 |
|------|------|---------|------------|
| 批 1: 基建+发送侧 | fixture 基建 + `MountXxxMocks()`/`PrepareXxx()` helper 体系(§5 umq_backend_test 实战模式);内容校验组(纯数据构造,无 UmqSocket 依赖,成本最低,吸收孤儿测试 ~14 个拒绝路径用例,贡献 `ubsocket_bigdata_order.h` 51 行);`TrySenderPost` 发送路径(UMQ API 全 mock) | 构建 + 单 target ctest 全绿;**mock 模式可行性确认后才进入批 2**(`GetTxOps` 注入、CQE 驱动等阻塞点在批 1 验证) | 基建 1 + 发送侧 1–2 |
| 批 2: 接收侧 | `DoReadOffer` → `HandleTxCompletion` → `FinalizeIo`(READ 分支: 先走 `DoReadOffer` 全链 mock 全部 UMQ API 让 post 成功,再模拟 CQE) | 构建 + ctest 全绿;行覆盖率接近 80/50 | 1–2 |
| 批 3: 收尾 | 超时/回压/清理 + `GetGenCheckStats` 计数断言(差值模式);边界清单逐项(§6) + 豁免登记(并发竞态、`GetTxOps` 记账分支评估);过 SKILL.md §9 评审清单 → 新陷阱回流 `ut-gen/modules/core.md` → 认领流转 done | 覆盖率达标(行≥80%/分支≥50%);评审清单全勾 | 1 + 合入 |

约束: 每批保持全量 ctest 全绿;单 target 单文件(§8),测试文件内按公共 API 分 section,支持分批 review。

**批次间交接约定**(跨上下文接力时仓库是唯一载体,会话之间不传对话记忆):
- 每批结束时,在当前批次小节下追加"批次小结",固化 4 项: ①完成内容与 commit hash;②覆盖率增量(数字入 `UBSOCKET-COVERAGE-ANALYSIS.ch.md`,小结只引用);③已验证的 mock 模式结论与批 2 路线修正(如 `GetTxOps` 注入是否必要、`FinalizeIo` 用全链还是直构);④遗留事项/新陷阱(新陷阱回流 core.md)。
- 下一批的开场指令: 加载 ut-gen + core.md,先读本文件"批次小结"与 §9.2,再开工;同一时刻仅一个会话在本文件对应的测试文件上工作(批次严格顺序,不并行)。

## 批次小结(批 1: 基建+发送侧,2026-08-28)

**① 完成内容与 commit hash**: commit `67e57d0e`(2 文件 +1639 行)。`ubsocket_bigdata_test.cpp` 81 用例: order.h 7 个 inline 函数直测(29 用例,零覆盖贡献)、HandleRxControl 内容校验门禁(20 用例,吸收孤儿测试拒绝路径,改真实 UmqSocket + mockcpp,正例 DONE/ABORT 走 ReleasePinned)、TrySenderPost 发送路径(32 用例: 入口校验/validateSegs/4064 边界/coalesce/offer 封板/256 批截断/全拒与部分接受回滚/SN 回退/ReleasePinned 回滚/gen check 头房/统一轮询 Notify 路径)。`unit_test/CMakeLists.txt` 注册 target(仿 tx_unified_poller_test 块)。

**② 覆盖率增量**(2026-08-28 全局重跑,coverage_detailed.txt): `ubsocket_bigdata.cpp` 行 0% → **42.0%**(1484 行),函数 63.8%(58),分支 21.6%(1726);`ubsocket_bigdata_order.h` 行 **100%**(80)/函数 100%(9)/分支 84.1%(82)。批 1 起点快照的全局数字见数据源;本批增量刷新待合入后按协作规则-2 执行。

**③ 已验证 mock 模式结论与批 2 路线修正**:
- §9.2"`GetTxOps()` 记账分支"风险**已解除,无需豁免**: 仅 `ReinitTxOps` 时条目内 `txw` 壳的 `tx_ops_` 仍是默认 nullptr,需走 `SocketBase::GenerateSocketCommOps(sock)`(ubsocket_socket.cpp:97-119,内部 ReinitTxOps+ReinitRxOps+装配壳)后 `GetTxOps()` 才非空;TxDepthAccounting 用例已覆盖 `tx_queue_avail_num_` fetch_sub 与 poller MarkActive。批 2 发送完成路径直接复用该装配模式。
- **Fake 池必须稳定指针**: mock 后端若用 `std::vector` 存 buf 池,生产代码中途再 alloc 触发扩容 → 旧 buf 指针 UAF(valgrind 定位 `ubsocket_bigdata.cpp:2314`);改用 `std::deque`(emplace_back 不使既有元素地址失效,贴近真实 UMQ 池分配语义)。
- 大段判定基于 `seg.len > UBS_SMALL_DATA_MAX`(4064): 测 offer 路径的用例段长必须 >4064,否则静默走 small 路径(read_gen==0 是池 memset 巧合)。
- 批 2 `FinalizeIo` 全链 vs 直构仍未定,批 2 开工时按 §9.2 先试全链(CQE 驱动),受阻再直构 `UbsBigIoCtx`(-fno-access-control 已具备)。

**④ 遗留事项/新陷阱**:
- 新陷阱(待回流 `ut-gen/modules/core.md`): mock 池指针稳定性(deque 而非 vector);大段用例段长阈值;`GenerateSocketCommOps` 装配壳后才能取 GetTxOps()。
- 遗留: bigdata.cpp 接收侧(`DoReadOffer`/`HandleTxCompletion`/`FinalizeIo`)与超时/回压/清理组 → 批 2/批 3;并发竞态豁免登记保留;`StatsDumpThread` 环境(§9.2)未测。
- 批 1 全量 ctest 55/55 全绿;单 target ctest 81/81;单用例均 ≤100ms。

## 批次小结(批 2: 接收与完成路径组,2026-08-28)

**① 完成内容与 commit hash**: commit `a657799e`(1 文件 +904 行)。`ubsocket_bigdata_test.cpp` 81→112 用例,新增 31 个,接收侧全链驱动(DoReadOffer 经 HandleRxControl;FinalizeIo 经 CQE 驱动——批 1 遗留的"全链 vs 直构"问题**已定案:全链**,见 ③):
- 接收正例: 单段 offer post READ(remote_sge/MP 三元组/first_sn 盖戳/tx_queue_avail fetch_sub/统一轮询 NotifyInflight/peer 超时 deadline 分支)+ CQE 清理 + rxQueue 交付;gen check 开启时 READ 范围前移 8B。
- offer 失败路径: seg idx 越界 / mempool state ERR / NEED_IMPORT 导入后续投 / import 失败 / alloc 失败(无 post 无断链) / get_remote_fields 失败 / post EAGAIN 延迟(无 ABORT 无释放) / 部分提交等终端 CQE 再 finalize / 非重试错误立即 finalize-as-failed / EAGAIN 队列满 64 丢弃 + 断链 / bad==nullptr 按"全部已提交"处理。
- CQE 语义: 单 WR 成功交付 DONE / 错误 status(12) ABORT+CLOSE / RNR(99) 不累计不释放 / 多 WR 第二 CQE 才 finalize / gen 不匹配静默丢弃(无 ABORT 无断链,计数+1) / gen 匹配剥离 8B 头房交付 / data_size<8 视为不匹配丢弃 / user_ctx 无槽位直接释放 / 清理后 destroying 路径静默释放。
- ctrl CQE: 有效 DONE/ABORT 释放 + span=1 / buf_data 空 false / RNR 不释放。
- SEND CQE 触发 pending_reads 重投: 成功重投→CQE→DONE / 持续 EAGAIN 留守队首 / 其他错误排空 + 逐个 finalize-as-failed。
- HandleRxControl 门禁补漏: socket 已 CLOSE 拒绝。

**② 覆盖率增量**(2026-08-28 全局重跑,gcov 直读): `ubsocket_bigdata.cpp` 行 42.0% → **79.4%**(1484 行),分支 executed 67.6% / taken-once 43.5%;`ubsocket_bigdata_order.h` 保持 100%。增量集中在 DoReadOffer/HandleTxCompletion/FinalizeIo/RetryPendingReads。剩余 ~306 行: 防御性/不可达(DoReadOffer state==nullptr、ctrl 门禁、destroying-mid-import、ctx alloc 失败、ConfigureOrderedReadCompletions 失败、FinalizeIo ctx->state==nullptr、pro==nullptr)+ 发送侧遗留子分支(validateSegs/FlushPendingOffer/coalesce)+ 后续批次范围(HandleFlowControlUpdate/SweepExpiredForSocket/CleanupSocketState 细节)。分支 taken-once 距 50 的缺口主要在批 3 超时/回压/清理组。

**③ 已验证 mock 模式结论与批 3 路线修正**:
- **FinalizeIo 全链定案(CQE 驱动)**: 无需直构 `UbsBigIoCtx`。DoReadOffer 经 HandleRxControl 全链驱动后,CQE 用 `slot->pending` 指向的同一 READ dest qbuf 注入 status 调 `HandleTxCompletion` 即触发 FinalizeIo;不完整类型限制(匿名 namespace 内 ctx)全部绕开。
- **FakeUmqPost 的 bad_qbuf 语义必须对齐真实 UMQ**: post 失败时 `bad` 指向第一个未提交 WR;未提交任何 WR 必须 `bad==head`(fake 用 `post_bad_index=0`),**不能 nullptr**——DoReadOffer 对 `bad==nullptr` 按"全部已提交"处理(submitted_wrs=size,等 CQE),`bad==head` 才走立即 finalize。post_bad_index=-1 仅用于测 bad==nullptr 的假定路径。
- **RNR(99) 软反压**: HandleTxCompletion 对 READ/CTRL CQE 在 status==99 时返回 false,不累计完成计数、不释放 buf——bondp 重试后同一 buf 的新 CQE 才走正常路径;误累计会在 wr_total=1 时提前 finalize → double free。
- **post_ret 残留污染计数**: FinalizeIo 的 ABORT/DONE 与重投共用同一 `umq_post` mock;模拟 CQE 前若 `post_ret` 仍是错误值,ABORT 会进 deferred_ctrl 队列并被 DrainDeferredControls 重投 → posts 计数 +1。CQE 前必须复位 `post_ret=0`。
- **FindReadWrTail 链校验要求 `data_size==total_data_size`**(单 buf): 破坏 data_size 的用例必须同步改 total_data_size,否则 LinkReadQbufsInOrder 失败 → 误入 failed 路径(ABORT+CLOSE),测不到目标分支。
- **gen_mismatch 是全局累计计数**,断言必须用 delta;且 `data_size<8` 分支不计入该计数(仅 got!=expect 分支 `g_read_gen_mismatch_count` +1)。
- **`GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED` 是全局 flag**: 用例内临时开启覆盖 NotifyInflight 后必须恢复,否则后续用例顺序依赖失败。

**④ 遗留事项/新陷阱**:
- **新发现真实代码缺陷(已修复,2026-08-31 commit `ad8c3639`)**: `DoReadOffer` get_remote_fields 失败路径(ubsocket_bigdata.cpp:1589-1593) 原只 `FreeQbufChain(dest)` 后发 READ_ABORT 即 return——`ctx`(new'ed)、`ctx->offer_rx_buf`(offer RX buf)与 state 引用全部泄漏:未 DeleteIoUctx,且 ctx 尚未入 active_io,CleanupSocketState 扫不到。对照 alloc 失败路径(1564-1575)的完整清理(FreeQbufChain(offer_rx_buf)+DeleteIoUctx+ABORT),明显遗漏。修复镜像 alloc 失败分支(issue 01): 释放已入队 wr_slots pending + offer_rx_buf(置空)+ DeleteIoUctx + ABORT;测试 `HandleRxControl_OfferGetRemoteFieldsFail_AbortSent` 断言更新为 free==2(移除"已知缺陷"注释),新增多段中途失败用例(free==3)。
- 新陷阱(待回流 `ut-gen/modules/core.md`): bad==head 语义、RNR 不释放、post_ret 残留、data_size==total_data_size 链校验、gen 计数 delta、全局 flag 恢复。
- 遗留: 超时/回压/清理组(HandleFlowControlUpdate/SweepExpiredForSocket/NeedsPollerAttention/CleanupSocketState 细节)→ 批 3;DoReadOffer destroying-mid-import、FinalizeIo ctx->state==nullptr 等防御分支继续豁免。
- 批 2 全量 ctest 55/55 全绿;单 target 113/113(审查修复补 NEED_REIMPORT 四态用例后,2026-08-29);单用例均 ≤100ms。

## 批次小结(批 3: 超时/回压/清理收尾组,2026-08-29)

**① 完成内容与 commit hash**: commit `1892ecff`(测试 1 文件; docs 见紧随的 docs commit)。`ubsocket_bigdata_test.cpp` 113→147 用例,新增 34 个(§5 组,复用批 2 全链 helpers + 5 个 §5 专用 helper(SleepPastDeadline/PostPinOffer/OfferIntoActive/OfferIntoPending/OfferImportFailIntoDeferred)):
- sweep-pin 两阶段(8): null/无 state/feature-off gate/快路径跳过/未到期/到期清 headroom+计数/grace 未到/grace 到 DecRef 释放。
- RetryPendingReads(8): null/无 state/CLOSE/retiring/空队/成功重投+finalize/持续 EAGAIN 留守/其他错误排空 finalize。
- HandleFlowControlUpdate(7): 无 state no-op/重投成功排空/仍暂停留守队首/pending_reads 重试/其他错误丢弃/retiring 排空不重投/队满 128 丢最旧(129 个 import-fail offer→128 入队,free 130、posts 257)。
- NeedsPollerAttention(6): null/无 state/pinned deadline true→done 后 false/ctx deadline→Cleanup 后 false/仅 pending/空 state false。
- CleanupSocketState(4): null/无 state/full-state 四容器收尾(DecRef+deferred 释放+pending 删 ctx+active 留待 CQE destroying 静默收尾,无 ABORT 不 close)/幂等。
- GetGenCheckStats(1): 六字段快照一致性。
**已发现并修复 1 处真实生产缺陷(SweepExpiredCtxs active_io 残留,见 ③),修复与 §5.2 ctx sweep 3 用例见 ④。**

**② 覆盖率增量**(2026-08-29 全局重跑,gcov 直读): `ubsocket_bigdata.cpp` 行 79.4% → **88.14%**(1484 行),分支 executed 67.6% → **76.6%**,taken-once 43.5% → **50.21%**;`ubsocket_bigdata_order.h` 保持 100%。**门禁双达标(行≥80%/分支≥50%)**。全仓快照与模块/逐文件数字入 `UBSOCKET-COVERAGE-ANALYSIS.ch.md`(2026-08-29 刷新: 全仓行 81.8%/函数 87.8%/分支 57.1%,60 target / 2213 case,60/60 通过;bigdata 行 88.1%/96.6%/53.4%)。剩余未覆盖: 防御性/不可达(§9.2 豁免登记)。修复后实测(2026-08-31,issue 01/02 提交 `ad8c3639`/`24c83402`): 行 89.3% / 函数 96.6% / 分支 54.7%,双达标无回退(`UBSOCKET-COVERAGE-ANALYSIS.ch.md` 快照仍为 2026-08-29,待下次全仓刷新同步)。

**③ 已验证 mock 模式结论与遗留缺陷**:
- **确定性两阶段 sweep 时序**: `UBS_BIG_PIN_TIMEOUT_MARGIN_MS=0` + `SetLocalRpcTimeoutMs(1)` → deadline=now+1ms,`SleepPastDeadline`(5ms)即过期;timeout=100ms 不 sleep 即未过期;`UBS_GRACE_MS=0` → stage-2 下次 sweep 触发,1000 → 跳过。**默认 margin=1000ms 会吞掉 1ms deadline,用例必须显式清零**(否则"静默失效")。
- **`SweepExpiredForSocket` 双 gate**: `sock==nullptr`/无 state 直接 return;`!UBS_READ_GEN_CHECK_ENABLED` → 连 sweep 都不进(快路径跳过基于两个 deadline 计数器全零)。
- **offer_rx_buf 生命周期陷阱(测试侧,已修复)**: DoReadOffer 保留 `offer_rx_buf=&tbuf.qbuf` 直到 ctx 收尾;helper 局部 TestBuf 在 helper 返回后被覆盖 → FreeQbufChain 读 qbuf_next 崩(实测间歇 SEGFAULT,整组崩溃堆栈地址 `0x2a6675625f716d75`="umq_buf*")。§5 的 offer 载体改为 **fixture 成员 `offerTbuf_`**。
- **新发现真实代码缺陷(已开发修复)**: `SweepExpiredCtxs` pending 超时分支删除 ctx 后未从 `active_io` 移除——paused ctx 在 enqueue 前也被插入 active_io(DoReadOffer 1613 行),缺失 erase 导致同一次 sweep 重复计数 `rx_ctx_timeout`(delta=2 而非 1)+ ctx delete 后 stale 指针 UAF(后续 sweep/CleanupSocketState 解引用已 free ctx)。修复: `state->active_io.erase(ctx)`(与 CleanupSocketState 2933 行既有模式一致),本地验证 150/150。对应 §5.2 3 用例见 ④。
- 全局计数断言: `GetGenCheckStats()` before/after delta(6 字段: pin_timeout/gen_mismatch/gen_fallback/rx_ctx_timeout/offer_total/pin_alive_max_ms)。

**④ 遗留事项/新陷阱**:
- 批 2 遗留缺陷(get_remote_fields 泄漏)**已修复**(2026-08-31,commit `ad8c3639`,见批 2 小结 ④)。
- 批 3 缺陷(SweepExpiredCtxs active_io 残留)修复 + §5.2 3 用例已提交(issue `.scratch/ubsocket-bigdata/issues/02`,resolved)。
- 新陷阱已回流 `ut-gen/modules/core.md` #16-19: margin 默认 1000ms 掩盖 1ms deadline;SweepExpiredForSocket 的 feature-gate;offer_rx_buf TestBuf 栈悬垂;SweepExpiredCtxs active_io.erase 缺陷。边界清单已登记(deferred_ctrl 128/pending_reads 64/两阶段 timing/mempool 四态等)。
- 豁免登记(core.md 边界清单): destroying 并发守卫、ctx alloc 失败、GetOrCreateBigdataState 失败、BigdataOrderDebugEnabled env-on、StatsDumpThread。
- 批 3 全量 ctest 55/55 全绿(含修复后多次复跑);单 target 147/147(含修复时 150/150);单用例均 ≤100ms。**bigdata 认领已流转 done**。
