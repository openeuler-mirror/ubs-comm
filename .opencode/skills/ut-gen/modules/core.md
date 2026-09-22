# Core 模块测试附录 (csrc/core/, 不含 umq/urma/)

与 `ut-gen/SKILL.md` 配合使用。通用抽象层: socket/epoll/事件/数据收发,只操作虚接口(`AcceptorOps`/`ConnectorOps`/`DataTxOps`/`DataRxOps` 等)。

## 模块范围(2026-08-24 实测)

`.h`(14): `ubsocket_bigdata.h`、`ubsocket_bigdata_order.h`、`ubsocket_buf_converter.h`、`ubsocket_core_types.h`、`ubsocket_data_rx.h`、`ubsocket_data_tx.h`、`ubsocket_event_epoll.h`、`ubsocket_proto.h`、`ubsocket_socket.h`、`ubsocket_socket_acceptor.h`、`ubsocket_socket_connector.h`、`ubsocket_socket_helper.h`、`ubsocket_tx_cqe_poller.h`、`ubsocket_wakeup_event.h`

`.cpp`(11): `ubsocket_bigdata.cpp`、`ubsocket_core_types.cpp`、`ubsocket_data_rx.cpp`、`ubsocket_data_tx.cpp`、`ubsocket_event_epoll.cpp`、`ubsocket_socket.cpp`、`ubsocket_socket_acceptor.cpp`、`ubsocket_socket_connector.cpp`、`ubsocket_socket_helper.cpp`、`ubsocket_tx_cqe_poller.cpp`、`ubsocket_wakeup_event.cpp`

注意: 不存在 `ubsocket_socket_set.*`(SocketSet 类已不存在,职责由 `ArraySet<Socket>` 承担,见 `modules/common.md`);`ubsocket_ring_buffer`/`ubsocket_qbuf_queue`/`ubsocket_spsc_ring_queue`/`ubsocket_buf_converter` 是纯头文件(在 `csrc/common/`),别在 core 找 .cpp。

## 现有测试

顶层 `unit_test/` 中与 core 相关的 target: `ubsocket_signal_handler_test`、`ubsocket_socket_helper_test`、`ubsocket_core_types_test`、`ubsocket_buf_converter_test`、`ubsocket_bigdata_proto_test`、`ubsocket_bigdata_test`、`ubsocket_bigdata_order_test`(后两者拆分自同一测试文件: order_test 专测 `ubsocket_bigdata_order.h` 纯头,仅链 gtest_main 的极轻 target)、`ubsocket_tx_unified_poller_test`、`ubsocket_degrade_api_test`、`ubsocket_socket_acceptor_test`、`ubsocket_socket_connector_test` 等(完整清单以 `unit_test/CMakeLists.txt` 为准)。

**`ubsocket_bigdata*` 深度调研**(API 清单/mock 策略/边界候选/批次计划): `docs/ubsocket/UBSOCKET-BIGDATA-UT-RESEARCH.ch.md`;认领状态见 `UBSOCKET-CLAIMING.md` core 表。

## 特有模式与陷阱

