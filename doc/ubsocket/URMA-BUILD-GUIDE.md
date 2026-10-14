# UBSocket 编译指导（urma/umq 后端，三种模式）

> 适用分支：`br_urma_rw_0728` 起
> 目标平台：openEuler 24.03 SP3 aarch64（鲲鹏）

## 1. 概述

UBSocket 支持 **两种 UB 传输后端**：urma（默认）和 umq。其中 umq 后端有两种 libumq.so 链接方式（dlopen 运行时加载 / adapter 编译期 link），共形成三种编译配置，通过 CMake 选项切换，**互斥**：

| 编译配置 | 传输后端 | libumq.so 链接 | 运行时依赖 | 说明 |
|---------|---------|---------------|-----------|------|
| **urma**（默认） | urma | 不依赖 | dlopen liburma.so | 直接用底层 urma verbs |
| **umq dlopen** | umq | 运行时 dlopen | dlopen libumq.so | 基于 urma 的高层封装，运行时加载 |
| **umq adapter** | umq | 编译期 link | libumq.so 在链接路径 | 基于 urma 的高层封装，编译期链接 |

> 注：urma 和 umq 是两种传输后端；umq dlopen / umq adapter 是 umq 后端的两种 libumq.so 链接方式（非独立后端）。三者不能同时启用（CMake 三对互斥约束 `FATAL_ERROR`）。

## 2. 后端选项

`src/ubsocket/CMakeLists.txt:11-13` 定义：

| CMake 选项 | 默认 | 宏 | 说明 |
|-----------|------|-----|------|
| `BUILD_URMA_DLOPEN_BACKEND` | **ON** | `URMA_DLOPEN_BACKEND_ENABLED` + `URMA_BACKEND_ENABLED` | urma 后端，编 `core/urma/*.cpp` |
| `BUILD_UMQ_DLOPEN_BACKEND` | **OFF** | `UMQ_DLOPEN_BACKEND_ENABLED` + `UMQ_BACKEND_ENABLED` | umq dlopen，编 `core/umq/*.cpp`，运行时 dlopen libumq.so |
| `BUILD_UMQ_ADAPTER_BACKEND` | OFF | `UMQ_ADAPTER_BACKEND_ENABLED` + `UMQ_BACKEND_ENABLED` | umq adapter，编 `core/umq/*.cpp`，编译期 link libumq.so |

**统一宏 `UMQ_BACKEND_ENABLED`**（`CMakeLists.txt:82-84`）：`BUILD_UMQ_DLOPEN_BACKEND` 或 `BUILD_UMQ_ADAPTER_BACKEND` 任一 ON 时定义。解耦代码用 `#ifdef UMQ_BACKEND_ENABLED` 包裹 umq 通用逻辑（不区分 dlopen/adapter）；`ubsocket.cpp` 别名宏块用 `#elif defined(UMQ_DLOPEN_BACKEND_ENABLED)` / `#elif defined(UMQ_ADAPTER_BACKEND_ENABLED)` 区分 `LOAD_FLAGS`（dlopen 需 `LOAD_UMQ`，adapter 不需）。

**互斥约束**（`:19-25`，三对完全互斥，只能选一个后端）：
- `BUILD_UMQ_ADAPTER_BACKEND` 与 `BUILD_UMQ_DLOPEN_BACKEND` 不能同时 ON（umq adapter vs dlopen）
- `BUILD_UMQ_DLOPEN_BACKEND` 与 `BUILD_URMA_DLOPEN_BACKEND` 不能同时 ON（umq dlopen vs urma）
- `BUILD_UMQ_ADAPTER_BACKEND` 与 `BUILD_URMA_DLOPEN_BACKEND` 不能同时 ON（umq adapter vs urma）

## 3. urma 模式编译（默认）

```bash
# 方式一：构建脚本（推荐）
UBSOCKET_BUILD=on ./build/build_umq_and_ubsocket.sh

# 方式二：直接 cmake（src/ubsocket 下）
cmake -S src/ubsocket -B build -DBUILD_URMA_DLOPEN_BACKEND=ON -DBUILD_UMQ_DLOPEN_BACKEND=OFF
cmake --build build -j$(nproc)
```

