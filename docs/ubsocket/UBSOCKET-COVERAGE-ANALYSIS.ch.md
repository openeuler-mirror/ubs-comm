# UBSocket csrc 覆盖率数据源

> **唯一权威**: 本文是 ubsocket 覆盖率数字的唯一来源——所有行/函数/分支百分比只在本文件出现一次。
> 写 UT 的规则见 `.opencode/skills/ut-gen/SKILL.md`(含 `modules/` 分支附录);分工与认领见 `UBSOCKET-CLAIMING.md`(协调页);政策见仓根 `docs/adr/0001-coverage-policy-per-module-80-50-no-cap.md`。
> **禁止手改数字**: 本文件所有数字由覆盖率重跑生成。数字过期 → 按 §10 刷新,不手改;认领状态、进度、人员分工属于协调页,不写在本文件。

## 1. 快照信息

| 项 | 值 |
|----|----|
| 快照日期 | 2026-08-29 |
| 最近刷新人 | wang-shun1999 |
| 生成方式 | `build/build_umq_and_ubsocket.sh`(USE_URMA_STUB=on UMQ_BUILD=on UBSOCKET_UT=on UBSOCKET_COVERAGE=on,见 §10) |
| 工具链 | gtest 1.12.1 / mockcpp v2.7 / lcov 1.16 |
| 测试现状 | 69 个 target / 2689 个 case,69/69 通过 |
| 报告文件数 | 121(47 `.cpp` + 74 `.h`) |

## 2. 全局指标

| 行 | 函数 | 分支 |
|----|------|------|
| 93.5%(13255/14183) | 94.4%(1585/1679) | 63.6%(10354/16288) |

## 3. 覆盖率政策(口径)

- 每个模块目标: 行 ≥80% / 分支 ≥50%,越高越好(`docs/adr/0001-coverage-policy-per-module-80-50-no-cap.md`)
- 软性强制: 当前不设 CI 门禁;全仓行覆盖到 ~60% 时重新讨论是否收紧
- 认领、分工、验收均以 §4 模块汇总 + §5 逐文件表为准;数字变化只来自 §10 重跑

## 4. 模块汇总

| 模块 | 文件数 | 行% | 函数% | 分支% |
|------|-------|-----|-------|-------|
| common | 29 | 95.2 | 97.8 | 70.6 |
| core | 24 | 91.6 | 87.8 | 54.7 |
| core/umq | 33 | 95.2 | 95.0 | 69.3 |
| iobuf | 2 | 100.0 | 90.9 | 55.2 |
| profiling | 28 | 93.5 | 95.8 | 63.5 |
| top-level | 5 | 84.8 | 97.8 | 57.2 |
| **全仓** | **121** | **93.5** | **94.4** | **63.6** |
注: iobuf 唯一的 `.cpp`(`ubsocket_zcopy_adapter.cpp`)无可执行行未纳入(见 §7),两行数据全部来自 `.h`。

## 5. 逐文件明细(.cpp,47 个)

> 认领与分工的输入表。文件路径相对 `csrc/<模块>/`;行数 = 可执行行(LF);百分比 = 行 / 函数 / 分支。`0.0` 即零覆盖。
> 刷新: 用 §10 命令 B 的输出整表替换本节。

