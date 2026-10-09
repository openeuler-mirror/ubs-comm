# Summary 

## 目的

本文介绍了UBS COMM 通信子系统的整体架构和设计原则，用于设计人员和开发人员理解系统的架构和设计原则，指导设计人员进行系统和特性设计，指导开发人员进行开发工作。

## 范围

本文作为MatrixServer通信子系统架构设计，整体包括2个通信组件，HCOM和URMA。主要包括的关键领域特性为：全面支持灵衢2.0网络通信，北向提供Socket生态和纯UB生态两种通信方式，使能应用平滑迁移灵衢架构。本文的架构设计以总-分方式，由通信系统整体设计到各组件设计。

## 利益相关人
|   利益相关人|   关注点与需求|
| ------------ | ------------ |
|通信子系统架构师 |1. 负责定义LingQu BeiMing-TD 2.0通信子系统架构<br>2. 关注包括安全可信在内的架构DFx属性<br>3. 关注架构演进与架构中长期竞争力  |
|通信子系统设计师   |1. 负责确定特性级架构与技术方案<br>2. 关注特性模块设计是否遵循整体架构设计原则   |
|通信子系统软件开发人员   |开发实现系统架构及模块，反馈实现中涉及的问题   |
|产品架构师与设计人员   |1. 关注技术项目合入产品版本后对产品现有架构的影响<br>2. 关注技术项目架构创新能否提升产品的架构竞争力   |
|解决方案集成人员（含ISV/开发者）   |1. 清晰的开发者界面，明确的开发接口、架构与设计原则与约束、文档与指导手册，方便进行解决方案集成<br>2. 完善的开发工作链   |

## 对已有架构的借鉴和反思

### HCOM vs UCX
在通算数据库场景、虚拟化场景、大数据场景、以及项目内部场景等多个场景都提出了通过一个通讯组件屏蔽下层的多种协议(RDMA、TCP、UDS、SHM、URMA等)，这个组件向上提供一组统一的API，这样可以简化上层软件的开发难度；因为RDMA、TCP、UDS、SHM、URMA存在多个方面的差异，概要如下：
1. 功能： RDMA、URMA既有双边通信的能力，又有单边通信的能力；socket只有双边的能力；SHM既没有双边的能力，也没有单边的能力； TCP、UDS拥有自建链的能力，而RDMA、URMA、SHM没有自建链接的能力，需求借助TCP或其他通道来建立链接；
2. 工作模式: RDMA、URMA为proactive的模式（接收方可以不干预，发送方直接操作接收方内存），Socket/UDS为reactive模式（接收方主动recv）；
3. API: RDMA、URMA接口近似，TCP Socket/UDS较为相近，SHM与其它又不一样；
4. DFX: 安全方面, RDMA、URMA、SHM软件没有配套的安全能力，TCP配合openssl的TLS能力达成较好的安全能力； 链接存在的检测，TCP有Keepalive机制，URMA、RDMA、SHM没有这样的能力；

该组件必须保证：
1. 暴露能力的最大集(单边+双边); 
2. 简化底层API的复杂性(比如RDMA、URMA)；
3. 接口统一；
4. 同一个API行为完成一致；
5. 足够的DFX能力；

在业界，开源组件UCX(Unified Communication - X Framework) 有这样的架构，它分为UCT和UCP两层，UCP上层提供统一接口、UCT是对不同底层协议的封装，包括TCP\RDMA\SHM等；在设计HCOM之前，我们使用UCX做基础去构建统一封装，在UCP的基础上补齐了多线程的支持、辅助建链、安全相关的能力。
![](./images/ucx.png)

通过不断的使用和实践后，发现本质问题，即UCX为HPC的集合通讯而设计，将其使用在client/server这种场景下产生了不可弥补的gap，包括：
1. UCX的TCP、UDS、SHM的语义实现与RDMA不对等，即行为不一致；
2. UCX的单线程友好，但多线程的性能有gap, 因为UCX的主要使用者是MPI, MPI基本是单线程；而且使用在多线程的程序里，有多种意想不到的race condition发生，难以处理的core dump;
3. 建链方面，UCX需要双向建TCP，而不是client向server connect的单向；双向建立tcp会引入端口过多也不必要的问题；
4. 资源释放，UCX的使用者是MPI，这类程序的进程是同生共死的，不是client/server, 导致资源释放不及时等问题；

由于上述原因，我们没有选择UCX, 也没有选择在UCX的基础上做修改，而且采用开发HCOM以应对client/server的融合通信组件。这样HCOM也有非常明确的定位与发力方向，UCX定位于集合通讯，而HCOM定位于client/server的通信场景；2个组件独立发展，技术互相借鉴。

### UCM vs RDMA CM
UCM即UB Communication Manager, 主要负责UB通信前的建链，类似RDMA的CM。
UB的RC/RM通信之前必须建立两边的信息交换通道。这个通道可以有三种方式：
1. 利用UCM（公知jetty）建链
2. 利用TCP Socket建链
3. 利用UNIC/IPoUB建链

第1种方式即所谓的带内建链，可直接跑在纯UB环境中，且Bypass了TCP/IP内核协议栈，性能更优。

第2种方式依赖网卡，依赖IP/Socket通道， 适用于有网卡的业务场景。

第3方式需要MatrixServer管控面集中分配和管理IP，而且依然需要走完整的TCP/IP内核协议栈，在大规模建链场景下性能较差，同时IP无法由用户管理配置，有违用户对传统IP的认知。

综上，MatrixServer在没有TCP Socket的情况下采用UCM（公知jetty）的建链方式，同时通信子系统支持TCP Socket建链方式作为可选。


# Usage Example

## 用例视图

### 上下文模型

#### 上下文图

MatrixServer通信子系统（HCOM、URMA）北向对接分布式计算应用（数据库、大数据、虚机热迁），东西向对接MatrixServer逻辑资源管理模块（MXE）， 南向对接UB硬件传输协议。

- HCOM上下文模型

![](./images/4.1.1_1.png)


- URMA上下文模型

![](./images/4.1.1_2.png)

#### 外部接口描述

- HCOM外部接口描述

表1 HCOM外部关键接口