**依赖**：
- 编译期：urma 头文件（`/usr/include/ub/umdk/urma`，系统 umdk-urma）+ umq 类型头（`src/hcom/umq/include`，源码仓自带，**不需 build libumq.so**）
- 运行期：liburma.so（dlopen 加载，**不需 libumq.so**）

**产物**：`libubsocket.so` + `libubsocket_static.a`（含 urma 适配层，不含 umq 适配层）

## 4. umq dlopen 模式编译

```bash
# 方式一：构建脚本（需先 build libumq.so）
UMQ_BUILD=on UMQ_BACKEND=on UBSOCKET_BUILD=on ./build/build_umq_and_ubsocket.sh

# 方式二：直接 cmake
cmake -S src/ubsocket -B build -DBUILD_URMA_DLOPEN_BACKEND=OFF -DBUILD_UMQ_DLOPEN_BACKEND=ON
cmake --build build -j$(nproc)
```

**依赖**：
- 编译期：umq 头文件（`src/hcom/umq/include`，源码仓自带）
- 运行期：libumq.so（dlopen 加载；libumq.so 内部 link liburma.so/libtpsa.so）

**产物**：`libubsocket.so` + `libubsocket_static.a`（含 umq 适配层，不含 urma 适配层，不 link libumq.so）

**特点**：`UmqApi` 用函数指针表（`DL_API_DEFINE`），`Load()` 时 dlopen libumq.so + dlsym 填表。

## 5. umq adapter 模式编译

```bash
# 直接 cmake（adapter 模式构建脚本未支持环境变量，用直接 cmake）
cmake -S src/ubsocket -B build -DBUILD_URMA_DLOPEN_BACKEND=OFF \
    -DBUILD_UMQ_DLOPEN_BACKEND=OFF -DBUILD_UMQ_ADAPTER_BACKEND=ON
cmake --build build -j$(nproc)
```

**依赖**：
- 编译期：umq 头文件 + **libumq.so + libumq_buf.so**（CMake 自动 build 如不存在，`CMakeLists.txt:86-106`；`csrc/CMakeLists.txt:67-70/83-86` 编译期 `target_link_libraries umq umq_buf`）
- 运行期：libumq.so 在动态库搜索路径（编译期 link，非 dlopen）

**产物**：`libubsocket.so` + `libubsocket_static.a`（含 umq 适配层，**编译期 link libumq.so/umq_buf**）

**与 dlopen 区别**：
| 维度 | umq dlopen | umq adapter |
|------|-----------|-------------|
| libumq.so 链接 | 运行时 dlopen | 编译期 `target_link_libraries` |
| UmqApi 调用 | 函数指针表（dlsym） | 直接 `::umq_*`（extern 符号） |
| Load() | dlopen libumq.so | 返回 UBS_OK（无需加载） |
| CMake 自动 build libumq.so | 否 | 是（`:86-106`） |

## 6. 构建脚本环境变量

`build/build_umq_and_ubsocket.sh` 支持环境变量（urma/umq dlopen 模式；**adapter 模式用直接 cmake**）：

| 变量 | 默认 | 说明 |
|-----|------|------|
| `URMA_BACKEND` | ON | urma 后端开关（传 `-DBUILD_URMA_DLOPEN_BACKEND`） |
| `UMQ_BACKEND` | OFF | umq dlopen 后端开关（传 `-DBUILD_UMQ_DLOPEN_BACKEND`；ON 时需 `UMQ_BUILD=on`） |
| `UBSOCKET_BUILD` | off | 是否构建 ubsocket |
| `UMQ_BUILD` | off | 是否构建 libumq.so（umq dlopen 模式需 on） |
| `UBSOCKET_UT` | off | 是否运行 UT |
| `UBSOCKET_BUILD_SHM` | off | 是否构建 SHM |

