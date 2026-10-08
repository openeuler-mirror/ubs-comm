---
name: ut-coverage-coord
description: Coordination skill for managing the 16-person team sprint to raise ubsocket csrc UT coverage to 80% line / 50% branch. Use for: coverage baseline, progress tracking, file claiming, priority matrix, knowledge back-flow. Trigger on keywords: coverage, baseline, 进度, 优先级, claiming, 认领, coord, 协调, sprint, 覆盖率. ALWAYS cross-reference doc/ubsocket/UBSOCKET-COVERAGE-ANALYSIS.ch.md for detailed data.
---

# UT覆盖率协调Skill

> **关键关联文档**: `doc/ubsocket/UBSOCKET-COVERAGE-ANALYSIS.ch.md` — 覆盖率基线、文件完备性验证、模块级明细、零覆盖率文件优先级、Mock依赖分析、任务分发准备清单、已知陷阱。本 skill 是该文档的**动态跟踪层**，负责认领、进度和里程碑。

## Sprint目标

| 指标 | 基线 (2026-05-27) | iobuf后 | 目标 | 缺口 |
|--------|----------------------|---------|--------|-----|
| 行覆盖率 | 11.1% (623/5637) | 15.1% (850/5637) | ≥ 80% | +3650行 |
| 分支覆盖率 | 5.3% (361/6861) | 7.0% (479/6861) | ≥ 50% | +2953分支 |
| 函数覆盖率 | 19.0% (117/615) | 27.0% (166/615) | — | +449函数 |

## 模块优先级 (来自覆盖率分析文档)

| 模块 | 需+80% | 难度 | 人数(16人总) | 关键Mock |
|--------|-----------|------------|-------------------|----------|
| core/umq | +1460 | hard | 5-6 | UMQ API + epoll + socket ops |
| core/socket | +1126 | hard | 4-5 | epoll + socket + pthread |
| common | +384 | medium | 2 | pthread |
| iobuf | +274→+77 | easy→done | 1→0 | UMQ buf (已完成57%, 45 cases) |
| entry | +235 | hard | 1(延后) | 依赖core模块 |
| under_api + urma | +414 | easy | 1 | dlopen |
| profiling | 0(84.2%已达标) | done | 0 | — |

## Sprint前必备条件 (文档section 六.6.2)

分发任务前，按顺序完成:

1. **P0: Mock基础设施** — `unit_test/stub/` 已建立(含fake_epoll/AllocMockBuf/SocketTestHelper等stub)，但**新增UT默认不使用stub**，优先用mockcpp
2. **P1: CMake模板** — 每模块test binary结构
3. **P2: 文件认领表** — 本skill下方进度表
4. **P3: 工作流标准化** — 构建/覆盖率/验证命令文档
5. **P4: 更新CLAIMING.md** — 构建/覆盖率命令已纳入CLAIMING.md快速上手章节

## UT约束

- **stub默认不启用**: 新增UT时优先使用mockcpp(`MOCKER_CPP`/`MOCKER`)mock C API和系统调用，**默认不使用stub方式**(fake_epoll_static/AllocMockBufWithBlock/SocketTestHelper等)。只有用户明确指定需要stub实现时才启用。
- **单用例执行≤1s**: 每个`TEST_F`用例的执行时间不超过1秒。禁止在测试中使用长时间sleep、阻塞等待、密集计算循环等。如需等待异步事件，使用短超时(≤100ms)+轮询。

## 小步快跑工作流 (按文件)

1. **Claim file** — update progress table below
2. **Load appropriate skill** — `ut-gen` + module sub-skill (e.g. `ut-gen-umq`)
3. **Analyze** — deep analysis methodology (4-step from ut-gen skill)
4. **Design** — test case names, mock strategy
5. **Write** — test file + CMakeLists.txt update
6. **构建** — `UMQ_BUILD=on UBSOCKET_UT=on UBSOCKET_COVERAGE=on bash build/build_umq_and_ubsocket.sh`
7. **运行** — `ctest --test-dir src/ubsocket/build -R <test_name>` 或 `cd src/ubsocket/build && ./<test_binary>`
8. **验证** — 全部通过，无crash
9. **覆盖率检查** — 在build目录重新 `make coverage`, 查看 `coverage_summary.txt`
10. **更新进度** — 在下方表格标记完成, 记录行覆盖率增量
11. **知识回流** — 若发现新模式/陷阱, 更新skill + doc + AGENTS.md

