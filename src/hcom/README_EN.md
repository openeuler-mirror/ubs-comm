# HCOM

`HCOM` is a high-performance communication library applicable to C/S applications. It has the following features:

- **High usability**: The underlying layer of `HCOM` supports multiple types of NIC hardware and communication protocols (such as `RDMA`, `TCP`, `SHM`, and `UB`). It shields the differences between different types of hardware or transmission protocols and provides a uniform API for developers. In addition, `HCOM` provides `QoS` capabilities (such as flow control, fault detection, and message retransmission) and authentication and encryption capabilities for developers to use.
- **High performance**: `HCOM` achieves ultimate performance through the combination of software and hardware. For different scenarios, the software implements acceleration features such as multi-thread management, `RNDV` (Rendezvous protocol, used in large-packet scenarios), and `MultiRail` (network port aggregation to fully utilize network bandwidth).

## 1. User Guide

`HCOM` provides the following documents for developers (located in `doc/hcom/`):
- [`HCOM-API-Spec.md`](../../doc/hcom/HCOM-API-Spec.md)
- [`HCOM-Architecture-Design-Specification.md`](../../doc/hcom/HCOM-Architecture-Design-Specification.md)
- [`HCOM-Tutorial-Demo.md`](../../doc/hcom/HCOM-Tutorial-Demo.md)
- [`HCOM-Tutorial-UseCase.md`](../../doc/hcom/HCOM-Tutorial-UseCase.md)

## 2. Compilation

> Run the following commands from the repository root (`build.sh` is at the root, not under `src/hcom/`).

`HCOM` provides a unified compilation and build script (`build.sh` at the repository root) in the code repository. You can directly execute the script to compile and build the project. (This script is also used to build the package in the CI pipeline.) By default, no configuration item is required. You can directly execute the script.

```shell
$ ./build.sh
```

After the execution is complete, you can find a `xxx.tar.gz` software package in the **dist** directory of the source code. The core content and description of the package are as follows:

```shell
$ tree
.
├── include  // C&C++ header files
│   └── hcom
│       ├── capi
│       │   ├── hcom_c.h
│       │   └── hcom_service_c.h
│       ├── hcom.h
│       └── hcom_service.h
└── lib     // C&C++ dynamic and static libraries
    ├── libhcom.so
    └── libhcom_static.a
```

You can use environment variables to control the compilation process of `build.sh`.

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

## 3. Compiling and Running Performance Test Tools

HCOM performance test cases are stored in the following directory:

- `test/hcom/tools/perf_test/`: stores performance test cases, which are linked to the `HCOM` static library.

Considering the gated build time, the perf_test case is not compiled by default. See `test/hcom/tools/perf_test/README.md` for details, or run the following commands to enable environment variables and then compile:

```
$ export HCOM_BUILD_EXAMPLE=on
$ ./build.sh
```

## 4. Compiling and Executing UT Cases

> Run the following commands from the repository root.

You can manually compile and execute UT cases as follows:

```shell
# UT cases involve many mock objects. The mock framework requires specific symbols
# and compilation can only be performed in debug mode.
$ export HCOM_BUILD_TYPE=debug
# When the package is built, UT is not compiled by default. Enable it manually.
$ export HCOM_BUILD_TESTS=on
# Run the build script to compile.
$ ./build.sh
# Before running UT cases, add third-party libraries to the search path.
$ export LD_LIBRARY_PATH=dist/hcom_3rdparty/libboundscheck/lib:dist/hcom/lib:$LD_LIBRARY_PATH
# Execute the UT cases and generate a test report. This process takes a long time,
# and the result is stored in the build directory.
$ ./build/generate_gtest_report.sh
# Generate UT coverage information. The result is stored in the build directory.
$ ./build/generate_lcov_report.sh
```