|接口类别   |接口   |类型   |功能   |备注   |
| ------------ | ------------ | ------------ | ------------ | ------------ |
|北向接口|static UBSHcomService* UBSHcomService::Create(UBSHcomServiceProtocol t, const std::string &name, const UBSHcomServiceOptions &opt = {});   |服务层|创建实例对象       | | 
|北向接口|static int32_t UBSHcomService::Destroy(const std::string &name);   |服务层|销毁实例对象       | | 
|北向接口|int32_t UBSHcomService::Start()   |服务层|启动实例           | |
|北向接口|int32_t UBSHcomService::Bind(const std::string &listenerUrl, const UBSHcomServiceNewChannelHandler &handler) |服务层|服务端绑定监听的url和端口号    | |
|北向接口|int32_t UBSHcomService::Connect(const std::string &serverUrl, UBSHcomChannelPtr &ch, const UBSHcomConnectOptions &opt = {})   |服务层|建立连接           | |
|北向接口|void UBSHcomService::Disconnect(const UBSHcomChannelPtr &ch)   |服务层|断开链接 |  | 
|北向接口|int32_t UBSHcomService::RegisterMemoryRegion(uint64_t size, UBSHcomRegMemoryRegion &mr)<br>int32_t UBSHcomService::RegisterMemoryRegion(uintptr_t address, uint64_t size, UBSHcomRegMemoryRegion &mr)   |服务层|1.注册一个内存区域，内存将在UBS Comm内部分配。<br>2.将用户申请的内存，注册到UBS Comm中。|   |
|北向接口|int32_t UBSHcomChannel::Send(const UBSHcomRequest &req, const Callback *done)<br>int32_t UBSHcomChannel::Send(const UBSHcomRequest &req)   |服务层|1. 向对端异步发送一个双边请求消息，并且不等待响应。<br>2. 向对端同步发送一个双边请求消息，并且不等待响应。|  |
|北向接口|int32_t UBSHcomChannel::Get(const UBSHcomOneSideRequest &req, const Callback *done)<br>int32_t UBSHcomChannel::Get(const UBSHcomOneSideRequest &req)   |服务层|1.同步模式下，发送一个读请求给对方。<br>2.异步模式下，发送一个读请求给对方。 | |
|北向接口|int32_t UBSHcomChannel::Put(const UBSHcomOneSideRequest &req, const Callback *done)<br>int32_t UBSHcomChannel::Put(const UBSHcomOneSideRequest &req)   |服务层|1.同步模式下，发送一个写请求给对方。<br>异步模式下，发送一个写请求给对方。 | |
|北向接口|static UBSHcomNetDriver *UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol t, const std::string &name, bool startOobSvr)   |传输层|创建实例对象       | | 
|北向接口|static NResult UBSHcomNetDriver::DestroyInstance(const std::string &name)   |传输层|销毁实例对象       | | 
|北向接口|NResult UBSHcomNetDriver::Initialize(const UBSHcomNetDriverOptions &option)   |传输层|初始化实例         | | 
|北向接口|void UBSHcomNetDriver::UnInitialize()   |传输层|反初始化实例       | | 
|北向接口|NResult UBSHcomNetDriver::Start()   |传输层|启动实例           | |
|北向接口|void UBSHcomNetDriver::Stop()   |传输层|停止实例           | |
|北向接口|void UBSHcomNetDriver::OobIpAndPort(const std::string &ip, uint16_t port)   |传输层|配置监听的IP和Port|   |
|北向接口|NResult UBSHcomNetDriver::Connect(const std::string &payload, UBSHcomNetEndpointPtr &ep, uint32_t flags, uint8_t serverGrpNo, uint8_t clientGrpNo)<br>NResult UBSHcomNetDriver::Connect(const std::string &serverUrl, const std::string &payload, UBSHcomNetEndpointPtr &ep, uint32_t flags, uint8_t serverGrpNo, uint8_t clientGrpNo)   |传输层|建立连接           | |
|北向接口|NResult UBSHcomNetEndpoint::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, uint32_t seqNo)   |传输层|发送双边通信请求   |  | 
|北向接口|NResult UBSHcomNetEndpoint::PostRead(const UBSHcomNetTransRequest &request)   |传输层|单边读请求         | | 
|北向接口|NResult UBSHcomNetEndpoint::PostWrite(const UBSHcomNetTransRequest &request)   |传输层|单边写请求         | | 


【备注】更详细接口在HCOM API使用手册中体现

- HCOM服务层常见错误码说明

表2

|错误码数   |错误码   |含义   |推荐处理方式   |
| ------------ | ------------ | ------------ | ------------ |
|	0	|	SER_OK	|	成功。	|	无	|
|	500	|	SER_ERROR	|	内部错误。	|	排查日志或者联系HCOM问题接口人处理	|
|	501	|	SER_INVALID_PARAM	|	无效参数。	|	检查参数	|
|	502	|	SER_NEW_OBJECT_FAILED	|	对象生成失败。	|	一般是new失败，检查资源是否充足	|
|	503	|	SER_CREATE_TIMEOUT_THREAD_FAILED	|	创建超时处理线程失败。	|	按照通用创建线程失败方式进行排查处理，比如检查是否资源不足，线程数是否达到上限等	|
|	504	|	SER_NEW_MESSAGE_DATA_FAILED	|	生成消息失败。	|	一般是malloc失败，检查资源是否充足	|
|	505	|	SER_NOT_ESTABLISHED	|	NetChannel未建链。	|	链接未建立，需要先通过connect建链	|
|	506	|	SER_STORE_SEQ_DUP	|	序列号重复。	|	极端情况偶发错误，尝试重试	|
|	507	|	SER_STORE_SEQ_NO_FOUND	|	序列号不存在。	|	极端情况偶发错误，尝试重试	|
|	508	|	SER_RSP_SIZE_TOO_SMALL	|	消息大小不一致。	|	检查传入的数据length是否和实际数据大小一致	|
|	509	|	SER_TIMEOUT	|	超时。	|	尝试重试或者检查网络状况	|
|	510	|	SER_TIMER_NOT_WORK	|	超时处理线程开启失败。	|	按照通用创建线程失败方式进行排查处理，比如检查是否资源不足，线程数是否达到上限等	|
|	511	|	SER_NOT_ENABLE_RNDV	|	开启Rndv失败。	|	未开启Rndv，需要在start时的option中设置开启	|
|	512	|	SER_RNDV_FAILED_BY_PEER	|	对端使用Rndv失败。	|	需要两端都启用rndv	|
|	513	|	SER_CHANNEL_ID_DUP	|	Channel Id重复。	|	尝试重试	|
|	514	|	SER_EP_NOT_BROKEN_ALL	|	NetChannel中所有EP未发生断链。	|	尝试重试	|
|	515	|	SER_CHANNEL_NOT_EXIST	|	NetChannel不存在。	|	使用了不存在的channel	|
|	517	|	SER_EP_BROKEN_DURING_CONNECTING	|	NetChannel中所有EP均断链。	|	channel中所有的ep都断开，尝试重新建链	|
|	518	|	SER_NOT_SUPPORT_SERVER_RECONNECT	|	不支持重建链。	|	仅客户端可以重新建链	|
|	519	|	SER_STOP	|	服务停止。	|	服务已停止	|
|	520	|	SER_NULL_INSTANCE	|	空指针	|	检查参数是否正确	|
|	521	|	SER_UNSUPPORTED	|	该功能不支持	|	部分功能仅针对特定场景开放，可联系hcom问题接口人	|
|	522	|	SER_INVALID_IP	|	无效ip	|	检查ip是否正确	|
|	523	|	SER_MALLOC_FAILED	|	malloc失败	|	检查系统资源是否充足	|
|	524	|	SER_SPLIT_INVALID_MSG	|	通信拆包模式下发生错误	|	查看日志获得更详细的报错，检查包大小是否在范围内，空间是否充足，是否有外部攻击者伪造的包等	|



- HCOM传输层常见错误码说明

表3

|错误码数   |错误码   |错误码   |推荐处理方式   |
| ------------ | ------------ | ------------ | ------------ |
|	0	|	NN_OK	|	成功。	|	无	|
|	100	|	NN_ERROR	|	内部错误。	|	排查日志或者联系HCOM问题接口人处理	|
|	101	|	NN_INVALID_IP	|	无效IP地址。	|	检查IP地址是否正确	|
|	102	|	NN_NEW_OBJECT_FAILED	|	创建对象失败。	|	一般为new操作失败，检查系统资源是否充足	|
|	103	|	NN_INVALID_PARAM	|	参数无效。	|	检查参数是否正确	|
|	104	|	NN_TWO_SIDE_MESSAGE_TOO_LARGE	|	双边消息size过大。	|	调整双边消息大小，或者调整为单边方式	|
|	105	|	NN_INVALID_OPCODE	|	无效opCode。	|	opCode设置错误	|
|	106	|	NN_EP_NOT_ESTABLISHED	|	EP未建链。	|	链接未建立，需先建立链接	|
|	107	|	NN_EP_NOT_INITIALIZED	|	EP未初始化。	|	链接未建立，需先建立链接	|
|	109	|	NN_TIMEOUT	|	超时。	|	尝试重试或者检查网络状态	|
|	110	|	NN_INVALID_OPERATION	|	无效操作。	|	未支持操作，请检查协议类型是否支持相应操作	|
|	111	|	NN_MALLOC_FAILED	|	获得内存失败。	|	检查系统资源是否充足	|
|	113	|	NN_NOT_INITIALIZED	|	NetDriver未初始化。	|	需要先初始化Driver	|