## 知识回流

发现新模式或陷阱时:

1. **记入root SKILL.md** — 跨模块模式 → `ut-gen/SKILL.md`
2. **记入模块子skill** — 模块特定陷阱 → `ut-gen-<module>/SKILL.md`
3. **记入AGENTS.md** — 构建/架构/gotchas → 项目级上下文
4. **记入覆盖率分析文档** — 覆盖率数据变更 → `doc/ubsocket/UBSOCKET-COVERAGE-ANALYSIS.ch.md`

### 回流更新检查清单

- [ ] 新mock模式 → ut-gen SKILL.md "mockcpp模式"
- [ ] 模块特定陷阱 → ut-gen-<module> SKILL.md "常见陷阱"
- [ ] 构建系统问题 → AGENTS.md "已知陷阱"
- [ ] 覆盖率基线变化 → UBSOCKET-COVERAGE-ANALYSIS.ch.md section 一
- [ ] 新文件覆盖率完成 → 本skill进度表 + 分析文档

## 文件认领与进度

### 认领规则

- 每人同一时间只能认领一个 .cpp 文件(但 .h inline代码可被任何人覆盖)
- 状态: `unclaimed` → `in_progress` → `review` → `done`
- 认领时也注明目标test binary名称
- 完成时记录该文件实际达到的行覆盖率

### 零覆盖率 .cpp 文件(最高优先级, 来自文档section 四.4.1)

| # | Module | File | Lines | Funcs | Mock Difficulty | Claimed by | Status | Test binary | Achieved line% |
|---|--------|------|-------|-------|----------------|------------|--------|-------------|---------------|
| 1 | core/umq | umq_socket_connector.cpp | 410 | 21 | hard | | unclaimed | | 0% |
| 2 | core/socket | ubsocket_event_epoll.cpp | 404 | 29 | hard | | unclaimed | | 0% |
| 3 | under_api/urma | dl_urma_api.cpp | 267 | 3 | easy | | unclaimed | | 0% |
| 4 | iobuf | ubsocket_zcopy_adapter.cpp | 242 | 19 | easy | AI | done | iobuf_zcopy_adapter_test | 52.5% |
| 5 | core/socket | ubsocket_socket_helper.cpp | 181 | 11 | medium | | unclaimed | | 0% |
| 6 | entry | ubsocket_sock.cpp | 143 | 24 | hard | | unclaimed | | 0% |
| 7 | under_api | dl_libc_api.cpp | 124 | 3 | medium | | unclaimed | | 0% |
| 8 | core/umq | umq_setting.cpp | 115 | 8 | easy | | unclaimed | | 0% |
| 9 | core/socket | ubsocket_socket_acceptor.cpp | 114 | 8 | medium | | unclaimed | | 0% |
| 10 | core/umq | umq_epoll_runner_ops.cpp | 112 | 2 | hard | | unclaimed | | 0% |
| 11 | entry | ubsocket.cpp | 107 | 9 | hard | | unclaimed | | 0% |
| 12 | core/socket | ubsocket_socket.cpp | 91 | 6 | medium | | unclaimed | | 0% |
| 13 | common | ubsocket_thread_pool.cpp | 78 | 10 | medium | | unclaimed | | 0% |
| 14 | common | ubsocket_global_setting.cpp | 69 | 4 | easy | | unclaimed | | 0% |
| 15 | core/socket | ubsocket_data_rx.cpp | 66 | 4 | easy | | unclaimed | | 0% |
| 16 | core/socket | ubsocket_wakeup_event.cpp | 63 | 6 | medium | | unclaimed | | 0% |
| 17 | core/socket | ubsocket_data_tx.cpp | 43 | 2 | easy | | unclaimed | | 0% |
| 18 | entry | ubsocket_epoll.cpp | 37 | 5 | hard | | unclaimed | | 0% |
| 19 | under_api | dl_api.cpp | 25 | 2 | medium | | unclaimed | | 0% |
| 20 | core/socket | ubsocket_socket_connector.cpp | 24 | 2 | easy | | unclaimed | | 0% |
| 21 | core/socket | ubsocket_core_types.cpp | 21 | 6 | easy | | unclaimed | | 0% |
| 22 | common | ubsocket_signal_handler.cpp | 4 | 1 | easy | | unclaimed | | 0% |

