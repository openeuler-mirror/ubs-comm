Status: ready-for-agent

## What to build

清理 ubscomm 中 `UBS_AUTO_FALLBACK_TCP` 死代码，并新增两个公开 C API 支持 brpc 的 UB 降级功能。

**清理死代码**：`UBS_AUTO_FALLBACK_TCP` 在 `global_setting.h:99` 声明、`global_setting.cpp:33` 初始化为 `true`，但全代码库无任何引用。与 `UBS_ENABLE_DEGRADE` 语义重叠，删除以避免混淆。

**新增 API `ubsocket_is_ub_transport(int fd)`**：暴露公开 C 函数，直接检查 fd 是否为活跃 UB 传输（在 ArraySet 中）。替代 brpc 间接通过 `getsockopt(fd, SOL_UB, UBS_OPT_PROTOCOL)` 返回 -1 检测降级的方式。返回值：
- 1: fd 是 UB 传输（在 ArraySet 中）
- 0: fd 是 TCP（不在 ArraySet 中，已降级或纯 TCP）
- -1: 错误（UBS_NATIVE_TCP_MODE 或未初始化）

**新增 API `ubsocket_set_degrade_enable(int enable)`**：暴露公开 C 函数，运行时直接设置 `GlobalSetting::UBS_ENABLE_DEGRADE`，替代 brpc 通过 `setenv("UBSOCKET_DEGRADE_ENABLE")` 间接控制的机制。消除 setenv 必须在 `ubsocket_init()` 之前调用的时序约束。

## Acceptance criteria

- [ ] `UBS_AUTO_FALLBACK_TCP` 从 `global_setting.h` 和 `global_setting.cpp` 中删除
- [ ] `ubsocket_is_ub_transport(int fd)` 声明在 `ubsocket.h` 中，extern "C" 保护
- [ ] `ubsocket_is_ub_transport` 实现在 `ubsocket_sock.cpp` 或 `ubsocket.cpp` 中
- [ ] `ubsocket_is_ub_transport` 在 `UBS_NATIVE_TCP_MODE` 时返回 -1
- [ ] `ubsocket_is_ub_transport` 在 fd 不在 ArraySet 时返回 0
- [ ] `ubsocket_is_ub_transport` 在 fd 在 ArraySet 时返回 1
- [ ] `ubsocket_set_degrade_enable(int enable)` 声明在 `ubsocket.h` 中
- [ ] `ubsocket_set_degrade_enable` 实现设置 `GlobalSetting::UBS_ENABLE_DEGRADE`
- [ ] `ubsocket_set_degrade_enable(0)` 后 `UBS_ENABLE_DEGRADE == false`
- [ ] `ubsocket_set_degrade_enable(1)` 后 `UBS_ENABLE_DEGRADE == true`
- [ ] 所有现有测试通过

## Blocked by

None - can start immediately