【备注】更详细错误码在HCOM API使用手册中体现


- urma外部接口描述
详细查看：[URMA对外接口](http://platformdoc.huawei.com/hedex/hdx.do?lib=075356218&v=01%20(2023-04-28)&homepage=resources/hedex-homepage.html&productId=2797)


### 关键用例模型

#### 关键用例

![](./images/4.2.1.png)

#### 交互场景

1. 通算场景使用socket：
- 用户使用socket，通过UB Socket进行通信。

2. 通算场景直接使用HCOM API / URMA API：
- 用户直接使用HCOM提供的API进行通信，HCOM底层对接URMA、verbs、socket等。
- 用户直接使用URMA提供的API进行通信。


## 部署模型

### 部署节点及规格定义

节点规格：
CPU：1620/1630/1650
芯片互联：HCCS/UB-C
内存：大于64G

### 部署模型

![部署模型 - HCOM](./images/部署模型-HCOM.png)

![部署模型 - URMA](./images/部署模型-URMA.png)

## 运行模型

![运行模型-HCOM](./images/运行模型-HCOM.png)




# Movitvation

## 架构和关键质量属性目标

### 架构目标

面向灵衢2.0新一代计算架构，通信子系统作为平台软件，通过在通信协议、通信算法关键优化技术方案充分发挥UB-C组网性能，支撑实现下一代芯片多打一战略目标。

对于通信子系统而言，灵衢2.0相比当代计算架构的主要变化为：
- 网络互联全面采用UB协议，组网采用超节点架构，超节点内使用UB-C形成组网（1D FullMesh，2D FullMesh等），超节点间使用TCP/IP或RDMA。

此外，考虑生态兼容性和架构归一性，通信子系统的架构演进主要为：
- 分布式通信库需要考虑现有生态的兼容性和易用性，如socket生态。

#### UB影响下的架构目标

MatrixServer框内通信使用UB-C协议进行通信加速，出框通信使用TCP/IP或者RDMA通信。同时网络形态也分为纯UB生态和UB及TCP/IP共存生态，通信子系统在此次架构设计中需要包含这方面的考虑。
![](./images/2.1.1.png)

#### 生态兼容性和易用性下的架构目标

灵衢2.0时代，网络发展路径逐步由TCP->RDMA->UB。网络协议和互联技术在不断的更新，但客户的应用场景使用的协议往往存在滞后性。同时，不同的场景对通信库有不同的诉求：有的要求最低的延迟、有的要求高带宽、有的要求易用性、有的要求应用不改动、有的要求无TCP时能工作、有的要求一套代码适配多种底层协议、有的要求在内核态、有的要求在用户态等。由于通信软件栈，向下使能高速的硬件互联，向上为应用提供API接口，同时为满足多个场景多样化的诉求，整个通信软件栈采用多层的设计，因为单一的组件无法满足多样化的需求。

通过多层多组件架构也达成，满足以下3个方面的要求：
- 能充分发挥硬件的性能（原生通信库接口）；
- 能提供较好的易用性；
- 能提供0修改兼容Legacy应用的能力；

目前主流的应用还是使用socket（TCP/IP），通信子系统在此次架构设计中亦包含这方面考虑。

### 关键架构需求

|序号   |SR编号   |需求名称   |IR编码   |IR标题   |
| ------------ | ------------ | ------------ | ------------ | ------------ |
|1   |SR20250429448477   |【业务面】【通信子系统】【功能】HCOM适配UB-C下URMA多路径和单路径选择能力   |IR20240528000345   |【业务面】【通信子系统】【交付通算】【HCOM】HCOM支持灵衢2.0，北向生态统一兼容。UB-C场景下，软件时延<=0.5us，带宽达到URMA带宽的96%。   |
|2   |SR20250429448690   |【业务面】【通信子系统】【功能】HCOM支持灵衢2.0 URMA自举建链   |IR20240528000345   |【业务面】【通信子系统】【交付通算】【HCOM】HCOM支持灵衢2.0，北向生态统一兼容。UB-C场景下，软件时延<=0.5us，带宽达到URMA带宽的96%。   |
|3   |SR20240626245974   |【业务面】【通信子系统】【交付通算】【性能】HCOM在灵衢2.0（UB-C）下，8KB 单并发<5us（**依赖硬件8KB@3us达成**），单并发256K<30us（**依赖硬件256KB@23us达成**），提供>50GB/s数据传输带宽   |IR20240528000345   |【业务面】【通信子系统】【交付通算】【HCOM】HCOM支持灵衢2.0，北向生态统一兼容。UB-C场景下，软件时延<=0.5us，带宽达到URMA带宽的96%。   |
|4   |SR20240619101230   |【业务面】【通信子系统】【交付通算】【性能】HCOM在灵衢2.0网络下，软件栈时延<=0.5us，带宽达到URMA带宽的96%   |IR20240528000345   |【业务面】【通信子系统】【交付通算】【HCOM】HCOM支持灵衢2.0，北向生态统一兼容。UB-C场景下，软件时延<=0.5us，带宽达到URMA带宽的96%。   |


|JDC RR编号   |JDC标题   |
| ------------ | ------------ |
|2025051360791   |【计算 灵衢 2.0】MXE生成MatrixServer内各节点BondingEID并将其与PrimaryEID、CNA的映射关系下发给URMA   |
|2025051360745   |【计算 灵衢 2.0】URMA提供BondingEID、PrimaryEID及CNA映射关系下发接口并本地缓存   |
|2025041844935   |【计算 灵衢 2.0】URMA支持MatrixServer公知jetty建链和通信   |
|2025041844928   |【计算 灵衢2.0】URMA基础通讯，支持MatrixServer内UBC通信   |
|2025052669524   |【计算 灵衢 2.0】URMA提供UB链路流量可观测性工具   |


### 假设和约束

#### 生命周期约束

生命周期与整体BeiMing-LingQu 2.0版本生命周期保持一致。


## 架构原则

公司级的通用可信架构设计原则，结合产品上下文等特点，涉及的主要架构原则如下：
- HCOM架构原则

|维度   |原则   |原则编号   |来源   |解读   |落地方式   |
| ------------ | ------------ | ------------ | ------------ | ------------ | ------------ |
|可信基础   |服务化/组件化原则   |2.1.1   |ICT可信设计原则V1.2   |根据产品业务诉求，合理的采用服务化、组件化架构，使产品具备灵活、按需组合的能力，以更好地适应为了业务、技术和环境等变化。   |HCOM南向对接URMA，功能实现插件化，与RDMA、TCP等平级，可灵活替换。   |
|可信基础   |分层设计原则   |2.1.2   |ICT可信设计原则V1.2   |系统分为多个层次，每个层次有明确的功能定位，层次之间具有明确的、可信的依赖关系。   |HCOM分为service和transport层，每层提供不同层级的API。   |
|可信基础   |可替换性优先原则   |2.1.4   |ICT可信设计原则V1.2   |优先针对可替换性进行设计，而不是可重用性。随着软件技术的急速发展，在进行架构设计时，产品部件被新技术替代的速度加快，可替代性的重要性在很多情况下远远超过了可重用性。   |HCOM不同通信协议支持插件化，符合可替代性优先原则   |
|可信基础   |最小修改原则   |2.1.5   |ICT可信设计原则V1.2   |业务应用层纵向划分优先原则，通过纵向划分，将大型的域分割为“变更孤岛”。避免业务应用层各部件间的复杂依赖关系。新增特性或问题修改涉及的组件/服务应该内聚，修改范围越少越好。   |基于对象语言类职责功能，合理定义各模块类的成员变量和成员函数，进行功能抽象解耦，满足最小修改原则。   |



- URMA架构原则
具体见：URMA架构设计原则



# Detailed Design

## 关键技术方案设计

### UBS COMM支持UB关键技术方案

![UBS COMM支持UB关键技术方案](./images/UBS-COMM支持UB关键技术方案.png)

表1 Socket生态

|场景   |性能   |兼容生态   |环境要求   |
| ------------ | ------------ | ------------ | ------------ |
|TCP/IP Socket原生协议   |性能低   |应用无感，生态好   |MatrixServer的每个Host配有一张TCP/IP网卡   |
|UBSocket（Socket转UB）<br> 进程级替换<br> ![](./images/进程级替换.png) |性能高|应用无感，需要启动修改脚本，生态较好| MatrixServer的每个Host配有一张TCP/IP网卡 |
|UBSocket（Socket转UB）<br> 修改一行代码<br> ![修改一行代码](./images/修改一行代码.png)   |性能高| 应用修改一行代码，生态一般| MatrixServer的每个Host配有一张TCP/IP网卡 |


表2 非Socket生态

|方式   |易用性   |性能   |高级特性及DFX   |环境要求   |
| ------------ | ------------ | ------------ | ------------ | ------------ |
|对接HCOM   |1. 易用性高<br>（1）控制面：一行代码/一个接口完成建链及协议切换，仅需单向建链，UB场景上层应用仅需感知BondingEID，对jetty等底层概念无感。<br>（2）数据面：提供类rpc和内存语义两种通信接口，提供同步和异步两种通信方式，上层应用通过注册回调方式处理数据收发结果。<br>2. 框内框外通信接口统一，框内框外通信协议不一致的时候使用HCOM可做到接口统一   | 服务层：性能是传输层的95%以上<br>传输层：性能高。性能几乎等同于直接使用URMA接口  | 1. 支持多链接管理，提供KEEP_ALIVE，RECONNECT, BROKEN_ALL三种链接管理方式。<br>2. 支持根据数据包大小自动择优选择通信方式，比如极小包采用inline，大包采用RNDV（双边转单边）。<br>3. 支持身份认证和消息加密传输功能。<br>4. 支持链接主动心跳检测，链接故障主动发现上报。<br>5. 提供性能Trace工具，辅助开发者快速定位故障位置及辅助性能分析。  | 不依赖TCP/IP网卡  |
|对接URMA   | 1. 易用性较低<br>（1）控制面：完整建链需要100行左右代码，需要双向建链，上层应用需要理解jetty 概念，jetty通信方式，公知jetty使用方法等底层概念。<br>（2） 数据面：仅提供post_send和post_recv等基础接口，上层应用需要针对实际场景进行适配，同时需要设计通信线程模型，内存模型等。但是使用灵活性相对较高。<br>2. 框内框外通信如果使用不同通信协议的话，需要对接两套编程接口。  | 性能高  | 无  |不依赖TCP/IP网卡   |


### 建链关键技术方案

UB通信，应用层建链需要先交换本端和对端的jetty信息，因此就有两种建链通道可选：TCP/IP和公知jetty。HCOM建链统一接口可任意选择这两种方式中的一种（方式见编程样例）。URMA API只提供创建公知jetty等基本接口，需要自行实现公知jetty建链或者通过socket接口实现TCP/IP建链。

【建链规格1】链接数限制通信子系统不做约束，同硬件规格（当前硬件出口规格是单节点jetty数上限为64K）。

【建链规格2】管控面4000条TP=》247MB，单个jetty JFS_WQEBB_SIZE(64B) * 深度。

#### 通过TCP/IP建链

![](./images/5.2.1.png)

#### 通过公知jetty建链

![](./images/5.2.2.png)

##### HCOM公知jetty建链和通信编程样例

表1

|端类型   | 代码样例  |
| ------------ | ------------ |
|客户端   | void HcomClientDemo()<br>{<br>UBSHcomServiceOptions options{};<br>options.maxSendRecvDataSize = 1024;<br>UBSHcomService *client = UBSHcomService::Create(UBSHcomServiceProtocol::UBC, "client", options);<br>client->RegisterRecvHandler(ReceivedRequest);<br>client->RegisterChannelBrokenHandler([](const UBSHcomChannelPtr &channel) {}, UBSHcomChannelBrokenPolicy::BROKEN_ALL);<br>client->RegisterSendHandler([](const UBSHcomServiceContext &ctx) {return 0;});<br>client->RegisterOneSideHandler([](const UBSHcomServiceContext &ctx) {return 0;});<br>service->Start();<br>UBSHcomChannelPtr channel;<br>UBSHcomConnectOptions connOpt{};<br>client->Connect("ubc://" + BondingEID + ":" + std::to_string(JettyID), channel, connOpt);<br>UBSHcomRequest req(reinterpret_cast<void *>(dataAddr), dataSize, 0);<br>// 同步发送双边消息<br>channel->Send(req, nullptr);<br>client->Disconnect(channel);<br>UBSHcomService::Destroy("client");<br>}  |
|服务端   |// 接收到新建链请求回调函数<br>int ReceivedRequest(UBSHcomServiceContext &context)<br>{<br>// 执行业务逻辑<br>return 0;<br>}<br><br>int NewChannel(const std::string &ipPort, const UBSHcomChannelPtr &ch, const std::string &payload)<br>{<br>// 执行业务逻辑<br>return 0;<br>}<br>void HcomServerDemo()<br>{<br>UBSHcomServiceOptions options;<br>options.maxSendRecvDataSize = 1024;<br>options.workerGroupMode = ock::hcom::NET_EVENT_POLLING;<br>UBSHcomService *server = UBSHcomService::Create(UBSHcomServiceProtocol::UBC, "server", options);<br>uint64_t bondingEid; // 应用本端的bondingEid<br>uint32_t jettyId; // 应用自己设置公知jettyId<br>server->RegisterRecvHandler(ReceivedRequest);<br>server->RegisterChannelBrokenHandler([](const UBSHcomChannelPtr &channel) {}, UBSHcomChannelBrokenPolicy::BROKEN_ALL);<br>server->RegisterSendHandler([](const UBSHcomServiceContext &ctx) { return 0; });<br>server->RegisterOneSideHandler([](const UBSHcomServiceContext &ctx) { return 0; });<br>server->Bind("ubc://" + bondingEid + ":" + std::to_string(jettyId), NewChannel);<br>server->Start();<br>// 业务逻辑执行完成后清理资源<br>UBSHcomService::Destroy("server");<br>}|


##### URMA公知jetty建链和通信编程样例

表1

|端类型   |代码样例   |
| ------------ | ------------ |
|客户端   |void UrmaClientDemo()<br>{<br>    // URMA资源初始化<br>    urma_init_attr_t attr{};<br>    urma_status_t status = urma_init(&attr);<br>    urma_eid_t eid;<br>    urma_device_t *device = urma_get_device_by_eid(eid, URMA_TRANSPORT_UB);<br>    urma_context_t *context =  urma_create_context(device, 0);<br><br>    // 创建jfc队列<br>    urma_jfc_cfg_t jfcCfg{};<br>    urma_jfc_t *jfc = urma_create_jfc(context, &jfcCfg);<br><br>    // 创建数据面jetty<br>    urma_jfs_cfg_t jfsCfg{};<br>    urma_jfr_cfg_t jfrCfg{};<br>    urma_jetty_cfg_t jettyCfg{};<br>    jettyCfg.jfs_cfg = jfsCfg;<br>    jettyCfg.jfr_cfg = &jfrCfg;<br>    urma_jfr_t *jfr = urma_create_jfr(context, &jfrCfg);<br>    urma_jetty_t *jetty = urma_create_jetty(context, &jettyCfg);<br>    <br>    // 创建公知jetty<br>    urma_jetty_cfg_t publicJettyCfg{};<br>    publicJettyCfg.id = 100; // 自定义公知jetty号<br>    urma_jetty_t *publicJetty = urma_create_jetty(context, &publicJettyCfg);<br><br>    // 公知jetty建链<br>    urma_rjetty_t remotePublicJetty{};<br>    remotePublicJetty.jetty_id.eid = 1;    // 指定对端公知jetty所在的eid<br>    remotePublicJetty.jetty_id.id = 100;   // 指定对端公知jetty的jetty_id<br>    urma_token_t tokenValue{};<br>    urma_target_jetty_t *targetPublicJetty = urma_import_jetty(context, &remotePublicJetty, &tokenValue);<br>    <br>    // 通过公知jetty发送本端数据面通信jetty信息<br>    urma_jfs_wr_t wr{}; // wr中填充本端jetty信息<br>    wr.tjetty = targetPublicJetty;<br>    urma_jfs_wr_t *badWr;<br>    status = urma_post_jetty_send_wr(publicJetty, &wr, &badWr);<br><br>    // 创建线程poll jfc队列，或者通过中断的方式poll jfc队列，本部分较复杂，暂不做代码样例实现<br>    // 下面流程假定已经从jfc队列中poll到了对端返回来的对端数据面jetty信息，假定为remoteJetty<br>    // 与对端数据面jetty建链<br>    urma_rjetty_t remoteJetty{};<br>    urma_target_jetty_t *targetJetty = urma_import_jetty(context, &remoteJetty, &tokenValue);<br>    <br>    // 通过数据面jetty进行通信<br>    urma_jfs_wr_t wr2{};    // wr2中填充需要发送的数据等信息<br>    wr2.tjetty = targetJetty;<br>    urma_jfs_wr_t *badWr2;<br>    status = urma_post_jetty_send_wr(publicJetty, &wr, &badWr2);<br><br>    // 业务发送处理完成后清理回收资源<br>    status =  urma_unimport_jetty(targetJetty);<br>    status = urma_unbind_jetty(jetty);<br>    status = urma_unimport_jetty(targetPublicJetty);<br>    status = urma_unbind_jetty(publicJetty);<br>}   |
|服务端   |void UrmaServerDemo()<br>{<br>    // URMA资源初始化<br>    urma_init_attr_t attr{};<br>    urma_status_t status = urma_init(&attr);<br>    urma_eid_t eid;<br>    urma_device_t *device = urma_get_device_by_eid(eid, URMA_TRANSPORT_UB);<br>    urma_context_t *context =  urma_create_context(device, 0);<br><br>    // 创建jfc队列<br>    urma_jfc_cfg_t jfcCfg{};<br>    urma_jfc_t *jfc = urma_create_jfc(context, &jfcCfg);<br><br>    // 创建数据面jetty<br>    urma_jfs_cfg_t jfsCfg{};<br>    urma_jfr_cfg_t jfrCfg{};<br>    urma_jetty_cfg_t jettyCfg{};<br>    jettyCfg.jfs_cfg = jfsCfg;<br>    jettyCfg.jfr_cfg = &jfrCfg;<br>    urma_jfr_t *jfr = urma_create_jfr(context, &jfrCfg);<br>    urma_jetty_t *jetty = urma_create_jetty(context, &jettyCfg);<br><br>    // 创建公知jetty<br>    urma_jetty_cfg_t publicJettyCfg{};<br>    publicJettyCfg.id = 100; // 自定义公知jetty号<br>    urma_jetty_t *publicJetty = urma_create_jetty(context, &publicJettyCfg);<br><br>    // 创建线程poll jfs队列，或者通过中断方式poll jfc队列，本部分较复杂，暂不做样例实现<br>    // 下面流程假定已经从jfc队列中poll到了对端通过公知jetty通道发过来的对端数据面jetty信息，假定为remoteJetty<br>    // 与对端数据面jetty建链<br>    urma_rjetty_t remoteJetty{};<br>    urma_token_t tokenValue{};<br>    urma_target_jetty_t *targetJetty = urma_import_jetty(context, &remoteJetty, &tokenValue);<br><br>    // 通过数据面jetty通道，将本端数据面jetty信息发送给对端<br>    urma_jfs_wr_t wr{};    // wr中填充需要发送的数据等信息<br>    wr.tjetty = targetJetty;<br>    urma_jfs_wr_t *badWr;<br>    status = urma_post_jetty_send_wr(publicJetty, &wr, &badWr);<br><br>    // 业务发送处理完成后清理回收资源<br>    status =  urma_unimport_jetty(targetJetty);<br>    status = urma_unbind_jetty(jetty);<br>    status = urma_unbind_jetty(publicJetty);<br>}   |


### URMA支持多路径通信关键技术方案

![1D_FM](./images/1D_FM.png "1D_FM")![2D_FM](./images/2D_FM.png "2D_FM")


URMA多路径会使能两节点间直连路径和全部的跨跳路径（最多一跳）。

如上图，8节点1D FM组网两个Host之间存在14条路径，16节点2D FullMesh组网同轴两个Host之间存在6条路径。跨跳路径的时延会劣于直连路径，因此，针对不同应用的诉求，URMA提供两种多路径模式：低时延模式和高带宽模式。

低时延模式：URMA发送数据包只使用一条路径，优先使用直连路径，达到极致时延目的。（如：大数据、数据库）
高带宽模式：URMA发送数据包会使用全部6条路径，达到极致带宽目的。（如：虚机热迁）


- HCOM多路径接口和数据结构定义如下：（通过服务层的SetUbcMode接口设置多路径模式）

```
/**
 * @brief 设置 UB-C 多路径模式
 *
 * @param ubcMode UB-C 多路径模式
 */
virtual void SetUbcMode(UBSHcomUbcMode ubcMode) = 0;

enum class UBSHcomUbcMode : int8_t {
    Disabled = -1,      ///< 禁用多路径能力（默认）
    LowLatency = 0,     ///< 低时延模式，使用单路径发送
    HighBandwidth = 1,  ///< 高带宽模式，使用多条路径发送
};
```

- URMA多路径接口和数据结构定义如下：（创建jetty时在jetty_cfg中设置多路径模式）

```
urma_jetty_t *urma_create_jetty(urma_context_t *ctx, urma_jetty_cfg_t *jetty_cfg);
typedef struct urma_jetty_cfg {
    …
    int mode;   // 0：低时延模式（只使用一条路径）；1：高带宽模式（使用全部路径）
} urma_jetty_cfg_t;

```

### TP/CTP选型方案

表1

|类型   |限制   |
| ------------ | ------------ |
|TP   |1. 无拥塞控制<br>2. 双边数据大小限制最大64KB，数据大小超出4KB的话进程退出需等待28s（TP必须等待28s确保没有新的REQ到达才能销毁）=>RC模式或者不共享TP的RM模式可以通过先销毁TP在销毁TA的方式规避该问题<br>3. 有TA层的重传机制   |
|CTP（compacted Transport）   |1. 拥塞控制只有TA粒度的，一条路径发送拥塞，会导致整体降速<br>2. 双边数据大小限制最大4KB<br>3. 仅仅有链路层重传   |


- HCOM对外呈现：
HCOM只支持RC模式+TP，该使用方式基本对齐RDMA方式。满足数据库/大数据等使用HCOM业务对于时延迟和性能的要求。
- URMA对外呈现：
既支持TP又支持CTP，import时设置。


## 逻辑架构

### 逻辑模型

1. EID和CNA模型

![](./images/EID模型.png)![](./images/6.1.1_2.png)


EID（Entity ID）：是UB Entity在UB Domain内的唯一标识，EID用于标识参与通信的对象，可唯一的标识UB Domain内的主机和设备。在需要访问某个UB Entity时，需要先知道目标EID，这个属于先验知识。EID是一个UB Domain内全局唯一的128bit值。

CNA（Clan Network Address），是UB Clan网络层地址。

EID和CNA根据其所属主体不同，有分为Bonding EID，Primary EID，Primary CNA及Port CNA，相互关系和说明如下表：

表1

|   |主体   |说明   |归属   |
| ------------ | ------------ | ------------ | ------------ |
|	Bonding EID	|	节点	|	软件层面对上层应用屏蔽Primary EID，对Primary EID做Bonding，Bonding后的EID即为Bonding EID	|	软件	|
|	Primary EID	|	IO Die	|	IO Die的Entity ID	|	硬件	|
|	Primary CNA	|	IO Die	|	IO Die的网络地址	|	硬件	|
|	Port CNA	|	port	|	IO Die上port的网络地址	|	硬件	|


2. 系统架构模型

![系统架构模型](./images/6.1.2.png)

表2

|模块   |职责   |形态   |
| ------------ | ------------ | ------------ |
|	分布式应用（大数据、数据库、虚机热迁等）	|	1. 获取BondingEID（URMA把Bonding设置到URMA设备上，可以通过命令行查到）<br>2. 调用HCOM/URMA建链通信接口	|	用户态进程	|
|	HCOM	|	1. 北向提供统一建链、通信接口，屏蔽底层网络通信协议差异<br>2. 灵衢2.0网络中，南向对接URMA，使能UB网络协议	|	lib库	|
|	URMA	|	1. 提供UB建链、通信接口<br>2. 通过UDMA进行数据收发<br>3. UVS本地缓存Bonding EID、Primary EID及CNA的映射关系	|	1. urma用户态so<br>2. urma内核态ko<br>3. uvs用户态so<br>4. uvs内核态ko	|
|	UDMA	|	1. 海思硬件驱动，提供网络层数据收发能力	|	udma.ko	|
|	MXE	|	1. 逻辑资源管理，负责Bonding EID生成<br>2. MXE通过dlopen uvs.so的方式调用推送接口将Bonding EID、Primary EID和CNA映射关系下发给UVS，并且更新时进行全量推送	|	用户态进程	|


3. 通信链接模型

![](./images/通信链接模型.png)

表3

|元素   |说明   |与下一层关系说明   |
| ------------ | ------------ | ------------ |
|	Process	|	应用进程	|	1对n，一个Process中可以创建n个Hcom_instance（Process每调一次Hcom的HcomService::Create接口创建一个Hcom_instance）	|
|	Hcom_instance	|	Hcom实例	|	1对n，一个Hcom实例中可以创建n个Hcom_channel（通过instance指针每调一次connect接口创建一个Hcom_channel）	|
|	Hcom_channel	|	Hcom封装的逻辑链接	|	1对1~16，一个Hcom_channel中可以创建1~16个Hcom_ep（通过配置选项中ep数量）	|
|	Hcom_ep	|	Hcom通信的endpoint，每个ep是一条URMA链接的一个通信结点	|	1对1，一个Hcom_ep对应一个jetty	|
|	jetty	|	urma通信实体	|	通过import_jetty来创建urma链接	|


#### 架构模式

整体通信子系统架构，采用分层架构的架构模式进行设计。

通信服务层：北向提供对接不同场景业务的API，其中包含（部分）业界标准或事实标准的接口

通信传输层：南向对接多种硬件、多种协议（含UB）。

![通信子系统-L0](./images/通信子系统-L0.png)


#### 1层-n层逻辑模型

##### HCOM逻辑模型

![HCOM](./images/HCOM.png)

#### 接口设计

##### HCOM接口设计

HCOM接口设计：

|接口名称   |接口类型   |职责   |备注   |
| ------------ | ------------ | ------------ | ------------ |
|	static UBSHcomService* UBSHcomService::Create(UBSHcomServiceProtocol t, const std::string &name, const UBSHcomServiceOptions &opt = {})	|	服务层	|	创建服务对象	|	/	|
|	static int32_t UBSHcomService::Destroy(const std::string &name)	|	服务层	|	销毁服务对象	|	/	|
|	int32_t UBSHcomService::Start()	|	服务层	|	启动实例	|	/	|
|	int32_t UBSHcomService::Connect(const std::string &serverUrl, UBSHcomChannelPtr &ch, const UBSHcomConnectOptions &opt = {})	|	服务层	|	建立连接	|	/	|
|	int32_t UBSHcomService::Bind(const std::string &listenerUrl, const UBSHcomServiceNewChannelHandler &handler)	|	服务层	|	服务端绑定监听的url和端口号	|	/	|
|	int32_t UBSHcomChannel::Send(const UBSHcomRequest &req, const Callback *done)<br>int32_t UBSHcomChannel::Send(const UBSHcomRequest &req)	|	服务层	|1. 向对端异步发送一个双边请求消息，并且不等待响应。<br>2. 向对端同步发送一个双边请求消息，并且不等待响应。|	/	|
|	int32_t UBSHcomChannel::Get(const UBSHcomOneSideRequest &req, const Callback *done)<br>int32_t UBSHcomChannel::Get(const UBSHcomOneSideRequest &req)	|	服务层	|	1.同步模式下，发送一个读请求给对方。<br>异步模式下，发送一个读请求给对方。	|	/	|
|	int32_t UBSHcomChannel::Put(const UBSHcomOneSideRequest &req, const Callback *done)<br>int32_t UBSHcomChannel::Put(const UBSHcomOneSideRequest &req)	|	服务层	|1.同步模式下，发送一个写请求给对方。<br>2.异步模式下，发送一个写请求给对方。	|	/	|
|	static UBSHcomNetDriver *UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol t, const std::string &name, bool startOobSvr)	|	传输层	|	创建实例对象	|	/	|
|	static NResult UBSHcomNetDriver::DestroyInstance(const std::string &name)	|	传输层	|	销毁实例对象	|	/	|
|	NResult UBSHcomNetDriver::Initialize(const UBSHcomNetDriverOptions &option)	|	传输层	|	初始化实例	|	/	|
|	void UBSHcomNetDriver::UnInitialize()	|	传输层	|	反初始化实例	|	/	|
|	NResult UBSHcomNetDriver::Start()	|	传输层	|	启动实例	|	/	|
|	void UBSHcomNetDriver::Bind(const std::string &url)	|	传输层	|	停止实例	|	/	|
|	NResult UBSHcomNetDriver::Connect( const std::string &serverUrl uint16_t oobPort, const std::string &payload, UBSHcomNetEndpointPtr &ep)	|	传输层	|	建立连接	|	/	|
|	NResult UBSHcomNetEndpoint::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request)	|	传输层	|	发送双边通信请求	|	/	|
|	NResult UBSHcomNetEndpoint::PostRead(const UBSHcomNetTransRequest &request)	|	传输层	|	单边读请求	|	/	|
|	NResult UBSHcomNetEndpoint::PostWrite(const UBSHcomNetTransRequest &request)	|	传输层	|	单边写请求	|	/	|


### 行为模型

#### 分布式应用使用HCOM行为模型

![](./images/分布式应用使用HCOM行为模型.png)


#### 分布式应用使用URMA行为模型

![](./images/6.2.2.png)


### 数据模型

#### 架构模式

#### 关键数据设计

##### HCOM关键数据设计

- UBSHcomNetDriverOptions

```
/**
 * @brief UBSHcomNetDriver options
*/
struct UBSHcomNetDriverOptions { 
    char netDeviceIpMask[NN_NO256] {};// ip masks for devices
    bool enableTls = false;// enable ssl
    UBSHcomNetCipherSuite cipherSuite= AES_GCM_128;// if tls enabled can set cipher suite, client and server should same
/* worker setting */
    bool dontStartWorkers = false;// start worker or not
    UBSHcomNetDriverWorkingMode mode= NET_BUSY_POLLING;// worker polling mode, could busy polling or event polling
    char workerGroups[NN_NO64] {};// worker groups, for example 1,3,3
    char workerGroupsCpuSet[NN_NO128] {};// worker groups cpu set, for example 1-1,2-5,na
// worker thread priority [-20,20], 20 is the lowest, -20 is the highest, 0 (default) means do not set priority
    int workerThreadPriority = 0; 
/* connection attribute */
    NetDriverOobType oobType= NET_OOB_TCP;// oob type, tcp or UDS, UDS cannot accept remote connection
    UBSHcomNetDriverLBPolicy lbPolicy= NET_ROUND_ROBIN;// select worker load balance policy, default round-robin
    uint16_t magic = NN_NO256;// magic number for c/s connect validation
    uint8_t version = 0;// program version used by connect validation
/* heart beat attribute */
    uint16_t heartBeatIdleTime = NN_NO60;// heart beat idle time, in seconds
    uint16_t heartBeatProbeTimes = NN_NO7;// heart beat probe times
    uint16_t heartBeatProbeInterval = NN_NO2;// heart beat probe interval, in seconds
/* options for only tcp protocol */
// timeout during io (s), it should be [-1, 1024], -1 means do not set, 0 means never timeout during io
    int16_t tcpUserTimeout = -1; 
    bool tcpEnableNoDelay = true;// tcp TCP_NODELAY option, true in default
    bool tcpSendZCopy = false;// tcp whether copy request to inner memory, false in default
/* The buffer sizes will be adjusted automatically when these two variables are 0, and the performance would be
     * better */
    uint16_t tcpSendBufSize = 0;// tcp connection send buffer size in kernel, by KB
    uint16_t tcpReceiveBufSize = 0;// tcp connection send receive buf size in kernel, by KB
/* options for rdma protocol only */
    uint32_t mrSendReceiveSegCount = NN_NO8192;// memory region segment count for two side operation
    uint32_t mrSendReceiveSegSize = NN_NO1024;// data size of memory region segment
/* transmit of 256b data performs better when dmSegSize is 290 */
    uint32_t dmSegSize = NN_NO290;// data size of device memory segment
    uint32_t dmSegCount = NN_NO400;// segment count of device memory segment
    uint16_t completionQueueDepth = NN_NO2048;// completion queue size of rdma
    uint16_t maxPostSendCountPerQP = NN_NO64;// max number request could issue
    uint16_t prePostReceiveSizePerQP = NN_NO64;// pre post receive of qp
    uint16_t pollingBatchSize = NN_NO4;// polling batch size for worker
    uint32_t eventPollingTimeout = NN_NO500;// event polling timeout in ms, max value is 2000000ms
    uint32_t qpSendQueueSize = NN_NO256;// max send working request of qp for rdma
    uint32_t qpReceiveQueueSize = NN_NO256;// max receive working request of qp for rdma
    uint16_t oobConnHandleThreadCount = NN_NO2;// server accept connection thread num
    uint32_t oobConnHandleQueueCap = NN_NO4096;// server accept connection queue capability
    uint8_t slave = 1;// slave 1 or 2
    char oobPortRange[NN_NO16] {};// port range when enable port auto selection
/* verify the common options of each driver */
    NResultValidateCommonOptions(); 
    std::stringNetDeviceIpMask() const; 
    std::stringWorkGroups() const; 
    std::stringWorkerGroupCpus() const; 
    /**
     * @brief Set the ip mask for net devices, example: 192.168.0.1/24
*/
    bool SetNetDeviceIpMask(const std::string &mask); 
    /**
     * @brief Set worker groups, example: 1,3,4
     * meaning 3 groups for workers:
     * group0 has 1 workers
     * group1 has 3 workers
     * group2 has 4 workers
*/
    bool SetWorkerGroups(const std::string &groups); 
    /**
     * @brief Set worker groups, example: 10-10,11-13,na
     * meaning 3 groups for workers:
     * group0 bind to cpu 10
     * group1 bind to cpu 11, 12, 13
     * group2 not bind to cpu
*/
    bool SetWorkerGroupsCpuSet(const std::string &value); 
    std::stringToString() const; 
    std::stringToStringForSock() const; 
} __attribute__((packed));

```

- UBSHcomNetTransRequest

```
/**
 * @brief Transfer request
*/
struct UBSHcomNetTransRequest { 
    uintptr_t lAddress = 0;// local buffer address
    uintptr_t rAddress = 0;// remote buffer address
    uint32_t lKey = 0;// local memory region key, for rdma etc.
    uint32_t rKey = 0;// remote memory region key, for rdma etc.
    void *srcSeg = nullptr; 
    void *dstSeg = nullptr; 
    uint32_t size = 0;// buffer size
    uint16_t upCtxSize = 0;// upper context size
    char upCtxData[NN_NO64] = {};// upper context data
    UBSHcomNetTransRequest() = default; 
    UBSHcomNetTransRequest(void *data, uint32_t dataSize, uint16_t upContextSize) 
    :lAddress(reinterpret_cast<uintptr_t>(data)), size(dataSize), upCtxSize(upContextSize) 
    {}
    UBSHcomNetTransRequest(uintptr_t la, uintptr_t ra, uint32_t lk, uint32_t rk, uint32_t s, uint16_t upCtxSi) 
    :lAddress(la), rAddress(ra), lKey(lk), rKey(rk), size(s), upCtxSize(upCtxSi) 
    {}
} __attribute__((packed));

```

- UBSHcomServiceOptions

```
struct UBSHcomServiceOptions {
    uint32_t maxSendRecvDataSize = 1024;    // 发送数据块最大值
    uint16_t workerGroupId = 0;     // group id of the worker group, must increment from 0 and be unique
    uint16_t workerGroupThreadCount = 1;    // worker线程数，如果设置为0的话，不启动worker线程
    UBSHcomWorkerMode workerGroupMode = NET_BUSY_POLLING;  // worker线程工作模式，默认busy_polling
    int8_t workerThreadPriority = 0;    // 线程优先级[-20,19]，19优先级最低，-20优先级最高，同nice值
    std::pair<uint32_t, uint32_t> workerGroupCpuIdsRange = {UINT32_MAX, UINT32_MAX};  // default not bind
};

```

- UBSHcomRequest

```
struct UBSHcomRequest {
    void *address = nullptr;                    /* pointer of data */
    uint32_t size = 0;                          /* size of data */
    uint32_t key = 0;
    uint16_t opcode = 0;                        /* operation code of request */

    UBSHcomRequest() = default;
    UBSHcomRequest(void *addr, uint32_t sz, uint16_t op) : address(addr), size(sz), opcode(op) {}
};

```

#### 静态数据结构模型

#### 数据所有权模型


### 逻辑元素清单


## 实现架构

### 技术模型

#### 技术选型

- HCOM：延续原先架构进行演进，增加对接URMA支持UB协议，扩充HCOM对外API的丰富性和兼容性。

- URMA/UVS: 延续Scale Out的架构，增加Scale Up的支持。

### 代码模型（HCOM）

#### 代码模型

![](./images/代码模型-HCOM.png)

#### 代码元素清单

|逻辑元素（服务/微服务/组件/模块）|逻辑元素编号| 代码元素名称（目录/代码仓链接） |代码元素编号|
| ------------ | ------------ | ------------ | ------------ |
|	组件	|	1	|	api	|	1	|
|	组件	|	2	|	common	|	2	|
|	组件	|	3	|	service	|	3	|
|	组件	|	4	|	transport	|	4	|
|	组件	|	5	|	under_api	|	5	|


### 构建模型

#### 构建模型

![构建模型-HCOM](./images/构建模型-HCOM.png)


#### 构建元素清单

|构建元素（编译目标文件/执行目标文件）|构建元素编号 |构建过程/工具链|对应的代码元素|代码元素编号   |
| ------------ | ------------ | ------------ | ------------ | ------------ |
|	执行目标文件	|	1	|	cmake	|	libhcom.so	|	1	|
|	执行目标文件	|	2	|	cmake	|	libhcom_static.a	|	2	|
|	执行目标文件	|	3	|	cmake	|	libhcom_adapter.so	|	3	|
|	执行目标文件	|	4	|	cmake	|	libhcom_jni.so	|	4	|
|	执行目标文件	|	5	|	java构建工具	|	hcom-sdk.jar	|	5	|
|	执行目标文件	|	6	|	cmake	|	hcom.mod	|	6	|


### 硬件实现模型

不涉及

### 交付模型

#### 交付模型

![HCOM交付模型](./images/HCOM交付模型.png)

![URMA交付模型](./images/URMA交付模型.png)


# Design constraints

## HCOM异常处理和可定位性（补充章节）

- 异常处理和可靠性
HCOM异常及其对应的可靠性保障主要发生在三个阶段：初始化&启动，建链，数据面通信，每个阶段的典型场景和可靠性保障如下表：

|阶段   |功能|故障模式| 可能的故障原因 |故障影响|可靠性措施 |
| ------------ | ------------ | ------------ | ------------ | ------------ | ------------ |
|	初始化&启动	|	线程模型初始化（包括超时处理periodic线程，worker线程，心跳线程）	|	死循环	|	未收到needStop信号	|	程序占用大量CPU导致系统响应慢<br>停止线程无法退出，卡死导致无法收发消息	|	循环通过neestop发送信号退出线程	|
|	初始化&启动	|	线程模型初始化（包括超时处理periodic线程，worker线程，心跳线程）	|	退出	|	错误调用stop，让线程收到needStop信号	|	程序异常终止，可能是由于未捕获的异常用户主动终止程序，线程退出，无法收发消息	|	代码流程保证只有在退出时才调用stop	|
|	初始化&启动	|	线程模型初始化（包括超时处理periodic线程，worker线程，心跳线程）	|	句柄泄漏	|	连续创建过多句柄，或句柄随线程创建导致过多	|	线程退出句柄释放，确认代码保证句柄不会泄露	|	析构、出错、线程退出时候释放句柄	|
|	初始化&启动	|	线程模型初始化（包括超时处理periodic线程，worker线程，心跳线程）	|	栈溢出	|	局部变量过大，或任务堆栈设置过小	|	确认代码保证无栈溢出	|	确认代码保证无栈溢出	|
|	初始化&启动	|	线程模型初始化（包括超时处理periodic线程，worker线程，心跳线程）	|	无法启动	|	资源不够无法启动、权限不对	|	service启动失败，报错返回	|	服务退出	|
|	初始化&启动	|	内存池创建和初始化	|	内存泄漏	|	内存池资源申请了未释放	|	资源泄漏，导致拒绝服务	|	代码保证调用Allocate后再调用Free对应释放	|
|	建链	|	客户端connect	|	消息发送失败	|	网络连接问题、目标主机不可达、软件错误、硬件故障等	|	建链消息发送失败，建链失败	|	设置重试次数和间隔进行重试建链	|
|	建链	|	客户端收rsp	|	等待应答超时	|	网络延迟过高、目标主机处理缓慢、网络拥塞、目标主机未响应等	|	建链回复时recv卡死，若无法超时退出，进程卡死	|	通过环境变量设置recv的超时时间，超时退出，建链失败	|
|	建链	|	客户端收rsp	|	报文内容损坏	|	网络原因导致报完损坏	|	建链回复状态connectStatus被修改，导致建链成功了但是提示建链失败，再尝试建链，会多次尝试建链	|	通过环境变量设置重试间隔和次数，保证能够有效退出。并且代码里限制最大重试次数为10次，最大重试间隔为60s	|
|	建链	|	客户端收rsp	|	报文丢失	|	网络拥塞导致丢包、硬件故障、路由错误等	|	建链回复时recv卡死，若无法超时退出，进程卡死	|	通过环境变量设置recv的超时时间，超时退出，建链失败	|
|	建链	|	客户端收rsp	|	报文超大	|	网络攻击，网络报文超大	|	只收sizeof(ConnectResp)大小的数据，消息解析失败	|	只收sizeof(ConnectResp)大小的数据，消息解析失败	|
|	建链	|	服务端accept	|	等待应答超时	|	网络延迟过高、目标主机处理缓慢、网络拥塞、目标主机未响应等	|	建链回复时recv卡死，若无法超时退出，进程卡死	|	通过环境变量设置recv的超时时间，超时退出，建链失败	|
|	建链	|	服务端accept	|	报文内容损坏	|	网络原因导致报完损坏	|	建链回复状态connectStatus被修改，导致建链成功了但是提示建链失败，再测去尝试建链	|	异常退出	|
|	建链	|	服务端accept	|	报文丢失	|	网络拥塞导致丢包、硬件故障、路由错误等	|	建链回复时recv卡死，若无法超时退出，进程卡死	|	通过环境变量设置recv的超时时间，超时退出，建链失败	|
|	建链	|	服务端accept	|	报文乱序	|	不涉及，阻塞式发送，只发一条消息	|		|		|
|	建链	|	服务端accept	|	报文超大	|	网络攻击，网络报文超大	|	报文被分片，增加了复杂度；部分报文可能丢失或乱序	|	只收固定大小的数据，消息解析失败	|
|	数据面通信	|	双边发送消息send	|	拥塞窗口大小不合理	|	拥塞窗口设置不合理过大或者过小	|	窗口过大导致拥塞加剧，窗口过小导致数据传输效率低	|	设置时间窗口和数据量显示，当前时间窗口如果超过了发送数据则不再发送。并且时间窗口和数据窗口都可配置	|
|	数据面通信	|	双边发送消息send	|	资源申请失败	|	资源空间不够	|	消息发送一次失败，返回SER_NEW_OBJECT_FAILED，重试发送（当前也有重试机制）	|	重试发送，在超时的时间窗内会尝试重发SER_NEW_OBJECT_FAILED错误码会重试，重试时间间隔usleeep(100UL)	|

HCOM全量故障模式分析详见：通信子系统-HCOM-SFMEA故障模式&分析表 .xlsx

- 可定位性
HCOM提供CLI工具定位分析链路详情，可以定位每条链路的调用次数/成功次数/失败次数/时延最大值/时延最小值/时延平均值/时延分位值等，使用方式详见：[HCOM性能分析工具使用指导手册](https://idp.huawei.com/idp-designer-war/design?op=edit&locate=newMode/EDIT/205482681522/ZH-CN_BOOKMAP_0000002062055353/ZH-CN_TOPIC_0000002117625377/6)

![](./images/8.1.png)


# Adoption strategy
- 当前的应用/模块如何适配到此模块

# Related Documentions
其他与此模块相关的设计文档

# SIGs/Maintianers
所属与关联的SIG与相关的maintainer