| 模块 | 文件 | 行数 | 行% | 函数% | 分支% |
|------|------|-----|-----|-------|-------|
| common | common/ubsocket_global_setting.cpp | 177 | 99.4 | 100.0 | 98.7 |
| common | common/ubsocket_lock.cpp | 149 | 92.6 | 100.0 | 71.4 |
| common | common/ubsocket_thread_pool.cpp | 86 | 94.2 | 90.0 | 61.2 |
| common | common/ubsocket_port_cooldown.cpp | 17 | 100.0 | 100.0 | 61.1 |
| common | common/ubsocket_link_trace.cpp | 10 | 100.0 | 100.0 | 87.5 |
| common | common/ubsocket_signal_handler.cpp | 4 | 100.0 | 100.0 | 100.0 |
| core | core/ubsocket_bigdata.cpp | 1484 | 8.8 | 25.9 | 4.3 |
| core | core/ubsocket_event_epoll.cpp | 559 | 98.4 | 75.6 | 64.5 |
| core | core/ubsocket_socket_helper.cpp | 441 | 86.8 | 92.9 | 59.2 |
| core | core/ubsocket_tx_cqe_poller.cpp | 410 | 86.3 | 87.5 | 67.4 |
| core | core/ubsocket_socket_acceptor.cpp | 209 | 93.3 | 93.8 | 62.7 |
| core | core/ubsocket_socket.cpp | 140 | 89.3 | 100.0 | 58.1 |
| core | core/ubsocket_data_rx.cpp | 87 | 98.9 | 100.0 | 68.4 |
| core | core/ubsocket_data_tx.cpp | 74 | 86.5 | 100.0 | 56.8 |
| core | core/ubsocket_wakeup_event.cpp | 54 | 96.3 | 100.0 | 76.7 |
| core | core/ubsocket_socket_connector.cpp | 47 | 97.9 | 100.0 | 58.6 |
| core | core/ubsocket_core_types.cpp | 21 | 100.0 | 100.0 | 47.0 |
| core/umq | core/umq/umq_socket.cpp | 652 | 87.0 | 100.0 | 61.2 |
| core/umq | core/umq/umq_data_tx_ops.cpp | 496 | 94.8 | 100.0 | 61.0 |
| core/umq | core/umq/umq_socket_connector.cpp | 402 | 92.0 | 94.4 | 66.3 |
| core/umq | core/umq/umq_share_jfr_epoll_runner_ops.cpp | 388 | 97.7 | 100.0 | 69.7 |
| core/umq | core/umq/umq_backend.cpp | 348 | 97.4 | 100.0 | 74.3 |
| core/umq | core/umq/umq_socket_acceptor.cpp | 325 | 100.0 | 100.0 | 75.4 |
| core/umq | core/umq/umq_tx_helper.cpp | 314 | 97.1 | 100.0 | 78.6 |
| core/umq | core/umq/umq_data_rx_ops.cpp | 290 | 100.0 | 100.0 | 62.7 |
| core/umq | core/umq/umq_transport_pool.cpp | 229 | 97.4 | 100.0 | 69.3 |
| core/umq | core/umq/umq_setting.cpp | 209 | 97.6 | 100.0 | 97.9 |
| core/umq | core/umq/umq_buffer_receive_queue.cpp | 208 | 92.8 | 100.0 | 81.1 |
| core/umq | core/umq/umq_conn_helper.cpp | 159 | 100.0 | 100.0 | 60.0 |
| core/umq | core/umq/umq_tp_tx_epoll_runner_ops.cpp | 148 | 99.3 | 100.0 | 70.4 |
| core/umq | core/umq/umq_errno_converter.cpp | 73 | 87.7 | 100.0 | 92.6 |
| core/umq | core/umq/umq_tp_wait_queue.cpp | 66 | 97.0 | 100.0 | 78.8 |
| core/umq | core/umq/umq_tp_event_epoll_runner_ops.cpp | 19 | 100.0 | 100.0 | 68.8 |
| profiling | profiling/statistics/stat_exporter.cpp | 172 | 99.4 | 93.8 | 68.9 |
| profiling | profiling/impl/ubsocket_prof_tracer_ext.cpp | 150 | 92.7 | 100.0 | 86.8 |
| profiling | profiling/impl/ubsocket_prof_tracer.cpp | 99 | 84.8 | 100.0 | 80.4 |
| profiling | profiling/ubsocket_prof.cpp | 55 | 100.0 | 100.0 | 71.4 |
| profiling | profiling/statistics/statistics.cpp | 48 | 97.9 | 100.0 | 81.0 |
| profiling | profiling/impl/ubsocket_prof_tracepoint_combiner.cpp | 44 | 97.7 | 100.0 | 65.9 |
| profiling | profiling/trace/ubsocket_trace.cpp | 44 | 100.0 | 88.9 | 80.0 |
| profiling | profiling/impl/ubsocket_prof_tracepoint_combiner_ext.cpp | 40 | 97.5 | 100.0 | 60.4 |
| profiling | profiling/impl/ubsocket_prof_tracepoint_dumper.cpp | 9 | 100.0 | 100.0 | 100.0 |
| profiling | profiling/impl/ubsocket_prof_tracepoint_dumper_ext.cpp | 9 | 100.0 | 100.0 | 100.0 |
| top-level | ubsocket_sock.cpp | 180 | 68.3 | 100.0 | 41.3 |
| top-level | ubsocket.cpp | 170 | 90.0 | 90.9 | 60.1 |
| top-level | ubsocket_data.cpp | 101 | 100.0 | 100.0 | 79.1 |
| top-level | ubsocket_epoll.cpp | 68 | 91.2 | 100.0 | 69.7 |

## 6. 零覆盖 .h(1 个,共 51 行)

> 仅列零覆盖的 `.h`;部分覆盖的 `.h` 在 `coverage_report/index.html` 查。文件路径相对 `csrc/<模块>/`。
> 刷新: 用 §10 命令 C 的输出整表替换本节。

| 模块 | 文件 | 行数 |
|------|------|-----|
| core | core/ubsocket_bigdata_order.h | 51 |

## 7. 未纳入报告的文件(11 个)

