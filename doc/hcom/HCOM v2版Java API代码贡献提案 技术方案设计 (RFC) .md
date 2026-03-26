**状态 (Status):** Draft / Reviewing / Approved / Rejected / Superseded
**作者 (Authors):** 吴宗谚
**创建日期 (Created):** 2026-3-12 
**更新日期 (Updated):** 2026-3-12 
**相关 Issue/PR:** 

---

# 1. 概述

## 1.1 简介

HCOM Java API V2是基于JNI技术对HCOM底层C++库的Java语言封装，提供了一套完整的、高性能的分布式通信编程接口。本提案旨在解决Java生态系统中高性能分布式通信的需求痛点，通过JNI技术桥接C++底层能力，为Java开发者提供统一、易用、高性能的通信SDK。

## 1.2 动机

openfuyao Uniffle 基于UB改造项目应用

在高性能大数据分布式计算场景中，Java作为企业级开发主流语言，其原生网络通信库性能存在明显瓶颈。具体痛点包括：

1）Java NIO模型复杂，难以充分利用硬件加速特性；

2）传统TCP通信延迟高、吞吐量低，无法满足UB、RDMA等高性能网络技术要求；

3）缺乏统一的、针对分布式场景优化的通信编程模型。

实现本提案的必要性在于：1）填补HCOM Java高性能分布式通信技术栈空白；2）充分利用HCOM在C++领域的成熟技术积累；

## 1.3 目标

**目标：**

- 提供完整的高性能分布式通信Java API，支持TCP、UDS、RDMA、UBC等多种协议栈
- 兼容主流Java开发环境（JDK 8+）和构建工具（Maven/Gradle）
- 提供完整的开发工具链和测试框架

**非目标：**

- 不涉及Java语言层面的网络协议重构
- 不替代现有的JVM或Java安全机制
- 不提供与HCOM无关的分布式协调功能

# 2. 用例分析

支持Uniffle 对接HCOM 使用UB协议的能力，完成大约20+个常用C++ 接口JNI封装，并且通过perf测试用例

**功能点：**

- 支持海量数据节点间的高效数据传输
- 提供异步数据发送和接收机制
- 支持UB、TCP、RDMA等协议切换

# 3. 方案设计

## 3.1 总体方案

### 3.1.1 整体架构设计

HCOM Java API V2采用分层架构设计，自底向上分为：**底层C++服务层**、**JNI桥接层**、**Java接口层**、**应用层**。整体架构遵循高内聚低耦合原则，各层职责明确，便于扩展和维护。

```

┌─────────────────────────────────────────────────────┐
│                    应用层                              │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐     │
│  │ 数据处理应用 │ │ 交易系统应用 │ │ 微服务应用  │     │
│  └─────────────┘ └─────────────┘ └─────────────┘     │
└─────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────┐
│                Java接口层                            │
│  ┌─────────────────────────────────────────────────┐ │
│  │            UBSHcomService                       │ │
│  │  ┌──────────────┐ ┌─────────────┐ ┌─────────────┐ │ │
│  │  │ UBSHcomChannel │UBSHcomRequest│ UBSHcomReplyContext │ │ │
│  │  └──────────────┘ └─────────────┘ └─────────────┘ │ │
│  └─────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────┐
│                JNI桥接层                            │
│  ┌─────────────────────────────────────────────────┐ │
│  │           com_huawei_ock_hcom_v2_service       │ │
│  │    JNI实现：C++方法⟷Java对象映射               │ │
│  └─────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────┐
│               C++服务层                            │
│  ┌─────────────────────────────────────────────────┐ │
│  │            service_imp.cpp                      │ │
│  │        HcomServiceImp / ChannelImp             │ │
│  └─────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────┘

```

### 3.1.2 核心组件设计

**1. UBSHcomService（服务层）**

- 负责服务的创建、启动、停止等生命周期管理
- 支持多协议配置（TCP、UDS、RDMA、UBC等）

**2. UBSHcomChannel（通道层）**

- 管理单个网络连接的生命周期
- 支持双向通信（send/call/reply）和单边操作

**3. UBSHcomRequest/Response（消息层）**

- 定义消息格式和传输协议
- 提供消息编解码能力

**4. JNI桥接层**

