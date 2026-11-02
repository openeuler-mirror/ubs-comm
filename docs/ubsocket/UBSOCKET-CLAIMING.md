# UBSocket UT 覆盖率 Sprint — 协调页(认领/进度/里程碑)

> **本页是 Sprint 的动态跟踪层,也是唯一入口**: 认领、状态、分工、里程碑都在这里维护。
> 规则(写 UT 的方法/命名/mock 策略/陷阱)见 `.opencode/skills/ut-gen/SKILL.md` + `modules/` 分支;所有覆盖率数字见 `UBSOCKET-COVERAGE-ANALYSIS.ch.md`(唯一数字来源)——**本页不重复数字,认领表只存状态**。
> **全员自助**: 认领、状态流转、刷新覆盖率、登记里程碑——每位同事都可以做,按下方"协作规则"执行,不需要等专人。

## 目标

将 `src/ubsocket/csrc/` 每个模块的行覆盖率提升到 ≥80%、分支 ≥50%,越高越好(仓根 `docs/adr/0001-coverage-policy-per-module-80-50-no-cap.md`)。起点见覆盖率数据源 §1 快照。软性强制: 不设 CI 门禁,全仓行覆盖到 ~60% 时重议。

## 快速上手(5 步)

```bash
# 1. 构建 + 跑 UT(脚本自含 LD_LIBRARY_PATH)
UMQ_BUILD=on UBSOCKET_UT=on UBSOCKET_COVERAGE=on bash build/build_umq_and_ubsocket.sh

# 2. 跑全部 / 单个 target
ctest --test-dir src/ubsocket/build --output-on-failure
ctest --test-dir src/ubsocket/build -R <test_name>

# 3. 覆盖率(认领前、提交前看自己改动的文件)
cd src/ubsocket/build && make coverage
#   报告: coverage_report/index.html;全局: coverage_summary.txt
```

1. **读架构** — `UBSOCKET-ARCHITECTURE.ch.md`(不读写不出正确 UT)
2. **读规则** — `.opencode/skills/ut-gen/SKILL.md` + 对应 `modules/<module>.md`(必读)
3. **认领** — 直接在下方认领表登记(认领人/状态/测试 target),一次只认领一个 `.cpp`;优先零覆盖大文件
4. **写** — 按 ut-gen 工作流;状态流转 `unclaimed → in_progress → review → done`
5. **验证** — 单用例 ≤100ms,`ctest` 全绿,再看覆盖率增量;完成后通知协调人

## 分工框架(6 人)

> 分工由领导填写;以下按工作量给出的建议仅供参考。

| # | 模块 | .cpp 数 | 零覆盖 .cpp | 建议人力 | 分工(领导填) |
|---|------|--------|------------|---------|-------------|
| 1 | core/umq | 16 | 11 | 2 人 | |
| 2 | core(不含 umq) | 11 | 7 | 1 人 | |
| 3 | common | 6 | 1 | 与 #4 合并 1 人 | |
| 4 | top-level(csrc/ 根) | 4 | 3 | 与 #3 合并 1 人 | |
| 5 | profiling | 10 | 8 | 1 人 | |
| 6 | under_api | 3 | 3 | 1 人 | |
| — | iobuf | 0(唯一 .cpp 无可执行行) | — | wuxinyue | `ubsocket_iobuf_test` 补充 `.h` inline UT;专项口径行 100%/分支 55.2%;分支统计受 inline CFG 重复展开影响,当前进行中 |

## 认领规则(自助登记)

- 每人同一时间只认领一个 `.cpp` 文件;`.h` inline 代码可被任何人顺带覆盖(零覆盖 `.h` 清单见数据源 §6)
- **认领**: 在下表找到目标文件,直接填"认领人"并把状态改为 `in_progress`,同时登记测试 target 名(命名规则见 ut-gen §4)。填表前先确认该文件当前无人认领;改完**立即 git 提交**,把撞车窗口缩到最小
- **状态流转**: `unclaimed → in_progress → review → done`,由认领人自己流转,不需要批准
- **完成标准(自查)**: 该文件测试全绿、无 crash、单用例 ≤100ms、未引入全局状态泄漏(单例清理见 ut-gen §8);覆盖率提升以自己按 §协作规则-2 重跑后的数据源快照确认

## 文件认领表(.cpp,50 个)

> 行数、当前行/函数/分支% 见 `UBSOCKET-COVERAGE-ANALYSIS.ch.md` §5(路径与本节一一对应);本表只存状态。

### core/umq