> 报告覆盖 csrc 下所有含可执行行且参与当前构建的文件;以下文件不在报告中,不产生 UT 任务,覆盖率口径不含它们。

| 文件 | 原因 |
|------|------|
| `cli/` ×4(`cli_client.cpp`、`cli_args_parser.cpp`、`terminal_display.cpp` 等) | `UBSOCKET_BUILD_CLI` 默认 `OFF`,未参与构建 |
| `core/urma/` ×4(`urma_backend.cpp`、`urma_setting.cpp`、`urma_socket.cpp`、`urma_wrapper.cpp`) | `BUILD_URMA_DLOPEN_BACKEND=OFF`,未参与构建 |
| `under_api/dl_umq_api.cpp` | 实现整体在 `#ifdef UMQ_DLOPEN_BACKEND_ENABLED` 内,adapter 后端(默认)下为空翻译单元 |
| `iobuf/ubsocket_zcopy_adapter.cpp` | 19 行,仅 `g_zcopy_allocator` 全局变量定义,无可执行行 |
| `ubsocket_cntl.cpp` | 11 行,仅 include + 声明,无可执行行 |

## 8. 数据口径

- 报告源: `src/ubsocket/build/coverage_filtered.info`;过滤规则(CMakeLists `coverage` target 实际值): `*/_deps/*` `*/usr/include/*` `/opt/*` `*/3rdparty/*` `*/unit_test/*` `*/tools/*` `*/csrc/under_api/*`
- lcov `--ignore-errors` 与 branch coverage key 随主版本变化:`coverage` target 已在 `src/ubsocket/CMakeLists.txt` 探测 lcov 主版本自动适配——lcov ≥2.x 用 `--ignore-errors gcov,inconsistent,negative,unused,corrupt` + `branch_coverage` key;lcov 1.x 只用 `--ignore-errors gcov` + `lcov_branch_coverage` key(旧配置的 `unused,range` 在 1.x 不支持,会报 unknown argument)
- 必须含 `--initial --capture` 步骤,否则零覆盖文件不纳入
- **分支% 工具链口径注**: 分支% 只在**同一次工具链运行内**可比。lcov 2.x 下 `lcov_excl_br_line` 对日志宏行可能不生效,日志内部噪音(`('0','3')` 等)计入 BRF,导致**分支% 系统性偏低**——跨 lcov 1.16 / 2.x 快照对比分支% 会得出虚假回归(行%/函数% 不受影响,可比)。本快照由 lcov 1.16 + GCC 12.3.1 生成(与 08-28 前快照同口径,分支% 可直接对比)。刷新时若工具链与上次不同,在 §1 注明
- 模块划分与 `ut-gen/modules/` 分支一致;`top-level` = `csrc/` 根目录的 entry 文件

## 9. 报告产出物

| 产物 | 路径 | 用途 |
|------|------|------|
| HTML 报告 | `src/ubsocket/build/coverage_report/index.html` | 文件级 + 行级可视化 |
| 总览 TXT | `src/ubsocket/build/coverage_summary.txt` | 全局 3 行数字 |
| 详细 TXT | `src/ubsocket/build/coverage_detailed.txt` | 每文件行/函数/分支率 |
| tracefile | `src/ubsocket/build/coverage_filtered.info` | lcov 原始数据,本文件数字的来源 |

## 10. 刷新(全员可执行)

> 没有专职协调人——任何人都可以刷新本文件: 认领前(看起点)、合入前(看增量)、阶段结束(全员同步)、里程碑达成(确认数字)时各刷一次即可。

1. **重跑**:
   ```bash
   # 方式一(推荐,脚本自含环境变量)
   UMQ_BUILD=on UBSOCKET_UT=on UBSOCKET_COVERAGE=on bash build/build_umq_and_ubsocket.sh
   # 方式二(build 目录已配置 coverage target 时)
   cd src/ubsocket/build && make coverage
   ```