- 实现Java对象与C++对象的内存映射
- 处理线程模型转换和异常传递
- 提供高效的数据序列化/反序列化

## 3.2 技术选型

Uniffle项目&大数据服务场景常用语言为：Java、Go，优先考虑支持Java API的封装

| 技术方案    | 优势                         | 劣势                   | 选择理由                                |
| ----------- | ---------------------------- | ---------------------- | --------------------------------------- |
| **传统JNI** | 成熟稳定，JVM官方支持        | 开发复杂，性能开销大   | 作为基础选择，兼容性最佳                |
| **JNA**     | 开发简单，无需生成头文件     | 性能损失较大，功能受限 | 本次实现性能要求高，不适用              |
| **JNR**     | 性能接近JNI，开发相对简单    | 生态相对较少，调试困难 | 作为备选方案，最终选择传统JNI以保证性能 |
| **JavaCPP** | 跨平台支持好，代码生成自动化 | 依赖较多，项目侵入性强 | 不适合本次轻量化API设计                 |

**不选择JNA/JNR的原因：**

1. 性能要求：HCOM对通信性能要求极高，JNI提供了最接近C++原生的性能
2. 功能完整度：JNI提供了最完整的C++库访问能力
3. 调试便利性：JNI的调试工具链更加成熟

## 3.3 功能与性能设计

1. 服务创建与管理流程

```
开始
  ↓
[参数验证] → UBSHcomServiceOptions → 选项校验
  ↓
[JNI调用] → nativeCreate() → C++层对象创建
  ↓
[资源初始化] → 内存池、线程池、PGTable初始化
  ↓
[协议栈初始化] → TCP/UDS/RDMA网络栈配置
  ↓
[回调注册] → 注册各种事件回调函数
  ↓
服务就绪返回
```

**2. 连接管理流程**

**连接建立过程：**

1. 客户端调用`connect()`方法发起连接
2. JNI层封装连接参数转换
3. C++层根据协议类型选择相应的连接策略
4. 创建Channel对象并建立网络连接
5. 注册连接状态回调函数
6. 返回已建立的Channel对象

**连接断开过程：**

1. 调用`disconnect()`或`close()`
2. 发送连接关闭信号
3. 释放网络资源
4. 清理回调函数引用
5. 销毁Channel对象

**3. 消息传输流程**

**同步消息传输：**

```
发送端: call() → JNI层 → C++同步发送 → 等待响应 → 返回结果
接收端: 注册回调 → 接收请求 → 处理数据 → 回复响应
```

## 3.4 安全隐私与DFX设计

不涉及

## 3.5 编程与调用能力

**交付件形态**：hcom-sdk-v2-beiming.24.4.jar

*若本提案相关特性/功能的组件/模块等支持被开发者集成调用（二次开发），则需要提供便捷易用的编程与调用能力。要站在开发者如何进行编程开发、接口调用及系统集成的使用方式上，给出相应的**编程模型定义和设计**，包括各要素的可获取方式和途径。*

### 3.5.1 编程模型基本设计

环境依赖与HCOM 保持一致

加速库和算子：

- HCOM核心库：libhcom.so
- JNI库：libhcom_jni.so

功能验收环境

- 单元测试覆盖率≥60%
- 集成测试场景覆盖所有主要功能

### 3.5.2 接口定义与设计——Java API

#### 1.1 create

**函数定义**

根据类型、名字和可选配置项创建一个服务层的NetService对象。

**实现方法**

public static UBSHcomService create(HcomServiceProtocol t, String name, UBSHcomServiceOptions opt) throws Exception;

**参数说明**

| 参数名 | 数据类型              | 参数类型 | 描述                                                         |
| ------ | --------------------- | -------- | ------------------------------------------------------------ |
| t      | HcomServiceProtocol   | 入参     | UBSHcomService协议类型。                                     |
| name   | String                | 入参     | UBSHcomService的名称。长度范围[1, 64]，只能包含数字、字母、‘_’和‘-’。 |
| opt    | UBSHcomServiceOptions | 入参     | 可选基础配置项。                                             |

**返回值**

成功则返回UBSHcomService类型的实例，否则返回空。

#### 1.2 destroy

**函数定义**

销毁服务，会清理全局map并根据名字销毁对象。

**实现方法**

public static int destroy(String name)