> 认领与状态仅在此表维护(单一状态源);9 个目标文件按优先级自上而下认领(先 P0,同优先级按表序),同一时刻仅一个 `in_progress`。依据 `docs/ubsocket/UBSOCKET-UMQ-CORE-UT-RESEARCH.ch.md` §6.1。

| 文件 | 认领人 | 状态 | 测试 target | 优先级 | 依赖/说明 |
|------|--------|------|------------|--------|-----------|
| umq_buffer_receive_queue.cpp | wangshun-1999 | done | umq_buffer_receive_queue_test | P0 | 50 用例;行 92.8%/分支 71.0%;`umq_intrusive_buf_queue.h` 顺带(行 100%/分支 93.8%,含 2 个直接用例);ctest 30/30 全绿;已提交 e4f19f2c |
| umq_setting.cpp | wangshun-1999 | done | umq_setting_test | P1 | 57 用例;行 97.6%/分支 82.6%;ctest 31/31 全绿;边界清单 5 条全过;已提交 b43c9281 |
| umq_conn_helper.cpp | wangshun-1999 | done | umq_conn_helper_test | P1 | 34 用例;行 100%/分支 54.4%;ctest 32/32 全绿;边界清单 7 条全过;`MOCKER_CPP_VIRTUAL` 拦截纯虚 EpollRunner;已提交 ca65b79c |
| umq_tp_wait_queue.cpp | wangshun-1999 | done | umq_tp_wait_queue_test | P1 | 19 用例;行 97.0%/分支 58.7%;ctest 37/37 全绿;边界清单 5 条全过(容量行镜像断言,`cap==0` 回退标注死代码);已提交 0360a45c |
| umq_tx_helper.cpp | wangshun-1999 | done | umq_tx_helper_test | P2 | 72 用例;行 98.1%/分支 66.2%;ctest 40/40 全绿;边界清单 7 条全过;已提交 121cbb3b |
| umq_data_rx_ops.cpp | wangshun-1999 | done | umq_data_rx_ops_test | P2 | 58 用例;行 100%/分支 62.1%;ctest 43/43 全绿;边界清单 7 条全过;已提交 62bf268b |
| umq_data_tx_ops.cpp | wangshun-1999 | done | umq_data_tx_ops_test | P2 | 81 用例;行 94.8%/分支 61.0%;ctest 45/45 全绿;边界清单 10 条全过(审查修复 wr_num 阈值归因 batch=3);陷阱已回流 umq.md #14;已提交 ae60b08e |
| umq_transport_pool.cpp | wangshun-1999 | done | umq_transport_pool_test | P2 | 39 用例;行 97.4%/分支 69.3%;ctest 46/46 全绿;边界清单 5 条全过(审查修复 mock 漏挂载);死代码豁免 3 处登记;陷阱已回流 umq.md #15;已提交 85786f21 |
| umq_backend.cpp | wangshun-1999 | done | umq_backend_test | P3 | 61 用例;行 97.4%/分支 74.3%/函数 100%;ctest 47/47 全绿;边界清单 7 条全过;code-review 修复(删冗余 1、补 BONDING_ROUTE、断言增强、挂载 helper 提取,未覆盖分支尽数为豁免);死代码豁免 4 处登记 umq.md;已提交 00298325;2026-08-28 流转 done |
| umq_socket.cpp | yankai | done | umq_socket_test | P1 | 124 用例;行 87.0%/分支 61.2%/函数 100%;全量 ctest 67/67 全绿;边界清单已登记 umq.md(依赖最重: socket 状态机/全局 ArraySet/TxRunner 虚方法);2026-08-29 流转 done |
| umq_socket_connector.cpp | yankai | done | umq_socket_connector_test | P2 | 74 用例;行 92.0%/分支 66.3%/函数 94.4%;全量 ctest 59/59 全绿;code-review 修复(删死代码 2、memcpy_s、类静态方法统一 MOCKER、TFO 限制注释);2026-08-29 流转 done |
| umq_share_jfr_epoll_runner_ops.cpp | wuxinyue | review | umq_share_jfr_epoll_runner_ops_test | | 58 用例;官方过滤口径行 98.7%(379/384)/分支 71.1%(297/418)/函数 100%(9/9);可达业务行全覆盖,仅剩 4 行 `nothrow` OOM 防御分支和 1 行前置 `continue` 后的冗余死分支;全量 ctest 47/47 全绿 |
| umq_socket_acceptor.cpp | yankai_ | done | umq_socket_acceptor_test | | 73 用例;行 100%/函数 100%/分支 75.4%;ctest 60/60 全绿;单用例均 ≤100ms;边界清单全过;发现缺陷 1 处(DoUbAccept 中 GenerateSocketCommOps 失败被静默吞掉,UT 已记录实际行为,见提测说明);2026-08-29 流转 done |
| umq_tp_tx_epoll_runner_ops.cpp | wuxinyue | done | umq_tp_tx_epoll_runner_ops_test | | 27 用例;行 99.3%(147/148)/分支 61.5%;仅剩 D0 deleting-destructor(需多态 delete,非业务遗漏);ctest 39/39 全绿 |
| umq_errno_converter.cpp | | unclaimed | | | |
| umq_tp_event_epoll_runner_ops.cpp | wuxinyue | done | umq_tp_event_epoll_runner_ops_test | | 5 用例;行 100%/分支 62.5%;`umq_tp_wait_queue.h` 顺带(行 87.5%);ctest 33/33 全绿;分支业务路径全双边(仅剩 throw 异常边);已提交 3e2a20c7 |