1. **系统调用直接 `MOCKER_CPP(::xxx)`** — 本模块没有 OsAPiMgr 包装类(旧文档描述已过时),`poll`/`eventfd_write` 等全局 C 函数直接 mock(实测: `MOCKER_CPP(::poll).stubs().will(invoke(&FakePollSleep15ms))`、`MOCKER_CPP(::eventfd_write).expects(exactly(0))`)
2. **精确调用次数断言** — 用 `.expects(exactly(N))` / `.expects(once())`,典型场景: 验证 poller 路径只调一次 `umq_poll`、`eventfd_write` 零次
3. **poller 线程与 trace** — `TxCqePoller` 周期轮询,走 `DrainTxCqe` 独立路径(trace=nullptr)避免 SplitTrace 爆炸;测试直接操作 private 成员需要 `-fno-access-control`(顶层 target 按需加,实例: `ubsocket_tx_unified_poller_test` 的 CMake 块)
4. **纯 TCP 兼容性** — 通用层必须保持未放入 `ArraySet<Socket>` 的 fd 语义不变;涉及 epoll 语义的测试要覆盖"非托管 fd 透传"路径
5. **`epoll_event_num_` 原子协调** — `ubsocket_event_epoll.cpp`(550 行)与旧文档描述的简单包装差异大;写测试前先读实现,别照旧 skill 描述写
6. **虚接口成员函数必须 `MOCKER_CPP_VIRTUAL(实例, &Class::method)`** — 本模块只操作虚接口(`AcceptorOps`/`ConnectorOps`/`DataTxOps`/`DataRxOps`)。虚成员函数不能 `MOCKER_CPP(&Class::method)`(member-pointer 编码成 vtable 偏移,打桩即 SEGV);须用 `MOCKER_CPP_VIRTUAL` 从实例 vtable 槽位取真实地址后函数级打桩(实测: `ubsocket_socket_connector_test.cpp` 对 `UmqConnectorOps::PrepareConnect/Negotiate/CreateSocketResources`;`umq_conn_helper_test.cpp` 对纯虚 `EpollRunner::Start`)
7. **mock 池指针必须稳定(deque 而非 vector)** — mock `umq_buf_alloc` 的缓冲池若用 `std::vector` 存元素并返回元素地址,被测代码中途再次 alloc 会触发扩容 → 旧 buf 指针 UAF(实测: `ubsocket_bigdata_test.cpp` valgrind 定位 `ubsocket_bigdata.cpp:2314` 写已 free 内存,`malloc(): unaligned tcache chunk` 且单跑不崩、多用例连跑才崩)。用 `std::deque`(`emplace_back` 不使既有元素地址失效,贴近真实 UMQ 池分配语义)。
8. **大段判定基于 `seg.len > UBS_SMALL_DATA_MAX`(4064)** — 测 `HandleLargeSegment`/offer 路径的用例,段长必须 >4064,否则静默走 small 路径(零 memset 的 buf 恰好让 `read_gen==0` 等断言"假过")。sliced seg(`start_pos != nullptr || offset > 0`)同理需 len>4064 才触发 offer `read_gen=0` fallback。
9. **`GetTxOps()` 需先经 `SocketBase::GenerateSocketCommOps(sock)` 装配** — `ReinitTxOps()` 只重建 `DataPlaneEntry`(placement-new),条目内 `txw` 壳的 `tx_ops_` 仍是默认 nullptr;只有 `GenerateSocketCommOps`(ubsocket_socket.cpp)才会 `entry->txw = DataTx(sock, tx_ops)` 装配。测试验证 `tx_queue_avail_num_` 记账/MarkActive 等尾部路径前必须先调用它,否则 `GetTxOps()` 返回 nullptr 分支静默跳过。
10. **bigdata 接收侧 full-chain 测试模式** — `UbsBigIoCtx` 在 anonymous namespace,不能直接构造;只能通过 observable effects(umq post 次数/free 计数/`umqSock_->State()`)验证。链路: `HandleRxControl(READ_OFFER)` → `DoReadOffer`(全 mock umq API)→ 向 `slot->pending` 同一个 qbuf 注入 CQE(模拟 `HandleTxCompletion`)→ `FinalizeIo`。实例: `ubsocket_bigdata_test.cpp` §4 组。
11. **`umq_post` 失败时 `bad` 语义** — 失败时 `bad` 指向第一个未提交 WR;nothing submitted → 必须 `bad == head`(fake 用 `post_bad_index = 0`);`bad == nullptr`(fake 默认) → 被测代码假设 **ALL submitted**(`submitted_wrs = wr_slots.size()`,等待永不出现的 CQE)。写 post 失败用例时先想清楚 `bad` 该指到哪。
12. **同一 mock 的返回值残留(post_ret residue)** — FinalizeIo 的 ABORT/DONE posts 与 DoReadOffer 的初始 post 复用同一个 `umq_post` mock。模拟 CQE 前必须恢复 `fake_.post_ret = 0`,否则 EAGAIN 残留会让 ABORT 进 deferred_ctrl queue、被 `DrainDeferredControls` 重新 post → posts 计数污染、断言莫名失败。
13. **RNR 软反压 CQE 不计数、不释放** — `HandleTxCompletion` 对 status=99(RNR) 的 READ/CTRL CQE 返回 false,不计完成数、不释放 buf(语义: bondp retry 后同一 buf 会再回来,提前释放即 double-free)。断言这类路径时 free/posts 计数保持不动。
14. **链校验要求 `data_size == total_data_size`** — `FindReadWrTail` 校验 read 链;只改 `data_size` 不改 `total_data_size` → `LinkReadQbufsInOrder` 失败 → 用例误进 failed 路径。构造坏链用例时要同时破坏两个字段。
15. **全局计数器用 delta 断言、全局 flag 必须恢复** — `GenCheckStats.gen_mismatch` 是全局静态计数器(跨用例累计),断言用 before/after delta;`GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED` 等全局 flag 测试结束后恢复默认值,否则污染后续用例。
16. **sweep 测试必须显式清零 `UBS_BIG_PIN_TIMEOUT_MARGIN_MS`(默认 1000ms)** — 1ms 级 deadline(`SetLocalRpcTimeoutMs(1)`/`SetPeerRpcTimeoutMs(1)` → deadline = now+1ms)会被默认 1000ms margin 拖成 1001ms,睡 5ms 根本过不了,两阶段 sweep 用例"静默失效"而非报错。正面: 用例开头 `GlobalSetting::UBS_BIG_PIN_TIMEOUT_MARGIN_MS = 0;`。`UBS_GRACE_MS`(默认 100)同理: stage-2 到达/未到达分支要显式设 0/1000。确定性时序: deadline=now+1ms, `SleepPastDeadline`(5ms)即过期;timeout=100ms 且不 sleep 即未过期(单用例仍 ≤100ms)。
17. **`SweepExpiredForSocket` 的第一个 gate 是 `UBS_READ_GEN_CHECK_ENABLED`** — feature off 时直接 return,即使 state 有 deadline 也会跳过两个 sweep(慢路径全跳过,连计数器都不动)。测试 sweep 的用例必须显式 `GlobalSetting::UBS_READ_GEN_CHECK_ENABLED = true;`(与 pinned 条目创建的 flag 一致,且记得恢复)。
18. **offer RX buf 生命周期: `DoReadOffer` 保留 `offer_rx_buf = &tbuf.qbuf` 直到 ctx 收尾**(FinalizeIo/CleanupSocketState/sweep)。TestBuf 若活在 helper 局部,helper 返回后 qbuf 内存被覆盖,后续 `FreeQbufChain` 读 `qbuf_next` 直接崩(实测间歇 SEGFAULT,valgrind 级难查)。**供 offer 的 TestBuf 必须是 fixture 成员**(`offerTbuf_`),不能是 helper 局部;TestBody 局部的 TestBuf 只要 FinalizeIo/CQE 模拟在 TestBody 内完成就安全。
19. **`SweepExpiredCtxs` pending 超时分支必须 `state->active_io.erase(ctx)`** — paused ctx 在 enqueue 进 pending_reads 前已被插入 active_io(DoReadOffer post 前插入);不 erase 则同一次 sweep 的 active 分支对同一 ctx 重复计数 `rx_ctx_timeout`(delta=2 而非 1),且 ctx delete 后 active_io 残留 stale 指针 → 后续 sweep / CleanupSocketState 解引用已 free 的 ctx(UAF)。批 3(2026-08-29)发现并开发修复(`state->active_io.erase(ctx)`,与 CleanupSocketState 既有模式一致,issue: `.scratch/ubsocket-bigdata/issues/02`);§5.2 3 用例(含 `SweepExpiredForSocket_PendingCtxExpired_FreedAndCounterUp` 断言 delta=1 + free 计数)须显式置 `UBS_READ_GEN_CHECK_ENABLED=true` + `UBS_BIG_PIN_TIMEOUT_MARGIN_MS=0`(fixture SetUp 默认 gate off + margin 1000ms,陷阱 #16/#17)。

## 测试范例

- `ubsocket_tx_unified_poller_test.cpp` — 成员函数 mock(`MOCKER_CPP(&UmqTxOps::DoUmqTxPoll)`)、精确次数断言、`LibcApi::close_ptr` 保存/恢复路由到真实 `::close`
- `ubsocket_socket_helper_test.cpp` — 类静态方法 mock(`MOCKER(&LibcApi::recv)`)与全局 C 函数 mock 混合使用
- `ubsocket_socket_connector_test.cpp` — 虚接口成员函数 `MOCKER_CPP_VIRTUAL(ops, &UmqConnectorOps::xxx)` 打桩 + `expects(never()/exactly(1))` 断言各建链阶段调用次数;`LibcApi::fcntl_ptr` 逐函数替换覆盖阻塞/非阻塞分支

## 边界清单

**`ubsocket_bigdata.cpp`**(批 1-3 登记,2026-08-29,源码行号以当时为准):

| 判定点 | 边界值 | 用例 | 批 |
|--------|--------|------|-----|
| `seg.len` vs `UBS_SMALL_DATA_MAX=4064` 大/小段路由 | 4064(小)/4065(大) | §4 小/大段 offer 用例 | 1 |
| 小包合并预算 `total+len > small_max` | 累计恰 4064(合)/4065(拆) | 批 1 合并/拆出用例 | 1 |
| `OfferBuilder::CanAppend` 32 段封板 + 4064B wire 预算 | 第 33 段触发新 offer | 批 1 封板用例 | 1 |
| 批提交上限 `min(batch.size(), UMQ_BATCH_SIZE=256)` | 批 256(全发)/257(尾回滚) | 批 1 批截断用例 | 1 |
| `mempool_info_len` 下界 72(24+48) | 72(过)/71(拒) | 批 1/批 2 用例 | 1 |
| `deferred_ctrl` 满 `UBS_BIG_DEFERRED_CTRL_MAX=128` 丢最旧 | 127(入队)/128(pop_front 丢旧) | `HandleFlowControlUpdate_DeferredQueueFull128_DropsOldest`(129 offer→128 入队,断言 free 130/posts 257) | 3 |
| `pending_reads` 满 `UBS_BIG_PENDING_READ_MAX=64` 丢交易 | 63(入队)/64(丢弃 finalize-as-failed) | `HandleRxControl_OfferPostEagain_PendingFull_DroppedAndClosed` | 2 |
| 两阶段 sweep: `now >= deadline_ns`(stage-1)与 `now >= grace_deadline_ns`(stage-2) | deadline 前 1ms/后 5ms;grace 0/1000 | §5.1 8 用例(确定性: margin 清零 + 1ms timeout + 5ms sleep) | 3 |
| `SweepExpiredCtxs` pending 删/active 置 failed | 到期前/后 | §5.2 3 用例(已提交) | 3 |
| RNR 软反压 `status==99` 不累计不释放 | 99/非 99 | 批 2 CQE 用例 | 2 |
| mempool 状态四态 REUSE/ERR/NEED_IMPORT/NEED_REIMPORT | 四态各一 | 批 2 offer 用例 | 2 |

**豁免登记**(批 3,2026-08-29): 以下分支需并发/外部条件,单线程测试不可达,豁免不测(见 research doc §9.2 与批次小结):
- `SweepExpiredPinned`/`SweepExpiredCtxs`/`RetryPendingReads`/`DoReadOffer` 中的 `destroying` 守卫(需真正并发收尾中的 ctx)
- ctx alloc 失败分支(`new UbsBigIoCtx` 失败)、`GetOrCreateBigdataState` 失败
- `BigdataOrderDebugEnabled()` env-on 分支(首调用读 env 后 static 缓存,跨用例不可重置)
- `StatsDumpThread`/`UBS_GEN_CHECK_STATS_DUMP=1` env 开启的 dump 线程路径