**参数说明**

| 参数名 | 数据类型 | 参数类型 | 描述                                                         |
| ------ | -------- | -------- | ------------------------------------------------------------ |
| name   | String   | 入参     | 要删除的服务对象的名称。长度范围[1, 100]，只能包含数字、字母、‘_’和‘-’。 |

**返回值**

表示函数执行结果，返回值为0则表示销毁成功。

#### 1.3 bind

**函数定义**

服务端绑定监听的url和端口号。

**实现方法**

 public int bind(String listenerUrl, NetServiceListener listener);

**参数说明**

| 参数名      | 数据类型           | 参数类型 | 描述                                                         |
| ----------- | ------------------ | -------- | ------------------------------------------------------------ |
| listenerUrl | String             | 入参     | 要监听的url，格式如下：TCP：tcp://127.0.0.1:9981表示是TVP协议。监听IP为127.0.0.1，port为9981。UDS：uds://file:perm表示是UDS协议。监听文件名为file，如果:perm为空则表示监听抽象文件，否则监听真实文件，perm表示文件权限，例如0600。 |
| handler     | NetServiceListener | 入参     | NetServiceListener接口                                       |

**返回值**

绑定成功返回0，失败返回对应错误码

#### 1.4 start

**函数定义**

启动服务。

**实现方法**

public  int  start()

**参数说明**

无

**返回值**

启动成功返回0，启动失败返回失败错误码。

#### 1.5 connect 

**函数定义**

客户端向服务端发起建链。

**实现方法**

public UBSHcomChannel connect(String serverUrl, UBSHcomConnectOptions opt);

**参数说明**

| 参数名    | 数据类型              | 参数类型 | 描述                  |
| --------- | --------------------- | -------- | --------------------- |
| serverUrl | String                | 入参     | 服务端绑定监听的url。 |
| opt       | UBSHcomConnectOptions | 入参     | 建链配置项。          |

**返回值**

无

#### 1.6 disconnect

**函数定义**

断开链接。

**实现方法**

public void disconnect()

**参数说明**

无

**返回值**

无

#### 1.7 addListener

**函数定义**

向Service中增加listener。

**实现方法**

 void addListener(String url, int workerCount);  

**参数说明**

| 参数名      | 数据类型 | 参数类型 | 描述                                                         |
| ----------- | -------- | -------- | ------------------------------------------------------------ |
| url         | string   | 入参     | 增加的listener监听的url，同bind。                            |
| workerCount | int      | 入参     | 从workerGroup中选取workerCount个线程，与该url建立的连接请求通过这workerCount个线程去处理。 |

**返回值**

无

#### 1.8 setEnableMrCache

**函数定义**

若用户需要使用RNDV，则需要设置为true。

设置RegisterMemoryRegion是否将mr放入pgTable管理。

**实现方法**

void setEnableMrCache(bool enableMrCache);

**参数说明**

| 参数名        | 数据类型 | 参数类型 | 描述                      |
| ------------- | -------- | -------- | ------------------------- |
| enableMrCache | boolen   | 入参     | mr放入pgTable管理标志位。 |

**返回值**

无

#### 1.9 setDeviceIpMask

**函数定义**

设置设备ipMask，用于rdma/ub。

**实现说明**

void setDeviceIpMask(List\<String> ipMasks);

**参数说明**

| 参数名  | 数据类型      | 参数类型 | 描述                   |
| ------- | ------------- | -------- | ---------------------- |
| ipMasks | List\<String> | 入参     | 用于过滤的ipMask集合。 |

**返回值**

无

#### 1.10 registerMemoryRegion

**函数定义**

- 注册一个内存区域，内存将在UBS Comm内部分配。
- 将用户申请的内存，注册到UBS Comm中。

**实现方法**

 int registerMemoryRegion(long size, UBSHcomRegMemoryRegion mr);   

 int registerMemoryRegion(long address, long size, UBSHcomRegMemoryRegion mr);

**参数说明**

| 参数名  | 数据类型               | 参数类型 | 描述                                                         |
| ------- | ---------------------- | -------- | ------------------------------------------------------------ |
| address | long                   | 入参     | 如果有此参数，则是外部申请的内存。                           |
| size    | long                   | 入参     | 需要注册的内存大小，单位byte。方法1：范围为(0, 107374182400]。方法2：范围为(0, 1099511627776]。 |
| mr      | UBSHcomRegMemoryRegion | 出参     | 内存区域结构，包含key、名字、大小、buf等字段。               |

