# UBSocket

## 1 简介

`UBSocket`通信加速库，支持拦截TCP应用中的`POSIX Socket API`，将TCP通信转换为UB高性能通信，从而实现通信加速。使用`UBSocket`，传统TCP应用或TCP通信库可以少修改甚至不修改源码，快速使能UB通信。`UBSocket`的通信加速能力已经在[bRPC](https://brpc.apache.org/zh/docs/overview/)上验证，并取得“相对原生TCP性能提升40%+”的收益，未来将继续拓展更多场景。

## 2 编译和使用

### 2.1 底层依赖

`UBSocket`的运行必须依赖UB硬件。此外，`UBSocket`还依赖了如下软件。

- `openssl`
- [`libboundscheck`](https://atomgit.com/openeuler/libboundscheck)
- `urma`

在编译和使用`UBSocket`前，请确保环境的软硬件符合要求。

在`OpenEuler`系统中，可以通过如下命令安装软件依赖。

```shell
yum install -y openssl
yum install -y libboundcheck

# 安装内核态urma
modprobe ubcore  
modprobe uburma

# 安装用户态urma
yum install -y umdk-urma*

# 关闭numa balancing
echo 0 > /proc/sys/kernel/numa_balancing
echo "kernel.numa_balancing=0" >> /etc/sysctl.conf
sysctl -p
```

> 说明：
>
> - urma用户态和内核态的软件包均需要安装。urma和系统强相关，建议优先参考系统配套的urma安装指导，上述urma安装仅做参考。
> - 非Euler系统，软件源中可能没有libboundcheck，需要源码安装。

### 2.2 编译

`UBSocket`归属于在`UBS Comm`项目，使用了该项目的部分公共能力，故需要分两部分编译。进行源码编译前，请先下载`UBS Comm`源码，并切换到目标分支或tag。

```shell
# 编译UMQ
cd ubs-comm/src/hcom/umq
mkdir build && cd build
cmake ..
make -j32
```

完成`UMQ`的编译后，可以得到如下目标编译产物：

- `build/src/libumq.so`

- `build/src/qbuf/libumq_buf.so`

- `build/src/umq_ub/libumq_ub.so`


> 说明：
>
> 在编译UMQ时，可以通过`-DOPENSSL_ROOT_DIR=/path/to/openssl`制定`openssl`路径。


```shell
# 编译ubsocket
cd ubs-comm/src/ubsocket
mkdir build && cd build
cmake ..
make -j32
```

完成`UBSocket`的编译后，可以得到`build/brpc/librpc_adapter_brpc.so`目标编译产物。

> 说明：
>
> 在编译UBSocket时，可以通过`-DUMQ_INCLUDE=/path/to/umq_include -DUMQ_LIB=/path/to/umq_lib`来指定umq的头文件和lib库文件路径（如`=-DUMQ_INCLUDE=/prefix/ubs-comm/src/hcom/umq/include/umq/  -DUMQ_LIB=/prefix/ubs-comm/src/hcom/umq/build/src/libumq.so`）。

### 2.3 使用

`UBSocket`通过`LD_PRELOAD`的方式劫持TCP应用中的`POSIX Socket API`并转换为UB通信，无需修改TCP应用源码。假设TCP应用正常启动命令为`./application`，可以通过如下命令启动来使用`UBSocket`通信加速能力。

```shell
$ env LD_PRELOAD=/path/to/lib/librpc_adapter_brpc.so \
RPC_ADPT_TRANS_MODE=UB \
RPC_ADPT_DEV_NAME="bonding_dev_0" \
RPC_ADPT_SRC_EID="xxxx:xxxx:0000:0000:0000:0000:0100:0000" \
RPC_ADPT_LOG_LEVEL=info \
RPC_ADPT_TX_DEPTH=1024 \
RPC_ADPT_RX_DEPTH=1024 \
RPC_ADPT_READV_UNLIMITED=true \
./application
```

> 说明：
> 通过`urma_admin show`命令，可以查询到bonding_dev_0设备对应的eid。

## 3 配置项含义

在启动`UBSocket`时，支持通过环境变量进行配置，各环境变量的含义如下。

| 名称                       | 含义                   | 取值范围                                                     | 默认值  | 必填                               |
| :------------------------- | :--------------------- | :----------------------------------------------------------- | :------ |----------------------------------|
| RPC_ADPT_TRANS_MODE        | 通信协议               | ub，ib                                                       | ub      | 否                                |
| RPC_ADPT_DEV_NAME          | 设备名称               | 根据实际场景填写设备名称；例如，udma2或者bonding_dev_0       | NA      | 是，使用bonding设备时可以不填               |
| RPC_ADPT_DEV_IP            | 设备名称               | 根据实际场景填写，支持ipv6和ipv4写法。`ub协议下不需要填写`   | NA      | 否                                |
| RPC_ADPT_EID_IDX           | 使用普通设备的eid编号  | ub协议下，通过`urma_admin show`命令查询获得                  | 0       | `RPC_ADPT_DEV_NAME`为普通设备时必填      |
| RPC_ADPT_SRC_EID           | 使用bonding设备的eid   | ub协议下，通过`urma_admin show`命令查询获得                  | NA      | `RPC_ADPT_DEV_NAME`为bonding设备时必填 |
| RPC_ADPT_LOG_LEVEL         | 日志级别               | emerg，alert，crit，err，warn，notice，info，debug           | info    | 否                                |
| RPC_ADPT_LOG_USE_PRINTF    | 是否将日志打印到前台   | 0，1                                                         | 0       | 否                                |
| RPC_ADPT_TX_DEPTH          | 发送队列深度           | 最小值是2，设置上限由实际机器环境决定（根据命令`urma_admin show --whole`中`max_jfc_depth`与`max_jfs_depth`两者的最小值） | 1024     | 否                                |
| RPC_ADPT_RX_DEPTH          | 接受队列深度           | 最小值是2，设置上限由实际机器环境决定（根据命令`urma_admin show --whole`中`max_jfc_depth`与`max_jfr_depth`两者的最小值） | 1024     | 否                                |
| RPC_ADPT_READV_UNLIMITED   | 是否打开readv上报限制  | false，true                                                  | true   | 否                                |
| RPC_ADPT_BLOCK_TYPE        | 内存池的最小分片       | default，small，medium，large                                | default | 否                                |
| RPC_ADPT_POOL_INITIAL_SIZE | IO内存的总大小，单位MB | 应用按需配置                                                 | 1024    | 否                                |
| RPC_ADPT_UB_FORCE | 是否强制使用UB协议加速TCP | 0：不强制用UB加速TCP 1：强制用UB加速TCP                                                | 0    | 否                                |
| RPC_SCHEDULE_POLICY | 设置多平面负载分担策略 | affinity，rr                                                | affinity   | 否                                |
| RPC_AUTO_FALLBACK_TCP | 协议不匹配时是否自动降级为TCP | 0, 1                                                | 1   | 否                                |
| UBSOCKET_TRACE_ENABLE      | 是否打开trace统计       | false, true                                                 | false    | 否                                |
| UBSOCKET_TRACE_TIME        | 控制维测数据输出间隔（单位s）   | [1, 300]                                                    | 10       | 否                               |
| UBSOCKET_TRACE_FILE_PATH   | 控制维测数据输出路径  | [1, 512]                                                    | /tmp/ubsocket/log | 否                        |
| UBSOCKET_TRACE_FILE_SIZE   | 控制维测数据文件大小（MB）   | [1, 300]                                                   | 10 | 否                        |
| RPC_ADPT_ENABLE_SHARE_JFR | 设置是否开启共享JFR | true, false                                                | false   | 否                                |
| RPC_ADPT_SHARE_JFR_RX_QUEUE_DEPTH | 设置开启共享JFR后，每个Socket链接接收缓存队列深度 | 最小值是64，设置上限由实际机器环境决定                                                | 1024   | 否                                |

>  说明：
>
>  - `UBSocket`支持使用bonding设备和普通udma设备，通过`urma_admin show`命令可以查看各设备信息。
>  - 普通udma设备，需要感知网络拓扑连线，使用较复杂，通常用于开发调测。
>  - 建议应用使用bonding设备，`UBSocket`会自动选择bonding设备及自动选路，进一步简化使用。



## 4 其它

`bRPC`内部实现了内存池管理功能，默认单个内存块大小为8K，通过增大内存块大小，可以提升大包发送性能。通过调整`bRPC源码`和调整`UBSocket`配置项可以使用大内存块传输，具体需要做如下两部分调整：

- 修改BRPC源码`iobuf.h`iobuf.h中`DEFAULT_BLOCK_SIZE`，可以从8K（8192）调整为16K/32K/64K。
- 通过`UBSocket`配置项`RPC_ADPT_BLOCK_TYPE`，相应调整`UBSocket`中内存块大小。

> 说明：
>
> 启用更大的内存块后，可能需要消耗更多内存，可以通过`RPC_ADPT_POOL_INITIAL_SIZE`配置`UBSocket`内存池大小。
