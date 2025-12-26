# UBSocket

`UBSocket`是一个基于UB网络无损适配`Socket API`接口的高性能通信库。通过使用`UBSocket`，用户无需修改原有基于`Socket API`开发的代码，即可基于UB网络进行通信加速。

## 1 简介

UB是一种面向超节点的互联协议，将IO、内存访问和各类处理单元件的通信统一在同一互联技术体系，实现高性能数据搬移、资源同一管理、资源灵活组合、处理单元间高效协同和高效编程，具有高性能、低时延等特性。当前诸多业务基于传统`Socket API`进行开发，而转换为UB组网，原有业务需要基于UB接口进行二次开发，业务环境组网多种多样，适配开发工作量大。基于上述情况，提供`UBSocket`高性能通信库，无需修改原有业务代码，即可基于UB网络进行加速。

## 2 环境要求

高性能通信库`UBSocket`将`Socket`连接转化成UB连接，从而达到高性能、低时延的目的，因此需要节点间为UB组网并且节点间UB协议通信正常。同时环境上需安装`UMQ`动态库的标准安装包。

## 3 源码下载

```shell
$ git clone <ubs-comm-repo-url>
```

## 4 源码目录结构

`UBSocket`源码位于`ubs-comm`目录下，如下所示：

```shell
.
├── build
├── doc
├── src
    ├── hcom      // hcom通信库源码
    ├── ubsocket  // ubsocket通信库源码
├── test
└── build.sh
```

## 5 编译

`UBSocket`在源码目录下提供CMakeLists进行编译，具体编译流程如下：

```shell
$ cd ./src/ubsocket
$ mkdir build && cd build
$ cmake ..
$ make -j8
```

编译后在`build/brpc`路径下可以找到一个名为`librpc_adapter_brpc.so`的动态库，即为UBSocket高性能通信库。

## 6 使用说明

在运行程序前通过LD_PRELOAD方式加载此通信库，将Socket API转换为UB网络的通信API，如下所示：
```shell
$ env LD_PRELOAD=/path/to/lib/librpc_adapter_brpc.so \
RPC_ADPT_TRANS_MODE=UB \
RPC_ADPT_DEV_NAME="bonding_dev_0" \
RPC_ADPT_SRC_EID="xxxx:xxxx:0000:0000:0000:0000:0100:0000" \
RPC_ADPT_LOG_LEVEL=info \
RPC_ADPT_TX_DEPTH=1024 \
RPC_ADPT_RX_DEPTH=1024 \
RPC_ADPT_READV_UNLIMITED=true \
./your_program
```
> 说明：
> 通过`urma_admin show`命令，可以查询到bonding_dev_0设备对应的eid。

UBSocket通过环境变量配置通信库的各种属性
| 环境变量 | 含义 |
| :--- | :--- |
| RPC_ADPT_TRANS_MODE | 协议模式 |
| RPC_ADPT_DEV_NAME | 设备名称 |
| RPC_ADPT_LOG_LEVEL | 日志级别 |
| RPC_ADPT_TX_DEPTH | 发送队列深度 |
| RPC_ADPT_RX_DEPTH | 接受队列深度 |
| RPC_ADPT_READV_UNLIMITED | 是否打开readv上报限制 |
| RPC_ADPT_BLOCK_TYPE | 内存池的最小分片,default(8k) small(16k) medium(32k) large(64k) |
| RPC_ADPT_POOL_INITIAL_SIZE | IO内存的总大小，应用按需配置 |
| RPC_ADPT_EID_IDX | 使用普通设备的eid编号 设备为普通设备时需填写 |
| RPC_ADPT_SRC_EID | 使用bonding设备的eid 设备为bonding设备时需填写|
| RPC_ADPT_LOG_LEVEL | 打印的日志级别 |
| RPC_ADPT_LOG_USE_PRINTF | 是否将日志打印到前台 |

## 7 其他
若需要使能BRPC内存块为64k，则需要通过以下方式修改
1. 修改BRPC源码`iobuf.h`iobuf.h中`DEFAULT_BLOCK_SIZE`从8192改为65536
2. 适配ubsocket时，需要加上配置项
```shell
$ RPC_ADPT_BLOCK_TYPE="large" RPC_ADPT_POOL_INITIAL_SIZE=4096
```