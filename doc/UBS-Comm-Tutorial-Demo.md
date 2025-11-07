|  |  |  |  |  |
|:--:|:--:|:--:|:--:|:--:|
|  |  |  |  |  |
|  | **UBS CommTutorial Demo** |  |  |  |
|  | **文档版本** | **1** |  |  |
|  | **发布日期** | **2025-09-30** |  |  |
| ![华为网格系统---方案4-032.png](media/image1.png) |  |  |  |  |
|  | 华为技术有限公司 |  | ![附件1-16K](media/image2.png) |  |

[TABLE]

| 华为技术有限公司 |                                             |
|------------------|---------------------------------------------|
| 地址：           | 深圳市龙岗区坂田华为总部办公楼 邮编：518129 |
| 网址：           | <https://www.huawei.com>                    |
| 客户服务邮箱：   | <support@huawei.com>                        |
| 客户服务电话：   | 4008302118                                  |

[TABLE]

# 前言

## 概述

本文档详细的描述了UBS Comm的使用指南，包括环境配置、安全管理和库文件链接方法等内容。

## 读者对象

本文档主要适用于升级的操作人员。操作人员必须具备以下经验和技能：

- 熟悉当前网络的组网和相关网元的版本信息。

- 有该设备维护经验，熟悉设备的操作维护方式。

## 符号约定

在本文中可能出现下列标志，它们所代表的含义如下。

[TABLE]

# 目 录

