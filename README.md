# UBS-COMM

本仓库包含两个独立出包的通信库子项目：

| 子项目 | 目录 | 详细文档 |
|--------|------|----------|
| **HCOM** | `src/hcom/` | [`src/hcom/README.md`](src/hcom/README.md) |
| **UBSocket** | `src/ubsocket/` | [`src/ubsocket/README.md`](src/ubsocket/README.md) |

> 两个子项目支持通过 `build.sh` 一站式构建, 也可分别独立编译和测试。**编译和测试前必须阅读各子项目文档（见上方表格链接）。**

## 1 源码下载

```shell
# 方法一
$ git clone <repo-url>
$ git submodule update --init --recursive
# 方法二
$ git clone <repo-url> --recurse-submodules
```

## 2 源码目录结构

```shell
.
├── build      // 构建脚本
├── doc        // 项目文档
├── src        // 子项目源码
│   ├── hcom   // HCOM
│   └── ubsocket  // UBSocket
├── test       // UT 和性能测试
└── build.sh   // HCOM 构建入口
```

## 3 平台兼容性说明

> **重要：单元测试（UT）通过 ≠ 端到端（E2E）验证通过。**

| 组件 | x86_64 | aarch64 | 说明 |
|------|--------|---------|------|
| HCOM — TCP | 编译 + UT | 编译 + UT | E2E 需对应传输硬件 |
| HCOM — RDMA | 编译 + UT | 编译 + UT | **E2E 需 RDMA 网卡** |
| HCOM — SHM | 编译 + UT | 编译 + UT | **E2E 需共享内存支持** |
| HCOM — UB | 编译 + UT | 编译 + UT + **E2E** | **E2E 仅 aarch64**（Kunpeng） |
| UBSocket | 编译 + UT | 编译 + UT + **E2E** | **E2E 仅 aarch64**（Kunpeng） |

- **UT**：使用 mockcpp + fake_ibv_static + urma stub 模拟底层依赖，与平台无关，x86 和 ARM 均可全量通过。
- **E2E**（实际物理机端到端验证）：需要对应传输层的硬件支持。
  - TCP 需要网络栈和网卡，适用性最广。
  - RDMA 需要 RDMA 网卡（如 InfiniBand/RoCE）。
  - SHM 需要共享内存支持（同一宿主机进程间通信）。
  - UB 依赖 Kunpeng 硬件，UBSocket 依赖 UMQ/URMA 运行时，**仅 aarch64（Kunpeng 920/950）可用**。

## 4 编译构建

### 4.1 构建依赖

构建前需安装以下工具链和依赖（**仅支持 openEuler**）：

```shell
$ dnf install -y cmake gcc gcc-c++ make git rdma-core-devel openssl-devel libboundscheck time
```

### 4.2 编译

`build.sh` 支持一站式构建全部子项目：

```shell
# 仅 HCOM（默认）
$ ./build.sh

# HCOM + UMQ + UBSocket
$ UMQ_BUILD=on UBSOCKET_BUILD=on ./build.sh

# HCOM debug + UT + UMQ + UBSocket + UBSocket UT
$ HCOM_BUILD_TYPE=debug HCOM_BUILD_TESTS=on UMQ_BUILD=on UBSOCKET_BUILD=on UBSOCKET_UT=on ./build.sh
```

> **注意**：仅执行UT，无需进行E2E测试，未安装完整 URMA SDK 时增加 `USE_URMA_STUB=ON`，提供 `umq_ub` 编译需要的依赖。
>
> 各子项目的独立编译、测试、样例命令，**必须分别阅读**：
> - HCOM 构建命令：[`src/hcom/README.md §编译`](src/hcom/README.md)
> - UBSocket 构建与测试命令：[`src/ubsocket/README.md`](src/ubsocket/README.md)

直接运行 test binary 前需设置 `LD_LIBRARY_PATH`（包含 `dist/hcom_3rdparty/libboundscheck/lib` 和 `dist/hcom/lib`）。

运行 UBSocket 单元测试：

```shell
$ ctest --test-dir src/ubsocket/build --output-on-failure
```

### 4.3 容器/Docker 环境说明

编译和运行环境仅支持：**openEuler**。

在容器（或任何最小化环境）中构建和运行测试前，确保已安装上述工具链和依赖。仓库无需额外 Dockerfile，`build.sh` 直接管理构建流程。

测试二进制通过 `dlopen` 动态加载 `libibverbs.so` 和 `libssl.so`，即使使用 `fake_ibv_static` 模拟 RDMA verbs 也是如此。

## License

UBS-COMM 采用 Mulan V2 License.

## 贡献指南

请阅读 `CONTRIBUTING.md` 以了解如何贡献项目。