**返回值**

表示函数执行结果，返回值为0则表示注册成功。

#### 1.11 destroyMemoryRegion

**函数定义**

销毁一个内存区域。

**实现方法**

 void destroyMemoryRegion(UBSHcomRegMemoryRegion mr);

**参数说明**

| 参数名 | 数据类型               | 参数类型 | 描述 |
| ------ | ---------------------- | -------- | ---- |
| mr     | UBSHcomRegMemoryRegion |          |      |

**返回值**

无

#### 1.12 registerChannelBrokenHandler

**函数定义**

给UBSHcomService注册断链回调函数。

**实现方法**

public void registerChannelBrokenHandler(HcomServiceChannelBrokenHandler handler, UBSHcomChannelBrokenPolicy policy)

**参数说明**

| 参数名  | 数据类型                        | 参数类型 | 描述                 |
| ------- | ------------------------------- | -------- | -------------------- |
| handler | HcomServiceChannelBrokenHandler | 入参     | 断链回调函数。       |
| policy  | UBSHcomChannelBrokenPolicy      | 入参     | 断链回调策略。枚举值 |

**返回值**

无

#### 1.13 registerRecvHandler

**函数定义**

注册回调函数以处理异步通信收到消息事件。

**实现方法**

public void registerRecvHandler(HcomServiceRecvHandler recvHandler)

**参数说明**

| 参数名      | 数据类型               | 参数类型 | 描述                                   |
| ----------- | ---------------------- | -------- | -------------------------------------- |
| recvHandler | HcomServiceRecvHandler | 入参     | 处理异步通信收数据事件的回调函数句柄。 |

**返回值**

无



#### 1.14 registerSendHandler

**函数定义**

注册回调函数以处理消息发送完成事件。

**实现方法**

public void registerSendHandler(HcomServiceSendHandler sendHandler) 

**参数说明**

| 参数名      | 数据类型               | 参数类型 | 描述                             |
| ----------- | ---------------------- | -------- | -------------------------------- |
| sendHandler | HcomServiceSendHandler | 入参     | 处理发送完成事件的回调函数句柄。 |

**返回值**

无

#### 1.15 registerOneSideHandler

**函数定义**

注册回调函数以处理单边读/写完成事件。

**实现方法**

public void registerOneSideHandler(HcomServiceOneSideDoneHandler oneSideDoneHandler) 

**参数说明**

| 参数名             | 数据类型                      | 参数类型 | 描述                 |
| ------------------ | ----------------------------- | -------- | -------------------- |
| oneSideDoneHandler | HcomServiceOneSideDoneHandler | 入参     | 周期任务处理线程数。 |

**返回值**

无

#### 1.16 send

**函数定义**

- 向对端异步发送一个双边请求消息，并且不等待响应。
- 向对端同步发送一个双边请求消息，并且不等待响应。

**实现方法**

 public int send(UBSHcomRequest req, UBSHcomCallback done); 

public  int send(UBSHcomRequest req);

**参数说明**

| 参数名 | 数据类型        | 参数类型 | 描述                                                         |
| ------ | --------------- | -------- | ------------------------------------------------------------ |
| req    | UBSHcomRequest  | 入参     | 发送给对端的消息message。                                    |
| done   | UBSHcomCallback | 入参     | 回调函数。如果选择nullptr，函数是同步行为。如果定义了回调函数，接口异步读，直接返回。 |

**返回值**

表示函数执行结果，0表示发送成功。

#### 1.17 call

**函数定义**

- 异步模式下，发送一个Request消息，并等待对方回复Response响应消息。
- 同步模式下，发送一个Request消息，并等待对方回复Response响应消息。

**实现方法**

public int call(UBSHcomRequest req, UBSHcomResponse rsp, Callback done); 

public int call(UBSHcomRequest req, UBSHcomResponse rsp);

**参数说明**

