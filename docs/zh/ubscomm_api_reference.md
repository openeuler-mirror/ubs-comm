# API参考

## 介绍<a name="ZH-CN_TOPIC_0000002566158928"></a>

本文主要介绍UBS Comm对外提供的API。可以从两个不同的角度，对UBS Comm的API进行分类：

- 编程语言

    UBS Comm主体使用C++语言开发，对外提供C++ API。为了方便不同场景的开发者使用，UBS Comm还对C++ API做了一层封装，对外提供C和Java API。

- 功能架构

    考虑性能及易用性，UBS Comm使用了“传输层”和“服务层”两层架构。传输层追求极致性能，服务层追求极致易用性。传输层和服务层均提供API，使用传输层或服务层的API均可以独立完成通信功能。传输层仅提供了高性能的通信基础功能，服务层还提供了链路重连、限流、超时检测等常用的高级功能。

由于UBS Comm对外提供的API较多，为方便开发者阅读及理解，本文分为如下几个大章节介绍UBS Comm的API。

- 基础API参考

    介绍应用开发过程中最常用和基础的API，建议使用UBS Comm的开发者对这些API都有所了解。

- 高级API参考

    介绍应用开发过程中不常用的API，开发者可以根据自身场景需要进行查阅。

- 环境变量

    介绍UBS Comm对外提供的环境变量。

- 错误码

    介绍UBS Comm的错误码名称、取值及部分常见错误码的处理方法。

## 基础API参考<a name="ZH-CN_TOPIC_0000002565998504"></a>

### C++ API<a name="ZH-CN_TOPIC_0000002596637931"></a>

#### 服务层API<a name="ZH-CN_TOPIC_0000002596637923"></a>

##### UBSHcomService::Create<a name="ZH-CN_TOPIC_0000002596638365"></a>

**函数定义<a name="section4292195611128"></a>**

根据类型、名字和可选配置项创建一个服务层的NetService对象。

**实现方法<a name="section144356817161"></a>**