组合示例：
```bash
# urma 模式（默认，不 build libumq.so）
UBSOCKET_BUILD=on ./build/build_umq_and_ubsocket.sh

# umq dlopen 模式（需先 build libumq.so）
UMQ_BUILD=on UMQ_BACKEND=on UBSOCKET_BUILD=on ./build/build_umq_and_ubsocket.sh

# umq dlopen 模式 + UT
UMQ_BUILD=on UMQ_BACKEND=on UBSOCKET_BUILD=on UBSOCKET_UT=on ./build/build_umq_and_ubsocket.sh

# umq adapter 模式（用直接 cmake，见 §5）
```

## 7. 验证

**urma 模式验证**（不引用 libumq.so 的 `umq_*` API）：
```bash
nm -D build/csrc/libubsocket.so | grep -E "umq_create|umq_post|umq_buf_alloc|umq_init"
# 应无输出（运行时不依赖 libumq.so）
```

**umq（dlopen/adapter）模式验证**（不含 urma 适配层符号）：
```bash
nm -D build/csrc/libubsocket.so | grep -i "urma::"
# 应无输出
```

**互斥校验**（三对）：
```bash
# umq dlopen + urma（应 FATAL_ERROR）
cmake -S src/ubsocket -B build -DBUILD_URMA_DLOPEN_BACKEND=ON -DBUILD_UMQ_DLOPEN_BACKEND=ON
# umq adapter + urma（应 FATAL_ERROR）
cmake -S src/ubsocket -B build -DBUILD_URMA_DLOPEN_BACKEND=ON -DBUILD_UMQ_ADAPTER_BACKEND=ON
# umq adapter + umq dlopen（应 FATAL_ERROR）
cmake -S src/ubsocket -B build -DBUILD_UMQ_ADAPTER_BACKEND=ON -DBUILD_UMQ_DLOPEN_BACKEND=ON
```

## 8. 前置准备（系统依赖）

openEuler 24.03 SP3 aarch64 需安装：

| 依赖 | 来源 | urma | umq dlopen | umq adapter |
|------|------|------|-----------|-------------|
| umdk-urma 头文件 | umdk-urma RPM（`/usr/include/ub/umdk/urma`） | 编译期 | 编译期（libumq.so build 时） | 编译期（libumq.so build 时） |
| liburma.so | urma RPM | 运行时 dlopen | 运行时（libumq.so 依赖） | 运行时（libumq.so 依赖） |
| libtpsa.so | tpsa RPM | 不需要 | 运行时（libumq.so 依赖） | 运行时（libumq.so 依赖） |
| libumq.so | 源码 build（`src/hcom/umq`） | 不需要 | 运行时 dlopen | 编译期 link（CMake 自动 build） |
| libboundscheck | 系统或源码 build | 编译期+运行时 | 编译期+运行时 | 编译期+运行时 |

## 9. 注意事项

- **urma 是默认后端**（`br_urma_rw_0728` 分支起），新场景优先用 urma
- **umq dlopen 需先 build libumq.so**（`UMQ_BUILD=on`）；**umq adapter CMake 自动 build libumq.so**（`:86-106`）
- **切换模式需重新 cmake**（清 build 目录），三模式 libubsocket.so 产物名相同但内容不同
- urma 模式编译期仍 include umq 类型头（`src/hcom/umq/include`，源码仓自带，不需 build libumq.so），运行期不依赖 libumq.so。后续阶段二抽象通用类型后可完全脱离 umq 头
- adapter 模式构建脚本未支持环境变量，用直接 cmake（§5）

## 10. 产物对比

| 模式 | libubsocket.so 内容 | libumq.so 链接 | 运行时 dlopen |
|------|---------------------|---------------|---------------|
| urma | common + core 抽象 + urma 适配层（不含 umq） | 不依赖 | liburma.so |
| umq dlopen | common + core 抽象 + umq 适配层（不含 urma） | 不 link（运行时 dlopen） | libumq.so（→ liburma.so/libtpsa.so） |
| umq adapter | common + core 抽象 + umq 适配层（不含 urma） | 编译期 link | libumq.so（已在链接路径） |

## 11. 相关文档

- `doc/ubsocket/UBSOCKET-ARCHITECTURE.ch.md` — UBSocket 内部架构与调用链
- `doc/ubsocket/UBSOCKET-USER-GUIDE.md` — UBSocket 使用手册