### 部分覆盖需更多测试的文件(来自文档section 四.4.3)

| Module | File | Current line% | Total lines | Need +80% | Claimed by | Status | Test binary |
|--------|------|--------------|-------------|-----------|------------|--------|-------------|
| core/umq | umq_data_tx_ops.cpp | 9.3% | 375 | +276 | | unclaimed | |
| core/umq | umq_socket.cpp | 5.5% | 293 | +220 | | unclaimed | |
| core/umq | umq_socket_acceptor.cpp | 7.6% | 185 | +133 | | unclaimed | |
| core/umq | umq_data_rx_ops.cpp | 18.0% | 217 | +132 | | unclaimed | |
| core/umq | umq_eid_table.h | 18.0% | 122 | +73 | | unclaimed | |
| common | ubsocket_lock.cpp | 29.8% | 131 | +69 | | unclaimed | |

## 覆盖率里程碑

| 里程碑 | 目标 | 达成日期 | 实际值 |
|-----------|--------|--------------|--------|
| 基线测量 | Phase 0 | 2026-05-27 | 11.1% 行 / 5.3% 分支 |
| iobuf UT完成 | Phase 1 | 2026-05-27 | 15.1% 行 / 7.0% 分支 |
| 30% 行覆盖率 | Phase 1中期 | _待定_ | _待定_ |
| 50% 行 / 25% 分支 | Phase 2中期 | _待定_ | _待定_ |
| 80% 行 / 50% 分支 | Sprint结束 | _待定_ | _待定_ |

## 构建与覆盖率命令

```bash
# 全量构建 + UT + 覆盖率
UMQ_BUILD=on UBSOCKET_UT=on UBSOCKET_COVERAGE=on bash build/build_umq_and_ubsocket.sh

# 运行所有测试
ctest --test-dir src/ubsocket/build --output-on-failure

# 运行特定测试
ctest --test-dir src/ubsocket/build -R <test_name>

# 生成覆盖率报告(构建脚本已包含, 或手动)
cd src/ubsocket/build && make coverage

# 查看覆盖率结果
cat src/ubsocket/build/coverage_summary.txt
cat src/ubsocket/build/coverage_detailed.txt
# HTML: src/ubsocket/build/coverage_report/index.html
```

### lcov手动步骤(make coverage失败时)

```bash
lcov --directory src/ubsocket/build --zerocounters --rc lcov_branch_coverage=1
ctest --test-dir src/ubsocket/build --output-on-failure
lcov --rc lcov_branch_coverage=1 --directory src/ubsocket/build \
    --base-directory src/ubsocket/csrc --initial --capture \
    --output-file src/ubsocket/build/coverage_initial.info --ignore-errors gcov
lcov --rc lcov_branch_coverage=1 --directory src/ubsocket/build \
    --base-directory src/ubsocket/csrc --capture \
    --output-file src/ubsocket/build/coverage.info --ignore-errors gcov
lcov --rc lcov_branch_coverage=1 -a src/ubsocket/build/coverage_initial.info \
    -a src/ubsocket/build/coverage.info \
    --output-file src/ubsocket/build/coverage_combined.info
lcov --rc lcov_branch_coverage=1 --remove src/ubsocket/build/coverage_combined.info \
    '*/_deps/*' '/usr/include/*' '*/3rdparty/*' '*/unit_test/*' '*/tools/*' \
    --output-file src/ubsocket/build/coverage_filtered.info --ignore-errors gcov
lcov --rc lcov_branch_coverage=1 --summary src/ubsocket/build/coverage_filtered.info \
    > src/ubsocket/build/coverage_summary.txt 2>&1
lcov --rc lcov_branch_coverage=1 --list src/ubsocket/build/coverage_filtered.info \
    > src/ubsocket/build/coverage_detailed.txt 2>&1
genhtml --branch-coverage --output-directory src/ubsocket/build/coverage_report \
    src/ubsocket/build/coverage_filtered.info
```