> 附注: 零覆盖纯头文件 `umq_buf_converter.h`/`umq_eid_table.h` 需专项或随相关任务顺带;`umq_errno_converter.*` 冻结且已达标不产生任务;`umq_qbuf_list.h` 纯宏、`umq_epoll_ops.h` 死代码,均不补测。

### core(不含 umq)

| 文件 | 认领人 | 状态 | 测试 target |
|------|--------|------|------------|
| ubsocket_bigdata.cpp | wangshun-1999 | done | ubsocket_bigdata_test |
| ubsocket_event_epoll.cpp | | unclaimed | |
| ubsocket_tx_cqe_poller.cpp | | unclaimed | |
| ubsocket_socket_helper.cpp | yankai | done | ubsocket_socket_helper_test |
| ubsocket_socket_acceptor.cpp | yankai | done | ubsocket_socket_acceptor_test |
| ubsocket_socket.cpp | yankai | done | ubsocket_socket_test |
| ubsocket_data_rx.cpp | | unclaimed | |
| ubsocket_data_tx.cpp | | unclaimed | |
| ubsocket_wakeup_event.cpp | | unclaimed | |
| ubsocket_socket_connector.cpp | yankai | done | ubsocket_socket_connector_test |
| ubsocket_core_types.cpp | | unclaimed | |

### common

| 文件 | 认领人 | 状态 | 测试 target |
|------|--------|------|------------|
| ubsocket_global_setting.cpp | | unclaimed | |
| ubsocket_lock.cpp | | unclaimed | |
| ubsocket_thread_pool.cpp | | unclaimed | |
| ubsocket_port_cooldown.cpp | | unclaimed | |
| ubsocket_link_trace.cpp | | unclaimed | |
| ubsocket_signal_handler.cpp | | unclaimed | |

### top-level(csrc/ 根)

| 文件 | 认领人 | 状态 | 测试 target |
|------|--------|------|------------|
| ubsocket_sock.cpp | wuxinyue | in_progress | ubsocket_sock_api_test |
| ubsocket.cpp | wuxinyue | in_progress | ubsocket_test |
| ubsocket_data.cpp | wuxinyue | in_progress | ubsocket_data_test |
| ubsocket_epoll.cpp | wuxinyue | in_progress | ubsocket_epoll_api_test |

### profiling