[前言 [iii](#前言)](#前言)

[1 介绍 [1](#介绍)](#介绍)

[2 环境配置 [5](#环境配置)](#环境配置)

[2.1 组网规划 [5](#组网规划)](#组网规划)

[2.2 环境要求 [5](#环境要求)](#环境要求)

[2.3 安装使用 [7](#安装使用)](#安装使用)

[2.3.1 安装MLNX_OFED驱动 [7](#安装ubs-comm)](#安装ubs-comm)

[2.3.2 配置服务器侧RDMA网卡无损特性 [10](#rdma场景配置服务器侧rdma网卡无损特性)](#rdma场景配置服务器侧rdma网卡无损特性)

[2.3.3 安装UBS Comm [11](#安装ubs-comm)](#安装ubs-comm)

[2.3.4 UBC仿真环境 [12](#_Toc256000009)](#_Toc256000009)

[3 使用指导 [15](#使用指导)](#使用指导)

[3.1 服务层 [15](#服务层)](#服务层)

[3.1.1 说明 [15](#说明)](#说明)

[3.1.2 服务端 [15](#服务端)](#服务端)

[3.1.3 客户端 [16](#客户端)](#客户端)

[3.1.4 服务端与客户端启动后 [16](#服务端与客户端启动后)](#服务端与客户端启动后)

[3.1.5 服务层编程 [17](#服务层编程)](#服务层编程)

[3.2 传输层 [30](#传输层)](#传输层)

[3.2.1 说明 [30](#说明-1)](#说明-1)

[3.2.2 服务端 [30](#服务端-1)](#服务端-1)

[3.2.3 客户端 [30](#客户端-1)](#客户端-1)

[3.2.4 服务端和客户端启动后 [31](#服务端和客户端启动后)](#服务端和客户端启动后)

[3.2.5 传输层编程 [32](#传输层编程)](#传输层编程)

[3.3 Java使用指导 [52](#_Toc256000023)](#_Toc256000023)

[3.3.1 Java服务层编程 [52](#_Toc256000024)](#_Toc256000024)

[4 安全管理 [67](#ZH-CN_TOPIC_0000002363191544)](#ZH-CN_TOPIC_0000002363191544)

[5 UBS Comm库文件链接方法参考 [69](#ubs-comm库文件链接方法参考)](#ubs-comm库文件链接方法参考)

[A 公网地址声明 [70](#公网地址声明)](#公网地址声明)

[B 术语和缩略语 [71](#术语和缩略语)](#术语和缩略语)

# 介绍

1.  概述

UBS Comm（UB service communication）是一个适用于高带宽和低延迟网络C/S（Client/Server）架构应用程序的高性能通信框架。

UBS Comm提供一组支持各种协议的高级API（Application Programming Interface），并屏蔽了包括RDMA（Remote Direct Memory Access）、TCP（Transmission Control Protocol）、UDS（Unix Domain Socket）、SHM（Shared Memory）、UBC（Unified bus clan）等低级API的复杂性与差异性，同时尽可能发挥硬件能力，以保证其拥有高性能。

2.  整体方案

UBS Comm主要分为服务层和传输层。其中，服务层（[图1-1](#fig42211835370)中的Service所展示的内容）提供了更易用的API，包含Net Service（服务层对象）、Net Channel（消息收发通道）、同步/异步模型、链路重连和限流、IO超时检测、传输加密等功能。传输层（[图1 软件架构](#fig42211835370)的Net Driver所展示的内容）也有单独的API，同时提供多个协议（RDMA/TCP/UDS/SHM/UBC）的同步异步通信、心跳、传输加密等功能。

1.  软件架构

**![image-20251029102007735](C:\Users\y00835993\AppData\Roaming\Typora\typora-user-images\image-20251029102007735.png)**

3.  特性介绍

- 线程模型

UBS Comm会创建3种类型的线程：主线程、Worker线程和心跳线程。

- 主线程：每个Client或Server会创建一个主线程进行侦听、建链、收发消息等操作。

- Worker线程：同时可以配置多个Worker线程，每条链路EP（End Point）会在建链时选择某个Worker线程，每个Worker线程可能对应多个EP（多条链路）。链路的异步收发回调、断链回调等都会由Worker线程进行处理。

用户能够使用参数workerGroups配置线程组以及每个组线程的个数，并通过参数workerGroupsCpuSet配置线程绑核。

- 心跳线程：心跳线程会定时监测对端状态，以保证能感知对端服务是否还存在。用户可以通过参数heartBeatIdleTime、heartBeatProbeTime、heartBeatProbeInterval来配置心跳检查时长。

  1.  RDMA模式下，启动心跳线程，对所有链路发送单边写来判断链路状态。

  2.  TCP模式下，使用TCP协议的keepalive特性，配置TCP_KEEPIDLE/TCP_KEEPINTVL等字段，保证链路状态正常。

      1.  线程模型

> ![image-20251029102016672](C:\Users\y00835993\AppData\Roaming\Typora\typora-user-images\image-20251029102016672.png)

- 双向RPC

UBS Comm提供双向的RPC通信，每个Client和Server都是对等的，都可以启动监听线程等待对方建链，可以由建立Instance时的第三个bool参数startOobSvr来决定是否启动监听线程。Client和Server彼此之间可以相互建链也可以相互收发消息。

- RNDV特性

RNDV协议（Rendezvous协议）是MPI通信协议中的一类，会在接收端协调缓存来接收信息，通常适用于发送比较大的消息。为了增加易用性，UBS Comm引入Rendezvous协议提供给用户使用。

RNDV协议主要采用单边+双边结合的方式完成。使用双边协议传递控制消息以及回复响应，如单边的MR信息、用户控制头、用户处理结果等；使用单边协议进行数据拉取，并通过回调通知业务处理。

- 超时机制

UBS Comm可以对每个IO进行超时检测，通过获取每个IO的时间戳标记，然后加入到定时器中，检测标记时间和当前时间，判断该IO是否发生超时，从而及时进行业务回调处理。

用户可以通过NetServiceOpInfo结构的timeout字段来配置每个IO的超时时间。

- 认证加密

UBS Comm提供了加密认证的能力，可以选择AES_128_GCM_SHA256、AES_256_GCM_SHA384、AES_128_CCM_SHA256、TLS_CHACHA20_POLY1305_SHA256四种加密算法进行加密，同时可以选择设置TLS版本，当前默认且仅支持TLS 1.3版本。用户只需要把“enableTls”参数设置为“true”，然后配置“cipherSuite”参数，注册三个TLS相关的回调函数（具体可参见“《UBS-Comm-API-Spec.md》”中tls相关章节），提供CA证书、公钥、私钥信息，即可开启加密的流程。



- 传输口令，密钥，银行账号等敏感数据、敏感个人数据和批量个人数据时，建议开启TLS能力。

- 当用户使用UBS Comm时，应该自己做好三面隔离，如果将UBS Comm使用在登录认证场景时，用户需要自己做好管理接口提供接入认证机制。

- 当用户使用TLS加密能力时，建议用户做好证书安全管理，参见[证书安全管理](#section1911412125313)。

&nbsp;

- RDMA协议加速特性Device Memory

在发送数据量很小的情况下，RDMA协议提供DM（Device Memory）特性来加速传输效率，DM是存在于硬件网卡上的内存，直接使用该内存可以免去将消息拷贝到网卡的时间从而提升性能。在UBS Comm中可以通过配置选项的dmSegCount和dmSegSize来配置，其中dmSegSize决定使用DM特性的消息最大长度，在小于或等于1024bytes时有明显提升，dmSegCount决定预申请多少个dmSegSize长度的内存。在配置过大时由于硬件内存有限会申请失败，但依旧可以正常运行UBS Comm，只是无法使用DM特性。

- RDMA协议加速特性inline

普通的情况下，消息请求中存放的是需要发送消息的地址，网卡需要去地址处拷贝内容。而当发送数据大小在128bytes及以下时，RDMA提供一种比DM更高效的特性inline，inline可以把需要发送的消息直接存放在消息请求中，可以明显节省拷贝用时。

- 兼容性检查

UBS Comm版本号区分主次版本，如HCOM1.0，HCOM1.1。其中小数点前数字为主版本，小数点后数字为次版本。客户端服务端主版本要相同，但服务端的次版本一定要大于等于客户端的次版本。

- 限制客户端的连接数消减DOS攻击风险

UBS Comm服务端支持开启建链TLS认证，但认证过程比较耗时；DOS攻击可以通过伪造大量的客户端发送建链报文对UBS Comm服务端进行攻击，迫使UBS Comm服务端忙于执行TLS认证校验，无法响应合法建链请求。

支持通过配置项限定某个客户端IP地址最大允许EP建链数，默认值为250，异常IP发来的请求达到阈值后直接报错，不再执行TLS认证校验，并通过日志告警；通过提高恶意建链成本，提升服务端服务韧性。

# 环境配置

[2.1 组网规划](#组网规划)

[2.2 环境要求](#环境要求)

[2.3 安装使用](#安装使用)

## 组网规划

UBS Comm组网可由2台服务器组成，其中：

- Server用于等待其他节点建链，也可以主动向其他节点建链，并可以使用链路来向对端发送消息。

- Client用于主动向其他节点建链，并可以使用链路来向对端发送消息。![image-20251029101933187](C:\Users\y00835993\AppData\Roaming\Typora\typora-user-images\image-20251029101933187.png)

  

## 环境要求

1.  硬件要求


| 服务器名称 | TaiShan服务器 |
|----|----|
| 处理器 | 鲲鹏处理器 |
| 网卡 | Mellanox CX5 (仅使用RDMA通讯协议时必须，使用其他通讯协议不需要) |
| CPU | 通过系统文件“/sys/devices/system/cpu/cpu0/regs/identification/midr_el1”中获取CPU厂商信息判断，当前配套机型鲲鹏处理器型号为0x48。 |

2.  软件版本

| 软件名称  | 软件版本                                                     |
| --------- | ------------------------------------------------------------ |
| OS        | l   openEuler 20.03 LTS  l   openEuler 22.03 LTS  l   openEuler 24.03 LTS  l   CentOS 7.6 |
| rdma-core | 42.7                                                         |
| GCC       | 7.3.0                                                        |
| CCA       | VPP V300R024C10SPC001                                        |

3. 获取软件安装包

| 名称     | 包名                                       | 发布类型 | 说明                    | 获取地址                             |
| -------- | ------------------------------------------ | -------- | ----------------------- | ------------------------------------ |
| UBS Comm | l   ubs-hcom-2.0.0-1.oe2403sp1.aarch64.rpm | 开源     | UBS Comm软件rpm安装包。 | 华为技术企业网：Link  鲲鹏社区：Link |

[TABLE]

1.  校验软件包完整性

为了防止软件包在传递过程或存储期间被恶意篡改，获取软件包时需下载对应的数字签名文件用于完整性验证。

1.  参见[获取软件安装包](#section3489574613)获取软件包。

&nbsp;

1.  获取《OpenPGP签名验证指南》。

- 运营商客户：请访问<http://support.huawei.com/carrier/digitalSignatureAction>

- 企业客户：请访问<https://support.huawei.com/enterprise/zh/tool/pgp-verify-TL1000000054>

  1.  根据《OpenPGP签名验证指南》进行软件安装包完整性检查。



- 如果校验失败，请不要使用该软件包，先联系华为技术支持工程师解决。

- 使用软件包安装或升级之前，也需要按上述过程先验证软件包的数字签名，确保软件包未被篡改。

----结束

1.  UBS Comm软件版本可查询

UBS Comm支持查询软件版本。

1.  参考[获取软件安装包](#section3489574613)获取软件包。

2.  参考[安装UBS Comm](#ubc场景安装与规格限制)安装UBS Comm

&nbsp;

1.  查询UBS Comm软件版本。

rpm -qi ubs-hcom-2.0.0

----结束

## 编译构建

### 拉取三方库

执行以下命令自动拉取

`git submodule update --init –recursive`

或以下命令手动拉取

```
yum install libboundscheck

mkdir 3rdparty && cd 3rdparty && git clone <https://szv-open.codehub.huawei.com/OpenSourceCenter/linux-rdma/rdma-core.git>

cd ..
```

### 编译ubs comm源码

执行以下命令编译

bash ./build.sh

执行完毕后可以在源码的dist目录中找到BoostKit-hcom_1.0.0_aarch64.tar.gz的压缩包

### 高级编译选项

Ubs comm支持编译隔离，可通过环境变量控制部分功能是否编译。

具体的环境变量请参考源码中README.md与build.sh文件

## 安装使用

### 安装UBS Comm

1.  前提条件

安装libboundscheck rpm包

> 方式一：`yum install libboundscheck`
>
> 方式二：通过gitee下载源码编译
>
> https://gitee.com/openeuler/libboundscheck
>
> 【下载发行版本】（最新的release版本是 v1.1.16 两年前）  
> https://gitee.com/openeuler/libboundscheck/releases/tag/v1.1.16  
> 【编译】  
> `make CC=gcc`  
>
> 编译后根目录下 lib目录中，存在libboundscheck.so

2.  操作步骤（安装rpm软件包）

    1.  `rpm -ivh ubs-hcom-2.0.0-1.oe2403sp1.aarch64.rpm`
2.  安装完成，安装路径可以通过rpm -qpl ubs-hcom-2.0.0-1.oe2403sp1.aarch64.rpm查看
    3.  若需使用RDMA场景，请参考2.4.1、2.4.2章节配置RDMA驱动环境
4.  若需使用UBC场景，请参考2.4.3章节配置UBC驱动环境

----结束



hcom_utils.h和hcom_ref.h文件中的函数为hcom内部使用。

3.  操作步骤（安装tar.gz软件包）

    1.  tar -zxvf BoostKit-hcom_1.0.0_aarch64.tar.gz

    2.  动态库和头文件会解压到当前路径，用户可以按自己的需求将文件拷贝到需要的路径

    3.  若需使用RDMA场景，请参考2.4.1、2.4.2章节配置RDMA驱动环境

    4.  若需使用UBC场景，请参考2.4.3章节配置UBC驱动环境

----结束



hcom_utils.h和hcom_ref.h文件中的函数为hcom内部使用。

### RDMA场景安装MLNX_OFED驱动

![注意](media/image13.png)

使用RDMA通信协议时，请在UBS Comm所有通信节点执行本章节操作。未使用RDMA通信协议，则可跳过本章节。

1.  安装步骤

    1.  执行以下命令，查询服务器操作系统。

uname -a

返回信息如下所示。

Linux 4826-node62 5.10.0-182.0.0.95.oe2203sp3.aarch64

1.  执行以下命令，查看Mellanox网卡信息。

lspci \|grep Mellanox

返回信息如下所示。

81:00.0 Ethernet controller: Mellanox Technologies MT28800 Family \[ConnectX-5 Ex\]  
81:00.1 Ethernet controller: Mellanox Technologies MT28800 Family \[ConnectX-5 Ex\]

2.  获取与操作系统匹配的MLNX_OFED驱动包至本地。

地址为<https://network.nvidia.com/products/infiniband-drivers/linux/mlnx_ofed/>。

1.  下载页面

![](media/image14.png)

3.  执行以下命令，新建目录并将操作系统镜像文件挂载至新建目录。

mkdir -p */mnt/iso*  
mount openEuler-20.03-LTS-aarch64-dvd.iso */mnt/iso*



操作系统镜像名称请根据实际情况进行修改。

4.  配置操作系统镜像源，此处以配置本地镜像源为例，配置前请做好镜像源配置文件备份。

&nbsp;

1.  执行以下命令打开镜像源配置文件。

vi /etc/yum.repos.d/openEuler.repo

2.  按“i”进入编辑模式，只保留以下内容。

\[OS\]  
name=OS  
baseurl=file:///mnt/iso  
enabled=1  
gpgcheck=0

3.  按“Esc”键，输入**:wq!**，按“Enter”保存并退出编辑。

    1.  执行以下命令刷新软件包缓存信息。

yum makecache

2.  上传驱动包至服务器并解压。

tar -zxvf *MLNX_OFED_LINUX-5.4-3.7.5.0-openeuler22.03-x86_64.tgz*

3.  进入压缩包解压文件夹目录下，执行以下命令安装驱动。

./mlnxofedinstall –force

- 若提示内核不匹配，则执行以下命令。

./mlnxofedinstall --add-kernel-support

- 若不想进行固件更新，则执行以下命令。

./mlnxofedinstall --without-fw-update



- 安装程序将删除所有之前安装的OFED驱动，并重新安装，系统会提示您确认删除旧包。

- **./mlnxofedinstall -h**可查询参数配置，请根据实际情况选择参数。

  1.  安装完成后，执行以下命令重启服务器。

reboot

2.  执行以下命令，配置MLNX_OFED驱动安装完成后自启动。

chkconfig --add openibd  
/etc/init.d/openibd start  
chkconfig openibd on

3.  执行以下命令验证MLNX_OFED驱动是否安装成功。

- Server节点请执行以下命令：

ib_send_bw -d mlx5_1 -a

- Client节点请执行以下命令：

ib_send_bw -d mlx5_1 -a *\<Server节点IP地址\>*

返回信息如下所示，即为安装成功。

---------------------------------------------------------------------------------------
Send BW Test  
Dual-port : OFF Device : mlx5_1  
Number of qps : 1 Transport type : IB  
Connection type : RC Using SRQ : OFF  
PCIe relax order: ON  
ibv_wr\* API : ON  
TX depth : 128  
CQ Moderation : 100  
Mtu : 4096\[B\]  
Link type : Ethernet  
GID index : 3  
Max inline data : 0\[B\]  
rdma_cm QPs : OFF  
Data ex. method : Ethernet  
---------------------------------------------------------------------------------------  
local address: LID 0000 QPN 0x19b8 PSN 0xa3aa02  
GID: 00:00:00:00:00:00:00:00:00:00:255:255:10:10:01:62  
remote address: LID 0000 QPN 0x19b9 PSN 0xf3ab0  
GID: 00:00:00:00:00:00:00:00:00:00:255:255:10:10:01:62  
---------------------------------------------------------------------------------------  
\#bytes \#iterations BW peak\[MB/sec\] BW average\[MB/sec\] MsgRate\[Mpps\]  
2 1000 10.60 10.11 5.298300



当对RDMA通信协议有性能调优需求时，请参见[Performance Tuning for Mellanox Adapters](https://enterprise-support.nvidia.com/s/article/performance-tuning-for-mellanox-adapters)。

----结束

1.  卸载

    1.  进入解压包。

    &nbsp;

    1.  执行以下命令，卸载MLNX_OFED驱动。

./uninstall.sh

----结束

### RDMA场景配置服务器侧RDMA网卡无损特性

RDMA无损配置可以提高网络传输的性能和效率，确保数据传输的可靠性和一致性，同时减少CPU的负担。

![注意](media/image13.png)

未使用RDMA通信协议时，以下操作步骤可以不执行；否则需要在使用UBS Comm的所有通信节点上执行。

1.  登录服务器，执行以下命令查询CX5网卡设备net_card信息，以CX5网卡为例。

net_card=\$(ibdev2netdev \| grep *mlx5_1* \| awk '{print \$5}')

1.  使用[步骤1](#li1919910204286)查询出的net_card作为参数，执行以下命令进行CX5网卡配置。

cma_roce_tos -d *mlx5_1* -t 106  
mlnx_qos -i \${net_card} --pfc 0,0,0,1,0,0,0,0 --trust dscp  
ifconfig \${net_card} mtu 4500



服务器每次重启后都需要重新执行当前步骤进行配置。

2.  执行以下命令配置网卡的CNP中的DSCP字段。

echo 48 \>/sys/class/net/\${net_card}/ecn/roce_np/cnp_dscp

3.  执行以下命令配置网卡的RoCEv2中的DCQCN拥塞控制机制。

echo 1 \>/sys/class/net/\${net_card}/ecn/roce_np/enable/3  
echo 1 \>/sys/class/net/\${net_card}/ecn/roce_rp/enable/3

----结束

### UBC场景安装与规格限制

1.  前提条件

安装前置的LCNE、MAMI、UDMA、URMA、UBSE等驱动

2.  规格限制

- 双边发送数据长度小于等于64KB。

- 单边读写数据小于等于16MB。

- 单边带宽为0.12 MB/S。

3. 操作步骤

- rpm -ivh ubs-hcom-2.0.0-1.oe2403sp1.aarch64.rpm

- 安装完成，动态库的安装路径可以通过rpm -qpl ubs-hcom-2.0.0-1.oe2403sp1.aarch64.rpm查看

### 风险声明

当前公知Jetty存在内存越权访问风险，token_value泄漏和篡改风险，需要部署在可信的环境中。

----结束

# 使用指导

[3.1 服务层](#服务层)

## 服务层

### 说明

本章节将通过基础示例来演示如何使用UBS Comm，开发者可以通过学习此指导来快速上手UBS Comm。UBS Comm向开发者提供了传输层和服务层，因此使用指导也将分别提供一个示例代码来演示如何使用传输层和服务层。

### 服务端

- 使用NetService::Instance创建一个service的对象。

```
UBSHcomServiceOptions options;

UBSHcomService *service = UBSHcomService::Create(RDMA, "server1", options);
```

- 此处创建了一个使用RDMA协议的名为server1的服务端Driver，支持通过option设置基本配置项。

- 设置NetServiceOptions选项，使用service对象注册回调函数，并用service的Bind方法设置需要侦听的IP地址和端口。

```
service->RegisterRecvHandler(ReceivedRequest);

service->RegisterChannelBrokenHandler([](const UBSHcomChannelPtr &channel) {}, UBSHcomChannelBrokenPolicy::BROKEN_ALL);

service->RegisterSendHandler([](const UBSHcomServiceContext &ctx) { return 0; });

service->RegisterOneSideHandler([](const UBSHcomServiceContext &ctx) { return 0; });  
service->Bind("uds://" + oobIp + ":" + std::to_string(oobPort), NewChannel);
```



- ServiceOptions的参数，详情请参见《UBS-COMM-API-Spec》的“ServiceOptions”章节。

- 注册回调函数，详情请参见《UBS-COMM-API-Spec》的“RegisterRecvHandler”等章节。

- Bind用来设置需要侦听的IP地址和端口以及收到建链请求时的回调。

  1.  调用service的Start方法，完成服务端的启动。

`service->Start();`

----结束

### 客户端

- 使用NetService::Instance创建一个service的对象。

```
UBSHcomServiceOptions options;

UBSHcomService *service = UBSHcomService::Create(RDMA, "client1", options);
```

- 此处创建了一个使用RDMA协议的名为client1的服务端Driver，支持通过option设置基本配置项。

- 设置NetServiceOptions选项，使用service对象注册回调函数。

```
service->RegisterRecvHandler(ReceivedRequest);

service->RegisterChannelBrokenHandler([](const UBSHcomChannelPtr &channel) {}, UBSHcomChannelBrokenPolicy::BROKEN_ALL);

service->RegisterSendHandler([](const UBSHcomServiceContext &ctx) { return 0; });
```

- 调用service的Start方法，完成客户端的启动。

`service->Start();`

----结束

### 服务端与客户端启动后

1.  当服务端与客户端都完成启动后，客户端的Service可以调用Connect方法来连接服务端。

UBSHcomConnectOptions opt;

UBSHcomChannelPtr channel

service-\>Connect("tcp://" + oobIp + ":" + std::to_string(oobPort), channel, opt);



- oobIp：需要建链的IP地址。

- oobPort：需要建链的Port。

- channel：Connect函数的返回值，即为得到的链路的本端，对端的HcomChannelPtr在NewChannel回调函数的第二个参数中获得。

- options：设置这条链路的选项。详情请参见《UBS-COMM-API-Spec》的“ConnectOptions”章节。

  1.  连接成功后，服务端与客户端都会获得一个HcomChannel对象，服务端与客户端都可以使用该对象来调用各种消息发送接口向对端发送消息。

UBSHcomRequest req(reinterpret_cast\<void \*\>(addr), dataSize, 0);  
channel-\>Send(req, nullptr)

详情请参见《UBS-COMM-API-Spec》的“UBSHcomChannel::Send”章节。

----结束

### 服务层编程

此示例仅限帮助开发者具象化理解如何使用UBS Comm，作为实际使用场景的参考，请勿直接复制使用。

1.  Sever端完整示例代码

&nbsp;

1.  以下为服务层Server端的完整示例代码，当Server端收到Client端的消息时，会调用初始化时注册的回调函数RequestReceived，可以在回调函数中给Client端回复消息。

```
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Author:
 * Description: UBC通信服务端，接收客户端大量请求并响应
 */
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <iostream>

#include "hcom/hcom_service.h"
#include "hcom/hcom_service_context.h"
#include "hcom/hcom.h"

using namespace ock::hcom;

ock::hcom::UBSHcomService *service = nullptr;

UBSHcomServiceProtocol driverType = ock::hcom::UBSHcomServiceProtocol::UBC;
std::string oobIp = "";
uint16_t oobPort = 9981;

std::string ipSeg = "192.168.100.0/24";
int32_t dataSize = 1024;
uint32_t workerNum = 1;
int16_t asyncWorkerCpuId = -1;
UBSHcomChannelPtr channel = nullptr;

uint32_t multiRailEnable = 1;
uint32_t multiRailThresh = 8192;
UBSHcomUbcMode mUbcMode = UBSHcomUbcMode::LowLatency;
uint32_t splitSendThreshold = UINT32_MAX;

using TestRegMrInfo = struct _reg_sgl_info_test_ {
    uintptr_t lAddress = 0;
    uint32_t size = 0;
    UBSHcomMemoryKey lKey;
};

UBSHcomRegMemoryRegion rndvMr;
uintptr_t rndvAddr = 0;
uint32_t rndvSize = 1048576;
uint32_t rndvThreshold = UINT32_MAX;

TestRegMrInfo localMrInfo;

int NewChannel(const std::string &ipPort, const UBSHcomChannelPtr &ch, const std::string &payload)
{
    NN_LOG_INFO("new channel " << ch->GetId() << " call from " << ipPort << " payload: " << payload);
    channel = ch;

    UBSHcomTwoSideThreshold threshold{};
    threshold.rndvThreshold = rndvThreshold;

    auto result = channel->SetTwoSideThreshold(threshold);
    if (result != 0) {
        NN_LOG_ERROR("failed to set two side threshold, result " << result);
        return false;
    }
    return 0;
}

int CallBackReply(UBSHcomServiceContext &context)
{
    Callback *cb = UBSHcomNewCallback([](UBSHcomServiceContext &context) {}, std::placeholders::_1);
    if (NN_UNLIKELY(cb == nullptr)) {
        NN_LOG_ERROR("new callback is nullptr");
        return -1;
    }
    if (context.OpCode() == 0) {
        NN_LOG_DEBUG("receive msg, channel id " << context.Channel()->GetId() << ", MessageData " <<
            reinterpret_cast<char *>(context.MessageData()) << " MessageDataLen: " << context.MessageDataLen());
    } else if (context.OpCode() == 1) {
        UBSHcomRequest req;
        req.address = context.MessageData();
        req.size = context.MessageDataLen();

        UBSHcomReplyContext replyCtx;
        replyCtx.errorCode = 200;
        replyCtx.rspCtx = context.RspCtx();

        if (context.Channel()->Reply(replyCtx, req, cb) != 0) {
            NN_LOG_ERROR("failed to post message to data to server");
            return -1;
        }
    } else if (context.OpCode() == 2) {
        UBSHcomRequest req((void *)&localMrInfo, sizeof(localMrInfo), 1);
        UBSHcomReplyContext replyCtx(context.RspCtx(), 200);
        if (context.Channel()->Reply(replyCtx, req, cb) != 0) {
            NN_LOG_ERROR("failed to post message to data to server");
            return -1;
        }
    }
    return 0;
}

int ReceivedRequest(UBSHcomServiceContext &context)
{
    if (context.OpType() == UBSHcomServiceContext::Operation::SER_RNDV) {
        uintptr_t contextRsp = context.RspCtx();
        const UBSHcomChannelPtr &rspChannel = context.Channel();
        Callback *newCallback = UBSHcomNewCallback(
            [contextRsp, rspChannel](UBSHcomServiceContext &ctx) {
                if (NN_UNLIKELY(ctx.Result() != SER_OK)) {
                    NN_LOG_ERROR("Rndv recv callback failed " << ctx.Result());
                }

                UBSHcomRequest req;
                char str[] = "rndv reply!";
                char *ptr = str;
                req.address = str;
                req.size = strlen(str);

                UBSHcomReplyContext replyCtx;
                replyCtx.errorCode = 0;
                replyCtx.rspCtx = contextRsp;
                Callback *cb = UBSHcomNewCallback([](UBSHcomServiceContext &context) {}, std::placeholders::_1);
                if (NN_UNLIKELY(cb == nullptr)) {
                    NN_LOG_ERROR("new callback is nullptr");
                    return;
                }
                if (rspChannel->Reply(replyCtx, req, cb) != 0) {
                    NN_LOG_ERROR("failed to post message to data to server");
                }
            },
            std::placeholders::_1);
        if (context.Channel()->Recv(context, rndvAddr, dataSize, newCallback) != 0) {
            NN_LOG_ERROR("failed to rndv recv data to server");
            return -1;
        }
        return 0;
    }

    return CallBackReply(context);
}

bool CreateService()
{
    if (service != nullptr) {
        NN_LOG_ERROR("service already created");
        return false;
    }

    UBSHcomServiceOptions options;
    options.maxSendRecvDataSize = dataSize + 1024;
    options.workerGroupMode = ock::hcom::NET_EVENT_POLLING;
    if (asyncWorkerCpuId != -1) {
        options.workerGroupCpuIdsRange = { asyncWorkerCpuId, asyncWorkerCpuId };
    }
    service = UBSHcomService::Create(driverType, "server1", options);
    if (service == nullptr) {
        NN_LOG_ERROR("failed to create service already created");
        return false;
    }
    if (driverType != UBC) {
        service->SetDeviceIpMask({ ipSeg });
    }
    service->RegisterRecvHandler(ReceivedRequest);
    service->RegisterChannelBrokenHandler([](const UBSHcomChannelPtr &channel) {}, UBSHcomChannelBrokenPolicy::BROKEN_ALL);
    service->RegisterSendHandler([](const UBSHcomServiceContext &ctx) { return 0; });
    service->RegisterOneSideHandler([](const UBSHcomServiceContext &ctx) { return 0; });
    service->SetUbcMode(mUbcMode);

    if (driverType == SHM) {
        service->Bind("uds://" + oobIp + ":" + std::to_string(oobPort), NewChannel);
    } else if (driverType == UBC) {
        service->Bind("ubc://" + oobIp + ":" + std::to_string(oobPort), NewChannel);
    } else {
        service->Bind("tcp://" + oobIp + ":" + std::to_string(oobPort), NewChannel);
    }

    UBSHcomHeartBeatOptions hbOptions{};
    hbOptions.heartBeatIdleSec = 2;
    service->SetHeartBeatOptions(hbOptions);

    int result = 0;
    if ((result = service->Start()) != 0) {
        NN_LOG_ERROR("failed to initialize service " << result);
        return false;
    }
    NN_LOG_INFO("service initialized");
    return true;
}


bool RegSglMem()
{
    UBSHcomRegMemoryRegion mr;
    auto result = service->RegisterMemoryRegion(dataSize, mr);
    if (result != NN_OK) {
        NN_LOG_ERROR("reg mr failed");
        return false;
    }
    localMrInfo.lAddress = mr.GetAddress();
    mr.GetMemoryKey(localMrInfo.lKey);
    localMrInfo.size = mr.GetSize();

    strcpy(reinterpret_cast<char *>(localMrInfo.lAddress), "aaaaa server");
    return true;
}


bool RegRndvMem()
{
    void *address = memalign(4096, rndvSize);
    if (address == nullptr) {
        NN_LOG_ERROR("Failed to alloc memory, maybe lack of spare memory in system.");
        return false;
    }
    rndvAddr = reinterpret_cast<uintptr_t>(address);
    auto result = service->RegisterMemoryRegion(rndvAddr, rndvSize, rndvMr);
    if (result != NN_OK) {
        NN_LOG_ERROR("reg mr failed");
        free(address);
        return false;
    }
    memset(address, 'A', rndvSize);
    return true;
}

void SendMemInfo()
{
    UBSHcomRequest req((void *)&localMrInfo, sizeof(localMrInfo), 1);

    if ((channel->Send(req, nullptr)) != 0) {
        NN_LOG_ERROR("failed to send message to data to server");
        return;
    }
    NN_LOG_INFO("SendMemInfo success");
}

void SendRequest()
{
    NN_LOG_INFO("input q means quit, d means dump obj static, c means channel close");
    while (true) {
        auto tmpChar = getchar();
        switch (tmpChar) {
            case 'q':
                service->DestroyMemoryRegion(rndvMr);
                free(reinterpret_cast<void *>(rndvAddr));
                UBSHcomService::Destroy("server1");
                return;
            case 'd':
                NetObjStatistic::Dump();
                continue;
            case 'c':
                continue;
            case 's':
                SendMemInfo();
            default:
                NN_LOG_INFO("input q means quit, d means dump obj static, c means channel close");
                continue;
        }
    }
}

void exitFunc()
{
    service = nullptr;
}

void Run()
{
    if (!CreateService()) {
        return;
    }

    atexit(exitFunc);

    if (!RegSglMem()) {
        return;
    }

    if (!RegRndvMem()) {
        return;
    }

    SendRequest();
}

int main(int argc, char *argv[])
{
    struct option options[] = {
        {"driver", required_argument, NULL, 'd'},
        {"ip", required_argument, NULL, 'i'},
        {"port", required_argument, NULL, 'p'},
        {"size", required_argument, NULL, 's'},
        {"worker num", required_argument, NULL, 'w'},
        {"cpuId", required_argument, NULL, 'c'},
        {"multiRail", optional_argument, NULL, 'r'},
        {"multiRailThresh", optional_argument, NULL, 'R'},
        {"RndvThreshold", optional_argument, NULL, 'v'},
        {"ubcMode", optional_argument, NULL, 'u'},
        {"splitSendThreshold", optional_argument, NULL, 'S'},
        {NULL, 0, NULL, 0},
    };

    const char *usage = "usage\n"
        "        -d, --driver,                 driver type, 0 for rdma, 1 for tcp, 3 for shm, 7 for UBC\n"
        "        -i, --ip,                     server ip mask, e.g. 10.175.118.1; eid for UBC, e.g. "
        "4245:4944:0000:0000:0000:0000:0100:0000\n"
        "        -p, --port,                   server port, by default 9981; jetty id for UBC, e.g. 998\n"
        "        -s, --io size ,               max data size\n"
        "        -w, --worker num ,            worker num\n"
        "        -c, --cpuId,                  async worker\n"
        "        -r, --enableMultiRail,        enable multiRail\n"
        "        -R, --multiRailThresh,        multiRail threshhold\n"
        "        -v, --RndvThreshold,          Perf case only supports an RNDV threshold of less than 1048576, actual "
        "scenario requires a value less than UINT32_MAX\n"
        "        -u, --ubcMode,                UB-C mode, 0 means LowLatency, other value means HighBandwidth\n"
        "        -S, --splitSendThreshold,     the threshold of split send, UINT32_MAX by default\n";


    int ret = 0;
    int index = 0;

    std::string str = "d:i:p:s:w:c:r:R:v:u:S:";
    while ((ret = getopt_long(argc, argv, str.c_str(), options, &index)) != -1) {
        switch (ret) {
            case 'd':
                driverType = static_cast<UBSHcomServiceProtocol>((uint16_t)strtoul(optarg, NULL, 0));
                if (driverType > UBC) {
                    printf("invalid driver type %d", driverType);
                    return -1;
                }
                break;
            case 'i':
                oobIp = optarg;
                ipSeg = oobIp + "/24";
                break;
            case 'p':
                oobPort = (uint16_t)strtoul(optarg, NULL, 0);
                break;
            case 's':
                dataSize = (int32_t)strtoul(optarg, NULL, 0);
                break;
            case 'w':
                workerNum = (int32_t)strtoul(optarg, NULL, 0);
                break;
            case 'c':
                asyncWorkerCpuId = strtoul(optarg, nullptr, 0);
                break;
            case 'r':
                multiRailEnable = (uint32_t)strtoul(optarg, nullptr, 0);
                break;
            case 'R':
                multiRailThresh = (uint32_t)strtoul(optarg, nullptr, 0);
                break;
            case 'v':
                rndvThreshold = (uint32_t)strtoul(optarg, NULL, 0);
                break;
            case 'u':
                mUbcMode = std::stoi(optarg) ? UBSHcomUbcMode::HighBandwidth : UBSHcomUbcMode::LowLatency;
                break;
            case 'S':
                splitSendThreshold = (uint32_t)strtoul(optarg, nullptr, 0);
                break;
        }
    }

    Run();
    return 0;
}
```

2.  执行以下命令运行代码，启动Server端。

`./*pp_service_server_simple* -d 1 -i 127.0.0.1 -p 9980`

*pp_service_server_simple*：编译后可执行文件名，请根据实际情况进行修改。

- -d：配置Driver类型。

  1.  0：RDMA

  2.  1：TCP

  3.  2：UDS

  4.  3：SHM

  5.  7：UBC

- -i：Server IP地址。

- -p: 监听的端口

- 其余参数可以根据实际情况自由配置

  1.  Client端完整示例代码

1.  以下为服务层一个完整的Client端示例，入口为main函数，经过参数解析后，进入Run函数。

```
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Author:
 * Description: UBC通信客户端，使用多种不同接口发送大量请求组并计算请求的延迟和吞吐量
 */
#include <unistd.h>
#include <getopt.h>
#include <semaphore.h>
#include <thread>
#include <stdio.h>
#include "hcom/hcom_service.h"
#include "hcom/hcom_service_context.h"
#include "hcom/hcom.h"

using namespace ock::hcom;

UBSHcomService *service = nullptr;
UBSHcomChannelPtr channel = nullptr;

UBSHcomServiceProtocol driverType = ock::hcom::UBSHcomServiceProtocol::UBC;
std::string oobIp = "";
uint16_t oobPort = 9981;

std::string ipSeg = "192.168.100.0/24";
int32_t pingCount = 1000000;
uint64_t startTime = 0;
uint64_t finishTime = 0;
uint64_t asyncTime = 0;


uint64_t mode = 0;
uint64_t periodThreadCnt = 0;
int32_t dataSize = 1024;
int32_t epSize = 1;
int16_t asyncWorkerCpuId = -1;
char *data = nullptr;
char *rcvData = nullptr;
bool start = false;
uint32_t multiRailEnable = 1;
uint32_t multiRailThresh = 8192;
UBSHcomUbcMode mUbcMode = UBSHcomUbcMode::LowLatency;
uint32_t splitSendThreshold = UINT32_MAX;

using TestRegMrInfo = struct _reg_sgl_info_test_ {
    uintptr_t lAddress = 0;
    uint32_t size = 0;
    UBSHcomMemoryKey lKey;
};

TestRegMrInfo localMrInfo;
TestRegMrInfo remoteMrInfo;

UBSHcomRegMemoryRegion rndvMr;
uintptr_t rndvAddr = 0;
uint32_t rndvSize = 1048576;
uint32_t rndvThreshold = UINT32_MAX;

int ReceivedRequest(UBSHcomServiceContext &context)
{
    if (context.OpCode() == 1) {
        memcpy((void *)&remoteMrInfo, context.MessageData(), sizeof(remoteMrInfo));
        NN_LOG_INFO("remoteMrInfo lAddress is " << remoteMrInfo.lAddress << ", lKey: " << remoteMrInfo.lKey.keys[0] << ", size: " << remoteMrInfo.size);
    }
    return 0;
}

bool CreateService()
{
    if (service != nullptr) {
        NN_LOG_ERROR("service already created");
        return false;
    }

    UBSHcomServiceOptions options{};
    options.maxSendRecvDataSize = dataSize + 1024;
    options.workerGroupMode = ock::hcom::NET_EVENT_POLLING;
    if (asyncWorkerCpuId != -1) {
        options.workerGroupCpuIdsRange = {asyncWorkerCpuId, asyncWorkerCpuId};
    }
    service = UBSHcomService::Create(driverType, "client1", options);
    if (service == nullptr) {
        NN_LOG_ERROR("failed to create service already created");
        return false;
    }

    service->SetTimeOutDetectionThreadNum(periodThreadCnt);
    service->RegisterRecvHandler(ReceivedRequest);
    service->RegisterChannelBrokenHandler([](const UBSHcomChannelPtr &channel) {}, UBSHcomChannelBrokenPolicy::BROKEN_ALL);
    service->RegisterSendHandler([](const UBSHcomServiceContext &ctx) {return 0;});
    service->RegisterOneSideHandler([](const UBSHcomServiceContext &ctx) {return 0;});
    service->SetUbcMode(mUbcMode);

    if (driverType != UBC) {
        service->SetDeviceIpMask({ipSeg});
    }

    int result = 0;
    if ((result = service->Start()) != 0) {
        NN_LOG_ERROR("failed to start service " << result);
        return false;
    }
    NN_LOG_ERROR("service started");

    return true;
}

bool Connect()
{
    if (service == nullptr) {
        NN_LOG_ERROR("service is null");
        return false;
    }

    int result = 0;
    UBSHcomConnectOptions opt;
    opt.linkCount = epSize;

    NN_LOG_INFO("connect mode: " << mode);
    if (mode == 1) {
        opt.mode = UBSHcomClientPollingMode::SELF_POLL;
    }

    if (driverType == SHM) {
        result = service->Connect("uds://" + oobIp, channel, opt);
    } else if (driverType == UBC) {
        result = service->Connect("ubc://" + oobIp + ":" + std::to_string(oobPort), channel, opt);
    } else {
        result = service->Connect("tcp://" + oobIp + ":" + std::to_string(oobPort), channel, opt);
    }

    if (result != 0) {
        NN_LOG_ERROR("failed to connect to server, result " << result);
        return false;
    }

    data = static_cast<char *>(malloc(dataSize));

    UBSHcomTwoSideThreshold threshold{};
    threshold.rndvThreshold = rndvThreshold;
    threshold.splitThreshold = splitSendThreshold;

    result = channel->SetTwoSideThreshold(threshold);
    if (result != 0) {
        NN_LOG_ERROR("failed to set two side threshold, result " << result);
        return false;
    }

    return true;
}

void SendRequest()
{
    UBSHcomRequest req(reinterpret_cast<void *>(rndvAddr), dataSize, 0);

    if ((channel->Send(req, nullptr)) != 0) {
        NN_LOG_ERROR("failed to send message to data to server");
        return;
    }
}

bool CallRequest()
{
    UBSHcomRequest req(reinterpret_cast<void *>(rndvAddr), dataSize, 1);
    UBSHcomResponse rsp(reinterpret_cast<void *>(rndvAddr), dataSize);
    if ((channel->Call(req, rsp, nullptr)) != 0) {
        NN_LOG_ERROR("failed to call message to data to server");
        return false;
    }
    return true;
}

bool AsyncCallRequest()
{
    UBSHcomRequest req(reinterpret_cast<void *>(rndvAddr), dataSize, 1);

    rcvData = static_cast<char*>(malloc(dataSize));
    // char *rcvData = static_cast<char*>(malloc(dataSize));
    UBSHcomResponse rsp(rcvData, dataSize);

    int32_t ret = 0;
    sem_t sem;
    sem_init(&sem, 0, 0);
    Callback *callback = UBSHcomNewCallback(
        [&sem, &ret, &rsp](UBSHcomServiceContext &context) {
            if (NN_UNLIKELY(context.Result() != SER_OK)) {
                NN_LOG_ERROR("Channel Async send callback failed " << context.Result() << ", optype: " << context.OpType());
                ret = -1;
                sem_post(&sem);
                return;
            }
            memcpy(rsp.address, context.MessageData(), context.MessageDataLen());
            sem_post(&sem);
        },
        std::placeholders::_1);


    if ((channel->Call(req, rsp, callback)) != 0) {
        NN_LOG_ERROR("failed to call message to data to server");
        return false;
    }

    sem_wait(&sem);
    sem_destroy(&sem);
    if (ret != 0) {
        NN_LOG_ERROR("failed to async call");
        return false;
    }

    return true;
}

void AsyncSendRequest()
{
    UBSHcomRequest req(reinterpret_cast<void *>(rndvAddr), dataSize, 0);

    Callback *callback = UBSHcomNewCallback(
        [](UBSHcomServiceContext &context) {
            if (NN_UNLIKELY(context.Result() != SER_OK)) {
                NN_LOG_ERROR("Channel Async send callback failed " << context.Result() << ", optype: " <<
                    context.OpType());
            }
        },
        std::placeholders::_1);

    if ((channel->Send(req, callback)) != 0) {
        NN_LOG_ERROR("failed to send message to data to server");
        return;
    }
}

void ReadRequest()
{
    UBSHcomOneSideRequest req {};
    req.lAddress = localMrInfo.lAddress;
    req.lKey = localMrInfo.lKey;
    req.rAddress = remoteMrInfo.lAddress;
    req.rKey = remoteMrInfo.lKey;
    req.size = dataSize;

    if ((channel->Get(req, nullptr)) != 0) {
        NN_LOG_ERROR("failed to read data to server");
        return;
    }
}

void AsyncReadRequest()
{
    UBSHcomOneSideRequest req {};
    req.lAddress = localMrInfo.lAddress;
    req.lKey = localMrInfo.lKey;
    req.rAddress = remoteMrInfo.lAddress;
    req.rKey = remoteMrInfo.lKey;
    req.size = dataSize;

    Callback *callback = UBSHcomNewCallback(
        [](UBSHcomServiceContext &context) {
            if (NN_UNLIKELY(context.Result() != SER_OK || context.OpType() != 4)) {
                NN_LOG_ERROR("Channel Async read callback failed " << context.Result() << ", optype: " << context.OpType());
            } else {
                NN_LOG_INFO("Channel Async read callback successful " << context.Result());
            }
        },
        std::placeholders::_1);
    if ((channel->Get(req, callback)) != 0) {
        NN_LOG_ERROR("failed to read data to server");
        return;
    }
    NN_LOG_INFO("read data from server" << std::string((char *)req.lAddress));
}

void WriteRequest()
{
    UBSHcomOneSideRequest req {};
    req.lAddress = localMrInfo.lAddress;
    req.lKey = localMrInfo.lKey;
    req.rAddress = remoteMrInfo.lAddress;
    req.rKey = remoteMrInfo.lKey;
    req.size = dataSize;

    if ((channel->Put(req, nullptr)) != 0) {
        NN_LOG_ERROR("failed to read data to server");
        return;
    }
}

void AsyncWriteRequest()
{
    UBSHcomOneSideRequest req {};
    req.lAddress = localMrInfo.lAddress;
    req.lKey = localMrInfo.lKey;
    req.rAddress = remoteMrInfo.lAddress;
    req.rKey = remoteMrInfo.lKey;
    req.size = dataSize;

    Callback *callback = UBSHcomNewCallback(
        [](UBSHcomServiceContext &context) {
            if (NN_UNLIKELY(context.Result() != SER_OK || context.OpType() != 4)) {
                NN_LOG_ERROR("Channel Async write callback failed " << context.Result() << ", optype: " << context.OpType());
            } else {
                NN_LOG_INFO("Channel Async write callback successful " << context.Result());
            }
        },
        std::placeholders::_1);
    if ((channel->Put(req, callback)) != 0) {
        NN_LOG_ERROR("failed to read data to server");
        return;
    }
}

bool RegRndvMem()
{
    void *address = memalign(4096, rndvSize);
    if (address == nullptr) {
        NN_LOG_ERROR("Failed to alloc memory, maybe lack of spare memory in system.");
        return false;
    }
    rndvAddr = reinterpret_cast<uintptr_t>(address);
    auto result = service->RegisterMemoryRegion(rndvAddr, rndvSize, rndvMr);
    if (result != NN_OK) {
        NN_LOG_ERROR("reg mr failed");
        free(address);
        return false;
    }
    memset(address, 'B', rndvSize);
    return true;
}

int userChar = 0;

void RunInThread()
{
    while (!start) {
        usleep(1);
    }
    bool ret;
    switch (userChar) {
        case '0':
            for (int32_t i = 0; i < pingCount; i++) {
                SendRequest();
            }
            printf("SendRequest finish\n");
            break;
        case '1':
            for (int32_t i = 0; i < pingCount; i++) {
                AsyncSendRequest();
            }
            break;
        case '2':
            for (int32_t i = 0; i < pingCount; i++) {
                ret = CallRequest();
                if (!ret) {
                    return;
                }
            }
            break;
        case '3':
            for (int32_t i = 0; i < pingCount; i++) {
                ret = AsyncCallRequest();
                if (!ret) {
                    return;
                }
            }
            break;
        case '4':
            for (int32_t i = 0; i < pingCount; i++) {
                ReadRequest();
            }
            break;
        case '5':
            for (int32_t i = 0; i < pingCount; i++) {
                AsyncReadRequest();
            }
            break;
        case '6':
            for (int32_t i = 0; i < pingCount; i++) {
                WriteRequest();
            }
            break;
        case '7':
            for (int32_t i = 0; i < pingCount; i++) {
                AsyncWriteRequest();
            }
            break;
        default:
            return;
    }
}

void Test()
{
    NN_LOG_INFO(
        "input 0:send, 1:async send, 2:call, 3:async call, 4:read, 5:async read, 6:write, 7:async write ");
    int ret;
    while (true) {
        userChar = getchar();
        startTime = MONOTONIC_TIME_NS();

        std::thread threads[epSize];

        start = false;
        for (int i = 0; i < epSize; i++) {
            threads[i] = std::thread(RunInThread);
        }

        NN_LOG_INFO("Wait for finish");
        start = true;
        for (auto &t : threads) {
            t.join();
        }

        switch (userChar) {
            case '0':
                printf("\tType sync send\n");
                break;
            case '1':
                printf("\tType async send\n");
                break;
            case '2':
                printf("\tType sync call\n");
                break;
            case '3':
                printf("\tType async call\n");
                break;
            case '4':
                printf("\tType sync read\n");
                break;
            case '5':
                printf("\tType sync read\n");
                break;
            case '6':
                printf("\tType sync write\n");
                break;
            case '7':
                printf("\tType async write\n");
                break;
            case 'q':
                service->DestroyMemoryRegion(rndvMr);
                free(reinterpret_cast<void *>(rndvAddr));
                UBSHcomService::Destroy("client1");
                return;
            case 'd':
                NetObjStatistic::Dump();
                break;
            case 'c':
                printf("\tOperate close\n");
                service->Disconnect(channel);
                break;
            default:
                NN_LOG_INFO("input 0:send, 1:async send, 2:call, 3:async call, 4:read, 5:async read, 6:write, 7:sync "
                            "write ");
                continue;
        }

        if (userChar == 'd' || userChar == 'c' || userChar == 'r') {
            continue;
        }

        finishTime = MONOTONIC_TIME_NS();
        printf("\tPerf summary\n");
        printf("\tPingpong times:\t\t%d\n", pingCount);
        printf("\tData size:\t\t%d\n", dataSize);
        printf("\tEp size:\t\t%d\n", epSize);
        printf("\tThread count:\t\t%d\n", epSize);
        printf("\tTotal time(us):\t\t%f\n", (finishTime - startTime) / 1000.0);
        printf("\tTotal time(ms):\t\t%f\n", (finishTime - startTime) / 1000000.0);
        printf("\tTotal time(s):\t\t%f\n", (finishTime - startTime) / 1000000000.0);
        printf("\tLatency(us):\t\t%f\n", (finishTime - startTime) / pingCount / 1000.0);
        printf("\tAvg ops:\t\t%f pp/s\n", (pingCount * 1000000000.0) / (finishTime - startTime));
        printf("\tTotal ops:\t\t%f pp/s\n", (pingCount * 1000000000.0) / (finishTime - startTime) * epSize);
        printf("\tAvg bw:\t\t\t%f MB/s\n",
            (pingCount * 1000000000.0) / (finishTime - startTime) * dataSize / 1024 / 1024);
        printf("\tTotal bw:\t\t%f MB/s\n",
            (pingCount * 1000000000.0) / (finishTime - startTime) * dataSize / 1024 / 1024 * epSize);

        if (userChar == 'a') {
            printf("\tAsync call latency(us):\t%f\n", asyncTime / pingCount / 1000.0 / epSize);
            asyncTime = 0;
        }
    }
}

bool GetRemoteMr()
{
    UBSHcomRequest req(data, sizeof(data), 2);
    UBSHcomResponse rsp((void*)&remoteMrInfo, sizeof(remoteMrInfo));

    if ((channel->Call(req, rsp, nullptr)) != 0) {
        NN_LOG_INFO("failed to call message to data to server");
        return false;
    }
    NN_LOG_INFO("remoteMrInfo lAddress is " << remoteMrInfo.lAddress << ", lKey: " << remoteMrInfo.lKey.keys[0] << ", size: " << remoteMrInfo.size);
    return true;
}

bool RegSglMem()
{
    UBSHcomRegMemoryRegion mr;
    auto result = service->RegisterMemoryRegion(dataSize, mr);
    if (result != NN_OK) {
        NN_LOG_ERROR("reg mr failed");
        return false;
    }
    localMrInfo.lAddress = mr.GetAddress();
    mr.GetMemoryKey(localMrInfo.lKey);
    localMrInfo.size = dataSize;

    return true;
}

void exitFunc()
{
    free(data);
    free(rcvData);
    service = nullptr;
    data = nullptr;
    rcvData = nullptr;
}

void Run()
{
    if (!CreateService()) {
        return;
    }

    atexit(exitFunc);

    if (!RegSglMem()) {
        return;
    }

    if (!RegRndvMem()) {
        return;
    }

    if (!Connect()) {
        return;
    }

    if (!GetRemoteMr()) {
        return;
    }

    Test();
}

int main(int argc, char *argv[])
{
    struct option options[] = {
        {"driver", required_argument, NULL, 'd'},
        {"ip", required_argument, NULL, 'i'},
        {"port", required_argument, NULL, 'p'},
        {"pingpongtimes", required_argument, NULL, 't'},
        {"size", required_argument, NULL, 's'},
        {"epSize", required_argument, NULL, 'e'},
        {"epMode", required_argument, NULL, 'm'},
        {"timeout thread", required_argument, NULL, 'o'},
        {"cpuId", required_argument, NULL, 'c'},
        {"multiRail", optional_argument, NULL, 'r'},
        {"multiRailThresh", optional_argument, NULL, 'R'},
        {"RndvThreshold", optional_argument, NULL, 'v'},
        {"ubcMode", optional_argument, NULL, 'u'},
        {"splitSendThreshold", optional_argument, NULL, 'S'},
        {"rndvThreshold", optional_argument, NULL, 'v'},
        {NULL, 0, NULL, 0},
    };

    const char *usage = "usage\n"
        "        -d, --driver,                 driver type, 0 for rdma, 1 for tcp, 3 for shm, 7 for UBC\n"
        "        -i, --ip,                     coord server ip mask, e.g. 10.175.118.1; remote eid for UBC, e.g. "
        "4245:4944:0000:0000:0000:0000:0100:0000 \n"
        "        -p, --port,                   coord server port, by default 9981; jetty id for UBC, e.g. 998\n"
        "        -t, --pingpongtimes,          ping pong times\n"
        "        -s, --size,                   max data size\n"
        "        -e, --ep size,                connect and run ep size\n"
        "        -m, --ep mode,                connect and run ep mode\n"
        "        -o, --timeout thread,         range [1, 4]\n"
        "        -c, --cpuId,                  async worker\n"
        "        -r, --enableMultiRail,        enable multiRail\n"
        "        -R, --multiRailThresh,        multiRail threshhold\n"
        "        -v, --RndvThreshold,          Perf case only supports an RNDV threshold of less than 1048576, actual "
                                               "scenario requires a value less than UINT32_MAX\n"
        "        -u, --ubcMode,                UB-C mode, 0 means LowLatency, other value means HighBandwidth\n"
        "        -S, --splitSendThreshold,     the threshold of split send, UINT32_MAX by default\n";


    int ret = 0;
    int index = 0;

    std::string str = "d:i:p:t:s:e:m:o:c:r:R:v:u:S:";
    while ((ret = getopt_long(argc, argv, str.c_str(), options, &index)) != -1) {
        switch (ret) {
            case 'd':
                driverType = static_cast<UBSHcomNetDriverProtocol>((uint16_t)strtoul(optarg, NULL, 0));
                if (driverType > UBC) {
                    printf("invalid driver type %d", driverType);
                    return -1;
                }
                break;
            case 'i':
                oobIp = optarg;
                ipSeg = oobIp + "/24";
                break;
            case 'p':
                oobPort = (uint16_t)strtoul(optarg, NULL, 0);
                break;
            case 't':
                pingCount = (int32_t)strtoul(optarg, NULL, 0);
                break;
            case 's':
                dataSize = (int32_t)strtoul(optarg, NULL, 0);
                break;
            case 'e':
                epSize = (int32_t)strtoul(optarg, NULL, 0);
                break;
            case 'm':
                mode = (uint64_t)strtoul(optarg, NULL, 0);
                break;
            case 'o':
                periodThreadCnt = (uint64_t)strtoul(optarg, NULL, 0);
                break;
            case 'c':
                asyncWorkerCpuId = strtoul(optarg, nullptr, 0);
                break;
            case 'r':
                multiRailEnable = (uint32_t)strtoul(optarg, nullptr, 0);
                break;
            case 'R':
                multiRailThresh = (uint32_t)strtoul(optarg, nullptr, 0);
                break;
            case 'v':
                rndvThreshold = (uint32_t)strtoul(optarg, NULL, 0);
                break;
            case 'u':
                mUbcMode = std::stoi(optarg) ? UBSHcomUbcMode::HighBandwidth : UBSHcomUbcMode::LowLatency;
                break;
            case 'S':
                splitSendThreshold = (uint32_t)strtoul(optarg, nullptr, 0);
                break;
        }
    }

    Run();
    return 0;
}
```

2.  使用以下命令运行代码，启动Client端。

`./*pp_service_client_simple* -d 1 -i 127.0.0.1 -p 9980`

*pp_service_client_simple*：编译后可执行文件名，请根据实际情况进行修改。

- -d：配置Driver类型。

  1.  0：RDMA

  2.  1：TCP

  3.  2：UDS

  4.  3：SHM

  5.  7：UBC

- -i：Server IP地址。

- -p: 监听的端口

- 其余参数可以根据实际情况自由配置

## 传输层

### 说明

本章节将通过基础示例来演示如何使用UBS Comm，开发者可以通过学习此指导来快速上手UBS Comm。UBS Comm向开发者提供了传输层和服务层，因此使用指导也将分别提供一个示例代码来演示如何使用传输层和服务层。

### 服务端

1.  使用NetDriver::Instance创建一个Driver的对象。

`UBSHcomNetDriver \*driver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "server1", true);`

此处创建了一个使用RDMA协议的名为server1的服务端Driver。true代表启动监听线程，可以接受其他Driver对象的建链请求。

1.  设置NetDriverOptions选项，使用Driver对象注册回调函数，并用Driver的OobIpAndPort方法设置需要侦听的IP地址和端口。

```cpp
UBSHcomNetDriverOptions options {}; 
 
driver->RegisterNewEPHandler(std::bind(&NewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)); 
driver->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1)); 
driver->RegisterNewReqHandler(std::bind(&RequestReceived, std::placeholders::_1)); 
driver->RegisterReqPostedHandler(std::bind(&RequestPosted, std::placeholders::_1)); 
driver->RegisterOneSideDoneHandler(std::bind(&OneSideDone, std::placeholders::_1)); 
 
driver->OobIpAndPort(oobIp, oobPort);
```

- NetDriverOptions的参数，详情请参见《UBS-Comm-API-Spec.md》的“UBSHcomNetDriver::Initialize”章节。

- 注册回调函数，详情请参见《UBS-Comm-API-Spec.md》的“UBSHcomNetDriver::RegisterTLSCaCallback”章节和“TLSEraseKeypass函数类型”章节。

- OobIpAndPort用来设置需要侦听的IP地址和端口。

  1.  使用设置好的NetDriverOptions选项作为参数来调用Driver的Initialize方法，然后调用Driver的Start方法，完成服务端的启动。

```CPP
driver->Initialize(options); 
driver->Start();
```

----结束

### 客户端

1.  使用NetDriver::Instance创建一个Driver的对象。

`UBSHcomNetDriver *driver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "client1", false);`

第三个参数可以为false，因为客户端通常不需要被建链，无需启动监听线程。

1.  设置NetDriverOptions选项，使用Driver对象注册回调函数，并用Driver的OobIpAndPort方法设置需要建立连接的IP地址和端口。若不启动监听线程，则RegisterNewEPHandler可以不注册，但其它四个回调函数依旧需要注册。

```
UBSHcomNetDriverOptions options {}; 
 
driver->RegisterNewEPHandler(std::bind(&NewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)); 
driver->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1)); 
driver->RegisterNewReqHandler(std::bind(&RequestReceived, std::placeholders::_1)); 
driver->RegisterReqPostedHandler(std::bind(&RequestPosted, std::placeholders::_1)); 
driver->RegisterOneSideDoneHandler(std::bind(&OneSideDone, std::placeholders::_1)); 
 
driver->OobIpAndPort(oobIp, oobPort);
```



2.  使用设置好的选项NetDriverOptions作为参数来调用Driver的Initialize方法，然后调用Driver的Start方法，完成客户端的启动。

```
driver->Initialize(options); 
driver->Start();
```



----结束

### 服务端和客户端启动后

1.  当服务端和客户端都完成启动后，客户端的Driver可以调用Connect方法来连接服务端。

driver-\>Connect("hello world", ep, 0);

- "hello world"：连接时本端发送给对端的一条消息，对端可以在NewEndpoint回调函数的第三个参数中获取该消息。

- ep：Connect函数的返回值，即为得到链路的本端，服务端的NetEndpoint可以在NewEndpoint回调函数的第二个参数中获得。

- 0：链路类型。

&nbsp;

- 0：表示异步NetEndpoint。

- 1：表示同步NetEndpoint。

- 2：代表在RDMA协议中，同步'EventPoll的NetEndpoint。

  1.  连接完成后，客户端和服务端都会得到一个NetEndpoint对象，服务端和客户端都可以使用该对象来调用各种消息发送接口向对端发送消息。

UBSHcomNetTransRequest req((void \*)(data), sizeof(data), 0);  
ep-\>PostSend(1, req);

此处仅以PostSend为例，更多消息发送接口，请参见《UBS-Comm-API-Spec.md》的“UBSHcomNetEndpoint::PostSend”章节、“UBSHcomNetEndpoint::WaitCompletion”章节“UBSHcomNetEndpoint::PostSendRaw”章节和“UBSHcomNetEndpoint::ReceiveRawSgl”章节。

- 1：用户指定的opCode，取值范围0 ~ 1023。

- req：需要发送内容的结构体，结构体中的data为发送消息体。

----结束

### 传输层编程

此示例仅限帮助开发者具象化理解如何使用UBS Comm，作为实际使用场景的参考，请勿直接复制使用。

1.  Sever端示例

以下为传输层Server端的完整示例代码。

1.  当Server端收到Client端的消息时，会调用初始化时注册的回调函数RequestReceived，可以在回调函数中给Client端回复消息。

```
#include <unistd.h> 
#include <getopt.h> 
#include "hcom_service.h" 
 
using namespace ock::hcom; 
 
UBSHcomNetDriver *driver = nullptr; 
UBSHcomNetEndpointPtr ep = nullptr; 
using TestRegMrInfo = struct _reg_sgl_info_test_ { 
    uintptr_t lAddress = 0; 
    uint32_t lKey = 0; 
    uint32_t size = 0; 
} __attribute__((packed)); 
TestRegMrInfo localMrInfo[4]; 
TestRegMrInfo remoteMrInfo[4]; 
 
std::string ipSeg = "192.168.100.0/24"; 
std::string oobIp = ""; 
uint16_t oobPort = 9980; 
int16_t asyncWorkerCpuId = -1; 
 
UBSHcomNetDriverProtocol driverType = RDMA; 
std::string udsName = "SHM_UDS"; 
int32_t dataSize = 1024; 
int32_t workerMode = 0; 
void *data = nullptr; 
 
int NewEndPoint(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEP, const std::string &payload) 
{ 
    NN_LOG_INFO("new endpoint from " << ipPort << " payload " << payload << " id " << newEP->Id()); 
    ep = newEP; 
    return 0; 
} 
 
void EndPointBroken(const UBSHcomNetEndpointPtr &netEp) 
{ 
    NN_LOG_INFO("end point " << netEp->Id() << " is broken"); 
    if (ep != nullptr && netEp->Id() == ep->Id()) { 
        ep.Set(nullptr); 
    } 
} 
 
UBSHcomNetTransSgeIov iovPtr[4]; 
int RequestReceived(const UBSHcomNetRequestContext &ctx) 
{ 
    int result = 0; 
    if (driverType == 1 || driverType == 2) { 
        if ((ctx.Header().opCode == 0) && (ctx.Header().flags == NTH_TWO_SIDE) && (ctx.Header().immData == 0)) { 
            goto postSend1; 
        } else if ((ctx.Header().opCode == 1) && (ctx.Header().flags == NTH_TWO_SIDE) && (ctx.Header().immData == 0)) { 
            goto postSend2; 
        } else if ((ctx.Header().seqNo == 1) && (ctx.Header().flags == NTH_TWO_SIDE) && (ctx.Header().immData == 1)) { 
            goto PostSendRaw; 
        } else if ((ctx.Header().seqNo == 2) && (ctx.Header().flags == NTH_TWO_SIDE_SGL)) { 
            goto PostSendRawSgl; 
        } 
    } 
    if (ctx.Header().opCode == 0) { 
    postSend1: 
        UBSHcomNetTransRequest rsp((void *)(localMrInfo), sizeof(localMrInfo), 0); 
        if ((result = ep->PostSend(0, rsp)) != 0) { 
            NN_LOG_ERROR("failed to post message to data to server, result " << result); 
            return result; 
        } 
        return 0; 
    } else if (ctx.Header().opCode == 1) { 
    postSend2: 
        UBSHcomNetTransRequest req(data, dataSize, 0); 
        if ((result = ep->PostSend(1, req)) != 0) { 
            NN_LOG_ERROR("failed to post message to data to server, result " << result); 
            return result; 
        } 
        return 0; 
    }else if (ctx.Header().seqNo == 1) { 
    PostSendRaw: 
        UBSHcomNetTransRequest req(data, dataSize, 0); 
        if ((result = ep->PostSendRaw(req, ctx.Header().seqNo)) != 0) { 
            NN_LOG_ERROR("failed to post message to data to server, result " << result); 
            return result; 
        } 
        return 0; 
    } else if (ctx.Header().seqNo == 2) { 
    PostSendRawSgl: 
        UBSHcomNetTransSglRequest req(iovPtr, 4, 0); 
        if ((result = ep->PostSendRawSgl(req, ctx.Header().seqNo)) != 0) { 
            NN_LOG_ERROR("failed to post message to data to server, result " << result); 
            return result; 
        } 
        return 0; 
    } 
 
    return 0; 
} 
 
int RequestPosted(const UBSHcomNetRequestContext &ctx) 
{ 
    if (ctx.Result() != NN_OK) { 
        NN_LOG_ERROR("Post send err"); 
    } 
    NN_LOG_TRACE_INFO("RequestPosted"); 
    return 0; 
} 
 
int OneSideDone(const UBSHcomNetRequestContext &ctx) 
{ 
    NN_LOG_INFO("one side done"); 
    return 0; 
} 
 
bool CreateDriver() 
{ 
    if (driver != nullptr) { 
        NN_LOG_ERROR("driver already created"); 
        return false; 
    } 
    driver = UBSHcomNetDriver::Instance(driverType, "pp_transport_server", true); 
    if (driver == nullptr) { 
        NN_LOG_ERROR("failed to create driver already created"); 
        return false; 
    } 
 
    UBSHcomNetDriverOptions options{}; 
    options.mode = static_cast<UBSHcomNetDriverWorkingMode>(workerMode); 
    options.mrSendReceiveSegSize = dataSize * 4 + 32; 
    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask); 
    if (asyncWorkerCpuId != -1) { 
        std::string str = std::to_string(asyncWorkerCpuId) + "-" + std::to_string(asyncWorkerCpuId); 
        options.SetWorkerGroupsCpuSet(str); 
        NN_LOG_INFO("set cpuId " << options.WorkerGroupCpus()); 
    } 
 
    if (driverType == ock::hcom::SHM) { 
        options.oobType = NET_OOB_UDS; 
        UBSHcomNetOobUDSListenerOptions listenOpt; 
        listenOpt.Name(udsName); 
        listenOpt.perm = 0; 
        driver->AddOobUdsOptions(listenOpt); 
    } 
 
    options.SetNetDeviceIpMask(ipSeg); 
    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask); 
    driver->OobIpAndPort(oobIp, oobPort); 
 
    driver->RegisterNewEPHandler( 
        std::bind(&NewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)); 
    driver->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1)); 
    driver->RegisterNewReqHandler(std::bind(&RequestReceived, std::placeholders::_1)); 
    driver->RegisterReqPostedHandler(std::bind(&RequestPosted, std::placeholders::_1)); 
    driver->RegisterOneSideDoneHandler(std::bind(&OneSideDone, std::placeholders::_1)); 
 
    int result = 0; 
    if ((result = driver->Initialize(options)) != 0) { 
        NN_LOG_ERROR("failed to initialize driver " << result); 
        return false; 
    } 
    NN_LOG_INFO("driver initialized"); 
 
    if ((result = driver->Start()) != 0) { 
        NN_LOG_ERROR("failed to start driver " << result); 
        return false; 
    } 
    NN_LOG_INFO("driver started"); 
 
    return true; 
} 
 
bool RegSglMem() 
{ 
    // write read 
    for (uint16_t i = 0; i < 4; i++) { 
        UBSHcomNetMemoryRegionPtr mr; 
        auto result = driver->CreateMemoryRegion(dataSize, mr); 
        if (result != NN_OK) { 
            NN_LOG_ERROR("reg mr failed"); 
            return false; 
        } 
        localMrInfo[i].lAddress = mr->GetAddress(); 
        localMrInfo[i].lKey = mr->GetLKey(); 
        localMrInfo[i].size = dataSize; 
        memset(reinterpret_cast<void *>(localMrInfo[i].lAddress), 0, dataSize); 
    } 
 
    // sendsgl 
    for (uint16_t i = 0; i < 4; i++) { 
        UBSHcomNetMemoryRegionPtr mr; 
        auto result = driver->CreateMemoryRegion(dataSize, mr); 
        if (result != NN_OK) { 
            NN_LOG_ERROR("reg mr failed"); 
            return false; 
        } 
 
        iovPtr[i].lAddress = mr->GetAddress(); 
        iovPtr[i].lKey = mr->GetLKey(); 
        iovPtr[i].size = dataSize; 
        memset(reinterpret_cast<void *>(iovPtr[i].lAddress), 0, dataSize); 
    } 
 
    return true; 
} 
 
void SendRequest() 
{ 
    NN_LOG_INFO("input q means quit."); 
    while (true) { 
        auto tmpChar = getchar(); 
        switch (tmpChar) { 
            case 'q': 
                return; 
            default: 
                NN_LOG_INFO("input q means quit."); 
                continue; 
        } 
    } 
} 
 
void Run() 
{ 
    if (!CreateDriver()) { 
        return; 
    } 
 
    if (!RegSglMem()) { 
        return; 
    } 
 
    SendRequest(); 
} 
 
int main(int argc, char *argv[]) 
{ 
    struct option options[] = { 
        {"driver", required_argument, NULL, 'd'}, 
        {"ip", required_argument, NULL, 'i'}, 
        {"port", required_argument, NULL, 'p'}, 
        {"size", required_argument, NULL, 's'}, 
        {"worker Mode", required_argument, NULL, 'w'}, 
        {"worker Num", required_argument, NULL, 'n'}, 
        {"cpuId", required_argument, NULL, 'c'}, 
        {NULL, 0, NULL, 0}, 
    }; 
 
    const char *usage = "usage\n" 
        "        -d, --driver,                 driver type, 0 means rdma, 1 means tcp, 2 means uds, 3 means shm\n" 
        "        -i, --ip,                     server ip mask, e.g. 10.175.118.1\n" 
        "        -p, --port,                   server port, by default 9980\n" 
        "        -s, --io size ,               max data size\n" 
        "        -w, --worker mode,            0 means busy polling, 1 means event polling\n" 
        "        -c, --cpuId,                  async worker\n"; 
 
    int ret = 0; 
    int index = 0; 
 
    if (argc != 13) { 
        printf("invalid param, %s, for example %s -d 0 -i rdma_nic_ip -p 9980 -s 1024 -w 0 -c 5\n", usage, argv[0]); 
        return -1; 
    } 
 
    std::string str = "d:i:p:s:w:c:"; 
    while ((ret = getopt_long(argc, argv, str.c_str(), options, &index)) != -1) { 
        switch (ret) { 
            case 'd': 
                driverType = static_cast<UBSHcomNetDriverProtocol>((uint16_t)strtoul(optarg, NULL, 0)); 
                if (driverType > UBC) { 
                    printf("invalid driver type %d", driverType); 
                    return -1; 
                } 
                break; 
            case 'i': 
                oobIp = optarg; 
                ipSeg = oobIp + "/24"; 
                break; 
            case 'p': 
                oobPort = (uint16_t)strtoul(optarg, NULL, 0); 
                break; 
            case 's': 
                dataSize = (int32_t)strtoul(optarg, NULL, 0); 
                break; 
            case 'w': 
                workerMode = (int32_t)strtoul(optarg, NULL, 0); 
                break; 
            case 'c': 
                asyncWorkerCpuId = strtoul(optarg, nullptr, 0); 
                break; 
        } 
    } 
    data = malloc(dataSize); 
    Run(); 
    free(data); 
    return 0; 
}
```



1.  使用以下命令运行代码，启动Server端。

`./*pp_server* -i 127.0.0.1 -p 9980 -c -1`

- *pp_server*：编译后可执行文件名，请根据实际情况进行修改。

- -i：Server IP地址。

- -p：Server端口号。

- -c：Worker绑定CPU，-1表示不绑核。

----结束

1.  Client端示例

以下为传输层一个完整的Client端示例。

1.  初始化流程和Server端基本一致。入口为main函数，经过参数解析后，进入Run函数。

```
#include <unistd.h> 
#include <getopt.h> 
#include "hcom_service.h" 
#include "net_monotonic.h" 
#include <semaphore.h> 
#include <thread> 
 
using namespace ock::hcom; 
 
UBSHcomNetDriver *driver = nullptr; 
UBSHcomNetEndpointPtr ep = nullptr; 
std::string oobIp = ""; 
uint16_t oobPort = 9980; 
std::string ipSeg = "192.168.100.0/24"; 
std::string dumpStr = ""; 
std::string udsName = "SHM_UDS"; 
UBSHcomNetDriverProtocol driverType = RDMA; 
int32_t dataSize = 1024; 
int16_t asyncWorkerCpuId = -1; 
uint64_t mode = 0; 
uint32_t flags = 0; 
bool start = false; 
uint64_t startTime = 0; 
uint64_t finishTime = 0; 
 
using TestRegMrInfo = struct _reg_sgl_info_test_ { 
    uintptr_t lAddress = 0; 
    uint32_t lKey = 0; 
    uint32_t size = 0; 
} __attribute__((packed)); 
TestRegMrInfo localMrInfo[4]; 
TestRegMrInfo remoteMrInfo[4]; 
UBSHcomNetTransRequest iov[4]; 
int32_t pingCount = 100000; 
int32_t pingCount1 = 100000; 
int seqNo = 1; 
int workerMode = 0; 
sem_t sem; 
void* data = nullptr; 
void printPerf() 
{ 
    finishTime = MONOTONIC_TIME_NS(); 
    NN_LOG_INFO("Finished " << pingCount1 << " pingpong"<<" ,startTime:"<<startTime<<" ,finishTime: "<<finishTime); 
    printf("\tPerf summary\n"); 
    printf("\tPingpong times:\t\t%d\n", pingCount1); 
    printf("\tTotal time(us):\t\t%f\n", (finishTime - startTime) / 1000.0); 
    printf("\tTotal time(ms):\t\t%f\n", (finishTime - startTime) / 1000000.0); 
    printf("\tTotal time(s):\t\t%f\n", (finishTime - startTime) / 1000000000.0); 
    printf("\tLatency(us):\t\t%f\n", (finishTime - startTime) / pingCount1 / 1000.0); 
    printf("\tOps:\t\t\t%f pp/s\n", (pingCount1 * 1000000000.0) / (finishTime - startTime)); 
} 
 
void EndPointBroken(const UBSHcomNetEndpointPtr &ep1) 
{ 
    NN_LOG_INFO("end point " << ep1->Id() << " broken"); 
    if (ep != nullptr) { 
        ep.Set(nullptr); 
    } 
} 
 
 
void SendRequest() 
{ 
    int result = 0; 
    UBSHcomNetTransRequest req(data, dataSize, 0); 
 
    if (pingCount-- == 0) { 
        printPerf(); 
        sem_post(&sem); 
        return; 
    } 
    if ((result = ep->PostSend(1, req)) != 0) { 
        NN_LOG_ERROR("failed to post message to data to server, result " << result); 
        return; 
    } 
} 
 
void SyncSendRequest() 
{ 
    int result = 0; 
    UBSHcomNetTransRequest req(data, dataSize, 0); 
    UBSHcomNetResponseContext respCtx{}; 
    startTime = MONOTONIC_TIME_NS(); 
 
    uint32_t count = 0; 
    for (int32_t i = 0; i < pingCount; i++) { 
        count++; 
        if ((result = ep->PostSend(1, req)) != 0) { 
            if (result == 314) { 
                NN_LOG_ERROR("post message to data to server successfully,but fail to post message to client"); 
                return; 
            } 
            NN_LOG_ERROR("failed to post message to data to server"); 
            break; 
        } 
        if ((result = ep->WaitCompletion(2)) != 0) { 
            NN_LOG_ERROR("failed to get WaitCompletion, result " << result); 
            break; 
        } 
 
        if ((result = ep->Receive(2, respCtx)) != 0) { 
            NN_LOG_ERROR("failed to get response, result " << result); 
            break; 
        } 
    } 
    printPerf(); 
    sem_post(&sem); 
    return; 
} 
 
void SendRawRequest() 
{ 
    int result = 0; 
    UBSHcomNetTransRequest req(data, dataSize, 0); 
 
    if (pingCount-- == 0) { 
        printPerf(); 
        sem_post(&sem); 
        return; 
    } 
    if ((result = ep->PostSendRaw(req, 1)) != 0) { 
        NN_LOG_INFO("failed to post message to data to server, result " << result); 
        return; 
    } 
} 
 
void SyncSendRawRequest() 
{ 
    int result = 0; 
    UBSHcomNetTransRequest req(data, dataSize, 0); 
    UBSHcomNetResponseContext respCtx{}; 
    startTime = MONOTONIC_TIME_NS(); 
 
    for (int32_t i = 0; i < pingCount; i++) { 
        if ((result = ep->PostSendRaw(req, 1)) != 0) { 
            if (result == 314) { 
                NN_LOG_ERROR("post message to data to server successfully,but fail to post message to client"); 
                return; 
            } 
            NN_LOG_ERROR("failed to post message to data to server"); 
            break; 
        } 
        if ((result = ep->WaitCompletion(2)) != 0) { 
            NN_LOG_ERROR("failed to get WaitCompletion, result " << result); 
            break; 
        } 
 
        if ((result = ep->ReceiveRaw(2, respCtx)) != 0) { 
            NN_LOG_ERROR("failed to get response, result " << result); 
            break; 
        } 
    } 
    printPerf(); 
    sem_post(&sem); 
    return; 
} 
UBSHcomNetTransSgeIov iovPtr[4]; 
void SendRawSglRequest() 
{ 
    int result = 0; 
    UBSHcomNetTransSglRequest req(iovPtr, 4, 0); 
 
    if (pingCount-- == 0) { 
        printPerf(); 
        sem_post(&sem); 
        return; 
    } 
    if ((result = ep->PostSendRawSgl(req, 2)) != 0) { 
        NN_LOG_INFO("failed to post message to data to server, result " << result); 
        return; 
    } 
} 
 
void SyncSendRawSglRequest() 
{ 
    int result = 0; 
    UBSHcomNetTransSglRequest req(iovPtr, 4, 0); 
    UBSHcomNetResponseContext respCtx{}; 
    startTime = MONOTONIC_TIME_NS(); 
 
    for (int32_t i = 0; i < pingCount1; i++) { 
        if ((result = ep->PostSendRawSgl(req, 2)) != 0) { 
            if (result == 314) { 
                NN_LOG_ERROR("post message to data to server successfully,but fail to post message to client"); 
                return; 
            } 
            NN_LOG_ERROR("failed to post message to data to server"); 
            break; 
        } 
        if ((result = ep->WaitCompletion(2)) != 0) { 
            NN_LOG_ERROR("failed to get WaitCompletion, result " << result); 
            break; 
        } 
 
        if ((result = ep->ReceiveRawSgl(respCtx)) != 0) { 
            NN_LOG_ERROR("failed to get response, result " << result); 
            break; 
        } 
    } 
    printPerf(); 
    sem_post(&sem); 
    return; 
} 
 
void ReadRequest() 
{ 
    for (int32_t i = 0; i < pingCount1; i++) { 
        if (ep->PostRead(iov[0]) != 0) { 
            NN_LOG_ERROR("failed to read data from server"); 
            return; 
        } 
    } 
} 
 
void SyncReadRequest() 
{ 
    for (int32_t i = 0; i < pingCount1; i++) { 
        if (ep->PostRead(iov[0]) != 0) { 
            NN_LOG_ERROR("failed to read data from server"); 
            return; 
        } 
        if (ep->WaitCompletion(-1) != 0) { 
            NN_LOG_ERROR("failed to read data from server"); 
            return; 
        } 
    } 
    printPerf(); 
    sem_post(&sem); 
    return; 
} 
 
void ReadSglRequest() 
{ 
    UBSHcomNetTransSgeIov segIov[4]; 
    for (uint16_t i = 0; i < 4; i++) { 
        segIov[i].lAddress = iov[i].lAddress; 
        segIov[i].rAddress = iov[i].rAddress; 
        segIov[i].lKey = iov[i].lKey; 
        segIov[i].rKey = iov[i].rKey; 
        segIov[i].size = iov[i].size; 
    } 
    UBSHcomNetTransSglRequest reqRead(segIov, 4, 0); 
    for (int i = 0; i < pingCount1; ++i) { 
        if (ep->PostRead(reqRead) != 0) { 
            NN_LOG_ERROR("failed to read data from server"); 
            return; 
        } 
    } 
} 
 
void SyncReadSglRequest() 
{ 
    UBSHcomNetTransSgeIov segIov[4]; 
    for (uint16_t i = 0; i < 4; i++) { 
        segIov[i].lAddress = iov[i].lAddress; 
        segIov[i].rAddress = iov[i].rAddress; 
        segIov[i].lKey = iov[i].lKey; 
        segIov[i].rKey = iov[i].rKey; 
        segIov[i].size = iov[i].size; 
    } 
    UBSHcomNetTransSglRequest reqRead(segIov, 4, 0); 
    for (int32_t i = 0; i < pingCount1; i++) { 
        if (ep->PostRead(reqRead) != 0) { 
            NN_LOG_ERROR("failed to read data from server"); 
            return; 
        } 
        if (ep->WaitCompletion(-1) != 0) { 
            NN_LOG_ERROR("failed to read data from server"); 
            return; 
        } 
    } 
    printPerf(); 
    sem_post(&sem); 
    return; 
} 
 
void WriteRequest() 
{ 
    for (int32_t i = 0; i < pingCount1; i++) { 
        if (ep->PostWrite(iov[0]) != 0) { 
            NN_LOG_ERROR("failed to read data from server"); 
            return; 
        } 
    } 
} 
 
void SyncWriteRequest() 
{ 
    for (int32_t i = 0; i < pingCount1; i++) { 
        if (ep->PostWrite(iov[0]) != 0) { 
            NN_LOG_ERROR("failed to read data from server"); 
            return; 
        } 
        if (ep->WaitCompletion(-1) != 0) { 
            NN_LOG_ERROR("failed to read data from server"); 
            return; 
        } 
    } 
    printPerf(); 
    sem_post(&sem); 
    return; 
} 
 
void WriteSglRequest() 
{ 
    UBSHcomNetTransSgeIov segIov[4]; 
    for (uint16_t i = 0; i < 4; i++) { 
        segIov[i].lAddress = iov[i].lAddress; 
        segIov[i].rAddress = iov[i].rAddress; 
        segIov[i].lKey = iov[i].lKey; 
        segIov[i].rKey = iov[i].rKey; 
        segIov[i].size = iov[i].size; 
    } 
    UBSHcomNetTransSglRequest reqRead(segIov, 4, 0); 
    for (int i = 0; i < pingCount1; ++i) { 
        if (ep->PostWrite(reqRead) != 0) { 
            NN_LOG_ERROR("failed to read data from server"); 
            return; 
        } 
    } 
} 
 
void SyncWriteSglRequest() 
{ 
    UBSHcomNetTransSgeIov segIov[4]; 
    for (uint16_t i = 0; i < 4; i++) { 
        segIov[i].lAddress = iov[i].lAddress; 
        segIov[i].rAddress = iov[i].rAddress; 
        segIov[i].lKey = iov[i].lKey; 
        segIov[i].rKey = iov[i].rKey; 
        segIov[i].size = iov[i].size; 
    } 
    UBSHcomNetTransSglRequest reqRead(segIov, 4, 0); 
    for (int32_t i = 0; i < pingCount1; i++) { 
        if (ep->PostWrite(reqRead) != 0) { 
            NN_LOG_ERROR("failed to read data from server"); 
            return; 
        } 
        if (ep->WaitCompletion(-1) != 0) { 
            NN_LOG_ERROR("failed to read data from server"); 
            return; 
        } 
    } 
    printPerf(); 
    sem_post(&sem); 
    return; 
} 
 
int RequestReceived(const UBSHcomNetRequestContext &ctx) 
{ 
    if (driverType == 1 || driverType == 2) { 
        if ((ctx.Header().opCode == 0) && (ctx.Header().flags == NTH_TWO_SIDE) && (ctx.Header().immData == 0)) { 
            goto postSend1; 
        } else if ((ctx.Header().opCode == 1) && (ctx.Header().flags == NTH_TWO_SIDE) && (ctx.Header().immData == 0)) { 
            goto postSend2; 
        } else if ((ctx.Header().seqNo == 1) && (ctx.Header().flags == NTH_TWO_SIDE) && (ctx.Header().immData == 1)) { 
            goto PostSendRaw; 
        } else if ((ctx.Header().seqNo == 2) && (ctx.Header().flags == NTH_TWO_SIDE_SGL)) { 
            goto PostSendRawSgl; 
        } 
    } 
 
    if (ctx.Header().opCode == 0) { 
    postSend1: 
        memcpy(remoteMrInfo, ctx.Message()->Data(), ctx.Message()->DataLen()); 
        sem_post(&sem); 
        return 0; 
    }else if (ctx.Header().opCode == 1) { 
    postSend2: 
        SendRequest(); 
        return 0; 
    }else if (ctx.Header().seqNo == 1) { 
    PostSendRaw: 
        SendRawRequest(); 
        return 0; 
    } else if (ctx.Header().seqNo == 2) { 
    PostSendRawSgl: 
        SendRawSglRequest(); 
    } 
    return 0; 
} 
 
int RequestPosted(const UBSHcomNetRequestContext &ctx) 
{ 
    return 0; 
} 
 
int OneSideDone(const UBSHcomNetRequestContext &ctx) 
{ 
    if (--pingCount == 0) { 
        printPerf(); 
        sem_post(&sem); 
    } 
    return 0; 
} 
 
void exitFunc() 
{ 
    driver->Stop(); 
    driver->UnInitialize(); 
} 
 
bool CreateDriver() 
{ 
    if (driver != nullptr) { 
        NN_LOG_ERROR("driver already created"); 
        return false; 
    } 
 
    driver = UBSHcomNetDriver::Instance(driverType, "transport_pp_client", false); 
    if (driver == nullptr) { 
        NN_LOG_ERROR("failed to create driver already created"); 
        return false; 
    } 
 
    atexit(exitFunc); 
    UBSHcomNetDriverOptions options{}; 
    options.mode = static_cast<UBSHcomNetDriverWorkingMode>(workerMode); 
    options.mrSendReceiveSegSize = dataSize * 4 + 32; 
    options.mrSendReceiveSegCount = 10; 
    if (mode == 1) { 
        options.dontStartWorkers = true; 
    } 
    if (driverType == SHM) { 
        options.oobType = NET_OOB_UDS; 
    } 
    if (asyncWorkerCpuId != -1) { 
        std::string str = std::to_string(asyncWorkerCpuId) + "-" + std::to_string(asyncWorkerCpuId); 
        options.SetWorkerGroupsCpuSet(str); 
        NN_LOG_INFO(" set cpuId: " << options.WorkerGroupCpus()); 
    } 
    options.SetNetDeviceIpMask(ipSeg); 
    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask); 
 
    driver->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1)); 
    driver->RegisterNewReqHandler(std::bind(&RequestReceived, std::placeholders::_1)); 
    driver->RegisterReqPostedHandler(std::bind(&RequestPosted, std::placeholders::_1)); 
    driver->RegisterOneSideDoneHandler(std::bind(&OneSideDone, std::placeholders::_1)); 
 
    driver->OobIpAndPort(oobIp, oobPort); 
    int result = 0; 
    if ((result = driver->Initialize(options)) != 0) { 
        NN_LOG_ERROR("failed to initialize driver " << result); 
        return false; 
    } 
    NN_LOG_INFO("driver initialized"); 
 
    if ((result = driver->Start()) != 0) { 
        NN_LOG_ERROR("failed to start driver " << result); 
        return false; 
    } 
    NN_LOG_INFO("driver started"); 
    sem_init(&sem, 0, 0); 
    return true; 
} 
 
bool Connect() 
{ 
    if (driver == nullptr) { 
        NN_LOG_ERROR("driver is null"); 
        return false; 
    } 
 
    int result = 0; 
    if (mode == 1) { 
        flags = NET_EP_SELF_POLLING; 
    } 
 
    if (driverType == SHM) { 
        result = driver->Connect(udsName, 0, "hello server", ep, flags); 
    } else { 
        result = driver->Connect(oobIp, oobPort, "hello server", ep, flags); 
    } 
 
    if (result != 0) { 
        NN_LOG_ERROR("failed to connect to server, result " << result); 
        return false; 
    } 
 
    NN_LOG_INFO("success to connect to server, ep id " << ep->Id()); 
    return true; 
} 
 
int userChar = 0; 
int startTime1=0; 
void RunInThread() 
{ 
    while (!start) { 
        usleep(1); 
    } 
    pingCount = pingCount1; 
    startTime1 = MONOTONIC_TIME_NS(); 
    switch (userChar) { 
        case '0': 
            NN_LOG_INFO("Wait for finish, Type post send:"); 
            startTime = MONOTONIC_TIME_NS(); 
            NN_LOG_INFO("******startTime: "<<startTime<<"****"<<startTime-startTime1); 
            mode == 0 ? SendRequest() : SyncSendRequest(); 
            sem_wait(&sem); 
            break; 
        case '1': 
            NN_LOG_INFO("Wait for finish, Type post send raw:"); 
            startTime = MONOTONIC_TIME_NS(); 
            mode == 0 ? SendRawRequest() : SyncSendRawRequest(); 
            sem_wait(&sem); 
            break; 
        case '2': 
            NN_LOG_INFO("Wait for finish, Type post send raw Sgl:"); 
            startTime = MONOTONIC_TIME_NS(); 
            mode == 0 ? SendRawSglRequest() : SyncSendRawSglRequest(); 
            sem_wait(&sem); 
            break; 
        case '3': 
            NN_LOG_INFO("Wait for finish, Type read:"); 
            startTime = MONOTONIC_TIME_NS(); 
            mode == 0 ? ReadRequest() : SyncReadRequest(); 
            sem_wait(&sem); 
            break; 
        case '4': 
            NN_LOG_INFO("Wait for finish, Type read sgl:"); 
            startTime = MONOTONIC_TIME_NS(); 
            mode == 0 ? ReadSglRequest() : SyncReadSglRequest(); 
            sem_wait(&sem); 
            break; 
        case '5': 
            NN_LOG_INFO("Wait for finish, Type write:"); 
            startTime = MONOTONIC_TIME_NS(); 
            mode == 0 ? WriteRequest() : SyncWriteRequest(); 
            sem_wait(&sem); 
            break; 
        case '6': 
            NN_LOG_INFO("Wait for finish, Type write sgl:"); 
            startTime = MONOTONIC_TIME_NS(); 
            mode == 0 ? WriteSglRequest() : SyncWriteSglRequest(); 
            sem_wait(&sem); 
            break; 
        default: 
            return; 
    } 
} 
 
void Test() 
{ 
    NN_LOG_INFO("input 0:send, 1:send raw, 2:send raw sgl, 3:write, 4:write sgl, 5:read, 6:read" 
        "sgl, q mean quit, c means close"); 
    while (true) { 
        userChar = getchar(); 
        start = false; 
        std::thread threads; 
        threads = std::thread(RunInThread); 
        start = true; 
        threads.join(); 
 
        switch (userChar) { 
            case '0': 
                break; 
            case '1': 
                break; 
            case '2': 
                break; 
            case '3': 
                break; 
            case '4': 
                break; 
            case '5': 
                break; 
            case '6': 
                break; 
            case 'q': 
                return; 
            case 'c': 
                printf("\tOperate close\n"); 
                ep->Close(); 
                break; 
            default: 
                NN_LOG_INFO("input 0:send, 1:send raw, 2:send raw sgl, 3:read, 4:read sgl, 5:write " 
                    "6:write sgl, q mean quit, c ep close"); 
                continue; 
        } 
 
        if (userChar == 'c') { 
            continue; 
        } 
    } 
} 
 
bool GetRemoteMr() 
{ 
    int result = 0; 
    std::string value = "hello world"; 
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0); 
    UBSHcomNetResponseContext respCtx{}; 
    if ((result = ep->PostSend(0, req)) != 0) { 
        NN_LOG_INFO("failed to post message to data to server"); 
        return false; 
    } 
    if (mode == 1) { 
        if ((result = ep->WaitCompletion(2)) != 0) { 
            NN_LOG_ERROR("failed to get WaitCompletion, result " << result); 
            return false; 
        } 
 
        if ((result = ep->Receive(2, respCtx)) != 0) { 
            NN_LOG_ERROR("failed to get response, result " << result); 
            return false; 
        } 
        memcpy(remoteMrInfo, respCtx.Message()->Data(), respCtx.Message()->DataLen()); 
        sem_post(&sem); 
    } 
 
    return true; 
} 
 
bool RegSglMem() 
{ 
    // write read 
    for (uint16_t i = 0; i < 4; i++) { 
        UBSHcomNetMemoryRegionPtr mr; 
        auto result = driver->CreateMemoryRegion(dataSize, mr); 
        if (result != NN_OK) { 
            NN_LOG_ERROR("reg mr failed"); 
            return false; 
        } 
        localMrInfo[i].lAddress = mr->GetAddress(); 
        localMrInfo[i].lKey = mr->GetLKey(); 
        localMrInfo[i].size = dataSize; 
        memset(reinterpret_cast<void *>(localMrInfo[i].lAddress), 0, dataSize); 
    } 
 
    // sendsgl 
    for (uint16_t i = 0; i < 4; i++) { 
        UBSHcomNetMemoryRegionPtr mr; 
        auto result = driver->CreateMemoryRegion(dataSize, mr); 
        if (result != NN_OK) { 
            NN_LOG_ERROR("reg mr failed"); 
            return false; 
        } 
 
        iovPtr[i].lAddress = mr->GetAddress(); 
        iovPtr[i].lKey = mr->GetLKey(); 
        iovPtr[i].size = dataSize; 
        memset(reinterpret_cast<void *>(iovPtr[i].lAddress), 0, dataSize); 
    } 
 
    return true; 
} 
 
 
void Run() 
{ 
    if (!CreateDriver()) { 
        return; 
    } 
 
    if (!Connect()) { 
        return; 
    } 
    if (!RegSglMem()) { 
        return; 
    } 
    if (!GetRemoteMr()) { 
        return; 
    } 
    sem_wait(&sem); 
 
    for (int i = 0; i < 4; ++i) { 
        iov[i].lAddress = localMrInfo[i].lAddress; 
        iov[i].rAddress = remoteMrInfo[i].lAddress; 
        iov[i].lKey = localMrInfo[i].lKey; 
        iov[i].rKey = remoteMrInfo[i].lKey; 
        iov[i].size = localMrInfo[i].size; 
    } 
 
    Test(); 
} 
 
int main(int argc, char *argv[]) 
{ 
    struct option options[] = { 
        {"driver", required_argument, NULL, 'd'}, 
        {"ip", required_argument, NULL, 'i'}, 
        {"port", required_argument, NULL, 'p'}, 
        {"pingpongtimes", required_argument, NULL, 't'}, 
        {"size", required_argument, NULL, 's'}, 
        {"epMode", required_argument, NULL, 'm'}, 
        {"workerMode", required_argument, NULL, 'w'}, 
        {"cpuId", required_argument, NULL, 'c'}, 
        {NULL, 0, NULL, 0}, 
    }; 
 
    const char *usage = "usage\n" 
        "        -d, --driver,                 driver type, 0 means rdma, 1 means tcp, 2 means uds, 3 means shm\n" 
        "        -i, --ip,                     coord server ip mask, e.g. 10.175.118.1\n" 
        "        -p, --port,                   coord server port, by default 9980\n" 
        "        -t, --pingpongtimes,          ping pong times\n" 
        "        -s, --size,                   max data size\n" 
        "        -m, --ep mode,                0 means worker polling(Async), 1 means self polling(Sync)\n" 
        "        -w, --worker mode,            0 means busy polling, 1 means event polling()\n" 
        "        -c, --cpuId,                  async worker\n"; 
 
    int ret = 0; 
    int index = 0; 
 
    if (argc != 17) { 
        printf("invalid param, %s, for example %s -d 0 -i rdma_nic_ip -p 9980 -t 1000000 -s 1024 -m 0 -w 1 -c 5\n", 
            usage, argv[0]); 
        return -1; 
    } 
 
    std::string str = "d:i:p:t:s:m:w:c:"; 
    while ((ret = getopt_long(argc, argv, str.c_str(), options, &index)) != -1) { 
        switch (ret) { 
            case 'd': 
                driverType = static_cast<UBSHcomNetDriverProtocol>((uint16_t)strtoul(optarg, NULL, 0)); 
                if (driverType > UBC) { 
                    printf("invalid driver type %d", driverType); 
                    return -1; 
                } 
                break; 
            case 'i': 
                oobIp = optarg; 
                ipSeg = oobIp + "/24"; 
                break; 
            case 'p': 
                oobPort = (uint16_t)strtoul(optarg, NULL, 0); 
                break; 
            case 't': 
                pingCount = (int32_t)strtoul(optarg, NULL, 0); 
                break; 
            case 's': 
                dataSize = (int32_t)strtoul(optarg, NULL, 0); 
                break; 
            case 'm': 
                mode = (uint64_t)strtoul(optarg, NULL, 0); 
                break; 
            case 'w': 
                workerMode = (uint64_t)strtoul(optarg, NULL, 0); 
                break; 
            case 'c': 
                asyncWorkerCpuId = strtoul(optarg, nullptr, 0); 
                break; 
        } 
    } 
    data = malloc(dataSize); 
    pingCount1 = pingCount; 
    Run(); 
    free(data); 
    return 0; 
}
```

使用以下命令运行代码，启动Client端。

`./*pp_client* -i 127.0.0.1 -p 9980 -t 10000 -c -1`

- *pp_client*：编译后可执行文件名，请根据实际情况进行修改。

- -i：Server端IP地址，127.0.0.1。

- -p：Server端口号

- -t：Pingpong次数。

- -c：Worker绑定CPU，-1表示不绑核。

----结束

- 

# 安全管理

1.  推荐环境变量配置

环境变量配置，请参见《UBS-COMM-API-Spec》的“环境变量参考”章节。

2.  防病毒软件例行检查

定期开展对集群和UBS Comm组件的防病毒扫描是十分必要的，防病毒软件例行检查会帮助集群免受病毒、恶意代码、间谍软件以及恶意程序侵害，减少系统瘫痪、信息泄露等安全风险。可以使用业界主流的防病毒软件进行防病毒检查。

3.  漏洞修复

为保证环境安全，降低被攻击的风险，请开启防火墙，并定期修复以下漏洞。

- 操作系统漏洞

- rdma-core漏洞

- OpenSSL漏洞

- 其他相关组件漏洞

  1.  证书安全管理

- 需使用X509v3格式的证书，并使用安全的证书签名算法。

- 证书应设置合理的有效期，允许华为设备预置证书的有效期略长于产品生命周期。

- 证书的私钥要使用基于口令的加密机制保存，私钥保护口令应满足复杂度要求并加密保存，同时控制私钥文件和证书文件的访问权限。

- 必须验证对端证书的有效性，必须验证项包括对端证书是否由受信根CA签发、是否在有效期内、是否已被吊销。

- 使用安全随机数生成密钥对，且必须使用至少2048位，推荐使用3072位的RSA密钥对（第三方CA签发证书、与第三方系统对接、兼容老版本等场景可例外）。

- 在使用数字证书进行内层软件完整性保护时，必须防止用于验证软件完整性的根证书被篡改。

#### 安全申明

对于UB通信建链方式，做出如下安全申明：

- 推荐使用以太网卡+TCP方式建链，默认开启TLS安全认证

- 不依赖以太网卡的公知jetty建链方式当前不支持TLS安全认证，后续通过补丁版本支持IPoverURMA解决安全认证问题。

  1.  无属主文件安全加固

用户可以执行find / -nouser -nogroup命令，查找容器内或物理机上的无属主文件。根据文件的UID和GID创建相应的用户和用户组，或者修改已有用户的UID、用户组的GID来适配，赋予文件属主，避免无属主文件给系统带来安全隐患。

# UBS Comm库文件链接方法参考

UBS Comm以头文件和库文件的形式提供给开发者集成和使用，开发者可以根据自己的实际项目需要选择使用动态库或静态库。

\# 链接动态库  
`gcc -o \<输出文件名称\> \<被链接的文件\> -L\<动态库路径\> -lhcom -lstdc++ -I\<HCOM头文件目录\> ` 
\# 链接静态库  
`gcc -o \<输出文件名称\> \<被链接的文件\> -L\<静态库路径\> -lhcom_static -lm -lstdc++ -I\<HCOM头文件目录\>`

静态库在编译期就已经被链接到可执行文件中，无需像动态库一样在运行期加载，故执行效率更高。但静态库会增加可执行文件大小，多个程序同时使用同一静态库时，会造成存储资源浪费。另外库文件更新时，使用动态库场景可以仅更新动态库文件，使用静态库场景必须重新编译应用程序。

# 公网地址声明

以下表格中列出了当前产品中包含的公网地址，不涉及安全风险。

| 网址 | 说明 |
|----|----|
| https://gcc.gnu.org/bugs/ | 该网址为开源软件GCC编译引入，无安全风险。 |
| http://license.coscl.org.cn/MulanPSL2 | 该网址为版权声明license，无安全风险 |

# 术语和缩略语

| 缩略语 | 英文全称 | **说明** |
|----|----|----|
| CQ | Completion Queue | 完成队列。 |
| CRC | Cyclic Redundancy Code | 循环冗余码，一种线性检错码，通过多项式除法的余数来生成奇偶校验位。 |
| CNP | Congestion Notification Packet | 拥塞通知报文。 |
| DM | Device Memory | 设备内存。 |
| DSCP | Differentiated Services Code Point | 区分服务编码点，根据Diff-Serv（Differentiated Service）的QoS分类标准，在每个数据包IP头部的服务类别TOS字节中，利用已使用的6比特和未使用的2比特，通过编码值来区分优先级。DSCP是TOS字节中已使用6比特的标识，是“IP优先”和“服务类型”字段的组合。为了利用只支持“IP优先”的旧路由器，会使用DSCP值，因为DSCP值与“IP优先”字段兼容。每一个DSCP编码值都被映射到一个已定义的PHB（Per-Hop-Behavior）标识码。通过键入DSCP值，终端设备可对流量进行标识。 |
| DCQCN | Data Center Quantized Congestion Notification | 数据中心网络的拥塞控制算法。 |
| EP | Endpoint | 端点，数据信源和数据信宿，是运行在物理链路上的虚拟链路。 |
| MR | Memory Region | 内存区域。 |
| QP | Queue Pair | 队列对。 |
| RDMA | Remote Direct Memory Access | 远程直接存储器访问，从一台计算机的存储器直接访问另一台计算机的存储器的技术。它使得网卡能够直接访问应用存储器，支持零拷贝网络通信。 |
| RPC | Remote Procedure Call | 远程过程调用，是一个计算机通信协议。该协议允许运行于一台计算机的程序调用另一台计算机的子程序，而程序员无需额外地为这个交互作用编程。如果涉及的软件采用面向对象编程，那么远程过程调用亦可称作远程调用或远程方法调用。 |
| SHM | Shared Memory | 共享内存，在计算机硬件中，共享内存通常是指大量的无序访问内存。该内存能够被多处理器电脑系统的多个不同的CPU访问。 |
| TCP | Transmission Control Protocol | 传输控制协议，TCP/IP中的协议，用于将数据信息分解成信息包，使之经过IP协议发送；并对利用IP协议接收来的信息包进行校验并将其重新装配成完整的信息。TCP是面向连接的可靠协议，能够确保信息的无误发送，它与ISO/OSI基准模型中的传输层相对应。 |
| UDS | Unix Domain Socket | UNIX域套接字，是一种在同一台计算机上的进程间通信机制。 |
| UBC | Unified Bus Clan | UBC协议。 |