| 参数名 | 数据类型        | 参数类型 | 描述                                                         |
| ------ | --------------- | -------- | ------------------------------------------------------------ |
| req    | UBSHcomRequest  | 入参     | 发送给对端的消息请求。                                       |
| rsp    | UBSHcomResponse | 出参     | 对端回复的Response消息。如果消息大小未知，则可以传入空指针，由UBS Comm调用malloc申请内存，并交给用户进行释放。如果响应消息大小已知，则传入已申请的地址。 |
| done   | UBSHcomCallback | 入参     | 回调函数。如果选择nullptr，函数是同步行为。如果定义了回调函数，接口异步读，直接返回。 |

**返回值**

表示函数执行结果，返回值为0则表示发送成功。

#### 1.18 reply

**函数定义**

1. 异步模式下，向对端回复一个消息，配合Call接口使用

2. 同步模式下，向对端回复一个消息，配合Call接口使用

**实现方法**

 public  int reply(UBSHcomReplyContext ctx, UBSHcomRequest req, UBSHcomCallback done);    

 public int reply(UBSHcomReplyContext ctx, UBSHcomRequest req);

**参数说明**

| 参数名 | 数据类型            | 参数类型 | 描述                                                         |
| ------ | ------------------- | -------- | ------------------------------------------------------------ |
| ctx    | UBSHcomReplyContext | 入参     | 消息回复上下文。                                             |
| req    | UBSHcomRequest      | 入参     | 回复发送给对端的请求。                                       |
| done   | UBSHcomCallback     | 入参     | 回调函数。如果选择nullptr，函数是同步行为。如果定义了回调函数，接口异步读，直接返回。 |

**返回值**

表示函数执行结果，0表示发送成功。

#### 1.19 recv

**函数定义**

只用于接收RNDV请求。

**实现方法**

public int recv(UBSHcomServiceContext context, long address, int size, UBSHcomCallback done); 

**参数说明**

| 参数名  | 数据类型              | 参数类型 | 描述                                                         |
| ------- | --------------------- | -------- | ------------------------------------------------------------ |
| context | UBSHcomServiceContext | 入参     | 回调中获得的上下文信息。                                     |
| address | long                  | 入参     | 接收请求的数据地址。                                         |
| size    | int                   | 入参     | 接收请求的数据大小。                                         |
| done    | UBSHcomCallback       | 入参     | 回调函数。如果选择nullptr，函数是同步行为。如果定义了回调函数，接口异步写，直接返回。 |

**返回值**

表示函数执行结果，返回值为0则表示接收请求成功。

#### 1.20 setTlsOptions

**函数定义**

设置TLS可选配置项。

**实现说明**

public void setTlsOptions( UBSHcomTlsOptions tlsOptions);

**参数说明**

| 参数名 | 数据类型          | 参数类型 | 描述            |
| ------ | ----------------- | -------- | --------------- |
| opt    | UBSHcomTlsOptions | 入参     | TLS可选配置项。 |

使用UB自举建链时，暂不支持安全认证和安全加密。

**返回值**

无

#### 1.21 setMaxSendRecvDataCount

**函数定义**

设置发送数据块最大数量，不设置的话默认8192。

**实现说明**

public void SetMaxSendRecvDataCount(int maxSendRecvDataCount);

**参数说明**

| 参数名               | 数据类型 | 参数类型 | 描述                 |
| -------------------- | -------- | -------- | -------------------- |
| maxSendRecvDataCount | int      | 入参     | 发送数据块最大数量。 |

**返回值**

无

*...*

### 3.5.3 编程手册设计

为了帮助开发者能快速上手开发，要设计好本提案相关特性/功能的《编程手册》，要包含哪些内容和章节，单独输出还是共用，在已有的手册中更新还是新输出等。确保最后输出的《编程手册》中有相关变更内容。*

# 4. 缺点和风险

*说明潜在风险（Breaking Change、性能回退、复杂度提升、引入的安全问题）、负面影响（对现有功能/用户的冲击）、实现成本（代码量/维护成本/人力投入）、是否有API或版本兼容性、旧版本迁移方案问题等，给出应对措施。*

# 5. 现有技术

*参考其他项目/社区的类似设计，说明借鉴与差异。*

# 6. 未解决问题

*待社区讨论/决策的开放问题，如硬件适配范围、参数默认值等（需在RFC通过前解决）。*

---

附录

* **参考资料链接。**
* **术语表。**
* **文档更新计划**