| 文件 | 认领人 | 状态 | 测试 target |
|------|--------|------|------------|
| statistics/stat_exporter.cpp | 宋锐 | unclaimed | |
| probe/probe_manager.h | wuxinyue | in_progress | ubsocket_probe_manager_test |
| impl/ubsocket_prof_tracer_ext.cpp | 宋锐 | unclaimed | |
| impl/ubsocket_prof_tracer.cpp | | unclaimed | |
| ubsocket_prof.cpp | songrui42 | done | profiling_test(覆盖 fast/ext 双模式分派 + init/record/combind/reset/uninit 全路径,顺带内联 `Tracer::Record`/`TracerExt::RecordExt` 错误路径: 未初始化/负 CPU/越界 CPU;行已满,分支剩余为 UBS_VLOG 噪音 + std::string 异常边,数字见数据源 §5) |
| impl/ubsocket_prof_tracepoint_combiner_ext.cpp | AI(opencode) | review | ubsocket_prof_tracepoint_combiner_ext_test | private `OutputTracePointCliExt/OutputTracePointStatsExt` 直测(生产无公开入口);target 已补 `-fno-access-control`(陷阱 profiling.md #33) |
| impl/ubsocket_prof_tracepoint_combiner.cpp | AI(opencode) | review | ubsocket_prof_tracepoint_combiner_test | private `OutputTracePointCli/OutputTracePointStats` 直测(`OutputTracePointCli` 生产死代码);target 已补 `-fno-access-control`(陷阱 profiling.md #33) |
| statistics/statistics.cpp | | unclaimed | |
| trace/ubsocket_trace.cpp | AI(opencode) | done | ubsocket_trace_test(顺带覆盖 `trace/ubsocket_trace.h` 内联 `SplitTrace` 全方法,单用例 ≤100ms) |
| impl/ubsocket_prof_tracepoint_dumper.cpp | | unclaimed | |
| impl/ubsocket_prof_tracepoint_dumper_ext.cpp | AI(opencode) | review | ubsocket_prof_tracer_ext_test | `DumpDataExt` 双分支 + WARN/DEBUG 使能/禁用 4 用例(陷阱 profiling.md #32),顺带内联 `RotateDumpFileExt`/`WriteDumpTitleExt` 分支(陷阱 profiling.md #34/#35);需 `-fno-access-control`(陷阱 #33) |

### under_api

| 文件 | 认领人 | 状态 | 测试 target |
|------|--------|------|------------|
| urma/dl_urma_api.cpp | | unclaimed | |
| dl_libc_api.cpp | | unclaimed | |
| dl_api.cpp | | unclaimed | |

## 里程碑

> 数值判定一律以数据源最新快照为准;达成者自行登记——先按 §协作规则-2 刷新数据源确认数字,再填达成日期。

| 里程碑 | 判定 | 达成日期 | 备注 |
|--------|------|----------|------|
| 基线 | 2026-08-24 快照 | 2026-08-24 | 18.1% 行 / 10.9% 分支(数据源 §2) |
| 30% 行覆盖 | 数据源 §2 全局行% ≥30 | 2026-08-25 | 32.0% 行(数据源 §2) |
| 30% 行覆盖 | 数据源 §2 全局行% ≥30 | 2026-08-27 | 48.3% 达成(umq_data_tx_ops 合入后快照) |
| 50% 行 / 25% 分支 | 数据源 §2 | 2026-08-28 | 52.6% 行 / 38.8% 分支达成(umq_backend+transport_pool 合入后快照);到 ~60% 时重议是否加 CI 门禁(ADR-0003) |
| 每模块 80% 行 / 50% 分支 | 数据源 §4 逐模块 | _待定_ | 最终目标 |

## 协作规则(全员自助,无人专职)

> 没有人是专职协调员: 认领、刷新、登记、回流都由当事同事自己完成。每条规则都写明"做什么 + 怎么做"。

### 1. 认领与状态(直接改表)

- **做什么**: 认领一个 `.cpp`、流转自己的状态、标记完成
- **怎么做**: 直接在下方认领表改"认领人/状态/测试 target"列
  1. 认领前先看表——目标文件已是 `in_progress`/`review` 就不要重复认领,换下一个或与对方协商
  2. 认领: 填认领人 + 状态改 `in_progress` + 登记测试 target 名(命名见 ut-gen §4)
  3. 改完**立即 `git pull` 后提交**,把并发撞车窗口缩到最小
  4. 状态语义: `in_progress` = 开始写;`review` = 写完、自测全绿、准备合入;`done` = 已合入
  5. 标记 `done` 前自查: 测试全绿、无 crash、单用例 ≤100ms、无全局状态泄漏(单例清理见 ut-gen §8),并按规则 2 重跑确认覆盖率提升

### 2. 刷新覆盖率数据(任何人,随时)

- **做什么**: 更新数据源的快照(日期/全局/模块/逐文件/零覆盖 .h)——不依赖专人
- **什么时候做**: 认领前(看起点)、合入前(看增量)、阶段结束(全员同步)、里程碑达成(确认数字)
- **怎么做**: 按数据源 §10 执行——重跑 `make coverage` → 用 §10 的三条 awk 命令重新生成表格 → 粘贴替换数据源 §2/§4/§5/§6 → 更新 §1 快照日期与刷新人 → `git pull` 后提交
- **纪律**: 数字必须由重跑生成,禁止手改;同一天多人刷新以最后提交为准

### 3. 里程碑登记(达成者填)

- **做什么**: 达成里程碑时记录日期
- **怎么做**: 先按规则 2 刷新数据源,以 §2/§4 最新快照确认达标,再在本页里程碑表填达成日期

### 4. 知识回流(发现者直接更新)

- **做什么**: 发现新模式/陷阱时更新对应文档,不留给别人
- **怎么做**: 按目标选择——通用模式/陷阱→`ut-gen/SKILL.md`;模块特有→`ut-gen/modules/<module>.md`;数字变化→数据源(按规则 2,不手改);构建/架构→`AGENTS.md`(详见 ut-gen §11 回流表)