## Test Binary命名约定

| 模式 | 示例 |
|---------|---------|
| Converter-only | `<feature>_converter_test` → `umq_errno_converter_test` |
| Ops级(errno映射) | `<feature>_ops_errno_test` → `umq_ops_errno_test` |
| 模块级(全覆盖) | `<module>_<feature>_test` → `umq_socket_test`, `core_event_epoll_test` |
| Common工具 | `common_<feature>_test` → `common_global_setting_test`, `common_lock_test` |

## CMakeLists.txt增长计划

当前3个target: `umq_errno_converter_test`, `umq_ops_errno_test`, `profiling_test`

预计最终: ~15-20个target覆盖所有模块。

增量添加——每人写测试文件时添加自己的target。

## 参考文档与Skills

| 资源 | 用途 |
|----------|---------|
| `doc/ubsocket/UBSOCKET-COVERAGE-ANALYSIS.ch.md` | **权威数据源** — 覆盖率基线、文件完备性、模块明细、mock分析、陷阱 |
| `doc/ubsocket/UBSOCKET-CLAIMING.md` | Sprint唯一入口 — 认领表+快速上手+mock基础设施 |
| `AGENTS.md` | 项目级上下文(构建命令、gotchas、覆盖率基线) |
| `.opencode/skills/ut-gen/SKILL.md` | 主 UT 生成 skill |
| `.opencode/skills/ut-gen-umq/SKILL.md` | UMQ 模块子 skill |
| `.opencode/skills/ut-gen-core/SKILL.md` | Core 模块子 skill |
| `.opencode/skills/ut-gen-common/SKILL.md` | Common 模块子 skill |
| `.opencode/skills/ut-gen-under-api/SKILL.md` | Under-API 模块子 skill |
| `.opencode/skills/ut-gen-profiling/SKILL.md` | Profiling 模块子 skill |
| `src/ubsocket/unit_test/CMakeLists.txt` | 当前test targets(5) |
| `src/ubsocket/CMakeLists.txt` | Coverage target定义(lines 126-183) |

## 如何使用本Skill

### 对协调者(你)

1. **Sprint前**: 确保P0-P4必备条件完成
2. **认领**: 同事认领文件时，更新进度表
3. **进度检查**: 定期 `make coverage`, 更新里程碑
4. **知识回流**: 同事报告陷阱/模式时，更新skills + doc + AGENTS.md

### 对每位同事

1. **认领文件**: 告诉协调者选哪个文件，在进度表中登记
2. **加载skills**: 告诉opencode加载 `ut-gen` + 相关模块子skill
3. **遵循小步快跑**: 一次一个文件，立即构建/验证循环
4. **报告进度**: 告诉协调者状态变化(in_progress → review → done) + 覆盖率增量
5. **报告发现**: 新陷阱 → 协调者更新skills/docs

### Skills之间如何关联

```
ut-gen (root)          ← 通用模式、mockcpp技巧、分析方法论
  ├── ut-gen-umq       ← UMQ模块特定：umq_* mock、Share-JFR、UmqOperation
  ├── ut-gen-core      ← core/socket特定：epoll mock、OsAPiMgr、socket ops
  ├── ut-gen-common    ← common特定：singleton cleanup、pthread mock
  ├── ut-gen-under-api ← under_api特定：dlopen mock、两后端模式
  ├── ut-gen-profiling ← profiling特定：无mock纯逻辑
  └── ut-coverage-coord ← 协调层：认领、进度、里程碑（本 skill）
```

每个子skill与ut-gen**一起加载**，当opencode被要求为特定模块文件写测试时。本coord skill在任务涉及**规划、跟踪或协调**(而非写代码)时加载。