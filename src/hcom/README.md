# HCOM

`HCOM`是一个适用于C/S架构应用程序的高性能通信库，主要有以下特征：

- **高易用性**：`HCOM`底层支持多种网卡硬件及通信协议（如`RDMA`、`TCP`、`SHM`、`UB`），屏蔽了这些硬件或传输协议间的差异，向开发者提供统一的API。此外，`HCOM`还提供了`QoS`能力（如流控、故障检测、消息重传等），认证加密能力等，进一步方便开发者使用。
- **高性能**：`HCOM`通过软硬件结合，实现极致高性能。针对不同的场景，软件实现了多线程管理、`RNDV`（Rendezvous协议，用于大包场景）、`MultiRail`（多网口聚合，充分利用网络带宽）等加速特性。

## 1 用户指南

`HCOM`提供给开发者的资料（位于 `doc/hcom/`）主要有以下几本：
- [`HCOM-API-Spec.md`](../../doc/hcom/HCOM-API-Spec.md)
- [`HCOM-Architecture-Design-Specification.md`](../../doc/hcom/HCOM-Architecture-Design-Specification.md)
- [`HCOM-Tutorial-Demo.md`](../../doc/hcom/HCOM-Tutorial-Demo.md)
- [`HCOM-Tutorial-UseCase.md`](../../doc/hcom/HCOM-Tutorial-UseCase.md)

## 2 编译

> 以下命令在仓库根目录执行（`build.sh` 位于根目录，不在 `src/hcom/` 下）。

`HCOM`在代码仓中提供了统一的编译构建脚本（即根目录的`build.sh`），可以直接执行该脚本编译构建（该脚本同时用于CI流水线构建出包）。默认无需任何配置项，直接执行即可。

```shell
$ ./build.sh
```

执行完毕后可以在源码的dist目录中找到一个`xxx.tar.gz`的软件包，其核心内容及介绍如下所示。

```shell
$ tree
.
├── include  // C&C++头文件
│   └── hcom
│       ├── capi
│       │   ├── hcom_c.h
│       │   └── hcom_service_c.h
│       ├── hcom.h
│       └── hcom_service.h
└── lib     // C&C++动态库和静态库
    ├── libhcom.so
    └── libhcom_static.a
```

可以通过环境变量，对`build.sh`的编译过程进行控制，如下所示。

```shell
$ cat build.sh | head -n 23
#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
# Script for building HCOM.
# Build options can be configured through environment variables.
# (1) HCOM_BUILD_TYPE(optional, default is release) => set build type.(release/debug)
# (2) HCOM_BUILD_TESTS(optional, default is off) => enable build test or not.(on/off)
# (3) HCOM_BUILD_JAVA_SDK(optional, default is off) => build java sdk or not.(on/off)
# (4) HCOM_BUILD_SERVICE(optional, default is on) => build service level or not.(on/off)
# (5) HCOM_BUILD_RDMA(optional, default is on) => build rdma or not.(on/off)
# (6) HCOM_BUILD_SOCK(optional, default is on) => build sock (tcp/uds) or not.(on/off)
# (7) HCOM_BUILD_SHM(optional, default is on) => build shm or not.(on/off)
# (8) HCOM_BUILD_EXAMPLE(optional, default is off) => build example and perf.(on/off)
# (9) HCOM_ENABLE_ARM_KP(optional, default is on) => check kunpeng or not.(on/off)
# (10) HCOM_TEST_TOOL_PATH(optional) => test tool install path.(mockcpp/gtest/dtfuzz)
# (11) HCOM_CI_WORKSPACE(optional) => ci workspace, for buildInfo.properties file.
# (12) HCOM_BUILD_RPM(optional, default is on) => build rpm.(on/off)
# (13) HCOM_BUILD_TOOLS_PERF(optional, default is off) => build perf tools, for Bazel build only (on/off)
# (14) HCOM_BUILD_HW_CRC(optional, default is off) => build with hardware based crc.(on/off)
# (15) HCOM_BUILD_MULTICAST(optional, default is off) => build multicast or not.(on/off)

# version: 1.0.0
# change log:
# ***********************************************************************
```

## 3 编译和执行性能测试工具

HCOM的性能测试用例存放在以下目录：

- `test/hcom/tools/perf_test/`：存放性能用例，用例链接`HCOM`静态库。

考虑门禁构建时间，默认不会编译perf_test用例，请参考 `test/hcom/tools/perf_test/README.md` 编译，或执行以下命令，开启环境变量后编译：

```shell
# HCOM E2E 性能测试工具 hcom_perf（需 server+client 配对 + 传输硬件，传输硬件具体要求参见仓库根 README §3 平台兼容性说明）,
$ export HCOM_BUILD_EXAMPLE=on
# 根目录build编译
$ ./build.sh
```

执行如下命令查看hcom_perf使用方式：

```shell
hcom_perf --help
```

## 4 编译和执行UT用例

> 以下命令在仓库根目录执行。

可以按照如下方式，手动编译和执行UT用例。

```shell
# ut用例中涉及较多mock，mock框架需要知道具体的符号，只能以debug模式编译
$ export HCOM_BUILD_TYPE=debug
# 构建出包时，默认不编译ut，需手动开启
$ export HCOM_BUILD_TESTS=on
# 直接执行构建脚本，即可编译
$ ./build.sh
# 执行UT用例前，需将第三方库加入搜索路径
$ export LD_LIBRARY_PATH=dist/hcom_3rdparty/libboundscheck/lib:dist/hcom/lib:$LD_LIBRARY_PATH
# 执行UT用例并生成测试报告，耗时较长，结果存放在build目录中
$ ./build/generate_gtest_report.sh
# 生成UT覆盖率信息，结果存放在build目录中
$ ./build/generate_lcov_report.sh
```