static UBSHcomService\* UBSHcomService::Create\(UBSHcomServiceProtocol t, const std::string &name, const UBSHcomServiceOptions &opt = \{\}\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|t|UBSHcomNetDriverProtocol|入参|UBSHcomService协议类型。|
|name|String|入参|UBSHcomService的名称。长度范围[1, 64]，只能包含数字、字母、‘_’和‘-’。|
|opt|UBSHcomServiceOptions|入参|可选基础配置项。|

**返回值<a name="section851917373122"></a>**

成功则返回NetService类型的实例，否则返回空。

##### UBSHcomService::Destroy<a name="ZH-CN_TOPIC_0000002596638703"></a>

**函数定义<a name="section4292195611128"></a>**

销毁服务，会清理全局map并根据名字销毁对象。

**实现方法<a name="section964071915014"></a>**

static int32\_t UBSHcomService::Destroy\(const std::string &name\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|name|String|入参|要删除的服务对象的名称。长度范围[1, 100]，只能包含数字、字母、‘_’和‘-’。|

**返回值<a name="section851917373122"></a>**

表示函数执行结果，返回值为0则表示销毁成功。

##### UBSHcomService::Bind<a name="ZH-CN_TOPIC_0000002596638753"></a>

**函数定义<a name="section4292195611128"></a>**

服务端绑定监听的url和端口号

**实现方法<a name="section964071915014"></a>**

int32\_t UBSHcomService::Bind\(const std::string &listenerUrl, const UBSHcomServiceNewChannelHandler &handler\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|listenerUrl|String|入参|要监听的url，格式如下：TCP：tcp://127.0.0.1:9981表示是TVP协议。监听IP为127.0.0.1，port为9981。在使用IPOverUrma的情况下，监听IP可以为bondingEid，例如：4245:4944:0000:0000:0000:0000:0100:0000。UDS：uds://file:perm表示是UDS协议。监听文件名为file，如果:perm为空则表示监听抽象文件，否则监听真实文件，perm表示文件权限，例如0600。|
|handler|UBSHcomServiceNewChannelHandler|入参|建链回调函数。|

**返回值<a name="section851917373122"></a>**

绑定成功返回0，失败返回对应错误码

##### UBSHcomService::Start<a name="ZH-CN_TOPIC_0000002596637955"></a>

**函数定义<a name="section4292195611128"></a>**

启动服务

**实现方法<a name="section8707131035510"></a>**

int32\_t UBSHcomService::Start\(\)

**参数说明<a name="section8984192751117"></a>**

无

**返回值<a name="section198115833114"></a>**

启动成功返回0，启动失败返回失败错误码。

##### UBSHcomService::Connect<a name="ZH-CN_TOPIC_0000002566158158"></a>

**函数定义<a name="section4292195611128"></a>**

客户端向服务端发起建链。

**实现方法<a name="section8707131035510"></a>**

int32\_t UBSHcomService::Connect\(const std::string &serverUrl, UBSHcomChannelPtr &ch, const UBSHcomConnectOptions &opt = \{\}\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|serverUrl|const std::string|入参|服务端绑定监听的url。|
|ch|UBSHcomChannelPtr|出参|建链成功返回的channel通道。|
|opt|const UBSHcomConnectOptions &|入参|建链配置项。|

**返回值<a name="section198115833114"></a>**

无

##### UBSHcomService::Disconnect<a name="ZH-CN_TOPIC_0000002566158810"></a>

**函数定义<a name="section4292195611128"></a>**

断开链接。

**实现方法<a name="section8707131035510"></a>**

void UBSHcomService::Disconnect\(const UBSHcomChannelPtr &ch\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ch|const UBSHcomChannelPtr|入参|要断开的channel通道。|

**返回值<a name="section198115833114"></a>**

无

##### UBSHcomService::RegisterMemoryRegion<a name="ZH-CN_TOPIC_0000002596637825"></a>

**函数定义<a name="section4292195611128"></a>**

- 注册一个内存区域，内存将在UBS Comm内部分配。
- 将用户申请的内存，注册到UBS Comm中。

**实现方法<a name="section25141526171720"></a>**

- int32\_t UBSHcomService::RegisterMemoryRegion\(uint64\_t size, UBSHcomRegMemoryRegion &mr\)
- int32\_t UBSHcomService::RegisterMemoryRegion\(uintptr\_t address, uint64\_t size, UBSHcomRegMemoryRegion &mr\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|address|uintptr_t|入参|如果有此参数，则是外部申请的内存。|
|size|uint64_t|入参|需要注册的内存大小，单位byte。方法1：范围为(0, 107374182400]。方法2：范围为(0, 1099511627776]。|
|mr|UBSHcomRegMemoryRegion|出参|内存区域结构，包含key、名字、大小、buf等字段。|

>[!NOTE]说明
>若需要放入pgTable管理（通过UBSHcomService::SetEnableMrCache设置为true，默认不放入），则要求首地址\(startAddress\)和尾地址（startAddress+size）都需要16字节对齐，因此用户申请的size需要能被16整除。

**返回值<a name="section851917373122"></a>**

表示函数执行结果，返回值为0则表示注册成功。

##### UBSHcomService::DestroyMemoryRegion<a name="ZH-CN_TOPIC_0000002596758635"></a>

**函数定义<a name="section4292195611128"></a>**

销毁一个内存区域。

**实现方法<a name="section1999816118227"></a>**

void UBSHcomService::DestroyMemoryRegion\(UBSHcomRegMemoryRegion &mr\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|mr|UBSHcomRegMemoryRegion|入参|要销毁的内存区域。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::RegisterChannelBrokenHandler<a name="ZH-CN_TOPIC_0000002596758457"></a>

>[!NOTE]说明
>用户实现的回调函数，内部不能销毁Service及相关的资源。

**函数定义<a name="section4292195611128"></a>**

给UBSHcomService注册断链回调函数。

**实现方法<a name="section8707131035510"></a>**

void UBSHcomService::RegisterChannelBrokenHandler\(const UBSHcomServiceChannelBrokenHandler &handler, const UBSHcomChannelBrokenPolicy policy\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|handler|UBSHcomServiceChannelBrokenHandler|入参|断链回调函数。|
|policy|UBSHcomChannelBrokenPolicy|入参|断链回调策略。|

**返回值<a name="section198115833114"></a>**

无

##### UBSHcomService::RegisterIdleHandler<a name="ZH-CN_TOPIC_0000002596757951"></a>

>[!NOTE]说明
>用户实现的回调函数，内部不能销毁service及相关的资源。

**函数定义<a name="section4292195611128"></a>**

给此UBSHcomService注册worker闲时回调函数

**实现方法<a name="section169444462015"></a>**

void UBSHcomService::RegisterIdleHandler\(const NetServiceIdleHandler &h\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|handler|const UBSHcomServiceIdleHandler|入参|回调函数。|

**返回值<a name="section1864818153381"></a>**

无

>[!NOTE]说明
>
>数据类型解释如下：
>using UBSHcomServiceIdleHandler= std::function<void\(const UBSHcomNetWorkerIndex &\)\>.

##### UBSHcomService::RegisterRecvHandler<a name="ZH-CN_TOPIC_0000002596637873"></a>

>[!NOTE]说明
>
>- 用户实现的回调函数，内部不能销毁Service及相关的资源。
>- 用户需要避免在该回调中死等发送完成事件，应添加超时时间，否则会造成死锁。
>- 用户需要尽量避免在该回调中占用过长时间处理业务，以免影响性能。

**函数定义<a name="section4292195611128"></a>**

注册回调函数以处理异步通信收到消息事件。

**实现方法<a name="section338155855315"></a>**

void UBSHcomService::RegisterRecvHandler\(const UBSHcomServiceRecvHandler &recvHandler\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|recvHandler|UBSHcomServiceRecvHandler|入参|处理异步通信收数据事件的回调函数句柄。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::RegisterSendHandler<a name="ZH-CN_TOPIC_0000002565998578"></a>

>[!NOTE]说明
>用户实现的回调函数，内部不能销毁Service及相关的资源。

**函数定义<a name="section12041359114911"></a>**

注册回调函数以处理消息发送完成事件。

**实现方法<a name="section423613305015"></a>**

void UBSHcomService::RegisterSendHandler\(const UBSHcomServiceSendHandler &sendHandler\)

**参数说明<a name="section11705618500"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|sendHandler|UBSHcomServiceSendHandler|入参|处理发送完成事件的回调函数句柄。|

**返回值<a name="section89006148504"></a>**

无

##### UBSHcomService::RegisterOneSideHandler<a name="ZH-CN_TOPIC_0000002566158256"></a>

>[!NOTE]说明
>用户实现的回调函数，内部不能销毁Service及相关的资源。

**函数定义<a name="section4292195611128"></a>**

注册回调函数以处理单边读/写完成事件。

**实现方法<a name="section131335313568"></a>**

void UBSHcomService::RegisterOneSideHandler\(const UBSHcomServiceOneSideDoneHandler &oneSideDoneHandler\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|oneSideDoneHandler|UBSHcomServiceOneSideDoneHandler|入参|处理单边读/写完成事件的回调函数句柄。|

**返回值<a name="section11833161119446"></a>**

无

##### UBSHcomChannel::Send<a name="ZH-CN_TOPIC_0000002566159050"></a>

>[!NOTE]说明
>
>- 若使用拆包和rndv的功能，需要通过UBSHcomChannel::SetUBSHcomTwoSideThreshold设置拆包和rndv的阈值。
>- 使用rndv，则需要创建service后将UBSHcomService::SetEnableMrCache设置为true（UBSHcomService::RegisterMemoryRegion函数调用前）。

**函数定义<a name="section4292195611128"></a>**

- 向对端异步发送一个双边请求消息，并且不等待响应。
- 向对端同步发送一个双边请求消息，并且不等待响应。

**实现方法<a name="section1860515325571"></a>**

- int32\_t UBSHcomChannel::Send\(const UBSHcomRequest &req, const Callback \*done\)
- int32\_t UBSHcomChannel::Send\(const UBSHcomRequest &req\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|req|UBSHcomRequest|入参|发送给对端的消息message。|
|done|Callback|入参|回调函数。如果选择nullptr，函数是同步行为。如果定义了回调函数，接口异步读，直接返回。|

**返回值<a name="section851917373122"></a>**

表示函数执行结果，0表示发送成功。

##### UBSHcomChannel::Call<a name="ZH-CN_TOPIC_0000002596758671"></a>

>[!NOTE]说明
>
>- rsp中若address字段填了有效内存地址，则回复会被拷贝到该地址上。
>- 若address==NULL，则UBS Comm会通过malloc申请内存，但用户需要自行维护该内存的生命周期，在使用完后通过free释放。
>- 若使用拆包和rndv的功能，需要通过UBSHcomChannel::SetUBSHcomTwoSideThreshold设置拆包和rndv的阈值。
>- 使用rndv，则需要创建service后将UBSHcomService::SetEnableMrCache设置为true（UBSHcomService::RegisterMemoryRegion函数调用前）。

**函数定义<a name="section4292195611128"></a>**

- 异步模式下，发送一个UBSHcomRequest消息，并等待对方回复UBSHcomResponse响应消息。
- 同步模式下，发送一个UBSHcomRequest消息，并等待对方回复UBSHcomResponse响应消息。

**实现方法<a name="section33164411395"></a>**

- int32\_t UBSHcomChannel::Call\(const UBSHcomRequest &req, UBSHcomResponse &rsp, const Callback \*done\)
- int32\_t UBSHcomChannel::Call\(const UBSHcomRequest &req, UBSHcomResponse &rsp\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|req|UBSHcomRequest|入参|发送给对端的消息请求。|
|rsp|UBSHcomResponse|出参|对端回复的UBSHcomResponse消息。如果消息大小未知，则可以传入空指针，由UBS Comm调用malloc申请内存，并交给用户进行释放。如果响应消息大小已知，则传入已申请的地址。|
|done|Callback|入参|回调函数。如果选择nullptr，函数是同步行为。如果定义了回调函数，接口异步读，直接返回。|

**返回值<a name="section851917373122"></a>**

表示函数执行结果，返回值为0则表示发送成功。

##### UBSHcomChannel::Reply<a name="ZH-CN_TOPIC_0000002566159020"></a>

**函数定义<a name="section4292195611128"></a>**

- 异步模式下，向对端回复一个消息，配合Call接口使用

- 同步模式下，向对端回复一个消息，配合Call接口使用

**实现方法<a name="section12476114016714"></a>**

- int32\_t UBSHcomChannel::Reply\(const UBSHcomReplyContext &ctx, const UBSHcomRequest &req, const Callback \*done\)

- int32\_t UBSHcomChannel::Reply\(const UBSHcomReplyContext &ctx, const UBSHcomRequest &req\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ctx|UBSHcomReplyContext|入参|消息回复上下文。|
|req|UBSHcomRequest|入参|回复发送给对端的请求。|
|done|Callback|入参|回调函数。如果选择nullptr，函数是同步行为。如果定义了回调函数，接口异步读，直接返回。|

**返回值<a name="section851917373122"></a>**

表示函数执行结果，0表示发送成功。

##### UBSHcomChannel::Get<a name="ZH-CN_TOPIC_0000002596757913"></a>

**函数定义<a name="section4292195611128"></a>**

- 同步模式下，发送一个读请求给对方。
- 异步模式下，发送一个读请求给对方。

**实现方法<a name="section1672474211472"></a>**

-int32\_t UBSHcomChannel::Get\(const UBSHcomOneSideRequest &req, const Callback \*done\)

- int32\_t UBSHcomChannel::Get\(const UBSHcomOneSideRequest &req\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|req|UBSHcomOneSideRequest|入参|请求信息。可以在调用OneSideHandler之后释放。|
|done|Callback|入参|回调函数。如果选择nullptr，函数是同步行为。如果定义了回调函数，接口异步读，直接返回。|

**返回值<a name="section851917373122"></a>**

表示函数执行结果，返回值为0则表示读请求成功。

##### UBSHcomChannel::Put<a name="ZH-CN_TOPIC_0000002596638313"></a>

**函数定义<a name="section4292195611128"></a>**

- 同步模式下，发送一个写请求给对方。
- 异步模式下，发送一个写请求给对方。

**实现方法<a name="section1672474211472"></a>**

- int32\_t UBSHcomChannel::Put\(const UBSHcomOneSideRequest &req, const Callback \*done\)

- int32\_t UBSHcomChannel::Put\(const UBSHcomOneSideRequest &req\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|req|UBSHcomOneSideRequest|入参|请求信息。可以在调用OneSideHandler之后释放。|
|done|Callback|入参|回调函数。如果选择nullptr，函数是同步行为。如果定义了回调函数，接口异步写，直接返回。|

**返回值<a name="section851917373122"></a>**

表示函数执行结果，返回值为0则表示写请求成功。

##### UBSHcomChannel::Recv<a name="ZH-CN_TOPIC_0000002596637837"></a>

**函数定义<a name="section4292195611128"></a>**

只用于接收RNDV请求。

**实现方法<a name="section1672474211472"></a>**

int32\_t Recv\(const UBSHcomServiceContext &context, uintptr\_t address, uint32\_t size, const Callback \*done = nullptr\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|context|UBSHcomServiceContext|入参|回调中获得的上下文信息。|
|address|uintptr_t|入参|接收请求的数据地址。|
|size|uint32_t|入参|接收请求的数据大小。|
|done|Callback|入参|回调函数。如果选择nullptr，函数是同步行为。如果定义了回调函数，接口异步写，直接返回。|

**返回值<a name="section851917373122"></a>**

表示函数执行结果，返回值为0则表示接收请求成功。

##### UBSHcomChannel::SetFlowControlConfig<a name="ZH-CN_TOPIC_0000002596638571"></a>

**函数定义<a name="section4292195611128"></a>**

设置限流。

**实现方法<a name="section8707131035510"></a>**

int32\_t SetFlowControlConfig\(const FlowCtrlOptions &opt\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|opt|UBSHcomFlowCtrlOptions|入参|流控配置项。|

**返回值<a name="section198115833114"></a>**

返回0表示成功

##### UBSHcomChannel::SetChannelTimeOut<a name="ZH-CN_TOPIC_0000002566158516"></a>

**函数定义<a name="section4292195611128"></a>**

给该channel设置超时时间。未设置时默认超时时间30s。

**实现方法<a name="section169444462015"></a>**

void UBSHcomChannel::SetChannelTimeOut\(int16\_t oneSideTimeout, int16\_t twoSideTimeout\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|oneSideTimeout|int16_t|入参|单边超时时间，单位为秒，0为立即超时，负数为永不超时（通常设置为-1）。范围是[-1, INT16_MAX]。未设置时默认超时时间30s。|
|twoSideTimeout|int16_t|入参|双边超时时间，单位为秒，0为立即超时，负数为永不超时（通常设置为-1）。范围是[-1, INT16_MAX]。未设置时默认超时时间30s。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomChannel::SetUBSHcomTwoSideThreshold<a name="ZH-CN_TOPIC_0000002596757891"></a>

**函数定义<a name="section4292195611128"></a>**

设置双边操作阈值。

**实现方法<a name="section8707131035510"></a>**

int32\_t SetUBSHcomTwoSideThreshold\(const UBSHcomTwoSideThreshold &threshold\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|threshold|UBSHcomTwoSideThreshold|入参|双边操作阈值。|

**返回值<a name="section198115833114"></a>**

返回0表示成功。

##### UBSHcomChannel::GetId<a name="ZH-CN_TOPIC_0000002596757941"></a>

**函数定义<a name="section4292195611128"></a>**

获得channel ID。

**实现方法<a name="section169444462015"></a>**

uint64\_t UBSHcomChannel::GetId\(\)

**参数说明<a name="section8984192751117"></a>**

无

**返回值<a name="section851917373122"></a>**

uint64\_t id信息。

##### UBSHcomChannel::GetPeerConnectPayload<a name="ZH-CN_TOPIC_0000002565998588"></a>

**函数定义<a name="section4292195611128"></a>**

获得建链的payLoad信息。

**实现方法<a name="section169444462015"></a>**

std::string UBSHcomChannel::GetPeerConnectPayload\(\)

**参数说明<a name="section8984192751117"></a>**

无

**返回值<a name="section851917373122"></a>**

playLoad信息。

##### UBSHcomChannel::SetTraceId<a name="ZH-CN_TOPIC_0000002596637981"></a>

**函数定义<a name="section4292195611128"></a>**

设置trace id。

**实现方法<a name="section169444462015"></a>**

void SetTraceId\(const std::string &traceId\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|traceId|std::string|入参|要设置的trace ID。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomServiceContext::Result<a name="ZH-CN_TOPIC_0000002566158784"></a>

**函数定义<a name="section4292195611128"></a>**

获得ctx的结果，表示通信操作的成功与否。

**实现方法<a name="section155052744912"></a>**

SerResult UBSHcomServiceContext::Result\(\) const

**参数说明<a name="section8984192751117"></a>**

无

**返回值<a name="section851917373122"></a>**

ctx的结果。

##### UBSHcomServiceContext::Channel<a name="ZH-CN_TOPIC_0000002565998538"></a>

**函数定义<a name="section4292195611128"></a>**

获得ctx的NetChannel，可以用于向对端回复消息。

**实现方法<a name="section155052744912"></a>**

const UBSHcomChannelPtr &UBSHcomServiceContext::Channel\(\) const

**参数说明<a name="section8984192751117"></a>**

无

**返回值<a name="section851917373122"></a>**

ctx的NetChannel。

##### UBSHcomServiceContext::OpType<a name="ZH-CN_TOPIC_0000002596637947"></a>

**函数定义<a name="section4292195611128"></a>**

获得ctx的操作类型。

**实现方法<a name="section155052744912"></a>**

Operation UBSHcomServiceContext::OpType() const

**参数说明<a name="section8984192751117"></a>**

无

**返回值<a name="section851917373122"></a>**

ctx的操作类型。

##### UBSHcomServiceContext::RspCtx<a name="ZH-CN_TOPIC_0000002566158998"></a>

**函数定义<a name="section4292195611128"></a>**

获得ctx的rspCtx，可以用于接收对端发送call消息后回复消息时当作参数使用。

**实现方法<a name="section155052744912"></a>**

uintptr\_t UBSHcomServiceContext::RspCtx\(\) const

**参数说明<a name="section8984192751117"></a>**

无

**返回值<a name="section851917373122"></a>**

ctx的rspCtx。

##### UBSHcomServiceContext::ErrorCode<a name="ZH-CN_TOPIC_0000002566158220"></a>

**函数定义<a name="section4292195611128"></a>**

获得ctx的errorCode。

**实现方法<a name="section155052744912"></a>**

const int32\_t UBSHcomServiceContext::ErrorCode\(\)

**参数说明<a name="section8984192751117"></a>**

无

**返回值<a name="section851917373122"></a>**

ctx的errorCode

##### UBSHcomServiceContext::OpCode<a name="ZH-CN_TOPIC_0000002566158942"></a>

**函数定义<a name="section4292195611128"></a>**

获得ctx的opCode。

**实现方法<a name="section155052744912"></a>**

uint16\_t UBSHcomServiceContext::OpCode\(\) const

**参数说明<a name="section8984192751117"></a>**

无

**返回值<a name="section851917373122"></a>**

ctx的opCode。

##### UBSHcomServiceContext::MessageData<a name="ZH-CN_TOPIC_0000002566159060"></a>

**函数定义<a name="section4292195611128"></a>**

获得ctx的消息，为对端发送过来的消息。

**实现方法<a name="section155052744912"></a>**

void \*UBSHcomServiceContext::MessageData\(\) const

**参数说明<a name="section8984192751117"></a>**

无

**返回值<a name="section851917373122"></a>**

ctx的消息。

##### UBSHcomServiceContext::MessageDataLen<a name="ZH-CN_TOPIC_0000002565998702"></a>

**函数定义<a name="section4292195611128"></a>**

获得ctx的消息长度。

**实现方法<a name="section155052744912"></a>**

uint32\_t UBSHcomServiceContext::MessageDataLen\(\) const

**参数说明<a name="section8984192751117"></a>**

无

**返回值<a name="section851917373122"></a>**

ctx的消息长度。

##### UBSHcomServiceContext::Clone<a name="ZH-CN_TOPIC_0000002565998996"></a>

**函数定义<a name="section4292195611128"></a>**

将ctx的内容拷贝。

**实现方法<a name="section169444462015"></a>**

static SerResult UBSHcomServiceContext::Clone\(UBSHcomServiceContext &newOne, const UBSHcomServiceContext &oldOne, bool copyData = true\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|newOne|UBSHcomServiceContext|出参|拷贝得到的ctx。|
|oldOne|const UBSHcomServiceContext|入参|被拷贝的ctx。|
|copyData|bool|入参|是否拷贝数据。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示成功。

##### UBSHcomServiceContext::IsTimeout<a name="ZH-CN_TOPIC_0000002596758677"></a>

**函数定义<a name="section4292195611128"></a>**

获得ctx是否超时，表示此次操作是否超时。

**实现方法<a name="section155052744912"></a>**

bool UBSHcomServiceContext::IsTimeout\(\) const

**参数说明<a name="section8984192751117"></a>**

无

**返回值<a name="section851917373122"></a>**

ctx是否超时。

##### UBSHcomServiceContext::Invalidate<a name="ZH-CN_TOPIC_0000002565998824"></a>

**函数定义<a name="section4292195611128"></a>**

将ctx的内容失效。

**实现方法<a name="section155052744912"></a>**

void UBSHcomServiceContext::Invalidate\(\)

**参数说明<a name="section8984192751117"></a>**

无

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::SetEnableMrCache<a name="ZH-CN_TOPIC_0000002596758673"></a>

>[!NOTE]说明
>若用户需要使用RNDV，则需要设置为true。

**函数定义<a name="section4292195611128"></a>**

设置RegisterMemoryRegion是否将mr放入pgTable管理。

**实现说明<a name="section19204713125617"></a>**

void SetEnableMrCache\(bool enableMrCache\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|enableMrCache|bool|入参|mr放入pgTable管理标志位。|

**返回值<a name="section851917373122"></a>**

无

### C API<a name="ZH-CN_TOPIC_0000002596757921"></a>

#### 服务层API<a name="ZH-CN_TOPIC_0000002596758709"></a> 

##### ubs\_hcom\_service\_create<a name="ZH-CN_TOPIC_0000002596638747"></a>

**函数定义<a name="section4292195611128"></a>**

根据类型和名字创建一个服务层的NetService对象。

**实现方法<a name="section87131452418"></a>**

int Service\_Create\(ubs\_hcom\_service\_type t, const char \*name, ubs\_hcom\_service\_options options, ubs\_hcom\_service \*service\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|t|ubs_hcom_service_type|入参|ubs_hcom_service协议类型。|
|name|const char *|入参|ubs_hcom_service的名字。长度范围[1, 64]，只能包含数字、字母、‘_’和‘-’。|
|options|ubs_hcom_service_options|入参|Service配置项。|
|service|ubs_hcom_service|出参|表示创建的ubs_hcom_service对象，如果创建失败返回空。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示发送消息成功。

##### ubs\_hcom\_service\_bind<a name="ZH-CN_TOPIC_0000002596758501"></a>

**函数定义<a name="section4292195611128"></a>**

根据类型和名字创建一个服务层的NetService对象。

**实现方法<a name="section87131452418"></a>**

int ubs\_hcom\_service\_bind\(ubs\_hcom\_service service, const char \*listenerUrl, ubs\_hcom\_service\_channel\_handler h\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|表示创建的ubs_hcom_service对象，如果创建失败返回空。|
|listenerUrl|const char *|入参|监听的URL。对于TCP来说：tcp://127.0.0.1:9981。在使用IPOverUrma的情况下，监听IP可以为bondingEid，例如：4245:4944:0000:0000:0000:0000:0100:0000。对于UDS来说：uds://file:perm。如果有:perm则使用真实文件，perm格式如：0600，没有则使用抽象文件。|
|h|ubs_hcom_service_channel_handler|入参|收到新建链的channel回调。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示bind成功。

##### ubs\_hcom\_service\_start<a name="ZH-CN_TOPIC_0000002596758711"></a>

**函数定义<a name="section4292195611128"></a>**

根据类型和名字创建一个服务层的NetService对象。

**实现方法<a name="section1474145814313"></a>**

int ubs\_hcom\_service\_start\(ubs\_hcom\_service service\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|之前创建的ubs_hcom_service对象。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示创建成功。

##### ubs\_hcom\_service\_destroy<a name="ZH-CN_TOPIC_0000002596637875"></a>

**函数定义<a name="section4292195611128"></a>**

销毁服务，会清理全局map根据名字销毁对象。

**实现方法<a name="section157416011244"></a>**

int ubs\_hcom\_service\_destroy\(ubs\_hcom\_service service, const char \*name\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|需要销毁的ubs_hcom_service对象。|
|name|const char *|入参|需要销毁的ubs_hcom_service对象名字。|

##### ubs\_hcom\_service\_connect<a name="ZH-CN_TOPIC_0000002596758293"></a>

**函数定义<a name="section4292195611128"></a>**

建立与远程服务器的连接，并返回连接通道。

**实现方法<a name="section72946485175"></a>**

int ubs\_hcom\_service\_connect\(ubs\_hcom\_service service, const char \*serverUrl, ubs\_hcom\_channel \*channel, Service\_UBSHcomConnectOptions options\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|之前创建的ubs_hcom_service对象。|
|serverUrl|const char *|入参|连接的远程服务器的IP地址或名称。对于TCP来说：tcp://127.0.0.1:9981。对于UDS来说：uds://file:perm。如果有:perm则使用真实文件，perm格式如：0600，没有则使用抽象文件。|
|channel|ubs_hcom_channel|出参|建链生成的连接通道NetChannel。|
|options|Service_UBSHcomConnectOptions|入参|建链使用的参数选项。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示建链成功。

##### ubs\_hcom\_service\_disconnect<a name="ZH-CN_TOPIC_0000002566158284"></a>

**函数定义<a name="section4292195611128"></a>**

切断与远程服务器的连接。

**实现方法<a name="section72946485175"></a>**

int ubs\_hcom\_service\_disconnect\(ubs\_hcom\_service service, ubs\_hcom\_channel channel\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|之前创建的ubs_hcom_service对象。|
|channel|ubs_hcom_channel|入参|建链生成的连接通道ubs_hcom_channel对象。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示断链成功。

##### ubs\_hcom\_service\_register\_memory\_region<a name="ZH-CN_TOPIC_0000002596637879"></a>

**函数定义<a name="section4292195611128"></a>**

注册一个内存区域，内存将在UBS Comm内部分配。

**实现方法<a name="section1638151172212"></a>**

int ubs\_hcom\_service\_register\_memory\_region\(ubs\_hcom\_service service, uint64\_t size, ubs\_hcom\_memory\_region \*mr\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|初始化创建的ubs_hcom_service对象。|
|size|uint64_t|入参|需要注册的内存大小，单位byte。范围为(0, 107374182400]。|
|mr|ubs_hcom_memory_region|入参|内存区域结构，包含key、名字、大小、buf等字段。|

>[!NOTE]说明
>若需要放入pgTable管理（通过ubs\_hcom\_service\_set\_enable\_mrcache设置为true，默认不放入），则要求首地址\(startAddress\)和尾地址\(startAddress+size\)都需要16字节对齐，因此用户申请的size需要能被16整除。

**返回值<a name="section851917373122"></a>**

表示函数执行结果，返回值为0则表示注册成功。

##### ubs\_hcom\_service\_get\_memory\_region\_info<a name="ZH-CN_TOPIC_0000002596757985"></a>

**函数定义<a name="section4292195611128"></a>**

获得mr的内容。

**实现方法<a name="section1638151172212"></a>**

int ubs\_hcom\_service\_get\_memory\_region\_info\(ubs\_hcom\_memory\_region mr, ubs\_hcom\_mr\_info \*info\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|mr|ubs_hcom_memory_region|入参|mr对象。|
|info|ubs_hcom_mr_info|出参|mr的信息。|

**返回值<a name="section851917373122"></a>**

表示函数执行结果，返回值为0则表示注册成功。

##### ubs\_hcom\_service\_register\_assign\_memory\_region<a name="ZH-CN_TOPIC_0000002565999048"></a>

**函数定义<a name="section4292195611128"></a>**

注册一个内存区域，内存将在UBS Comm外部分配。

**实现方法<a name="section1638151172212"></a>**

int ubs\_hcom\_service\_register\_assign\_memory\_region\(ubs\_hcom\_service service, uintptr\_t address, uint64\_t size, ubs\_hcom\_memory\_region \*mr\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|初始化创建的ubs_hcom_service对象。|
|address|uintptr_t|入参|外部申请的内存地址。|
|size|uint64_t|入参|外部申请的内存大小，单位byte。范围为(0, 1099511627776]。|
|mr|ubs_hcom_memory_region|出参|内存区域结构，包含key、名字、大小、buf等字段。|

**返回值<a name="section851917373122"></a>**

表示函数执行结果，返回值为0则表示注册成功。

##### ubs\_hcom\_service\_destroy\_memory\_region<a name="ZH-CN_TOPIC_0000002566158228"></a>

**函数定义<a name="section4292195611128"></a>**

销毁一个内存区域，内存将在UBS Comm内部分配。

**实现方法<a name="section712111002313"></a>**

int ubs\_hcom\_service\_destroy\_memory\_region\(ubs\_hcom\_service service, ubs\_hcom\_memory\_region mr\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|初始化创建的ubs_hcom_service对象。|
|mr|ubs_hcom_memory_region|入参|内存区域结构，包含key、名字、大小、buf等字段。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_service\_register\_broken\_handler<a name="ZH-CN_TOPIC_0000002596758557"></a>

Service\_RegisterChannelHandler

>[!NOTE]说明
>用户注册的回调函数，不能销毁Service及相关的资源。

**函数定义<a name="section4292195611128"></a>**

注册通道Channel的回调函数，以处理通道建链和断连事件。

**实现方法<a name="section1347512411418"></a>**

void ubs\_hcom\_service\_register\_broken\_handler\(ubs\_hcom\_service service, ubs\_hcom\_service\_channel\_handler h,

ubs\_hcom\_service\_channel\_policy policy, uint64\_t usrCtx\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|之前创建的ubs_hcom_service对象。|
|t|ubs_hcom_service_handler_type|入参|句柄的类型。|
|h|ubs_hcom_service_channel_handler|入参|回调函数句柄。|
|policy|ubs_hcom_service_channel_policy|入参|链路断开时的策略，策略可选。|
|usrCtx|uint64_t|入参|用户上下文。|

**返回值<a name="section851917373122"></a>**

uintptr\_t，返回内部句柄地址。

##### ubs\_hcom\_service\_register\_idle\_handler<a name="ZH-CN_TOPIC_0000002565998472"></a>

>[!NOTE]说明
>用户注册的回调函数，不能销毁Service及相关的资源。

**函数定义<a name="section4292195611128"></a>**

设置NetService的worker闲时回调函数。

**实现方法<a name="section169444462015"></a>**

void ubs\_hcom\_service\_register\_idle\_handler\(ubs\_hcom\_service service, ubs\_hcom\_service\_idle\_handler h, uint64\_t usrCtx\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|ubs_hcom_service对象。|
|h|ubs_hcom_service_idle_handler|入参|worker闲时回调函数。|
|usrCtx|uint64_t|入参|用户上下文，可以在回调函数中使用。|

**返回值<a name="section851917373122"></a>**

内部回调函数地址。

>[!NOTE]说明
>
>数据类型解释如下：
>typedef void \(\*ubs\_hcom\_service\_idle\_handler\)\(uint8\_t wkrGrpIdx, uint16\_t idxInGrp, uint64\_t usrCtx\).

##### ubs\_hcom\_service\_register\_handler<a name="ZH-CN_TOPIC_0000002596757943"></a>

>[!NOTE]说明
>
>- 用户注册的回调函数，不能销毁Service及相关的资源。
>- 用户需要避免在该回调中死等发送完成事件，应添加超时时间，否则会造成死锁。
>- 用户需要尽量避免在该回调中占用过长时间处理业务，以免影响性能。

**函数定义<a name="section4292195611128"></a>**

注册回调函数，以处理通道双边发送完成、单边发送完成、双边收消息事件。

**实现方法<a name="section2099511113158"></a>**

void ubs\_hcom\_service\_register\_handler\(ubs\_hcom\_service service, ubs\_hcom\_service\_handler\_type t, ubs\_hcom\_service\_request\_handler h,

uint64\_t usrCtx\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|之前创建的ubs_hcom_service对象。|
|t|ubs_hcom_service_handler_type|入参|句柄的类型。|
|h|ubs_hcom_service_request_handler|入参|回调函数句柄。|
|usrCtx|uint64_t|入参|用户上下文。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_service\_set\_enable\_mrcache<a name="ZH-CN_TOPIC_0000002566158452"></a>

>[!NOTE]说明
>用户需要使用RNDV，则需要设置为true。

**函数定义<a name="section4292195611128"></a>**

设置RegisterMemoryRegion是否将mr放入pgTable管理。

**实现方法<a name="section2099511113158"></a>**

void ubs\_hcom\_service\_set\_enable\_mrcache\(ubs\_hcom\_service service, bool enableMrCache\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|之前创建的ubs_hcom_service对象。|
|enableMrCache|bool|入参|mr放入pgTable管理标志位。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_channel\_refer<a name="ZH-CN_TOPIC_0000002596758371"></a>

**函数定义<a name="section4292195611128"></a>**

将此NetChannel增加一次引用计数。

**实现方法<a name="section169444462015"></a>**

void ubs\_hcom\_channel\_refer\(ubs\_hcom\_channel channel\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|channel|ubs_hcom_channel|入参|需要增加引用计数的NetChannel。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_channel\_derefer<a name="ZH-CN_TOPIC_0000002565998916"></a>

**函数定义<a name="section4292195611128"></a>**

将此ubs\_hcom\_channel 减少一次引用计数。

**实现方法<a name="section169444462015"></a>**

void ubs\_hcom\_channel\_derefer\(ubs\_hcom\_channel channel\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|channel|ubs_hcom_channel|入参|需要减少引用计数的ubs_hcom_channel 。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_channel\_send<a name="ZH-CN_TOPIC_0000002596757743"></a>

>[!NOTE]说明
>
>- 若使用拆包和rndv的功能，需要通过UBSHcomChannel::SetUBSHcomTwoSideThreshold设置拆包和rndv的阈值。
>- 使用rndv，则需要创建service后将UBSHcomService::SetEnableMrCache设置为true（UBSHcomService::RegisterMemoryRegion函数调用前）。

**函数定义<a name="section4292195611128"></a>**

发送双边消息，不需要对端回复。

**实现方法<a name="section169444462015"></a>**

int ubs\_hcom\_channel\_send\(ubs\_hcom\_channel channel, ubs\_hcom\_channel\_request req, ubs\_hcom\_channel\_callback \*cb\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|channel|ubs_hcom_channel|入参|创建的channel对象。|
|req|ubs_hcom_channel_request|入参|发送双边消息请求。|
|cb|ubs_hcom_channel_callback|入参|nullptr：同步发送。非nullptr：异步发送，发送完成后回调函数。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示成功。

##### ubs\_hcom\_channel\_call<a name="ZH-CN_TOPIC_0000002566158580"></a>

>[!NOTE]说明
>
>- rsp中若address字段填了有效内存地址，则用户回复的信息会被拷贝到该地址上。
>- 若address==NULL，则UBS Comm会通过malloc申请内存，但用户需要自行维护该内存的生命周期，在使用完后通过free释放。
>- 若使用拆包和rndv的功能，需要通过UBSHcomChannel::SetUBSHcomTwoSideThreshold设置拆包和rndv的阈值。
>- 使用rndv，则需要创建service后将UBSHcomService::SetEnableMrCache设置为true（UBSHcomService::RegisterMemoryRegion函数调用前）。

**函数定义<a name="section4292195611128"></a>**

发送双边消息并等待回复，需要对端配合Reply使用。

**实现方法<a name="section169444462015"></a>**

int ubs\_hcom\_channel\_call\(ubs\_hcom\_channel channel, ubs\_hcom\_channel\_request req, ubs\_hcom\_channel\_response \*rsp, ubs\_hcom\_channel\_callback \*cb\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|channel|ubs_hcom_channel|入参|创建的channel对象。|
|req|ubs_hcom_channel_request|入参|发送双边消息请求|
|rsp|ubs_hcom_channel_response|入参|出参，发送双边消息请求后对端回复|
|cb|ubs_hcom_channel_callback|入参|nullptr：同步发送。非nullptr：异步发送，发送完成后回调函数。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示成功。

##### ubs\_hcom\_channel\_reply<a name="ZH-CN_TOPIC_0000002566159070"></a>

**函数定义<a name="section4292195611128"></a>**

回复双边消息，接收端配合Call使用。

**实现方法<a name="section169444462015"></a>**

int ubs\_hcom\_channel\_reply\(ubs\_hcom\_channel channel, ubs\_hcom\_channel\_request req, ubs\_hcom\_channel\_reply\_context ctx, ubs\_hcom\_channel\_callback\*cb\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|channel|ubs_hcom_channel|入参|创建的channel对象。|
|req|ubs_hcom_channel_request|入参|回复数据。|
|ctx|ubs_hcom_channel_reply_context|入参|回复上下文。|
|cb|ubs_hcom_channel_callback|入参|nullptr：同步发送。非nullptr：异步发送，发送完成后回调函数。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示成功。

##### ubs\_hcom\_channel\_put<a name="ZH-CN_TOPIC_0000002596757793"></a>

**数据定义<a name="section4292195611128"></a>**

发送单边写请求。

**实现方法<a name="section169444462015"></a>**

int ubs\_hcom\_channel\_put\(ubs\_hcom\_channel channel, ubs\_hcom\_oneside\_request req, ubs\_hcom\_channel\_callback\*cb\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|channel|ubs_hcom_channel|入参|创建的channel对象。|
|req|ubs_hcom_oneside_request|入参|单边请求。|
|cb|ubs_hcom_channel_callback|入参|异步请求回调函数。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示成功。

##### ubs\_hcom\_channel\_get<a name="ZH-CN_TOPIC_0000002566158728"></a>

**函数定义<a name="section4292195611128"></a>**

发送单边读请求。

**实现方法<a name="section169444462015"></a>**

int ubs\_hcom\_channel\_get\(ubs\_hcom\_channel channel, ubs\_hcom\_oneside\_request req, ubs\_hcom\_channel\_callback\*cb\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|channel|ubs_hcom_channel|入参|创建的channel对象。|
|req|ubs_hcom_oneside_request|入参|单边请求。|
|cb|ubs_hcom_channel_callback|入参|异步请求回调函数。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示成功。

##### ubs\_hcom\_channel\_recv<a name="ZH-CN_TOPIC_0000002596757895"></a>

**函数定义<a name="section4292195611128"></a>**

只用于接收RNDV请求。

**实现方法<a name="section169444462015"></a>**

int ubs\_hcom\_channel\_recv\(ubs\_hcom\_channel channel, ubs\_hcom\_service\_context ctx, uintptr\_t address, uint32\_t size,  [ubs\_hcom\_channel\_callback](#ZH-CN_TOPIC_0000002565999390)  \*cb\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|channel|ubs_hcom_channel|入参|创建的channel对象。|
|ctx|ubs_hcom_service_context|入参|上下文。|
|address|uintptr_t|入参|接收数据地址。|
|size|uint32_t|入参|接收数据大小。|
|cb|ubs_hcom_channel_callback *|入参|异步请求回调。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示成功。

##### ubs\_hcom\_channel\_set\_flowctl\_cfg<a name="ZH-CN_TOPIC_0000002566159038"></a>

**函数定义<a name="section4292195611128"></a>**

给此NetChannel设置流控参数。

**实现方法<a name="section169444462015"></a>**

int Channel\_ConfigFlowControl\(ubs\_hcom\_channel channel, ubs\_hcom\_flowctl\_opts options\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|channel|ubs_hcom_channel|入参|通信渠道。|
|options|ubs_hcom_flowctl_opts|入参|流控参数。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示成功。

##### ubs\_hcom\_channel\_set\_timeout<a name="ZH-CN_TOPIC_0000002596758141"></a>

**函数定义<a name="section4292195611128"></a>**

设置ubs\_hcom\_channel 的双边超时时间。

**实现方法<a name="section155052744912"></a>**

void ubs\_hcom\_channel\_set\_timeout\(ubs\_hcom\_channel channel, int32\_t timeout\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|channel|ubs_hcom_channel|入参|ubs_hcom_channel 。|
|timeout|int32_t|入参|超时时间，单位为秒，0为立即超时，负数为永不超时（一般设置为-1）。范围是[-1, INT16_MAX]。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_channel\_set\_twoside\_threshold<a name="ZH-CN_TOPIC_0000002596757807"></a>

**函数定义<a name="section4292195611128"></a>**

设置拆包和rndv的阈值。

**实现方法<a name="section155052744912"></a>**

int ubs\_hcom\_channel\_set\_twoside\_threshold\(ubs\_hcom\_channel channel, ubs\_hcom\_twoside\_threshold threshold\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|channel|Net_Channel|入参|NetChannel。|
|threshold|ubs_hcom_twoside_threshold|入参|拆包和rndv阈值。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_channel\_get\_id<a name="ZH-CN_TOPIC_0000002596638447"></a>

**函数定义<a name="section4292195611128"></a>**

获取channelId。

**实现方法<a name="section155052744912"></a>**

int ubs\_hcom\_channel\_get\_id\(ubs\_hcom\_channel channel\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|channel|ubs_hcom_channel|入参|通信channel对象。|

**返回值<a name="section851917373122"></a>**

返回0为成功。

##### ubs\_hcom\_context\_get\_channel<a name="ZH-CN_TOPIC_0000002565999060"></a>

**函数定义<a name="section4292195611128"></a>**

通过ctx获得ubs\_hcom\_channel。

**实现方法<a name="section155052744912"></a>**

int ubs\_hcom\_context\_get\_channel\(ubs\_hcom\_service\_context context, ubs\_hcom\_channel \*channel\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|context|ubs_hcom_service_context|入参|回调函数的参数ctx。|
|channel|ubs_hcom_channel|出参|返回得到的ubs_hcom_channel。|

**返回值<a name="section851917373122"></a>**

返回0为成功。

##### ubs\_hcom\_context\_get\_type<a name="ZH-CN_TOPIC_0000002596757749"></a>

**函数定义<a name="section4292195611128"></a>**

通过ctx获得操作类型。

**实现方法<a name="section155052744912"></a>**

int ubs\_hcom\_context\_get\_type\(ubs\_hcom\_service\_context context, ubs\_hcom\_service\_context\_type \*type\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|context|ubs_hcom_service_context|入参|回调函数的参数ctx。|
|type|ubs_hcom_service_context_type|出参|返回得到的ubs_hcom_channel。|

**返回值<a name="section851917373122"></a>**

返回0为成功。

##### ubs\_hcom\_context\_get\_result<a name="ZH-CN_TOPIC_0000002565999144"></a>

**函数定义<a name="section4292195611128"></a>**

通过ctx获得操作结果。

**实现方法<a name="section155052744912"></a>**

int ubs\_hcom\_context\_get\_result\(ubs\_hcom\_service\_context context, int \*result\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|context|ubs_hcom_service_context|入参|回调函数的参数ctx。|
|result|int|出参|操作结果。|

**返回值<a name="section851917373122"></a>**

返回0为成功。

##### ubs\_hcom\_context\_get\_rspctx<a name="ZH-CN_TOPIC_0000002566158114"></a>

**函数定义<a name="section4292195611128"></a>**

通过ctx获得回复消息所需的rspCtx。

**实现方法<a name="section155052744912"></a>**

int ubs\_hcom\_context\_get\_rspctx\(ubs\_hcom\_service\_context context, ubs\_hcom\_channel\_reply\_context \*rspCtx\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|context|ubs_hcom_service_context|入参|回调函数的参数ctx。|
|rspCtx|ubs_hcom_channel_reply_context|出参|回复消息接口所需参数。|

**返回值<a name="section851917373122"></a>**

返回0为成功。

##### ubs\_hcom\_context\_get\_opcode<a name="ZH-CN_TOPIC_0000002596757823"></a>

**函数定义<a name="section4292195611128"></a>**

通过ctx获得OpCode。

**实现方法<a name="section155052744912"></a>**

uint16\_t ubs\_hcom\_context\_get\_opcode\(ubs\_hcom\_service\_context context\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|context|ubs_hcom_service_context|入参|回调函数的参数ctx。|
|返回值|uint16_t|出参|OpCode。|

**返回值<a name="section851917373122"></a>**

返回0为成功。

##### ubs\_hcom\_context\_get\_data<a name="ZH-CN_TOPIC_0000002596758633"></a>

**函数定义<a name="section4292195611128"></a>**

通过ctx获得接收到的消息。

**实现方法<a name="section155052744912"></a>**

void \*ubs\_hcom\_context\_get\_data\(ubs\_hcom\_service\_context context\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|context|ubs_hcom_service_context|入参|回调函数的参数ctx。|

**返回值<a name="section851917373122"></a>**

返回接收到的消息。

##### ubs\_hcom\_context\_get\_datalen<a name="ZH-CN_TOPIC_0000002596758435"></a>

**函数定义<a name="section4292195611128"></a>**

通过ctx获得接收到的消息长度。

**实现方法<a name="section155052744912"></a>**

uint32\_t ubs\_hcom\_context\_get\_datalen\(ubs\_hcom\_service\_context context\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|context|ubs_hcom_service_context|入参|回调函数的参数ctx。|

**返回值<a name="section851917373122"></a>**

返回接收到的消息长度。

#### 传输层API<a name="ZH-CN_TOPIC_0000002565999380"></a> 

##### ubs\_hcom\_driver\_create<a name="ZH-CN_TOPIC_0000002565999346"></a>

**函数定义<a name="section4292195611128"></a>**

根据类型和名字创建一个传输层的HcomDriver对象。

**实现方法<a name="section136264112260"></a>**

int ubs\_hcom\_driver\_create\(ubs\_hcom\_driver\_type t, const char \*name, uint8\_t startOobSvr, ubs\_hcom\_driver \*driver\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|t|ubs_hcom_driver_type|入参|ubs_hcom_driver协议类型，取值范围详见ubs_hcom_driver_type。|
|name|char *|入参|ubs_hcom_driver的名字。长度范围[1, 100]，只能包含数字、字母、‘_’和‘-’。|
|startOobSvr|uint8_t|入参|Server端设置为0，Client端设置为1。|
|driver|ubs_hcom_driver|出参|创建的ubs_hcom_driver实例。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示创建HcomDriver成功。

##### ubs\_hcom\_driver\_set\_ipport<a name="ZH-CN_TOPIC_0000002565999040"></a>

**函数定义<a name="section4292195611128"></a>**

给HcomDriver对象设置OOB的IP和Port。

**实现方法<a name="section8707131035510"></a>**

void ubs\_hcom\_driver\_set\_ipport\(ubs\_hcom\_driver driver, const char \*ip, uint16\_t port\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|driver|ubs_hcom_driver|入参|ubs_hcom_driver对象。|
|ip|const char *|入参|IP。该参数内部有系统函数对IP有效性进行校验。|
|port|uint16_t|入参|端口。范围值[1024, 65535]。|

**返回值<a name="section198115833114"></a>**

无

##### ubs\_hcom\_driver\_get\_ipport<a name="ZH-CN_TOPIC_0000002596638565"></a>

**函数定义<a name="section4292195611128"></a>**

得到HcomDriver对象的OOB的IP和Port。

**实现方法<a name="section8707131035510"></a>**

bool ubs\_hcom\_driver\_get\_ipport\(ubs\_hcom\_driver driver, char \*\*\*ipArray, uint16\_t \*\*portArray, int \*length\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|driver|ubs_hcom_driver|入参|ubs_hcom_driver对象。|
|ipArray|char **|出参|OOB的IP数组。|
|portArray|uint16_t *|出参|OOB的端口数组。|
|length|int|出参|数组长度。|

**返回值<a name="section198115833114"></a>**

返回值为true则表示成功。

>[!NOTE]说明 
>出参ipArray和portArray为内部分配的内存，用户需要在使用完成之后自行释放此内存。

##### ubs\_hcom\_driver\_set\_udsname<a name="ZH-CN_TOPIC_0000002596637855"></a>

**函数定义<a name="section4292195611128"></a>**

给HcomDriver对象设置的OOB type为UDS时的name。

**实现方法<a name="section8707131035510"></a>**

void ubs\_hcom\_driver\_set\_udsname\(ubs\_hcom\_driver driver, const char \*name\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|driver|ubs_hcom_driver|入参|ubs_hcom_driver对象。|
|name|const char *|入参|需要设置的name。长度范围是(0, 96)。|

**返回值<a name="section198115833114"></a>**

无

##### ubs\_hcom\_driver\_add\_uds\_opt<a name="ZH-CN_TOPIC_0000002565998544"></a>

**函数定义<a name="section4292195611128"></a>**

给HcomDriver对象设置的OOB type为UDS时的name和一些参数。

**实现方法<a name="section8707131035510"></a>**

void ubs\_hcom\_driver\_add\_uds\_opt\(ubs\_hcom\_driver driver, ubs\_hcom\_driver\_uds\_listen\_opts option\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|driver|ubs_hcom_driver|入参|ubs_hcom_driver对象。|
|option|ubs_hcom_driver_listen_opts|入参|需要设置的UDS参数。|

**返回值<a name="section198115833114"></a>**

无

##### ubs\_hcom\_driver\_add\_oob\_opt<a name="ZH-CN_TOPIC_0000002565998556"></a>

**函数定义<a name="section4292195611128"></a>**

设置HcomDriver对象的OOB的IP和Port。

**实现方法<a name="section8707131035510"></a>**

void ubs\_hcom\_driver\_add\_oob\_opt\(ubs\_hcom\_driver driver, ubs\_hcom\_driver\_listen\_opts options\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|driver|ubs_hcom_driver|入参|ubs_hcom_driver对象。|
|options|ubs_hcom_driver_listen_opts|出参|需要设置的IP和Port参数。|

**返回值<a name="section198115833114"></a>**

无

##### ubs\_hcom\_driver\_initizalize<a name="ZH-CN_TOPIC_0000002596638385"></a>

**函数定义<a name="section4292195611128"></a>**

根据类型和名字创建一个传输层的HcomDriver对象。

**实现方法<a name="section6298104122711"></a>**

int ubs\_hcom\_driver\_initizalize\(ubs\_hcom\_driver driver, ubs\_hcom\_driver\_opts options\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|driver|ubs_hcom_driver|入参|需要初始化的ubs_hcom_driver。|
|options|ubs_hcom_driver_opts|入参|根据Option，初始化ubs_hcom_driver。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示初始化HcomDriver成功。

##### ubs\_hcom\_driver\_start<a name="ZH-CN_TOPIC_0000002596758705"></a>

**函数描述<a name="section104221811114"></a>**

根据类型和名字创建一个传输层的HcomDriver对象。

**函数定义<a name="section4292195611128"></a>**

int ubs\_hcom\_driver\_start\(ubs\_hcom\_driver driver\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|driver|ubs_hcom_driver|入参|需要开启的ubs_hcom_driver。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示开启HcomDriver成功。

##### ubs\_hcom\_driver\_connect<a name="ZH-CN_TOPIC_0000002566158538"></a>

>[!NOTE]说明
>如果用户实现中需要主动销毁EP，要先调用ubs\_hcom\_ep\_close接口；如果需要减少EP的引用计数，可调用ubs\_hcom\_ep\_destroy函数。

**函数定义<a name="section4292195611128"></a>**

建立与远程服务器的连接，并返回连接创建的EP。

**实现方法<a name="section785602653215"></a>**

- int ubs\_hcom\_driver\_connect\(ubs\_hcom\_driver driver, const char \*payloadData, ubs\_hcom\_endpoint \*ep, uint32\_t flags\)
- int ubs\_hcom\_driver\_connect\_with\_grpno\(ubs\_hcom\_driver driver, const char \*payloadData, ubs\_hcom\_endpoint \*ep, uint32\_t flags, uint8\_t serverGrpNo, uint8\_t clientGrpNo\)
- int ubs\_hcom\_driver\_connect\_to\_ipport\(ubs\_hcom\_driver driver, const char \*serverIp, uint16\_t serverPort, const char \*payloadData, ubs\_hcom\_endpoint \*ep, uint32\_t flags\)
- int ubs\_hcom\_driver\_connect\_to\_ipport\_with\_grpno\(ubs\_hcom\_driver driver, const char \*serverIp, uint16\_t serverPort, const char \*payloadData, ubs\_hcom\_endpoint \*ep, uint32\_t flags, uint8\_t serverGrpNo, uint8\_t clientGrpNo\)
- int ubs\_hcom\_driver\_connect\_to\_ipport\_with\_ctx\(ubs\_hcom\_driver driver, const char \*serverIp, uint16\_t serverPort, const char \*payloadData, ubs\_hcom\_endpoint \*ep, uint32\_t flags, uint8\_t serverGrpNo, uint8\_t clientGrpNo, uint64\_t ctx\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|driver|ubs_hcom_driver|入参|需要连接的ubs_hcom_driver。|
|payloadData|char *|入参|建链时传输到远程服务器的字段，将在ep connected NetCallback中获取到。长度范围[0, 512]。|
|ep|ubs_hcom_endpoint *|出参|连接之后新创建的EP。|
|flags|uint32_t|入参|默认是Net_C_EP_EVENT_POLLING。当创建同步EP时flags设置为Net_C_EP_SELF_POLLING。|
|serverGrpNo|uint8_t|入参|对端EP所在的worker组号。|
|clientGrpNo|uint8_t|入参|本端EP所在的worker组号。|
|serverIp|const char *|入参|对端监听的IP地址。|
|serverPort|uint16_t|入参|对端监听的Port。范围是[1024, 65535]。|
|ctx|uint64_t|入参|secInfo回调时的ctx。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示连接成功。

##### ubs\_hcom\_driver\_stop<a name="ZH-CN_TOPIC_0000002596758663"></a>

**函数定义<a name="section4292195611128"></a>**

停止服务和内部启动的线程。

**实现方法<a name="section13660124153716"></a>**

void ubs\_hcom\_driver\_stop\(ubs\_hcom\_driver driver\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|driver|ubs_hcom_driver|入参|需要停止的ubs_hcom_driver。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_driver\_uninitialize<a name="ZH-CN_TOPIC_0000002566158670"></a>

**函数定义<a name="section4292195611128"></a>**

清理服务创建时的相关资源。

**实现方法<a name="section91511612164020"></a>**

void ubs\_hcom\_driver\_uninitialize\(ubs\_hcom\_driver driver\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|driver|ubs_hcom_driver|入参|需要清理的ubs_hcom_driver。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_driver\_destroy<a name="ZH-CN_TOPIC_0000002596638309"></a>

**函数定义<a name="section104221811114"></a>**

销毁HcomDriver。

**实现方法<a name="section13866253174013"></a>**

void ubs\_hcom\_driver\_destroy\(ubs\_hcom\_driver driver\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|driver|ubs_hcom_driver|入参|需要销毁的ubs_hcom_driver。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_driver\_register\_ep\_handler<a name="ZH-CN_TOPIC_0000002596638081"></a>

>[!NOTE]说明
>用户实现的回调函数，内部不能销毁driver及相关的资源。

**函数定义<a name="section4292195611128"></a>**

注册EP的回调函数，以处理EP建链和断连事件。并把回调函数句柄放入全局句柄管理器。

**实现方法<a name="section194511350317"></a>**

uintptr\_t ubs\_hcom\_driver\_register\_ep\_handler\(ubs\_hcom\_driver driver, ubs\_hcom\_ep\_handler\_type t, ubs\_hcom\_ep\_handler h, uint64\_t usrCtx\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|driver|ubs_hcom_driver|入参|需要注册回调函数的ubs_hcom_driver。|
|t|ubs_hcom_ep_handler_type|入参|句柄的类型。|
|h|ubs_hcom_ep_handler|入参|回调函数的句柄。|
|usrCtx|uint64|入参|用户上下文。|

**返回值<a name="section851917373122"></a>**

uintptr\_t类型，返回内部句柄地址。

##### ubs\_hcom\_driver\_register\_op\_handler<a name="ZH-CN_TOPIC_0000002566158598"></a>

>[!NOTE]说明
>用户实现的回调函数，内部不能销毁driver及相关的资源。

**函数定义<a name="section104221811114"></a>**

注册回调函数，以处理通道双边发送完成、单边发送完成、双边收消息事件。并把回调函数句柄放入全局句柄管理器。

**实现方法<a name="section48031456143110"></a>**

uintptr\_t ubs\_hcom\_driver\_register\_op\_handler\(ubs\_hcom\_driver driver, ubs\_hcom\_op\_handler\_type t, ubs\_hcom\_request\_handler h, uint64\_t usrCtx\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|driver|ubs_hcom_driver|入参|需要注册回调函数的ubs_hcom_driver。|
|t|ubs_hcom_op_handler_type|入参|句柄的类型。|
|h|ubs_hcom_request_handler|入参|回调函数的句柄。|
|usrCtx|uint64_t|入参|用户上下文。|

**返回值<a name="section851917373122"></a>**

uintptr\_t类型，返回内部句柄地址。

##### ubs\_hcom\_driver\_register\_idle\_handler<a name="ZH-CN_TOPIC_0000002596758123"></a>

>[!NOTE]说明
>用户实现的回调函数，内部不能销毁driver及相关的资源。

**函数定义<a name="section4292195611128"></a>**

给HcomDriver对象设置EP闲时回调函数。并把回调函数句柄放入全局句柄管理器。

**实现方法<a name="section8707131035510"></a>**

uintptr\_t ubs\_hcom\_driver\_register\_idle\_handler\(ubs\_hcom\_driver driver, ubs\_hcom\_idle\_handler h, uint64\_t usrCtx\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|driver|ubs_hcom_driver|入参|ubs_hcom_driver对象。|
|h|ubs_hcom_idle_handler|入参|闲时回调函数。|
|usrCtx|uint64_t|入参|带到回调函数中的ctx。|

**返回值<a name="section198115833114"></a>**

内部回调函数地址。

>[!NOTE]说明
> 
>数据类型解释如下：
>typedef void \(\*ubs\_hcom\_idle\_handler\)\(uint8\_t wkrGrpIdx, uint16\_t idxInGrp, uint64\_t usrCtx\)

##### ubs\_hcom\_driver\_register\_secinfo\_provider<a name="ZH-CN_TOPIC_0000002565999406"></a>

>[!NOTE]说明
>用户实现的回调函数，内部不能销毁driver及相关的资源。

**函数定义<a name="section4292195611128"></a>**

给HcomDriver对象设置EP安全信息提供函数。

**实现方法<a name="section8707131035510"></a>**

uintptr\_t ubs\_hcom\_driver\_register\_secinfo\_provider\(ubs\_hcom\_driver driver, ubs\_hcom\_secinfo\_provider provider\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|driver|ubs_hcom_driver|入参|ubs_hcom_driver对象。|
|provider|ubs_hcom_secinfo_provider|入参|安全信息提供函数。|

**返回值<a name="section198115833114"></a>**

内部回调函数地址。

>[!NOTE]说明
>数据类型解释如下：
>typedef int \(\*ubs\_hcom\_secinfo\_provider\)\(uint64\_t ctx, int64\_t \*flag, ubs\_hcom\_driver\_sec\_type \*type, char \*\*output, uint32\_t \*outLen, int \*needAutoFree\)

##### ubs\_hcom\_driver\_register\_secinfo\_validator<a name="ZH-CN_TOPIC_0000002566158362"></a>

>[!NOTE]说明
>用户实现的回调函数，内部不能销毁driver及相关的资源。

**函数定义<a name="section4292195611128"></a>**

给HcomDriver对象设置EP安全信息校验函数。

**实现方法<a name="section8707131035510"></a>**

uintptr\_t ubs\_hcom\_driver\_register\_secinfo\_validator\(ubs\_hcom\_driver driver, ubs\_hcom\_secinfo\_validator validator\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|driver|ubs_hcom_driver|入参|ubs_hcom_driver对象。|
|validator|ubs_hcom_secinfo_validator|入参|安全信息校验函数。|

**返回值<a name="section198115833114"></a>**

内部回调函数地址。

>[!NOTE]说明
> 
>数据类型解释如下：
>typedef int \(\*ubs\_hcom\_secinfo\_validator\)\(uint64\_t ctx, int64\_t flag, const char \*input, uint32\_t inputLen\)

##### ubs\_hcom\_driver\_unregister\_ep\_handler<a name="ZH-CN_TOPIC_0000002565998682"></a>

**函数定义<a name="section4292195611128"></a>**

从全局回调函数句柄管理器中去掉某一个回调函数句柄。

**实现方法<a name="section8707131035510"></a>**

void ubs\_hcom\_driver\_unregister\_ep\_handler\(ubs\_hcom\_ep\_handler\_type t, uintptr\_t handle\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|t|ubs_hcom_ep_handler_type|入参|回调函数类型。0：新建链回调函数1：断链回调函数|
|handle|uintptr_t|入参|回调函数句柄。|

**返回值<a name="section198115833114"></a>**

无

##### ubs\_hcom\_driver\_unregister\_op\_handler<a name="ZH-CN_TOPIC_0000002596638345"></a>

**函数定义<a name="section4292195611128"></a>**

从全局回调函数句柄管理器中去掉某一个回调函数句柄。

**实现方法<a name="section8707131035510"></a>**

void ubs\_hcom\_driver\_unregister\_op\_handler\(ubs\_hcom\_op\_handler\_type t, uintptr\_t handle\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|t|ubs_hcom_op_handler_type|入参|回调函数类型。0：接收消息回调函数1：消息发送回调函数2：单边操作完成回调函数|
|handle|uintptr_t|入参|回调函数句柄。|

**返回值<a name="section198115833114"></a>**

无

##### ubs\_hcom\_driver\_unregister\_idle\_handler<a name="ZH-CN_TOPIC_0000002566158446"></a>

**函数定义<a name="section4292195611128"></a>**

从全局回调函数句柄管理器中去掉某一个回调函数句柄。

**实现方法<a name="section8707131035510"></a>**

void ubs\_hcom\_driver\_unregister\_idle\_handler\(uintptr\_t handle\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|handle|uintptr_t|入参|回调函数句柄。|

**返回值<a name="section198115833114"></a>**

无

##### ubs\_hcom\_driver\_create\_memory\_region<a name="ZH-CN_TOPIC_0000002596758669"></a>

**函数定义<a name="section4292195611128"></a>**

通过HcomDriver对象来创建一个Memory region。

**实现方法<a name="section8707131035510"></a>**

int ubs\_hcom\_driver\_create\_memory\_region\(ubs\_hcom\_driver driver, uint64\_t size, ubs\_hcom\_memory\_region \*mr\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|driver|ubs_hcom_driver|入参|ubs_hcom_driver对象。|
|size|uint64_t|入参|MR的大小。范围为(0, 107374182400]。|
|mr|ubs_hcom_memory_region|出参|创建的MR。|

**返回值<a name="section198115833114"></a>**

返回0为成功。

##### ubs\_hcom\_driver\_create\_assign\_memory\_region<a name="ZH-CN_TOPIC_0000002566159078"></a>

**函数定义<a name="section4292195611128"></a>**

通过HcomDriver对象来创建一个Memory region。

**实现方法<a name="section8707131035510"></a>**

int ubs\_hcom\_driver\_create\_assign\_memory\_region\(ubs\_hcom\_driver driver, uintptr\_t address, uint64\_t size, ubs\_hcom\_memory\_region \*mr\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|driver|ubs_hcom_driver|入参|ubs_hcom_driver对象。|
|address|uintptr_t|入参|内存地址。|
|size|uint64_t|入参|内存的大小。|
|mr|ubs_hcom_memory_region|出参|创建的MR。|

**返回值<a name="section198115833114"></a>**

返回0为成功。

##### ubs\_hcom\_driver\_destroy\_memory\_region<a name="ZH-CN_TOPIC_0000002566159058"></a>

**函数定义<a name="section4292195611128"></a>**

通过HcomDriver对象来销毁一个Memory region。

**实现方法<a name="section8707131035510"></a>**

void ubs\_hcom\_driver\_destroy\_memory\_region\(ubs\_hcom\_driver driver, ubs\_hcom\_memory\_region mr\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|driver|ubs_hcom_driver|入参|ubs_hcom_driver对象。|
|mr|ubs_hcom_memory_region|入参|需要销毁的MR。|

**返回值<a name="section198115833114"></a>**

无

##### ubs\_hcom\_driver\_get\_memory\_region\_info<a name="ZH-CN_TOPIC_0000002596637941"></a>

**函数定义<a name="section4292195611128"></a>**

获取一个MR的信息。

**实现方法<a name="section8707131035510"></a>**

int ubs\_hcom\_driver\_get\_memory\_region\_info\(ubs\_hcom\_memory\_region mr, ubs\_hcom\_memory\_region\_info \*info\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|mr|ubs_hcom_memory_region|入参|创建的MR。|
|info|ubs_hcom_memory_region_info|出参|MR相关的信息。|

**返回值<a name="section198115833114"></a>**

返回0为成功。

##### ubs\_hcom\_ep\_set\_context<a name="ZH-CN_TOPIC_0000002565998474"></a>

**函数定义<a name="section4292195611128"></a>**

给EP设置本端回调函数可使用的ctx。

**实现方法<a name="section8707131035510"></a>**

void ubs\_hcom\_ep\_set\_context\(ubs\_hcom\_endpoint ep, uint64\_t ctx\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|EndPoint。|
|ctx|uint64_t|入参|设置的context。|

**返回值<a name="section198115833114"></a>**

无

##### ubs\_hcom\_ep\_get\_context<a name="ZH-CN_TOPIC_0000002596638121"></a>

**函数定义<a name="section4292195611128"></a>**

获得EP的ctx。

**实现方法<a name="section8707131035510"></a>**

uint64\_t ubs\_hcom\_ep\_get\_context\(ubs\_hcom\_endpoint ep\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|EndPoint。|

**返回值<a name="section198115833114"></a>**

EP的context。

##### ubs\_hcom\_ep\_get\_worker\_idx<a name="ZH-CN_TOPIC_0000002596638697"></a>

**函数定义<a name="section4292195611128"></a>**

获取EP所在的worker group的worker索引。

**实现方法<a name="section8707131035510"></a>**

uint16\_t ubs\_hcom\_ep\_get\_worker\_idx\(ubs\_hcom\_endpoint ep\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|EndPoint。|

**返回值<a name="section198115833114"></a>**

返回worker group的worker索引。

##### ubs\_hcom\_ep\_get\_workergroup\_idx<a name="ZH-CN_TOPIC_0000002565999192"></a>

**函数定义<a name="section4292195611128"></a>**

获取EP所在的worker group索引。

**实现方法<a name="section8707131035510"></a>**

uint8\_t ubs\_hcom\_ep\_get\_workergroup\_idx\(ubs\_hcom\_endpoint ep\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|EndPoint。|

**返回值<a name="section198115833114"></a>**

返回worker group索引。

##### ubs\_hcom\_ep\_get\_listen\_port<a name="ZH-CN_TOPIC_0000002596758703"></a>

**函数定义<a name="section4292195611128"></a>**

获取EP建链时所监听的端口号。

**实现方法<a name="section8707131035510"></a>**

uint32\_t ubs\_hcom\_ep\_get\_listen\_port\(ubs\_hcom\_endpoint ep\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|EndPoint。|

**返回值<a name="section198115833114"></a>**

返回端口号。

##### ubs\_hcom\_ep\_version<a name="ZH-CN_TOPIC_0000002566158744"></a>

**函数定义<a name="section4292195611128"></a>**

获取EP的版本。

**实现方法<a name="section8707131035510"></a>**

uint8\_t ubs\_hcom\_ep\_version\(ubs\_hcom\_endpoint ep\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|EndPoint。|

**返回值<a name="section198115833114"></a>**

返回EP的版本。

##### ubs\_hcom\_ep\_set\_timeout<a name="ZH-CN_TOPIC_0000002565998554"></a>

**函数定义<a name="section4292195611128"></a>**

设置EP的超时时间。

**实现方法<a name="section8707131035510"></a>**

void ubs\_hcom\_ep\_set\_timeout\(ubs\_hcom\_endpoint ep, int32\_t timeout\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|EndPoint。|
|timeout|int32_t|入参|超时时间，单位是秒。0为立刻超时，负数为永不超时。|

**返回值<a name="section198115833114"></a>**

无

##### ubs\_hcom\_ep\_post\_send<a name="ZH-CN_TOPIC_0000002596758097"></a>

**函数定义<a name="section4292195611128"></a>**

向对端发送一个带有op信息的请求。

**实现方法<a name="section124411141193412"></a>**

int ubs\_hcom\_ep\_post\_send\(ubs\_hcom\_endpoint ep, uint16\_t opcode, ubs\_hcom\_send\_request \*req\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|建链创建好的EP对象。|
|opcode|uint16_t|入参|操作码，取值范围[0, 1023]。|
|*req|ubs_hcom_send_request|入参|发送请求信息，使用本地内存来存储数据，数据会被复制，调用后可释放本地内存。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示发送成功。

##### ubs\_hcom\_ep\_post\_send\_with\_opinfo<a name="ZH-CN_TOPIC_0000002596758627"></a>

**函数定义<a name="section4292195611128"></a>**

使用EP发送PostSend消息，带有opInfo。

**实现方法<a name="section8707131035510"></a>**

int ubs\_hcom\_ep\_post\_send\_with\_opinfo\(ubs\_hcom\_endpoint ep, uint16\_t opcode, Hcom\_SendRequest \*req, ubs\_hcom\_opinfo \*opInfo\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|EndPoint。|
|opcode|uint16_t|入参|操作编号。|
|req|ubs_hcom_send_request|入参|需要发送的消息。|
|opInfo|ubs_hcom_opinfo|入参|操作信息。|

**返回值<a name="section198115833114"></a>**

返回0为成功。

##### ubs\_hcom\_ep\_post\_send\_with\_seqno<a name="ZH-CN_TOPIC_0000002566158492"></a>

**函数定义<a name="section4292195611128"></a>**

使用EP发送PostSend消息，带有seqNo。

**实现方法<a name="section8707131035510"></a>**

int ubs\_hcom\_ep\_post\_send\_with\_seqno\(ubs\_hcom\_endpoint ep, uint16\_t opcode, ubs\_hcom\_send\_request \*req, uint32\_t replySeqNo\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|EndPoint。|
|opcode|uint16_t|入参|操作编号。|
|req|ubs_hcom_send_request|入参|需要发送的消息。|
|replySeqNo|uint32_t|入参|序列号。|

**返回值<a name="section198115833114"></a>**

返回0为成功。

##### ubs\_hcom\_ep\_post\_read<a name="ZH-CN_TOPIC_0000002566158496"></a>

**函数定义<a name="section4292195611128"></a>**

向对端发送一个读请求。

**实现方法<a name="section147147813616"></a>**

int ubs\_hcom\_ep\_post\_read\(ubs\_hcom\_endpoint ep, ubs\_hcom\_readwrite\_request \*req\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|建链创建好的EP对象。|
|*req|ubs_hcom_readwrite_request|入参|读请求信息。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示读成功。

##### ubs\_hcom\_ep\_post\_write<a name="ZH-CN_TOPIC_0000002596638481"></a>

**函数定义<a name="section4292195611128"></a>**

向对端发送一个写请求。

**实现方法<a name="section1660211376366"></a>**

int ubs\_hcom\_ep\_post\_write\(ubs\_hcom\_endpoint ep, ubs\_hcom\_readwrite\_request \*req\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|建链创建好的EP对象。|
|*req|ubs_hcom_readwrite_request|入参|写请求信息。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示写成功。

##### ubs\_hcom\_ep\_wait\_completion<a name="ZH-CN_TOPIC_0000002566158170"></a>

**函数定义<a name="section4292195611128"></a>**

等待send，read，write消息完成，只有在EP是NET\_EP\_SELF\_POLLING时生效。

**实现方法<a name="section147147813616"></a>**

int ubs\_hcom\_ep\_wait\_completion\(ubs\_hcom\_endpoint ep, int32\_t timeout\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|EndPoint。|
|timeout|int32_t|入参|超时时间，单位是秒。0为立刻超时，负数为永不超时。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示成功。

##### ubs\_hcom\_ep\_receive<a name="ZH-CN_TOPIC_0000002596757849"></a>

**函数定义<a name="section4292195611128"></a>**

接收对端发送过来的消息。

**实现方法<a name="section147147813616"></a>**

int ubs\_hcom\_ep\_receive\(ubs\_hcom\_endpoint ep, int32\_t timeout, ubs\_hcom\_response\_context \*\*ctx\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|EndPoint。|
|timeout|int32_t|入参|超时时间，单位是秒。0为立刻超时，负数为永不超时。|
|ctx|ubs_hcom_response_context|出参|接收到的消息。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示成功。

##### ubs\_hcom\_ep\_refer<a name="ZH-CN_TOPIC_0000002566158818"></a>

**函数定义<a name="section4292195611128"></a>**

给EP增加一次引用。

**实现方法<a name="section147147813616"></a>**

void ubs\_hcom\_ep\_refer\(ubs\_hcom\_endpoint ep\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|EndPoint。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_ep\_close<a name="ZH-CN_TOPIC_0000002596637823"></a>

>[!NOTE]说明
>如果用户实现中需要主动销毁EP，要先调用ubs\_hcom\_ep\_close接口；如果需要减少EP的引用计数，可调用ubs\_hcom\_ep\_destroy函数。

**函数定义<a name="section4292195611128"></a>**

关闭EP。

**实现方法<a name="section147147813616"></a>**

void ubs\_hcom\_ep\_close\(ubs\_hcom\_endpoint ep\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|EndPoint。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_ep\_destroy<a name="ZH-CN_TOPIC_0000002596757993"></a>

>[!NOTE]说明 
>如果用户实现中需要主动销毁EP，要先调用ubs\_hcom\_ep\_close接口；如果需要减少EP的引用计数，可调用ubs\_hcom\_ep\_destroy函数。

**函数定义<a name="section4292195611128"></a>**

销毁EP。

**实现方法<a name="section147147813616"></a>**

void ubs\_hcom\_ep\_destroy\(ubs\_hcom\_endpoint ep\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|EndPoint。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_err\_str<a name="ZH-CN_TOPIC_0000002596757919"></a>

**函数定义<a name="section4292195611128"></a>**

得到errorCode的解释。

**实现方法<a name="section147147813616"></a>**

const char \*ubs\_hcom\_err\_str\(int16\_t errCode\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|errCode|int16_t|入参|需要翻译的错误码。|

**返回值<a name="section851917373122"></a>**

返回错误码翻译。

##### ubs\_hcom\_mem\_allocator\_create<a name="ZH-CN_TOPIC_0000002596757801"></a>

**函数定义<a name="section104221811114"></a>**

创建一个内存分配器。

**实现方法<a name="section13866253174013"></a>**

int ubs\_hcom\_mem\_allocator\_create\(ubs\_hcom\_memory\_allocator\_type t, ubs\_hcom\_memory\_allocator\_options \*options, ubs\_hcom\_memory\_allocator \*allocator\)

**参数说明\`<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|t|ubs_hcom_memory_allocator_type|入参|分配器类型。0：动态大小1：动态大小和缓存|
|options|ubs_hcom_memory_allocator_options|入参|分配器参数。|
|out|ubs_hcom_memory_allocator|出参|创建的分配器指针。|

**返回值<a name="section198115833114"></a>**

返回值为0则表示成功。

##### ubs\_hcom\_mem\_allocator\_destroy<a name="ZH-CN_TOPIC_0000002596638695"></a>

**函数定义<a name="section4292195611128"></a>**

销毁一个内存分配器。

**实现方法<a name="section91511612164020"></a>**

int ubs\_hcom\_mem\_allocator\_destroy\(ubs\_hcom\_memory\_allocator allocator\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|allocator|ubs_hcom_memory_allocator|入参|需要销毁的内存分配器。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示成功。

##### ubs\_hcom\_mem\_allocator\_set\_mr\_key<a name="ZH-CN_TOPIC_0000002565999362"></a>

**函数定义<a name="section19806151103611"></a>**

给分配器设置memory region key。

**实现方法<a name="section8707131035510"></a>**

int ubs\_hcom\_mem\_allocator\_set\_mr\_key\(ubs\_hcom\_memory\_allocator allocator, uint32\_t mrKey\)

**参数说明<a name="section88061251123620"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|allocator|ubs_hcom_memory_allocator|入参|需要被设置的内存分配器。|
|mrKey|uint32_t|入参|memory region key。范围值(0, UINT32_MAX]。|

**返回值<a name="section198115833114"></a>**

返回值为0则表示成功。

##### ubs\_hcom\_mem\_allocator\_get\_offset<a name="ZH-CN_TOPIC_0000002566158292"></a>

**函数定义<a name="section4292195611128"></a>**

得到地址在分配器内存的偏移值。

**实现方法<a name="section8707131035510"></a>**

int ubs\_hcom\_mem\_allocator\_get\_offset\(ubs\_hcom\_memory\_allocator allocator, uintptr\_t address, uintptr\_t \*offset\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|allocator|ubs_hcom_memory_allocator|入参|内存分配器。|
|address|uintptr_t|入参|内存地址。|
|offset|uintptr_t|出参|偏移值。|

**返回值<a name="section198115833114"></a>**

返回0为成功。

##### ubs\_hcom\_mem\_allocator\_get\_free\_size<a name="ZH-CN_TOPIC_0000002565999336"></a>

**函数定义<a name="section4292195611128"></a>**

得到分配器剩余的内存大小。

**实现方法<a name="section8707131035510"></a>**

int ubs\_hcom\_mem\_allocator\_get\_free\_size\(ubs\_hcom\_memory\_allocator allocator, uintptr\_t \*size\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|allocator|ubs_hcom_memory_allocator|入参|内存分配器。|
|size|uintptr_t|出参|剩余的内存大小。|

**返回值<a name="section198115833114"></a>**

返回0为成功。

##### ubs\_hcom\_mem\_allocator\_allocate<a name="ZH-CN_TOPIC_0000002565998566"></a>

**函数定义<a name="section4292195611128"></a>**

从内存分配器中分配出指定大小的内存。

**实现方法<a name="section8707131035510"></a>**

int ubs\_hcom\_mem\_allocator\_allocate\(ubs\_hcom\_memory\_allocator allocator, uint64\_t size, uintptr\_t \*address, uint32\_t \*key\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|allocator|ubs_hcom_memory_allocator|入参|内存分配器。|
|size|uint64_t|入参|分配的内存大小。|
|address|uintptr_t|出参|分配的内存地址。|
|key|uint32_t|出参|MR Key。|

**返回值<a name="section198115833114"></a>**

返回值为0则表示成功。

##### ubs\_hcom\_mem\_allocator\_free<a name="ZH-CN_TOPIC_0000002566159042"></a>

**函数定义<a name="section4292195611128"></a>**

将从内存分配器中分配的内存释放给分配器。

**实现方法<a name="section8707131035510"></a>**

int ubs\_hcom\_mem\_allocator\_free\(ubs\_hcom\_memory\_allocator allocator, uintptr\_t address\)

>[!NOTE]说明
>使用时防止相同address多次调用该函数。

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|allocator|ubs_hcom_memory_allocator|入参|内存分配器。|
|address|uintptr_t|入参|需要释放的内存地址。|

**返回值<a name="section198115833114"></a>**

返回值为0则表示成功。

##### ubs\_hcom\_set\_log\_handler<a name="ZH-CN_TOPIC_0000002596637967"></a>

**函数定义<a name="section4292195611128"></a>**

设置外部日志。

**实现方法<a name="section8707131035510"></a>**

void ubs\_hcom\_set\_log\_handler\(ubs\_hcom\_log\_handler h\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|h|ubs_hcom_log_handler|入参|外部日志函数。|

**返回值<a name="section198115833114"></a>**

无

>[!NOTE]说明
>
>数据类型解释如下：
>typedef void \(\*ubs\_hcom\_log\_handler\)\(int level, const char \*msg\)

##### ubs\_hcom\_check\_local\_supporr<a name="ZH-CN_TOPIC_0000002596758397"></a>

**函数定义<a name="section4292195611128"></a>**

校验本机是否支持所提供协议，若为RDMA协议且支持的情况下，会返回设备信息。

**实现方法<a name="section8707131035510"></a>**

int ubs\_hcom\_check\_local\_supporr\(ubs\_hcom\_driver\_type t, ubs\_hcom\_device\_info \*info\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|t|ubs_hcom_driver_type|入参|需要校验的协议。|
|info|ubs_hcom_device_info|出参|RDMA设备信息，最大的SGL的iov count。|

**返回值<a name="section198115833114"></a>**

返回值为1则表示支持此协议。

##### ubs\_hcom\_get\_remote\_uds\_info<a name="ZH-CN_TOPIC_0000002565999422"></a>

**函数定义<a name="section4292195611128"></a>**

仅支持服务端且OOB type为UDS时，查询此EP的对端UDS ID信息。

**实现方法<a name="section8707131035510"></a>**

int ubs\_hcom\_get\_remote\_uds\_info\(ubs\_hcom\_endpoint ep, ubs\_hcom\_uds\_id\_info \*idInfo\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|EndPoint。|
|idInfo|ubs_hcom_uds_id_info|出参|对端UDS ID信息。|

**返回值<a name="section198115833114"></a>**

返回值为0则表示成功。

## 高级API参考<a name="ZH-CN_TOPIC_0000002596638259"></a>

### C++API<a name="ZH-CN_TOPIC_0000002565998976"></a>

#### 服务层<a name="ZH-CN_TOPIC_0000002596758569"></a>

##### UBSHcomService::AddWorkerGroup<a name="ZH-CN_TOPIC_0000002565998582"></a>

**函数定义<a name="section4292195611128"></a>**

向Service中增加内存池。

**实现说明<a name="section19204713125617"></a>**

void UBSHcomService::AddWorkerGroup\(uint16\_t workerGroupId, uint32\_t threadCount,const std::pair<uint32\_t, uint32\_t\> &cpuIdsRange, int8\_t priority = 0, uint16\_t multirailIdx = 0\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|workerGroupId|uint16_t|入参|增加的workerGroup的编号ID，用于标识不同的workerGroup。|
|threadCount|uint32_t|入参|该workerGroup中的线程数。|
|cpuIdsRange|const std::pair<uint32_t, uint32_t>|入参|该workerGroup绑定的cpu范围，如{0, 10}表示绑定在0到10号CPU上。|
|priority|int8_t|入参|线程优先级，同线程nice值，范围[-20, 19]，取值越大优先级越低。|
|multirailIdx|uint16_t|入参|该workerGroup绑定的MultiRail索引序号。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::AddListener<a name="ZH-CN_TOPIC_0000002566158994"></a>

**函数定义<a name="section4292195611128"></a>**

向Service中增加listener。

**实现说明<a name="section19204713125617"></a>**

void UBSHcomService::AddListener\(const std::string &url,  uint16\_t workerCount = UINT16\_MAX\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|url|const std::string|入参|增加的listener监听的url，同bind。|
|workerCount|uint16_t|入参|从workerGroup中选取workerCount个线程，与该url建立的连接请求通过这workerCount个线程去处理。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::SetConnectLBPolicy<a name="ZH-CN_TOPIC_0000002596757797"></a>

**函数定义<a name="section4292195611128"></a>**

设置建链负载均衡策略

**实现说明<a name="section19204713125617"></a>**

void UBSHcomService::SetConnectLBPolicy\(UBSHcomServiceLBPolicy lbPolicy\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|lbPolicy|UBSHcomNetDriverLBPolicy|入参|建链时，设置Worker的负载均衡模式。NET_ROUND_ROBIN = 0NET_HASH_IP_PORT = 1|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::SetUBSHcomTlsOptions<a name="ZH-CN_TOPIC_0000002596758689"></a>

**函数定义<a name="section4292195611128"></a>**

设置TLS可选配置项。

**实现说明<a name="section19204713125617"></a>**

void UBSHcomService::SetUBSHcomTlsOptions\(const UBSHcomTlsOptions &opt\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|opt|UBSHcomTlsOptions|入参|TLS可选配置项。|

>[!NOTE]说明 
>使用UB自举建链时，暂不支持安全认证和安全加密。

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::SetConnSecureOpt<a name="ZH-CN_TOPIC_0000002566158792"></a>

**函数定义<a name="section4292195611128"></a>**

链接安全配置项

**实现说明<a name="section19204713125617"></a>**

void UBSHcomService::SetConnSecureOpt\(const UBSHcomConnSecureOptions &opt\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|opt|const UBSHcomConnSecureOptions &|入参|链接安全可选配置项。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::SetTcpUserTimeOutSec<a name="ZH-CN_TOPIC_0000002596638033"></a>

**函数定义<a name="section4292195611128"></a>**

设置TCP套接字选项TCP\_USER\_TIMEOUT。

**实现说明<a name="section19204713125617"></a>**

void UBSHcomService::SetTcpUserTimeOutSec\(uint16\_t timeOutSec\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|timeOutSec|uint16_t|入参|对应TCP_USER_TIMEOUT套接字选项，范围[0, 1024]，0表示永不超时。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::SetTcpSendZCopy<a name="ZH-CN_TOPIC_0000002565999156"></a>

**函数定义<a name="section4292195611128"></a>**

设置TCP发送是否要做内存拷贝。

**实现说明<a name="section19204713125617"></a>**

void UBSHcomService::SetTcpSendZCopy\(bool tcpSendZCopy\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|tcpSendZCopy|bool|入参|true：开启ZCopyfalse：关闭ZCopy|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::SetDeviceIpMask<a name="ZH-CN_TOPIC_0000002596758665"></a>

**函数定义<a name="section4292195611128"></a>**

设置设备ipMask，用于rdma/ub。

**实现说明<a name="section19204713125617"></a>**

`void UBSHcomService::SetDeviceIpMask(const std::vector<std::string> &ipMasks);`

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ipMasks|`const std::vector<std::string>`|入参|用于过滤的ipMask集合。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::SetDeviceIpGroups<a name="ZH-CN_TOPIC_0000002596757973"></a>

**函数定义<a name="section4292195611128"></a>**

设置设备ipGroup。

**实现说明<a name="section19204713125617"></a>**

`void UBSHcomService::SetDeviceIpGroups(const std::vector<std::string> &ipGroups);`

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ipGroups|`const std::vector<std::string>`|入参|设备的ipGroups集合。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::SetCompletionQueueDepth<a name="ZH-CN_TOPIC_0000002565998478"></a>

**函数定义<a name="section4292195611128"></a>**

设置cq队列深度。

**实现说明<a name="section19204713125617"></a>**

void UBSHcomService::SetCompletionQueueDepth\(uint16\_t depth\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|depth|uint16_t|入参|完成队列深度。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::SetSendQueueSize<a name="ZH-CN_TOPIC_0000002566158832"></a>

**函数定义<a name="section4292195611128"></a>**

设置发送队列深度。

**实现说明<a name="section19204713125617"></a>**

void UBSHcomService::SetSendQueueSize\(uint32\_t sqSize\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|sqSize|uint32_t|入参|发送队列深度。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::SetRecvQueueSize<a name="ZH-CN_TOPIC_0000002596638495"></a>

**函数定义<a name="section4292195611128"></a>**

设置接收队列深度。

**实现说明<a name="section19204713125617"></a>**

void UBSHcomService::SetRecvQueueSize\(uint32\_t rqSize\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|rqSize|uint32_t|入参|接收队列深度。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::SetPollingBatchSize<a name="ZH-CN_TOPIC_0000002596638271"></a>

**函数定义<a name="section4292195611128"></a>**

设置批量polling的大小。

**实现说明<a name="section19204713125617"></a>**

void UBSHcomService::SetPollingBatchSize\(uint16\_t pollSize\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|pollSize|uint16_t|入参|批量polling的大小。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::SetEventPollingTimeOutUs<a name="ZH-CN_TOPIC_0000002596638295"></a>

**函数定义<a name="section4292195611128"></a>**

设置event polling的超时时间。

**实现说明<a name="section19204713125617"></a>**

void UBSHcomService::SetEventPollingTimeOutUs\(uint16\_t pollTimeout\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|pollTimeout|uint16_t|入参|event polling超时时间。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::SetTimeOutDetectionThreadNum<a name="ZH-CN_TOPIC_0000002566158278"></a>

**函数定义<a name="section4292195611128"></a>**

设置周期任务处理线程数。

**实现说明<a name="section19204713125617"></a>**

void UBSHcomService::SetTimeOutDetectionThreadNum\(uint32\_t threadNum\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|threadNum|uint32_t|入参|周期任务处理线程数。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::SetMaxConnectionCount<a name="ZH-CN_TOPIC_0000002596638781"></a>

**函数定义<a name="section4292195611128"></a>**

设置最大链接数。

**实现说明<a name="section19204713125617"></a>**

void UBSHcomService::SetMaxConnectionCount\(uint32\_t maxConnCount\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|maxConnCount|uint32_t|入参|最大链接数。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::SetUBSHcomHeartBeatOptions<a name="ZH-CN_TOPIC_0000002566158674"></a>

**函数定义<a name="section4292195611128"></a>**

设置心跳参数配置项。

**实现说明<a name="section19204713125617"></a>**

void UBSHcomService::SetUBSHcomHeartBeatOptions\(const UBSHcomHeartBeatOptions &opt\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|opt|UBSHcomHeartBeatOptions|入参|心跳可选参数项。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::SetUBSHcomMultiRailOptions<a name="ZH-CN_TOPIC_0000002596758701"></a>

**函数定义<a name="section4292195611128"></a>**

设置多路径参数配置项。

**实现说明<a name="section19204713125617"></a>**

void UBSHcomService::SetUBSHcomMultiRailOptions\(const UBSHcomMultiRailOptions &opt\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|opt|UBSHcomMultiRailOptions|入参|多路径参数配置项。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::SetQueuePrePostSize<a name="ZH-CN_TOPIC_0000002566158282"></a>

**函数定义<a name="section4292195611128"></a>**

设置提前下发wr的数量，不设置的话默认64。

**实现说明<a name="section19204713125617"></a>**

void UBSHcomService::SetQueuePrePostSize\(uint32\_t prePostSize\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|prePostSize|uint32_t|入参|预先下发的wr数量。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomService::SetMaxSendRecvDataCount<a name="ZH-CN_TOPIC_0000002565998496"></a>

**函数定义<a name="section4292195611128"></a>**

设置发送数据块最大数量，不设置的话默认8192。

**实现说明<a name="section19204713125617"></a>**

void SetMaxSendRecvDataCount\(uint32\_t maxSendRecvDataCount\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|maxSendRecvDataCount|uint32_t|入参|发送数据块最大数量。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomRegMemoryRegion::GetMemoryKey<a name="ZH-CN_TOPIC_0000002565998608"></a>

**函数定义<a name="section4292195611128"></a>**

获得所有内存池的keys。

**实现说明<a name="section19204713125617"></a>**

void UBSHcomRegMemoryRegion::GetMemoryKey\(UBSHcomMemoryKey &mrKey\)；

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|mrKey|UBSHcomMemoryKey|出参|内存池的keys。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomRegMemoryRegion::GetAddress<a name="ZH-CN_TOPIC_0000002596638757"></a>

**函数定义<a name="section4292195611128"></a>**

获得首个内存池地址。

**实现说明<a name="section19204713125617"></a>**

uintptr\_t UBSHcomRegMemoryRegion::GetAddress\(\)；

**参数说明<a name="section8984192751117"></a>**

无

**返回值<a name="section851917373122"></a>**

地址值。

##### UBSHcomRegMemoryRegion::GetSize<a name="ZH-CN_TOPIC_0000002566159010"></a>

**函数定义<a name="section4292195611128"></a>**

获得首个内存池长度。

**实现说明<a name="section19204713125617"></a>**

uint64\_t UBSHcomRegMemoryRegion::GetSize\(\)；

**参数说明<a name="section8984192751117"></a>**

无

**返回值<a name="section851917373122"></a>**

内存池的长度。

##### UBSHcomRegMemoryRegion::GetHcomMrs<a name="ZH-CN_TOPIC_0000002565998494"></a>

**函数定义<a name="section4292195611128"></a>**

获得内存池组。

**实现说明<a name="section19204713125617"></a>**

std::vector<UBSHcomMemoryRegionPtr\>& UBSHcomRegMemoryRegion::GetHcomMrs\(\)；

**参数说明<a name="section8984192751117"></a>**

无

**返回值<a name="section851917373122"></a>**

返回std::vector<UBSHcomMemoryRegionPtr\>类型的数组。

##### UBSHcomNewCallback<a name="ZH-CN_TOPIC_0000002566159052"></a>

**函数定义<a name="section4292195611128"></a>**

创建Callback函数。

**实现说明<a name="section19204713125617"></a>**

template <typename... Args\> Callback \*UBSHcomNewCallback\(Args... args\)；

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|args|Args|入参|回调函数入参。|

**返回值<a name="section851917373122"></a>**

返回Callback \*函数。

#### 传输层<a name="ZH-CN_TOPIC_0000002566159056"></a>

##### UBSHcomNetDriver::RegisterTLSCaCallback<a name="ZH-CN_TOPIC_0000002596638735"></a> 

###### 接口使用方法<a name="ZH-CN_TOPIC_0000002566158990"></a>

**函数定义<a name="section4292195611128"></a>**

注册建链双向认证的回调函数，用于获取CA证书。

**实现方法<a name="section0906193214152"></a>**

void UBSHcomNetDriver::RegisterTLSCaCallback\(const UBSHcomTLSCaCallback &cb\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|cb|UBSHcomTLSCaCallback|入参|获取CA证书的回调函数。|

**返回值<a name="section851917373122"></a>**

无

**代码样例<a name="section277525020254"></a>**

```xml
int Verify(void *x509, const char *path)
{
    return 0;
}

bool CACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    caPath = certPath + "/CA/cacert.pem";
    cb = std::bind(&Verify, std::placeholders::_1, std::placeholders::_2);
    return true;
}
```

```xml
driver->RegisterTLSCaCallback(std::bind(&CACallback, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
```

###### UBSHcomTLSCaCallback函数类型<a name="ZH-CN_TOPIC_0000002565998550"></a>

**函数定义<a name="section4292195611128"></a>**

using UBSHcomTLSCaCallback = std::function<bool\(const std::string &name, std::string &capath, std::string &crlPath, UBSHcomPeerCertVerifyType &verifyPeerCert, UBSHcomTLSCertVerifyCallback &cb\)\>;

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|name|String|入参|句柄名字。|
|capath|String|出参|提供的CA证书路径。|
|crlPath|String|出参|提供的吊销列表路径。|
|verifyPeerCert|UBSHcomPeerCertVerifyType|出参|证书校验方式：VERIFY_BY_NONE：不验证对方证书。VERIFY_BY_DEFAULT：用UBS Comm默认的方式校验证书。VERIFY_BY_CUSTOM_FUNC：用自定义的函数校验证书。|
|cb|UBSHcomTLSCertVerifyCallback|出参|证书校验函数，当verifyPeerCert值为VERIFY_BY_CUSTOM_FUNC的时候设置。|

**返回值<a name="section851917373122"></a>**

Bool类型，表示回调函数是否执行成功。

- 返回值为true：表示成功。
- 返回值为false：表示失败。

**代码样例<a name="section277525020254"></a>**

```xml
bool CACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    caPath = certPath + "/CA/cacert.pem";
    cb = std::bind(&Verify, std::placeholders::_1, std::placeholders::_2);
    return true;
}
```

###### UBSHcomTLSCertVerifyCallback函数类型<a name="ZH-CN_TOPIC_0000002565999228"></a>

**函数定义<a name="section4292195611128"></a>**

using UBSHcomTLSCertVerifyCallback = std::function<int\(void \*, const char \*\)\>;

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|-|void*|入参|加载之后的证书。|

**返回值<a name="section851917373122"></a>**

表示函数执行结果，返回值为0表示证书验证成功。

##### UBSHcomNetDriver::RegisterTLSCertificationCallback<a name="ZH-CN_TOPIC_0000002596758699"></a>

###### 接口使用方法<a name="ZH-CN_TOPIC_0000002596638599"></a>

**函数定义<a name="section104221811114"></a>**

注册建链双向认证的回调函数，用于获取公钥证书。

**实现方法<a name="section483016141204"></a>**

void UBSHcomNetDriver::RegisterTLSCertificationCallback\(const UBSHcomTLSCertificationCallback &cb\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|cb|UBSHcomTLSCertificationCallback|入参|获取公钥的回调函数。|

**返回值<a name="section851917373122"></a>**

无

**代码样例<a name="section277525020254"></a>**

```xml
bool CertCallback(const std::string &name, std::string &value)
{
    value = certPath + "/client/cert.pem";
    return true;
}
```

```xml
driver->RegisterTLSCertificationCallback( std::bind(&CertCallback, std::placeholders::_1, std::placeholders::_2));
```

###### UBSHcomTLSCertificationCallback函数类型<a name="ZH-CN_TOPIC_0000002565999136"></a>

**函数定义<a name="section104221811114"></a>**

using UBSHcomTLSCertificationCallback = std::function<bool\(const std::string &name, std::string &path\)\>;

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|name|String|入参|句柄名字。|
|path|String|出参|提供的公钥证书路径。|

**返回值<a name="section851917373122"></a>**

Bool类型，表示回调函数是否执行成功。

- 返回值为true：表示成功。
- 返回值为false：表示失败。

**代码样例<a name="section277525020254"></a>**

```xml
bool CertCallback(const std::string &name, std::string &value)
{
    value = certPath + "/client/cert.pem";
    return true;
}
```

##### UBSHcomNetDriver::RegisterTLSPrivateKeyCallback<a name="ZH-CN_TOPIC_0000002566158160"></a>

###### 接口使用方法<a name="ZH-CN_TOPIC_0000002596638435"></a>

**函数定义<a name="section4292195611128"></a>**

注册建链双向认证的回调函数，用户获取私钥证书。

**实现方法<a name="section9177825164020"></a>**

void UBSHcomNetDriver::RegisterTLSPrivateKeyCallback\(const UBSHcomTLSPrivateKeyCallback &cb\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|cb|UBSHcomTLSPrivateKeyCallback|入参|私钥回调函数。|

**返回值<a name="section851917373122"></a>**

无

**代码样例<a name="section277525020254"></a>**

```xml
void Erase(void *pass, int len) {}

bool PrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len, UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "xxxx";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = certPath + "/client/key.pem";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);
    return true;
}
```

```xml
driver->RegisterTLSPrivateKeyCallback(std::bind(&PrivateKeyCallback, std::placeholders::_1,  std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
```

###### UBSHcomTLSPrivateKeyCallback函数类型<a name="ZH-CN_TOPIC_0000002565999184"></a>

**函数定义<a name="section4292195611128"></a>**

using UBSHcomTLSPrivateKeyCallback = std::function<bool\(const std::string &name, std::string &path, void \*&password, int &length, UBSHcomTLSEraseKeypass &erase\)\>;

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|name|String|入参|句柄名字。|
|path|String|出参|提供的私钥证书路径。|
|password|void*|出参|私钥加载的明文密码。|
|length|int|出参|私钥加载的密码长度。|
|erase|UBSHcomTLSEraseKeypass|出参|擦除私钥密码的回调函数，当加载完私钥的时候调用。|

**返回值<a name="section851917373122"></a>**

Bool类型，表示回调函数是否执行成功。

- 返回值为true表示成功。
- 返回值为false表示失败。

**代码样例<a name="section277525020254"></a>**

```xml
void Erase(void *pass, int len) {}

bool PrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len, UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "xxxx";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = certPath + "/client/key.pem";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);
    return true;
}
```

###### UBSHcomTLSEraseKeypass函数类型<a name="ZH-CN_TOPIC_0000002596757771"></a>

**函数定义<a name="section4292195611128"></a>**

using UBSHcomTLSEraseKeypass = std::function<void\(void \*, int\)\>;

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|password|void*|出参|私钥加载的明文密码。|
|length|int|出参|私钥加载的密码长度。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomNetDriver::RegisterPskUseSessionCb<a name="ZH-CN_TOPIC_0000002566158922"></a>

###### 接口使用方法<a name="ZH-CN_TOPIC_0000002596638705"></a>

**函数定义<a name="section4292195611128"></a>**

供Client端注册PSK回调函数。

**实现方法<a name="section105036585115"></a>**

void UBSHcomNetDriver::RegisterPskUseSessionCb\(const UBSHcomPskUseSessionCb &cb\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|cb|UBSHcomPskUseSessionCb|入参|预共享密钥回调函数。|

**返回值<a name="section851917373122"></a>**

无。

###### UBSHcomPskUseSessionCb函数类型<a name="ZH-CN_TOPIC_0000002596758455"></a>

**函数定义<a name="section4292195611128"></a>**

using UBSHcomPskUseSessionCb = std::function<int\(void \*ssl, const void \*md, const unsigned char \*\*id, size\_t \*idlen, void \*\*sess\)\>;

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ssl|void*|入参|SSL连接对象。|
|md|void*|入参|摘要算法。|
|id|char *|出参|预共享密钥身份标识。|
|idlen|size_t|出参|预共享密钥身份标识长度。|
|sess|void*|出参|SSL会话对象。|

**返回值<a name="section851917373122"></a>**

int类型。

- 1：表示回调函数执行成功。
- 0：表示回调函数执行失败。

##### UBSHcomNetDriver::RegisterPskFindSessionCb<a name="ZH-CN_TOPIC_0000002596638507"></a>

###### 接口使用方法<a name="ZH-CN_TOPIC_0000002596758223"></a>

**函数定义<a name="section4292195611128"></a>**

供Server端注册PSK回调函数。

**实现方法<a name="section105036585115"></a>**

void UBSHcomNetDriver::RegisterPskFindSessionCb\(const UBSHcomPskFindSessionCb &cb\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|cb|UBSHcomPskFindSessionCb|入参|预共享密钥回调函数。|

**返回值<a name="section851917373122"></a>**

无。

###### UBSHcomPskFindSessionCb函数类型<a name="ZH-CN_TOPIC_0000002596637909"></a>

**函数定义<a name="section4292195611128"></a>**

using UBSHcomPskFindSessionCb = std::function<int\(void \*ssl, const unsigned char \*identity, size\_t identity\_len, void \*\*sess\)\>;

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ssl|void*|入参|SSL连接对象。|
|identity|char *|入参|预共享密钥身份标识。|
|identity_len|size_t|入参|预共享密钥身份标识长度。|
|sess|void*|出参|SSL会话对象。|

**返回值<a name="section851917373122"></a>**

int类型，1表示回调函数执行成功，0表示执行失败。

##### UBSHcomNetDriver::RegisterEndpointSecInfoProvider<a name="ZH-CN_TOPIC_0000002565998758"></a>

**函数定义<a name="section4292195611128"></a>**

给UBSHcomNetDriver对象设置EP安全信息提供函数。

**实现方法<a name="section8707131035510"></a>**

void UBSHcomNetDriver::RegisterEndpointSecInfoProvider\(const UBSHcomNetDriverEndpointSecInfoProvider &provider\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|provider|UBSHcomNetDriverEndpointSecInfoProvider|入参|安全信息提供函数。|

**返回值<a name="section198115833114"></a>**

无

>[!NOTE]说明
>
>数据类型解释如下：
>using UBSHcomNetDriverEndpointSecInfoProvider = std::function<int\(uint64\_t ctx, int64\_t &flag, UBSHcomNetDriverSecType &type,   char \*&output, uint32\_t &outLen, bool &needAutoFree\)\>;
>其中，outLen的有效范围为\(0,2147483646\]。

##### UBSHcomNetDriver::RegisterEndpointSecInfoValidator<a name="ZH-CN_TOPIC_0000002565998696"></a>

**函数定义<a name="section4292195611128"></a>**

给UBSHcomNetDriver对象设置EP安全信息校验函数。

**实现方法<a name="section8707131035510"></a>**

void UBSHcomNetDriver::RegisterEndpointSecInfoValidator\(const UBSHcomNetDriverEndpointSecInfoValidator &validator\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|validator|UBSHcomNetDriverEndpointSecInfoValidator|入参|安全信息校验函数。|

**返回值<a name="section198115833114"></a>**

无

>[!NOTE]说明
>
>数据类型解释如下：
>using UBSHcomNetDriverEndpointSecInfoValidator =   std::function<int\(uint64\_t ctx, int64\_t flag, const char \*input, uint32\_t inputLen\)\>;

##### UBSHcomNetEndpoint::PostSendRawSgl<a name="ZH-CN_TOPIC_0000002565999414"></a>

**函数定义<a name="section4292195611128"></a>**

发送一个不带opcode和header的请求给对方，对方将触发新的请求回调，也不带opcode和header，当客户有自己定义的header时可以使用。

**实现方法<a name="section199474361515"></a>**

NResult UBSHcomNetEndpoint::PostSendRawSgl\(const UBSHcomNetTransSglRequest &request,uint32\_t seqNo\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|request|UBSHcomNetTransSglRequest|入参|请求信息，填入本地注册MR，按本地MR顺序发送到同一个远端MR，调用后即可释放，rKey/rAddress不需要赋值。|
|seqNo|uint32_t|入参|对方要回复的seqNo必须大于0，对方可以从context.Header().seqNo中获取它。如果seqNo为0，则生成自动递增的数字。在同步发送消息的情况下，请求和响应的seqNo相等。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示发送消息成功。

>[!NOTE]说明
>
>- 如果NET\_EP\_SELF\_POLLING未设置，则只发出发送请求，不等待发送请求完成情况。
>- 如果NET\_EP\_SELF\_POLLING设置，则发出发送请求并等待发送到达对端。

##### UBSHcomNetEndpoint::ReceiveRaw<a name="ZH-CN_TOPIC_0000002566158708"></a>

**函数定义<a name="section4292195611128"></a>**

获得发送请求应答的响应，不包含header和opCode，默认超时生效。

**实现方法<a name="section173381131157"></a>**

NResult UBSHcomNetEndpoint::ReceiveRaw\(UBSHcomNetResponseContext &ctx\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ctx|UBSHcomNetResponseContext|出参|响应消息的ctx。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示发送消息成功。

##### UBSHcomNetEndpoint::EstimatedEncryptLen<a name="ZH-CN_TOPIC_0000002596637809"></a>

**函数定义<a name="section4292195611128"></a>**

输入原始数据大小的估计加密长度。

**实现方法<a name="section327619492449"></a>**

uint64\_t UBSHcomNetEndpoint::EstimatedEncryptLen\(uint64\_t rawLen\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|rawLen|uint64_t|入参|原始数据长度。范围是(0,  18446744073709551571]。|

**返回值<a name="section851917373122"></a>**

返回uint64\_t类型，表示数据加密长度。

##### UBSHcomNetEndpoint::Encrypt<a name="ZH-CN_TOPIC_0000002565999068"></a>

**函数定义<a name="section4292195611128"></a>**

加密数据。

**实现方法<a name="section910515196454"></a>**

NResult UBSHcomNetEndpoint::Encrypt\(const void \*rawData, uint64\_t rawLen, void \*cipher, uint64\_t &cipherLen\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|rawData|void *|入参|原始数据地址。|
|rawLen|uint64_t|入参|原始数据长度。|
|cipher|void *|出参|加密后数据地址。|
|cipherLen|uint64_t|出参|加密后数据长度。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示加密成功。

##### UBSHcomNetEndpoint::EstimatedDecryptLen<a name="ZH-CN_TOPIC_0000002565999342"></a>

**函数定义<a name="section4292195611128"></a>**

输出原始数据大小。

**实现方法<a name="section21041447104518"></a>**

uint64\_t UBSHcomNetEndpoint::EstimatedDecryptLen\(uint64\_t cipherLen\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|cipherLen|uint64_t|入参|加密后的数据长度。|

**返回值<a name="section851917373122"></a>**

返回uint64\_t类型，表示解密后的原始数据长度。

##### UBSHcomNetEndpoint::Decrypt<a name="ZH-CN_TOPIC_0000002596638469"></a>

**函数定义<a name="section4292195611128"></a>**

解密数据。

**实现方法<a name="section152241229204912"></a>**

NResult UBSHcomNetEndpoint::Decrypt\(const void \* cipher, uint64\_t cipherLen, void \*rawData, uint64\_t &rawLen\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|cipher|void *|入参|待解密的数据地址。|
|cipherLen|uint64_t|入参|待解密的数据长度。|
|rawData|void *|出参|解密后，原始数据地址。|
|rawLen|uint64_t|出参|解密后，原始数据长度。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示解密成功。

##### UBSHcomNetEndpoint::SendFds<a name="ZH-CN_TOPIC_0000002596758551"></a>

**函数定义<a name="section4292195611128"></a>**

发送共享文件的句柄，该接口只支持在SHM协议下使用。

**实现方法<a name="section734410110445"></a>**

NResult UBSHcomNetEndpoint::SendFds\(int fds\[\], uint32\_t len\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|fds|int[]|入参|需要发送的句柄数组。|
|len|uint32_t|入参|发送的句柄数量。范围是[1, 4]。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示句柄发送成功。

##### UBSHcomNetEndpoint::ReceiveFds<a name="ZH-CN_TOPIC_0000002565998506"></a>

**函数定义<a name="section104221811114"></a>**

接收共享文件的句柄，该接口只支持在SHM协议下使用。

**实现方法<a name="section9427182618446"></a>**

NResult UBSHcomNetEndpoint::ReceiveFds\(int fds\[\], uint32\_t len\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|fds|int[]|入参|需要接收的句柄数组。|
|len|uint32_t|入参|接收的句柄数量。范围是[1, 4]。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示句柄发送成功。

##### UBSHcomNetOutLogger::Instance<a name="ZH-CN_TOPIC_0000002596637839"></a>

**函数定义<a name="section104221811114"></a>**

创建外部日志对象。

**实现方法<a name="section9427182618446"></a>**

static UBSHcomNetOutLogger \*UBSHcomNetOutLogger::Instance\(\)

**参数说明<a name="section8984192751117"></a>**

无

**返回值<a name="section851917373122"></a>**

返回外部日志导入对象。

##### UBSHcomNetOutLogger::SetLogLevel<a name="ZH-CN_TOPIC_0000002596638039"></a>

**函数定义<a name="section104221811114"></a>**

- 设置外部日志对象日志等级，设置为环境变量HCOM\_SET\_LOG\_LEVEL。
- 设置外部日志对象日志等级，大于等于此等级的日志将被打印。

    日志等级如下：

    - 0：debug
    - 1：info
    - 2：warn
    - 3：error

**实现方法<a name="section9427182618446"></a>**

- static void UBSHcomNetOutLogger::SetLogLevel\(\)
- static void UBSHcomNetOutLogger::SetLogLevel\(int level\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|level|int|入参|日志等级。范围是[0, 3]。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomNetOutLogger::SetExternalLogFunction<a name="ZH-CN_TOPIC_0000002565999248"></a>

**函数定义<a name="section104221811114"></a>**

设置外部日志对象的外部日志函数。

**实现方法<a name="section9427182618446"></a>**

void UBSHcomNetOutLogger::SetExternalLogFunction\(ExternalLog func\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|func|ExternalLog|入参|外部日志函数。|

**返回值<a name="section851917373122"></a>**

无

>[!NOTICE]说明
> 
>数据类型解释如下：
>typedef void \(\*ExternalLog\)\(int level, const char \*msg\).

##### UBSHcomNetOutLogger::Print<a name="ZH-CN_TOPIC_0000002596638419"></a>

**函数定义<a name="section104221811114"></a>**

打印日志。

**实现方法<a name="section9427182618446"></a>**

static inline void UBSHcomNetOutLogger::Print\(int level, const char \*msg\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|level|int|入参|日志等级。范围是[0, 3]。|
|msg|const char *|入参|日志内容。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomNetOutLogger::Log<a name="ZH-CN_TOPIC_0000002596758653"></a>

**函数定义<a name="section104221811114"></a>**

打印日志，如果有外部日志函数，则使用外部日志函数。

**实现方法<a name="section9427182618446"></a>**

void UBSHcomNetOutLogger::Log\(int level, const std::ostringstream &oss\) const

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|level|int|入参|日志等级。范围是[0, 3]。|
|oss|const std::ostringstream|入参|日志内容。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomNetOutLogger::GetLogLevel<a name="ZH-CN_TOPIC_0000002596638685"></a>

**函数定义<a name="section104221811114"></a>**

获取当日志打印等级。

**实现方法<a name="section9427182618446"></a>**

int UBSHcomNetOutLogger::GetLogLevel\(\)

**参数说明<a name="section8984192751117"></a>**

无

**返回值<a name="section851917373122"></a>**

返回日志等级。

##### UBSHcomNetAtomicState::Get<a name="ZH-CN_TOPIC_0000002596757879"></a>

**函数定义<a name="section4292195611128"></a>**

获得原子状态。

**实现方法<a name="section660624014504"></a>**

T Get\(\) const

**参数说明<a name="section8984192751117"></a>**

无

**返回值<a name="section851917373122"></a>**

获得原子状态。

##### UBSHcomNetAtomicState::Set<a name="ZH-CN_TOPIC_0000002566158766"></a>

**函数定义<a name="section4292195611128"></a>**

设置原子状态。

**实现方法<a name="section660624014504"></a>**

void Set\(T newState\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|newState|T|入参|需要设置的原子状态。|

**返回值<a name="section851917373122"></a>**

无

##### UBSHcomNetAtomicState::CAS<a name="ZH-CN_TOPIC_0000002596638649"></a>

**函数定义<a name="section4292195611128"></a>**

原子状态比较并交换。

**实现方法<a name="section660624014504"></a>**

bool CAS\(T oldState, T newState\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|oldState|T|入参|旧原子状态。|
|newState|T|入参|新原子状态。|

**返回值<a name="section851917373122"></a>**

布尔值。检查mState是否等于oldState。如果是，则将其设置为newState，并返回true；否则不做任何修改，返回false。

##### UBSHcomNetAtomicState::Compare<a name="ZH-CN_TOPIC_0000002596638713"></a>

**函数定义<a name="section4292195611128"></a>**

原子状态比较。

**实现方法<a name="section660624014504"></a>**

bool Compare\(T state\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|state|T|入参|原子状态。|

**返回值<a name="section851917373122"></a>**

布尔值。检查mState是否等于state，如果是返回true；否则返回false。

#### 组播API<a name="ZH-CN_TOPIC_0000002581233072"></a>

##### PublisherService::Create<a name="ZH-CN_TOPIC_0000002611712939"></a>

**函数定义<a name="section4292195611128"></a>**

创建发布者服务。

**实现方法<a name="section660624014504"></a>**

static PublisherService \*Create\(const std::string &name, const MulticastServiceOptions &opt = \{\}\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|name|const std::string|入参|需要创建的服务名称。|
|opt|const MulticastServiceOptions|入参|服务创建所需的配置项。|

**返回值<a name="section851917373122"></a>**

成功时返回创建好的PublisherService实例，失败时返回空。

##### PublisherService::Detroy<a name="ZH-CN_TOPIC_0000002611632839"></a>

**函数定义<a name="section4292195611128"></a>**

销毁发布者服务。

**实现方法<a name="section660624014504"></a>**

static int32\_t Destroy\(const std::string &name\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|name|const std::string|入参|需要销毁的服务名称。|

**返回值<a name="section851917373122"></a>**

in32\_t。成功时返回0，失败时返回错误码。

##### PublisherService::Start<a name="ZH-CN_TOPIC_0000002581392994"></a>

**函数定义<a name="section4292195611128"></a>**

启动发布者服务。

**实现方法<a name="section660624014504"></a>**

int32\_t Start\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

in32\_t。成功时返回0，失败时返回错误码。

##### PublisherService::Stop<a name="ZH-CN_TOPIC_0000002581233074"></a>

**函数定义<a name="section4292195611128"></a>**

停止发布者服务。

**实现方法<a name="section660624014504"></a>**

void Stop\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

无。

##### PublisherService::CreatePublisher<a name="ZH-CN_TOPIC_0000002611712941"></a>

**函数定义<a name="section4292195611128"></a>**

创建发布者publisher。

**实现方法<a name="section660624014504"></a>**

int32\_t CreatePublisher\(NetRef<Publisher\> &publisher\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|pulisher|`NetRef<Publisher>`|出参|返回创建好的发布者publisher。|

**返回值<a name="section851917373122"></a>**

in32\_t。成功时返回0，失败时返回错误码。

##### PublisherService::DestroyPublisher<a name="ZH-CN_TOPIC_0000002611632841"></a>

**函数定义<a name="section4292195611128"></a>**

销毁发布者publisher。

**实现方法<a name="section660624014504"></a>**

void DestroyPublisher\(NetRef<Publisher\> &publisher\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|pulisher|`NetRef<Publisher>`|入参|需要销毁的发布者publisher。|

**返回值<a name="section851917373122"></a>**

无。

##### PublisherService::Bind<a name="ZH-CN_TOPIC_0000002581392996"></a>

**函数定义<a name="section4292195611128"></a>**

绑定监听url，指定监听的类型及url。

**实现方法<a name="section660624014504"></a>**

int32\_t Bind\(const std::string &listenerUrl, const NewSubscriptionHandler &handler, const int cpuId = -1\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|listenerUrl|const std::string|入参|监听url，如：tcp://127.0.0.1:9981。|
|handler|NewSubscriptionHandler|入参|收到订阅者建链请求后的回调函数。|
|cpuId|int|入参|监听线程绑定的cpuId， 默认不绑核|

**返回值<a name="section851917373122"></a>**

in32\_t。成功时返回0，失败时返回错误码。

>[!NOTE]说明
>
>数据类型解释如下：
>using NewSubscriptionHandler = std::function<int\(SubscriptionInfoPtr &info\)\>
>using SubscriptionInfoPtr = NetRef<SubscriptionInfo\>

##### PublisherService::GetConfig<a name="ZH-CN_TOPIC_0000002581233078"></a>

**函数定义<a name="section4292195611128"></a>**

获取MulticastConfig，用于做高级配置。

**实现方法<a name="section660624014504"></a>**

MulticastConfig &GetConfig\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

MulticastConfig&。返回MulticastConfig组播配置对象。

##### PublisherService::RegisterSubscriptionExceptionHandler<a name="ZH-CN_TOPIC_0000002611712945"></a>

**函数定义<a name="section4292195611128"></a>**

注册SubscriptionException回调函数。

**实现方法<a name="section660624014504"></a>**

void RegisterSubscriptionExceptionHandler\(const SubscriptionExceptionHandler &handler\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|handler|const SubscriptionExceptionHandler|入参|订阅异常后的回调函数。|

**返回值<a name="section851917373122"></a>**

无。

>[!NOTE]说明
> 
>数据类型解释如下：
>using SubscriptionExceptionHandler = std::function<void\(SubscriptionInfo &info\)\>

##### PublisherService::RegisterBrokenHandler<a name="ZH-CN_TOPIC_0000002611632843"></a>

**函数定义<a name="section4292195611128"></a>**

注册Broken断链回调函数。

**实现方法<a name="section660624014504"></a>**

void RegisterBrokenHandler\(const MulticastEpBrokenHandler &handler\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|handler|const MulticastEpBrokenHandler|入参|断链回调函数。|

**返回值<a name="section851917373122"></a>**

无。

>[!NOTE]说明
>
>数据类型解释如下：
>using MulticastEpBrokenHandler = std::function<void\(const ock::hcom::NetEndpointPtr &ep\)\>

##### PublisherService::RegisterSendHandler<a name="ZH-CN_TOPIC_0000002581392998"></a>

**函数定义<a name="section4292195611128"></a>**

注册发送回调函数。

**实现方法<a name="section660624014504"></a>**

void RegisterSendHandler\(const MulticastReqPostedHandler &handler\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|handler|const MulticastReqPostedHandler|入参|消息发送后的回调函数。|

**返回值<a name="section851917373122"></a>**

无。

>[!NOTE]说明
>
>数据类型解释如下：
>using MulticastReqPostedHandler = std::function<int\(ock::hcom::NetServiceContext &ctx\)\>

##### PublisherService::RegisterPubRecvHandler<a name="ZH-CN_TOPIC_0000002581233080"></a>

**函数定义<a name="section4292195611128"></a>**

注册发布者接收回调函数。

**实现方法<a name="section660624014504"></a>**

void RegisterPubRecvHandler\(const MulticastPubReqRecvHandler &handler\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|handler|const MulticastPubReqRecvHandler|入参|发布者接收消息后的回调函数。|

**返回值<a name="section851917373122"></a>**

无。

>[!NOTE]说明
>
>数据类型解释如下：
>using MulticastPubReqRecvHandler = std::function<int\(ock::hcom::PublisherContext &ctx\)\>

##### PublisherService::AddWorkerGroup<a name="ZH-CN_TOPIC_0000002611712947"></a>

**函数定义<a name="section4292195611128"></a>**

添加WorkerGroup配置。

**实现方法<a name="section660624014504"></a>**

void AddWorkerGroup\(uint16\_t workerGroupId, uint32\_t threadCount, const std::pair<uint32\_t, uint32\_t\> &cpuIdsRange, int8\_t priority\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|workerGroupId|uint16_t|入参|worker线程中的组ID。|
|threadCount|uint32_t|入参|线程总数。|
|cpuIdsRange|const std::pair<uint32_t, uint32_t>|入参|指定worker线程CPU范围。|
|priority|int8_t|入参|线程优先级。范围：[-20, 19]。|

**返回值<a name="section851917373122"></a>**

无。

##### PublisherService::RegisterMemoryRegion<a name="ZH-CN_TOPIC_0000002611632845"></a>

**函数定义<a name="section4292195611128"></a>**

- 注册一个内存区域，内存将在组播内部分配。
- 将用户申请的内存，注册到组播内部。

**实现方法<a name="section25141526171720"></a>**

- SerResult RegisterMemoryRegion\(uint64\_t size, NetMemoryRegionPtr &mr\)
- SerResult RegisterMemoryRegion\(uintptr\_t address, uint64\_t size, NetMemoryRegionPtr &mr\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|address|uintptr_t|入参|如果有此参数，则是外部申请的内存。|
|size|uint64_t|入参|需要注册的内存大小，单位byte。|
|mr|NetMemoryRegionPtr|出参|内存区域结构，包含key、名字、大小、buf等字段。|

**返回值<a name="section851917373122"></a>**

表示函数执行结果，返回值为0则表示注册成功。

##### PublisherService::DestroyMemoryRegion<a name="ZH-CN_TOPIC_0000002581393000"></a>

**函数定义<a name="section4292195611128"></a>**

销毁内存区域。

**实现方法<a name="section25141526171720"></a>**

void DestroyMemoryRegion\(NetMemoryRegionPtr &mr\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|mr|NetMemoryRegionPtr|入参|需要销毁的内存区域。|

**返回值<a name="section851917373122"></a>**

无。

##### Publisher::Call<a name="ZH-CN_TOPIC_0000002581233082"></a>

**函数定义<a name="section4292195611128"></a>**

发布双边消息，需要订阅者回复。

**实现方法<a name="section25141526171720"></a>**

SerResult Call\(const NetServiceOpInfo &opInfo, const MultiRequest &req, const MultiCastCallback \*done\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|opInfo|NetServiceOpInfo|入参|发送组播消息的opInfo，主要用于设置超时时间。|
|req|const MultiRequest|入参|发送组播消息请求。|
|done|MultiCastCallback *|入参|发送组播完成或超时的回调函数。|

**返回值<a name="section851917373122"></a>**

SerResult。成功返回0，失败返回错误码。

##### Publisher::AddSubscription<a name="ZH-CN_TOPIC_0000002611712949"></a>

**函数定义<a name="section4292195611128"></a>**

发布者添加订阅信息。

**实现方法<a name="section25141526171720"></a>**

bool AddSubscription\(SubscriptionInfoPtr &info\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|info|SubscriptionInfoPtr|入参|订阅者信息。|

**返回值<a name="section851917373122"></a>**

bool。成功返回true，失败返回false。

##### Publisher::DelSubscription<a name="ZH-CN_TOPIC_0000002611632847"></a>

**函数定义<a name="section4292195611128"></a>**

发布者删除订阅信息。

**实现方法<a name="section25141526171720"></a>**

bool DelSubscription\(SubscriptionInfoPtr &info\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|info|SubscriptionInfoPtr|入参|订阅者信息。|

**返回值<a name="section851917373122"></a>**

bool。成功返回true，失败返回false。

##### Publisher::GetSubscriberNum<a name="ZH-CN_TOPIC_0000002581393002"></a>

**函数定义<a name="section4292195611128"></a>**

发布者删除订阅者数量。

**实现方法<a name="section25141526171720"></a>**

uint32\_t GetSubscriberNum\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

uint32\_t。返回订阅者数量。

##### Publisher::GetAllSubscriberInfo<a name="ZH-CN_TOPIC_0000002581233084"></a>

**函数定义<a name="section4292195611128"></a>**

获取所有订阅者信息。

**实现方法<a name="section25141526171720"></a>**

std::vector<SubscriptionInfoPtr\> GetAllSubscriberInfo\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

vector。返回所有订阅者信息。

##### Publisher::GetSubscribeByEpId<a name="ZH-CN_TOPIC_0000002611712951"></a>

**函数定义<a name="section4292195611128"></a>**

通过ep id获取订阅者信息。

**实现方法<a name="section25141526171720"></a>**

SubscriptionInfoPtr GetSubscribeByEpId\(uint64\_t id\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|id|uint64_t|入参|ep id。|

**返回值<a name="section851917373122"></a>**

SubscriptionInfoPtr。返回获取到的订阅者信息。

##### Publisher::Initialize<a name="ZH-CN_TOPIC_0000002611632851"></a>

**函数定义<a name="section4292195611128"></a>**

初始化发布者。

**实现方法<a name="section25141526171720"></a>**

SerResult Initialize\(uintptr\_t memPool, uintptr\_t pubMemPool, uintptr\_t periodicMgr, uint32\_t ctxStoreCapacity, UBSHcomNetDriverProtocol protocol = UBSHcomNetDriverProtocol::RDMA\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|memPool|uintptr_t|入参|内存池，用于创建timer ctx store。|
|pubMemPool|uintptr_t|入参|内存池，用于创建回复信息ctx store。|
|periodicMgr|uintptr_t|入参|定时管理器。|
|ctxStoreCapacity|uint32_t|入参|ctx store的容量大小。|
|protocol|UBSHcomNetDriverProtocol|入参|组播driver协议类型，默认是RDMA，当前支持RDMA/TCP|

**返回值<a name="section851917373122"></a>**

SerResult。成功时返回0，失败时返回错误码。

##### PublisherContext::GetSubscriberRspInfo<a name="ZH-CN_TOPIC_0000002581393004"></a>

**函数定义<a name="section4292195611128"></a>**

获取订阅者回复信息列表。

**实现方法<a name="section25141526171720"></a>**

const std::vector<SubscriberRspInfo\>& GetSubscriberRspInfo\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

vector。返回订阅者回复信息列表。

##### PublisherContext::SetResponseStatus<a name="ZH-CN_TOPIC_0000002581233086"></a>

**函数定义<a name="section4292195611128"></a>**

指定subscrption，设置其在subscriberRspList内的SubscriberRspStatus响应状态。

**实现方法<a name="section25141526171720"></a>**

void SetResponseStatus\(SubscriptionInfoPtr &sub, NetMessage \*message, SubscriberRspStatus status\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|sub|SubscriptionInfoPtr|入参|订阅者信息。|
|message|NetMessage *|入参|message信息。|
|status|SubscriberRspStatus|入参|订阅者回复响应状态。|

**返回值<a name="section851917373122"></a>**

无。

##### PublisherContext::InitSubscribers<a name="ZH-CN_TOPIC_0000002611712953"></a>

**函数定义<a name="section4292195611128"></a>**

初始化所有订阅者状态。

**实现方法<a name="section25141526171720"></a>**

SerResult InitSubscribers\(const std::unordered\_map<uint32\_t, SubscriptionInfoPtr\> &allSubs\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|allSubs|const std::unordered_map<uint32_t, SubscriptionInfoPtr>|入参|订阅者信息map。|

**返回值<a name="section851917373122"></a>**

SerResult。成功时返回0。

##### PublisherContext::GetReplyCount<a name="ZH-CN_TOPIC_0000002611632853"></a>

**函数定义<a name="section4292195611128"></a>**

获取订阅者回复数量。

**实现方法<a name="section25141526171720"></a>**

uint32\_t GetReplyCount\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

uint32\_t。返回订阅者回复数量。

##### PublisherContext::SetSendCount<a name="ZH-CN_TOPIC_0000002581393006"></a>

**函数定义<a name="section4292195611128"></a>**

设置发布者发送数量。内部使用。

**实现方法<a name="section25141526171720"></a>**

void SetSendCount\(uint32\_t count\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|count|uint32_t|入参|要设置的发布者发送数量。|

**返回值<a name="section851917373122"></a>**

无。

##### PublisherContext::GetSendCount<a name="ZH-CN_TOPIC_0000002581233088"></a>

**函数定义<a name="section4292195611128"></a>**

获取发布者发送数量。

**实现方法<a name="section25141526171720"></a>**

uint32\_t GetSendCount\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

uint32\_t。返回发布者发送数量。

##### SubscriptionInfo::GetId<a name="ZH-CN_TOPIC_0000002611712955"></a>

**函数定义<a name="section4292195611128"></a>**

获取订阅者id信息。

**实现方法<a name="section25141526171720"></a>**

uint64\_t GetId\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

uint64\_t。返回订阅者id信息。

##### SubscriptionInfo::GetName<a name="ZH-CN_TOPIC_0000002611632855"></a>

**函数定义<a name="section4292195611128"></a>**

获取订阅者名称信息。

**实现方法<a name="section25141526171720"></a>**

std::string& GetName\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

std::string。返回订阅者名称信息。

##### SubscriptionInfo::GetIp<a name="ZH-CN_TOPIC_0000002581393008"></a>

**函数定义<a name="section4292195611128"></a>**

获取订阅者ip信息。

**实现方法<a name="section25141526171720"></a>**

std::string& GetIp\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

std::string。返回订阅者ip信息。

##### SubscriptionInfo::GetPort<a name="ZH-CN_TOPIC_0000002581233090"></a>

**函数定义<a name="section4292195611128"></a>**

获取订阅者port信息。

**实现方法<a name="section25141526171720"></a>**

uint64\_t GetPort\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

uint64\_t。返回订阅者port信息。

##### SubscriberRspInfo::GetSubInfos<a name="ZH-CN_TOPIC_0000002611712957"></a>

**函数定义<a name="section4292195611128"></a>**

获取订阅者信息。

**实现方法<a name="section25141526171720"></a>**

SubscriptionInfoPtr GetSubInfos\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

返回SubscriptionInfoPtr订阅者信息。

##### SubscriberRspInfo::GetStatus<a name="ZH-CN_TOPIC_0000002611632857"></a>

**函数定义<a name="section4292195611128"></a>**

获取订阅者响应状态。

**实现方法<a name="section25141526171720"></a>**

SubscriberRspStatus GetStatus\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

返回SubscriberRspStatus订阅者响应信息。

##### SubscriberRspInfo::GetMultiResponse<a name="ZH-CN_TOPIC_0000002581393012"></a>

**函数定义<a name="section4292195611128"></a>**

获取订阅者回复数据。

**实现方法<a name="section25141526171720"></a>**

const MultiResponse& GetMultiResponse\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

返回MultiResponse订阅者回复数据。

##### SubscriberService::Create<a name="ZH-CN_TOPIC_0000002581233094"></a>

**函数定义<a name="section4292195611128"></a>**

创建订阅者服务。

**实现方法<a name="section660624014504"></a>**

static SubscriberService \*Create\(const std::string &name, const MulticastServiceOptions &opt = \{\}\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|name|const std::string|入参|需要创建的服务名称。|
|opt|const MulticastServiceOptions|入参|服务创建所需的配置项。|

**返回值<a name="section851917373122"></a>**

成功时返回创建好的SubscriberService实例，失败时返回空。

##### SubscriberService::Destroy<a name="ZH-CN_TOPIC_0000002611712959"></a>

**函数定义<a name="section4292195611128"></a>**

销毁订阅者服务。

**实现方法<a name="section660624014504"></a>**

static int32\_t Destroy\(const std::string &name\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|name|const std::string|入参|需要销毁的服务名称。|

**返回值<a name="section851917373122"></a>**

in32\_t。成功时返回0，失败时返回错误码。

##### SubscriberService::Start<a name="ZH-CN_TOPIC_0000002611632859"></a>

**函数定义<a name="section4292195611128"></a>**

启动订阅者服务。

**实现方法<a name="section660624014504"></a>**

int32\_t Start\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

in32\_t。成功时返回0，失败时返回错误码。

##### SubscriberService::Stop<a name="ZH-CN_TOPIC_0000002581393014"></a>

**函数定义<a name="section4292195611128"></a>**

停止订阅者服务。

**实现方法<a name="section660624014504"></a>**

void Stop\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

无。

##### SubscriberService::CreateSubscriber<a name="ZH-CN_TOPIC_0000002581233096"></a>

**函数定义<a name="section4292195611128"></a>**

创建订阅者subscriber。

**实现方法<a name="section660624014504"></a>**

int32\_t CreateSubscriber\(const std::string &serverUrl, NetRef<Subscriber\> &subscriber\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|serverUrl|const std::string|入参|目标url，如：tcp://127.0.0.1:9981。|
|subscriber|`NetRef<Subscriber>`|出参|返回创建好的订阅者subscriber。|

**返回值<a name="section851917373122"></a>**

in32\_t。成功时返回0，失败时返回错误码。

##### SubscriberService::DestroySubscriber<a name="ZH-CN_TOPIC_0000002611712961"></a>

**函数定义<a name="section4292195611128"></a>**

销毁订阅者subscriber。

**实现方法<a name="section660624014504"></a>**

void DestroySubscriber\(const NetRef<Subscriber\> &subscriber\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|subscriber|`NetRef<Subscriber>`|入参|需要销毁的订阅者subscriber。|

**返回值<a name="section851917373122"></a>**

无。

##### SubscriberService::GetConfig<a name="ZH-CN_TOPIC_0000002611632861"></a>

**函数定义<a name="section4292195611128"></a>**

获取MulticastConfig，用于做高级配置。

**实现方法<a name="section660624014504"></a>**

MulticastConfig &GetConfig\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

MulticastConfig&。返回MulticastConfig组播配置对象。

##### SubscriberService::RegisterBrokenHandler<a name="ZH-CN_TOPIC_0000002581393016"></a>

**函数定义<a name="section4292195611128"></a>**

注册Broken断链回调函数。

**实现方法<a name="section660624014504"></a>**

void RegisterBrokenHandler\(const MulticastEpBrokenHandler &handler\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|handler|const MulticastEpBrokenHandler|入参|断链回调函数。|

**返回值<a name="section851917373122"></a>**

无。

>[!NOTE]说明
>
>数据类型解释如下：
>using MulticastEpBrokenHandler = std::function<void\(constock::hcom::NetEndpointPtr &ep\)\>

##### SubscriberService::RegisterRecvHandler<a name="ZH-CN_TOPIC_0000002581233100"></a>

**函数定义<a name="section4292195611128"></a>**

注册订阅者接收回调函数。

**实现方法<a name="section660624014504"></a>**

void RegisterRecvHandler\(const MulticastReqRecvHandler &handler\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|handler|const MulticastReqRecvHandler|入参|订阅者接收消息后的回调函数。|

**返回值<a name="section851917373122"></a>**

无。

>[!NOTE]说明
>
>数据类型解释如下：
>using MulticastReqRecvHandler = std::function<int\(ock::hcom::NetServiceContext &ctx\)\>

##### Subscriber::GetEp<a name="ZH-CN_TOPIC_0000002611712963"></a>

**函数定义<a name="section4292195611128"></a>**

获取订阅者对应ep。仅用于SubscriberService::DestorySubscriber。

**实现方法<a name="section25141526171720"></a>**

NetEndpointPtr &GetEp\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

返回订阅者对应ep。

##### Subscriber::GetIp<a name="ZH-CN_TOPIC_0000002611632863"></a>

**函数定义<a name="section4292195611128"></a>**

获取订阅者ip信息。

**实现方法<a name="section25141526171720"></a>**

const std::string& GetIp\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

std::string。返回订阅者ip信息。

##### Subscriber::GetPort<a name="ZH-CN_TOPIC_0000002581393018"></a>

**函数定义<a name="section4292195611128"></a>**

获取订阅者port信息。

**实现方法<a name="section25141526171720"></a>**

const uint16\_t GetPort\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

uint16\_t。返回订阅者port信息。

##### Subscriber::Close<a name="ZH-CN_TOPIC_0000002581233102"></a>

**函数定义<a name="section4292195611128"></a>**

关闭订阅者连接即取消订阅。

**实现方法<a name="section25141526171720"></a>**

void Close\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

无。

##### SubscriberContext::Reply<a name="ZH-CN_TOPIC_0000002611712965"></a>

**函数定义<a name="section4292195611128"></a>**

订阅者回复双边消息。

**实现方法<a name="section25141526171720"></a>**

int Reply\(const MultiRequest &req\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|req|const MultiRequest|入参|发送组播消息请求。|

**返回值<a name="section851917373122"></a>**

int。成功返回0，失败返回错误码。

##### MulticastConfig::Init<a name="ZH-CN_TOPIC_0000002611632865"></a>

**函数定义<a name="section4292195611128"></a>**

初始化组播配置。

**实现方法<a name="section660624014504"></a>**

bool Init\(const std::string &name, const MulticastServiceOptions &opt\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|name|const std::string|入参|服务名称。|
|opt|const MulticastServiceOptions|入参|组播配置项。|

**返回值<a name="section851917373122"></a>**

bool。成功时返回true。

##### MulticastConfig::GetName<a name="ZH-CN_TOPIC_0000002581393020"></a>

**函数定义<a name="section4292195611128"></a>**

获取配置名称。

**实现方法<a name="section25141526171720"></a>**

const std::string &GetName\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

std::string。返回配置项中name名称。

##### MulticastConfig::SetDeviceIpMask<a name="ZH-CN_TOPIC_0000002581233104"></a>

**函数定义<a name="section4292195611128"></a>**

设置设备IpMask。

**实现方法<a name="section660624014504"></a>**

`void SetDeviceIpMask(const std::vector<std::string> &ipMasks)`

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ipMasks|`const std::vector<std::string>`|入参|ipMask集合。|

**返回值<a name="section851917373122"></a>**

无。

##### MulticastConfig::GetDeviceIpMask<a name="ZH-CN_TOPIC_0000002611712967"></a>

**函数定义<a name="section4292195611128"></a>**

获取设备IpMask。

**实现方法<a name="section660624014504"></a>**

const std::vector\<std::string\> GetDeviceIpMask\( \)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

返回获取到的ipMask集合。

##### MulticastConfig::SetCompletionQueueDepth<a name="ZH-CN_TOPIC_0000002611632867"></a>

**函数定义<a name="section4292195611128"></a>**

设置cq完成队列深度。

**实现方法<a name="section660624014504"></a>**

void SetCompletionQueueDepth\(uint16\_t depth\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|depth|uint16_t|入参|cq完成队列深度。|

**返回值<a name="section851917373122"></a>**

无。

##### MulticastConfig::GetCompletionQueueDepth<a name="ZH-CN_TOPIC_0000002581393022"></a>

**函数定义<a name="section4292195611128"></a>**

获取cq完成队列深度。

**实现方法<a name="section660624014504"></a>**

const uint16\_t GetCompletionQueueDepth\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

uint16\_t 。返回获取的cq完成队列深度。

##### MulticastConfig::SetSendQueueSize<a name="ZH-CN_TOPIC_0000002581233106"></a>

**函数定义<a name="section4292195611128"></a>**

设置qp发送队列大小。

**实现方法<a name="section660624014504"></a>**

void SetSendQueueSize\(uint32\_t sqSize\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|sqSize|uint32_t|入参|qp发送队列大小。|

**返回值<a name="section851917373122"></a>**

无。

##### MulticastConfig::GetSendQueueSize<a name="ZH-CN_TOPIC_0000002611712969"></a>

**函数定义<a name="section4292195611128"></a>**

获取qp发送队列大小。

**实现方法<a name="section660624014504"></a>**

const uint32\_t GetSendQueueSize\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

uint32\_t。返回获取的qp发送队列大小。

##### MulticastConfig::SetRecvQueueSize<a name="ZH-CN_TOPIC_0000002611632869"></a>

**函数定义<a name="section4292195611128"></a>**

设置qp接收队列大小。

**实现方法<a name="section660624014504"></a>**

void SetRecvQueueSize\(uint32\_t rqSize\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|rqSize|uint32_t|入参|qp接收队列大小。|

**返回值<a name="section851917373122"></a>**

无。

##### MulticastConfig::GetRecvQueueSize<a name="ZH-CN_TOPIC_0000002581393024"></a>

**函数定义<a name="section4292195611128"></a>**

获取qp接收队列大小。

**实现方法<a name="section660624014504"></a>**

const uint32\_t GetRecvQueueSize\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

uint32\_t。返回获取的qp接收队列大小。

##### MulticastConfig::SetQueuePrePostSize<a name="ZH-CN_TOPIC_0000002581233108"></a>

**函数定义<a name="section4292195611128"></a>**

设置qp队列预申请大小。

**实现方法<a name="section660624014504"></a>**

void SetQueuePrePostSize\(uint32\_t prePostSize\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|prePostSize|uint32_t|入参|qp队列预申请大小。|

**返回值<a name="section851917373122"></a>**

无。

##### MulticastConfig::GetQueuePrePostSize<a name="ZH-CN_TOPIC_0000002611712971"></a>

**函数定义<a name="section4292195611128"></a>**

获取qp队列预申请大小。

**实现方法<a name="section660624014504"></a>**

const uint32\_t GetQueuePrePostSize\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

uint32\_t。返回获取的qp队列预申请大小。

##### MulticastConfig::SetPollingBatchSize<a name="ZH-CN_TOPIC_0000002611632871"></a>

**函数定义<a name="section4292195611128"></a>**

设置批量poll cq的个数。

**实现方法<a name="section660624014504"></a>**

void SetPollingBatchSize\(uint16\_t pollSize\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|pollSize|uint16_t|入参|批量poll cq的个数。|

**返回值<a name="section851917373122"></a>**

无。

##### MulticastConfig::GetPollingBatchSize<a name="ZH-CN_TOPIC_0000002581393026"></a>

**函数定义<a name="section4292195611128"></a>**

获取批量poll cq的个数。

**实现方法<a name="section660624014504"></a>**

const uint16\_t GetPollingBatchSize\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

uint16\_t。返回获取的批量poll cq的个数。

##### MulticastConfig::SetEventPollingTimeOutUs<a name="ZH-CN_TOPIC_0000002581233110"></a>

**函数定义<a name="section4292195611128"></a>**

设置event polling超时时间。

**实现方法<a name="section660624014504"></a>**

void SetEventPollingTimeOutUs\(uint16\_t pollTimeout\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|pollTimeout|uint16_t|入参|event polling超时时间。|

##### MulticastConfig::GetEventPollingTimeOutUs<a name="ZH-CN_TOPIC_0000002611712975"></a>

**函数定义<a name="section4292195611128"></a>**

获取event polling超时时间。

**实现方法<a name="section660624014504"></a>**

const uint16\_t GetEventPollingTimeOutUs\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

uint16\_t。返回获取的event polling超时时间。

##### MulticastConfig::SetHeartBeatOptions<a name="ZH-CN_TOPIC_0000002611632875"></a>

**函数定义<a name="section4292195611128"></a>**

设置组播心跳配置。

**实现方法<a name="section660624014504"></a>**

void SetHeartBeatOptions\(const MulticastHeartBeatOptions &opt\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|opt|const MulticastHeartBeatOptions|入参|组播心跳配置。|

**返回值<a name="section851917373122"></a>**

无。

##### MulticastConfig::GetHeartBeatOptions<a name="ZH-CN_TOPIC_0000002581393028"></a>

**函数定义<a name="section4292195611128"></a>**

获取组播心跳配置。

**实现方法<a name="section660624014504"></a>**

const MulticastHeartBeatOptions &GetHeartBeatOptions\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

MulticastHeartBeatOptions。返回获取的组播心跳配置。

##### MulticastConfig::SetMaxSendRecvDataCount<a name="ZH-CN_TOPIC_0000002581233112"></a>

**函数定义<a name="section4292195611128"></a>**

设置同时最大发送的数据个数。

**实现方法<a name="section660624014504"></a>**

void SetMaxSendRecvDataCount\(uint32\_t maxSendRecvDataCount\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|maxSendRecvDataCount|uint32_t|入参|同时最大发送的数据个数。|

**返回值<a name="section851917373122"></a>**

无。

##### MulticastConfig::GetMaxSendRecvDataCount<a name="ZH-CN_TOPIC_0000002611712977"></a>

**函数定义<a name="section4292195611128"></a>**

获取同时最大发送的数据个数。

**实现方法<a name="section660624014504"></a>**

const uint32\_t GetMaxSendRecvDataCount\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

uint16\_t。返回获取的同时最大发送的数据个数。

##### MulticastConfig::SetMaxSendRecvDataSize<a name="ZH-CN_TOPIC_0000002611632877"></a>

**函数定义<a name="section4292195611128"></a>**

设置发送数据块最大值。

**实现方法<a name="section660624014504"></a>**

void SetMaxSendRecvDataSize\(uint32\_t maxSendRecvDataSize\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|maxSendRecvDataSize|uint32_t|入参|同时发送数据块最大值。|

**返回值<a name="section851917373122"></a>**

无。

##### MulticastConfig::GetMaxSendRecvDataSize<a name="ZH-CN_TOPIC_0000002581393030"></a>

**函数定义<a name="section4292195611128"></a>**

获取发送数据块最大值。

**实现方法<a name="section660624014504"></a>**

const uint32\_t GetMaxSendRecvDataSize\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

uint16\_t。返回获取的发送数据块最大值。

##### MulticastConfig::SetWorkerGroupInfo<a name="ZH-CN_TOPIC_0000002581233114"></a>

**函数定义<a name="section4292195611128"></a>**

设置worker组信息。

**实现方法<a name="section660624014504"></a>**

void SetWorkerGroupInfo\(std::vector<WorkerGroupInfo\> &info\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|info|std::vector\<WorkerGroupInfo\>|入参|worker组信息。|

**返回值<a name="section851917373122"></a>**

无。

##### MulticastConfig::GetWorkerGroupInfo<a name="ZH-CN_TOPIC_0000002611712979"></a>

**函数定义<a name="section4292195611128"></a>**

获取worker组信息。

**实现方法<a name="section660624014504"></a>**

const std::vector<WorkerGroupInfo\> &GetWorkerGroupInfo\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

std::vector<WorkerGroupInfo\>。返回worker组信息。

##### MulticastConfig::SetPeriodicThreadNum<a name="ZH-CN_TOPIC_0000002611632879"></a>

**函数定义<a name="section4292195611128"></a>**

设置定时线程数量。

**实现方法<a name="section660624014504"></a>**

void SetPeriodicThreadNum\(uint32\_t threadNum\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|threadNum|uint32_t|入参|定时线程数量。|

**返回值<a name="section851917373122"></a>**

无。

##### MulticastConfig::GetPeriodicThreadNum<a name="ZH-CN_TOPIC_0000002581393034"></a>

**函数定义<a name="section4292195611128"></a>**

获取定时线程数量。

**实现方法<a name="section660624014504"></a>**

const uint32\_t GetPeriodicThreadNum\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

uint32\_t。返回定时线程数量。

##### MulticastConfig::SetMaxSubscriberNum<a name="ZH-CN_TOPIC_0000002581233116"></a>

**函数定义<a name="section4292195611128"></a>**

设置一个发布者最大的订阅者数量。

**实现方法<a name="section660624014504"></a>**

void SetMaxSubscriberNum\(uint32\_t maxSubscriberNum\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|maxSubscriberNum|uint32_t|入参|一个发布者最大的订阅者数量。|

**返回值<a name="section851917373122"></a>**

无。

##### MulticastConfig::GetMaxSubscriberNum<a name="ZH-CN_TOPIC_0000002611712981"></a>

**函数定义<a name="section4292195611128"></a>**

获取一个发布者最大的订阅者数量。

**实现方法<a name="section660624014504"></a>**

const uint32\_t GetMaxSubscriberNum\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

uint32\_t。返回一个发布者最大的订阅者数量。

##### MulticastConfig::SetPeriodicCpuId<a name="ZH-CN_TOPIC_0000002581405274"></a>

**函数定义<a name="section4292195611128"></a>**

设置发布者超时定时器线程绑定的cpuId。

**实现方法<a name="section660624014504"></a>**

void SetPeriodicCpuId\(int cpuId\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|cpuId|int|入参|发布者超时定时器线程绑定的cpuId。|

**返回值<a name="section851917373122"></a>**

无。

##### MulticastConfig::GetPeriodicCpuId<a name="ZH-CN_TOPIC_0000002581245364"></a>

**函数定义<a name="section4292195611128"></a>**

获取发布者超时定时器线程绑定的cpuId。

**实现方法<a name="section660624014504"></a>**

const int GetPeriodicCpuId\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

int。返回发布者超时定时器线程绑定的cpuId。

##### MulticastConfig::GetProtocol<a name="ZH-CN_TOPIC_0000002611727633"></a>

**函数定义<a name="section4292195611128"></a>**

获取组播driver协议类型。

**实现方法<a name="section660624014504"></a>**

UBSHcomNetDriverProtocol GetProtocol\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

UBSHcomNetDriverProtocol。返回组播driver协议类型。

##### MultiCastCallback::Run<a name="ZH-CN_TOPIC_0000002611632881"></a>

**函数定义<a name="section4292195611128"></a>**

运行组播回调函数。

**实现方法<a name="section155052744912"></a>**

void Run\(PublisherContext &context\)

**参数说明<a name="section8984192751117"></a>**

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|context|PublisherContext|入参|PublisherContext上下文。|

**返回值<a name="section851917373122"></a>**

无。

##### MultiCastCallback::SetTime<a name="ZH-CN_TOPIC_0000002581393036"></a>

**函数定义<a name="section4292195611128"></a>**

设置组播回调初始时间。

**实现方法<a name="section155052744912"></a>**

void SetTime\(uint64\_t time\)

**参数说明<a name="section8984192751117"></a>**

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|time|uint64_t|入参|内部设置的初始时间。|

**返回值<a name="section851917373122"></a>**

无

##### MultiCastCallback::GetTime<a name="ZH-CN_TOPIC_0000002581233118"></a>

**函数定义<a name="section4292195611128"></a>**

获取组播回调初始时间。

**实现方法<a name="section155052744912"></a>**

uint64\_t GetTime\(\)

**参数说明<a name="section8984192751117"></a>**

无

**返回值<a name="section851917373122"></a>**

返回组播回调初始时间。

##### MultiCastCallback::Permanent<a name="ZH-CN_TOPIC_0000002611712983"></a>

**函数定义<a name="section4292195611128"></a>**

获取组播回调是否永久。

**实现方法<a name="section155052744912"></a>**

bool Permanent\(\)

**参数说明<a name="section8984192751117"></a>**

无。

**返回值<a name="section851917373122"></a>**

返回true则为永久回调，不会被析构，反之则会在Run后直接析构自身。

##### DestroyCallback<a name="ZH-CN_TOPIC_0000002611632883"></a>

**函数定义<a name="section4292195611128"></a>**

销毁回调。仅用于失败情况下一个 MultiCastCallback 对象没有被扔进超时队列中，需要被清理。当它需要被调用 Run\(\) 时无需调用此函数。另外需要考虑 permanent callback, 它不需要被清理，常用于回复消息。

**实现方法<a name="section155052744912"></a>**

void DestroyCallback\(const MultiCastCallback \*cb\)

**参数说明<a name="section8984192751117"></a>**

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|cb|const MultiCastCallback*|入参|需要销毁的组播回调函数。|

**返回值<a name="section851917373122"></a>**

返回true则为永久回调，不会被析构，反之则会在Run后直接析构自身。

##### NewMultiCastCallback<a name="ZH-CN_TOPIC_0000002581393038"></a>

**函数定义<a name="section4292195611128"></a>**

创建组播回调函数，会在Run后析构自身。

**实现方法<a name="section155052744912"></a>**

template <typename... Args\> MultiCastCallback \*NewMultiCastCallback\(Args... args\)

**参数说明<a name="section8984192751117"></a>**

用户可自定义传入的args类型与数量。

**返回值<a name="section851917373122"></a>**

成功返回MultiCastCallback指针，失败则返回nullptr。

##### NewPermanentCallback<a name="ZH-CN_TOPIC_0000002581233122"></a>

**函数定义<a name="section4292195611128"></a>**

创建永久回调函数，不会析构自身。

**实现方法<a name="section155052744912"></a>**

template <typename... Args\> MultiCastCallback \*NewPermanentCallback\(Args... args\)

**参数说明<a name="section8984192751117"></a>**

用户可自定义传入的args类型与数量。

**返回值<a name="section851917373122"></a>**

成功返回callback指针，失败则返回nullptr。

### C API<a name="ZH-CN_TOPIC_0000002596638771"></a>

#### 服务层<a name="ZH-CN_TOPIC_0000002596637885"></a>

##### ubs\_hcom\_service\_add\_workergroup<a name="ZH-CN_TOPIC_0000002566158722"></a>

**函数定义<a name="section4292195611128"></a>**

向Service中增加内存池。

**实现方法<a name="section2099511113158"></a>**

void ubs\_hcom\_service\_add\_workergroup\(ubs\_hcom\_service service, int8\_t priority, uint16\_t workerGroupId, uint32\_t threadCount,

const char \*cpuIdsRange\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|之前创建的ubs_hcom_service对象。|
|priority|int8_t|入参|线程优先级。|
|workerGroupId|uint16_t|入参|线程组ID。|
|threadCount|uint32_t|入参|组里的线程数。|
|cpuIdsRange|const char *|入参|CPU ID范围。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_service\_add\_listener<a name="ZH-CN_TOPIC_0000002565998466"></a>

**函数定义<a name="section4292195611128"></a>**

添加监听线程。

**实现方法<a name="section2099511113158"></a>**

void ubs\_hcom\_service\_add\_listener\(ubs\_hcom\_service service, const char \*url, uint16\_t workerCount\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|之前创建的ubs_hcom_service对象。|
|priority|int8_t|入参|线程优先级。|
|workerGroupId|uint16_t|入参|线程组ID。|
|threadCount|uint32_t|入参|组里的线程数。|
|cpuIdsRange|const char *|入参|CPU ID范围。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_service\_set\_lbpolicy<a name="ZH-CN_TOPIC_0000002596758625"></a>

**函数定义<a name="section4292195611128"></a>**

设置负载均衡策略。

**实现方法<a name="section2099511113158"></a>**

void ubs\_hcom\_service\_set\_lbpolicy\(ubs\_hcom\_service service, ubs\_hcom\_service\_lb\_policy lbPolicy\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|之前创建的ubs_hcom_service对象。|
|lbPolicy|ubs_hcom_service_lb_policy|入参|负载均衡策略。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_service\_set\_tls\_opt<a name="ZH-CN_TOPIC_0000002596758069"></a>

**函数定义<a name="section4292195611128"></a>**

设置TLS配置项。

**实现方法<a name="section2099511113158"></a>**

void ubs\_hcom\_service\_set\_tls\_opt\(ubs\_hcom\_service service, bool enableTls, ubs\_hcom\_service\_tls\_version version,

ubs\_hcom\_service\_cipher\_suite cipherSuite, ubs\_hcom\_tls\_get\_cert\_cb certCb, ubs\_hcom\_tls\_get\_pk\_cb priKeyCb, ubs\_hcom\_tls\_get\_ca\_cb caCb\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|之前创建的ubs_hcom_service对象。|
|enableTls|bool|入参|是否开启TLS。|
|version|ubs_hcom_service_tls_version|入参|TLS版本。|
|cipherSuite|ubs_hcom_service_cipher_suite|入参|加密方式。|
|certCb|ubs_hcom_tls_get_cert_cb|入参|获取TLS证书的回调。|
|priKeyCb|ubs_hcom_tls_get_pk_cb|入参|获取TLS私钥的回调。|
|caCb|ubs_hcom_tls_get_ca_cb|入参|获取TLS认证的回调。|

>[!NOTE]说明
>使用UB自举建链时，暂不支持安全认证和安全加密。

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_service\_set\_secure\_opt<a name="ZH-CN_TOPIC_0000002596638135"></a>

**函数定义<a name="section4292195611128"></a>**

设置安全加密选项。

**实现方法<a name="section2099511113158"></a>**

void ubs\_hcom\_service\_set\_secure\_opt\(ubs\_hcom\_service service, ubs\_hcom\_service\_secure\_type secType, ubs\_hcom\_secinfo\_provider provider,

ubs\_hcom\_secinfo\_validator validator, uint16\_t magic, uint8\_t version\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|之前创建的ubs_hcom_service对象。|
|secType|ubs_hcom_service_secure_type|入参|加密方式。|
|provider|ubs_hcom_secinfo_provider|入参|密钥提供回调。|
|validator|ubs_hcom_secinfo_validator|入参|密钥校验回调。|
|magic|uint16_t|入参|魔数。|
|version|uint8_t|入参|安全加密版本。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_service\_set\_tcp\_usr\_timeout<a name="ZH-CN_TOPIC_0000002566159004"></a>

**函数定义<a name="section4292195611128"></a>**

设置负载均衡策略。

**实现方法<a name="section2099511113158"></a>**

void ubs\_hcom\_service\_set\_tcp\_usr\_timeout\(ubs\_hcom\_service service, uint16\_t timeOutSec\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|之前创建的ubs_hcom_service对象。|
|timeOutSec|uint16_t|入参|TCP超时时间|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_service\_set\_tcp\_send\_zcopy<a name="ZH-CN_TOPIC_0000002596757871"></a>

**函数定义<a name="section4292195611128"></a>**

设置负载均衡策略。

**实现方法<a name="section2099511113158"></a>**

void ubs\_hcom\_service\_set\_tcp\_send\_zcopy\(ubs\_hcom\_service service, bool tcpSendZCopy\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|之前创建的ubs_hcom_service对象。|
|tcpSendZCopy|bool|入参|TCP是否开启ZCopy。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_service\_set\_ipmask<a name="ZH-CN_TOPIC_0000002596758347"></a>

**函数定义<a name="section4292195611128"></a>**

设置要监听的IP。

**实现方法<a name="section2099511113158"></a>**

void ubs\_hcom\_service\_set\_ipmask\(ubs\_hcom\_service service, const char \*ipMask\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|之前创建的ubs_hcom_service对象。|
|ipMask|const char *|入参|要监听的IP。用于rdma/ub，根据ipMask获取该网段的GID和UBEId。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_service\_set\_ipgroup<a name="ZH-CN_TOPIC_0000002565999092"></a>

**函数定义<a name="section4292195611128"></a>**

设置要监听的IP。

**实现方法<a name="section2099511113158"></a>**

void ubs\_hcom\_service\_set\_ipgroup\(ubs\_hcom\_service service, const char \*ipGroup\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|之前创建的ubs_hcom_service对象。|
|ipGroup|const char *|入参|要监听的IP，如果明确指定了ipGroup，则直接使用对应的设备。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_service\_set\_cq\_depth<a name="ZH-CN_TOPIC_0000002565999356"></a>

**函数定义<a name="section4292195611128"></a>**

设置cq队列的深度。

**实现方法<a name="section2099511113158"></a>**

void ubs\_hcom\_service\_set\_cq\_depth\(ubs\_hcom\_service service, uint16\_t depth\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|之前创建的ubs_hcom_service对象。|
|depth|uint16_t|入参|cq队列的深度。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_service\_set\_sq\_size<a name="ZH-CN_TOPIC_0000002596637883"></a>

**函数定义<a name="section4292195611128"></a>**

设置SQ队列的大小，默认256。

**实现方法<a name="section2099511113158"></a>**

void ubs\_hcom\_service\_set\_sq\_size\(ubs\_hcom\_service service, uint32\_t sqSize\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|之前创建的ubs_hcom_service对象。|
|sqSize|uint32_t|入参|SQ队列的大小，默认256。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_service\_set\_rq\_size<a name="ZH-CN_TOPIC_0000002566158210"></a>

**函数定义<a name="section4292195611128"></a>**

设置RQ队列的大小，默认256。

**实现方法<a name="section2099511113158"></a>**

void ubs\_hcom\_service\_set\_rq\_size\(ubs\_hcom\_service service, uint32\_t rqSize\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|之前创建的ubs_hcom_service对象。|
|rqSize|uint32_t|入参|RQ队列的大小，默认256。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_service\_set\_polling\_batchsize<a name="ZH-CN_TOPIC_0000002566158822"></a>

**函数定义<a name="section4292195611128"></a>**

设置传输层worker单次poll的个数。

**实现方法<a name="section2099511113158"></a>**

void ubs\_hcom\_service\_set\_polling\_batchsize\(ubs\_hcom\_service service, uint16\_t pollSize\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|之前创建的ubs_hcom_service对象。|
|pollSize|uint16_t|入参|单次poll的个数。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_service\_set\_polling\_timeoutus<a name="ZH-CN_TOPIC_0000002565998952"></a>

**函数定义<a name="section4292195611128"></a>**

设置event polling的超时时间。

**实现说明<a name="section19204713125617"></a>**

void ubs\_hcom\_service\_set\_polling\_timeoutus\(ubs\_hcom\_service service, uint16\_t pollTimeout\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|创建的ubs_hcom_service对象。|
|pollTimeout|uint16_t|入参|event polling超时时间。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_service\_set\_timeout\_threadnum<a name="ZH-CN_TOPIC_0000002565998498"></a>

**函数定义<a name="section4292195611128"></a>**

设置周期任务处理线程数。

**实现说明<a name="section19204713125617"></a>**

void ubs\_hcom\_service\_set\_timeout\_threadnum\(ubs\_hcom\_service service, uint32\_t threadNum\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|创建的ubs_hcom_service对象。|
|threadNum|uint32_t|入参|周期任务处理线程数。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_service\_set\_max\_connection\_cnt<a name="ZH-CN_TOPIC_0000002565999338"></a>

**函数定义<a name="section4292195611128"></a>**

设置最大链接数。

**实现说明<a name="section19204713125617"></a>**

void ubs\_hcom\_service\_set\_max\_connection\_cnt\(ubs\_hcom\_service service, uint32\_t maxConnCount\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|创建的ubs_hcom_service对象。|
|maxConnCount|uint32_t|入参|最大链接数。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_service\_set\_heartbeat\_opt<a name="ZH-CN_TOPIC_0000002565999388"></a>

**函数定义<a name="section4292195611128"></a>**

设置心跳参数配置项。

**实现说明<a name="section19204713125617"></a>**

void ubs\_hcom\_service\_set\_heartbeat\_opt\(ubs\_hcom\_service service, uint16\_t idleSec, uint16\_t probeTimes, uint16\_t intervalSec\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|创建的ubs_hcom_service对象。|
|idleSec|uint16_t|入参|发送心跳保活消息间隔时间。|
|probeTimes|uint16_t|入参|发送心跳探测失败/没收到回复重试次数，超过认为连接已经断开。|
|intervalSec|uint16_t|入参|发送心跳后再次发送时间。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_service\_set\_multirail\_opt<a name="ZH-CN_TOPIC_0000002596638767"></a>

**函数定义<a name="section4292195611128"></a>**

设置多路径参数配置项。

**实现说明<a name="section19204713125617"></a>**

void ubs\_hcom\_service\_set\_multirail\_opt\(ubs\_hcom\_service service, bool enable, uint32\_t threshold\);

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|service|ubs_hcom_service|入参|创建的ubs_hcom_service对象。|
|threshold|uint32_t|入参|MultiRail阈值。|
|enable|bool|入参|MultiRail开关。true：开启false：关闭|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_set\_log\_handler<a name="ZH-CN_TOPIC_0000002596638711"></a>

**函数定义<a name="section4292195611128"></a>**

设置外部日志模板。

**实现方法<a name="section169444462015"></a>**

void ubs\_hcom\_set\_log\_handler\(ubs\_hcom\_log\_handler h\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|h|ubs_hcom_log_handler|入参|外部日志回调函数。|

**返回值<a name="section851917373122"></a>**

无

>[!NOTE]说明
>
>数据类型解释如下：
>typedef void \(\*ubs\_hcom\_log\_handler\)\(int level, const char \*msg\).

#### 传输层<a name="ZH-CN_TOPIC_0000002596757851"></a> 

##### ubs\_hcom\_driver\_register\_tls\_cb<a name="ZH-CN_TOPIC_0000002565999022"></a>

###### 接口使用方法<a name="ZH-CN_TOPIC_0000002566158244"></a>

**函数定义<a name="section4292195611128"></a>**

注册建链双向认证的回调函数，分别用于获取CA证书、获取公钥证书和私钥证书。

**实现方法<a name="section102461413153816"></a>**

uintptr\_t ubs\_hcom\_driver\_register\_tls\_cb\(ubs\_hcom\_driver driver, ubs\_hcom\_tls\_get\_cert\_cb certCb, ubs\_hcom\_tls\_get\_pk\_cb  priKeyCb, ubs\_hcom\_tls\_get\_ca\_cb caCb\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|driver|ubs_hcom_driver|入参|创建的ubs_hcom_driver对象。|
|certCb|Hcom_TlsGetCb|入参|回调函数句柄。|
|priKeyCb|ubs_hcom_tls_get_pk_cb|入参|回调函数句柄。|
|caCb|ubs_hcom_tls_get_ca_cb|入参|回调函数句柄。|

**返回值<a name="section851917373122"></a>**

uintptr\_t，返回内部句柄地址。

###### ubs\_hcom\_tls\_get\_ca\_cb函数类型<a name="ZH-CN_TOPIC_0000002596758353"></a>

**函数定义<a name="section4292195611128"></a>**

typedef int \(\*ubs\_hcom\_tls\_get\_ca\_cb\)\(const char \*name, char \*\*caPath, char \*\*crlPath, ubs\_hcom\_peer\_cert\_verify\_type \*verifyType, ubs\_hcom\_tls\_cert\_verify \*verify\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|name|char *|出参|句柄名字。|
|*caPath|char *|出参|提供的CA证书路径。|
|*crlPath|char *|出参|提供的吊销列表路径。|
|verifyType|ubs_hcom_peer_cert_verify_type *|出参|证书校验方式：C_VERIFY_BY_NONE：不验证对方证书。C_VERIFY_BY_DEFAULT：用UBS Comm默认的方式校验证书。C_VERIFY_BY_CUSTOM_FUNC：用自定义的函数校验证书。|
|verify|ubs_hcom_tls_cert_verify|出参|证书校验函数，当verifyType值为C_VERIFY_BY_CUSTOM_FUNC的时候设置。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示回调函数执行成功。

###### ubs\_hcom\_peer\_cert\_verify\_type函数类型<a name="ZH-CN_TOPIC_0000002565999320"></a>

**函数定义<a name="section4292195611128"></a>**

typedef int \(\*ubs\_hcom\_tls\_cert\_verify\)\(void \*x509, const char \*crlPath\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|x509|void*|入参|加载之后的x509证书。|
|crlPath|char *|入参|提供的吊销列表路径。|

**返回值<a name="section851917373122"></a>**

表示函数执行结果，返回值为0则表示证书验证成功。

###### Hcom\_TlsGetCb<a name="ZH-CN_TOPIC_0000002566158606"></a>

**函数定义<a name="section4292195611128"></a>**

typedef int \(\*ubs\_hcom\_tls\_get\_cert\_cb\)\(const char \*name, char \*\*certPath\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|name|char *|出参|句柄名字。|
|*certPath|char *|出参|提供的公钥证书路径。|

**返回值<a name="section851917373122"></a>**

Bool类型，表示回调函数是否执行成功

- 返回值为true：表示成功。
- 返回值为false：表示失败。

###### ubs\_hcom\_tls\_get\_pk\_cb<a name="ZH-CN_TOPIC_0000002596638175"></a>

**函数定义<a name="section4292195611128"></a>**

typedef int \(\*ubs\_hcom\_tls\_get\_pk\_cb\)\(const char \*name, char \*\*priKeyPath, char \*\*keyPass, ubs\_hcom\_tls\_keypass\_erase \*erase\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|name|char *|出参|句柄名字。|
|*priKeyPath|char *|出参|提供的私钥证书路径。|
|*keyPass|void*|出参|私钥加载的明文密码。|
|erase|ubs_hcom_tls_keypass_erase *|出参|擦除私钥密码的回调函数，当加载完私钥的时候调用。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示回调函数执行成功。

###### ubs\_hcom\_tls\_keypass\_erase<a name="ZH-CN_TOPIC_0000002566158268"></a>

**函数定义<a name="section4292195611128"></a>**

typedef void \(\*ubs\_hcom\_tls\_keypass\_erase\)\(char \*keyPass, int len\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|keyPass|char *|入参|私钥加载的明文密码。|
|len|int|入参|私钥加载的密码长度。|

**返回值<a name="section851917373122"></a>**

无

##### ubs\_hcom\_ep\_post\_send\_raw<a name="ZH-CN_TOPIC_0000002596758687"></a>

**函数定义<a name="section4292195611128"></a>**

向对端发送一个带有op信息的请求。

**实现方法<a name="section124411141193412"></a>**

int ubs\_hcom\_ep\_post\_send\_raw\(ubs\_hcom\_endpoint ep, ubs\_hcom\_send\_request \*req, uint32\_t seqNo\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|建链创建好的EP对象。|
|*req|ubs_hcom_send_request|入参|发送请求信息，使用本地内存来存储数据，数据会被复制，调用后可释放本地内存。|
|seqNo|uint32_t|入参|对端用于回复的序列号。|

##### ubs\_hcom\_ep\_post\_send\_raw\_sgl<a name="ZH-CN_TOPIC_0000002565998462"></a>

**函数定义<a name="section4292195611128"></a>**

向对端发送请求。

**实现方法<a name="section67821473456"></a>**

int ubs\_hcom\_ep\_post\_send\_raw\_sgl\(ubs\_hcom\_endpoint ep, ubs\_hcom\_readwrite\_request\_sgl \*req, uint32\_t seqNo\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|建链创建好的EP对象。|
|*req|ubs_hcom_readwrite_request_sgl|入参|请求信息。|
|seqNo|uint32_t|入参|对方要回复的seqNo必须大于0，对方可以从context.Header().seqNo中获取它；如果seqNo为0，则生成自动递增的数字。在同步发送消息的情况下，请求和响应的seqNo相等。|

**表 2** ubs\_hcom\_readwrite\_request\_sgl结构体<a id="ubs\_hcom\_readwrite\_request\_sgl结构体"></a>

|配置项|类型|默认值|说明|
|--|--|--|--|
|*iov|ubs_hcom_readwrite_sgeStruct ubs_hcom_readwrite_sge{uintptr_t lAddress = 0;uintptr_t rAddress = 0;uint32_t lKey = 0;uint32_t rKey = 0;uint32_t size = 0;}|Nullptr|数组。|
|iovCount|uint16_t|0|小于max count(NET_SGE_MAX_IOV)。|
|upCtxSize|uint16_t|0|上下文大小。|
|upCtxData[16]|char|-|上下文数据。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示发送请求成功。

##### ubs\_hcom\_ep\_post\_read\_sgl<a name="ZH-CN_TOPIC_0000002565998658"></a>

**函数定义<a name="section4292195611128"></a>**

向对端发送一个读请求。

**实现方法<a name="section101971117154613"></a>**

int ubs\_hcom\_ep\_post\_read\_sgl\(ubs\_hcom\_endpoint ep, ubs\_hcom\_readwrite\_request\_sgl \*req\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|建链创建好的EP对象。|
|*req|ubs_hcom_readwrite_request_sgl|入参|读请求信息。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示读成功。

##### ubs\_hcom\_ep\_post\_write\_sgl<a name="ZH-CN_TOPIC_0000002566158248"></a>

**函数定义<a name="section4292195611128"></a>**

向对端发送一个写请求。

**实现方法<a name="section477013312472"></a>**

int ubs\_hcom\_ep\_post\_write\_sgl\(ubs\_hcom\_endpoint ep, ubs\_hcom\_readwrite\_request\_sgl \*req\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|建链创建好的EP对象。|
|*req|ubs_hcom_readwrite_request_sgl|入参|写请求信息。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示写成功。

##### ubs\_hcom\_ep\_receive\_raw<a name="ZH-CN_TOPIC_0000002596638027"></a>

**函数定义<a name="section4292195611128"></a>**

接收消息，仅对NET\_C\_EP\_SELF\_POLLING设置时使用。

**实现方法<a name="section062811419480"></a>**

int ubs\_hcom\_ep\_receive\_raw\(ubs\_hcom\_endpoint ep, int32\_t timeout, ubs\_hcom\_response\_context \*\*ctx\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|建链创建好的EP对象。|
|timeout|int32_t|入参|超时时间，单位s。timeout = 0：表示立即返回。timeout < 0：表示永不超时，通常设置为-1。timeout > 0：表示秒精度超时最大值为2000s。|
|*ctx|ubs_hcom_response_context *|出参|响应的上下文。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示接收消息成功。

##### ubs\_hcom\_ep\_receive\_raw\_sgl<a name="ZH-CN_TOPIC_0000002596757861"></a>

**函数定义<a name="section4292195611128"></a>**

接收对端发送过来的SGL消息。

**实现方法<a name="section147147813616"></a>**

int ubs\_hcom\_ep\_receive\_raw\_sgl\(ubs\_hcom\_endpoint ep, int32\_t timeout, ubs\_hcom\_response\_context \*\*ctx\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|EndPoint。|
|timeout|int32_t|入参|超时时间，单位是秒。0为立刻超时，负数为永不超时。|
|ctx|ubs_hcom_response_context|出参|接收到的消息。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示成功。

##### ubs\_hcom\_estimate\_encrypt\_len<a name="ZH-CN_TOPIC_0000002565999396"></a>

**函数定义<a name="section4292195611128"></a>**

输入原始数据大小的估计加密长度。

**实现方法<a name="section15439201216286"></a>**

uint64\_t ubs\_hcom\_estimate\_encrypt\_len\(ubs\_hcom\_endpoint ep, uint64\_t rawLen\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|建链创建好的EP对象。|
|rawLen|uint64_t|入参|原始数据长度。范围是(0, 18446744073709551571]。|

**返回值<a name="section851917373122"></a>**

返回uint64\_t类型，表示数据加密长度。

##### ubs\_hcom\_encrypt<a name="ZH-CN_TOPIC_0000002596638155"></a>

**函数定义<a name="section4292195611128"></a>**

加密数据。

**实现方法<a name="section13149103452818"></a>**

int ubs\_hcom\_encrypt\(ubs\_hcom\_endpoint ep, const void \*rawData, uint64\_t rawLen, void \*cipher, uint64\_t \*cipherLen\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|建链创建好的EP对象。|
|rawData|void *|入参|原始数据地址。|
|rawLen|uint64_t|入参|原始数据长度。|
|cipher|void *|出参|加密后数据地址。|
|cipherLen|uint64_t|出参|加密后数据长度。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示加密成功。

##### ubs\_hcom\_estimate\_decrypt\_len<a name="ZH-CN_TOPIC_0000002596758585"></a>

**函数定义<a name="section4292195611128"></a>**

输出原始数据大小。

**实现方法<a name="section162954210293"></a>**

uint64\_t ubs\_hcom\_estimate\_decrypt\_len\(ubs\_hcom\_endpoint ep, uint64\_t cipherLen\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|建链创建好的EP对象。|
|cipherLen|uint64_t|入参|加密后的数据长度。|

**返回值<a name="section851917373122"></a>**

返回uint64\_t类型，表示解密后的原始数据长度。

##### ubs\_hcom\_decrypt<a name="ZH-CN_TOPIC_0000002596637917"></a>

**函数定义<a name="section4292195611128"></a>**

解密数据。

**实现方法<a name="section101615185328"></a>**

int ubs\_hcom\_decrypt\(ubs\_hcom\_endpoint ep, const void \*cipher, uint64\_t cipherLen, void \*rawData, uint64\_t \*rawLen\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|建链创建好的EP对象。|
|cipher|void *|入参|待解密的数据地址。|
|cipherLen|uint64_t|入参|待解密的数据长度。|
|rawData|void *|出参|解密后，原始数据地址。|
|rawLen|uint64_t|出参|解密后，原始数据长度。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示解密成功。

##### ubs\_hcom\_send\_fds<a name="ZH-CN_TOPIC_0000002596757799"></a>

**函数定义<a name="section4292195611128"></a>**

发送共享文件的句柄，该接口只支持在SHM协议下使用。

**实现方法<a name="section134351444172517"></a>**

int ubs\_hcom\_send\_fds\(ubs\_hcom\_endpoint ep, int fds\[\], uint32\_t len\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|建链创建好的EP对象。|
|fds|int[]|入参|需要发送的句柄数组。|
|len|uint32_t|入参|发送的句柄数量。范围是[1, 4]。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示句柄发送成功。

##### ubs\_hcom\_receive\_fds<a name="ZH-CN_TOPIC_0000002596757843"></a>

**函数定义<a name="section4292195611128"></a>**

接收共享文件的句柄，该接口只支持在SHM协议下使用。

**实现方法<a name="section756171842613"></a>**

int ubs\_hcom\_receive\_fds\(ubs\_hcom\_endpoint ep, int fds\[\], uint32\_t len, int timeoutSec\)

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|ep|ubs_hcom_endpoint|入参|建链创建好的EP对象。|
|fds|int[]|出参|需要接收的句柄数组。|
|len|uint32_t|入参|接收的句柄数量。范围是[1, 4]。|
|timeoutSec|int|入参|设置接收超时时间，-1表示不设超时。|

**返回值<a name="section851917373122"></a>**

返回值为0则表示句柄发送成功。

## 结构体参考<a name="ZH-CN_TOPIC_0000002596637857"></a>

### C++结构体<a name="ZH-CN_TOPIC_0000002566158314"></a>

#### 服务层结构体<a name="ZH-CN_TOPIC_0000002596758237"></a>

##### UBSHcomServiceOptions<a name="ZH-CN_TOPIC_0000002565999054"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|默认值|描述|
|--|--|--|--|
|maxSendRecvDataSize|uint32_t|1024|双边发送消息时，采用bcopy模式，发送端和接收端预留的内存大小。范围为(0, 524288000]，单位byte。|
|workerGroupId|uint16_t|0|worker线程池编号。UBS Comm支持使用多个线程池，根据ID号进行区分。|
|workerGroupThreadCount|uint16_t|1|worker线程池线程数。|
|workerGroupMode|UBSHcomNetDriverWorkingMode|NET_BUSY_POLLING|worker工作模式。NET_BUSY_POLLING：Worker保持空转，CPU占用高，性能高。NET_EVENT_POLLING：Worker采用事件驱动，CPU占用低，性能相比略低。|
|workerThreadPriority|int8_t|0|worker线程优先级，同线程nice值，取值范围[-20, 19]，值越大优先级越低。|
|workerGroupCpuIdsRange|std::pair<uint32_t, uint32_t>|{UINT32_MAX, UINT32_MAX}|worker线程绑核范围，比如{0,10}表示绑定在CPU 0到CPU 10上，{UINT32_MAX, UINT32_MAX}表示不绑核。|

>[!NOTE]说明 
>双边操作允许发送最大消息的长度，可结合使用场景通过maxSendRecvDataSize来配置。

##### UBSHcomConnectOptions<a name="ZH-CN_TOPIC_0000002566159054"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|默认值|描述|
|--|--|--|--|
|clientGroupId|uint16_t|0|客户端worker线程池ID。|
|serverGroupId|uint16_t|0|服务端worker线程池ID。|
|linkCount|uint8_t|1|链接数。|
|mode|UBSHcomClientPollingMode|WORKER_POLL|客户端调用通信接口时poll的模式。|
|cbType|UBSHcomChannelCallBackType|CHANNEL_FUNC_CB|回调类型。|
|payload|std::string|空|建链发送给服务端的payload。|

##### UBSHcomRequest<a name="ZH-CN_TOPIC_0000002566159076"></a>

**表 1** 参数说明<a id="参数说明"></a>

|配置项|数据类型|默认值|说明|
|--|--|--|--|
|address|void*|nullptr|数据指针。该字段在内部有空指针校验。|
|size|uint32_t|0|数据大小。范围是(0,UINT32_MAX]。|
|key|uint64_t|0|数据地址key值。|
|opcode|uint16_t|0|操作类型。|

##### UBSHcomResponse<a name="ZH-CN_TOPIC_0000002596757925"></a>

**表 1** 参数说明<a id="参数说明"></a>

|配置项|类型|默认值|说明|
|--|--|--|--|
|address|void*|nullptr|数据指针。|
|size|uint32_t|0|数据大小。|
|errorCode|int16_t|0|回复错误码。|

##### UBSHcomReplyContext<a name="ZH-CN_TOPIC_0000002565998558"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|rspCtx|uintptr_t|回复上下文，可从回调context中获取。|
|errorCode|int16_t|回复的错误码。|

##### UBSHcomOneSideRequest<a name="ZH-CN_TOPIC_0000002565999334"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|lAddress|uintptr_t|单边通信本端内存地址。|
|rAddress|uintptr_t|单边通信，对端内存地址。|
|lKey|UBSHcomMemoryKey|单边通信，本端UBSHcomMemoryKey。|
|rKey|UBSHcomMemoryKey|单边通信，对端UBSHcomMemoryKey。|
|size|uint32_t|单边通信，数据大小。范围是(0,  UINT32_MAX]。|

**表 2** UBSHcomMemoryKey<a id="UBSHcomMemoryKey"></a>

|参数名|数据类型|描述|
|--|--|--|
|keys|uint64_t [4]|内存注册后的内存区域key，MultiRail场景下多个设备有多个key（最多4个），非MultiRail场景下只需要1个。|
|tokens|uint64_t|UBC场景下注册内存区域的token value，MultiRail场景下多个设备有多个token value（最多4个），非MultiRail场景下只需要1个。|

##### UBSHcomFlowCtrlOptions<a name="ZH-CN_TOPIC_0000002565998638"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|intervalTimeMs|uint64_t|等待时间，单位是微秒。有效范围[1, 1000]。|
|thresholdByte|uint64_t|阈值数据量，单位为byte。范围是(0, UINT64_MAX]。|
|flowCtrlLevel|UBSHcomFlowCtrlLevel|流控等级。0为死循环等待。1为休眠等待。默认为1。|

##### UBSHcomTlsOptions<a name="ZH-CN_TOPIC_0000002596638717"></a>

- **[参数说明](#ZH-CN_TOPIC_0000002596758615)**  

- **[UBSHcomTLSCaCallback函数类型](#ZH-CN_TOPIC_0000002596637903)**  

- **[UBSHcomTLSCertVerifyCallback函数类型](#ZH-CN_TOPIC_0000002565998586)**  

- **[UBSHcomTLSPrivateKeyCallback函数类型](#ZH-CN_TOPIC_0000002565998636)**  

###### 参数说明<a name="ZH-CN_TOPIC_0000002596758615"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|默认值|描述|
|--|--|--|--|
|caCb|UBSHcomTLSCaCallback|nullptr|建链双向认证的回调函数，用于获取CA证书。|
|cfCb|UBSHcomTLSCertificationCallback|nullptr|建链双向认证的回调函数，用于获取公钥证书。|
|pkCb|UBSHcomTLSPrivateKeyCallback|nullptr|建链双向认证的回调函数，用于获取私钥证书。|
|tlsVersion|UBSHcomTlsVersion|TlsVersion::TLS_1_3|TLS版本，支持TLS1.3，不再支持TLS1.2。|
|netCipherSuite|UBSHcomCipherSuite|UBSHcomCipherSuite::AES_GCM_128|加密算法，取值范围见UBSHcomNetCipherSuite。|
|enableTls|bool|true|是否开启TLS认证。|

###### UBSHcomTLSCaCallback函数类型<a name="ZH-CN_TOPIC_0000002596637903"></a>

**函数定义<a name="section4292195611128"></a>**

using UBSHcomTLSCaCallback = std::function<bool\(const std::string &name, std::string &capath, std::string &crlPath, UBSHcomPeerCertVerifyType &verifyPeerCert, UBSHcomTLSCertVerifyCallback &cb\)\>;

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|name|String|入参|句柄名字。|
|capath|String|出参|提供的CA证书路径。|
|crlPath|String|出参|提供的吊销列表路径。|
|verifyPeerCert|UBSHcomPeerCertVerifyType|出参|证书校验方式：VERIFY_BY_NONE：不验证对方证书。VERIFY_BY_DEFAULT：用UBS Comm默认的方式校验证书。VERIFY_BY_CUSTOM_FUNC：用自定义的函数校验证书。|
|cb|UBSHcomTLSCertVerifyCallback|出参|证书校验函数，当verifyPeerCert值为VERIFY_BY_CUSTOM_FUNC的时候设置。|

**返回值<a name="section851917373122"></a>**

Bool类型，表示回调函数是否执行成功。

- 返回值为true：表示成功。
- 返回值为false：表示失败。

**代码样例<a name="section19872815185412"></a>**

```xml
int Verify(void *x509, const char *path)
{
    return 0;
}
bool CACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    caPath = certPath + "/CA/cacert.pem";
    cb = std::bind(&Verify, std::placeholders::_1, std::placeholders::_2);
    return true;
}
```

###### UBSHcomTLSCertVerifyCallback函数类型<a name="ZH-CN_TOPIC_0000002565998586"></a>

**函数定义<a name="section104221811114"></a>**

using UBSHcomTLSCertificationCallback = std::function<bool\(const std::string &name, std::string &path\)\>;

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|name|String|入参|句柄名字。|
|path|String|出参|提供的公钥证书路径。|

**返回值<a name="section851917373122"></a>**

Bool类型，表示回调函数是否执行成功。

- 返回值为true：表示成功。
- 返回值为false：表示失败。

**代码样例<a name="section277525020254"></a>**

```xml
bool CertCallback(const std::string &name, std::string &value)
{
    value = certPath + "/client/cert.pem";
    return true;
}
```

###### UBSHcomTLSPrivateKeyCallback函数类型<a name="ZH-CN_TOPIC_0000002565998636"></a>

**函数定义<a name="section4292195611128"></a>**

using UBSHcomTLSPrivateKeyCallback = std::function<bool\(const std::string &name, std::string &path, void \*&password, int &length, UBSHcomTLSEraseKeypass &erase\)\>;

**参数说明<a name="section8984192751117"></a>**

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|参数类型|描述|
|--|--|--|--|
|name|String|入参|句柄名字。|
|path|String|出参|提供的私钥证书路径。|
|password|void*|出参|私钥加载的明文密码。|
|length|int|出参|私钥加载的密码长度。|
|erase|UBSHcomTLSEraseKeypass函数类型|出参|擦除私钥密码的回调函数，当加载完私钥的时候调用。|

**返回值<a name="section851917373122"></a>**

Bool类型，表示回调函数是否执行成功。

- 返回值为true：表示成功。
- 返回值为false：表示失败。

**代码样例<a name="section277525020254"></a>**

```xml
void Erase(void *pass, int len) {}

bool PrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len, UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "xxxx";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = certPath + "/client/key.pem";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);
    return true;
}
```

##### UBSHcomConnSecureOptions<a name="ZH-CN_TOPIC_0000002596638015"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|默认值|
|--|--|--|--|
|provider|UBSHcomDriverSecInfoProvider|安全信息提供函数。|nullptr|
|validator|UBSHcomDriverSecInfoValidator|安全信息校验函数。|nullptr|
|magic|uint16_t|建链时校验的魔术字，双方配置不同时，建链失败。可用于版本管理。|256|
|version|uint8_t|版本校验，建链回调时，可以通过NetChannel对象获取。|0|
|secType|UBSHcomNetDriverSecType|安全信息类型。|NetDriverSecType::NET_SEC_DISABLED|

>[!NOTE]说明
>
>数据类型解释如下：
>using UBSHcomDriverSecInfoProvider= std::function<int\(uint64\_t ctx, int64\_t &flag, UBSHcomNetDriverSecType &type, char \*&output, uint32\_t &outLen, bool &needAutoFree\)\>;
>其中，outLen的有效范围为\(0, 2147483646\]。
>using UBSHcomNetDriverEndpointSecInfoValidator = std::function<int\(uint64\_t ctx, int64\_t flag, const char \*input, uint32\_t inputLen\)\>;

##### UBSHcomHeartBeatOptions<a name="ZH-CN_TOPIC_0000002565999350"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|默认值|
|--|--|--|--|
|heartBeatIdleSec|uint16_t|发送心跳保活消息间隔时间。|60|
|heartBeatProbeTimes|uint16_t|发送心跳探测失败/没收到回复重试次数，超过认为连接已经断开。|7|
|heartBeatProbeIntervalSec|uint16_t|发送心跳后再次发送时间。|2|

##### UBSHcomMultiRailOptions<a name="ZH-CN_TOPIC_0000002566159044"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|默认值|
|--|--|--|--|
|threshold|uint32_t|MultiRail阈值。|8192|
|enable|bool|MultiRail开关。true：开启false：关闭|true|

##### UBSHcomIov<a name="ZH-CN_TOPIC_0000002566158212"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|默认值|
|--|--|--|--|
|address|void *|地址值。|nullptr|
|size|uint32_t|数据大小。|0|

##### UBSHcomOneSideSglRequest<a name="ZH-CN_TOPIC_0000002596638691"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|默认值|
|--|--|--|--|
|iov|UBSHcomOneSideRequest|单边iov数组。|nullptr|
|iovCount|uint16_t|iov数量。|0|

##### UBSHcomMemoryKey<a name="ZH-CN_TOPIC_0000002596757889"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|默认值|描述|
|--|--|--|--|
|keys[4]|uint64_t|-|key数组。|
|tokens[4]|uint64_t|-|UBC场景下的token value数组|

##### UBSHcomSglRequest<a name="ZH-CN_TOPIC_0000002596637961"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|默认值|描述|
|--|--|--|--|
|iov|UBSHcomRequest|nullptr|双边iov数组。|
|iovCount|uint16_t|0|iov数量。|

##### UBSHcomTwoSideThreshold<a name="ZH-CN_TOPIC_0000002596757893"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|默认值|
|--|--|--|--|
|splitThreshold|uint32_t|UBC专用。此值表示拆包发送的阈值，也可以当做拆包发送时每个小包的最大长度（含额外头部），一般将其配置成小于等于SegSize的值。可配置范围为[128,  maxSendRecvDataSize]，特别的配置成UINT32_MAX会禁用拆包功能。|UINT32_MAX|
|rndvThreshold|uint32_t|rndv阈值，请求长度大于等于该值，则启用RNDV。|UINT32_MAX|

#### 传输层结构体<a name="ZH-CN_TOPIC_0000002565999266"></a>

##### UBSHcomNetDriverDeviceInfo<a name="ZH-CN_TOPIC_0000002565999306"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|maxSge|int|最大SGL数组元素个数，默认为4。|

##### UBSHcomNetDriverOptions<a name="ZH-CN_TOPIC_0000002565998656"></a>

**表 1** 参数说明<a id="参数说明"></a>

|配置项|类型|默认值|说明|
|--|--|--|--|
|netDeviceIpMask/netDeviceEid|union {char[256]uint8_t[16]} __attribute__((packed))|-|填写ipMask时，为 IP掩码。非UBC多路径场景通过此掩码可查找得到实际设备的IP。监听的本机网卡配置的IP掩码，校验掩码下的网卡是否存在。长度范围为0~256，不包含0。填写eid时，为UB EID (128b)。多路径聚合设备为非IP设备，需用户显式指定。长度为16。|
|netDeviceIpGroup|char[1024]|-|监听的本机网卡配置的IP组，校验IP组下的网卡是否存在。长度范围为0~1024，不包含0。|
|enableTls|bool|true|加密特性开关。|
|secType|UBSHcomNetDriverSecType|NET_SEC_DISABLED|安全类型。|
|tlsVersion|UBSHcomTlsversion|TLS_1_3|TLS的版本。|
|cipherSuite|UBSHcomNetCipherSuite|AES_GCM_128|加密算法。|
|worker setting|
|dontStartWorkers|bool|false|启动Worker开关。false：通用场景，Server端对象，或者有建立event poll链路。true：典型场景，Client端并且只建立self poll链路。|
|mode|UBSHcomNetDriverWorkingMode|NET_BUSY_POLLING|Worker工作模式：NET_BUSY_POLLING = 0NET_EVENT_POLLING = 1|
|workerGroups|char[64]|-|设置Worker组，例如："1,2,3" ，当前有3个Worker组，worker[0]有1个worker，worker[1]有2个worker，worker[2]有3个worker。长度范围为0~64，不包含0。|
|workerGroupsCpuSeNetDriverOobType|char[128]|-|设置Worker线程绑核，例如："1-1,2-3,4-6"的绑核，对应上方举例"1,2,3"3个Worker组的配置，worker[0]有1个Worker线程绑在cpuId 1，worker[1]有2个Worker线程，绑在cpuId 2和3，worker[2]有3个Worker线程，绑在cpuId 4、5、6上。长度范围为0~128，不包含0。|
|workerGroupsThreadPriority|char[64]|--|worker groups thread priority, for example -10,na,9worker thread priority [-20, 19], 19 is the lowest, -20 is the highest, 0 (default) means do not set priority|
|workerThreadPriority|int|0|worker线程优先级设置，[-20, 20]，20为优先级最低，-20为优先级最高，0为不设置优先级。|
|connection attribute|
|oobType|NetDriverOobType|NET_OOB_TCP|设置监听线程的类型，节点间只能配置成NET_OOB_TCP，节点内可以配置成NET_OOB_TCP或NET_OOB_UDS。NET_OOB_TCP=0NET_OOB_UDS=1|
|lbPolicy|UBSHcomNetDriverLBPolicy|NET_ROUND_ROBIN|设置建链时，选择Worker的负载均衡模式。NET_ROUND_ROBIN = 0NET_HASH_IP_PORT = 1|
|magic|uint16_t|256|建链时校验的魔术字，双方配置不同时，建链失败。可使用于版本管理。|
|version|uint8_t|0|版本校验。|
|maxConnectionNum|uint32_t|250|最大连接数。范围为0~UINT32_MAX，不包含0。|
|heartbeat attribute|
|heartBeatIdleTime|uint16_t|60|正常发送心跳的周期。范围为0~1024，不包含0，单位s。|
|heartBeatProbeTimes|uint16_t|7|最大允许发送保活探测包的次数（RDMA不生效）。范围为0~1024，不包含0。|
|heartBeatProbeInterval|uint16_t|2|没有接收到对方确认，继续发送保活探测包的发送频率。范围为0~1024，不包含0。|
|options for protocol|
|tcpUserTimeout|int16_t|-1|TCP协议下，在IO时的超时时间，单位为秒，-1为不设置，0为永不超时，范围为[-1, 1024]。|
|tcpEnableNoDelay|bool|true|TCP协议下，是否设置TCP_NODELAY参数。|
|tcpSendZCopy|bool|false|TCP协议下，是否在双边消息时在UBS Comm内部是否会拷贝用户消息到内部内存。如果不拷贝，则需要保证消息的生命周期长于消息发送完成回调函数调用时。|
|tcpSendBufSize|uint16_t|0|TCP发送缓冲区大小。范围为0~4096，不包含0，单位KB。|
|tcpReceiveBufSize|uint16_t|0|TCP接收缓冲区大小。范围为0~4096，不包含0，单位KB。|
|mrSendReceiveSegCount|uint32_t|8192|双边发送消息时，采用bcopy模式，发送端预留的内存块数量。范围为0~65535，不包含0。|
|mrSendReceiveSegSize|uint32_t|1024|双边发送消息时，采用bcopy模式，发送端和接收端预留的内存大小。范围为0~524288000，不包含0，单位byte。shm:shmChannel mSendDcBuckSize|
|dmSegSize|uint32_t|290|RDMA：使用Device Memory特性时，申请的DM内存大小。|
|dmSegCount|uint32_t|400|RDMA：使用Device Memory特性时，申请的DM内存数量。|
|completionQueueDepth|uint16_t|2048|RDMA：CQ队列深度。SHM：eventqueue size。范围为0~8192，不包含0。|
|maxPostSendCountPerQP|uint16_t|64|RDMA：配置单个链路的最大双边发送个数，该值小于或等于prePostReceiveSizePerQP。范围为0~1024，不包含0。|
|prePostReceiveSizePerQP|uint16_t|64|RDMA：配置单个链路的最大双边接收个数。范围为0~1024，不包含0。|
|pollingBatchSize|uint16_t|4|RDMA：配置批量poll cq的个数，默认值适合busy poll，event poll模式在大并发下可以加大。取值范围[1, 1024]。|
|eventPollingTimeout|uint16_t|500|配置event poll模式的timeout时间，如果没有IO，idle函数将以timeout的频率被调用。范围为0~2000000，不包含0。|
|qpSendQueueSize|uint32_t|256|RDMA：配置发送队列大小（单边+双边），单边的发送队列深度。范围为[16, 65535]。|
|qpReceiveQueueSize|uint32_t|256|RDMA：配置接收队列大小（单边+双边）。范围为[16, 65535]。|
|oobConnHandleThreadCount|uint16_t|2|服务端监听线程数，可使用默认值。取值范围[1, 256]。|
|oobConnHandleQueueCap|uint32_t|4096|服务端监听队列容量，可使用默认值。|
|enableMultiRail|bool|false|MultiRail开关。|
|oobPortRange|char[16]|-|当启用了端口自动选取功能后，端口范围。长度范围为0~16，不包含0。port范围[1024, 65535]。|
|slave|uint8_t|1|当前已废弃|
|ubcMode|UBSHcomUbcMode|UBSHcomUbcMode::LowLatency|UB-C 专用: UB-C 具有多路径能力，发送时使用多条路径可以增大带宽，对于带宽要求不高、时延敏感型业务又提供单路径直连模式。|

>[!NOTICE]说明
>
>- UBS Comm默认开启TLS认证，关闭认证可能存在安全风险，用户可通过enableTls = false进行关闭。
>- 双边操作允许发送最大消息的长度，可结合使用场景通过mrSendReceiveSegSize来配置。

##### UBSHcomNetOobListenerOptions<a name="ZH-CN_TOPIC_0000002565998672"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|默认值|
|--|--|--|--|
|ip|char[16]|监听的IP。|-|
|port|uint16_t|监听的端口号，默认是9980。范围是[1024, 65535]。|9980|
|targetWorkerCount|uint16_t|可用worker数量，0代表全部，默认是全部。|UINT16_MAX|

##### UBSHcomNetOobUDSListenerOptions<a name="ZH-CN_TOPIC_0000002596758103"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|默认值|
|--|--|--|--|
|name|char[96]|监听的UDS name。长度范围是(0, 96)。|-|
|perm|uint16_t|0代表不使用文件，其他情况则使用文件，此参数为其权限，最高为0600。|0600|
|targetWorkerCount|uint16_t|可用worker数量，0代表全部，默认是全部。|UINT16_MAX|
|isCheck|bool|是否校验权限，默认值为true。|true|

##### UBSHcomEpOptions<a name="ZH-CN_TOPIC_0000002565998602"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|tcpBlockingIo|bool|是否为阻塞通信模式，默认为false。|
|cbByWorkerInBlocking|bool|在阻塞模式下，是否由worker线程来调用回调函数，默认为false。|
|sendTimeout|int32_t|发送超时时间，单位为秒，默认永不超时。timeout = 0：表示立即返回。timeout < 0：表示永不超时，通常设置为-1。timeout > 0：表示秒精度超时最大值为2000s。|

##### UBSHcomNetTransRequest<a name="ZH-CN_TOPIC_0000002565998764"></a>

**表 1** 参数说明<a id="参数说明"></a>

|配置项|类型|默认值|说明|
|--|--|--|--|
|lAddress|uintptr_t|0|本地缓存地址。|
|rAddress|uintptr_t|0|远程缓存地址。|
|lKey|uint64_t|0|本地内存区域key。|
|rKey|uint64_t|0|远程内存区域key。|
|size|uint32_t|0|缓存大小。有效范围为(0, UINT32_MAX]。|
|upCtxSize|uint16_t|0|上下文大小。|
|upCtxData|char[64]|-|上下文数据。|
|srcSeg|void *|nullptr|仅UB场景使用，填写发送端的urma_target_seg_t *指针。|
|dstSeg|void *|nullptr|仅UB场景使用，填写目的端的urma_target_seg_t *指针。|

##### UBSHcomNetTransOpInfo<a name="ZH-CN_TOPIC_0000002566158142"></a>

**表 1** 参数说明<a id="参数说明"></a>

|配置项|类型|默认值|说明|
|--|--|--|--|
|seqNo|uint32_t|0|序列号。范围是[0, 1023]。|
|timeout|uint16_t|0|超时时间，单位为秒，默认永不超时。timeout = 0：表示立即返回。timeout < 0：表示永不超时，通常设置为-1。timeout > 0：表示秒精度超时最大值为1200s。|
|errorCode|int16_t|0|用于存放错误码，不需要主动设置。|
|flags|uint8_t|0|标志位。|

##### UBSHcomNetUdsIdInfo<a name="ZH-CN_TOPIC_0000002596758187"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|默认值|
|--|--|--|--|
|pid|uint32_t|进程ID。|0|
|uid|uint32_t|用户ID。|0|
|gid|uint32_t|组ID。|0|

##### UBSHcomNetMemoryAllocatorOptions<a name="ZH-CN_TOPIC_0000002566158110"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|默认值|
|--|--|--|--|
|address|uintptr_t|内存地址。|0|
|size|uint64_t|内存大小。|0|
|minBlockSize|uint32_t|分配时最小单位大小(2的倍数)。范围是[4096, 1073741824]，单位是byte。|0|
|bucketCount|uint32_t|对齐的前提下，HashMap的桶数。|8192|
|alignedAddress|bool|是否对齐。|false|
|cacheTierCount|uint16_t|缓存器的层数。|8|
|cacheBlockCountPerTier|uint16_t|每层有多少个内存块。|16|
|cacheTierPolicy|UBSHcomNetMemoryAllocatorCacheTierPolicy|分层策略，0为times，1为power。|TIER_TIMES|

##### UBSHcomNetTransSglRequest<a name="ZH-CN_TOPIC_0000002565998720"></a>

**表 1** 参数说明<a id="参数说明"></a>

|配置项|类型|默认值|说明|
|--|--|--|--|
|*iov|UBSHcomNetTransSgeIov|Nullptr|消息数组。该字段在内部有空指针校验。|
|iovCount|uint16_t|0|数组长度。最大为4。|
|upCtxSize|uint16_t|0|上下文大小。|
|upCtxData[16]|char|-|上下文数据。|

##### UBSHcomNetTransSgeIov<a name="ZH-CN_TOPIC_0000002565998908"></a>

**表 1** 参数说明<a id="参数说明"></a>

|配置项|类型|默认值|说明|
|--|--|--|--|
|lAddress|uintptr_t|0|本端内存地址。|
|rAddress|uintptr_t|0|对端内存地址。|
|lKey|uint64_t|0|本端key。|
|rKey|uint64_t|0|对端key。|
|size|uint32_t|0|内存大小。|
|memid|unsigned long|0|显示Urmah在rndv中使用的obmm内存。|
|srcSeg|void *|nullptr|仅UB场景使用，填写发送端的urma_target_seg_t *指针。|
|dstSeg|void *|nullptr|仅UB场景使用，填写目的端的urma_target_seg_t *指针。|

##### UBSHcomWorkerGroupInfo<a name="ZH-CN_TOPIC_0000002566158188"></a>

**表 1** 参数说明<a id="参数说明"></a>

|配置项|类型|默认值|说明|
|--|--|--|--|
|threadPriority|int8_t|0|线程优先级。范围：[-20, 19]|
|threadCount|uint16_t|1|线程总数。|
|groupId|uint16_t|0|worker线程中的组ID。|
|cpuIdsRange|std:pair<uint32_t,uint32_t>|-|指定worker线程CPU ID。|

##### UBSHcomNetUdsIdInfo<a name="ZH-CN_TOPIC_0000002566158970"></a>

**表 1** 参数说明<a id="参数说明"></a>

|配置项|类型|默认值|说明|
|--|--|--|--|
|pid|uint32_t|0|进程ID。|
|uid|uint32_t|0|用户ID。|
|gid|uint32_t|0|组ID。|

##### UBSHcomNetTransHeader<a name="ZH-CN_TOPIC_0000002565998532"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|headerCrc|uint32_t|crc值。|
|opCode|int16_t|用户定义的操作码。传输层范围[0, 1023]，service层范围[0, 999]。|
|flags|uint16_t|保留位。|
|seqNo|uint32_t|序列号。|
|timeout|int16_t|超时时间。|
|errorCode|int16_t|错误码。|
|dataLength|uint32_t|数据长度。|
|immData|uint32_t|立即数。|
|extHeaderType|UBSHcomExtHeaderType|传输层payload中是否存在服务层的头部，用户不使用。|

**结构体函数定义<a name="section4292195611128"></a>**

重置opcode、seqNo、errorCode和dataLenagth。

**实现说明<a name="section19204713125617"></a>**

void Invalid\(\);

**参数说明<a name="section8984192751117"></a>**

无

**返回值<a name="section851917373122"></a>**

无

#### 组播结构体<a name="ZH-CN_TOPIC_0000002611712985"></a> 

##### MulticastServiceOptions<a name="ZH-CN_TOPIC_0000002611632885"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|maxSendRecvDataSize|uint32_t|发送数据块最大值。默认值为1024。|
|maxSendRecvDataCount|uint32_t|同时最大发送的数据个数。|
|workerGroupId|uint16_t|worker组的id ,需要从0开始，并且保持唯一。|
|workerGroupThreadCount|uint16_t|worker线程数，如果设置为0的话，不启动worker线程。|
|workerGroupMode|WorkerMode|worker线程工作模式，默认busy_polling。|
|workerThreadPriority|int8_t|线程优先级[-20,19]，19优先级最低，-20优先级最高，同nice值。|
|workerGroupCpuIdsRange|std::pair<uint32_t, uint32_t>|worker绑定的CPU核，默认不绑定。|
|protocol|UBSHcomNetDriverProtocol|组播driver协议类型，默认是RDMA，当前支持RDMA/TCP|
|qpSendQueueSize|uint32_t|qp发送队列大小。|
|qpRecvQueueSize|uint32_t|qp接收队列大小。|
|qpPrePostSize|uint32_t|qp队列预申请大小。|
|qpBatchRePostSize|uint32_t|qp批量还wr的大小。|
|completionQueueDepth|uint16_t|cq队列大小。|
|maxSubscriberNum|uint32_t|一个发布者最大的订阅者数量。|
|publisherWrkGroupNo|uint8_t|subscriber订阅时对应publisher的groupNum。|
|periodicCpuId|int|发布者超时定时器线程绑定的cpuId|

##### MulticastHeartBeatOptions<a name="ZH-CN_TOPIC_0000002581393040"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|heartBeatIdleSec|uint16_t|发送心跳保活消息间隔时间。|
|heartBeatProbeTimes|uint16_t|发送心跳探测失败/没收到回复重试次数，超了认为连接已经断开。|
|heartBeatProbeIntervalSec|uint16_t|发送心跳后再次发送时间。|

##### MultiRequest<a name="ZH-CN_TOPIC_0000002581233124"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|data|void *|数据地址。|
|size|uint32_t|数据大小。|
|lkey|uint64_t|已注册内存的lkey。|

##### MultiResponse<a name="ZH-CN_TOPIC_0000002611712987"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|data|void *|数据地址。|
|size|uint32_t|数据大小。|

### C结构体<a name="ZH-CN_TOPIC_0000002566158176"></a>

#### 服务层结构体<a name="ZH-CN_TOPIC_0000002565998678"></a>

##### ubs\_hcom\_mr\_info<a name="ZH-CN_TOPIC_0000002566158524"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|lAddress|uintptr_t|mr内存地址。|
|lKey|ubs_hcom_oneside_key|mr key。|
|size|uint32_t|mr内存大小。|

##### ubs\_hcom\_channel\_reply\_context<a name="ZH-CN_TOPIC_0000002596757867"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|rspCtx|void *|用于回复的RSP上下文。|
|errorCode|int16_t|失败场景下回复的错误码。|

##### ubs\_hcom\_oneside\_request<a name="ZH-CN_TOPIC_0000002566158112"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名称|数据类型|描述|
|--|--|--|
|lAddress|uintptr_t|本地的地址。|
|rAddress|uintptr_t|远端的地址。|
|lKey|ubs_hcom_oneside_key|本地MR的key。|
|rKey|ubs_hcom_oneside_key|远端MR的key。|
|size|uint32_t|数据大小。有效范围为(0, UINT32_MAX]。|

##### ubs\_hcom\_channel\_callback<a name="ZH-CN_TOPIC_0000002565999390"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|cb|ubs_hcom_channel_cb_func|回调函数。|
|arg|void *|回调函数的参数指针。|

##### ubs\_hcom\_flowctl\_opts<a name="ZH-CN_TOPIC_0000002565998772"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|flowCtrlLevel|ubs_hcom_channel_flowctl_level|流控等级，默认为1。0为死循环等待。1为休眠等待。|
|intervalTimeMs|uint16_t|等待时间，单位是微秒。有效范围[1,1000]。|
|thresholdByte|uint64_t|阈值数据量，单位为byte。范围是(0,UINT64_MAX]。|

##### ubs\_hcom\_service\_options<a name="ZH-CN_TOPIC_0000002566158544"></a>

**表 1** 参数说明<a id="参数说明"></a>

|配置项|数据类型|默认值|说明|
|--|--|--|--|
|mrSendReceiveSegSize|uint32_t|-|双边发送消息时，采用bcopy模式，发送端和接收端预留的内存大小。范围为(0, 524288000]，单位byte。|
|workerGroupId|uint16_t|-|worker group编号。|
|workerGroupThreadCount|uint16_t|-|worker group内worker线程数量。|
|workerGroupMode|ubs_hcom_service_worker_mode|-|busy poll/event poll模式。|
|workerThreadPriority|int8_t|-|worker线程优先级设置，[-20, 20]，20为优先级最低，-20为优先级最高，0为不设置优先级。|
|workerGroupCpuRange|char[64]|-|worker group内worker线程cpu绑核id，例：'0-0'，为绑在cpu id 0上。ID为UINT32_MAX即为不绑。|

>[!NOTE]说明
>UBS Comm默认开启TLS认证，关闭认证可能存在安全风险，用户可通过Service\_SetUBSHcomTlsOptions函数进行关闭。

##### ubs\_hcom\_service\_connect\_options<a name="ZH-CN_TOPIC_0000002596638593"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|类型|默认值|说明|
|--|--|--|--|
|clientGroupId|uint16_t|-|client端worker group索引。|
|serverGroupId|uint16_t|-|server端worker group索引。|
|linkCount|uint8_t|-|channel内单个路径的ep数量。多路径场景下实际ep数量为linkCount * 路径数。|
|mode|ubs_hcom_service_polling_mode|-|channel内ep poll模式。|
|cbType|ubs_hcom_channel_cb_type|-|cb方式，每次传入或全局同一个cb。|
|payLoad|char[512]|-|用户可携带的自定义信息。|

##### ubs\_hcom\_channel\_request<a name="ZH-CN_TOPIC_0000002565998896"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|类型|默认值|说明|
|--|--|--|--|
|address|void *|-|消息内存首地址。|
|size|uint32_t|-|消息大小。|
|opcode|uint16_t|-|用户自定义的opcode。|

##### ubs\_hcom\_channel\_response<a name="ZH-CN_TOPIC_0000002566159036"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|类型|默认值|说明|
|--|--|--|--|
|address|void *|-|消息内存首地址。|
|size|uint32_t|-|消息大小。|
|errorCode|uint16_t|-|用户自定义的errorCode，对端回复时用户可填写。|

##### ubs\_hcom\_twoside\_threshold<a name="ZH-CN_TOPIC_0000002566158192"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|类型|默认值|说明|
|--|--|--|--|
|splitThreshold|uint32_t|UINT32_MAX|拆包阈值。UBC专用。此值表示拆包发送的阈值，也可以当做拆包发送时每个小包的最大长度（含额外头部），一般将其配置成小于等于SegSize的值。可配置范围为 [128, maxSendRecvDataSize]，特别的配置成UINT32_MAX会禁用拆包功能。|
|rndvThreshold|uint32_t|UINT32_MAX|rndv阈值。|

##### ubs\_hcom\_oneside\_key<a name="ZH-CN_TOPIC_0000002596757855"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|类型|默认值|说明|
|--|--|--|--|
|keys|uint64_t[4]|-|已注册内存的key。多路径场景每个路径有一个key，单路径场景只使用key[0]。|

#### 传输层结构体<a name="ZH-CN_TOPIC_0000002596638725"></a> 

##### ubs\_hcom\_send\_request<a name="ZH-CN_TOPIC_0000002596638763"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|类型|默认值|说明|
|--|--|--|--|
|data|uintptr_t|0|准备发送给对方的数据地址。|
|size|uint32_t|0|数据大小。|
|upCtxSize|uint16_t|0|用户上下文大小。|
|upCtxData|char[16]|-|用户上下文。|

##### ubs\_hcom\_opinfo<a name="ZH-CN_TOPIC_0000002596637877"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名参数说明|数据类型|描述|
|--|--|--|
|seqNo|uint32_t|序列号。范围是[0, 1023]。|
|timeout|uint16_t|超时时间，单位为秒。0为立刻超时，负数为永不超时。范围[-1, 1200]。|
|errorCode|int16_t|错误码。|
|flags|uint8_t|标志位。|

##### ubs\_hcom\_device\_info<a name="ZH-CN_TOPIC_0000002565998622"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|maxSge|int|RDMA设备信息，最大的SGL的iov count。|

##### ubs\_hcom\_readwrite\_request<a name="ZH-CN_TOPIC_0000002596637979"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|lMRA|uintptr_t|本端MR的地址。|
|rMRA|uintptr_t|远端MR的地址。|
|lKey|uint64_t|本端MR的密钥。|
|rKey|uint64_t|远端MR的密钥。|
|size|uint32_t|数据大小。|
|upCtxSize|uint16_t|用户上下文的大小。|
|upCtxData|char[16]|用户上下文。|

##### ubs\_hcom\_readwrite\_sge<a name="ZH-CN_TOPIC_0000002596637973"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|lAddress|uintptr_t|本端MR的地址。|
|rAddress|uintptr_t|远端MR的地址。|
|lKey|uint64_t|本端MR的密钥。|
|rKey|uint64_t|远端MR的密钥。|
|size|uint32_t|数据大小。|

##### ubs\_hcom\_readwrite\_request\_sgl<a name="ZH-CN_TOPIC_0000002566158378"></a>

**表 1** 参数说明<a id="参数说明"></a>

|配置项|类型|默认值|说明|
|--|--|--|--|
|*iov|ubs_hcom_readwrite_sge|-|消息数组。|
|iovCount|uint16_t|-|小于max count(NET_SGE_MAX_IOV)。|
|upCtxSize|uint16_t|-|上下文大小。|
|upCtxData[16]|char|-|上下文数据。|

##### ubs\_hcom\_memory\_region\_info<a name="ZH-CN_TOPIC_0000002596637943"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|lAddress|uintptr_t|MR的地址。|
|lKey|uint64_t|MR的key。|
|size|uint32_t|MR的大小。|

##### ubs\_hcom\_request\_context<a name="ZH-CN_TOPIC_0000002565998644"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|type|ubs_hcom_request_type|请求的操作类型[0, 8]。|
|opCode|uint16_t|操作码，取值范围[0, 1023]。|
|flags|uint16_t|header中的标志位。|
|timeout|int16_t|超时时间。|
|errorCode|int16_t|错误码。|
|result|int|结果值0代表成功。|
|msgData|void *|数据指针。用于接收操作。|
|msgSize|uint32_t|数据大小。用于接收操作。|
|seqNo|uint32_t|序列号。用于post send raw。|
|ep|ubs_hcom_endpoint|建链创建好的EP对象。|
|originalSend|ubs_hcom_send_request|用于C_OP_REQUEST_POSTED复制的结构体信息。|
|originalReq|ubs_hcom_readwrite_request|用于C_OP_READWRITE_DONE复制的结构体信息。|
|originalSglReq|ubs_hcom_readwrite_request_sgl|用于C_OP_READWRITE_DONE复制的结构体信息。|

##### ubs\_hcom\_response\_context<a name="ZH-CN_TOPIC_0000002596757909"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|opCode|uint16_t|操作编号。|
|seqNo|uint32_t|序列号。|
|msgData|void *|接收到的消息。|
|msgSize|uint32_t|消息长度。|

##### ubs\_hcom\_uds\_id\_info<a name="ZH-CN_TOPIC_0000002596758281"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|pid|uint32_t|进程ID。|
|uid|uint32_t|用户ID。|
|gid|uint32_t|组ID。|

##### ubs\_hcom\_driver\_opts<a name="ZH-CN_TOPIC_0000002566159022"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|类型|默认值|说明|
|--|--|--|--|
|mode|ubs_hcom_driver_working_mode|-|Worker工作模式：C_BUSY_POLLING = 0C_EVENT_POLLING = 1|
|mrSendReceiveSegCount|uint32_t|-|双边发送消息时，采用bcopy模式，发送端预留的内存块数量。范围为(0, 65535]。|
|mrSendReceiveSegSize|uint32_t|-|双边发送消息时，采用bcopy模式，发送端和接收端预留的内存大小。范围为(0, 524288000]，单位byte。shm:shmChannel mSendDcBuckSize|
|netDeviceIpMask|char[256]|-|监听的本机网卡配置的IP掩码，校验掩码下的网卡是否存在。长度范围为(0, 256]。|
|netDeviceIpGroup|char[1024]|-|RDMA协议下，用于精准筛选IP地址所对应的网卡。长度范围为(0, 1024]。|
|completionQueueDepth|uint16_t|-|RDMA ：CQ队列深度。SHM：eventqueue size。范围为(0, 8192]。|
|maxPostSendCountPerQP|uint16_t|-|RDMA：配置单个链路的最大双边发送个数，该值小于或等于 prePostReceiveSizePerQP。范围为(0, 1024]。|
|prePostReceiveSizePerQP|uint16_t|-|RDMA：配置单个链路的最大双边接收个数。范围为(0, 1024]。|
|pollingBatchSize|uint16_t|-|RDMA：配置批量poll cq的个数，默认值适合busy poll，event poll模式在大并发下可以加大。取值范围[1, 1024]。|
|qpSendQueueSize|uint32_t|-|RDMA：配置发送队列大小（单边+双边），单边的发送队列深度。取值范围[16, 65536]。|
|qpReceiveQueueSize|uint32_t|-|RDMA：配置接收队列大小（单边+双边）。取值范围[16, 65536]。|
|dontStartWorkers|uint16_t|-|启动Worker开关。false：通用场景，Server端对象，或者有建立event poll链路。true：典型场景，Client端并且只建立self poll链路。|
|workerGroups|char[64]|-|设置Worker组，例如："1,2,3" ，当前有3个Worker组，worker[0]有1个worker，worker[1]有2个worker，worker[2]有3个worker。长度范围为(0, 64]。|
|workerGroupsCpuSet|char[128]|-|设置Worker线程绑核，例如："1-1,2-3,4-6"的绑核，对应上方举例"1,2,3"3个Worker组的配置，worker[0]有1个Worker线程绑在cpuId 1，worker[1]有2个Worker线程，绑在cpuId 2和3，worker[2]有3个Worker线程，绑在cpuId 4、5、6上。长度范围为(0, 128]。|
|workerThreadPriority|int|-|Worker线程优先级，取值范围[-20, 20]，20优先级最低，-20优先级最高，0不进行优先级设置。|
|heartBeatIdleTime|uint16_t|-|正常发送心跳的周期。取值范围[1, 1024]，单位s。|
|heartBeatProbeTimes|uint16_t|-|最大允许发送保活探测包的次数（RDMA不生效）。取值范围[1, 1024]，单位s。|
|heartBeatProbeInterval|uint16_t|-|没有接收到对方确认，继续发送保活探测包的发送频率。取值范围[1, 1024]，单位s。|
|tcpUserTimeout|int16_t|-|IO时的超时时间，-1表示直接返回，0表示永久阻塞。取值范围[1, 1024]，单位s。|
|tcpEnableNoDelay|bool|-|TCP的TCP_NODELAY选项设置。|
|tcpSendZCopy|bool|-|TCP协议下，是否在双边消息时在UBS Comm内部是否会拷贝用户消息到内部内存。如果不拷贝，则需要保证消息的生命周期长于消息发送完成回调函数调用时。|
|tcpSendBufSize|uint16_t|-|TCP发送缓冲区大小。范围为(0, 4096]，单位KB。|
|tcpReceiveBufSize|uint16_t|-|TCP接收缓冲区大小。范围为(0, 4096]，单位KB。|
|enableTls|uint16_t|-|加密特性开关，取值仅为0/1|
|secType|ubs_hcom_driver_sec_type|-|建链校验方式。|
|tlsVersion|ubs_hcom_driver_tls_version|-|TLS的版本。|
|cipherSuite|ubs_hcom_driver_cipher_suite|-|加密算法。|
|oobType|ubs_hcom_driver_oob_type|-|设置监听线程的类型，节点间只能配置成C_NET_OOB_TCP，节点内可以配置成C_NET_OOB_TCP或C_NET_OOB_UDS。C_NET_OOB_TCP=0C_NET_OOB_UDS=1|
|version|uint8_t|-|版本校验。|
|maxConnectionNum|uint32_t|-|最大连接数。范围为(0, UINT32_MAX]。|
|oobPortRange|char[16]|-|当启用了端口自动选取功能后，端口范围。长度范围为(0, 16]。port范围[1024, 65535]。|

>[!NOTE]说明
>UBS Comm默认开启TLS认证，关闭认证可能存在安全风险，用户可通过enableTls = false进行关闭。

##### ubs\_hcom\_driver\_listen\_opts<a name="ZH-CN_TOPIC_0000002596758539"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|name|char[16]|监听的IP地址。长度范围是(0, 16]。|
|port|uint16_t|监听的端口号。范围是[1024, 65535]。|
|targetWorkerCount|uint16_t|可用worker数量，0代表全部，默认可使用worker数量为全部。|

##### ubs\_hcom\_driver\_uds\_listen\_opts<a name="ZH-CN_TOPIC_0000002566158148"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|name|char[96]|监听的UDS name。长度范围(0, 96)。|
|perm|uint16_t|0代表不使用文件，其他情况则使用文件，此参数为其权限，最高为0600。|
|targetWorkerCount|uint16_t|可用worker数量，0代表全部，默认是全部。|

##### ubs\_hcom\_memory\_allocator\_options<a name="ZH-CN_TOPIC_0000002596638461"></a>

**表 1** 参数说明<a id="参数说明"></a>

|参数名|数据类型|描述|
|--|--|--|
|address|uintptr_t|内存地址。|
|size|uint64_t|内存大小。|
|minBlockSize|uint32_t|分配时最小单位大小(2的倍数)。范围是[4096, 1073741824]，单位是byte。|
|bucketCount|uint32_t|对齐的前提下，HashMap的桶数。|
|alignedAddress|uint16_t|是否对齐。0：不对齐。1：对齐。|
|cacheTierCount|uint16_t|缓存器的层数。|
|cacheBlockCountPerTier|uint16_t|每层有多少个内存块。|
|cacheTierPolicy|ubs_hcom_memory_allocator_cache_tier_policy|分层策略。0：times1：power|

## 枚举值参考<a name="ZH-CN_TOPIC_0000002565998714"></a>

### C++枚举值<a name="ZH-CN_TOPIC_0000002596638709"></a>

#### 服务层枚举值<a name="ZH-CN_TOPIC_0000002596638009"></a> 

##### UBSHcomChannelBrokenPolicy<a name="ZH-CN_TOPIC_0000002566158872"></a>

**枚举说明<a name="section4292195611128"></a>**

断链策略，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|BROKEN_ALL|0|当一个EP断开则断开channel。|
|RECONNECT|1|当一个EP断开尝试重连，若失败则断开channel。|
|KEEP_ALIVE|2|当一个EP断开，保持其他EP正常功能，直至所有EP断开。|

##### Operation<a name="ZH-CN_TOPIC_0000002566158302"></a>

**枚举说明<a name="section4292195611128"></a>**

此UBSHcomServiceContext所包含的操作类别，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|SER_RECEIVED|0|接收到新消息。|
|SER_RECEIVED_RAW|1|接收到新raw消息。|
|SER_SENT|2|消息发送完成。|
|SER_SENT_RAW|3|raw消息发送完成。|
|SER_ONE_SIDE|4|单边操作完成。|
|SER_INVALID_OP_TYPE|255|非法操作。|

##### UBSHcomClientPollingMode<a name="ZH-CN_TOPIC_0000002566158630"></a>

**枚举说明<a name="section4292195611128"></a>**

客户端poll模式

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|WORKER_POLL|0|使用worker线程poll。|
|SELF_POLL|1|使用调用通信接口的线程poll。|
|UNKNOWN|255|未知。|

##### UBSHcomChannelCallBackType<a name="ZH-CN_TOPIC_0000002565999034"></a>

**枚举说明<a name="section4292195611128"></a>**

Channel的回调函数类型，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|CHANNEL_FUNC_CB|0|会使用用户传入到异步通信方法中的回调函数。|
|CHANNEL_GLOBAL_CB|1|会使用注册给NetService的回调函数。|

##### UBSHcomFlowCtrlLevel<a name="ZH-CN_TOPIC_0000002565998832"></a>

**枚举说明<a name="section4292195611128"></a>**

Channel的流控等待策略，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|HIGH_LEVEL_BLOCK|0|忙循环等待。|
|LOW_LEVEL_BLOCK|1|睡眠指定时长等待。|

##### UBSHcomChannelState<a name="ZH-CN_TOPIC_0000002565999260"></a>

**枚举说明<a name="section4292195611128"></a>**

Channel状态，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|CH_NEW|0|新建状态。|
|CH_ESTABLISHED|1|就绪状态。|
|CH_CLOSE|2|关闭状态。|
|CH_DESTROY|3|销毁状态。|

##### UBSHcomOobType<a name="ZH-CN_TOPIC_0000002596638431"></a>

**枚举说明<a name="section4292195611128"></a>**

建链类型，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|TCP|0|TCP建链方式。|
|UDS|1|UDS建链方式。|

##### UBSHcomSecType<a name="ZH-CN_TOPIC_0000002596638373"></a>

**枚举说明<a name="section4292195611128"></a>**

建链安全校验，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|NET_SEC_DISABLED|0|不校验。|
|NET_SEC_VALID_ONE_WAY|1|单边校验。|
|NET_SEC_VALID_TWO_WAY|2|双边校验。|

#### 传输层枚举值<a name="ZH-CN_TOPIC_0000002566158330"></a>

##### UBSHcomNetEndPointState<a name="ZH-CN_TOPIC_0000002565999398"></a>

**枚举说明<a name="section4292195611128"></a>**

描述EP此时所处的状态，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|NEP_NEW|0|新建状态。|
|NEP_ESTABLISHED|1|就绪状态。|
|NEP_BROKEN|2|断开状态。|
|NEP_BUFF|3|-|

##### UBSHcomNetCipherSuite<a name="ZH-CN_TOPIC_0000002565998856"></a>

**枚举说明<a name="section4292195611128"></a>**

加密算法，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|AES_GCM_128|0|AES_GCM_128。|
|AES_GCM_256|1|AES_GCM_256。|
|AES_CCM_128|2|AES_CCM_128。|
|CHACHA20_POLY1305|3|CHACHA20_POLY1305。|

##### UBSHcomTlsVersion<a name="ZH-CN_TOPIC_0000002596758037"></a>

**枚举说明<a name="section4292195611128"></a>**

TLS的版本信息，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|TLS_1_2|771|1.2版本。|
|TLS_1_3|772|1.3版本。|

##### NN\_OpType<a name="ZH-CN_TOPIC_0000002596758307"></a>

**枚举说明<a name="section4292195611128"></a>**

此UBSHcomNetRequestContext所包含的操作类别，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|NN_SENT|0|消息发送完成。|
|NN_SENT_RAW|1|raw消息发送完成。|
|NN_SENT_RAW_SGL|2|SGL消息发送完成。|
|NN_RECEIVED|3|接收到新消息。|
|NN_RECEIVED_RAW|4|接收到新raw消息。|
|NN_WRITTEN|5|写操作完成。|
|NN_READ|6|读操作完成。|
|NN_SGL_WRITTEN|7|SGL写操作完成。|
|NN_SGL_READ|8|SGL读操作完成。|
|NN_INVALID_OP_TYPE|255|非法操作。|

##### UBSHcomNetMemoryAllocatorType<a name="ZH-CN_TOPIC_0000002596638219"></a>

**枚举说明<a name="section4292195611128"></a>**

内存分配器类型，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|DYNAMIC_SIZE|0|动态大小。|
|DYNAMIC_SIZE_WITH_CACHE|1|动态大小，配有缓存器。|

##### UBSHcomNetMemoryAllocatorCacheTierPolicy<a name="ZH-CN_TOPIC_0000002596757817"></a>

**枚举说明<a name="section4292195611128"></a>**

内存分配器的缓存器分级策略，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|TIER_TIMES|0|基准值的倍数策略。|
|TIER_POWER|1|基准值乘以2的幂数策略。|

##### UBSHcomPeerCertVerifyType<a name="ZH-CN_TOPIC_0000002566159008"></a>

**枚举说明<a name="section4292195611128"></a>**

对端校验类型，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|VERIFY_BY_NONE|0|对端不需要校验。|
|VERIFY_BY_DEFAULT|1|对端使用UBS Comm内部校验方式。|
|VERIFY_BY_CUSTOM_FUNC|2|对端使用用户定义的校验方式。|

##### UBSHcomNetDriverSecType<a name="ZH-CN_TOPIC_0000002565999272"></a>

**枚举说明<a name="section4292195611128"></a>**

UBSHcomNetDriver校验类型，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|NET_SEC_DISABLED|0|不需要校验。|
|NET_SEC_VALID_ONE_WAY|1|单边校验，仅服务端校验客户端。|
|NET_SEC_VALID_TWO_WAY|2|双边校验。|

##### NetDriverOobType<a name="ZH-CN_TOPIC_0000002596637821"></a>

**枚举说明<a name="section4292195611128"></a>**

OOB建链时协议，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|NET_OOB_TCP|0|TCP协议。|
|NET_OOB_UDS|1|UDS协议。|
|NET_OOB_UB|2|UBC自举建链，仅支持UBC协议配置。|

##### UBSHcomNetDriverWorkingMode<a name="ZH-CN_TOPIC_0000002566158206"></a>

**枚举说明<a name="section4292195611128"></a>**

worker线程工作模式，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|NET_BUSY_POLLING|0|busy polling。|
|NET_EVENT_POLLING|1|event polling。|

##### UBSHcomNetDriverLBPolicy<a name="ZH-CN_TOPIC_0000002566158356"></a>

**枚举说明<a name="section4292195611128"></a>**

worker线程分配策略，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|NET_ROUND_ROBIN|0|轮询策略。|
|NET_HASH_IP_PORT|1|根据IP和Port进行取哈希值分配策略。|

##### UBSHcomNetDriverProtocol<a name="ZH-CN_TOPIC_0000002566159034"></a>

**枚举说明<a name="section4292195611128"></a>**

UBSHcomNetDriver通信协议。

|枚举名|数值|描述|
|--|--|--|
|RDMA|0|RDMA。|
|TCP|1|TCP。|
|UDS|2|UDS。|
|SHM|3|SHM。|
|RDMA_MLX5_RC|4|需求MLX5网卡的RDMA。|
|UBC|7|UBC。|
|HSHMEM|8|HSHMEM。|
|UNKNOWN|255|不支持协议。|

##### UBSHcomUbcMode<a name="ZH-CN_TOPIC_0000002565998568"></a>

**枚举说明<a name="section4292195611128"></a>**

UBC协议专用能力。UB-C 具有多路径能力，发送时使用多条路径可以增大带宽，对于带宽要求不高、时延敏感型业务又提供单路径直连模式。

|枚举名|数值|描述|
|--|--|--|
|Disabled|-1|禁用多路径能力（默认）。|
|LowLatency|0|低时延模式，使用单路径发送。|
|HighBandwidth|1|高带宽模式，使用多条路径发送。|

#### 组播枚举值<a name="ZH-CN_TOPIC_0000002611632887"></a>

- **[PublisherState](#ZH-CN_TOPIC_0000002581393042)**  

- **[SubscriberRspStatus](#ZH-CN_TOPIC_0000002581233126)**  

##### PublisherState<a name="ZH-CN_TOPIC_0000002581393042"></a>

**枚举说明<a name="section4292195611128"></a>**

发布者状态。

|枚举名|数值|描述|
|--|--|--|
|PUB_NEW|0|新建状态。|
|PUB_ESTABLISHED|1|就绪状态。|
|PUB_CLOSE|2|关闭状态。|
|PUB_DESTROY|3|销毁状态。|

##### SubscriberRspStatus<a name="ZH-CN_TOPIC_0000002581233126"></a>

**枚举说明<a name="section4292195611128"></a>**

订阅者回复状态。

|枚举名|数值|描述|
|--|--|--|
|SUCCESS|0|已回复（成功）。|
|INIT|1|初始还未发送。|
|SEND_ERROR|2|发送错误。|
|TIMEOUT|3|超时未回复。|
|BROKEN|4|订阅者离线。|
|UNKNOWN_ERROR|5|其他未知错误。|

### C枚举值<a name="ZH-CN_TOPIC_0000002596758155"></a>

#### 服务层枚举值<a name="ZH-CN_TOPIC_0000002566158182"></a>

##### ubs\_hcom\_channel\_cb\_type<a name="ZH-CN_TOPIC_0000002596638663"></a>

**枚举说明<a name="section4292195611128"></a>**

Channel的回调函数类型，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_CHANNEL_FUNC_CB|0|会使用用户传入到异步通信方法中的回调函数。|
|C_CHANNEL_GLOBAL_CB|1|会使用注册给NetService的回调函数。|

##### ubs\_hcom\_service\_context\_type<a name="ZH-CN_TOPIC_0000002596637847"></a>

**枚举说明<a name="section4292195611128"></a>**

此ubs\_hcom\_service\_context所包含的操作类别，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|SER_RECEIVED|0|接收到新消息。|
|SER_RECEIVED_RAW|1|接收到新raw消息。|
|SER_SENT|2|消息发送完成。|
|SER_SENT_RAW|3|raw消息发送完成。|
|SER_ONE_SIDE|4|单边操作完成。|
|SERVICE_RNDV|5|rndv请求。|
|SER_INVALID_OP_TYPE|255|非法操作。|

##### ubs\_hcom\_channel\_flowctl\_level<a name="ZH-CN_TOPIC_0000002596757785"></a>

**枚举说明<a name="section4292195611128"></a>**

Channel的流控等待策略，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|HIGH_LEVEL_BLOCK|0|忙循环等待。|
|LOW_LEVEL_BLOCK|1|睡眠指定时长等待。|

##### ubs\_hcom\_service\_worker\_mode<a name="ZH-CN_TOPIC_0000002596758207"></a>

**枚举说明<a name="section4292195611128"></a>**

worker线程工作模式，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_SERVICE_BUSY_POLLING|0|busy polling。|
|C_SERVICE_EVENT_POLLING|1|event polling。|

##### ubs\_hcom\_service\_lb\_policy<a name="ZH-CN_TOPIC_0000002565999086"></a>

**枚举说明<a name="section4292195611128"></a>**

worker线程分配策略，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|SERVICE_ROUND_ROBIN|0|轮询策略。|
|SERVICE_HASH_IP_PORT|1|根据IP和Port进行取哈希值分配策略。|

##### ubs\_hcom\_service\_cipher\_suite<a name="ZH-CN_TOPIC_0000002596758359"></a>

**枚举说明<a name="section4292195611128"></a>**

加密算法，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_SERVICE_AES_GCM_128|0|AES_GCM_128。|
|C_SERVICE_AES_GCM_256|1|AES_GCM_256。|
|C_SERVICE_AES_CCM_128|2|AES_CCM_128。|
|C_SERVICE_CHACHA20_POLY1305|3|CHACHA20_POLY1305。|

##### ubs\_hcom\_service\_tls\_version<a name="ZH-CN_TOPIC_0000002566158214"></a>

**枚举说明<a name="section4292195611128"></a>**

TLS的版本信息，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_SERVICE_TLS_1_2|771|1.2版本。|
|C_SERVICE_TLS_1_3|772|1.3版本。|

##### ubs\_hcom\_service\_secure\_type<a name="ZH-CN_TOPIC_0000002566158264"></a>

**枚举说明<a name="section4292195611128"></a>**

NetService校验类型，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_SERVICE_NET_SEC_DISABLED|0|不需要校验。|
|C_SERVICE_NET_SEC_VALID_ONE_WAY|1|单边校验，仅服务端校验客户端。|
|C_SERVICE_NET_SEC_VALID_TWO_WAY|2|双边校验。|

##### ubs\_hcom\_service\_channel\_policy<a name="ZH-CN_TOPIC_0000002596638611"></a>

**枚举说明<a name="section4292195611128"></a>**

Channel断链策略，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_CHANNEL_BROKEN_ALL|0|当一个EP断开则断开channel。|
|C_CHANNEL_RECONNECT|1|当一个EP断开尝试重连，若失败则断开channel。|
|C_CHANNEL_KEEP_ALIVE|2|当一个EP断开，保持其他EP正常功能，直至所有EP断开。|

##### ubs\_hcom\_service\_channel\_handler\_type<a name="ZH-CN_TOPIC_0000002596757819"></a>

**枚举说明<a name="section4292195611128"></a>**

链路相关的回调函数类型，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_CHANNEL_NEW|0|新建链的回调函数。|
|C_CHANNEL_BROKEN|1|断链的回调函数。|

##### ubs\_hcom\_service\_handler\_type<a name="ZH-CN_TOPIC_0000002596637951"></a>

**枚举说明<a name="section4292195611128"></a>**

通信相关的回调函数类型，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_SERVICE_REQUEST_RECEIVED|0|接收新消息的回调函数。|
|C_SERVICE_REQUEST_POSTED|1|消息发送完成的回调函数。|
|C_SERVICE_READWRITE_DONE|2|读写完成的回调函数。|

##### ubs\_hcom\_service\_type<a name="ZH-CN_TOPIC_0000002596637919"></a>

**枚举说明<a name="section4292195611128"></a>**

NetService通信协议，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_SERVICE_RDMA|0|RDMA。|
|C_SERVICE_TCP|1|TCP。|
|C_SERVICE_UDS|2|UDS。|
|C_SERVICE_SHM|3|SHM。|
|C_SERVICE_UBC|6|UBC。|
|C_SERVICE_HSHMEM|7|HSHMEM（北冥版本暂不支持）。|

##### ubs\_hcom\_service\_polling\_mode<a name="ZH-CN_TOPIC_0000002565999424"></a>

#### 传输层枚举值<a name="ZH-CN_TOPIC_0000002565998458"></a>

##### ubs\_hcom\_request\_type<a name="ZH-CN_TOPIC_0000002566158740"></a>

**枚举说明<a name="section4292195611128"></a>**

此ubs\_hcom\_request\_context所包含的操作类别，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_SENT|0|消息发送完成。|
|C_SENT_RAW|1|raw消息发送完成。|
|C_SENT_RAW_SGL|2|SGL消息发送完成。|
|C_RECEIVED|3|接收到新消息。|
|C_RECEIVED_RAW|4|接收到新raw消息。|
|C_WRITTEN|5|写操作完成。|
|C_READ|6|读操作完成。|
|C_SGL_WRITTEN|7|SGL写操作完成。|
|C_SGL_READ|8|SGL读操作完成。|

##### ubs\_hcom\_driver\_working\_mode<a name="ZH-CN_TOPIC_0000002565999312"></a>

**枚举说明<a name="section4292195611128"></a>**

worker线程工作模式，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_BUSY_POLLING|0|busy polling。|
|C_EVENT_POLLING|1|event polling。|

##### ubs\_hcom\_driver\_type<a name="ZH-CN_TOPIC_0000002565999208"></a>

**枚举说明<a name="section4292195611128"></a>**

UBSHcomNetDriver通信协议，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_DRIVER_RDMA|0|RDMA。|
|C_DRIVER_TCP|1|TCP。|
|C_DRIVER_UDS|2|UDS。|
|C_DRIVER_SHM|3|SHM。|
|C_DRIVER_UBC|6|UBC。|

##### ubs\_hcom\_driver\_oob\_type<a name="ZH-CN_TOPIC_0000002565998604"></a>

**枚举说明<a name="section4292195611128"></a>**

OOB建链时协议，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_NET_OOB_TCP|0|TCP协议。|
|C_NET_OOB_UDS|1|UDS协议。|

##### ubs\_hcom\_driver\_sec\_type<a name="ZH-CN_TOPIC_0000002565999418"></a>

**枚举说明<a name="section4292195611128"></a>**

ubs\_hcom\_driver校验类型，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_NET_SEC_DISABLED|0|不需要校验。|
|C_NET_SEC_VALID_ONE_WAY|1|单边校验，仅服务端校验客户端。|
|C_NET_SEC_VALID_TWO_WAY|2|双边校验。|

##### ubs\_hcom\_driver\_tls\_version<a name="ZH-CN_TOPIC_0000002566158412"></a>

**枚举说明<a name="section4292195611128"></a>**

TLS的版本信息，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_TLS_1_2|771|1.2版本。|
|C_TLS_1_3|772|1.3版本。|

##### ubs\_hcom\_driver\_cipher\_suite<a name="ZH-CN_TOPIC_0000002596638743"></a>

**枚举说明<a name="section4292195611128"></a>**

加密算法，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_AES_GCM_128|0|AES_GCM_128。|
|C_AES_GCM_256|1|AES_GCM_256。|
|C_AES_CCM_128|2|AES_CCM_128。|
|C_CHACHA20_POLY1305|3|CHACHA20_POLY1305。|

##### ubs\_hcom\_peer\_cert\_verify\_type<a name="ZH-CN_TOPIC_0000002565999354"></a>

**枚举说明<a name="section4292195611128"></a>**

对端校验类型，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_VERIFY_BY_NONE|0|对端不需要校验。|
|C_VERIFY_BY_DEFAULT|1|对端使用UBS Comm内部校验方式。|
|C_VERIFY_BY_CUSTOM_FUNC|2|对端使用用户定义的校验方式。|

##### ubs\_hcom\_memory\_allocator\_cache\_tier\_policy<a name="ZH-CN_TOPIC_0000002596758161"></a>

**枚举说明<a name="section4292195611128"></a>**

内存分配器的缓存器分级策略，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_TIER_TIMES|0|基准值的倍数策略。|
|C_TIER_POWER|1|基准值乘以2的幂数策略。|

##### ubs\_hcom\_memory\_allocator\_type<a name="ZH-CN_TOPIC_0000002565998610"></a>

**枚举说明<a name="section4292195611128"></a>**

内存分配器类型，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_DYNAMIC_SIZE|0|动态大小。|
|C_DYNAMIC_SIZE_WITH_CACHE|1|动态大小，配有缓存器。|

##### ubs\_hcom\_ep\_handler\_type<a name="ZH-CN_TOPIC_0000002566159074"></a>

**枚举说明<a name="section4292195611128"></a>**

链路相关的回调函数类型，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_EP_NEW|0|新建链的回调函数。|
|C_EP_BROKEN|1|断链的回调函数。|

##### ubs\_hcom\_op\_handler\_type<a name="ZH-CN_TOPIC_0000002565999028"></a>

**枚举说明<a name="section4292195611128"></a>**

通信相关的回调函数类型，如[**表 1** RDMA协议错误码](#RDMA协议错误码)所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_OP_REQUEST_RECEIVED|0|接收新消息的回调函数。|
|C_OP_REQUEST_POSTED|1|消息发送完成的回调函数。|
|C_OP_READWRITE_DONE|2|读写完成的回调函数。|

##### ubs\_hcom\_polling\_mode<a name="ZH-CN_TOPIC_0000002566158108"></a>

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|NET_C_EP_SELF_POLLING|0|self ep模式。|
|NET_C_EP_EVENT_POLLING|1|非self ep模式。|

##### ubs\_hcom\_service\_polling\_mode<a name="ZH-CN_TOPIC_0000002565998600"></a>

**枚举说明<a name="section4292195611128"></a>**

client poll信息模式，如下表所示。

**表 1** 枚举说明<a id="枚举说明"></a>

|枚举名|数值|描述|
|--|--|--|
|C_CLIENT_WORKER_POLL|0|非self poll 模式。|
|C_CLIENT_SELF_POLL|1|self poll模式。|

## 环境变量参考<a name="ZH-CN_TOPIC_0000002565999402"></a>

**表 1** 环境变量参数<a id="环境变量参数"></a>

|参数名称|参数类型|**参数说明**|**取值范围**|缺省值|
|--|--|--|--|--|
|HCOM_FILE_PATH_PREFIX|String|UBS Comm生成的文件路径的前缀，通过前缀保证文件只会在当前路径下（需要已存在相应路径）创建删除。|－|－|
|HCOM_OPENSSL_PATH|String|表示UBS Comm依赖的OpenSSL或HITLS库的路径。OpenSSL的路径为libssl.so和libcrypt.so的目录路径。HiTLS的路径为libhitls_cca.so、libhitls_tls.so、libhitls_pse.so、libhitls_crypto.so、libhitls_bsl.so、libsecurec.so的目录路径（可根据版本说明书中的CCA版本号获取hitls相关库）。|－|－|
|HCOM_TRACE_LEVEL|int|UBS Comm的打点日志等级。0：未开启打点。1：打开高优先级打点。2：打开中优先级打点。3：打开低优先级打点。|0 ～ 3|0|
|HCOM_QP_TRAFFIC_CLASS|int|UBS Comm中的RDMA协议的traffic_class字段设置优先级。|0 ～ 255|106|
|HCOM_SHM_EXCHANGE_FD_QUEUE_SIZE|int|UBS Comm发送fds内部队列的大小。|10 ～ 256|10|
|HCOM_CONNECTION_RETRY_TIMES|int|UBS Comm建链重试的次数。|1 ～ 10|5|
|HCOM_CONNECTION_RETRY_INTERVAL_SEC|int|UBS Comm建链重试的间隔时间。单位：s。|1 ～ 60|20|
|HCOM_SET_LOG_LEVEL|int|UBS Comm打印日志级别。0：打印debug、info、warn和error日志。1：打印info、warn和error日志。2：打印warn和error日志。3：打印error日志。|0 ～ 3|1|
|HCOM_CONNECTION_RECV_TIMEOUT_SEC|int|建链时recv接收超时时间设置。单位：s。|1 ~ 7200|300|
|HCOM_CONNECTION_SEND_TIMEOUT_SEC|int|建链时send发送超时时间设置。单位：s。|1 ~ 7200|300|
|HCOM_ENABLE_TRACE|int|UBS  Comm trace打点开关。|0：打印level 0打点1：打印level 1打点|-|
|HCOM_INLINE_THRESHOLD|int|双边inline阈值。|0 ~ UINT32_MAX(实际上限与网卡相关)|0|
|HCOM_ENABLE_SPLIT_SEND|int|设置是否拆包发送。|0：不拆包1：拆包|0|
|HCOM_SHM_MAX_ENQUEUE_STUCK_TIME|int|SHM协议事件处理超时时间。单位：s。|1 ~ 7200|10|
|HCOM_SHM_ENQUEUE_TIMEOUT|int|SHM协议写队列时间超时时间。单位：s。|1 ~ 7200|20|
|HCOM_UB_CONNECTION_POLL_TIMEOUT|long|公知Jetty接收消息超时时间。单位：s。|1 ~ 180|60|
|HCOM_SLAVE_NUM|-|硬分区环境使用，已废弃。|-|-|

环境变量设置示例如下：

```xml
export HCOM_FILE_PATH_PREFIX="/home/uds/socket/file"
export HCOM_OPENSSL_PATH="/home/openssl"
export HCOM_TRACE_LEVEL=0
export HCOM_QP_TRAFFIC_CLASS=106
export HCOM_SHM_EXCHANGE_FD_QUEUE_SIZE=10
export HCOM_CONNECTION_RETRY_TIMES=5
export HCOM_CONNECTION_RETRY_INTERVAL_SEC=2
export HCOM_SET_LOG_LEVEL=1
```

## 错误码<a name="ZH-CN_TOPIC_0000002566159040"></a>

### 服务层错误码<a name="ZH-CN_TOPIC_0000002596757837"></a>

**表 1** 服务层错误码<a id="服务层错误码"></a>

|错误码数|错误码|含义|
|--|--|--|
|0|SER_OK|成功。|
|500|SER_ERROR|内部错误。|
|501|SER_INVALID_PARAM|无效参数。|
|502|SER_NEW_OBJECT_FAILED|对象生成失败。|
|503|SER_CREATE_TIMEOUT_THREAD_FAILED|创建超时处理线程失败。|
|504|SER_NEW_MESSAGE_DATA_FAILED|生成消息失败。|
|505|SER_NOT_ESTABLISHED|NetChannel未建链。|
|506|SER_STORE_SEQ_DUP|序列号重复。|
|507|SER_STORE_SEQ_NO_FOUND|序列号不存在。|
|508|SER_RSP_SIZE_TOO_SMALL|消息大小不一致。|
|509|SER_TIMEOUT|超时。|
|510|SER_TIMER_NOT_WORK|超时处理线程开启失败。|
|511|SER_NOT_ENABLE_RNDV|开启Rndv失败。|
|512|SER_RNDV_FAILED_BY_PEER|对端使用Rndv失败。|
|513|SER_CHANNEL_ID_DUP|Channel Id重复。|
|514|SER_EP_NOT_BROKEN_ALL|NetChannel中所有EP未发生断链。|
|515|SER_CHANNEL_NOT_EXIST|NetChannel不存在。|
|516|SER_CHANNEL_RECONNECT_OVER_WINDOW|-|
|517|SER_EP_BROKEN_DURING_CONNECTING|NetChannel中所有EP均断链。|
|518|SER_NOT_SUPPORT_SERVER_RECONNECT|不支持重建链。|
|519|SER_STOP|服务停止。|
|520|SER_NULL_INSTANCE|空指针。|
|521|SER_UNSUPPORTED|不支持的操作。|
|522|SER_INVALID_IP|非法IP。|
|523|SER_MALLOC_FAILED|分配内存失败。|
|524|SER_SPLIT_INVALID_MSG|拆包消息无效。|

### 传输层错误码<a name="ZH-CN_TOPIC_0000002596758695"></a>

**表 1** 传输层错误码<a id="传输层错误码"></a>

|错误码数|错误码|含义|
|--|--|--|
|0|NN_OK|成功。|
|100|NN_ERROR|内部错误。|
|101|NN_INVALID_IP|无效IP地址。|
|102|NN_NEW_OBJECT_FAILED|创建对象失败。|
|103|NN_INVALID_PARAM|参数无效。|
|104|NN_TWO_SIDE_MESSAGE_TOO_LARGE|双边消息size过大。|
|105|NN_INVALID_OPCODE|无效opCode。|
|106|NN_EP_NOT_ESTABLISHED|EP未建链。|
|107|NN_EP_NOT_INITIALIZED|EP未初始化。|
|108|NN_BLOCK_QUEUE_SEM_INIT_FAILED|队列初始化失败。|
|109|NN_TIMEOUT|超时。|
|110|NN_INVALID_OPERATION|无效操作。|
|111|NN_MALLOC_FAILED|获得内存失败。|
|112|NN_SEQ_NO_NOT_MATCHED|seqNo不匹配。|
|113|T_INITIALIZED|UBSHcomNetDriver未初始化。|
|114|NN_GET_BUFF_FAILED|获取缓存失败。|
|115|NN_MSG_TIMEOUT|超时。|
|116|NN_MSG_CANCELED|写操作取消。|
|117|NN_MSG_ERROR|消息错误。|
|118|NN_CONNECT_REFUSED|连接拒绝。|
|119|NN_CONNECT_PROTOCOL_MISMATCH|连接协议不匹配。|
|120|NN_INVALID_LKEY|无效内存key。|
|121|NN_EP_BROKEN|EP断链。|
|122|NN_EP_CLOSE|EP关闭。|
|123|NN_PARAM_INVALID|参数无效。|
|124|NN_OOB_LISTEN_SOCKET_ERROR|带外链路监听开启失败。|
|125|NN_OOB_CONN_SEND_ERROR|带外链路发送失败。|
|126|NN_OOB_CONN_RECEIVE_ERROR|带外链路接收失败。|
|127|NN_OOB_CONN_CB_NOT_SET|带外链路连接回调未设置。|
|128|NN_OOB_CLIENT_SOCKET_ERROR|带外链路客户端发起连接失败。|
|129|NN_OOB_SSL_INIT_ERROR|加密初始化失败。|
|130|NN_OOB_SSL_WRITE_ERROR|加密写失败。|
|131|NN_OOB_SSL_READ_ERROR|加密读失败。|
|132|NN_HEARTBEAT_CREATE_EPOLL_FAILED|心跳检测创建失败。|
|133|NN_HEARTBEAT_SET_SOCKET_OPT_FAILED|心跳检测设置失败。|
|134|NN_HEARTBEAT_IP_ALREADY_EXISTED|心跳检测IP地址已存在。|
|135|NN_HEARTBEAT_IP_ADD_FAILED|心跳检测IP地址添加失败。|
|136|NN_HEARTBEAT_IP_ADD_EPOLL_FAILED|心跳检测IP地址添加失败。|
|137|NN_HEARTBEAT_IP_REMOVE_EPOLL_FAILED|心跳检测IP地址移除失败。|
|138|NN_HEARTBEAT_IP_NO_FOUND|心跳检测IP地址未匹配。|
|139|NN_ENCRYPT_FAILED|加密失败。|
|140|NN_DECRYPT_FAILED|解密失败。|
|141|NN_OOB_SEC_PROCESS_ERROR|认证失败。|
|142|NN_EXCHANGE_FD_NOT_SUPPORT|不支持交换Fd。|
|143|NN_VALIDATE_HEADER_CRC_INVALID|校验Header CRC无效。|
|144|NN_UDS_ID_INFO_NOT_SUPPORT|不支持获取UDS ID。|
|145|NN_GET_UDS_ID_INFO_FAILED|获取UDS ID失败。|
|146|NN_VERSION_CHECK_FAILED|版本校验失败。|
|147|NN_URMA_ACCESS_ABRT|urma返回了错误消息，远端内存访问失败。|
|148|NN_URMA_ACK_TIMEOUT|urma返回了错误消息，本端send ack超时。对端rqe不足，一般是由于发送速率大于接收速率导致，偶现情况下建议等待后重试。路由配置错误，对端收到了消息但没有回复ACK，建议排查路由表。|

>[!NOTE]说明
>
>部分常见错误码详细说明：
>
>- 114：在RDMA和TCP协议的双边非SGL通信方式时，为了发送消息的持久化和RDMA特性需求，需要把用户发送的消息内容拷贝到UBS Comm内部预申请的内存中。但是在并发很大的情况下，可能将预申请的内存耗尽，在耗尽的时候如果再发送双边非SGL消息时就会产生此错误码。解决方式可以是调大UBSHcomNetDriverOptions中的mrSendReceiveSegCount参数来扩大预申请内存；如果是对端接收压力过大导致本端发送也可以调整对端接收队列的长度prePostReceiveSizePerQP。
>- 128：在进行建链的时候客户端建链失败时会返回此错误。请检查服务端是否启动并且启动监听线程，然后检查客户端发起建链的IP地址和端口是否和服务端监听的一致，推荐先启动服务端，再使用客户端去建链。

### RDMA协议错误码<a name="ZH-CN_TOPIC_0000002566158716"></a>

**表 1** RDMA协议错误码<a id="RDMA协议错误码"></a>

|错误码数|错误码|含义|
|--|--|--|
|0|RR_OK|成功。|
|200|RR_PARAM_INVALID|参数无效。|
|201|RR_MEMORY_ALLOCATE_FAILED|分配内存失败。|
|202|RR_NEW_OBJECT_FAILED|创建对象失败。|
|203|RR_OPEN_FILE_FAILED|打开文件失败。|
|204|RR_READ_FILE_FAILED|读取文件失败。|
|205|RR_DEVICE_FAILED_OPEN|得到RDMA设备失败。|
|206|RR_DEVICE_INDEX_OVERFLOW|RDMA设备序号异常。|
|207|RR_DEVICE_OPEN_FAILED|打开RDMA设备失败。|
|208|RR_DEVICE_FAILED_GET_IF_ADDRESS|获得网卡地址失败。|
|209|RR_DEVICE_NO_IF_MATCHED|获得符合IP地址的网卡地址失败。|
|210|RR_DEVICE_NO_IF_TO_GID_MATCHED|获得符合IP地址的RDMA设备GID。|
|211|RR_DEVICE_INVALID_IP_MASK|IP地址掩码异常。|
|212|RR_MR_REG_FAILED|Memory Region(MR)注册失败。|
|213|RR_CQ_NOT_INITIALIZED|Completion Queue(CQ)初始化失败。|
|214|RR_CQ_POLLING_FAILED|Poll CQ方法异常。|
|215|RR_CQ_POLLING_TIMEOUT|Poll CQ超时。|
|216|RR_CQ_POLLING_ERROR_RESULT|Poll CQ结果错误。|
|217|RR_CQ_POLLING_UNMATCHED_OPCODE|Poll CQ结果opcode不匹配。|
|218|RR_CQ_EVENT_GET_FAILED|Poll事件失败。|
|219|RR_CQ_EVENT_NOTIFY_FAILED|通知CQ失败。|
|220|RR_CQ_WC_WRONG|poll CQ后的完成事件的状态异常。|
|221|RR_CQ_EVENT_GET_TIMOUT|poll CQ超时。|
|222|RR_QP_CREATE_FAILED|创建Queue Pair(QP)失败。|
|223|RR_QP_NOT_INITIALIZED|初始化QP失败。|
|224|RR_QP_CHANGE_STATE_FAILED|更新QP状态失败。|
|225|RR_QP_POST_RECEIVE_FAILED|发起接收请求失败。|
|226|RR_QP_POST_SEND_FAILED|发起发送请求失败。|
|227|RR_QP_POST_READ_FAILED|发起读取请求失败。|
|228|RR_QP_POST_WRITE_FAILED|发起写请求失败。|
|229|RR_QP_RECEIVE_CONFIG_ERR|收发相关参数设定失败。|
|230|RR_QP_POST_SEND_WR_FULL|发送队列满。|
|231|RR_QP_ONE_SIDE_WR_FULL|单边请求队列满。|
|232|RR_QP_CTX_FULL|上下文耗尽。|
|233|RR_QP_CHANGE_ERR|更新QP状态至停止失败。|
|234|RR_OOB_LISTEN_SOCKET_ERROR|带外链路监听开启失败。|
|235|RR_OOB_CONN_SEND_ERROR|带外链路发送失败。|
|236|RR_OOB_CONN_RECEIVE_ERROR|带外链路接收失败。|
|237|RR_OOB_CONN_CB_NOT_SET|带外链路连接回调未设置。|
|238|RR_OOB_CLIENT_SOCKET_ERROR|带外链路客户端发起连接失败。|
|239|RR_OOB_SSL_INIT_ERROR|加密初始化失败。|
|240|RR_OOB_SSL_WRITE_ERROR|加密写失败。|
|241|RR_OOB_SSL_READ_ERROR|加密读失败。|
|242|RR_EP_NOT_INITIALIZED|EP未初始化。|
|243|RR_WORKER_NOT_INITIALIZED|Worker未初始化。|
|244|RR_WORKER_BIND_CPU_FAILED|Worker线程绑定CPU失败。|
|245|RR_WORKER_REQUEST_HANDLER_NOT_SET|Worker的新消息回调函数未注册。|
|246|RR_WORKER_SEND_POSTED_HANDLER_NOT_SET|Worker的消息发送回调函数未注册。|
|247|RR_WORKER_ONE_SIDE_DONE_HANDLER_NOT_SET|Worker的单边消息回调函数未注册。|
|248|RR_WORKER_FAILED_ADD_QP|Worker线程添加QP失败。|
|249|RR_HEARTBEAT_CREATE_EPOLL_FAILED|心跳检测创建失败。|
|250|RR_HEARTBEAT_SET_SOCKET_OPT_FAILED|心跳检测设置失败。|
|251|RR_HEARTBEAT_IP_ALREADY_EXISTED|心跳检测IP地址已存在。|
|252|RR_HEARTBEAT_IP_ADD_FAILED|心跳检测IP地址添加失败。|
|253|RR_HEARTBEAT_IP_ADD_EPOLL_FAILED|心跳检测IP地址添加失败。|
|254|RR_HEARTBEAT_IP_REMOVE_EPOLL_FAILED|心跳检测IP地址移除失败。|
|255|RR_HEARTBEAT_IP_NO_FOUND|心跳检测IP地址未匹配。|

>[!NOTE]说明
>
>部分常见错误码详细说明：
>230：RDMA的双边请求发起时，有限制长度的发送队列来限制并发，如果并发过大时，可能耗尽队列导致出现此错误。解决方式可以通过调大UBSHcomNetDriverOptions中的prePostReceiveSizePerQP和qpSendQueueSize来扩大队列，这个队列的值是取上述两个参数的较小值。
