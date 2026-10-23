# UBSocket UT覆盖率Sprint — 启动指南与认领表

> 本文档是Sprint的**唯一入口**：从快速上手到文件认领，读完这份即可开工。
> 深入参考: 架构→`UBSOCKET-ARCHITECTURE.ch.md`, 覆盖率数据→`UBSOCKET-COVERAGE-ANALYSIS.ch.md`
> 协调者负责更新认领表和里程碑；同事认领/完成时通知协调者。

***

## 目标

将 `src/ubsocket/csrc/` 单元测试覆盖率从 **11.1%行/5.3%分支** 提升至 **≥80%行/≥50%分支**。

***

## 快速上手(5步)

### 1. 拉代码 & 构建

```bash
git pull
cmake -S src/ubsocket -B build -DBUILD_URMA_DLOPEN_BACKEND=ON -DUBSOCKET_BUILD_TESTS=ON -DUBSOCKET_ENABLE_COVERAGE=ON
cmake --build build -j
ctest --test-dir build --output-on-failure   # 验证已有case全部PASS
```

### 2. 读架构文档(必读)

读 `doc/ubsocket/UBSOCKET-ARCHITECTURE.ch.md`，理解五层架构、Socket对象层次、状态机、关键调用链。**不读这个写不出正确的UT。**

需要覆盖率数据细节时再翻 `UBSOCKET-COVERAGE-ANALYSIS.ch.md`。

### 3. 认领文件

