# UBSocket 编译指导（urma 后端）

> 适用分支：`br_urma_rw_0728` 起
> 目标平台：openEuler 24.03 SP3 aarch64（鲲鹏）

## 1. 概述

UBSocket 的 UB 传输后端为 **urma**（`csrc/core/urma/`，唯一后端），运行时 dlopen liburma.so，通过 CMake 选项控制：

| 编译配置 | 传输后端 | 运行时依赖 | 说明 |
|---------|---------|-----------|------|
| **urma**（默认且唯一） | urma | dlopen liburma.so | 直接用底层 urma verbs |

## 2. 后端选项

`src/ubsocket/CMakeLists.txt:11` 定义：

| CMake 选项 | 默认 | 宏 | 说明 |
|-----------|------|-----|------|
| `BUILD_URMA_DLOPEN_BACKEND` | **ON** | `URMA_DLOPEN_BACKEND_ENABLED` + `URMA_BACKEND_ENABLED`（`:66-68`） | urma 后端，编 `core/urma/*.cpp` |

> 该选项必须为 ON：`CMakeLists.txt:17-19` 对 OFF 直接 `FATAL_ERROR`（urma 是唯一后端）。

## 3. urma 模式编译

```bash
# 直接 cmake（src/ubsocket 下）
cmake -S src/ubsocket -B build -DBUILD_URMA_DLOPEN_BACKEND=ON
cmake --build build -j$(nproc)
```

**依赖**：
- 编译期：urma 头文件（`/usr/include/ub/umdk/urma`，系统 umdk-urma）
- 运行期：liburma.so（dlopen 加载）

**产物**：`libubsocket.so` + `libubsocket_static.a`（含 urma 适配层）

## 4. 验证

```bash
nm -D build/csrc/libubsocket.so | grep -i "urma::"
# 应有 urma 适配层符号
```

## 5. 前置准备（系统依赖）

openEuler 24.03 SP3 aarch64 需安装：

| 依赖 | 来源 | 阶段 |
|------|------|------|
| umdk-urma 头文件 | umdk-urma RPM（`/usr/include/ub/umdk/urma`） | 编译期 |
| liburma.so | urma RPM | 运行时 dlopen |
| libboundscheck | 系统或源码 build | 编译期+运行时 |

## 6. 注意事项

- 切换构建配置（如增删 UT/SHM 等选项）需重新 cmake（清 build 目录）
- urma 为唯一后端，`BUILD_URMA_DLOPEN_BACKEND` 不可关闭

## 7. 相关文档

- `doc/ubsocket/UBSOCKET-ARCHITECTURE.ch.md` — UBSocket 内部架构与调用链
- `doc/ubsocket/UBSOCKET-USER-GUIDE.md` — UBSocket 使用手册
