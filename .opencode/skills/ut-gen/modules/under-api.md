# Under-API 模块测试附录 (csrc/under_api/)

与 `ut-gen/SKILL.md` 配合使用。系统调用与 UMQ API 的包装层: dlopen/dlsym 动态加载与 adapter 直连双后端。

## 模块范围(2026-08-24 实测)

- 顶层: `dl_api.{h,cpp}`、`dl_libc_api.{h,cpp}`、`dl_umq_api.{h,cpp}`、`umq_api.h`(**无 `umq_api.cpp`**——adapter 后端是纯内联包装,旧文档说成 `.h/.cpp` 是错的)
- `urma/`: `dl_urma_api.{h,cpp}`、`urma_opcode.h`、`urma_types.h`(随 `BUILD_URMA_DLOPEN_BACKEND=OFF` 未启用,不产生测试任务)

## 双后端模型(写本模块测试的第一前提)

`dl_umq_api.h`: `#ifdef UMQ_DLOPEN_BACKEND_ENABLED`(L14)与 `#else /* UMQ_ADAPTER_BACKEND_ENABLED */`(L486)分界。

| 后端 | 启用状态 | `_ptr` 成员 | mock 方式 |
|------|---------|------------|-----------|
| adapter(默认) | `BUILD_UMQ_ADAPTER_BACKEND=ON` | **无** | `MOCKER_CPP(::umq_xxx)` 直接 mock 全局 C 函数 |
| dlopen | `BUILD_URMA_DLOPEN_BACKEND=OFF`(未启用) | 有 | `_ptr` 赋值(本仓库不产测试) |

## LibcApi 关键事实

- 49 个 `DL_API_DECLARE` 生成逐函数 `xxx_ptr` 成员(`dl_libc_api.h:258-306`),宏定义在 `dl_api.h:22-24`,**初始 `nullptr`**
- `LibcApi::open` 是 variadic(`dl_libc_api.h:82-90`,走 `open_ptr(file, oflag, arg)`)——mockcpp 无法 mock,must `open_ptr` 替换
- 当前所有测试都不调用 `LibcApi::Load()`,全部走 `xxx_ptr` 直接替换

## 现状与空白点

- **`unit_test/` 下当前没有任何 under_api 测试文件**——本模块是全仓最大空白点,优先补
- 旧版文档中的代码示例无落地验证,引用前先 grep 验证

## 特有模式

- `DlApi` 通过 dlopen/dlsym 加载: 测试用 `MockDlsym` 做参数依赖返回(不同 symbol 返回不同 ptr)+ `invoke` 用 static 函数非 lambda
- `DlApi::Load(LOAD_URMA)` 已预留加载标志位(urma 后端启用时用)

## 特有陷阱

1. **不要给 `UmqApi::xxx_ptr` 赋值** — adapter 后端(默认启用)没有这些成员,赋值即编译错;只有 dlopen 后端有
2. **`LibcApi::xxx_ptr` 未设置 → segfault** — 每个用到的 `xxx_ptr` 在 `SetUp` 设置、`TearDown` 恢复 `nullptr`(实测清理函数 `SetLibcApiPtrsToNull()` 一次性归零 9 个指针)
3. **variadic 函数走 `xxx_ptr`** — `LibcApi::open`;同理适用于所有 `DL_API_DECLARE` 生成的 variadic 成员
4. **include 顺序** — `umq_data_tx_ops.h` 必须在 `umq_buf_converter.h` 之前(后者用 `umq_buf_t`);clang-format `IncludeBlocks: Preserve` 不会自动重排,手动遵守依赖链

## 边界清单

(占位——本模块首次认领时填写) 列出文件内**边界敏感类判定点**的具体实例(规则见 `ut-gen/SKILL.md` §2 质量加固目标;under_api 层候选: 双后端分界 `#ifdef UMQ_DLOPEN_BACKEND_ENABLED` 的编译边界、`dlsym` 查表命中/未命中边界、`xxx_ptr` nullptr/已赋值分界)。评审按此清单核对边界值 + 相邻值用例;认领时发现的新边界判定点先登记再写用例。
