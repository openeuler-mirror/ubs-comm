# Common 模块测试附录 (csrc/common/)

与 `ut-gen/SKILL.md` 配合使用。全局设置、锁、日志、容器、单例、线程池等基础设施,大量纯头文件实现。

## 模块范围(2026-08-24 实测)

`.h`(26): `ubsocket_common_includes.h`、`ubsocket_defines.h`、`ubsocket_errno.h`、`ubsocket_fast_heap.h`、`ubsocket_flash_dynamic_bitset.h`、`ubsocket_functions.h`、`ubsocket_global_setting.h`、`ubsocket_leaky_singleton.h`、`ubsocket_link_trace.h`、`ubsocket_lock.h`、`ubsocket_logger.h`、`ubsocket_mpsc_ring_queue.h`、`ubsocket_obj_statistics.h`、`ubsocket_port_cooldown.h`、`ubsocket_profiling.h`、`ubsocket_qbuf_queue.h`、`ubsocket_ref.h`、`ubsocket_ring_buffer.h`、`ubsocket_scope_exit.h`、`ubsocket_set.h`、`ubsocket_setting_validator.h`、`ubsocket_signal_handler.h`、`ubsocket_spsc_ring_queue.h`、`ubsocket_thread_pool.h`、`ubsocket_version_defs.h.in`、`ubsocket_version.h`

`.cpp`(6): `ubsocket_global_setting.cpp`、`ubsocket_link_trace.cpp`、`ubsocket_lock.cpp`、`ubsocket_port_cooldown.cpp`、`ubsocket_signal_handler.cpp`、`ubsocket_thread_pool.cpp`

**纯头文件陷阱**: `ubsocket_logger`、`ubsocket_obj_statistics`、`ubsocket_setting_validator`、`ubsocket_fast_heap`、`ubsocket_mpsc_ring_queue` 等只有 `.h` 没有 `.cpp`——旧文档把它们列成 `.h/.cpp` 是错的;写测试时对纯头文件实现只链 `ubsocket_static` 即可。

## 现有测试

`ubsocket_ring_buffer_test`、`ubsocket_ring_queue_test`、`ubsocket_queue_heap_test`、`ubsocket_scope_exit_ref_test`、`ubsocket_bitset_lock_seq_test`、`ubsocket_common_utils_test`、`ubsocket_leaky_singleton_test`、`ubsocket_set_test`、`ubsocket_obj_statistics_test`、`ubsocket_thread_pool_test`、`ubsocket_global_setting_test`、`ubsocket_version_test`、`ubsocket_port_cooldown_test`、`ubsocket_setting_validator_test`(全部在顶层 `unit_test/`)。

## 单例清理三件套(每个涉及单例的测试必做)

1. **GlobalSetting 公开字段**: `SetUp` 重置默认值(如 `UBS_ENABLE_SHARE_JFR=false`、`UBS_RX_DEPTH`),`TearDown` 恢复
2. **LeakySingleton**: 显式 unregister——`EidRegistry::UnregisterEid()` 等;`ProbeManager::GetInstance().Stop()`(probe 类)
3. **类单例**: `Init()`/`ReleaseAll()`——`ArraySet<Socket>::GetInstance().Init()/ReleaseAll()`(旧名 `SocketSet::Instance()` 已不存在,写测试时用现名)

前置: 构造依赖 lock/socket 基础设施的对象前,先 `LockRegistry::RegisterDefaultOps()`。

## 特有模式

| 被测对象 | 模式 |
|---------|------|
| ThreadPool | 短任务 + 原子计数验证,不 sleep 等待 |
| ScopeExit | 纯 RAII,直接构造析构验证回调 |
| SettingValidator | 设 `GlobalSetting` 字段后调 validator,纯逻辑无 mock |
| 静态方法依赖(如 `LibcApi::recv`) | `MOCKER(&LibcApi::recv).stubs().will(invoke(&FakeRecv))` |

## 特有陷阱

1. **`mockcpp::Result` vs `ock::ubs::Result` 冲突** — `using namespace ock::ubs;` + 显式限定 `ock::ubs::Result`
2. **`IO_SIZE_MB` 是宏** — `ubsocket_defines.h` 中定义,测试不可重定义为 `constexpr`
3. **`ArraySet` 相关语义** — `RPC_ADPT_FD_MAX=8192` 是编译时常量(仅 ProbeManager 环形队列用),不可替换为运行时 `ArraySet::Capacity()`(动态值,`min(rlim_cur, 65536)`)
4. **热路径不加日志** — `GetItem/OverrideItem/RemoveItem/ForEach` 是高频路径,测试不应验证日志输出
5. **C++11 `static constexpr` ODR-use**(严重,链接 undefined reference) — 模板类 `static constexpr` 成员传给引用参数函数(如 `std::min/max/clamp`)必须 `static_cast` 成临时 rvalue,否则 C++11 下需要类外定义而链接失败。HCOM 以 C++11 编译,common 层写测试时注意

## 边界清单

(占位——本模块首次认领时填写) 列出文件内**边界敏感类判定点**的具体实例(规则见 `ut-gen/SKILL.md` §2 质量加固目标;common 层候选: 队列满/空与动态扩缩容阈值如 `qbuf_queue`/`ring_queue`、`ArraySet::Capacity()` 上限、`RPC_ADPT_FD_MAX` 边界、`port_cooldown` 时间边界、O3 `FastHeap` 熔断阈值)。评审按此清单核对边界值 + 相邻值用例;认领时发现的新边界判定点先登记再写用例。