2. **重新生成表格**: 在 `src/ubsocket/build` 下执行以下命令,输出为 markdown 表行,直接粘贴替换对应章节整表(表头保留)
   - §2 全局指标: 读 `coverage_summary.txt` 三行数字
   - §4 模块汇总(命令 A):
     ```bash
     awk '
     /^SF:/ { file = substr($0, 4); if (match(file, /\/csrc\//)) { rel = substr(file, RSTART + 6) } else { rel = file }
       skip = (rel in seen); seen[rel] = 1; lf = lh = fnf = fnh = brf = brh = 0; next }
     /^LF:/ { sub(/^LF:/, "", $0); lf = $0; next }
     /^LH:/ { sub(/^LH:/, "", $0); lh = $0; next }
     /^FNF:/ { sub(/^FNF:/, "", $0); fnf = $0; next }
     /^FNH:/ { sub(/^FNH:/, "", $0); fnh = $0; next }
     /^BRF:/ { sub(/^BRF:/, "", $0); brf = $0; next }
     /^BRH:/ { sub(/^BRH:/, "", $0); brh = $0; next }
     /^end_of_record/ { if (skip) next
       mod = (rel ~ /^core\/umq\//) ? "core/umq" : (rel ~ /^common\//) ? "common" : (rel ~ /^core\//) ? "core" : (rel ~ /^profiling\//) ? "profiling" : (rel ~ /^iobuf\//) ? "iobuf" : (rel ~ /^under_api\//) ? "under_api" : "top-level"
       cnt[mod]++; LF[mod] += lf; LH[mod] += lh; FNF[mod] += fnf; FNH[mod] += fnh; BRF[mod] += brf; BRH[mod] += brh; next }
     END { for (m in cnt) { printf "| %s | %d | %.1f | %.1f | %.1f |\n", m, cnt[m], 100 * LH[m] / LF[m], 100 * FNH[m] / FNF[m], 100 * BRH[m] / BRF[m]
            tcnt += cnt[m]; tLF += LF[m]; tLH += LH[m]; tFNF += FNF[m]; tFNH += FNH[m]; tBRF += BRF[m]; tBRH += BRH[m] }
          printf "| **全仓** | **%d** | **%.1f** | **%.1f** | **%.1f** |\n", tcnt, 100 * tLH / tLF, 100 * tFNH / tFNF, 100 * tBRH / tBRF }' coverage_filtered.info | sort -u
     ```
   - §5 逐文件 .cpp(命令 B):
     ```bash
     awk '
     /^SF:/ { file = substr($0, 4); if (match(file, /\/csrc\//)) { rel = substr(file, RSTART + 6) } else { rel = file }
       skip = (rel in seen); seen[rel] = 1; lf = lh = fnf = fnh = brf = brh = 0; next }
     /^LF:/ { sub(/^LF:/, "", $0); lf = $0; next }
     /^LH:/ { sub(/^LH:/, "", $0); lh = $0; next }
     /^FNF:/ { sub(/^FNF:/, "", $0); fnf = $0; next }
     /^FNH:/ { sub(/^FNH:/, "", $0); fnh = $0; next }
     /^BRF:/ { sub(/^BRF:/, "", $0); brf = $0; next }
     /^BRH:/ { sub(/^BRH:/, "", $0); brh = $0; next }
     /^end_of_record/ { if (skip || rel !~ /\.cpp$/) next
       pct = (lf > 0) ? sprintf("%.1f", 100 * lh / lf) : "0.0"
       fpct = (fnf > 0) ? sprintf("%.1f", 100 * fnh / fnf) : "0.0"
       bpct = (brf > 0) ? sprintf("%.1f", 100 * brh / brf) : "0.0"
       mod = (rel ~ /^core\/umq\//) ? "core/umq" : (rel ~ /^common\//) ? "common" : (rel ~ /^core\//) ? "core" : (rel ~ /^profiling\//) ? "profiling" : (rel ~ /^under_api\//) ? "under_api" : "top-level"
       printf "| %s | %s | %d | %s | %s | %s |\n", mod, rel, lf, pct, fpct, bpct; next }' coverage_filtered.info | sort -t'|' -k2,2 -k4,4rn
     ```
   - §6 零覆盖 .h(命令 C):
     ```bash
     awk '
     /^SF:/ { file = substr($0, 4); if (match(file, /\/csrc\//)) { rel = substr(file, RSTART + 6) } else { rel = file }
       skip = (rel in seen); seen[rel] = 1; lf = lh = 0; next }
     /^LF:/ { sub(/^LF:/, "", $0); lf = $0; next }
     /^LH:/ { sub(/^LH:/, "", $0); lh = $0; next }
     /^end_of_record/ { if (skip || rel !~ /\.h$/ || lf == 0 || lh != 0) next
       mod = (rel ~ /^core\/umq\//) ? "core/umq" : (rel ~ /^common\//) ? "common" : (rel ~ /^core\//) ? "core" : (rel ~ /^profiling\//) ? "profiling" : (rel ~ /^iobuf\//) ? "iobuf" : (rel ~ /^under_api\//) ? "under_api" : "top-level"
       printf "| %s | %s | %d |\n", mod, rel, lf; next }' coverage_filtered.info | sort -t'|' -k2,2 -k4,4rn
     ```
3. **替换并提交**: 更新 §1 快照日期与最近刷新人、§2/§4/§5/§6 → `git pull` 后提交。同一天多人刷新以最后提交为准
4. **禁止**: 手改数字;把数字复制到其它文档(认领表只存状态,见协调页)
