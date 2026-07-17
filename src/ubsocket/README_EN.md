# UBSocket

## 1 Introduction

The `UBSocket` communication acceleration library intercepts `POSIX Socket API` in TCP applications and converts TCP communication into UB high-performance communication, thereby accelerating communication. With `UBSocket`, traditional TCP applications or TCP communication libraries can quickly enable UB communication with minimal or even no modification to the source code. The communication acceleration capability of `UBSocket` has been verified on [bRPC](https://brpc.apache.org/zh/docs/overview/), achieving a performance improvement of over 40% compared to native TCP. In the future, more scenarios will be explored.

## 2 Compilation and Use

### 2.1 Underlying Dependencies

The running of `UBSocket` depends on UB hardware. In addition, `UBSocket` depends on the following software:

- `openssl` and `openssl-devel`
- [`libboundscheck`](https://atomgit.com/openeuler/libboundscheck)
- `urma`

Before compiling and using `UBSocket`, ensure that the software and hardware in the environment meet the requirements.

In `openEuler`, you can run the following commands to install software dependencies:

```shell
yum install -y openssl openssl-devel
yum install -y libboundscheck

# Installing kernel-mode URMA
modprobe ubcore  
modprobe uburma

# Installing user-mode URMA
yum install -y umdk-urma*

# Installing the compiler
yum install -y gcc gcc-c++

# Disabling NUMA balancing
echo 0 > /proc/sys/kernel/numa_balancing
echo "kernel.numa_balancing=0" >> /etc/sysctl.conf
sysctl -p
```

> Note:
>
> - Both the user-mode and kernel-mode URMA software packages need to be installed. URMA is closely related to the system. You are advised to refer to the URMA installation guide of the system. The preceding URMA installation is for reference only.
> - In a non-EulerOS system, the software source may not contain libboundscheck. You need to install libboundscheck using the source code.

### 2.2 Compilation

`UBSocket` belongs to the `UBS Comm` project and uses some common capabilities of the project. Therefore, the compilation has two parts. Before compiling the source code, download the `UBS Comm` source code and switch to the target branch or tag.

```shell
# Compiling the UMQ
cd ubs-comm/src/hcom/umq
mkdir build && cd build
cmake ..
make -j32
```

After `UMQ` is compiled, the following target compilation products are obtained:

- `build/src/libumq.so`

- `build/src/qbuf/libumq_buf.so`

- `build/src/umq_ub/libumq_ub.so`

> Note:
>
> When compiling the UMQ, you can use `-DOPENSSL_ROOT_DIR=/path/to/openssl` to specify the `openssl` path.

```shell
# Compiling ubsocket
cd ubs-comm/src/ubsocket
mkdir build && cd build
cmake ..
make -j32
```

After `UBSocket` is compiled, the `build/csrc/libubsocket.so` target compilation product is obtained.

> Note:
>
> When compiling UBSocket, you can use `-DUMQ_INCLUDE=/path/to/umq_include -DUMQ_LIB=/path/to/umq_lib` to specify the paths of the UMQ header file and library file (for example, `=-DUMQ_INCLUDE=/prefix/ubs-comm/src/hcom/umq/include/umq/ -DUMQ_LIB=/prefix/ubs-comm/src/hcom/umq/build/src/libumq.so`).

### 2.3 Use

`UBSocket` hijacks the `POSIX Socket API` in TCP applications in `LD_PRELOAD` mode and converts it into UB communication. The source code of TCP applications does not need to be modified. Assume that the normal startup command for a TCP application is `./application`. You can run the following command to start the TCP application to use the `UBSocket` communication acceleration capability:

```shell
$ env LD_PRELOAD=/path/src/ubsocket/build/csrc/libubsocket.so \
UBSOCKET_TRANS_MODE=ub \
UBSOCKET_DEV_NAME="bonding_dev_0" \
UBSOCKET_SRC_EID="xxxx:xxxx:0000:0000:0000:0000:0100:0000" \
UBSOCKET_LOG_LEVEL=info \
UBSOCKET_TX_DEPTH=1024 \
UBSOCKET_RX_DEPTH=1024 \
UBSOCKET_READV_UNLIMITED=true \
./application
```

> Note:
> You can run the `urma_admin show` command to query the EID of the bonding_dev_0 device.

## 3 Description of Configuration Items

When starting `UBSocket`, you can configure environment variables. The following table describes the environment variables.

| Name                      | Meaning                  | Value Range                                                    | Default Value | Mandatory                              |
| :------------------------- | :--------------------- | :----------------------------------------------------------- | :------ |----------------------------------|
| UBSOCKET_TX_DEPTH          | Send queue depth          | The minimum value is 64. The upper limit is determined by the actual machine environment (the smaller value between `max_jfc_depth` and `max_jfs_depth` in the `urma_admin show --whole` command).| 1024     | No                               |
| UBSOCKET_RX_DEPTH          | Receive queue depth          | The minimum value is 64. The upper limit is determined by the actual machine environment (the smaller value between `max_jfc_depth` and `max_jfr_depth` in the `urma_admin show --whole` command).| 2048     | No                               |
| UBSOCKET_READV_UNLIMITED   | Whether to enable the readv reporting restriction | false, true                                                 | true   | No                               |
| UBSOCKET_BLOCK_TYPE        | Minimum fragment of the memory pool      | default, large                               | default | No                               |
| UBSOCKET_POOL_INITIAL_SIZE | Total size of the I/O memory, in MB.| Set based on application requirements                                                | 1024    | No                               |
| UBSOCKET_USE_UB_FORCE | Whether to forcibly use the UB protocol to accelerate TCP| false: UB is not forcibly used to accelerate TCP. true: UB is forcibly used to accelerate TCP.                                               | false    | No                               |
| UBSOCKET_SCHEDULE_POLICY | Multi-plane load balancing policy| affinity_priority, affinity, rr                                               | affinity_priority   | No                               |
| UBSOCKET_MONITOR_ENABLE      | Whether to enable trace statistics      | false, true                                                 | true    | No                               |
| UBSOCKET_MONITOR_INTERVAL        | Interval for outputting maintenance and test data (unit: s)  | [1, 300]                                                    | 10       | No                              |
| UBSOCKET_MONITOR_FILE_PATH   | Output path of maintenance and test data. The path length ranges from 1 to 512 bytes. | [1, 512]                                                    | /tmp/ubsocket/log | No                       |
| UBSOCKET_MONITOR_FILE_SIZE   | Size of the maintenance and test data file (MB)  | [1, 300]                                                   | 10 | No                       |
| UBSOCKET_CLI_ENABLE         | Whether to enable the trace CLI function| true, false                                                | false   | No                               |
| UBSOCKET_SHARE_JFR_ENABLE  | Whether to enable JFR sharing| false, true                                              | true   | No                               |
| UBSOCKET_USE_BRPC_ZCOPY    | Whether to use the brpc zcopy function| false, true                                               | true   | No                               |
| UBSOCKET_LINK_PRIORITY     | SL priority of URMA traffic| [0, 15] | -1 | No|
| UBSOCKET_POOL_MAX_SIZE     | Maximum value for elastic capacity expansion of the UB communication memory occupied by a single bRPC process, in MB| [UBSOCKET_POOL_INITIAL_SIZE + 64, 6144]. The minimum capacity expansion size at a time is 64 MB. Therefore, UBSOCKET_POOL_MAX_SIZE minus UBSOCKET_POOL_INITIAL_SIZE is 64 MB or higher.| 2048    | No                               |
| UBSOCKET_BUF_POOL_DEPTH    | Thread memory pool depth of a single bRPC process                    | Set based on application requirements           | 24576   | No                               |

> Note:
>
> - `UBSocket` supports bonding devices and common UDMA devices. You can run the `urma_admin show` command to view information about each device.
> - Common UDMA devices need to detect network topology connections and are complex to use. They are usually used for development and commissioning.
> - It is recommended that applications use bonding devices. `UBSocket` automatically selects bonding devices and routes, further simplifying use.

## 4 Others

The `bRPC` implements memory pool management. The default size of a single memory block is 8 KB. You can increase the size of a memory block to improve the performance of sending large packets. You can use large memory blocks for transmission by adjusting the `bRPC source code` and `UBSocket` configuration items. The specific adjustments are as follows:

- Change the value of `DEFAULT_BLOCK_SIZE` in the BRPC source code `iobuf.h` from 8 KB (8192) to 16 KB, 32 KB, or 64 KB.
- Adjust the memory block size in `UBSocket` by setting the `UBSocket` configuration item `UBSOCKET_BLOCK_TYPE`.

> Note:
>
> After a larger memory block is enabled, more memory may be consumed. You can use `UBSOCKET_POOL_INITIAL_SIZE` to configure the size of the `UBSocket` memory pool.
