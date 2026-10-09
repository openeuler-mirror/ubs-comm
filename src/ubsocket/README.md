## 🔄Latest News

* UB Support May 30, 2026

## 🎉Introduction

UBSocket is an easy and high performance communication library on top of Unified Bus from Huawei. Key concepts:
* <b>easy to use</b>: socket api compatible
* <b>high performance</b>: bypass kernel, zero copy, fast connection
* <b>large scale</b>: support massive connection 
* <b>well integrated</b>: well integrated with bRPC and so on

## Architecture overview
![architecture](../../doc/ubsocket/figures/ubsocket_architecture.png)

UBSocket is designed highly extendable to support different hardware, for example: Unified Bus, and RoCE/Posix SHM in the future.

## 🔥Performance

* [Details](./../../doc/ubsocket/performance/perf.md)

## 🚀Quickstart

### 方式一：仅构建 UMQ + UBSocket

跳过 HCOM 构建，直接执行 UMQ/UBSocket 编译脚本：

```shell
UMQ_BUILD=on UBSOCKET_BUILD=on bash build/build_umq_and_ubsocket.sh
```

### 方式二：手动 cmake 编译

编译和运行详见 [UBSocket 使用手册 §构建和运行](./../../doc/ubsocket/UBSOCKET-USER-GUIDE.md#4-构建和运行)。

## 📑How to use

* [Get Started](./../../doc/ubsocket/UBSOCKET-USER-GUIDE.md)
* [API Reference](./../../doc/ubsocket/api/api.md)

## 📦Pre-request hardware and software

- Hardware
    - Host: Kunpeng 950

- Software:
    - OS:
    - URMA:

## 📝 Other information

- [License](./../../LICENSE)