在下方的认领表里填你的名字和test binary名。规则:
- 每人同一时间只认领一个 `.cpp` 文件
- 优先从零覆盖率表(#1→#20)选，easy优先快速出活
- 状态流转: `unclaimed` → `in_progress` → `review` → `done`
- 完成时记录实际达到的行覆盖率

### 4. 写UT

参考 `src/ubsocket/unit_test/` 下已有test binary学习代码风格:

| Binary | 类型 | 学习重点 |
|--------|------|----------|
| `urma_errno_converter_test` | converter级纯逻辑 | 最简单，仅依赖GTest，无mock |
| `urma_socket_test` | ops级mock测试 | mockcpp典型模式 + `UrmaApi::_ptr`函数指针替换 |
| `ubsocket_conn_ops_base_test` | core/socket mock测试 | 连接/收发路径的mockcpp模式 |
| `block_cache_test` | iobuf mock测试 | mockcpp + `GlobalMockObject::verify()` |
| `allocator_test` | iobuf纯逻辑测试 | 无mock依赖 |

### 5. 构建验证

依赖匹配的 `umdk` 包，从 [atomgit.com/src-openeuler/umdk](https://atomgit.com/src-openeuler/umdk/tree/openEuler-24.03-LTS-SP3) 下载 `umdk-openEuler-24.03-LTS-SP3.zip` 并安装（安装方法联系 fanzhaonan）。

```bash
cmake -S src/ubsocket -B build -DBUILD_URMA_DLOPEN_BACKEND=ON -DUBSOCKET_BUILD_TESTS=ON -DUBSOCKET_ENABLE_COVERAGE=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
# 覆盖率报告: build/coverage_report/index.html
```

***

## mock基础设施

mockcpp + GTest 是当前UT的mock方式；原 `unit_test/stub/` 共享桩目录已移除，无预置helper头文件，mock环境在各test case内自建。

**常用mock手段:**

| 手段 | 用法 | 参考实现 |
|------|------|----------|
| mockcpp | `MOCKER(...)` 打桩 + 每个用例末尾 `GlobalMockObject::verify()` | `urma_alive_manager_test`、`ubsocket_conn_ops_base_test` |
| dlopen API函数指针 | `UrmaApi`/`LibcApi` 的 `*_ptr` 成员直接替换为测试函数，TearDown重置为nullptr | `urma_socket_test.cpp` SetUp/TearDown |
| LockRegistry注入 | 生产代码内函数指针表，pthread无需mock | `common/ubsocket_lock.h` |

***

## 关键规则(违反必返工)

1. **mockcpp `.stubs()` 默认返回void** — 非void函数必须加 `.will(returnValue(...))`
2. **`LibcApi::open` 是variadic** — mockcpp无法mock variadic函数，改用 `LibcApi::_ptr` 函数指针替换(见 `under_api/dl_libc_api.h`)
3. **`LibcApi`/`UrmaApi` 的 `_ptr` 函数指针初始为nullptr** — 未替换或未调用 `Load()` 时通过nullptr函数指针调用→segfault，SetUp中必须替换为mock实现
4. **构造依赖lock/socket的对象前必须先初始化** — `LockRegistry::RegisterDefaultOps()` + `ArraySet<Socket>::GetInstance().Init()`，否则crash(参考 `urma_socket_test.cpp` SetUp)
5. **用例间errno会残留** — SetUp/TearDown中 `errno = 0` 重置，避免前序用例残留errno导致断言错误
6. **TearDown必须清理单例与mock状态** — `ArraySet<Socket>::GetInstance().ReleaseAll()`、重置被替换的 `_ptr` 为nullptr、`GlobalMockObject::verify()`
7. **mockcpp `Result` vs `ock::ubs::Result` 类型冲突** — 用 `using namespace ock::ubs;` + 显式限定 `ock::ubs::Result`
8. **每个使用mock的test case末尾必须调用 `GlobalMockObject::verify()`** — 重置mock状态，否则下一个case受影响

更完整的陷阱列表(22条)见 `.opencode/skills/ut-gen/SKILL.md` §常见陷阱。

***

## 文件认领 — 按难度推荐起步

| 难度 | 推荐起步# | 说明 |
|------|-----------|------|
| easy | #11, #19 | 几乎纯getter/setter，mock简单 |
| easy | #2, #3 | dlopen/dlsym逻辑，参考allocator_test/block_cache_test |
| medium | #6, #4 | LibcApi `_ptr`路径 |
| hard | #1, #5 | 深度依赖链，有经验后再认领 |

***

## 文件认领表

| # | 模块 | 文件 | 行数 | 分支数 | 当前行% | 当前分支% | mock难度 | 认领人 | 状态 | 达成(行/分支) |
|---|------|------|------|--------|---------|----------|---------|--------|------|--------------|
| 1 | core/socket | ubsocket_event_epoll.cpp | 404 | 500 | 0% | 0% | hard | | unclaimed | |
| 2 | under_api/urma | dl_urma_api.cpp | 267 | 258 | 0% | 0% | easy | | unclaimed | |
| 3 | iobuf | ubsocket_zcopy_adapter.cpp | 242 | 278 | 0% | 0% | easy | | unclaimed | |
| 4 | core/socket | ubsocket_socket_helper.cpp | 181 | 282 | 0% | 0% | medium | | unclaimed | |
| 5 | entry | ubsocket_sock.cpp | 143 | 132 | 0% | 0% | hard | | unclaimed | |
| 6 | under_api | dl_libc_api.cpp | 124 | 130 | 0% | 0% | medium | | unclaimed | |
| 7 | core/socket | ubsocket_socket_acceptor.cpp | 114 | 162 | 0% | 0% | medium | | unclaimed | |
| 8 | entry | ubsocket.cpp | 107 | 126 | 0% | 0% | hard | | unclaimed | |
| 9 | core/socket | ubsocket_socket.cpp | 91 | 110 | 0% | 0% | medium | | unclaimed | |
| 10 | common | ubsocket_thread_pool.cpp | 78 | 106 | 0% | 0% | medium | | unclaimed | |
| 11 | common | ubsocket_global_setting.cpp | 69 | 78 | 0% | 0% | easy | | unclaimed | |
| 12 | core/socket | ubsocket_data_rx.cpp | 66 | 52 | 0% | 0% | easy | | unclaimed | |
| 13 | core/socket | ubsocket_wakeup_event.cpp | 63 | 62 | 0% | 0% | medium | | unclaimed | |
| 14 | core/socket | ubsocket_data_tx.cpp | 43 | 56 | 0% | 0% | easy | | unclaimed | |
| 15 | entry | ubsocket_epoll.cpp | 37 | 32 | 0% | 0% | hard | | unclaimed | |
| 16 | under_api | dl_api.cpp | 25 | 28 | 0% | 0% | medium | | unclaimed | |
| 17 | core/socket | ubsocket_socket_connector.cpp | 24 | 32 | 0% | 0% | easy | | unclaimed | |
| 18 | core/socket | ubsocket_core_types.cpp | 21 | 66 | 0% | 0% | easy | | unclaimed | |
| 19 | common | ubsocket_signal_handler.cpp | 4 | 16 | 100% | ~50% | easy | | unclaimed | |
| 20 | common | ubsocket_lock.cpp | 131 | 174 | 29.8% | 6.9% | medium | | unclaimed | |

***

## 覆盖率里程碑

| 里程碑 | 目标 | 达成日期 | 实际值 |
|--------|------|----------|--------|
| 基线 | Phase 0 | 2026-05-27 | 11.1%行 / 5.3%分支 |
| 30%行覆盖率 | Phase 1中期 | _待定_ | _待定_ |
| 50%行 / 25%分支 | Phase 2中期 | _待定_ | _待定_ |
| 80%行 / 50%分支 | Sprint结束 | _待定_ | _待定_ |