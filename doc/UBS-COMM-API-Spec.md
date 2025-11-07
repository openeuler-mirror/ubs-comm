[TABLE]

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

本文档详细描述了UBS Comm对外提供的API接口信息，包括API接口参数解释和使用样例等内容。

## 读者对象

本文档主要适用于以下工程师：

- 技术支持工程师

- 二次开发工程师

- 维护工程师

## 符号约定

在本文中可能出现下列标志，它们所代表的含义如下。

[TABLE]

## 修改记录

[TABLE]

# 目 录

[前言 [iii](#前言)](#前言)

[1 介绍 [1](#介绍)](#介绍)

[2 API 2.0 [2](#api-2.0)](#api-2.0)

[2.1 基础API参考 [2](#基础api参考)](#基础api参考)

[2.1.1 C++ API [2](#c-api)](#c-api)

[2.1.1.1 服务层API [2](#服务层api)](#服务层api)

[2.1.1.1.1 UBSHcomService::Create [2](#ubshcomservicecreate)](#ubshcomservicecreate)

[2.1.1.1.2 UBSHcomService::Destroy [3](#ubshcomservicedestroy)](#ubshcomservicedestroy)

[2.1.1.1.3 UBSHcomService::Bind [3](#ubshcomservicebind)](#ubshcomservicebind)

[2.1.1.1.4 UBSHcomService::Start [4](#ubshcomservicestart)](#ubshcomservicestart)

[2.1.1.1.5 UBSHcomService::Connect [5](#ubshcomserviceconnect)](#ubshcomserviceconnect)

[2.1.1.1.6 UBSHcomService::Disconnect [5](#ubshcomservicedisconnect)](#ubshcomservicedisconnect)

[2.1.1.1.7 UBSHcomService::RegisterMemoryRegion [6](#ubshcomserviceregistermemoryregion)](#ubshcomserviceregistermemoryregion)

[2.1.1.1.8 UBSHcomService::DestroyMemoryRegion [7](#ubshcomservicedestroymemoryregion)](#ubshcomservicedestroymemoryregion)

[2.1.1.1.9 UBSHcomService::RegisterChannelBrokenHandler [7](#ubshcomserviceregisterchannelbrokenhandler)](#ubshcomserviceregisterchannelbrokenhandler)

[2.1.1.1.10 UBSHcomService::RegisterIdleHandler [8](#ubshcomserviceregisteridlehandler)](#ubshcomserviceregisteridlehandler)

[2.1.1.1.11 UBSHcomService::RegisterRecvHandler [8](#ubshcomserviceregisterrecvhandler)](#ubshcomserviceregisterrecvhandler)

[2.1.1.1.12 UBSHcomService::RegisterSendHandler [9](#ubshcomserviceregistersendhandler)](#ubshcomserviceregistersendhandler)

[2.1.1.1.13 UBSHcomService::RegisterOneSideHandler [10](#ubshcomserviceregisteronesidehandler)](#ubshcomserviceregisteronesidehandler)

[2.1.1.1.14 UBSHcomChannel::Send [10](#ubshcomchannelsend)](#ubshcomchannelsend)

[2.1.1.1.15 UBSHcomChannel::Call [11](#ubshcomchannelcall)](#ubshcomchannelcall)

[2.1.1.1.16 UBSHcomChannel::Reply [13](#ubshcomchannelreply)](#ubshcomchannelreply)

[2.1.1.1.17 UBSHcomChannel::Get [14](#ubshcomchannelget)](#ubshcomchannelget)

[2.1.1.1.18 UBSHcomChannel::Put [14](#ubshcomchannelput)](#ubshcomchannelput)

[2.1.1.1.19 UBSHcomChannel::Recv [15](#ubshcomchannelrecv)](#ubshcomchannelrecv)

[2.1.1.1.20 UBSHcomChannel::SetFlowControlConfig [16](#ubshcomchannelsetflowcontrolconfig)](#ubshcomchannelsetflowcontrolconfig)

[2.1.1.1.21 UBSHcomChannel::SetChannelTimeOut [16](#ubshcomchannelsetchanneltimeout)](#ubshcomchannelsetchanneltimeout)

[2.1.1.1.22 UBSHcomChannel::SetUBSHcomTwoSideThreshold [17](#ubshcomchannelsetubshcomtwosidethreshold)](#ubshcomchannelsetubshcomtwosidethreshold)

[2.1.1.1.23 UBSHcomChannel::GetId [17](#ubshcomchannelgetid)](#ubshcomchannelgetid)

[2.1.1.1.24 UBSHcomChannel::GetPeerConnectPayload [18](#ubshcomchannelgetpeerconnectpayload)](#ubshcomchannelgetpeerconnectpayload)

[2.1.1.1.25 UBSHcomChannel::SetTraceId [18](#ubshcomchannelsettraceid)](#ubshcomchannelsettraceid)

[2.1.1.1.26 UBSHcomServiceContext::Result [19](#ubshcomservicecontextresult)](#ubshcomservicecontextresult)

[2.1.1.1.27 UBSHcomServiceContext::Channel [19](#ubshcomservicecontextchannel)](#ubshcomservicecontextchannel)

[2.1.1.1.28 UBSHcomServiceContext::OpType [19](#ubshcomservicecontextoptype)](#ubshcomservicecontextoptype)

[2.1.1.1.29 UBSHcomServiceContext::RspCtx [20](#ubshcomservicecontextrspctx)](#ubshcomservicecontextrspctx)

[2.1.1.1.30 UBSHcomServiceContext::ErrorCode [20](#ubshcomservicecontexterrorcode)](#ubshcomservicecontexterrorcode)

[2.1.1.1.31 UBSHcomServiceContext::OpCode [20](#ubshcomservicecontextopcode)](#ubshcomservicecontextopcode)

[2.1.1.1.32 UBSHcomServiceContext::MessageData [21](#ubshcomservicecontextmessagedata)](#ubshcomservicecontextmessagedata)

[2.1.1.1.33 UBSHcomServiceContext::MessageDataLen [21](#ubshcomservicecontextmessagedatalen)](#ubshcomservicecontextmessagedatalen)

[2.1.1.1.34 UBSHcomServiceContext::Clone [21](#ubshcomservicecontextclone)](#ubshcomservicecontextclone)

[2.1.1.1.35 UBSHcomServiceContext::IsTimeout [22](#ubshcomservicecontextistimeout)](#ubshcomservicecontextistimeout)

[2.1.1.1.36 UBSHcomServiceContext::Invalidate [22](#ubshcomservicecontextinvalidate)](#ubshcomservicecontextinvalidate)

[2.1.1.1.37 UBSHcomService::SetEnableMrCache [23](#ubshcomservicesetenablemrcache)](#ubshcomservicesetenablemrcache)

[2.1.1.2 传输层API [23](#传输层api)](#传输层api)

[2.1.1.2.1 UBSHcomNetDriver::Instance [23](#ubshcomnetdriverinstance)](#ubshcomnetdriverinstance)

[2.1.1.2.2 UBSHcomNetDriver::DestroyInstance [24](#ubshcomnetdriverdestroyinstance)](#ubshcomnetdriverdestroyinstance)

[2.1.1.2.3 UBSHcomNetDriver::LocalSupport [25](#ubshcomnetdriverlocalsupport)](#ubshcomnetdriverlocalsupport)

[2.1.1.2.4 UBSHcomNetDriver::MultiRailGetDevCount [25](#ubshcomnetdrivermultirailgetdevcount)](#ubshcomnetdrivermultirailgetdevcount)

[2.1.1.2.5 UBSHcomNetDriver::Initialize [26](#ubshcomnetdriverinitialize)](#ubshcomnetdriverinitialize)

[2.1.1.2.6 UBSHcomNetDriver::UnInitialize [26](#ubshcomnetdriveruninitialize)](#ubshcomnetdriveruninitialize)

[2.1.1.2.7 UBSHcomNetDriver::Start [27](#ubshcomnetdriverstart)](#ubshcomnetdriverstart)

[2.1.1.2.8 UBSHcomNetDriver::Stop [27](#ubshcomnetdriverstop)](#ubshcomnetdriverstop)

[2.1.1.2.9 UBSHcomNetDriver::CreateMemoryRegion [28](#ubshcomnetdrivercreatememoryregion)](#ubshcomnetdrivercreatememoryregion)

[2.1.1.2.10 UBSHcomNetDriver::DestroyMemoryRegion [28](#ubshcomnetdriverdestroymemoryregion)](#ubshcomnetdriverdestroymemoryregion)

[2.1.1.2.11 UBSHcomNetDriver::Connect [29](#ubshcomnetdriverconnect)](#ubshcomnetdriverconnect)

[2.1.1.2.12 UBSHcomNetDriver::DestroyEndpoint [31](#ubshcomnetdriverdestroyendpoint)](#ubshcomnetdriverdestroyendpoint)

[2.1.1.2.13 UBSHcomNetDriver::OobIpAndPort [31](#ubshcomnetdriveroobipandport)](#ubshcomnetdriveroobipandport)

[2.1.1.2.14 UBSHcomNetDriver::GetOobIpAndPort [32](#ubshcomnetdrivergetoobipandport)](#ubshcomnetdrivergetoobipandport)

[2.1.1.2.15 UBSHcomNetDriver::AddOobOptions [32](#ubshcomnetdriveraddooboptions)](#ubshcomnetdriveraddooboptions)

[2.1.1.2.16 UBSHcomNetDriver::OobUdsName [33](#ubshcomnetdriveroobudsname)](#ubshcomnetdriveroobudsname)

[2.1.1.2.17 UBSHcomNetDriver::AddOobUdsOptions [34](#ubshcomnetdriveraddoobudsoptions)](#ubshcomnetdriveraddoobudsoptions)

[2.1.1.2.18 UBSHcomNetDriver::RegisterNewEPHandler [34](#ubshcomnetdriverregisternewephandler)](#ubshcomnetdriverregisternewephandler)

[2.1.1.2.19 UBSHcomNetDriver::RegisterEPBrokenHandler [35](#ubshcomnetdriverregisterepbrokenhandler)](#ubshcomnetdriverregisterepbrokenhandler)

[2.1.1.2.20 UBSHcomNetDriver::RegisterNewReqHandler [36](#ubshcomnetdriverregisternewreqhandler)](#ubshcomnetdriverregisternewreqhandler)

[2.1.1.2.21 UBSHcomNetDriver::RegisterReqPostedHandler [36](#ubshcomnetdriverregisterreqpostedhandler)](#ubshcomnetdriverregisterreqpostedhandler)

[2.1.1.2.22 UBSHcomNetDriver::RegisterOneSideDoneHandler [37](#ubshcomnetdriverregisteronesidedonehandler)](#ubshcomnetdriverregisteronesidedonehandler)

[2.1.1.2.23 UBSHcomNetDriver::RegisterIdleHandler [38](#ubshcomnetdriverregisteridlehandler)](#ubshcomnetdriverregisteridlehandler)

[2.1.1.2.24 UBSHcomNetDriver::Name [39](#ubshcomnetdrivername)](#ubshcomnetdrivername)

[2.1.1.2.25 UBSHcomNetDriver::GetId [39](#ubshcomnetdrivergetid)](#ubshcomnetdrivergetid)

[2.1.1.2.26 UBSHcomNetDriver::Protocol [39](#ubshcomnetdriverprotocol)](#ubshcomnetdriverprotocol)

[2.1.1.2.27 UBSHcomNetDriver::IsStarted [40](#ubshcomnetdriverisstarted)](#ubshcomnetdriverisstarted)

[2.1.1.2.28 UBSHcomNetDriver::IsInited [40](#ubshcomnetdriverisinited)](#ubshcomnetdriverisinited)

[2.1.1.2.29 UBSHcomNetDriver::NetUid [41](#ubshcomnetdrivernetuid)](#ubshcomnetdrivernetuid)

[2.1.1.2.30 UBSHcomNetDriver::DumpObjectStatistics [41](#ubshcomnetdriverdumpobjectstatistics)](#ubshcomnetdriverdumpobjectstatistics)

[2.1.1.2.31 UBSHcomNetDriver::SetPeerDevId [41](#ubshcomnetdriversetpeerdevid)](#ubshcomnetdriversetpeerdevid)

[2.1.1.2.32 UBSHcomNetDriver::GetPeerDevId [42](#ubshcomnetdrivergetpeerdevid)](#ubshcomnetdrivergetpeerdevid)

[2.1.1.2.33 UBSHcomNetDriver::SetDeviceId [42](#ubshcomnetdriversetdeviceid)](#ubshcomnetdriversetdeviceid)

[2.1.1.2.34 UBSHcomNetDriver::GetDeviceId [43](#ubshcomnetdrivergetdeviceid)](#ubshcomnetdrivergetdeviceid)

[2.1.1.2.35 UBSHcomNetDriver::GetBandWidth [43](#ubshcomnetdrivergetbandwidth)](#ubshcomnetdrivergetbandwidth)

[2.1.1.2.36 UBSHcomNetDriver::OobEidAndJettyId [43](#ubshcomnetdriveroobeidandjettyid)](#ubshcomnetdriveroobeidandjettyid)

[2.1.1.2.37 UBSHcomNetEndpoint::SetEpOption [44](#ubshcomnetendpointsetepoption)](#ubshcomnetendpointsetepoption)

[2.1.1.2.38 UBSHcomNetEndpoint::GetSendQueueCount [44](#ubshcomnetendpointgetsendqueuecount)](#ubshcomnetendpointgetsendqueuecount)

[2.1.1.2.39 UBSHcomNetEndpoint::Id [45](#ubshcomnetendpointid)](#ubshcomnetendpointid)

[2.1.1.2.40 UBSHcomNetEndpoint::WorkerIndex [45](#ubshcomnetendpointworkerindex)](#ubshcomnetendpointworkerindex)

[2.1.1.2.41 UBSHcomNetEndpoint::IsEstablished [45](#ubshcomnetendpointisestablished)](#ubshcomnetendpointisestablished)

[2.1.1.2.42 UBSHcomNetEndpoint::UpCtx [46](#ubshcomnetendpointupctx)](#ubshcomnetendpointupctx)

[2.1.1.2.43 UBSHcomNetEndpoint::UpCtx [46](#ubshcomnetendpointupctx-1)](#ubshcomnetendpointupctx-1)

[2.1.1.2.44 UBSHcomNetEndpoint::PeerConnectPayload [47](#ubshcomnetendpointpeerconnectpayload)](#ubshcomnetendpointpeerconnectpayload)

[2.1.1.2.45 UBSHcomNetEndpoint::LocalIp [47](#ubshcomnetendpointlocalip)](#ubshcomnetendpointlocalip)

[2.1.1.2.46 UBSHcomNetEndpoint::ListenPort [47](#ubshcomnetendpointlistenport)](#ubshcomnetendpointlistenport)

[2.1.1.2.47 UBSHcomNetEndpoint::Version [48](#ubshcomnetendpointversion)](#ubshcomnetendpointversion)

[2.1.1.2.48 UBSHcomNetEndpoint::State [48](#ubshcomnetendpointstate)](#ubshcomnetendpointstate)

[2.1.1.2.49 UBSHcomNetEndpoint::PeerIpAndPort [48](#ubshcomnetendpointpeeripandport)](#ubshcomnetendpointpeeripandport)

[2.1.1.2.50 UBSHcomNetEndpoint::UdsName [49](#ubshcomnetendpointudsname)](#ubshcomnetendpointudsname)

[2.1.1.2.51 UBSHcomNetEndpoint::PostSend [49](#ubshcomnetendpointpostsend)](#ubshcomnetendpointpostsend)

[2.1.1.2.52 UBSHcomNetEndpoint::PostSendRaw [50](#ubshcomnetendpointpostsendraw)](#ubshcomnetendpointpostsendraw)

[2.1.1.2.53 UBSHcomNetEndpoint::PostRead [51](#ubshcomnetendpointpostread)](#ubshcomnetendpointpostread)

[2.1.1.2.54 UBSHcomNetEndpoint::PostWrite [52](#ubshcomnetendpointpostwrite)](#ubshcomnetendpointpostwrite)

[2.1.1.2.55 UBSHcomNetEndpoint::DefaultTimeout [52](#ubshcomnetendpointdefaulttimeout)](#ubshcomnetendpointdefaulttimeout)

[2.1.1.2.56 UBSHcomNetEndpoint::WaitCompletion [53](#ubshcomnetendpointwaitcompletion)](#ubshcomnetendpointwaitcompletion)

[2.1.1.2.57 UBSHcomNetEndpoint::Receive [53](#ubshcomnetendpointreceive)](#ubshcomnetendpointreceive)

[2.1.1.2.58 UBSHcomNetEndpoint::ReceiveRaw [54](#ubshcomnetendpointreceiveraw)](#ubshcomnetendpointreceiveraw)

[2.1.1.2.59 UBSHcomNetEndpoint::GetRemoteUdsIdInfo [55](#ubshcomnetendpointgetremoteudsidinfo)](#ubshcomnetendpointgetremoteudsidinfo)

[2.1.1.2.60 UBSHcomNetEndpoint::GetPeerIpPort [55](#ubshcomnetendpointgetpeeripport)](#ubshcomnetendpointgetpeeripport)

[2.1.1.2.61 UBSHcomNetEndpoint::Close [56](#ubshcomnetendpointclose)](#ubshcomnetendpointclose)

[2.1.1.2.62 UBSHcomNetEndpoint::GetDevIndex [56](#ubshcomnetendpointgetdevindex)](#ubshcomnetendpointgetdevindex)

[2.1.1.2.63 UBSHcomNetEndpoint::GetPeerDevIndex [56](#ubshcomnetendpointgetpeerdevindex)](#ubshcomnetendpointgetpeerdevindex)

[2.1.1.2.64 UBSHcomNetEndpoint::GetBandWidth [57](#ubshcomnetendpointgetbandwidth)](#ubshcomnetendpointgetbandwidth)

[2.1.1.2.65 UBSHcomNetMessage::DataLen [57](#ubshcomnetmessagedatalen)](#ubshcomnetmessagedatalen)

[2.1.1.2.66 UBSHcomNetMessage::Data [58](#ubshcomnetmessagedata)](#ubshcomnetmessagedata)

[2.1.1.2.67 UBSHcomNetRequestContext::EndPoint [58](#ubshcomnetrequestcontextendpoint)](#ubshcomnetrequestcontextendpoint)

[2.1.1.2.68 UBSHcomNetRequestContext::Result [58](#ubshcomnetrequestcontextresult)](#ubshcomnetrequestcontextresult)

[2.1.1.2.69 UBSHcomNetRequestContext::Header [59](#ubshcomnetrequestcontextheader)](#ubshcomnetrequestcontextheader)

[2.1.1.2.70 UBSHcomNetRequestContext::Message [59](#ubshcomnetrequestcontextmessage)](#ubshcomnetrequestcontextmessage)

[2.1.1.2.71 UBSHcomNetRequestContext::OpType [59](#ubshcomnetrequestcontextoptype)](#ubshcomnetrequestcontextoptype)

[2.1.1.2.72 UBSHcomNetRequestContext::OriginalRequest [60](#ubshcomnetrequestcontextoriginalrequest)](#ubshcomnetrequestcontextoriginalrequest)

[2.1.1.2.73 UBSHcomNetRequestContext::OriginalSgeRequest [60](#ubshcomnetrequestcontextoriginalsgerequest)](#ubshcomnetrequestcontextoriginalsgerequest)

[2.1.1.2.74 UBSHcomNetRequestContext::SafeClone [60](#ubshcomnetrequestcontextsafeclone)](#ubshcomnetrequestcontextsafeclone)

[2.1.1.2.75 UBSHcomNetResponseContext::Header [61](#ubshcomnetresponsecontextheader)](#ubshcomnetresponsecontextheader)

[2.1.1.2.76 UBSHcomNetResponseContext::Message [61](#ubshcomnetresponsecontextmessage)](#ubshcomnetresponsecontextmessage)

[2.1.1.2.77 UBSHcomNetMemoryRegion::GetLKey [62](#ubshcomnetmemoryregiongetlkey)](#ubshcomnetmemoryregiongetlkey)

[2.1.1.2.78 UBSHcomNetMemoryRegion::GetAddress [62](#ubshcomnetmemoryregiongetaddress)](#ubshcomnetmemoryregiongetaddress)

[2.1.1.2.79 UBSHcomNetMemoryRegion::Size [62](#ubshcomnetmemoryregionsize)](#ubshcomnetmemoryregionsize)

[2.1.1.2.80 UBSHcomNetMemoryAllocator::Create [63](#ubshcomnetmemoryallocatorcreate)](#ubshcomnetmemoryallocatorcreate)

[2.1.1.2.81 UBSHcomNetMemoryAllocator::MrKey [63](#ubshcomnetmemoryallocatormrkey)](#ubshcomnetmemoryallocatormrkey)

[2.1.1.2.82 UBSHcomNetMemoryAllocator::MrKey [64](#ubshcomnetmemoryallocatormrkey-1)](#ubshcomnetmemoryallocatormrkey-1)

[2.1.1.2.83 UBSHcomNetMemoryAllocator::MemOffset [64](#ubshcomnetmemoryallocatormemoffset)](#ubshcomnetmemoryallocatormemoffset)

[2.1.1.2.84 UBSHcomNetMemoryAllocator::FreeSize [65](#ubshcomnetmemoryallocatorfreesize)](#ubshcomnetmemoryallocatorfreesize)

[2.1.1.2.85 UBSHcomNetMemoryAllocator::Allocate [65](#ubshcomnetmemoryallocatorallocate)](#ubshcomnetmemoryallocatorallocate)

[2.1.1.2.86 UBSHcomNetMemoryAllocator::Free [66](#ubshcomnetmemoryallocatorfree)](#ubshcomnetmemoryallocatorfree)

[2.1.1.2.87 UBSHcomNetMemoryAllocator::Destroy [66](#ubshcomnetmemoryallocatordestroy)](#ubshcomnetmemoryallocatordestroy)

[2.1.1.2.88 UBSHcomNetMemoryAllocator::GetTargetSeg [67](#ubshcomnetmemoryallocatorgettargetseg)](#ubshcomnetmemoryallocatorgettargetseg)

[2.1.1.2.89 UBSHcomNetMemoryAllocator::SetTargetSeg [67](#ubshcomnetmemoryallocatorsettargetseg)](#ubshcomnetmemoryallocatorsettargetseg)

[2.1.1.2.90 UBSHcomNetMemoryAllocatorTypeToString [67](#ubshcomnetmemoryallocatortypetostring)](#ubshcomnetmemoryallocatortypetostring)

[2.1.1.2.91 UBSHcomNetDriverProtocolToString [68](#ubshcomnetdriverprotocoltostring)](#ubshcomnetdriverprotocoltostring)

[2.1.1.2.92 UBSHcomNetDriverSecTypeToString [68](#ubshcomnetdriversectypetostring)](#ubshcomnetdriversectypetostring)

[2.1.1.2.93 UBSHcomNetDriverOobTypeToString [69](#ubshcomnetdriveroobtypetostring)](#ubshcomnetdriveroobtypetostring)

[2.1.1.2.94 UBSHcomNetDriverLBPolicyToString [69](#ubshcomnetdriverlbpolicytostring)](#ubshcomnetdriverlbpolicytostring)

[2.1.1.2.95 UBSHcomNEPStateToString [70](#ubshcomnepstatetostring)](#ubshcomnepstatetostring)

[2.1.2 C API [71](#c-api-1)](#c-api-1)

[2.1.2.1 服务层API [71](#服务层api-1)](#服务层api-1)

[2.1.2.1.1 ubs_hcom_service_create [71](#ubs_hcom_service_create)](#ubs_hcom_service_create)

[2.1.2.1.2 ubs_hcom_service_bind [71](#ubs_hcom_service_bind)](#ubs_hcom_service_bind)

[2.1.2.1.3 ubs_hcom_service_start [72](#ubs_hcom_service_start)](#ubs_hcom_service_start)

[2.1.2.1.4 ubs_hcom_service_destroy [73](#ubs_hcom_service_destroy)](#ubs_hcom_service_destroy)

[2.1.2.1.5 ubs_hcom_service_connect [73](#ubs_hcom_service_connect)](#ubs_hcom_service_connect)

[2.1.2.1.6 ubs_hcom_service_disconnect [74](#ubs_hcom_service_disconnect)](#ubs_hcom_service_disconnect)

[2.1.2.1.7 ubs_hcom_service_register_memory_region [75](#ubs_hcom_service_register_memory_region)](#ubs_hcom_service_register_memory_region)

[2.1.2.1.8 ubs_hcom_service_get_memory_region_info [75](#ubs_hcom_service_get_memory_region_info)](#ubs_hcom_service_get_memory_region_info)

[2.1.2.1.9 ubs_hcom_service_register_assign_memory_region [76](#ubs_hcom_service_register_assign_memory_region)](#ubs_hcom_service_register_assign_memory_region)

[2.1.2.1.10 ubs_hcom_service_destroy_memory_region [77](#ubs_hcom_service_destroy_memory_region)](#ubs_hcom_service_destroy_memory_region)

[2.1.2.1.11 ubs_hcom_service_register_broken_handler [77](#ubs_hcom_service_register_broken_handler)](#ubs_hcom_service_register_broken_handler)

[2.1.2.1.12 ubs_hcom_service_register_idle_handler [78](#ubs_hcom_service_register_idle_handler)](#ubs_hcom_service_register_idle_handler)

[2.1.2.1.13 ubs_hcom_service_register_handler [79](#ubs_hcom_service_register_handler)](#ubs_hcom_service_register_handler)

[2.1.2.1.14 ubs_hcom_service_set_enable_mrcache [80](#ubs_hcom_service_set_enable_mrcache)](#ubs_hcom_service_set_enable_mrcache)

[2.1.2.1.15 ubs_hcom_channel_refer [81](#ubs_hcom_channel_refer)](#ubs_hcom_channel_refer)

[2.1.2.1.16 ubs_hcom_channel_derefer [81](#ubs_hcom_channel_derefer)](#ubs_hcom_channel_derefer)

[2.1.2.1.17 ubs_hcom_channel_send [82](#ubs_hcom_channel_send)](#ubs_hcom_channel_send)

[2.1.2.1.18 ubs_hcom_channel_call [82](#ubs_hcom_channel_call)](#ubs_hcom_channel_call)

[2.1.2.1.19 ubs_hcom_channel_reply [84](#ubs_hcom_channel_reply)](#ubs_hcom_channel_reply)

[2.1.2.1.20 ubs_hcom_channel_put [84](#ubs_hcom_channel_put)](#ubs_hcom_channel_put)

[2.1.2.1.21 ubs_hcom_channel_get [85](#ubs_hcom_channel_get)](#ubs_hcom_channel_get)

[2.1.2.1.22 ubs_hcom_channel_recv [86](#ubs_hcom_channel_recv)](#ubs_hcom_channel_recv)

[2.1.2.1.23 ubs_hcom_channel_set_flowctl_cfg [86](#ubs_hcom_channel_set_flowctl_cfg)](#ubs_hcom_channel_set_flowctl_cfg)

[2.1.2.1.24 ubs_hcom_channel_set_timeout [87](#ubs_hcom_channel_set_timeout)](#ubs_hcom_channel_set_timeout)

[2.1.2.1.25 ubs_hcom_channel_set_twoside_threshold [87](#ubs_hcom_channel_set_twoside_threshold)](#ubs_hcom_channel_set_twoside_threshold)

[2.1.2.1.26 Channel_Close [88](#channel_close)](#channel_close)

[2.1.2.1.27 ubs_hcom_channel_get_id [89](#ubs_hcom_channel_get_id)](#ubs_hcom_channel_get_id)

[2.1.2.1.28 ubs_hcom_context_get_channel [89](#ubs_hcom_context_get_channel)](#ubs_hcom_context_get_channel)

[2.1.2.1.29 ubs_hcom_context_get_type [90](#ubs_hcom_context_get_type)](#ubs_hcom_context_get_type)

[2.1.2.1.30 ubs_hcom_context_get_result [90](#ubs_hcom_context_get_result)](#ubs_hcom_context_get_result)

[2.1.2.1.31 ubs_hcom_context_get_rspctx [91](#ubs_hcom_context_get_rspctx)](#ubs_hcom_context_get_rspctx)

[2.1.2.1.32 ubs_hcom_context_get_opcode [91](#ubs_hcom_context_get_opcode)](#ubs_hcom_context_get_opcode)

[2.1.2.1.33 ubs_hcom_context_get_data [92](#ubs_hcom_context_get_data)](#ubs_hcom_context_get_data)

[2.1.2.1.34 ubs_hcom_context_get_datalen [92](#ubs_hcom_context_get_datalen)](#ubs_hcom_context_get_datalen)

[2.1.2.2 传输层API [93](#传输层api-1)](#传输层api-1)

[2.1.2.2.1 ubs_hcom_driver_create [93](#ubs_hcom_driver_create)](#ubs_hcom_driver_create)

[2.1.2.2.2 ubs_hcom_driver_set_ipport [94](#ubs_hcom_driver_set_ipport)](#ubs_hcom_driver_set_ipport)

[2.1.2.2.3 ubs_hcom_driver_get_ipport [94](#ubs_hcom_driver_get_ipport)](#ubs_hcom_driver_get_ipport)

[2.1.2.2.4 ubs_hcom_driver_set_udsname [95](#ubs_hcom_driver_set_udsname)](#ubs_hcom_driver_set_udsname)

[2.1.2.2.5 ubs_hcom_driver_add_uds_opt [95](#ubs_hcom_driver_add_uds_opt)](#ubs_hcom_driver_add_uds_opt)

[2.1.2.2.6 ubs_hcom_driver_add_oob_opt [96](#ubs_hcom_driver_add_oob_opt)](#ubs_hcom_driver_add_oob_opt)

[2.1.2.2.7 ubs_hcom_driver_initizalize [97](#ubs_hcom_driver_initizalize)](#ubs_hcom_driver_initizalize)

[2.1.2.2.8 ubs_hcom_driver_start [97](#ubs_hcom_driver_start)](#ubs_hcom_driver_start)

[2.1.2.2.9 ubs_hcom_driver_connect [98](#ubs_hcom_driver_connect)](#ubs_hcom_driver_connect)

[2.1.2.2.10 ubs_hcom_driver_stop [99](#ubs_hcom_driver_stop)](#ubs_hcom_driver_stop)

[2.1.2.2.11 ubs_hcom_driver_uninitialize [99](#ubs_hcom_driver_uninitialize)](#ubs_hcom_driver_uninitialize)

[2.1.2.2.12 ubs_hcom_driver_destroy [100](#ubs_hcom_driver_destroy)](#ubs_hcom_driver_destroy)

[2.1.2.2.13 ubs_hcom_driver_register_ep_handler [100](#ubs_hcom_driver_register_ep_handler)](#ubs_hcom_driver_register_ep_handler)

[2.1.2.2.14 ubs_hcom_driver_register_op_handler [101](#ubs_hcom_driver_register_op_handler)](#ubs_hcom_driver_register_op_handler)

[2.1.2.2.15 ubs_hcom_driver_register_idle_handler [102](#ubs_hcom_driver_register_idle_handler)](#ubs_hcom_driver_register_idle_handler)

[2.1.2.2.16 ubs_hcom_driver_register_secinfo_provider [103](#ubs_hcom_driver_register_secinfo_provider)](#ubs_hcom_driver_register_secinfo_provider)

[2.1.2.2.17 ubs_hcom_driver_register_secinfo_validator [104](#ubs_hcom_driver_register_secinfo_validator)](#ubs_hcom_driver_register_secinfo_validator)

[2.1.2.2.18 ubs_hcom_driver_unregister_ep_handler [104](#ubs_hcom_driver_unregister_ep_handler)](#ubs_hcom_driver_unregister_ep_handler)

[2.1.2.2.19 ubs_hcom_driver_unregister_op_handler [105](#ubs_hcom_driver_unregister_op_handler)](#ubs_hcom_driver_unregister_op_handler)

[2.1.2.2.20 ubs_hcom_driver_unregister_idle_handler [106](#ubs_hcom_driver_unregister_idle_handler)](#ubs_hcom_driver_unregister_idle_handler)

[2.1.2.2.21 ubs_hcom_driver_create_memory_region [106](#ubs_hcom_driver_create_memory_region)](#ubs_hcom_driver_create_memory_region)

[2.1.2.2.22 ubs_hcom_driver_create_assign_memory_region [107](#ubs_hcom_driver_create_assign_memory_region)](#ubs_hcom_driver_create_assign_memory_region)

[2.1.2.2.23 ubs_hcom_driver_destroy_memory_region [107](#ubs_hcom_driver_destroy_memory_region)](#ubs_hcom_driver_destroy_memory_region)

[2.1.2.2.24 ubs_hcom_driver_get_memory_region_info [108](#ubs_hcom_driver_get_memory_region_info)](#ubs_hcom_driver_get_memory_region_info)

[2.1.2.2.25 ubs_hcom_ep_set_context [108](#ubs_hcom_ep_set_context)](#ubs_hcom_ep_set_context)

[2.1.2.2.26 ubs_hcom_ep_get_context [109](#ubs_hcom_ep_get_context)](#ubs_hcom_ep_get_context)

[2.1.2.2.27 ubs_hcom_ep_get_worker_idx [109](#ubs_hcom_ep_get_worker_idx)](#ubs_hcom_ep_get_worker_idx)

[2.1.2.2.28 ubs_hcom_ep_get_workergroup_idx [110](#ubs_hcom_ep_get_workergroup_idx)](#ubs_hcom_ep_get_workergroup_idx)

[2.1.2.2.29 ubs_hcom_ep_get_listen_port [110](#ubs_hcom_ep_get_listen_port)](#ubs_hcom_ep_get_listen_port)

[2.1.2.2.30 ubs_hcom_ep_version [111](#ubs_hcom_ep_version)](#ubs_hcom_ep_version)

[2.1.2.2.31 ubs_hcom_ep_set_timeout [111](#ubs_hcom_ep_set_timeout)](#ubs_hcom_ep_set_timeout)

[2.1.2.2.32 ubs_hcom_ep_post_send [112](#ubs_hcom_ep_post_send)](#ubs_hcom_ep_post_send)

[2.1.2.2.33 ubs_hcom_ep_post_send_with_opinfo [113](#ubs_hcom_ep_post_send_with_opinfo)](#ubs_hcom_ep_post_send_with_opinfo)

[2.1.2.2.34 ubs_hcom_ep_post_send_with_seqno [113](#ubs_hcom_ep_post_send_with_seqno)](#ubs_hcom_ep_post_send_with_seqno)

[2.1.2.2.35 ubs_hcom_ep_post_read [114](#ubs_hcom_ep_post_read)](#ubs_hcom_ep_post_read)

[2.1.2.2.36 ubs_hcom_ep_post_write [115](#ubs_hcom_ep_post_write)](#ubs_hcom_ep_post_write)

[2.1.2.2.37 ubs_hcom_ep_wait_completion [115](#ubs_hcom_ep_wait_completion)](#ubs_hcom_ep_wait_completion)

[2.1.2.2.38 ubs_hcom_ep_receive [116](#ubs_hcom_ep_receive)](#ubs_hcom_ep_receive)

[2.1.2.2.39 ubs_hcom_ep_refer [116](#ubs_hcom_ep_refer)](#ubs_hcom_ep_refer)

[2.1.2.2.40 ubs_hcom_ep_close [117](#ubs_hcom_ep_close)](#ubs_hcom_ep_close)

[2.1.2.2.41 ubs_hcom_ep_destroy [117](#ubs_hcom_ep_destroy)](#ubs_hcom_ep_destroy)

[2.1.2.2.42 ubs_hcom_err_str [118](#ubs_hcom_err_str)](#ubs_hcom_err_str)

[2.1.2.2.43 ubs_hcom_mem_allocator_create [119](#ubs_hcom_mem_allocator_create)](#ubs_hcom_mem_allocator_create)

[2.1.2.2.44 ubs_hcom_mem_allocator_destroy [119](#ubs_hcom_mem_allocator_destroy)](#ubs_hcom_mem_allocator_destroy)

[2.1.2.2.45 ubs_hcom_mem_allocator_set_mr_key [120](#ubs_hcom_mem_allocator_set_mr_key)](#ubs_hcom_mem_allocator_set_mr_key)

[2.1.2.2.46 ubs_hcom_mem_allocator_get_offset [120](#ubs_hcom_mem_allocator_get_offset)](#ubs_hcom_mem_allocator_get_offset)

[2.1.2.2.47 ubs_hcom_mem_allocator_get_free_size [121](#ubs_hcom_mem_allocator_get_free_size)](#ubs_hcom_mem_allocator_get_free_size)

[2.1.2.2.48 ubs_hcom_mem_allocator_allocate [122](#ubs_hcom_mem_allocator_allocate)](#ubs_hcom_mem_allocator_allocate)

[2.1.2.2.49 ubs_hcom_mem_allocator_free [122](#ubs_hcom_mem_allocator_free)](#ubs_hcom_mem_allocator_free)

[2.1.2.2.50 ubs_hcom_set_log_handler [123](#ubs_hcom_set_log_handler)](#ubs_hcom_set_log_handler)

[2.1.2.2.51 ubs_hcom_check_local_supporr [123](#ubs_hcom_check_local_supporr)](#ubs_hcom_check_local_supporr)

[2.1.2.2.52 ubs_hcom_get_remote_uds_info [124](#ubs_hcom_get_remote_uds_info)](#ubs_hcom_get_remote_uds_info)

[2.2 高级API参考 [125](#高级api参考)](#高级api参考)

[2.2.1 C++API [125](#capi)](#capi)

[2.2.1.1 服务层 [125](#服务层)](#服务层)

[2.2.1.1.1 UBSHcomService::AddWorkerGroup [125](#ubshcomserviceaddworkergroup)](#ubshcomserviceaddworkergroup)

[2.2.1.1.2 UBSHcomService::AddListener [126](#ubshcomserviceaddlistener)](#ubshcomserviceaddlistener)

[2.2.1.1.3 UBSHcomService::SetConnectLBPolicy [126](#ubshcomservicesetconnectlbpolicy)](#ubshcomservicesetconnectlbpolicy)

[2.2.1.1.4 UBSHcomService::SetUBSHcomTlsOptions [127](#ubshcomservicesetubshcomtlsoptions)](#ubshcomservicesetubshcomtlsoptions)

[2.2.1.1.5 UBSHcomService::SetConnSecureOpt [127](#ubshcomservicesetconnsecureopt)](#ubshcomservicesetconnsecureopt)

[2.2.1.1.6 UBSHcomService::SetTcpUserTimeOutSec [128](#ubshcomservicesettcpusertimeoutsec)](#ubshcomservicesettcpusertimeoutsec)

[2.2.1.1.7 UBSHcomService::SetTcpSendZCopy [128](#ubshcomservicesettcpsendzcopy)](#ubshcomservicesettcpsendzcopy)

[2.2.1.1.8 UBSHcomService::SetDeviceIpMask [129](#ubshcomservicesetdeviceipmask)](#ubshcomservicesetdeviceipmask)

[2.2.1.1.9 UBSHcomService::SetDeviceIpGroups [129](#ubshcomservicesetdeviceipgroups)](#ubshcomservicesetdeviceipgroups)

[2.2.1.1.10 UBSHcomService::SetCompletionQueueDepth [130](#ubshcomservicesetcompletionqueuedepth)](#ubshcomservicesetcompletionqueuedepth)

[2.2.1.1.11 UBSHcomService::SetSendQueueSize [130](#ubshcomservicesetsendqueuesize)](#ubshcomservicesetsendqueuesize)

[2.2.1.1.12 UBSHcomService::SetRecvQueueSize [131](#ubshcomservicesetrecvqueuesize)](#ubshcomservicesetrecvqueuesize)

[2.2.1.1.13 UBSHcomService::SetPollingBatchSize [131](#ubshcomservicesetpollingbatchsize)](#ubshcomservicesetpollingbatchsize)

[2.2.1.1.14 UBSHcomService::SetEventPollingTimeOutUs [132](#ubshcomserviceseteventpollingtimeoutus)](#ubshcomserviceseteventpollingtimeoutus)

[2.2.1.1.15 UBSHcomService::SetTimeOutDetectionThreadNum [132](#ubshcomservicesettimeoutdetectionthreadnum)](#ubshcomservicesettimeoutdetectionthreadnum)

[2.2.1.1.16 UBSHcomService::SetMaxConnectionCount [133](#ubshcomservicesetmaxconnectioncount)](#ubshcomservicesetmaxconnectioncount)

[2.2.1.1.17 UBSHcomService::SetUBSHcomHeartBeatOptions [133](#ubshcomservicesetubshcomheartbeatoptions)](#ubshcomservicesetubshcomheartbeatoptions)

[2.2.1.1.18 UBSHcomService::SetUBSHcomMultiRailOptions [134](#ubshcomservicesetubshcommultirailoptions)](#ubshcomservicesetubshcommultirailoptions)

[2.2.1.1.19 UBSHcomService::SetQueuePrePostSize [134](#ubshcomservicesetqueueprepostsize)](#ubshcomservicesetqueueprepostsize)

[2.2.1.1.20 UBSHcomService::SetMaxSendRecvDataCount [135](#ubshcomservicesetmaxsendrecvdatacount)](#ubshcomservicesetmaxsendrecvdatacount)

[2.2.1.1.21 UBSHcomRegMemoryRegion::GetMemoryKey [135](#ubshcomregmemoryregiongetmemorykey)](#ubshcomregmemoryregiongetmemorykey)

[2.2.1.1.22 UBSHcomRegMemoryRegion::GetAddress [136](#ubshcomregmemoryregiongetaddress)](#ubshcomregmemoryregiongetaddress)

[2.2.1.1.23 UBSHcomRegMemoryRegion::GetSize [136](#ubshcomregmemoryregiongetsize)](#ubshcomregmemoryregiongetsize)

[2.2.1.1.24 UBSHcomRegMemoryRegion::GetHcomMrs [137](#ubshcomregmemoryregiongethcommrs)](#ubshcomregmemoryregiongethcommrs)

[2.2.1.1.25 UBSHcomNewCallback [137](#ubshcomnewcallback)](#ubshcomnewcallback)

[2.2.1.2 传输层 [138](#传输层)](#传输层)

[2.2.1.2.1 UBSHcomNetDriver::RegisterTLSCaCallback [138](#ubshcomnetdriverregistertlscacallback)](#ubshcomnetdriverregistertlscacallback)

[2.2.1.2.2 UBSHcomNetDriver::RegisterTLSCertificationCallback [140](#ubshcomnetdriverregistertlscertificationcallback)](#ubshcomnetdriverregistertlscertificationcallback)

[2.2.1.2.3 UBSHcomNetDriver::RegisterTLSPrivateKeyCallback [141](#ubshcomnetdriverregistertlsprivatekeycallback)](#ubshcomnetdriverregistertlsprivatekeycallback)

[2.2.1.2.4 UBSHcomNetDriver::RegisterPskUseSessionCb [144](#ubshcomnetdriverregisterpskusesessioncb)](#ubshcomnetdriverregisterpskusesessioncb)

[2.2.1.2.5 UBSHcomNetDriver::RegisterPskFindSessionCb [145](#ubshcomnetdriverregisterpskfindsessioncb)](#ubshcomnetdriverregisterpskfindsessioncb)

[2.2.1.2.6 UBSHcomNetDriver::RegisterEndpointSecInfoProvider [146](#ubshcomnetdriverregisterendpointsecinfoprovider)](#ubshcomnetdriverregisterendpointsecinfoprovider)

[2.2.1.2.7 UBSHcomNetDriver::RegisterEndpointSecInfoValidator [147](#ubshcomnetdriverregisterendpointsecinfovalidator)](#ubshcomnetdriverregisterendpointsecinfovalidator)

[2.2.1.2.8 UBSHcomNetEndpoint::PostSendRawSgl [147](#ubshcomnetendpointpostsendrawsgl)](#ubshcomnetendpointpostsendrawsgl)

[2.2.1.2.9 UBSHcomNetEndpoint::ReceiveRaw [148](#ubshcomnetendpointreceiveraw-1)](#ubshcomnetendpointreceiveraw-1)

[2.2.1.2.10 UBSHcomNetEndpoint::EstimatedEncryptLen [149](#ubshcomnetendpointestimatedencryptlen)](#ubshcomnetendpointestimatedencryptlen)

[2.2.1.2.11 UBSHcomNetEndpoint::Encrypt [149](#ubshcomnetendpointencrypt)](#ubshcomnetendpointencrypt)

[2.2.1.2.12 UBSHcomNetEndpoint::EstimatedDecryptLen [150](#ubshcomnetendpointestimateddecryptlen)](#ubshcomnetendpointestimateddecryptlen)

[2.2.1.2.13 UBSHcomNetEndpoint::Decrypt [150](#ubshcomnetendpointdecrypt)](#ubshcomnetendpointdecrypt)

[2.2.1.2.14 UBSHcomNetEndpoint::SendFds [151](#ubshcomnetendpointsendfds)](#ubshcomnetendpointsendfds)

[2.2.1.2.15 UBSHcomNetEndpoint::ReceiveFds [151](#ubshcomnetendpointreceivefds)](#ubshcomnetendpointreceivefds)

[2.2.1.2.16 UBSHcomNetOutLogger::Instance [152](#ubshcomnetoutloggerinstance)](#ubshcomnetoutloggerinstance)

[2.2.1.2.17 UBSHcomNetOutLogger::SetLogLevel [152](#ubshcomnetoutloggersetloglevel)](#ubshcomnetoutloggersetloglevel)

[2.2.1.2.18 UBSHcomNetOutLogger::SetExternalLogFunction [153](#ubshcomnetoutloggersetexternallogfunction)](#ubshcomnetoutloggersetexternallogfunction)

[2.2.1.2.19 UBSHcomNetOutLogger::Print [153](#ubshcomnetoutloggerprint)](#ubshcomnetoutloggerprint)

[2.2.1.2.20 UBSHcomNetOutLogger::Log [154](#ubshcomnetoutloggerlog)](#ubshcomnetoutloggerlog)

[2.2.1.2.21 UBSHcomNetOutLogger::GetLogLevel [155](#ubshcomnetoutloggergetloglevel)](#ubshcomnetoutloggergetloglevel)

[2.2.1.2.22 UBSHcomNetAtomicState::Get [155](#ubshcomnetatomicstateget)](#ubshcomnetatomicstateget)

[2.2.1.2.23 UBSHcomNetAtomicState::Set [155](#ubshcomnetatomicstateset)](#ubshcomnetatomicstateset)

[2.2.1.2.24 UBSHcomNetAtomicState::CAS [156](#ubshcomnetatomicstatecas)](#ubshcomnetatomicstatecas)

[2.2.1.2.25 UBSHcomNetAtomicState::Compare [156](#ubshcomnetatomicstatecompare)](#ubshcomnetatomicstatecompare)

[2.2.2 C API [157](#c-api-2)](#c-api-2)

[2.2.2.1 服务层 [157](#服务层-1)](#服务层-1)

[2.2.2.1.1 ubs_hcom_service_add_workergroup [157](#ubs_hcom_service_add_workergroup)](#ubs_hcom_service_add_workergroup)

[2.2.2.1.2 ubs_hcom_service_add_listener [158](#ubs_hcom_service_add_listener)](#ubs_hcom_service_add_listener)

[2.2.2.1.3 ubs_hcom_service_set_lbpolicy [158](#ubs_hcom_service_set_lbpolicy)](#ubs_hcom_service_set_lbpolicy)

[2.2.2.1.4 ubs_hcom_service_set_tls_opt [159](#ubs_hcom_service_set_tls_opt)](#ubs_hcom_service_set_tls_opt)

[2.2.2.1.5 ubs_hcom_service_set_secure_opt [160](#ubs_hcom_service_set_secure_opt)](#ubs_hcom_service_set_secure_opt)

[2.2.2.1.6 ubs_hcom_service_set_tcp_usr_timeout [161](#ubs_hcom_service_set_tcp_usr_timeout)](#ubs_hcom_service_set_tcp_usr_timeout)

[2.2.2.1.7 ubs_hcom_service_set_tcp_send_zcopy [161](#ubs_hcom_service_set_tcp_send_zcopy)](#ubs_hcom_service_set_tcp_send_zcopy)

[2.2.2.1.8 ubs_hcom_service_set_ipmask [162](#ubs_hcom_service_set_ipmask)](#ubs_hcom_service_set_ipmask)

[2.2.2.1.9 ubs_hcom_service_set_ipgroup [162](#ubs_hcom_service_set_ipgroup)](#ubs_hcom_service_set_ipgroup)

[2.2.2.1.10 ubs_hcom_service_set_cq_depth [163](#ubs_hcom_service_set_cq_depth)](#ubs_hcom_service_set_cq_depth)

[2.2.2.1.11 ubs_hcom_service_set_sq_size [163](#ubs_hcom_service_set_sq_size)](#ubs_hcom_service_set_sq_size)

[2.2.2.1.12 ubs_hcom_service_set_rq_size [164](#ubs_hcom_service_set_rq_size)](#ubs_hcom_service_set_rq_size)

[2.2.2.1.13 ubs_hcom_service_set_polling_batchsize [165](#ubs_hcom_service_set_polling_batchsize)](#ubs_hcom_service_set_polling_batchsize)

[2.2.2.1.14 ubs_hcom_service_set_polling_timeoutus [165](#ubs_hcom_service_set_polling_timeoutus)](#ubs_hcom_service_set_polling_timeoutus)

[2.2.2.1.15 ubs_hcom_service_set_timeout_threadnum [166](#ubs_hcom_service_set_timeout_threadnum)](#ubs_hcom_service_set_timeout_threadnum)

[2.2.2.1.16 ubs_hcom_service_set_max_connection_cnt [166](#ubs_hcom_service_set_max_connection_cnt)](#ubs_hcom_service_set_max_connection_cnt)

[2.2.2.1.17 ubs_hcom_service_set_heartbeat_opt [167](#ubs_hcom_service_set_heartbeat_opt)](#ubs_hcom_service_set_heartbeat_opt)

[2.2.2.1.18 ubs_hcom_service_set_multirail_opt [167](#ubs_hcom_service_set_multirail_opt)](#ubs_hcom_service_set_multirail_opt)

[2.2.2.1.19 ubs_hcom_set_log_handler [168](#ubs_hcom_set_log_handler-1)](#ubs_hcom_set_log_handler-1)

[2.2.2.2 传输层 [169](#传输层-1)](#传输层-1)

[2.2.2.2.1 ubs_hcom_driver_register_tls_cb [169](#ubs_hcom_driver_register_tls_cb)](#ubs_hcom_driver_register_tls_cb)

[2.2.2.2.2 ubs_hcom_ep_post_send_raw [172](#ubs_hcom_ep_post_send_raw)](#ubs_hcom_ep_post_send_raw)

[2.2.2.2.3 ubs_hcom_ep_post_send_raw_sgl [173](#ubs_hcom_ep_post_send_raw_sgl)](#ubs_hcom_ep_post_send_raw_sgl)

[2.2.2.2.4 ubs_hcom_ep_post_read_sgl [174](#ubs_hcom_ep_post_read_sgl)](#ubs_hcom_ep_post_read_sgl)

[2.2.2.2.5 ubs_hcom_ep_post_write_sgl [175](#ubs_hcom_ep_post_write_sgl)](#ubs_hcom_ep_post_write_sgl)

[2.2.2.2.6 ubs_hcom_ep_receive_raw [175](#ubs_hcom_ep_receive_raw)](#ubs_hcom_ep_receive_raw)

[2.2.2.2.7 ubs_hcom_ep_receive_raw_sgl [176](#ubs_hcom_ep_receive_raw_sgl)](#ubs_hcom_ep_receive_raw_sgl)

[2.2.2.2.8 ubs_hcom_estimate_encrypt_len [177](#ubs_hcom_estimate_encrypt_len)](#ubs_hcom_estimate_encrypt_len)

[2.2.2.2.9 ubs_hcom_encrypt [177](#ubs_hcom_encrypt)](#ubs_hcom_encrypt)

[2.2.2.2.10 ubs_hcom_estimate_decrypt_len [178](#ubs_hcom_estimate_decrypt_len)](#ubs_hcom_estimate_decrypt_len)

[2.2.2.2.11 ubs_hcom_decrypt [178](#ubs_hcom_decrypt)](#ubs_hcom_decrypt)

[2.2.2.2.12 ubs_hcom_send_fds [179](#ubs_hcom_send_fds)](#ubs_hcom_send_fds)

[2.2.2.2.13 ubs_hcom_receive_fds [180](#ubs_hcom_receive_fds)](#ubs_hcom_receive_fds)

[2.3 结构体参考 [180](#结构体参考)](#结构体参考)

[2.3.1 C++结构体 [180](#c结构体)](#c结构体)

[2.3.1.1 服务层结构体 [180](#服务层结构体)](#服务层结构体)

[2.3.1.1.1 UBSHcomServiceOptions [180](#ubshcomserviceoptions)](#ubshcomserviceoptions)

[2.3.1.1.2 UBSHcomConnectOptions [181](#ubshcomconnectoptions)](#ubshcomconnectoptions)

[2.3.1.1.3 UBSHcomRequest [182](#ubshcomrequest)](#ubshcomrequest)

[2.3.1.1.4 UBSHcomResponse [182](#ubshcomresponse)](#ubshcomresponse)

[2.3.1.1.5 UBSHcomReplyContext [182](#ubshcomreplycontext)](#ubshcomreplycontext)

[2.3.1.1.6 UBSHcomOneSideRequest [182](#ubshcomonesiderequest)](#ubshcomonesiderequest)

[2.3.1.1.7 UBSHcomFlowCtrlOptions [183](#flowctrloptions)](#flowctrloptions)

[2.3.1.1.8 UBSHcomTlsOptions [183](#ubshcomtlsoptions)](#ubshcomtlsoptions)

[2.3.1.1.9 UBSHcomConnSecureOptions [187](#ubshcomconnsecureoptions)](#ubshcomconnsecureoptions)

[2.3.1.1.10 UBSHcomHeartBeatOptions [187](#ubshcomheartbeatoptions)](#ubshcomheartbeatoptions)

[2.3.1.1.11 UBSHcomMultiRailOptions [188](#ubshcommultirailoptions)](#ubshcommultirailoptions)

[2.3.1.1.12 UBSHcomIov [188](#ubshcomiov)](#ubshcomiov)

[2.3.1.1.13 UBSHcomOneSideSglRequest [188](#ubshcomonesidesglrequest)](#ubshcomonesidesglrequest)

[2.3.1.1.14 UBSHcomMemoryKey [188](#ubshcommemorykey)](#ubshcommemorykey)

[2.3.1.1.15 UBSHcomSglRequest [189](#ubshcomsglrequest)](#ubshcomsglrequest)

[2.3.1.1.16 UBSHcomTwoSideThreshold [189](#ubshcomtwosidethreshold)](#ubshcomtwosidethreshold)

[2.3.1.2 传输层结构体 [189](#传输层结构体)](#传输层结构体)

[2.3.1.2.1 UBSHcomNetDriverDeviceInfo [189](#ubshcomnetdriverdeviceinfo)](#ubshcomnetdriverdeviceinfo)

[2.3.1.2.2 UBSHcomNetDriverOptions [190](#ubshcomnetdriveroptions)](#ubshcomnetdriveroptions)

[2.3.1.2.3 UBSHcomNetOobListenerOptions [194](#ubshcomnetooblisteneroptions)](#ubshcomnetooblisteneroptions)

[2.3.1.2.4 UBSHcomNetOobUDSListenerOptions [194](#ubshcomnetoobudslisteneroptions)](#ubshcomnetoobudslisteneroptions)

[2.3.1.2.5 UBSHcomEpOptions [194](#ubshcomepoptions)](#ubshcomepoptions)

[2.3.1.2.6 UBSHcomNetTransRequest [195](#ubshcomnettransrequest)](#ubshcomnettransrequest)

[2.3.1.2.7 UBSHcomNetTransOpInfo [195](#ubshcomnettransopinfo)](#ubshcomnettransopinfo)

[2.3.1.2.8 UBSHcomNetUdsIdInfo [196](#ubshcomnetudsidinfo)](#ubshcomnetudsidinfo)

[2.3.1.2.9 UBSHcomNetMemoryAllocatorOptions [196](#ubshcomnetmemoryallocatoroptions)](#ubshcomnetmemoryallocatoroptions)

[2.3.1.2.10 UBSHcomNetTransSglRequest [197](#ubshcomnettranssglrequest)](#ubshcomnettranssglrequest)

[2.3.1.2.11 UBSHcomNetTransSgeIov [197](#ubshcomnettranssgeiov)](#ubshcomnettranssgeiov)

[2.3.1.2.12 UBSHcomWorkerGroupInfo [197](#ubshcomworkergroupinfo)](#ubshcomworkergroupinfo)

[2.3.1.2.13 UBSHcomNetUdsIdInfo [198](#ubshcomnetudsidinfo-1)](#ubshcomnetudsidinfo-1)

[2.3.1.2.14 UBSHcomNetTransHeader [198](#ubshcomnettransheader)](#ubshcomnettransheader)

[2.3.2 C结构体 [199](#c结构体-1)](#c结构体-1)

[2.3.2.1 服务层结构体 [199](#服务层结构体-1)](#服务层结构体-1)

[2.3.2.1.1 ubs_hcom_mr_info [199](#ubs_hcom_mr_info)](#ubs_hcom_mr_info)

[2.3.2.1.2 ubs_hcom_channel_reply_context [199](#ubs_hcom_channel_reply_context)](#ubs_hcom_channel_reply_context)

[2.3.2.1.3 ubs_hcom_oneside_request [199](#ubs_hcom_oneside_request)](#ubs_hcom_oneside_request)

[2.3.2.1.4 ubs_hcom_channel_callback [200](#channel_callback)](#channel_callback)

[2.3.2.1.5 ubs_hcom_flowctl_opts [200](#ubs_hcom_flowctl_opts)](#ubs_hcom_flowctl_opts)

[2.3.2.1.6 ubs_hcom_service_options [200](#ubs_hcom_service_options)](#ubs_hcom_service_options)

[2.3.2.1.7 Service_UBSHcomConnectOptions [201](#service_ubshcomconnectoptions)](#service_ubshcomconnectoptions)

[2.3.2.1.8 ubs_hcom_channel_request [202](#ubs_hcom_channel_request)](#ubs_hcom_channel_request)

[2.3.2.1.9 ubs_hcom_channel_response [202](#ubs_hcom_channel_response)](#ubs_hcom_channel_response)

[2.3.2.1.10 Channel_UBSHcomTwoSideThreshold [202](#channel_ubshcomtwosidethreshold)](#channel_ubshcomtwosidethreshold)

[2.3.2.1.11 ubs_hcom_oneside_key [203](#ubs_hcom_oneside_key)](#ubs_hcom_oneside_key)

[2.3.2.2 传输层结构体 [203](#传输层结构体-1)](#传输层结构体-1)

[2.3.2.2.1 ubs_hcom_send_request [203](#ubs_hcom_send_request)](#ubs_hcom_send_request)

[2.3.2.2.2 ubs_hcom_opinfo [203](#ubs_hcom_opinfo)](#ubs_hcom_opinfo)

[2.3.2.2.3 ubs_hcom_device_info [203](#ubs_hcom_device_info)](#ubs_hcom_device_info)

[2.3.2.2.4 ubs_hcom_readwrite_request [204](#ubs_hcom_readwrite_request)](#ubs_hcom_readwrite_request)

[2.3.2.2.5 ubs_hcom_readwrite_sge [204](#ubs_hcom_readwrite_sge)](#ubs_hcom_readwrite_sge)

[2.3.2.2.6 ubs_hcom_readwrite_request_sgl [204](#ubs_hcom_readwrite_request_sgl)](#ubs_hcom_readwrite_request_sgl)

[2.3.2.2.7 ubs_hcom_memory_region_info [205](#ubs_hcom_memory_region_info)](#ubs_hcom_memory_region_info)

[2.3.2.2.8 ubs_hcom_request_context [205](#ubs_hcom_request_context)](#ubs_hcom_request_context)

[2.3.2.2.9 ubs_hcom_response_context [206](#ubs_hcom_response_context)](#ubs_hcom_response_context)

[2.3.2.2.10 ubs_hcom_uds_id_info [206](#ubs_hcom_uds_id_info)](#ubs_hcom_uds_id_info)

[2.3.2.2.11 ubs_hcom_driver_opts [206](#ubs_hcom_driver_opts)](#ubs_hcom_driver_opts)

[2.3.2.2.12 ubs_hcom_driver_listen_opts [209](#ubs_hcom_driver_listen_opts)](#ubs_hcom_driver_listen_opts)

[2.3.2.2.13 ubs_hcom_driver_uds_listen_opts [210](#ubs_hcom_driver_uds_listen_opts)](#ubs_hcom_driver_uds_listen_opts)

[2.3.2.2.14 ubs_hcom_memory_allocator_options [210](#ubs_hcom_memory_allocator_options)](#ubs_hcom_memory_allocator_options)

[2.4 枚举值参考 [211](#枚举值参考)](#枚举值参考)

[2.4.1 C++枚举值 [211](#c枚举值)](#c枚举值)

[2.4.1.1 服务层枚举值 [211](#服务层枚举值)](#服务层枚举值)

[2.4.1.1.1 UBSHcomChannelBrokenPolicy [211](#ubshcomchannelbrokenpolicy)](#ubshcomchannelbrokenpolicy)

[2.4.1.1.2 Operation [211](#operation)](#operation)

[2.4.1.1.3 UBSHcomClientPollingMode [212](#ubshcomclientpollingmode)](#ubshcomclientpollingmode)

[2.4.1.1.4 UBSHcomChannelCallBackType [212](#ubshcomchannelcallbacktype)](#ubshcomchannelcallbacktype)

[2.4.1.1.5 UBSHcomFlowCtrlLevel [212](#ubshcomflowctrllevel)](#ubshcomflowctrllevel)

[2.4.1.1.6 UBSHcomChannelState [213](#ubshcomchannelstate)](#ubshcomchannelstate)

[2.4.1.1.7 UBSHcomOobType [213](#ubshcomoobtype)](#ubshcomoobtype)

[2.4.1.1.8 UBSHcomSecType [213](#hcomsectype)](#hcomsectype)

[2.4.1.2 传输层枚举值 [214](#传输层枚举值)](#传输层枚举值)

[2.4.1.2.1 UBSHcomNetEndPointState [214](#ubshcomnetendpointstate-1)](#ubshcomnetendpointstate-1)

[2.4.1.2.2 UBSHcomNetCipherSuite [214](#ubshcomnetciphersuite)](#ubshcomnetciphersuite)

[2.4.1.2.3 UBSHcomTlsVersion [215](#ubshcomtlsversion)](#ubshcomtlsversion)

[2.4.1.2.4 NN_OpType [215](#nn_optype)](#nn_optype)

[2.4.1.2.5 UBSHcomNetMemoryAllocatorType [216](#ubshcomnetmemoryallocatortype)](#ubshcomnetmemoryallocatortype)

[2.4.1.2.6 UBSHcomNetMemoryAllocatorCacheTierPolicy [216](#ubshcomnetmemoryallocatorcachetierpolicy)](#ubshcomnetmemoryallocatorcachetierpolicy)

[2.4.1.2.7 UBSHcomPeerCertVerifyType [216](#ubshcompeercertverifytype)](#ubshcompeercertverifytype)

[2.4.1.2.8 UBSHcomNetDriverSecType [217](#ubshcomnetdriversectype)](#ubshcomnetdriversectype)

[2.4.1.2.9 NetDriverOobType [217](#netdriveroobtype)](#netdriveroobtype)

[2.4.1.2.10 UBSHcomNetDriverWorkingMode [217](#ubshcomnetdriverworkingmode)](#ubshcomnetdriverworkingmode)

[2.4.1.2.11 UBSHcomNetDriverLBPolicy [218](#ubshcomnetdriverlbpolicy)](#ubshcomnetdriverlbpolicy)

[2.4.1.2.12 UBSHcomNetDriverProtocol [218](#ubshcomnetdriverprotocol-1)](#ubshcomnetdriverprotocol-1)

[2.4.1.2.13 UBSHcomUbcMode [219](#ubshcomubcmode)](#ubshcomubcmode)

[2.4.2 C枚举值 [219](#c枚举值-1)](#c枚举值-1)

[2.4.2.1 服务层枚举值 [219](#服务层枚举值-1)](#服务层枚举值-1)

[2.4.2.1.1 ubs_hcom_channel_cb_type [219](#ubs_hcom_channel_cb_type)](#ubs_hcom_channel_cb_type)

[2.4.2.1.2 ubs_hcom_service_context_type [219](#ubs_hcom_service_context_type)](#ubs_hcom_service_context_type)

[2.4.2.1.3 ubs_hcom_channel_flowctl_level [220](#ubs_hcom_channel_flowctl_level)](#ubs_hcom_channel_flowctl_level)

[2.4.2.1.4 ubs_hcom_service_worker_mode [220](#ubs_hcom_service_worker_mode)](#ubs_hcom_service_worker_mode)

[2.4.2.1.5 ubs_hcom_service_lb_policy [221](#ubs_hcom_service_lb_policy)](#ubs_hcom_service_lb_policy)

[2.4.2.1.6 ubs_hcom_service_cipher_suite [221](#ubs_hcom_service_cipher_suite)](#ubs_hcom_service_cipher_suite)

[2.4.2.1.7 ubs_hcom_service_tls_version [221](#ubs_hcom_service_tls_version)](#ubs_hcom_service_tls_version)

[2.4.2.1.8 ubs_hcom_service_secure_type [222](#ubs_hcom_service_secure_type)](#ubs_hcom_service_secure_type)

[2.4.2.1.9 ubs_hcom_service_channel_policy [222](#ubs_hcom_service_channel_policy)](#ubs_hcom_service_channel_policy)

[2.4.2.1.10 ubs_hcom_service_channel_handler_type [223](#ubs_hcom_service_channel_handler_type)](#ubs_hcom_service_channel_handler_type)

[2.4.2.1.11 ubs_hcom_service_handler_type [223](#ubs_hcom_service_handler_type)](#ubs_hcom_service_handler_type)

[2.4.2.1.12 ubs_hcom_service_type [223](#ubs_hcom_service_type)](#ubs_hcom_service_type)

[2.4.2.1.13 ubs_hcom_service_polling_mode [224](#ubs_hcom_service_polling_mode)](#ubs_hcom_service_polling_mode)

[2.4.2.2 传输层枚举值 [224](#传输层枚举值-1)](#传输层枚举值-1)

[2.4.2.2.1 ubs_hcom_request_type [224](#ubs_hcom_request_type)](#ubs_hcom_request_type)

[2.4.2.2.2 ubs_hcom_driver_working_mode [225](#ubs_hcom_driver_working_mode)](#ubs_hcom_driver_working_mode)

[2.4.2.2.3 ubs_hcom_driver_type [225](#ubs_hcom_driver_type)](#ubs_hcom_driver_type)

[2.4.2.2.4 ubs_hcom_driver_oob_type [226](#ubs_hcom_driver_oob_type)](#ubs_hcom_driver_oob_type)

[2.4.2.2.5 ubs_hcom_driver_sec_type [226](#ubs_hcom_driver_sec_type)](#ubs_hcom_driver_sec_type)

[2.4.2.2.6 ubs_hcom_driver_tls_version [226](#ubs_hcom_driver_tls_version)](#ubs_hcom_driver_tls_version)

[2.4.2.2.7 ubs_hcom_driver_cipher_suite [227](#ubs_hcom_driver_cipher_suite)](#ubs_hcom_driver_cipher_suite)

[2.4.2.2.8 ubs_hcom_peer_cert_verify_type [227](#ubs_hcom_peer_cert_verify_type)](#ubs_hcom_peer_cert_verify_type)

[2.4.2.2.9 ubs_hcom_memory_allocator_cache_tier_policy [227](#ubs_hcom_memory_allocator_cache_tier_policy)](#ubs_hcom_memory_allocator_cache_tier_policy)

[2.4.2.2.10 ubs_hcom_memory_allocator_type [228](#ubs_hcom_memory_allocator_type)](#ubs_hcom_memory_allocator_type)

[2.4.2.2.11 ubs_hcom_ep_handler_type [228](#ubs_hcom_ep_handler_type)](#ubs_hcom_ep_handler_type)

[2.4.2.2.12 ubs_hcom_op_handler_type [228](#ubs_hcom_op_handler_type)](#ubs_hcom_op_handler_type)

[2.4.2.2.13 ubs_hcom_polling_mode [229](#ubs_hcom_polling_mode)](#ubs_hcom_polling_mode)

[2.4.2.2.14 ubs_hcom_service_polling_mode [229](#ubs_hcom_service_polling_mode-1)](#ubs_hcom_service_polling_mode-1)

[3 环境变量参考 [230](#环境变量参考)](#环境变量参考)

[4 错误码 [233](#错误码)](#错误码)

[4.1 服务层错误码 [233](#服务层错误码)](#服务层错误码)

[4.2 传输层错误码 [235](#传输层错误码)](#传输层错误码)

[4.3 RDMA协议错误码 [238](#rdma协议错误码)](#rdma协议错误码)

# 介绍

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

# API 2.0

[3.1 基础API参考](#基础api参考)

[3.2 高级API参考](#高级api参考)

[3.3 结构体参考](#结构体参考)

[3.4 枚举值参考](#枚举值参考)

## 基础API参考

### C++ API

#### 服务层API

##### UBSHcomService::Create

1.  函数定义

根据类型、名字和可选配置项创建一个服务层的NetService对象。

2.  实现方法

static UBSHcomService\* UBSHcomService::Create(UBSHcomServiceProtocol t, const std::string &name, const UBSHcomServiceOptions &opt = {});

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| t | [UBSHcomNetDriverProtocol](#ubshcomnetdriverprotocol-1) | 入参 | UBSHcomService协议类型。 |
| name | String | 入参 | UBSHcomService的名称。长度范围\[1, 64\]，只能包含数字、字母、‘\_’和‘-’。 |
| opt | [UBSHcomServiceOptions](#ubshcomserviceoptions) | 入参 | 可选基础配置项。 |

4.  返回值

成功则返回NetService类型的实例，否则返回空。

##### UBSHcomService::Destroy

1.  函数定义

销毁服务，会清理全局map并根据名字销毁对象。

2.  实现方法

static int32_t UBSHcomService::Destroy(const std::string &name);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| name | String | 入参 | 要删除的服务对象的名称。长度范围\[1, 100\]，只能包含数字、字母、‘\_’和‘-’。 |

4.  返回值

表示函数执行结果，返回值为0则表示销毁成功。

##### UBSHcomService::Bind

1.  函数定义

服务端绑定监听的url和端口号

2.  实现方法

int32_t UBSHcomService::Bind(const std::string &listenerUrl, const UBSHcomServiceNewChannelHandler &handler)

3.  参数说明

    1.  参数说明

[TABLE]

1.  返回值

绑定成功返回0，失败返回对应错误码

##### UBSHcomService::Start

1.  函数定义

启动服务

2.  实现方法

int32_t UBSHcomService::Start()

3.  参数说明

无

4.  返回值

启动成功返回0，启动失败返回失败错误码。

##### UBSHcomService::Connect

1.  函数定义

客户端向服务端发起建链。

2.  实现方法

int32_t UBSHcomService::Connect(const std::string &serverUrl, UBSHcomChannelPtr &ch, const UBSHcomConnectOptions &opt = {})

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| serverUrl | const std::string | 入参 | 服务端绑定监听的url。 |
| ch | UBSHcomChannelPtr | 出参 | 建链成功返回的channel通道。 |
| opt | const UBSHcomConnectOptions & | 入参 | 建链配置项。 |

4.  返回值

无

##### UBSHcomService::Disconnect

1.  函数定义

断开链接。

2.  实现方法

void UBSHcomService::Disconnect(const UBSHcomChannelPtr &ch)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型                | 参数类型 | 描述                  |
|--------|-------------------------|----------|-----------------------|
| ch     | const UBSHcomChannelPtr | 入参     | 要断开的channel通道。 |

4.  返回值

无

##### UBSHcomService::RegisterMemoryRegion

1.  函数定义

- 注册一个内存区域，内存将在UBS Comm内部分配。

- 将用户申请的内存，注册到UBS Comm中。

  1.  实现方法

- int32_t UBSHcomService::RegisterMemoryRegion(uint64_t size, UBSHcomRegMemoryRegion &mr)

- int32_t UBSHcomService::RegisterMemoryRegion(uintptr_t address, uint64_t size, UBSHcomRegMemoryRegion &mr)

  1.  参数说明

      1.  参数说明

[TABLE]

![](media/image8.png)

若需要放入pgTable管理(通过UBSHcomService::SetEnableMrCache设置为true，默认不放入)，则要求首地址(startAddress)和尾地址(startAddress+size)都需要16字节对齐，因此用户申请的size需要能16整除。

2.  返回值

表示函数执行结果，返回值为0则表示注册成功。

##### UBSHcomService::DestroyMemoryRegion

1.  函数定义

销毁一个内存区域。

2.  实现方法

void UBSHcomService::DestroyMemoryRegion(UBSHcomRegMemoryRegion &mr)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型               | 参数类型 | 描述               |
|--------|------------------------|----------|--------------------|
| mr     | UBSHcomRegMemoryRegion | 入参     | 要销毁的内存区域。 |

4.  返回值

无

##### UBSHcomService::RegisterChannelBrokenHandler

![](media/image9.png)

用户实现的回调函数，内部不能销毁Service及相关的资源。

1.  函数定义

给UBSHcomService注册断链回调函数。

2.  实现方法

void UBSHcomService::RegisterChannelBrokenHandler(const UBSHcomServiceChannelBrokenHandler &handler, const UBSHcomChannelBrokenPolicy policy)

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型                           | 参数类型 | 描述           |
|---------|------------------------------------|----------|----------------|
| handler | UBSHcomServiceChannelBrokenHandler | 入参     | 断链回调函数。 |
| policy  | UBSHcomChannelBrokenPolicy         | 入参     | 断链回调策略。 |

4.  返回值

无

##### UBSHcomService::RegisterIdleHandler

![](media/image9.png)

用户实现的回调函数，内部不能销毁service及相关的资源。

1.  函数定义

给此UBSHcomService注册worker闲时回调函数

2.  实现方法

void UBSHcomService::RegisterIdleHandler(const NetServiceIdleHandler &h)

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型                        | 参数类型 | 描述       |
|---------|---------------------------------|----------|------------|
| handler | const UBSHcomServiceIdleHandler | 入参     | 回调函数。 |

4.  返回值

无

![](media/image8.png)

数据类型解释如下：

using UBSHcomServiceIdleHandler= std::function\<void(const UBSHcomNetWorkerIndex &)\>.

##### UBSHcomService::RegisterRecvHandler

![](media/image9.png)

- 用户实现的回调函数，内部不能销毁Service及相关的资源。

- 用户需要避免在该回调中死等发送完成事件，应添加超时时间，否则会造成死锁。

- 用户需要尽量避免在该回调中占用过长时间处理业务，以免影响性能。

  1.  函数定义

注册回调函数以处理异步通信收到消息事件。

2.  实现方法

void UBSHcomService::RegisterRecvHandler(const UBSHcomServiceRecvHandler &recvHandler)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| recvHandler | UBSHcomServiceRecvHandler | 入参 | 处理异步通信收数据事件的回调函数句柄。 |

4.  返回值

无

##### UBSHcomService::RegisterSendHandler

![](media/image9.png)

用户实现的回调函数，内部不能销毁Service及相关的资源。

1.  函数定义

注册回调函数以处理消息发送完成事件。

2.  实现方法

void UBSHcomService::RegisterSendHandler(const UBSHcomServiceSendHandler &sendHandler)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| sendHandler | UBSHcomServiceSendHandler | 入参 | 处理发送完成事件的回调函数句柄。 |

4.  返回值

无

##### UBSHcomService::RegisterOneSideHandler

![](media/image9.png)

用户实现的回调函数，内部不能销毁Service及相关的资源。

1.  函数定义

注册回调函数以处理单边读/写完成事件。

2.  实现方法

void UBSHcomService::RegisterOneSideHandler(const UBSHcomServiceOneSideDoneHandler &oneSideDoneHandler)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| oneSideDoneHandler | UBSHcomServiceOneSideDoneHandler | 入参 | 处理单边读/写完成事件的回调函数句柄。 |

4.  返回值

无

##### UBSHcomChannel::Send

![](media/image9.png)

- 若使用拆包和rndv的功能，需要通过UBSHcomChannel::SetUBSHcomTwoSideThreshold设置拆包和rndv的阈值。

- 使用rndv，则需要创建service后将UBSHcomService::SetEnableMrCache设置为true（UBSHcomService::RegisterMemoryRegion函数调用前）。

  1.  函数定义

&nbsp;

- 向对端异步发送一个双边请求消息，并且不等待响应。

- 向对端同步发送一个双边请求消息，并且不等待响应。

  1.  实现方法

- int32_t UBSHcomChannel::Send(const UBSHcomRequest &req, const Callback \*done)

- int32_t UBSHcomChannel::Send(const UBSHcomRequest &req)

  1.  参数说明

      1.  参数说明

[TABLE]

1.  返回值

表示函数执行结果，0表示发送成功。

##### UBSHcomChannel::Call

![](media/image9.png)

- rsp中若address字段填了有效内存地址，则回复会被拷贝到该地址上。

- 若address==NULL，则UBS Comm会通过malloc申请内存，但用户需要自行维护该内存的生命周期，在使用完后通过free释放。

- 若使用拆包和rndv的功能，需要通过UBSHcomChannel::SetUBSHcomTwoSideThreshold设置拆包和rndv的阈值。

- 使用rndv，则需要创建service后将UBSHcomService::SetEnableMrCache设置为true（UBSHcomService::RegisterMemoryRegion函数调用前）。

  1.  函数定义

&nbsp;

- 异步模式下，发送一个UBSHcomRequest消息，并等待对方回复UBSHcomResponse响应消息。

- 同步模式下，发送一个UBSHcomRequest消息，并等待对方回复UBSHcomResponse响应消息。

  1.  实现方法

- int32_t UBSHcomChannel::Call(const UBSHcomRequest &req, UBSHcomResponse &rsp, const Callback \*done)

- int32_t UBSHcomChannel::Call(const UBSHcomRequest &req, UBSHcomResponse &rsp)

  1.  参数说明

      1.  参数说明

[TABLE]

1.  返回值

表示函数执行结果，返回值为0则表示发送成功。

##### UBSHcomChannel::Reply

1.  函数定义

1\. 异步模式下，向对端回复一个消息，配合Call接口使用

2\. 同步模式下，向对端回复一个消息，配合Call接口使用

2.  实现方法

1\. int32_t UBSHcomChannel::Reply(const UBSHcomReplyContext &ctx, const UBSHcomRequest &req, const Callback \*done)

2\. int32_t UBSHcomChannel::Reply(const UBSHcomReplyContext &ctx, const UBSHcomRequest &req)

3.  参数说明

    1.  参数说明

[TABLE]

1.  返回值

表示函数执行结果，0表示发送成功。

##### UBSHcomChannel::Get

1.  函数定义

&nbsp;

1.  同步模式下，发送一个读请求给对方。

2.  异步模式下，发送一个读请求给对方。

    1.  实现方法

&nbsp;

1.  int32_t UBSHcomChannel::Get(const UBSHcomOneSideRequest &req, const Callback \*done)

2.  int32_t UBSHcomChannel::Get(const UBSHcomOneSideRequest &req)

    1.  参数说明

        1.  参数说明

[TABLE]

1.  返回值

表示函数执行结果，返回值为0则表示读请求成功。

##### UBSHcomChannel::Put

1.  函数定义

&nbsp;

1.  同步模式下，发送一个写请求给对方。

2.  异步模式下，发送一个写请求给对方。

    1.  实现方法

&nbsp;

1.  int32_t UBSHcomChannel::Put(const UBSHcomOneSideRequest &req, const Callback \*done)

2.  int32_t UBSHcomChannel::Put(const UBSHcomOneSideRequest &req)

    1.  参数说明

        1.  参数说明

[TABLE]

1.  返回值

表示函数执行结果，返回值为0则表示写请求成功。

##### UBSHcomChannel::Recv

1.  函数定义

只用于接收RNDV请求。

2.  实现方法

int32_t Recv(const UBSHcomServiceContext &context, uintptr_t address, uint32_t size, const Callback \*done = nullptr)

3.  参数说明

    1.  参数说明

[TABLE]

1.  返回值

表示函数执行结果，返回值为0则表示接收请求成功。

##### UBSHcomChannel::SetFlowControlConfig

1.  函数定义

设置限流。

2.  实现方法

int32_t SetFlowControlConfig(const UBSHcomFlowCtrlOptions &opt)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型                            | 参数类型 | 描述         |
|--------|-------------------------------------|----------|--------------|
| opt    | [UBSHcomFlowCtrlOptions](#flowctrloptions) | 入参     | 流控配置项。 |

4.  返回值

返回0表示成功

##### UBSHcomChannel::SetChannelTimeOut

1.  函数定义

给该channel设置超时时间。未设置时默认超时时间30s。

2.  实现方法

void UBSHcomChannel::SetChannelTimeOut(int16_t oneSideTimeout, int16_t twoSideTimeout)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| oneSideTimeout | int16_t | 入参 | 单边超时时间，单位为秒，0为立即超时，负数为永不超时（通常设置为-1）。范围是\[-1, INT16_MAX\]。未设置时默认超时时间30s。 |
| twoSideTimeout | int16_t | 入参 | 双边超时时间，单位为秒，0为立即超时，负数为永不超时（通常设置为-1）。范围是\[-1, INT16_MAX\]。未设置时默认超时时间30s。 |

4.  返回值

无

##### UBSHcomChannel::SetUBSHcomTwoSideThreshold

1.  函数定义

设置双边操作阈值。

2.  实现方法

int32_t SetUBSHcomTwoSideThreshold(const UBSHcomTwoSideThreshold &threshold)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| threshold | [UBSHcomTwoSideThreshold](#ubshcomtwosidethreshold) | 入参 | 双边操作阈值。 |

4.  返回值

返回0表示成功。

##### UBSHcomChannel::GetId

1.  函数定义

获得channel ID。

2.  实现方法

uint64_t UBSHcomChannel::GetId()

3.  参数说明

无

4.  返回值

uint64_t id信息。

##### UBSHcomChannel::GetPeerConnectPayload

1.  函数定义

获得建链的payLoad信息。

2.  实现方法

std::string UBSHcomChannel::GetPeerConnectPayload()

3.  参数说明

无

4.  返回值

playLoad信息。

##### UBSHcomChannel::SetTraceId

1.  函数定义

设置trace id。

2.  实现方法

void SetTraceId(const std::string &traceId)

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型    | 参数类型 | 描述               |
|---------|-------------|----------|--------------------|
| traceId | std::string | 入参     | 要设置的trace ID。 |

4.  返回值

无

##### UBSHcomServiceContext::Result

1.  函数定义

获得ctx的结果，表示通信操作的成功与否。

2.  实现方法

SerResult UBSHcomServiceContext::Result() const

3.  参数说明

无

4.  返回值

ctx的结果。

##### UBSHcomServiceContext::Channel

1.  函数定义

获得ctx的NetChannel，可以用于向对端回复消息。

2.  实现方法

const UBSHcomChannelPtr &UBSHcomServiceContext::Channel() const

3.  参数说明

无

4.  返回值

ctx的NetChannel。

##### UBSHcomServiceContext::OpType

1.  函数定义

获得ctx的操作类型。

2.  实现方法

Operation UBSHcomServiceContext::OpType() const

3.  参数说明

无

4.  返回值

[ctx的操作类型](#ZH-CN_TOPIC_0000002465536418)。

##### UBSHcomServiceContext::RspCtx

1.  函数定义

获得ctx的rspCtx，可以用于接收对端发送call消息后回复消息时当作参数使用。

2.  实现方法

uintptr_t UBSHcomServiceContext::RspCtx() const

3.  参数说明

无

4.  返回值

ctx的rspCtx。

##### UBSHcomServiceContext::ErrorCode

1.  函数定义

获得ctx的errorCode。

2.  实现方法

const int32_t UBSHcomServiceContext::ErrorCode()

3.  参数说明

无

4.  返回值

ctx的errorCode

##### UBSHcomServiceContext::OpCode

1.  函数定义

获得ctx的opCode。

2.  实现方法

uint16_t UBSHcomServiceContext::OpCode() const

3.  参数说明

无

4.  返回值

ctx的opCode。

##### UBSHcomServiceContext::MessageData

1.  函数定义

获得ctx的消息，为对端发送过来的消息。

2.  实现方法

void \*UBSHcomServiceContext::MessageData() const

3.  参数说明

无

4.  返回值

ctx的消息。

##### UBSHcomServiceContext::MessageDataLen

1.  函数定义

获得ctx的消息长度。

2.  实现方法

uint32_t UBSHcomServiceContext::MessageDataLen() const

3.  参数说明

无

4.  返回值

ctx的消息长度。

##### UBSHcomServiceContext::Clone

1.  函数定义

将ctx的内容拷贝。

2.  实现方法

static SerResult UBSHcomServiceContext::Clone(NetServiceContext &newOne, const NetServiceContext &oldOne, bool copyData = true)

3.  参数说明

    1.  参数说明

| 参数名   | 数据类型                    | 参数类型 | 描述            |
|----------|-----------------------------|----------|-----------------|
| newOne   | UBSHcomServiceContext       | 出参     | 拷贝得到的ctx。 |
| oldOne   | const UBSHcomServiceContext | 入参     | 被拷贝的ctx。   |
| copyData | bool                        | 入参     | 是否拷贝数据。  |

4.  返回值

返回值为0则表示成功。

##### UBSHcomServiceContext::IsTimeout

1.  函数定义

获得ctx是否超时，表示此次操作是否超时。

2.  实现方法

bool UBSHcomServiceContext::IsTimeout() const

3.  参数说明

无

4.  返回值

ctx是否超时。

##### UBSHcomServiceContext::Invalidate

1.  函数定义

将ctx的内容失效。

2.  实现方法

void UBSHcomServiceContext::Invalidate()

3.  参数说明

无

4.  返回值

无

##### UBSHcomService::SetEnableMrCache

![](media/image9.png)

若用户需要使用RNDV，则需要设置为true。

1.  函数定义

设置RegisterMemoryRegion是否将mr放入pgTable管理。

2.  实现说明

void SetEnableMrCache(bool enableMrCache);

3.  参数说明

    1.  参数说明

| 参数名        | 数据类型 | 参数类型 | 描述                      |
|---------------|----------|----------|---------------------------|
| enableMrCache | bool     | 入参     | mr放入pgTable管理标志位。 |

4.  返回值

无

#### 传输层API

##### UBSHcomNetDriver::Instance

1.  函数定义

生成UBSHcomNetDriver实例。

2.  实现方法

static UBSHcomNetDriver \*UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol t, const std::string &name, bool startOobSvr)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| t | [UBSHcomNetDriverProtocol](#ZH-CN_TOPIC_0000002498495129) | 入参 | 设置UBSHcomNetDriver的协议类型。 |
| name | const std::string | 入参 | 设置UBSHcomNetDriver的名字。长度范围\[1, 100\]，只能包含数字、字母、‘\_’和‘-’。 |
| startOobSvr | bool | 入参 | Server端设置为true，Client端设置为false。 |

4.  返回值

成功则返回UBSHcomNetDriver类型的实例，否则返回空。

##### UBSHcomNetDriver::DestroyInstance

1.  函数定义

销毁UBSHcomNetDriver实例。

2.  实现方法

static NResult UBSHcomNetDriver::DestroyInstance(const std::string &name)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| name | String | 入参 | 需要销毁UBSHcomNetDriver的名字。长度范围\[1, 100\]，只能包含数字、字母、‘\_’和‘-’。 |

4.  返回值

返回值为0则表示成功销毁UBSHcomNetDriver实例。

##### UBSHcomNetDriver::LocalSupport

1.  函数定义

通过UBSHcomNetDriver对象，校验本机是否支持所提供协议，若为RDMA协议且支持的情况下，会返回设备信息。

2.  实现方法

static bool UBSHcomNetDriver::LocalSupport(UBSHcomNetDriverProtocol t, UBSHcomNetDriverDeviceInfo &deviceInfo)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| t | [UBSHcomNetDriverProtocol](#ZH-CN_TOPIC_0000002498495129) | 入参 | 需要校验的协议。 |
| deviceInfo | [UBSHcomNetDriverDeviceInfo](#ZH-CN_TOPIC_0000002465376694) | 出参 | RDMA设备信息。 |

4.  返回值

返回值为true则表示支持此协议。

##### UBSHcomNetDriver::MultiRailGetDevCount

1.  函数定义

通过UBSHcomNetDriver对象，校验本机是否支持所提供协议，若为RDMA协议且支持的情况下，会通过ipGroup筛选符合要求的IP，若ipGroup为空，则会用ipMask来筛选。

2.  实现方法

static bool UBSHcomNetDriver::MultiRailGetDevCount(UBSHcomNetDriverProtocol t, std::string ipMask, uint16_t &enableDevCount, std::string ipGroup)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| t | [UBSHcomNetDriverProtocol](#ZH-CN_TOPIC_0000002498495129) | 入参 | 需要校验的协议。 |
| ipMask | std::string | 入参 | IP掩码。长度范围\[1, 256\]。 |
| enableDevCount | uint16_t | 出参 | 符合要求的RDMA设备个数。 |
| ipGroup | std::string | 入参 | IP组。长度范围\[1, 1024\]。 |

4.  返回值

返回值为true则表示支持此协议。

##### UBSHcomNetDriver::Initialize

1.  函数定义

初始化UBSHcomNetDriver。

2.  实现方法

NResult UBSHcomNetDriver::Initialize(const UBSHcomNetDriverOptions &option)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| option | [UBSHcomNetDriverOptions](#ZH-CN_TOPIC_0000002465376494) | 入参 | 根据Option，初始化UBSHcomNetDriver。 |

4.  返回值

返回值为0则表示初始化成功。

##### UBSHcomNetDriver::UnInitialize

1.  函数定义

取消初始化UBSHcomNetDriver。

2.  方法实现

void UBSHcomNetDriver::UnInitialize()

3.  参数说明

无

4.  返回值

无

##### UBSHcomNetDriver::Start

1.  函数定义

运行UBSHcomNetDriver。

2.  实现方法

NResult UBSHcomNetDriver::Start()

3.  参数说明

无

4.  返回值

返回值为0则表示运行UBSHcomNetDriver成功。

##### UBSHcomNetDriver::Stop

1.  函数定义

停止UBSHcomNetDriver。

2.  实现方法

void UBSHcomNetDriver::Stop()

3.  参数说明

无

4.  返回值

无

##### UBSHcomNetDriver::CreateMemoryRegion

1.  函数定义

&nbsp;

1.  注册一个内存区域，内存将在UBS Comm内部分配。

2.  注册一个内存区域，内存需要外部传入。

    1.  实现方法

- NResult UBSHcomNetDriver::CreateMemoryRegion(uint64_t size, UBSHcomNetMemoryRegionPtr &mr)

- NResult UBSHcomNetDriver::CreateMemoryRegion(uintptr_t address, uint64_t size, UBSHcomNetMemoryRegionPtr &mr)

- NResult CreateMemoryRegion(uint64_t size, UBSHcomNetMemoryRegionPtr &mr, unsigned long memid)

  1.  参数说明

      1.  参数说明

[TABLE]

2.  返回值

返回值为0则表示发送消息成功。

##### UBSHcomNetDriver::DestroyMemoryRegion

1.  函数定义

注销内存区域。

2.  实现方法

void UBSHcomNetDriver::DestroyMemoryRegion(UBSHcomNetMemoryRegionPtr &mr)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型                  | 参数类型 | 描述                 |
|--------|---------------------------|----------|----------------------|
| mr     | UBSHcomNetMemoryRegionPtr | 入参     | 需要注销的内存区域。 |

4.  返回值

无

##### UBSHcomNetDriver::Connect

![](media/image9.png)

如果用户实现中需要主动销毁EP，要调用UBSHcomNetEndpoint::Close接口；如果需要减少EP的引用计数，可调用DecreaseRef函数。

1.  函数定义

- 建立与Server的连接。利用设置IP端口或者UDS名称的方法来选择对端，指定本端和对端的worker group，指定链路类型。

- 建立与Server的连接。利用设置IP端口或者UDS名称的方法来选择对端，指定本端和对端的worker group。

- 建立与Server的连接。利用设置IP端口或者UDS名称的方法来选择对端，指定链路类型。

- 建立与Server的连接。利用设置IP端口或者UDS名称的方法来选择对端。

- 建立与Server的连接。自定义地址来选择对端，指定本端和对端的worker group，指定链路类型。

- 建立与Server的连接。自定义地址来选择对端，指定本端和对端的worker group，指定链路类型，指定sec校验时回调中的ctx。

- 建立与Server的连接。自定义地址来选择对端，指定本端和对端的worker group。

- 建立与Server的连接。自定义地址来选择对端，指定链路类型。

- 建立与Server的连接。自定义地址来选择对端。

  1.  实现方法

- NResult UBSHcomNetDriver::Connect( const std::string &payload, UBSHcomNetEndpointPtr &ep, uint32_t flags, uint8_t serverGrpNo, uint8_t clientGrpNo)

- NResult UBSHcomNetDriver::Connect(const std::string &payload, UBSHcomNetEndpointPtr &ep, uint8_t serverGrpNo, uint8_t clientGrpNo)

- NResult UBSHcomNetDriver::Connect(const std::string &payload, UBSHcomNetEndpointPtr &ep, uint32_t flags)

- NResult UBSHcomNetDriver::Connect(const std::string &payload, UBSHcomNetEndpointPtr &ep)

- NResult UBSHcomNetDriver::Connect(const std::string &oobIpOrName, uint16_t oobPort, const std::string &payload, UBSHcomNetEndpointPtr &ep, uint32_t flags, uint8_t serverGrpNo, uint8_t clientGrpNo)

- NResult UBSHcomNetDriver::Connect(const std::string &oobIpOrName, uint16_t oobPort, const std::string &payload, UBSHcomNetEndpointPtr &ep, uint32_t flags, uint8_t serverGrpNo, uint8_t clientGrpNo, uint64_t ctx)

- NResult UBSHcomNetDriver::Connect(const std::string &oobIpOrName, uint16_t oobPort, const std::string &payload, UBSHcomNetEndpointPtr &ep, uint8_t serverGrpNo, uint8_t clientGrpNo)

- NResult UBSHcomNetDriver::Connect(const std::string &oobIpOrName, uint16_t oobPort, const std::string &payload, UBSHcomNetEndpointPtr &ep, uint32_t flags)

- NResult UBSHcomNetDriver::Connect( const std::string &oobIpOrName, uint16_t oobPort, const std::string &payload, UBSHcomNetEndpointPtr &ep)

- NResult Connect(const std::string &serverUrl, const std::string &payload, UBSHcomNetEndpointPtr &ep, uint32_t flags, uint8_t serverGrpNo, uint8_t clientGrpNo, uint64_t ctx)

  1.  参数说明

      1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| oobIpOrName | String | 入参 | 连接时带外链路IP地址或者名字，用TCP则用IP地址，UDS则用名字。 |
| oobPort | uint16_t | 入参 | 当带外链路是TCP的时候，需要设置。范围是\[1024, 65535\]。 |
| payload | String | 入参 | Payload传输给对端，对端通过ep connect的回调中获得。长度范围\[0, 512\]。 |
| ep | UBSHcomNetEndpointPtr & | 出参 | 连接的EP。 |
| flags | uint32_t | 入参 | 可选参数，当创建同步EP时flags设置为Net_EP_SELF_POLLING。 |
| serverGrpNo | uint8_t | 入参 | 选择对端的worker group number。 |
| clientGrpNo | uint8_t | 入参 | 选择本端的worker group number。 |
| ctx | uint64_t | 入参 | secInfo回调时的ctx。 |

2.  返回值

- 返回值为0表示connect成功。

- 返回值为其它值则表示建链失败。

##### UBSHcomNetDriver::DestroyEndpoint

1.  函数定义

通过UBSHcomNetDriver对象来销毁EP。

2.  实现方法

void UBSHcomNetDriver::DestroyEndpoint(UBSHcomNetEndpointPtr &ep)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型              | 参数类型 | 描述             |
|--------|-----------------------|----------|------------------|
| ep     | UBSHcomNetEndpointPtr | 入参     | 需要被销毁的EP。 |

4.  返回值

无

##### UBSHcomNetDriver::OobIpAndPort

1.  函数定义

给UBSHcomNetDriver对象设置OOB的IP和端口号。当此UBSHcomNetDriver是server时，UBSHcomNetDriver会监听此ipPort，且此方法在可以多次调用，会同时监听多个ipPort组合；当此UBSHcomNetDriver是client时，client在Connect时会默认向此ipPort建链。

2.  实现方法

void UBSHcomNetDriver::OobIpAndPort(const std::string &ip, uint16_t port)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| ip | const std::string | 入参 | IP。该字段内部有系统函数对IP有效性进行校验。 |
| port | uint16_t | 入参 | 端口号。范围值\[1024, 65535\]。 |

4.  返回值

无

##### UBSHcomNetDriver::GetOobIpAndPort

1.  函数定义

得到UBSHcomNetDriver对象的OOB的IP和Port。

2.  实现方法

bool UBSHcomNetDriver::GetOobIpAndPort(std::vector\<std::pair\<std::string, uint16_t\>\> &result)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| result | std::vector\<std::pair\<std::string, uint16_t\>\> | 出参 | IP和Port的数组。 |

4.  返回值

返回值为true则表示成功。

##### UBSHcomNetDriver::AddOobOptions

1.  函数定义

设置UBSHcomNetDriver对象的OOB的IP和Port。

2.  实现方法

void UBSHcomNetDriver::AddOobOptions(const UBSHcomNetOobListenerOptions &option)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| option | const [UBSHcomNetOobListenerOptions](#ZH-CN_TOPIC_0000002498615633) | 入参 | IP和Port的数组。 |

4.  返回值

无

##### UBSHcomNetDriver::OobUdsName

1.  函数定义

给UBSHcomNetDriver对象设置的OOB type为UDS时的name。当此UBSHcomNetDriver是server时，UBSHcomNetDriver会监听此name，且此方法在可以多次调用，会同时监听多个name；当此UBSHcomNetDriver是client时，client在Connect时会默认向此name建链。

2.  实现方法

void UBSHcomNetDriver::OobUdsName(const std::string &name)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型    | 参数类型 | 描述                                |
|--------|-------------|----------|-------------------------------------|
| name   | std::string | 入参     | 需要设置的name。长度范围是(0, 96)。 |

4.  返回值

无

##### UBSHcomNetDriver::AddOobUdsOptions

1.  函数定义

给UBSHcomNetDriver对象设置的OOB type为UDS时的name和一些参数。

2.  实现方法

void UBSHcomNetDriver::AddOobUdsOptions(const UBSHcomNetOobUDSListenerOptions &option)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| option | [UBSHcomNetOobUDSListenerOptions](#ZH-CN_TOPIC_0000002465536522) | 入参 | 需要设置的UDS参数。 |

4.  返回值

无

##### UBSHcomNetDriver::RegisterNewEPHandler

![](media/image9.png)

用户实现的回调函数，内部不能销毁driver及相关的资源。

1.  函数定义

为从Client端连接的新链接注册回调，只需要在Server端注册。

2.  实现方法

void UBSHcomNetDriver::RegisterNewEPHandler(const UBSHcomNetDriverNewEndPointHandler &handler)

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型                           | 参数类型 | 描述       |
|---------|------------------------------------|----------|------------|
| handler | UBSHcomNetDriverNewEndPointHandler | 入参     | 回调函数。 |

4.  返回值

无

![](media/image8.png)

数据类型解释如下：

using UBSHcomNetDriverNewEndPointHandler =std::function\<int(const std::string &ipPort, const UBSHcomNetEndpointPtr &, const std::string &payload)\>.

##### UBSHcomNetDriver::RegisterEPBrokenHandler

![](media/image9.png)

用户实现的回调函数，内部不能销毁driver及相关的资源。

1.  函数定义

给UBSHcomNetDriver对象设置EP断链回调函数。

2.  实现方法

void UBSHcomNetDriver::RegisterEPBrokenHandler(const UBSHcomNetDriverEndpointBrokenHandler &handler)

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型                              | 参数类型 | 描述           |
|---------|---------------------------------------|----------|----------------|
| handler | UBSHcomNetDriverEndpointBrokenHandler | 入参     | 断链回调函数。 |

4.  返回值

无

![](media/image8.png)

数据类型解释如下：

using UBSHcomNetDriverEndpointBrokenHandler = std::function\<void(const UBSHcomNetEndpointPtr &)\>.

##### UBSHcomNetDriver::RegisterNewReqHandler

![](media/image9.png)

用户实现的回调函数，内部不能销毁driver及相关的资源。

1.  函数定义

注册接收到对方请求的回调。

2.  实现方法

void UBSHcomNetDriver::RegisterNewReqHandler(const UBSHcomNetDriverReceivedHandler &handler)

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型                        | 参数类型 | 描述       |
|---------|---------------------------------|----------|------------|
| handler | UBSHcomNetDriverReceivedHandler | 入参     | 回调函数。 |

4.  返回值

无

![](media/image8.png)

数据类型解释如下：

using UBSHcomNetDriverReceivedHandler = std::function\<int(const UBSHcomNetRequestContext &)\>.

##### UBSHcomNetDriver::RegisterReqPostedHandler

![](media/image9.png)

用户实现的回调函数，内部不能销毁driver及相关的资源。

1.  函数定义

注册将请求发送到对端的回调。

2.  实现方法

void UBSHcomNetDriver::RegisterReqPostedHandler(const UBSHcomNetDriverSentHandler &handler)

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型                    | 参数类型 | 描述       |
|---------|-----------------------------|----------|------------|
| handler | UBSHcomNetDriverSentHandler | 入参     | 回调函数。 |

4.  返回值

无

![](media/image8.png)

数据类型解释如下：

using UBSHcomNetDriverSentHandler = std::function\<int(const UBSHcomNetRequestContext &)\>.

##### UBSHcomNetDriver::RegisterOneSideDoneHandler

![](media/image9.png)

用户实现的回调函数，内部不能销毁driver及相关的资源。

1.  函数定义

注册单边操作完成的回调。

2.  实现方法

void UBSHcomNetDriver::RegisterOneSideDoneHandler(const UBSHcomNetDriverOneSideDoneHandler &handler)

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型                           | 参数类型 | 描述       |
|---------|------------------------------------|----------|------------|
| handler | UBSHcomNetDriverOneSideDoneHandler | 入参     | 回调函数。 |

4.  返回值

无

![](media/image8.png)

数据类型解释如下：

using UBSHcomNetDriverOneSideDoneHandler = std::function\<int(const UBSHcomNetRequestContext &)\>.

##### UBSHcomNetDriver::RegisterIdleHandler

![](media/image9.png)

用户实现的回调函数，内部不能销毁driver及相关的资源。

1.  函数定义

给UBSHcomNetDriver对象设置worker闲时回调函数。

2.  实现方法

void UBSHcomNetDriver::RegisterIdleHandler(const UBSHcomNetDriverIdleHandler &handler)

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型                    | 参数类型 | 描述           |
|---------|-----------------------------|----------|----------------|
| handler | UBSHcomNetDriverIdleHandler | 入参     | 闲时回调函数。 |

4.  返回值

无

![](media/image8.png)

数据类型解释如下：

using UBSHcomNetDriverIdleHandler = std::function\<void(const UBSHcomNetWorkerIndex &)\>.

##### UBSHcomNetDriver::Name

1.  函数定义

得到UBSHcomNetDriver对象的name。

2.  实现方法

const std::string &UBSHcomNetDriver::Name() const

3.  参数说明

无

4.  返回值

返回UBSHcomNetDriver对象的name。

##### UBSHcomNetDriver::GetId

1.  函数定义

得到UBSHcomNetDriver对象的index。

2.  实现方法

uint8_t UBSHcomNetDriver::GetId() const

3.  参数说明

无

4.  返回值

返回UBSHcomNetDriver对象的index。

##### UBSHcomNetDriver::Protocol

1.  函数定义

得到UBSHcomNetDriver对象的通信协议。

2.  实现方法

UBSHcomNetDriverProtocol UBSHcomNetDriver::Protocol() const

3.  参数说明

无

4.  返回值

返回UBSHcomNetDriver对象的[通信协议](#ZH-CN_TOPIC_0000002498495129)。

##### UBSHcomNetDriver::IsStarted

1.  函数定义

得到UBSHcomNetDriver对象的是否启动。

2.  实现方法

bool UBSHcomNetDriver::IsStarted() const

3.  参数说明

无

4.  返回值

返回UBSHcomNetDriver对象的是否启动。

##### UBSHcomNetDriver::IsInited

1.  函数定义

得到UBSHcomNetDriver对象的是否初始化。

2.  实现方法

bool UBSHcomNetDriver::IsInited() const

3.  参数说明

无

4.  返回值

返回UBSHcomNetDriver对象的是否初始化。

##### UBSHcomNetDriver::NetUid

1.  函数定义

通过UBSHcomNetDriver对象获得一个新的UID。

2.  实现方法

uint64_t UBSHcomNetDriver::NetUid() const

3.  参数说明

无

4.  返回值

返回一个UID。

##### UBSHcomNetDriver::DumpObjectStatistics

1.  函数定义

得到UBSHcomNetDriver对象的各项成员变量的引用计数。

2.  实现方法

static void UBSHcomNetDriver::DumpObjectStatistics()

3.  参数说明

无

4.  返回值

返回UBSHcomNetDriver对象的各项成员变量的引用计数。

##### UBSHcomNetDriver::SetPeerDevId

1.  函数定义

给UBSHcomNetDriver对象设置对端RDMA设备索引。

2.  实现方法

void UBSHcomNetDriver::SetPeerDevId(uint8_t index)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述                                       |
|--------|----------|----------|--------------------------------------------|
| index  | uint8_t  | 入参     | 对端RDMA设备索引。范围是\[0, UINT8_MAX\]。 |

4.  返回值

无

##### UBSHcomNetDriver::GetPeerDevId

1.  函数定义

得到UBSHcomNetDriver对象的对端RDMA设备索引。

2.  实现方法

uint8_t UBSHcomNetDriver::GetPeerDevId() const

3.  参数说明

无

4.  返回值

返回UBSHcomNetDriver对象的对端RDMA设备索引。

##### UBSHcomNetDriver::SetDeviceId

1.  函数定义

给UBSHcomNetDriver对象设置RDMA设备索引。

2.  实现方法

void UBSHcomNetDriver::SetDeviceId(uint8_t index)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述                                   |
|--------|----------|----------|----------------------------------------|
| index  | uint8_t  | 入参     | RDMA设备索引。范围是\[0, UINT8_MAX\]。 |

4.  返回值

无

##### UBSHcomNetDriver::GetDeviceId

1.  函数定义

得到UBSHcomNetDriver对象的RDMA设备索引。

2.  实现方法

uint8_t UBSHcomNetDriver::GetDeviceId() const

3.  参数说明

无

4.  返回值

返回UBSHcomNetDriver对象的RDMA设备索引。

##### UBSHcomNetDriver::GetBandWidth

1.  函数定义

得到UBSHcomNetDriver对象的带宽。

2.  实现方法

uint8_t UBSHcomNetDriver::GetBandWidth() const

3.  参数说明

无

4.  返回值

返回UBSHcomNetDriver对象的带宽。

##### UBSHcomNetDriver::OobEidAndJettyId

1.  函数定义

暂时只支持UBC协议时使用，传入对应的EID用于公知jetty自举建链

2.  实现方法

void UBSHcomNetDriver::OobEidAndJettyId(const std::string &eid, uint16_t id)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型          | 参数类型 | 描述        |
|--------|-------------------|----------|-------------|
| eid    | const std::string | 入参     | eid字符串。 |
| id     | uint16_t          | 入参     | port。      |

4.  返回值

无

##### UBSHcomNetEndpoint::SetEpOption

1.  函数定义

暂时只支持TCP协议时使用，TCP通信默认为非阻塞通信，可以将此EP设置为阻塞通信。

2.  实现方法

NResult UBSHcomNetEndpoint::SetEpOption(UBSHcomEpOptions &epOptions)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| epOptions | [UBSHcomEpOptions](#ZH-CN_TOPIC_0000002498496065) | 入参 | 链路设置选项。 |

4.  返回值

返回值为0则表示设置成功。

##### UBSHcomNetEndpoint::GetSendQueueCount

1.  函数定义

得到此EP正在使用的发送队列大小。

2.  实现方法

uint32_t UBSHcomNetEndpoint::GetSendQueueCount()

3.  参数说明

无

4.  返回值

返回正在使用的发送队列大小。

##### UBSHcomNetEndpoint::Id

1.  函数定义

得到此EP的ID。

2.  实现方法

uint64_t UBSHcomNetEndpoint::Id() const

3.  参数说明

无

4.  返回值

返回此EP的ID。

##### UBSHcomNetEndpoint::WorkerIndex

1.  函数定义

得到此EP所在的worker的索引。

2.  实现方法

const UBSHcomNetWorkerIndex &UBSHcomNetEndpoint::WorkerIndex() const

3.  参数说明

无

4.  返回值

返回此EP所在的worker的索引。

##### UBSHcomNetEndpoint::IsEstablished

1.  函数定义

得到此EP的状态是否为已创建。

2.  实现方法

bool UBSHcomNetEndpoint::IsEstablished()

3.  参数说明

无

4.  返回值

返回此EP的状态是否为已创建。

##### UBSHcomNetEndpoint::UpCtx

1.  函数定义

用于设置此EP的上层上下文，其中储存用户所需的数据指针，在回调函数中可以被得到。

2.  实现方法

void UBSHcomNetEndpoint::UpCtx(uint64_t ctx)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述                                          |
|--------|----------|----------|-----------------------------------------------|
| ctx    | uint64_t | 入参     | 用户上下文数据指针。范围是\[0, UINT64_MAX\]。 |

4.  返回值

无

##### UBSHcomNetEndpoint::UpCtx

1.  函数定义

用于得到此EP的上层上下文。

2.  实现方法

uint64_t UBSHcomNetEndpoint::UpCtx() const

3.  参数说明

无

4.  返回值

返回上层上下文。

##### UBSHcomNetEndpoint::PeerConnectPayload

1.  函数定义

用于得到此EP建链时设置的payload信息。

2.  实现方法

const std::string &UBSHcomNetEndpoint::PeerConnectPayload() const

3.  参数说明

无

4.  返回值

返回payload。

##### UBSHcomNetEndpoint::LocalIp

1.  函数定义

用于得到此EP的本端IP地址。

2.  实现方法

uint32_t UBSHcomNetEndpoint::LocalIp() const

3.  参数说明

无

4.  返回值

返回此EP的本端IP地址。

##### UBSHcomNetEndpoint::ListenPort

1.  函数定义

用于得到此EP建链时监听所用的端口。

2.  实现方法

uint16_t UBSHcomNetEndpoint::ListenPort() const

3.  参数说明

无

4.  返回值

返回此EP建链时监听时所用端口。

##### UBSHcomNetEndpoint::Version

1.  函数定义

用于得到此EP所在UBSHcomNetDriver的version。

2.  实现方法

uint8_t UBSHcomNetEndpoint::Version() const

3.  参数说明

无

4.  返回值

返回此EP所在UBSHcomNetDriver的version。

##### UBSHcomNetEndpoint::State

1.  函数定义

用于得到此EP的状态。

2.  实现方法

UBSHcomNetAtomicState\<UBSHcomNetEndPointState\> &UBSHcomNetEndpoint::State()

3.  参数说明

无

4.  返回值

返回此[EP的状态](#ZH-CN_TOPIC_0000002465535858)。

##### UBSHcomNetEndpoint::PeerIpAndPort

1.  函数定义

用于得到此EP的对端IP和端口信息。

2.  实现方法

const std::string &UBSHcomNetEndpoint::PeerIpAndPort()

3.  参数说明

无

4.  返回值

返回此EP的对端IP和端口信息。

##### UBSHcomNetEndpoint::UdsName

1.  函数定义

仅对SHM协议有效，用于得到此EP的UDS name。

2.  实现方法

const std::string &UBSHcomNetEndpoint::UdsName()

3.  参数说明

无

4.  返回值

返回此EP的UDS name。

##### UBSHcomNetEndpoint::PostSend

1.  函数定义

- 发送一个带有opcode和header的请求给对方，并且自定义seqNo。

- 发送一个带有opcode和header的请求给对方，并且可以设置操作参数。

- 发送一个带有opcode和header的请求给对方。

  1.  实现方法

- NResult UBSHcomNetEndpoint::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, uint32_t seqNo)

- NResult UBSHcomNetEndpoint::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, const UBSHcomNetTransOpInfo &opInfo)

- NResult UBSHcomNetEndpoint::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request)

  1.  参数说明

      1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| opCode | uint16_t | 入参 | 操作码\[0, 1023\]。 |
| request | [UBSHcomNetTransRequest](#ZH-CN_TOPIC_0000002498615245) | 入参 | 发送请求信息，使用本地内存来存储数据，数据会被复制，调用后可释放本地内存。 |
| seqNo | uint32_t | 入参 | 对方要回复的seqNo必须大于0，对方可以从context.Header().seqNo中获取它；如果seqNo为0，则生成自动递增的数字。在同步发送消息的情况下，请求和响应的seqNo相等。 |
| opInfo | [UBSHcomNetTransOpInfo](#ZH-CN_TOPIC_0000002465377186) | 入参 | 此发送操作相关的参数。 |

2.  返回值

返回值为0则表示发送消息成功。

![](media/image8.png)

- 如果NET_EP_SELF_POLLING未设置，则只发出发送请求，不等待发送请求完成情况。

- 如果NET_EP_SELF_POLLING设置，则发出发送请求并等待发送到达对端。

##### UBSHcomNetEndpoint::PostSendRaw

1.  函数定义

发送一个不带有header的请求给对方，并且自定义为seqNo，对方将触发新的请求回调，同样不带有header。

2.  实现方法

NResult UBSHcomNetEndpoint::PostSendRaw(const UBSHcomNetTransRequest &request,uint32_t seqNo)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| request | [UBSHcomNetTransRequest](#ZH-CN_TOPIC_0000002498615245) | 入参 | 发送请求信息，使用本地内存来存储数据，数据会被复制，调用后可释放本地内存。 |
| seqNo | uint32_t | 入参 | 对方要回复的seqNo必须大于0，对方可以从context.Header().seqNo中获取它；如果seqNo为0，则生成自动递增的数字。在同步发送消息的情况下，请求和响应的seqNo相等。 |

4.  返回值

返回值为0则表示发送消息成功。

![](media/image8.png)

- 如果NET_EP_SELF_POLLING未设置，则只发出发送请求，不等待发送请求完成情况。

- 如果NET_EP_SELF_POLLING设置，则发出发送请求并等待发送到达对端。

##### UBSHcomNetEndpoint::PostRead

1.  函数定义

将单边读请求发送到对端，对端不会触发回调。

2.  实现方法

NResult UBSHcomNetEndpoint::PostRead(const UBSHcomNetTransRequest &request)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| request | [UBSHcomNetTransRequest](#ZH-CN_TOPIC_0000002498615245) | 入参 | 发送请求信息。 |

4.  返回值

返回值为0则表示发送消息成功。

##### UBSHcomNetEndpoint::PostWrite

1.  函数定义

将单边写请求发送到对端，对端不会触发回调。

2.  实现方法

NResult UBSHcomNetEndpoint::PostWrite(const UBSHcomNetTransRequest &request)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| request | [UBSHcomNetTransRequest](#ZH-CN_TOPIC_0000002498615245) | 入参 | 发送请求信息。 |

4.  返回值

返回值为0则表示发送消息成功。

##### UBSHcomNetEndpoint::DefaultTimeout

1.  函数定义

用于设置此EP的超时时间。

2.  实现方法

void UBSHcomNetEndpoint::DefaultTimeout(int32_t timeout)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| timeout | int32_t | 入参 | 超时时间，单位为秒，0为立即超时，负数为永不超时（一般设置为-1）。设置时小于或等于65536。 |

4.  返回值

无

##### UBSHcomNetEndpoint::WaitCompletion

1.  函数定义

- 等待发送/读/写完成，仅对NET_EP_SELF_POLLING设置时使用。

- 等待发送/读/写完成，仅对NET_EP_SELF_POLLING设置时使用。使用mDefaultTimeout作为超时时间。

  1.  实现方法

- NResult UBSHcomNetEndpoint::WaitCompletion(int32_t timeout)

- NResult UBSHcomNetEndpoint::WaitCompletion()

  1.  参数说明

      1.  参数说明

[TABLE]

1.  返回值

返回值为0则表示发送消息成功。

![](media/image8.png)

- 此函数用在发送后时，当请求发送到对方时被调用。

- 此函数用在读后时，当读完成时被调用。

- 此函数用在写后时，当写完成时被调用。

##### UBSHcomNetEndpoint::Receive

1.  函数定义

- 接收对端发送的Send消息。

- 接收对端发送的Send消息。使用mDefaultTimeout作为超时时间。

  1.  实现方法

- NResult UBSHcomNetEndpoint::Receive(int32_t timeout, UBSHcomNetResponseContext &ctx)

- NResult UBSHcomNetEndpoint::Receive(UBSHcomNetResponseContext &ctx)

  1.  参数说明

      1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| timeout | int32_t | 入参 | 超时时间，单位为秒，0为立即超时，负数为永不超时（一般设置为-1）。 |
| ctx | UBSHcomNetResponseContext | 入参 | 用来存放收到消息的内容的对象。 |

2.  返回值

返回值为0则表示成功。

##### UBSHcomNetEndpoint::ReceiveRaw

1.  函数定义

- 接收对端发送的SendRaw消息。

- 接收对端发送的SendRaw消息。使用mDefaultTimeout作为超时时间。

  1.  实现方法

- NResult UBSHcomNetEndpoint::ReceiveRaw(int32_t timeout, UBSHcomNetResponseContext &ctx)

- NResult UBSHcomNetEndpoint::ReceiveRaw(UBSHcomNetResponseContext &ctx)

  1.  参数说明

      1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| timeout | int32_t | 入参 | 超时时间，单位为秒，0为立即超时，负数为永不超时（一般设置为-1）。 |
| ctx | UBSHcomNetResponseContext | 入参 | 用来存放收到消息的内容的对象。 |

2.  返回值

返回值为0则表示成功。

##### UBSHcomNetEndpoint::GetRemoteUdsIdInfo

1.  函数定义

仅支持服务端且OOB type为UDS时，查询此EP的对端UDS ID信息。

2.  实现方法

NResult UBSHcomNetEndpoint::GetRemoteUdsIdInfo(UBSHcomNetUdsIdInfo &idInfo)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| idInfo | [UBSHcomNetUdsIdInfo](#ZH-CN_TOPIC_0000002498615509) | 出参 | 对端UDS ID信息。 |

4.  返回值

返回值为0则表示成功。

##### UBSHcomNetEndpoint::GetPeerIpPort

1.  函数定义

查询此EP对端的IP地址和端口信息。

2.  实现方法

bool UBSHcomNetEndpoint::GetPeerIpPort(std::string &ip, uint16_t &port)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述         |
|--------|----------|----------|--------------|
| ip     | String   | 出参     | 对端IP地址。 |
| port   | uint16_t | 出参     | 对端的端口。 |

4.  返回值

返回值为true则表示成功。

##### UBSHcomNetEndpoint::Close

![](media/image9.png)

如果用户实现中需要主动销毁EP，要调用UBSHcomNetEndpoint::Close接口；如果需要减少EP的引用计数，可调用DecreaseRef函数。

1.  函数定义

关闭此EP。

2.  实现方法

void UBSHcomNetEndpoint::Close()

3.  参数说明

无

4.  返回值

无

##### UBSHcomNetEndpoint::GetDevIndex

1.  函数定义

仅在RDMA协议下，得到此EP的设备索引。

2.  实现方法

uint8_t UBSHcomNetEndpoint::GetDevIndex()

3.  参数说明

无

4.  返回值

返回此EP的设备索引。

##### UBSHcomNetEndpoint::GetPeerDevIndex

1.  函数定义

仅在RDMA协议下，得到此EP的对端设备索引。

2.  实现方法

uint8_t UBSHcomNetEndpoint::GetPeerDevIndex()

3.  参数说明

无

4.  返回值

返回此EP的对端设备索引。

##### UBSHcomNetEndpoint::GetBandWidth

1.  函数定义

仅在RDMA协议下，得到此EP的设备带宽。

2.  实现方法

uint8_t UBSHcomNetEndpoint::GetBandWidth()

3.  参数说明

无

4.  返回值

返回此EP的设备带宽。

##### UBSHcomNetMessage::DataLen

1.  函数定义

得到此UBSHcomNetMessage的大小。

2.  实现方法

uint32_t UBSHcomNetMessage::DataLen() const

3.  参数说明

无

4.  返回值

返回此UBSHcomNetMessage的大小。

##### UBSHcomNetMessage::Data

1.  函数定义

得到此UBSHcomNetMessage的消息。

2.  实现方法

void \*UBSHcomNetMessage::Data() const

3.  参数说明

无

4.  返回值

返回此UBSHcomNetMessage的消息。

##### UBSHcomNetRequestContext::EndPoint

1.  函数定义

在回调函数中，通过ctx参数获得此消息所关联的EP。

2.  实现方法

const UBSHcomNetEndpointPtr &UBSHcomNetRequestContext::EndPoint() const

3.  参数说明

无

4.  返回值

返回此消息所关联的EP。

##### UBSHcomNetRequestContext::Result

1.  函数定义

在回调函数中，通过ctx参数获得此次通信的结果。

2.  实现方法

NResult UBSHcomNetRequestContext::Result() const

3.  参数说明

无

4.  返回值

返回此消息所关联的EP。

##### UBSHcomNetRequestContext::Header

1.  函数定义

在回调函数中，通过ctx参数获得此次通信对端发送过来的Header。

2.  实现方法

const UBSHcomNetTransHeader &UBSHcomNetRequestContext::Header() const

3.  参数说明

无

4.  返回值

返回此次通信对端发送过来的Header。

##### UBSHcomNetRequestContext::Message

1.  函数定义

在回调函数中，通过ctx参数获得此次通信对端发送过来的消息信息。

2.  实现方法

UBSHcomNetMessage \*UBSHcomNetRequestContext::Message() const

3.  参数说明

无

4.  返回值

返回此次通信对端发送过来的消息信息。

##### UBSHcomNetRequestContext::OpType

1.  函数定义

在回调函数中，通过ctx参数获得此次通信的类型。

2.  实现方法

NN_OpType UBSHcomNetRequestContext::OpType() const

3.  参数说明

无

4.  返回值

返回此次通信的类型。

##### UBSHcomNetRequestContext::OriginalRequest

1.  函数定义

在回调函数中，通过ctx参数获得此次通信的发送消息。

2.  实现方法

const UBSHcomNetTransRequest &UBSHcomNetRequestContext::OriginalRequest() const

3.  参数说明

无

4.  返回值

返回此次通信的发送消息。

##### UBSHcomNetRequestContext::OriginalSgeRequest

1.  函数定义

在回调函数中，通过ctx参数获得此次通信的SGL发送消息。

2.  实现方法

const UBSHcomNetTransSglRequest &UBSHcomNetRequestContext::OriginalSgeRequest() const

3.  参数说明

无

4.  返回值

返回此次通信的SGL发送消息。

##### UBSHcomNetRequestContext::SafeClone

1.  函数定义

在回调函数中，将ctx信息进行拷贝。

2.  实现方法

static bool UBSHcomNetRequestContext::SafeClone(const UBSHcomNetRequestContext &old, const UBSHcomNetRequestContextPtr &newOne)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型                    | 参数类型 | 描述              |
|--------|-----------------------------|----------|-------------------|
| old    | UBSHcomNetRequestContext    | 入参     | 需要被拷贝的ctx。 |
| newOne | UBSHcomNetRequestContextPtr | 出参     | 拷贝得到的ctx。   |

4.  返回值

返回true则成功，会拷贝其中的EP，mHeader，mOpType信息。

##### UBSHcomNetResponseContext::Header

1.  函数定义

得到NetResponseContext中的header。

2.  实现方法

const UBSHcomNetTransHeader &UBSHcomNetResponseContext::Header() const

3.  参数说明

无

4.  返回值

返回NetResponseContext中的header。

##### UBSHcomNetResponseContext::Message

1.  函数定义

得到NetResponseContext中的消息。

2.  实现方法

UBSHcomNetMessage \*UBSHcomNetResponseContext::Message() const

3.  参数说明

无

4.  返回值

返回NetResponseContext中的消息。

##### UBSHcomNetMemoryRegion::GetLKey

1.  函数定义

获得MR的local key。

2.  实现方法

uint32_t UBSHcomNetMemoryRegion::GetLKey() const

3.  参数说明

无

4.  返回值

返回local key。

##### UBSHcomNetMemoryRegion::GetAddress

1.  函数定义

获得MR的内存地址。

2.  实现方法

uintptr_t UBSHcomNetMemoryRegion::GetAddress() const

3.  参数说明

无

4.  返回值

返回内存地址。

##### UBSHcomNetMemoryRegion::Size

1.  函数定义

获得MR的内存大小。

2.  实现方法

uint64_t UBSHcomNetMemoryRegion::Size() const

3.  参数说明

无

4.  返回值

返回内存大小。

##### UBSHcomNetMemoryAllocator::Create

1.  函数定义

创建一个内存分配器。

2.  实现方法

static NResult UBSHcomNetMemoryAllocator::Create(UBSHcomNetMemoryAllocatorType t, const UBSHcomNetMemoryAllocatorOptions &options, UBSHcomNetMemoryAllocatorPtr &out)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| t | [UBSHcomNetMemoryAllocatorType](#ZH-CN_TOPIC_0000002498615653) | 入参 | 分配器类型。 |
| options | [UBSHcomNetMemoryAllocatorOptions](#ZH-CN_TOPIC_0000002498615221) | 入参 | 分配器参数。 |
| out | UBSHcomNetMemoryAllocatorPtr | 出参 | 创建的分配器指针。 |

4.  返回值

返回值为0则表示成功。

##### UBSHcomNetMemoryAllocator::MrKey

1.  函数定义

得到分配器的memory region key。

2.  实现方法

uint32_t UBSHcomNetMemoryAllocator::MrKey() const

3.  参数说明

无

4.  返回值

返回分配器的memory region key。

##### UBSHcomNetMemoryAllocator::MrKey

1.  函数定义

给分配器设置memory region key。

2.  实现方法

void UBSHcomNetMemoryAllocator::MrKey(uint32_t mrKey)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述                                        |
|--------|----------|----------|---------------------------------------------|
| mrKey  | uint32_t | 入参     | memory region key。范围值(0, UINT32_MAX\]。 |

4.  返回值

无

##### UBSHcomNetMemoryAllocator::MemOffset

1.  函数定义

得到地址在分配器内存的偏移值。

2.  实现方法

uintptr_t UBSHcomNetMemoryAllocator::MemOffset(uintptr_t address) const

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型  | 参数类型 | 描述       |
|---------|-----------|----------|------------|
| address | uintptr_t | 入参     | 内存地址。 |

4.  返回值

返回偏移值。

##### UBSHcomNetMemoryAllocator::FreeSize

1.  函数定义

得到分配器剩余的内存大小。

2.  实现方法

uint64_t UBSHcomNetMemoryAllocator::FreeSize() const

3.  参数说明

无

4.  返回值

返回分配器剩余的内存大小。

##### UBSHcomNetMemoryAllocator::Allocate

1.  函数定义

从内存分配器中分配出指定大小的内存。

2.  实现方法

NResult UBSHcomNetMemoryAllocator::Allocate(uint64_t size, uintptr_t &outAddress)

3.  参数说明

    1.  参数说明

| 参数名     | 数据类型  | 参数类型 | 描述                 |
|------------|-----------|----------|----------------------|
| size       | uint64_t  | 入参     | 需要分配的内存大小。 |
| outAddress | uintptr_t | 出参     | 分配的内存地址。     |

4.  返回值

返回值为0则表示成功。

##### UBSHcomNetMemoryAllocator::Free

1.  函数定义

将从内存分配器中分配的内存释放给分配器。

2.  实现方法

NResult UBSHcomNetMemoryAllocator::Free(uintptr_t address)

![](media/image8.png)

使用时防止相同address多次调用该函数。

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型  | 参数类型 | 描述                 |
|---------|-----------|----------|----------------------|
| address | uintptr_t | 入参     | 需要释放的内存地址。 |

4.  返回值

返回值为0则表示成功。

##### UBSHcomNetMemoryAllocator::Destroy

1.  函数定义

当编译选项BUILD_WITH_ALLOCATOR_PROTECTION为ON时，去掉内存保护。

2.  实现方法

void UBSHcomNetMemoryAllocator::Destroy()

3.  参数说明

无

4.  返回值

无

##### UBSHcomNetMemoryAllocator::GetTargetSeg

1.  函数定义

获得SEG。

2.  实现方法

void \*UBSHcomNetMemoryAllocator::GetTargetSeg()

3.  参数说明

无

4.  返回值

void \*

##### UBSHcomNetMemoryAllocator::SetTargetSeg

1.  函数定义

设置SEG。

2.  实现方法

void UBSHcomNetMemoryAllocator::SetTargetSeg(void \*targetSeg)

3.  参数说明

    1.  参数说明

| 参数名    | 数据类型 | 参数类型 | 描述            |
|-----------|----------|----------|-----------------|
| targetSeg | void \*  | 入参     | 需要设置的SEG。 |

4.  返回值

无

##### UBSHcomNetMemoryAllocatorTypeToString

1.  函数定义

将内存类型转化成字符串。

2.  实现方法

std::string &UBSHcomNetMemoryAllocatorTypeToString(UBSHcomNetMemoryAllocatorType v)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型                      | 参数类型 | 描述       |
|--------|-------------------------------|----------|------------|
| v      | UBSHcomNetMemoryAllocatorType | 入参     | 内存类型。 |

4.  返回值

字符串{"Dynamic size allocator", "Dynamic size allocator with cache","UNKNOWN ALLOCATOR TYPE"}

##### UBSHcomNetDriverProtocolToString

1.  函数定义

将协议类型转化成字符串。

2.  实现方法

std::string &UBSHcomNetDriverProtocolToString(UBSHcomNetDriverProtocol v)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型                 | 参数类型 | 描述       |
|--------|--------------------------|----------|------------|
| v      | UBSHcomNetDriverProtocol | 入参     | 协议类型。 |

4.  返回值

字符串{"RDMA", "TCP", "UDS", "SHM", "RDMA_MLX5", "UB", "UBOE", "UBC", "HSHMEM","UNKNOWN PROTOCOL"}

##### UBSHcomNetDriverSecTypeToString

1.  函数定义

将安全校验类型转化成字符串。

2.  实现方法

std::string &UBSHcomNetDriverSecTypeToString(UBSHcomNetDriverSecType v)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型                | 参数类型 | 描述           |
|--------|-------------------------|----------|----------------|
| v      | UBSHcomNetDriverSecType | 入参     | 安全校验类型。 |

4.  返回值

字符串{"SecNoValid", "SecValidOneWay", "SecValidTwoWay", "UNKNOWN SEC TYPE"}

##### UBSHcomNetDriverOobTypeToString

1.  函数定义

oob类型转化成字符串。

2.  实现方法

std::string &UBSHcomNetDriverOobTypeToString(NetDriverOobType v)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型         | 参数类型 | 描述                 |
|--------|------------------|----------|----------------------|
| v      | NetDriverOobType | 入参     | UBSHcomOobType类型。 |

4.  返回值

字符串{"Tcp", "UDS", "URMA","UNKNOWN OOB TYPE"}

##### UBSHcomNetDriverLBPolicyToString

1.  函数定义

将负载均衡类型转化成字符串。

2.  实现方法

std::string &UBSHcomNetDriverLBPolicyToString(UBSHcomNetDriverLBPolicy v)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型                 | 参数类型 | 描述           |
|--------|--------------------------|----------|----------------|
| v      | UBSHcomNetDriverLBPolicy | 入参     | 负载均衡类型。 |

4.  返回值

字符串{"RR", "Hash","UNKNOWN POLICY" }

##### UBSHcomNEPStateToString

1.  函数定义

将EndPoint状态类型转化成字符串。

2.  实现方法

std::string &UBSHcomNEPStateToString(UBSHcomNetEndPointState v)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型                | 参数类型 | 描述               |
|--------|-------------------------|----------|--------------------|
| v      | UBSHcomNetEndPointState | 入参     | EndPoint状态类型。 |

4.  返回值

字符串{"new", "established", "broken","UNKNOWN EP STATE"}

### C API

#### 服务层API

##### ubs_hcom_service_create

1.  函数定义

根据类型和名字创建一个服务层的NetService对象。

2.  实现方法

int ubs_hcom_service_create(ubs_hcom_service_type t, const char \*name, ubs_hcom_service_options options, ubs_hcom_service \*service);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| t | [ubs_hcom_service_type](#ZH-CN_TOPIC_0000002465536674) | 入参 | ubs_hcom_service协议类型。 |
| name | const char \* | 入参 | ubs_hcom_service的名字。长度范围\[1, 64\]，只能包含数字、字母、‘\_’和‘-’。 |
| options | ubs_hcom_service_options | 入参 | Service配置项。 |
| service | ubs_hcom_service | 出参 | 表示创建的ubs_hcom_service对象，如果创建失败返回空。 |

4.  返回值

返回值为0则表示发送消息成功。

##### ubs_hcom_service_bind

1.  函数定义

根据类型和名字创建一个服务层的NetService对象。

2.  实现方法

int ubs_hcom_service_bind(ubs_hcom_service service, const char \*listenerUrl, ubs_hcom_service_channel_handler h);

3.  参数说明

    1.  参数说明

[TABLE]

1.  返回值

返回值为0则表示bind成功。

##### ubs_hcom_service_start

1.  函数定义

根据类型和名字创建一个服务层的NetService对象。

2.  实现方法

int ubs_hcom_service_start(ubs_hcom_service service);

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型         | 参数类型 | 描述                             |
|---------|------------------|----------|----------------------------------|
| service | ubs_hcom_service | 入参     | 之前创建的ubs_hcom_service对象。 |

4.  返回值

返回值为0则表示创建成功。

##### ubs_hcom_service_destroy

1.  函数定义

销毁服务，会清理全局map根据名字销毁对象。

2.  实现方法

int ubs_hcom_service_destroy(ubs_hcom_service service, const char \*name);

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型      | 参数类型 | 描述                                 |
|---------|---------------|----------|--------------------------------------|
| service | Net_Service   | 入参     | 需要销毁的ubs_hcom_service对象。     |
| name    | const char \* | 入参     | 需要销毁的ubs_hcom_service对象名字。 |

##### ubs_hcom_service_connect

1.  函数定义

建立与远程服务器的连接，并返回连接通道。

2.  实现方法

int ubs_hcom_service_connect(ubs_hcom_service service, const char \*serverUrl, ubs_hcom_channel \*channel, Service_UBSHcomConnectOptions options);

3.  参数说明

    1.  参数说明

[TABLE]

1.  返回值

返回值为0则表示建链成功。

##### ubs_hcom_service_disconnect

1.  函数定义

切断与远程服务器的连接。

2.  实现方法

int ubs_hcom_service_disconnect(ubs_hcom_service service, ubs_hcom_channel channel);

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型    | 参数类型 | 描述                             |
|---------|-------------|----------|----------------------------------|
| service | Net_Service | 入参     | 之前创建的ubs_hcom_service对象。 |
| channel | Net_Channel | 入参     | 建链生成的连接通道NetChannel。   |

4.  返回值

返回值为0则表示断链成功。

##### ubs_hcom_service_register_memory_region

1.  函数定义

注册一个内存区域，内存将在UBS Comm内部分配。

2.  实现方法

int ubs_hcom_service_register_memory_region(ubs_hcom_service service, uint64_t size, ubs_hcom_memory_region \*mr);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| service | ubs_hcom_service | 入参 | 初始化创建的ubs_hcom_service对象。 |
| size | uint64_t | 入参 | 需要注册的内存大小，单位byte。范围为(0, 107374182400\]。 |
| mr | ubs_hcom_memory_region | 入参 | 内存区域结构，包含key、名字、大小、buf等字段。 |

![](media/image8.png)

若需要放入pgTable管理（通过ubs_hcom_service_set_enable_mrcache设置为true，默认不放入），则要求首地址(startAddress)和尾地址(startAddress+size)都需要16字节对齐，因此用户申请的size需要能16整除。

4.  返回值

表示函数执行结果，返回值为0则表示注册成功。

##### ubs_hcom_service_get_memory_region_info

1.  函数定义

获得mr的内容。

2.  实现方法

int ubs_hcom_service_get_memory_region_info(ubs_hcom_memory_region mr, ubs_hcom_mr_info \*info);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| mr | ubs_hcom_memory_region | 入参 | mr对象。 |
| info | [ubs_hcom_mr_info](#ZH-CN_TOPIC_0000002465377214) | 出参 | mr的信息。 |

4.  返回值

表示函数执行结果，返回值为0则表示注册成功。

##### ubs_hcom_service_register_assign_memory_region

1.  函数定义

注册一个内存区域，内存将在UBS Comm外部分配。

2.  实现方法

int ubs_hcom_service_register_assign_memory_region(ubs_hcom_service service, uintptr_t address, uint64_t size, ubs_hcom_memory_region \*mr);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| service | ubs_hcom_service | 入参 | 初始化创建的ubs_hcom_service对象。 |
| address | uintptr_t | 入参 | 外部申请的内存地址。 |
| size | uint64_t | 入参 | 外部申请的内存大小，单位byte。范围为(0, 1099511627776\]。 |
| mr | ubs_hcom_memory_region | 出参 | 内存区域结构，包含key、名字、大小、buf等字段。 |

4.  返回值

表示函数执行结果，返回值为0则表示注册成功。

##### ubs_hcom_service_destroy_memory_region

1.  函数定义

销毁一个内存区域，内存将在UBS Comm内部分配。

2.  实现方法

int ubs_hcom_service_destroy_memory_region(ubs_hcom_service service, ubs_hcom_memory_region mr);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| service | ubs_hcom_service | 入参 | 初始化创建的ubs_hcom_service对象。 |
| mr | ubs_hcom_memory_region | 入参 | 内存区域结构，包含key、名字、大小、buf等字段。 |

4.  返回值

无

##### ubs_hcom_service_register_broken_handler

Service_RegisterChannelHandler

![](media/image9.png)

用户注册的回调函数，不能销毁Service及相关的资源。

1.  函数定义

注册通道Channel的回调函数，以处理通道建链和断连事件。

2.  实现方法

void ubs_hcom_service_register_broken_handler(ubs_hcom_service service, ubs_hcom_service_channel_handler h,

ubs_hcom_service_channel_policy policy, uint64_t usrCtx);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| service | ubs_hcom_service | 入参 | 之前创建的ubs_hcom_service对象。 |
| t | [ubs_hcom_service_handler_type](#ZH-CN_TOPIC_0000002465536462) | 入参 | 句柄的类型。 |
| h | ubs_hcom_service_channel_handler | 入参 | 回调函数句柄。 |
| policy | [ubs_hcom_service_channel_policy](#ZH-CN_TOPIC_0000002465536134) | 入参 | 链路断开时的策略，策略可选。 |
| usrCtx | uint64_t | 入参 | 用户上下文。 |

4.  返回值

uintptr_t，返回内部句柄地址。

##### ubs_hcom_service_register_idle_handler

![](media/image9.png)

用户注册的回调函数，不能销毁Service及相关的资源。

1.  函数定义

设置NetService的worker闲时回调函数。

2.  实现方法

void ubs_hcom_service_register_idle_handler(ubs_hcom_service service, ubs_hcom_service_idle_handler h, uint64_t usrCtx);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| service | ubs_hcom_service | 入参 | ubs_hcom_service对象。 |
| h | ubs_hcom_service_idle_handler | 入参 | worker闲时回调函数。 |
| usrCtx | uint64_t | 入参 | 用户上下文，可以在回调函数中使用。 |

4.  返回值

内部回调函数地址。

![](media/image8.png)

数据类型解释如下：

typedef void (\*ubs_hcom_service_idle_handler)(uint8_t wkrGrpIdx, uint16_t idxInGrp, uint64_t usrCtx).

##### ubs_hcom_service_register_handler

![](media/image9.png)

- 用户注册的回调函数，不能销毁Service及相关的资源。

- 用户需要避免在该回调中死等发送完成事件，应添加超时时间，否则会造成死锁。

- 用户需要尽量避免在该回调中占用过长时间处理业务，以免影响性能。

  1.  函数定义

注册回调函数，以处理通道双边发送完成、单边发送完成、双边收消息事件。

2.  实现方法

void ubs_hcom_service_register_handler(ubs_hcom_service service, ubs_hcom_service_handler_type t, ubs_hcom_service_request_handler h,

uint64_t usrCtx);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| service | ubs_hcom_service | 入参 | 之前创建的ubs_hcom_service对象。 |
| t | ubs_hcom_service_handler_type | 入参 | 句柄的类型。 |
| h | ubs_hcom_service_request_handler | 入参 | 回调函数句柄。 |
| usrCtx | uint64_t | 入参 | 用户上下文。 |

4.  返回值

无

##### ubs_hcom_service_set_enable_mrcache

![](media/image9.png)

用户需要使用RNDV，则需要设置为true。

1.  函数定义

设置RegisterMemoryRegion是否将mr放入pgTable管理。

2.  实现方法

void ubs_hcom_service_set_enable_mrcache(ubs_hcom_service service, bool enableMrCache);

3.  参数说明

    1.  参数说明

| 参数名        | 数据类型         | 参数类型 | 描述                             |
|---------------|------------------|----------|----------------------------------|
| service       | ubs_hcom_service | 入参     | 之前创建的ubs_hcom_service对象。 |
| enableMrCache | bool             | 入参     | mr放入pgTable管理标志位。        |

4.  返回值

无

##### ubs_hcom_channel_refer

1.  函数定义

将此NetChannel增加一次引用计数。

2.  实现方法

void ubs_hcom_channel_refer(Net_Channel channel)

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型    | 参数类型 | 描述                           |
|---------|-------------|----------|--------------------------------|
| channel | Net_Channel | 入参     | 需要增加引用计数的NetChannel。 |

4.  返回值

无

##### ubs_hcom_channel_derefer

1.  函数定义

将此NetChannel减少一次引用计数。

2.  实现方法

void ubs_hcom_channel_derefer(Net_Channel channel)

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型    | 参数类型 | 描述                           |
|---------|-------------|----------|--------------------------------|
| channel | Net_Channel | 入参     | 需要减少引用计数的NetChannel。 |

4.  返回值

无

##### ubs_hcom_channel_send

![](media/image9.png)

- 若使用拆包和rndv的功能，需要通过UBSHcomChannel::SetUBSHcomTwoSideThreshold设置拆包和rndv的阈值。

- 使用rndv，则需要创建service后将UBSHcomService::SetEnableMrCache设置为true（UBSHcomService::RegisterMemoryRegion函数调用前）。

  1.  函数定义

发送双边消息，不需要对端回复。

2.  实现方法

int ubs_hcom_channel_send(ubs_hcom_channel channel, ubs_hcom_channel_request req, ubs_hcom_channel_callback \*cb);

3.  参数说明

    1.  参数说明

[TABLE]

1.  返回值

返回值为0则表示成功。

##### ubs_hcom_channel_call

![](media/image9.png)

- rsp中若address字段填了有效内存地址，则用户回复的信息会被拷贝到该地址上。

- 若address==NULL，则UBS Comm会通过malloc申请内存，但用户需要自行维护该内存的生命周期，在使用完后通过free释放。

- 若使用拆包和rndv的功能，需要通过UBSHcomChannel::SetUBSHcomTwoSideThreshold设置拆包和rndv的阈值。

- 使用rndv，则需要创建service后将UBSHcomService::SetEnableMrCache设置为true（UBSHcomService::RegisterMemoryRegion函数调用前）。

  1.  函数定义

发送双边消息并等待回复，需要对端配合Reply使用。

2.  实现方法

int ubs_hcom_channel_call(ubs_hcom_channel channel, ubs_hcom_channel_request req, ubs_hcom_channel_response \*rsp, ubs_hcom_channel_callback \*cb);

3.  参数说明

    1.  参数说明

[TABLE]

1.  返回值

返回值为0则表示成功。

##### ubs_hcom_channel_reply

1.  函数定义

回复双边消息，接收端配合Call使用

2.  实现方法

int ubs_hcom_channel_reply(ubs_hcom_channel channel, ubs_hcom_channel_request req, ubs_hcom_channel_reply_context ctx, ubs_hcom_channel_callback \*cb);

3.  参数说明

    1.  参数说明

[TABLE]

1.  返回值

返回值为0则表示成功。

##### ubs_hcom_channel_put

1.  数据定义

发送单边写请求。

2.  实现方法

int ubs_hcom_channel_put(ubs_hcom_channel channel, ubs_hcom_oneside_request req, ubs_hcom_channel_callback \*cb);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| channel | ubs_hcom_channel | 入参 | 创建的channel对象。 |
| req | [ubs_hcom_oneside_request](#ubs_hcom_oneside_request) | 入参 | 单边请求。 |
| cb | [ubs_hcom_channel_callback](#channel_callback) | 入参 | 异步请求回调函数。 |

4.  返回值

返回值为0则表示成功。

##### ubs_hcom_channel_get

1.  函数定义

发送单边读请求。

2.  实现方法

int ubs_hcom_channel_get(ubs_hcom_channel channel, ubs_hcom_oneside_request req, ubs_hcom_channel_callback \*cb);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| channel | ubs_hcom_channel | 入参 | 创建的channel对象。 |
| req | [ubs_hcom_oneside_request](#ubs_hcom_oneside_request) | 入参 | 单边请求。 |
| cb | [ubs_hcom_channel_callback](#channel_callback) | 入参 | 异步请求回调函数。 |

4.  返回值

返回值为0则表示成功。

##### ubs_hcom_channel_recv

1.  函数定义

只用于接收RNDV请求。

2.  实现方法

int ubs_hcom_channel_recv(ubs_hcom_channel channel, ubs_hcom_service_context ctx, uintptr_t address, uint32_t size, ubs_hcom_channel_callback \*cb);

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型            | 参数类型 | 描述                |
|---------|---------------------|----------|---------------------|
| channel | ubs_hcom_channel    | 入参     | 创建的channel对象。 |
| ctx     | ubs_hcom_service_context     | 入参     | 上下文。            |
| address | uintptr_t           | 入参     | 接收数据地址。      |
| size    | uint32_t            | 入参     | 接收数据大小。      |
| cb      | ubs_hcom_channel_callback \* | 入参     | 异步请求回调。      |

4.  返回值

返回值为0则表示成功。

##### ubs_hcom_channel_set_flowctl_cfg

1.  函数定义

给此NetChannel设置流控参数。

2.  实现方法

int Channel_ConfigFlowControl(ubs_hcom_channel channel, ubs_hcom_flowctl_opts options)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| channel | ubs_hcom_channel | 入参 | 通信渠道。 |
| options | [ubs_hcom_flowctl_opts](#ZH-CN_TOPIC_0000002465536490) | 入参 | 流控参数。 |

4.  返回值

返回值为0则表示成功。

##### ubs_hcom_channel_set_timeout

1.  函数定义

设置NetChannel的双边超时时间。

2.  实现方法

void Channel_SetTwoSideTimeout(Net_Channel channel, int32_t timeout)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| channel | Net_Channel | 入参 | NetChannel。 |
| timeout | int32_t | 入参 | 超时时间，单位为秒，0为立即超时，负数为永不超时（一般设置为-1）。范围是\[-1, INT16_MAX\]。 |

4.  返回值

无

##### ubs_hcom_channel_set_twoside_threshold

1.  函数定义

设置拆包和rndv的阈值。

2.  实现方法

int ubs_hcom_channel_set_twoside_threshold(ubs_hcom_channel channel, Channel_UBSHcomTwoSideThreshold threshold);

3.  参数说明

    1.  参数说明

| 参数名    | 数据类型                        | 参数类型 | 描述             |
|-----------|---------------------------------|----------|------------------|
| channel   | Net_Channel                     | 入参     | NetChannel。     |
| threshold | Channel_UBSHcomTwoSideThreshold | 入参     | 拆包和rndv阈值。 |

4.  返回值

无

##### Channel_Close

![](media/image9.png)

如果用户实现中需要主动销毁channel，要调用Channel_Close和Channel_Destroy接口。

1.  函数定义

关闭NetChannel。

2.  实现方法

void Channel_Close(Net_Channel channel)

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型    | 参数类型 | 描述         |
|---------|-------------|----------|--------------|
| channel | Net_Channel | 入参     | NetChannel。 |

4.  返回值

无

##### ubs_hcom_channel_get_id

1.  函数定义

获取channelId。

2.  实现方法

int ubs_hcom_channel_get_id(ubs_hcom_channel channel);

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型         | 参数类型 | 描述              |
|---------|------------------|----------|-------------------|
| channel | ubs_hcom_channel | 入参     | 通信channel对象。 |

4.  返回值

返回0为成功。

##### ubs_hcom_context_get_channel

1.  函数定义

通过ctx获得NetChannel。

2.  实现方法

int ubs_hcom_context_get_channel(ubs_hcom_service_context context, ubs_hcom_channel \*channel);

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型                 | 参数类型 | 描述                         |
|---------|--------------------------|----------|------------------------------|
| context | ubs_hcom_service_context | 入参     | 回调函数的参数ctx。          |
| channel | ubs_hcom_channel         | 出参     | 返回得到的ubs_hcom_channel。 |

4.  返回值

返回0为成功。

##### ubs_hcom_context_get_type

1.  函数定义

通过ctx获得操作类型。

2.  实现方法

int ubs_hcom_context_get_type(ubs_hcom_service_context context, ubs_hcom_service_context_type \*type);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| context | ubs_hcom_service_context | 入参 | 回调函数的参数ctx。 |
| type | [ubs_hcom_service_context_type](#ZH-CN_TOPIC_0000002498615725) | 出参 | 返回得到的NetChannel。 |

4.  返回值

返回0为成功。

##### ubs_hcom_context_get_result

1.  函数定义

通过ctx获得操作结果。

2.  实现方法

int ubs_hcom_context_get_result(ubs_hcom_service_context context, int \*result)

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型                 | 参数类型 | 描述                |
|---------|--------------------------|----------|---------------------|
| context | ubs_hcom_service_context | 入参     | 回调函数的参数ctx。 |
| result  | int                      | 出参     | 操作结果。          |

4.  返回值

返回0为成功。

##### ubs_hcom_context_get_rspctx

1.  函数定义

通过ctx获得回复消息所需的rspCtx。

2.  实现方法

int ubs_hcom_context_get_rspctx(ubs_hcom_service_context context, ubs_hcom_channel_reply_context \*rspCtx);

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型                       | 参数类型 | 描述                   |
|---------|--------------------------------|----------|------------------------|
| context | ubs_hcom_service_context       | 入参     | 回调函数的参数ctx。    |
| rspCtx  | ubs_hcom_channel_reply_context | 出参     | 回复消息接口所需参数。 |

4.  返回值

返回0为成功。

##### ubs_hcom_context_get_opcode

1.  函数定义

通过ctx获得OpCode。

2.  实现方法

uint16_t ubs_hcom_context_get_opcode(ubs_hcom_service_context context);

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型                 | 参数类型 | 描述                |
|---------|--------------------------|----------|---------------------|
| context | ubs_hcom_service_context | 入参     | 回调函数的参数ctx。 |
| 返回值  | uint16_t                 | 出参     | OpCode。            |

4.  返回值

返回0为成功。

##### ubs_hcom_context_get_data

1.  函数定义

通过ctx获得接收到的消息。

2.  实现方法

void \*ubs_hcom_context_get_data(ubs_hcom_service_context context)

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型                 | 参数类型 | 描述                |
|---------|--------------------------|----------|---------------------|
| context | ubs_hcom_service_context | 入参     | 回调函数的参数ctx。 |

4.  返回值

返回接收到的消息。

##### ubs_hcom_context_get_datalen

1.  函数定义

通过ctx获得接收到的消息长度。

2.  实现方法

uint32_t ubs_hcom_context_get_datalen(ubs_hcom_service_context context)

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型                 | 参数类型 | 描述                |
|---------|--------------------------|----------|---------------------|
| context | ubs_hcom_service_context | 入参     | 回调函数的参数ctx。 |

4.  返回值

返回接收到的消息长度。

#### 传输层API

##### ubs_hcom_driver_create

1.  函数定义

根据类型和名字创建一个传输层的HcomDriver对象。

2.  实现方法

int ubs_hcom_driver_create(ubs_hcom_driver_type t, const char \*name, uint8_t startOobSvr, ubs_hcom_driver \*driver)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| t | [ubs_hcom_driver_type](#ubs_hcom_driver_type) | 入参 | ubs_hcom_driver协议类型，取值范围详见[ubs_hcom_driver_type](#ubs_hcom_driver_type)。 |
| name | char \* | 入参 | ubs_hcom_driver的名字。长度范围\[1, 100\]，只能包含数字、字母、‘\_’和‘-’。 |
| startOobSvr | uint8_t | 入参 | Server端设置为0，Client端设置为1。 |
| driver | ubs_hcom_driver | 出参 | 创建的ubs_hcom_driver实例。 |

4.  返回值

返回值为0则表示创建HcomDriver成功。

##### ubs_hcom_driver_set_ipport

1.  函数定义

给HcomDriver对象设置OOB的IP和Port。

2.  实现方法

void ubs_hcom_driver_set_ipport(ubs_hcom_driver driver, const char \*ip, uint16_t port)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| driver | ubs_hcom_driver | 入参 | ubs_hcom_driver对象。 |
| ip | const char \* | 入参 | IP。该参数内部有系统函数对IP有效性进行校验。 |
| port | uint16_t | 入参 | 端口。范围值\[1024, 65535\]。 |

4.  返回值

无

##### ubs_hcom_driver_get_ipport

1.  函数定义

得到HcomDriver对象的OOB的IP和Port。

2.  实现方法

bool ubs_hcom_driver_get_ipport(ubs_hcom_driver driver, char \*\*\*ipArray, uint16_t \*\*portArray, int \*length)

3.  参数说明

    1.  参数说明

| 参数名    | 数据类型        | 参数类型 | 描述                  |
|-----------|-----------------|----------|-----------------------|
| driver    | ubs_hcom_driver | 入参     | ubs_hcom_driver对象。 |
| ipArray   | char \*\*       | 出参     | OOB的IP数组。         |
| portArray | uint16_t \*     | 出参     | OOB的端口数组。       |
| length    | int             | 出参     | 数组长度。            |

4.  返回值

返回值为true则表示成功。

![](media/image8.png)

出参ipArray和portArray为内部分配的内存，用户需要在使用完成之后自行释放此内存。

##### ubs_hcom_driver_set_udsname

1.  函数定义

给HcomDriver对象设置的OOB type为UDS时的name。

2.  实现方法

void ubs_hcom_driver_set_udsname(ubs_hcom_driver driver, const char \*name)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型        | 参数类型 | 描述                                |
|--------|-----------------|----------|-------------------------------------|
| driver | ubs_hcom_driver | 入参     | ubs_hcom_driver对象。               |
| name   | const char \*   | 入参     | 需要设置的name。长度范围是(0, 96)。 |

4.  返回值

无

##### ubs_hcom_driver_add_uds_opt

1.  函数定义

给HcomDriver对象设置的OOB type为UDS时的name和一些参数。

2.  实现方法

void ubs_hcom_driver_add_uds_opt(ubs_hcom_driver driver, ubs_hcom_driver_uds_listen_opts option)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| driver | ubs_hcom_driver | 入参 | ubs_hcom_driver对象。 |
| option | [ubs_hcom_driver_listen_opts](#ubs_hcom_driver_listen_opts) | 入参 | 需要设置的UDS参数。 |

4.  返回值

无

##### ubs_hcom_driver_add_oob_opt

1.  函数定义

设置HcomDriver对象的OOB的IP和Port。

2.  实现方法

void ubs_hcom_driver_add_oob_opt(ubs_hcom_driver driver, ubs_hcom_driver_listen_opts options)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| driver | ubs_hcom_driver | 入参 | ubs_hcom_driver对象。 |
| options | [ubs_hcom_driver_listen_opts](#ubs_hcom_driver_listen_opts) | 出参 | 需要设置的IP和Port参数。 |

4.  返回值

无

##### ubs_hcom_driver_initizalize

1.  函数定义

根据类型和名字创建一个传输层的HcomDriver对象。

2.  实现方法

int ubs_hcom_driver_initizalize(ubs_hcom_driver driver, ubs_hcom_driver_opts options)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| driver | ubs_hcom_driver | 入参 | 需要初始化的ubs_hcom_driver。 |
| options | [ubs_hcom_driver_opts](#ubs_hcom_driver_opts) | 入参 | 根据Option，初始化ubs_hcom_driver。 |

4.  返回值

返回值为0则表示初始化HcomDriver成功。

##### ubs_hcom_driver_start

1.  函数描述

根据类型和名字创建一个传输层的HcomDriver对象。

2.  函数定义

int ubs_hcom_driver_start(ubs_hcom_driver driver)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型        | 参数类型 | 描述                        |
|--------|-----------------|----------|-----------------------------|
| driver | ubs_hcom_driver | 入参     | 需要开启的ubs_hcom_driver。 |

4.  返回值

返回值为0则表示开启HcomDriver成功。

##### ubs_hcom_driver_connect

![](media/image9.png)

如果用户实现中需要主动销毁EP，要先调用ubs_hcom_ep_close接口；如果需要减少EP的引用计数，可调用ubs_hcom_ep_destroy函数。

1.  函数定义

建立与远程服务器的连接，并返回连接创建的EP。

2.  实现方法

- int ubs_hcom_driver_connect(ubs_hcom_driver driver, const char \*payloadData, ubs_hcom_endpoint \*ep, uint32_t flags)

- int ubs_hcom_driver_connect_with_grpno(ubs_hcom_driver driver, const char \*payloadData, ubs_hcom_endpoint \*ep, uint32_t flags, uint8_t serverGrpNo, uint8_t clientGrpNo)

- int ubs_hcom_driver_connect_to_ipport(ubs_hcom_driver driver, const char \*serverIp, uint16_t serverPort, const char \*payloadData, ubs_hcom_endpoint \*ep, uint32_t flags)

- int ubs_hcom_driver_connect_to_ipport_with_grpno(ubs_hcom_driver driver, const char \*serverIp, uint16_t serverPort, const char \*payloadData, ubs_hcom_endpoint \*ep, uint32_t flags, uint8_t serverGrpNo, uint8_t clientGrpNo)

- int ubs_hcom_driver_connect_to_ipport_with_ctx(ubs_hcom_driver driver, const char \*serverIp, uint16_t serverPort, const char \*payloadData, ubs_hcom_endpoint \*ep, uint32_t flags, uint8_t serverGrpNo, uint8_t clientGrpNo, uint64_t ctx)

  1.  参数说明

      1.  参数说明

[TABLE]

2.  返回值

返回值为0则表示连接成功。

##### ubs_hcom_driver_stop

1.  函数定义

停止服务和内部启动的线程。

2.  实现方法

void ubs_hcom_driver_stop(ubs_hcom_driver driver)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型        | 参数类型 | 描述                        |
|--------|-----------------|----------|-----------------------------|
| driver | ubs_hcom_driver | 入参     | 需要停止的ubs_hcom_driver。 |

4.  返回值

无

##### ubs_hcom_driver_uninitialize

1.  函数定义

清理服务创建时的相关资源。

2.  实现方法

void ubs_hcom_driver_uninitialize(ubs_hcom_driver driver)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型        | 参数类型 | 描述                        |
|--------|-----------------|----------|-----------------------------|
| driver | ubs_hcom_driver | 入参     | 需要清理的ubs_hcom_driver。 |

4.  返回值

无

##### ubs_hcom_driver_destroy

1.  函数定义

销毁HcomDriver。

2.  实现方法

void ubs_hcom_driver_destroy(ubs_hcom_driver driver)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型        | 参数类型 | 描述                        |
|--------|-----------------|----------|-----------------------------|
| driver | ubs_hcom_driver | 入参     | 需要销毁的ubs_hcom_driver。 |

4.  返回值

无

##### ubs_hcom_driver_register_ep_handler

![](media/image9.png)

用户实现的回调函数，内部不能销毁driver及相关的资源。

1.  函数定义

注册EP的回调函数，以处理EP建链和断连事件。并把回调函数句柄放入全局句柄管理器。

2.  实现方法

uintptr_t ubs_hcom_driver_register_ep_handler(ubs_hcom_driver driver, ubs_hcom_ep_handler_type t, ubs_hcom_ep_handler h, uint64_t usrCtx)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| driver | ubs_hcom_driver | 入参 | 需要注册回调函数的ubs_hcom_driver。 |
| t | [Net_EPHandlerType](#ZH-CN_TOPIC_0000002498495477) | 入参 | 句柄的类型。 |
| h | ubs_hcom_ep_handler | 入参 | 回调函数的句柄。 |
| usrCtx | uint64 | 入参 | 用户上下文。 |

4.  返回值

uintptr_t类型，返回内部句柄地址。

##### ubs_hcom_driver_register_op_handler

![](media/image9.png)

用户实现的回调函数，内部不能销毁driver及相关的资源。

1.  函数定义

注册回调函数，以处理通道双边发送完成、单边发送完成、双边收消息事件。并把回调函数句柄放入全局句柄管理器。

2.  实现方法

uintptr_t ubs_hcom_driver_register_op_handler(ubs_hcom_driver driver, ubs_hcom_op_handler_type t, ubs_hcom_request_handler h, uint64_t usrCtx)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| driver | ubs_hcom_driver | 入参 | 需要注册回调函数的ubs_hcom_driver。 |
| t | [ubs_hcom_op_handler_type](#ubs_hcom_op_handler_type) | 入参 | 句柄的类型。 |
| h | Net_RequestHandler | 入参 | 回调函数的句柄。 |
| usrCtx | uint64_t | 入参 | 用户上下文。 |

4.  返回值

uintptr_t类型，返回内部句柄地址。

##### ubs_hcom_driver_register_idle_handler

![](media/image9.png)

用户实现的回调函数，内部不能销毁driver及相关的资源。

1.  函数定义

给HcomDriver对象设置EP闲时回调函数。并把回调函数句柄放入全局句柄管理器。

2.  实现方法

uintptr_t ubs_hcom_driver_register_idle_handler(ubs_hcom_driver driver, ubs_hcom_idle_handler h, uint64_t usrCtx)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型              | 参数类型 | 描述                  |
|--------|-----------------------|----------|-----------------------|
| driver | ubs_hcom_driver       | 入参     | ubs_hcom_driver对象。 |
| h      | ubs_hcom_idle_handler | 入参     | 闲时回调函数。        |
| usrCtx | uint64_t              | 入参     | 带到回调函数中的ctx。 |

4.  返回值

内部回调函数地址。

![](media/image8.png)

数据类型解释如下：

typedef void (\*ubs_hcom_idle_handler)(uint8_t wkrGrpIdx, uint16_t idxInGrp, uint64_t usrCtx)

##### ubs_hcom_driver_register_secinfo_provider

![](media/image9.png)

用户实现的回调函数，内部不能销毁driver及相关的资源。

1.  函数定义

给HcomDriver对象设置EP安全信息提供函数。

2.  实现方法

uintptr_t ubs_hcom_driver_register_secinfo_provider(ubs_hcom_driver driver, ubs_hcom_secinfo_provider provider)

3.  参数说明

    1.  参数说明

| 参数名   | 数据类型                  | 参数类型 | 描述                  |
|----------|---------------------------|----------|-----------------------|
| driver   | ubs_hcom_driver           | 入参     | ubs_hcom_driver对象。 |
| provider | ubs_hcom_secinfo_provider | 入参     | 安全信息提供函数。    |

4.  返回值

内部回调函数地址。

![](media/image8.png)

数据类型解释如下：

typedef int (\*ubs_hcom_secinfo_provider)(uint64_t ctx, int64_t \*flag, ubs_hcom_driver_sec_type \*type, char \*\*output, uint32_t \*outLen, int \*needAutoFree)

##### ubs_hcom_driver_register_secinfo_validator

![](media/image9.png)

用户实现的回调函数，内部不能销毁driver及相关的资源。

1.  函数定义

给HcomDriver对象设置EP安全信息校验函数。

2.  实现方法

uintptr_t ubs_hcom_driver_register_secinfo_validator(ubs_hcom_driver driver, ubs_hcom_secinfo_validator validator)

3.  参数说明

    1.  参数说明

| 参数名    | 数据类型                   | 参数类型 | 描述                  |
|-----------|----------------------------|----------|-----------------------|
| driver    | ubs_hcom_driver            | 入参     | ubs_hcom_driver对象。 |
| validator | ubs_hcom_secinfo_validator | 入参     | 安全信息校验函数。    |

4.  返回值

内部回调函数地址。

![](media/image8.png)

数据类型解释如下：

typedef int (\*ubs_hcom_secinfo_validator)(uint64_t ctx, int64_t flag, const char \*input, uint32_t inputLen)

##### ubs_hcom_driver_unregister_ep_handler

1.  函数定义

从全局回调函数句柄管理器中去掉某一个回调函数句柄。

2.  实现方法

void ubs_hcom_driver_unregister_ep_handler(ubs_hcom_ep_handler_type t, uintptr_t handle)

3.  参数说明

    1.  参数说明

[TABLE]

1.  返回值

无

##### ubs_hcom_driver_unregister_op_handler

1.  函数定义

从全局回调函数句柄管理器中去掉某一个回调函数句柄。

2.  实现方法

void ubs_hcom_driver_unregister_op_handler(ubs_hcom_op_handler_type t, uintptr_t handle)

3.  参数说明

    1.  参数说明

[TABLE]

1.  返回值

无

##### ubs_hcom_driver_unregister_idle_handler

1.  函数定义

从全局回调函数句柄管理器中去掉某一个回调函数句柄。

2.  实现方法

void ubs_hcom_driver_unregister_idle_handler(uintptr_t handle)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型  | 参数类型 | 描述           |
|--------|-----------|----------|----------------|
| handle | uintptr_t | 入参     | 回调函数句柄。 |

4.  返回值

无

##### ubs_hcom_driver_create_memory_region

1.  函数定义

通过HcomDriver对象来创建一个Memory region。

2.  实现方法

int ubs_hcom_driver_create_memory_region(ubs_hcom_driver driver, uint64_t size, ubs_hcom_memory_region \*mr)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型               | 参数类型 | 描述                                 |
|--------|------------------------|----------|--------------------------------------|
| driver | ubs_hcom_driver        | 入参     | ubs_hcom_driver对象。                |
| size   | uint64_t               | 入参     | MR的大小。范围为(0, 107374182400\]。 |
| mr     | ubs_hcom_memory_region | 出参     | 创建的MR。                           |

4.  返回值

返回0为成功。

##### ubs_hcom_driver_create_assign_memory_region

1.  函数定义

通过HcomDriver对象来创建一个Memory region。

2.  实现方法

int ubs_hcom_driver_create_assign_memory_region(ubs_hcom_driver driver, uintptr_t address, uint64_t size, ubs_hcom_memory_region \*mr)

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型               | 参数类型 | 描述                  |
|---------|------------------------|----------|-----------------------|
| driver  | ubs_hcom_driver        | 入参     | ubs_hcom_driver对象。 |
| address | uintptr_t              | 入参     | 内存地址。            |
| size    | uint64_t               | 入参     | 内存的大小。          |
| mr      | ubs_hcom_memory_region | 出参     | 创建的MR。            |

4.  返回值

返回0为成功。

##### ubs_hcom_driver_destroy_memory_region

1.  函数定义

通过HcomDriver对象来销毁一个Memory region。

2.  实现方法

void ubs_hcom_driver_destroy_memory_region(ubs_hcom_driver driver, ubs_hcom_memory_region mr)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型               | 参数类型 | 描述                  |
|--------|------------------------|----------|-----------------------|
| driver | ubs_hcom_driver        | 入参     | ubs_hcom_driver对象。 |
| mr     | ubs_hcom_memory_region | 入参     | 需要销毁的MR。        |

4.  返回值

无

##### ubs_hcom_driver_get_memory_region_info

1.  函数定义

获取一个MR的信息。

2.  实现方法

int ubs_hcom_driver_get_memory_region_info(ubs_hcom_memory_region mr, ubs_hcom_memory_region_info \*info)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| mr | ubs_hcom_memory_region | 入参 | 创建的MR。 |
| info | [ubs_hcom_memory_region_info](#ubs_hcom_memory_region_info) | 出参 | MR相关的信息。 |

4.  返回值

返回0为成功。

##### ubs_hcom_ep_set_context

1.  函数定义

给EP设置本端回调函数可使用的ctx。

2.  实现方法

void ubs_hcom_ep_set_context(ubs_hcom_endpoint ep, uint64_t ctx)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型          | 参数类型 | 描述            |
|--------|-------------------|----------|-----------------|
| ep     | ubs_hcom_endpoint | 入参     | EndPoint。      |
| ctx    | uint64_t          | 入参     | 设置的context。 |

4.  返回值

无

##### ubs_hcom_ep_get_context

1.  函数定义

获得EP的ctx。

2.  实现方法

uint64_t ubs_hcom_ep_get_context(ubs_hcom_endpoint ep)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型          | 参数类型 | 描述       |
|--------|-------------------|----------|------------|
| ep     | ubs_hcom_endpoint | 入参     | EndPoint。 |

4.  返回值

EP的context。

##### ubs_hcom_ep_get_worker_idx

1.  函数定义

获取EP所在的worker group的worker索引。

2.  实现方法

uint16_t ubs_hcom_ep_get_worker_idx(ubs_hcom_endpoint ep)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型          | 参数类型 | 描述       |
|--------|-------------------|----------|------------|
| ep     | ubs_hcom_endpoint | 入参     | EndPoint。 |

4.  返回值

返回worker group的worker索引。

##### ubs_hcom_ep_get_workergroup_idx

1.  函数定义

获取EP所在的worker group索引。

2.  实现方法

uint8_t ubs_hcom_ep_get_workergroup_idx(ubs_hcom_endpoint ep)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型          | 参数类型 | 描述       |
|--------|-------------------|----------|------------|
| ep     | ubs_hcom_endpoint | 入参     | EndPoint。 |

4.  返回值

返回worker group索引。

##### ubs_hcom_ep_get_listen_port

1.  函数定义

获取EP建链时所监听的端口号。

2.  实现方法

uint32_t ubs_hcom_ep_get_listen_port(ubs_hcom_endpoint ep)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型          | 参数类型 | 描述       |
|--------|-------------------|----------|------------|
| ep     | ubs_hcom_endpoint | 入参     | EndPoint。 |

4.  返回值

返回端口号。

##### ubs_hcom_ep_version

1.  函数定义

获取EP的版本。

2.  实现方法

uint8_t ubs_hcom_ep_version(ubs_hcom_endpoint ep)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型          | 参数类型 | 描述       |
|--------|-------------------|----------|------------|
| ep     | ubs_hcom_endpoint | 入参     | EndPoint。 |

4.  返回值

返回EP的版本。

##### ubs_hcom_ep_set_timeout

1.  函数定义

设置EP的超时时间。

2.  实现方法

void ubs_hcom_ep_set_timeout(ubs_hcom_endpoint ep, int32_t timeout)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| ep | ubs_hcom_endpoint | 入参 | EndPoint。 |
| timeout | int32_t | 入参 | 超时时间，单位是秒。0为立刻超时，负数为永不超时。 |

4.  返回值

无

##### ubs_hcom_ep_post_send

1.  函数定义

向对端发送一个带有op信息的请求。

2.  实现方法

int ubs_hcom_ep_post_send(ubs_hcom_endpoint ep, uint16_t opcode, ubs_hcom_send_request \*req)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| ep | ubs_hcom_endpoint | 入参 | 建链创建好的EP对象。 |
| opcode | uint16_t | 入参 | 操作码，取值范围\[0, 1023\]。 |
| \*req | [ubs_hcom_send_request](#ubs_hcom_send_request) | 入参 | 发送请求信息，使用本地内存来存储数据，数据会被复制，调用后可释放本地内存。 |

4.  返回值

返回值为0则表示发送成功。

##### ubs_hcom_ep_post_send_with_opinfo

1.  函数定义

使用EP发送PostSend消息，带有opInfo。

2.  实现方法

int ubs_hcom_ep_post_send_with_opinfo(ubs_hcom_endpoint ep, uint16_t opcode, ubs_hcom_send_request \*req, ubs_hcom_opinfo \*opInfo)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| ep | ubs_hcom_endpoint | 入参 | EndPoint。 |
| opcode | uint16_t | 入参 | 操作编号。 |
| req | [ubs_hcom_send_request](#ubs_hcom_send_request) | 入参 | 需要发送的消息。 |
| opInfo | [ubs_hcom_opinfo](#ubs_hcom_opinfo) | 入参 | 操作信息。 |

4.  返回值

返回0为成功。

##### ubs_hcom_ep_post_send_with_seqno

1.  函数定义

使用EP发送PostSend消息，带有seqNo。

2.  实现方法

int ubs_hcom_ep_post_send_with_seqno(ubs_hcom_endpoint ep, uint16_t opcode, ubs_hcom_send_request \*req, uint32_t replySeqNo)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| ep | ubs_hcom_endpoint | 入参 | EndPoint。 |
| opcode | uint16_t | 入参 | 操作编号。 |
| req | [ubs_hcom_send_request](#ubs_hcom_send_request) | 入参 | 需要发送的消息。 |
| replySeqNo | uint32_t | 入参 | 序列号。 |

4.  返回值

返回0为成功。

##### ubs_hcom_ep_post_read

1.  函数定义

向对端发送一个读请求。

2.  实现方法

int ubs_hcom_ep_post_read(ubs_hcom_endpoint ep, ubs_hcom_readwrite_request \*req)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| ep | ubs_hcom_endpoint | 入参 | 建链创建好的EP对象。 |
| \*req | [ubs_hcom_readwrite_request](#ubs_hcom_readwrite_request) | 入参 | 读请求信息。 |

4.  返回值

返回值为0则表示读成功。

##### ubs_hcom_ep_post_write

1.  函数定义

向对端发送一个写请求。

2.  实现方法

int ubs_hcom_ep_post_write(ubs_hcom_endpoint ep, ubs_hcom_readwrite_request \*req)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| ep | ubs_hcom_endpoint | 入参 | 建链创建好的EP对象。 |
| \*req | [ubs_hcom_readwrite_request](#ubs_hcom_readwrite_request) | 入参 | 写请求信息。 |

4.  返回值

返回值为0则表示写成功。

##### ubs_hcom_ep_wait_completion

1.  函数定义

等待send，read，write消息完成，只有在EP是NET_EP_SELF_POLLING时生效。

2.  实现方法

int ubs_hcom_ep_wait_completion(ubs_hcom_endpoint ep, int32_t timeout)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| ep | ubs_hcom_endpoint | 入参 | EndPoint。 |
| timeout | int32_t | 入参 | 超时时间，单位是秒。0为立刻超时，负数为永不超时。 |

4.  返回值

返回值为0则表示成功。

##### ubs_hcom_ep_receive

1.  函数定义

接收对端发送过来的消息。

2.  实现方法

int ubs_hcom_ep_receive(ubs_hcom_endpoint ep, int32_t timeout, ubs_hcom_response_context \*\*ctx)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| ep | ubs_hcom_endpoint | 入参 | EndPoint。 |
| timeout | int32_t | 入参 | 超时时间，单位是秒。0为立刻超时，负数为永不超时。 |
| ctx | [ubs_hcom_response_context](#ubs_hcom_response_context) | 出参 | 接收到的消息。 |

4.  返回值

返回值为0则表示成功。

##### ubs_hcom_ep_refer

1.  函数定义

给EP增加一次引用。

2.  实现方法

void ubs_hcom_ep_refer(ubs_hcom_endpoint ep)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型          | 参数类型 | 描述       |
|--------|-------------------|----------|------------|
| ep     | ubs_hcom_endpoint | 入参     | EndPoint。 |

4.  返回值

无

##### ubs_hcom_ep_close

![](media/image9.png)

如果用户实现中需要主动销毁EP，要先调用ubs_hcom_ep_close接口；如果需要减少EP的引用计数，可调用ubs_hcom_ep_destroy函数。

1.  函数定义

关闭EP。

2.  实现方法

void ubs_hcom_ep_close(ubs_hcom_endpoint ep)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型          | 参数类型 | 描述       |
|--------|-------------------|----------|------------|
| ep     | ubs_hcom_endpoint | 入参     | EndPoint。 |

4.  返回值

无

##### ubs_hcom_ep_destroy

![](media/image9.png)

如果用户实现中需要主动销毁EP，要先调用ubs_hcom_ep_close接口；如果需要减少EP的引用计数，可调用ubs_hcom_ep_destroy函数。

1.  函数定义

销毁EP。

2.  实现方法

void ubs_hcom_ep_destroy(ubs_hcom_endpoint ep)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型          | 参数类型 | 描述       |
|--------|-------------------|----------|------------|
| ep     | ubs_hcom_endpoint | 入参     | EndPoint。 |

4.  返回值

无

##### ubs_hcom_err_str

1.  函数定义

得到errorCode的解释。

2.  实现方法

const char \*ubs_hcom_err_str(int16_t errCode)

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型 | 参数类型 | 描述               |
|---------|----------|----------|--------------------|
| errCode | int16_t  | 入参     | 需要翻译的错误码。 |

4.  返回值

返回错误码翻译。

##### ubs_hcom_mem_allocator_create

1.  函数定义

创建一个内存分配器。

2.  实现方法

int ubs_hcom_mem_allocator_create(ubs_hcom_memory_allocator_type t, ubs_hcom_memory_allocator_options \*options, ubs_hcom_memory_allocator \*allocator)

3.  参数说明\`

    1.  参数说明

[TABLE]

1.  返回值

返回值为0则表示成功。

##### ubs_hcom_mem_allocator_destroy

1.  函数定义

销毁一个内存分配器。

2.  实现方法

int ubs_hcom_mem_allocator_destroy(ubs_hcom_memory_allocator allocator)

3.  参数说明

    1.  参数说明

| 参数名    | 数据类型                  | 参数类型 | 描述                   |
|-----------|---------------------------|----------|------------------------|
| allocator | ubs_hcom_memory_allocator | 入参     | 需要销毁的内存分配器。 |

4.  返回值

返回值为0则表示成功。

##### ubs_hcom_mem_allocator_set_mr_key

1.  函数定义

给分配器设置memory region key。

2.  实现方法

int ubs_hcom_mem_allocator_set_mr_key(ubs_hcom_memory_allocator allocator, uint32_t mrKey)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| allocator | ubs_hcom_memory_allocator | 入参 | 需要被设置的内存分配器。 |
| mrKey | uint32_t | 入参 | memory region key。范围值(0, UINT32_MAX\]。 |

4.  返回值

返回值为0则表示成功。

##### ubs_hcom_mem_allocator_get_offset

1.  函数定义

得到地址在分配器内存的偏移值。

2.  实现方法

int ubs_hcom_mem_allocator_get_offset(ubs_hcom_memory_allocator allocator, uintptr_t address, uintptr_t \*offset)

3.  参数说明

    1.  参数说明

| 参数名    | 数据类型                  | 参数类型 | 描述         |
|-----------|---------------------------|----------|--------------|
| allocator | ubs_hcom_memory_allocator | 入参     | 内存分配器。 |
| address   | uintptr_t                 | 入参     | 内存地址。   |
| offset    | uintptr_t                 | 出参     | 偏移值。     |

4.  返回值

返回0为成功。

##### ubs_hcom_mem_allocator_get_free_size

1.  函数定义

得到分配器剩余的内存大小。

2.  实现方法

int ubs_hcom_mem_allocator_get_free_size(ubs_hcom_memory_allocator allocator, uintptr_t \*size)

3.  参数说明

    1.  参数说明

| 参数名    | 数据类型                  | 参数类型 | 描述             |
|-----------|---------------------------|----------|------------------|
| allocator | ubs_hcom_memory_allocator | 入参     | 内存分配器。     |
| size      | uintptr_t                 | 出参     | 剩余的内存大小。 |

4.  返回值

返回0为成功。

##### ubs_hcom_mem_allocator_allocate

1.  函数定义

从内存分配器中分配出指定大小的内存。

2.  实现方法

int ubs_hcom_mem_allocator_allocate(ubs_hcom_memory_allocator allocator, uint64_t size, uintptr_t \*address, uint32_t \*key)

3.  参数说明

    1.  参数说明

| 参数名    | 数据类型                  | 参数类型 | 描述             |
|-----------|---------------------------|----------|------------------|
| allocator | ubs_hcom_memory_allocator | 入参     | 内存分配器。     |
| size      | uint64_t                  | 入参     | 分配的内存大小。 |
| address   | uintptr_t                 | 出参     | 分配的内存地址。 |
| key       | uint32_t                  | 出参     | MR Key。         |

4.  返回值

返回值为0则表示成功。

##### ubs_hcom_mem_allocator_free

1.  函数定义

将从内存分配器中分配的内存释放给分配器。

2.  实现方法

int ubs_hcom_mem_allocator_free(ubs_hcom_memory_allocator allocator, uintptr_t address)

![](media/image8.png)

使用时防止相同address多次调用该函数。

3.  参数说明

    1.  参数说明

| 参数名    | 数据类型                  | 参数类型 | 描述                 |
|-----------|---------------------------|----------|----------------------|
| allocator | ubs_hcom_memory_allocator | 入参     | 内存分配器。         |
| address   | uintptr_t                 | 入参     | 需要释放的内存地址。 |

4.  返回值

返回值为0则表示成功。

##### ubs_hcom_set_log_handler

1.  函数定义

设置外部日志。

2.  实现方法

void ubs_hcom_set_log_handler(ubs_hcom_log_handler h)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型             | 参数类型 | 描述           |
|--------|----------------------|----------|----------------|
| h      | ubs_hcom_log_handler | 入参     | 外部日志函数。 |

4.  返回值

无

![](media/image8.png)

数据类型解释如下：

typedef void (\*ubs_hcom_log_handler)(int level, const char \*msg)

##### ubs_hcom_check_local_supporr

1.  函数定义

校验本机是否支持所提供协议，若为RDMA协议且支持的情况下，会返回设备信息。

2.  实现方法

int ubs_hcom_check_local_supporr(ubs_hcom_driver_type t, ubs_hcom_device_info \*info)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| t | ubs_hcom_driver_type | 入参 | 需要校验的协议。 |
| info | [ubs_hcom_device_info](#ubs_hcom_device_info) | 出参 | RDMA设备信息，最大的SGL的iov count。 |

4.  返回值

返回值为1则表示支持此协议。

##### ubs_hcom_get_remote_uds_info

1.  函数定义

仅支持服务端且OOB type为UDS时，查询此EP的对端UDS ID信息。

2.  实现方法

int ubs_hcom_get_remote_uds_info(ubs_hcom_endpoint ep, ubs_hcom_uds_id_info \*idInfo)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| ep | ubs_hcom_endpoint | 入参 | EndPoint。 |
| idInfo | [ubs_hcom_uds_id_info](#ubs_hcom_uds_id_info) | 出参 | 对端UDS ID信息。 |

4.  返回值

返回值为0则表示成功。

## 高级API参考

### C++API

#### 服务层

##### UBSHcomService::AddWorkerGroup

1.  函数定义

向Service中增加内存池。

2.  实现说明

void UBSHcomService::AddWorkerGroup(uint16_t workerGroupId, uint32_t threadCount,const std::pair\<uint32_t, uint32_t\> &cpuIdsRange, int8_t priority = 0, uint16_t multirailIdx = 0);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| workerGroupId | uint16_t | 入参 | 增加的workerGroup的编号ID，用于标识不同的workerGroup。 |
| threadCount | uint32_t | 入参 | 该workerGroup中的线程数。 |
| cpuIdsRange | const std::pair\<uint32_t, uint32_t\> | 入参 | 该workerGroup绑定的cpu范围，如{0, 10}表示绑定在0到10号CPU上。 |
| priority | int8_t | 入参 | 线程优先级，同线程nice值，范围\[-20, 19\]，取值越大优先级越低。 |
| multirailIdx | uint16_t | 入参 | 该workerGroup绑定的MultiRail索引序号。 |

4.  返回值

无

##### UBSHcomService::AddListener

1.  函数定义

向Service中增加listener。

2.  实现说明

void UBSHcomService::AddListener(const std::string &url, uint16_t workerCount = UINT16_MAX);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| url | const std::string | 入参 | 增加的listener监听的url，同bind。 |
| workerCount | uint16_t | 入参 | 从workerGroup中选取workerCount个线程，与该url建立的连接请求通过这workerCount个线程去处理。 |

4.  返回值

无

##### UBSHcomService::SetConnectLBPolicy

1.  函数定义

设置建链负载均衡策略

2.  实现说明

void UBSHcomService::SetConnectLBPolicy(UBSHcomServiceLBPolicy lbPolicy)

3.  参数说明

    1.  参数说明

[TABLE]

1.  返回值

无

##### UBSHcomService::SetUBSHcomTlsOptions

1.  函数定义

设置TLS可选配置项。

2.  实现说明

void UBSHcomService::SetUBSHcomTlsOptions(const UBSHcomTlsOptions &opt);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型                                | 参数类型 | 描述            |
|--------|-----------------------------------------|----------|-----------------|
| opt    | [UBSHcomTlsOptions](#ubshcomtlsoptions) | 入参     | TLS可选配置项。 |

![](media/image8.png)

使用UB自举建链时，暂不支持安全认证和安全加密。

4.  返回值

无

##### UBSHcomService::SetConnSecureOpt

1.  函数定义

链接安全配置项

2.  实现说明

void UBSHcomService::SetConnSecureOpt(const UBSHcomConnSecureOptions &opt);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型                         | 参数类型 | 描述                 |
|--------|----------------------------------|----------|----------------------|
| opt    | const UBSHcomConnSecureOptions & | 入参     | 链接安全可选配置项。 |

4.  返回值

无

##### UBSHcomService::SetTcpUserTimeOutSec

1.  函数定义

设置TCP套接字选项TCP_USER_TIMEOUT。

2.  实现说明

void UBSHcomService::SetTcpUserTimeOutSec(uint16_t timeOutSec);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| timeOutSec | uint16_t | 入参 | 对应TCP_USER_TIMEOUT套接字选项，范围\[0, 1024\]，0表示永不超时。 |

4.  返回值

无

##### UBSHcomService::SetTcpSendZCopy

1.  函数定义

设置TCP发送是否要做内存拷贝。

2.  实现说明

void UBSHcomService::SetTcpSendZCopy(bool tcpSendZCopy);

3.  参数说明

    1.  参数说明

[TABLE]

1.  返回值

无

##### UBSHcomService::SetDeviceIpMask

1.  函数定义

设置设备ipMask，用于rdma/ub。

2.  实现说明

void UBSHcomService::SetDeviceIpMask(const std::vector\<std::string\> &ipMasks);

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型                         | 参数类型 | 描述                   |
|---------|----------------------------------|----------|------------------------|
| ipMasks | const std::vector\<std::string\> | 入参     | 用于过滤的ipMask集合。 |

4.  返回值

无

##### UBSHcomService::SetDeviceIpGroups

1.  函数定义

设置设备ipGroup。

2.  实现说明

void UBSHcomService::SetDeviceIpGroups(const std::vector\<std::string\> &ipGroups);

3.  参数说明

    1.  参数说明

| 参数名   | 数据类型                         | 参数类型 | 描述                 |
|----------|----------------------------------|----------|----------------------|
| ipGroups | const std::vector\<std::string\> | 入参     | 设备的ipGroups集合。 |

4.  返回值

无

##### UBSHcomService::SetCompletionQueueDepth

1.  函数定义

设置cq队列深度。

2.  实现说明

void UBSHcomService::SetCompletionQueueDepth(uint16_t depth);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述           |
|--------|----------|----------|----------------|
| depth  | uint16_t | 入参     | 完成队列深度。 |

4.  返回值

无

##### UBSHcomService::SetSendQueueSize

1.  函数定义

设置发送队列深度。

2.  实现说明

void UBSHcomService::SetSendQueueSize(uint32_t sqSize);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述           |
|--------|----------|----------|----------------|
| sqSize | uint32_t | 入参     | 发送队列深度。 |

4.  返回值

无

##### UBSHcomService::SetRecvQueueSize

1.  函数定义

设置接收队列深度。

2.  实现说明

void UBSHcomService::SetRecvQueueSize(uint32_t rqSize);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述           |
|--------|----------|----------|----------------|
| rqSize | uint32_t | 入参     | 接收队列深度。 |

4.  返回值

无

##### UBSHcomService::SetPollingBatchSize

1.  函数定义

设置批量polling的大小。

2.  实现说明

void UBSHcomService::SetPollingBatchSize(uint16_t pollSize);

3.  参数说明

    1.  参数说明

| 参数名   | 数据类型 | 参数类型 | 描述                |
|----------|----------|----------|---------------------|
| pollSize | uint16_t | 入参     | 批量polling的大小。 |

4.  返回值

无

##### UBSHcomService::SetEventPollingTimeOutUs

1.  函数定义

设置event polling的超时时间。

2.  实现说明

void UBSHcomService::SetEventPollingTimeOutUs(uint16_t pollTimeout);

3.  参数说明

    1.  参数说明

| 参数名      | 数据类型 | 参数类型 | 描述                    |
|-------------|----------|----------|-------------------------|
| pollTimeout | uint16_t | 入参     | event polling超时时间。 |

4.  返回值

无

##### UBSHcomService::SetTimeOutDetectionThreadNum

1.  函数定义

设置周期任务处理线程数。

2.  实现说明

void UBSHcomService::SetTimeOutDetectionThreadNum(uint32_t threadNum);

3.  参数说明

    1.  参数说明

| 参数名    | 数据类型 | 参数类型 | 描述                 |
|-----------|----------|----------|----------------------|
| threadNum | uint32_t | 入参     | 周期任务处理线程数。 |

4.  返回值

无

##### UBSHcomService::SetMaxConnectionCount

1.  函数定义

设置最大链接数。

2.  实现说明

void UBSHcomService::SetMaxConnectionCount(uint32_t maxConnCount);

3.  参数说明

    1.  参数说明

| 参数名       | 数据类型 | 参数类型 | 描述         |
|--------------|----------|----------|--------------|
| maxConnCount | uint32_t | 入参     | 最大链接数。 |

4.  返回值

无

##### UBSHcomService::SetUBSHcomHeartBeatOptions

1.  函数定义

设置心跳参数配置项。

2.  实现说明

void UBSHcomService::SetUBSHcomHeartBeatOptions(const UBSHcomHeartBeatOptions &opt);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| opt | [UBSHcomHeartBeatOptions](#ubshcomheartbeatoptions) | 入参 | 心跳可选参数项。 |

4.  返回值

无

##### UBSHcomService::SetUBSHcomMultiRailOptions

1.  函数定义

设置多路径参数配置项。

2.  实现说明

void UBSHcomService::SetUBSHcomMultiRailOptions(const UBSHcomMultiRailOptions &opt);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| opt | [UBSHcomMultiRailOptions](#ubshcommultirailoptions) | 入参 | 多路径参数配置项。 |

4.  返回值

无

##### UBSHcomService::SetQueuePrePostSize

1.  函数定义

设置提前下发wr的数量，不设置的话默认64。

2.  实现说明

void UBSHcomService::SetQueuePrePostSize(uint32_t prePostSize);

3.  参数说明

    1.  参数说明

| 参数名      | 数据类型 | 参数类型 | 描述               |
|-------------|----------|----------|--------------------|
| prePostSize | uint32_t | 入参     | 预先下发的wr数量。 |

4.  返回值

无

##### UBSHcomService::SetMaxSendRecvDataCount

1.  函数定义

设置发送数据块最大数量，不设置的话默认8192。

2.  实现说明

void SetMaxSendRecvDataCount(uint32_t maxSendRecvDataCount);

3.  参数说明

    1.  参数说明

| 参数名               | 数据类型 | 参数类型 | 描述                 |
|----------------------|----------|----------|----------------------|
| maxSendRecvDataCount | uint32_t | 入参     | 发送数据块最大数量。 |

4.  返回值

无

##### UBSHcomRegMemoryRegion::GetMemoryKey

1.  函数定义

获得所有内存池的keys。

2.  实现说明

void UBSHcomRegMemoryRegion::GetMemoryKey(UBSHcomMemoryKey &mrKey)；

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型         | 参数类型 | 描述           |
|--------|------------------|----------|----------------|
| mrKey  | UBSHcomMemoryKey | 出参     | 内存池的keys。 |

4.  返回值

无

##### UBSHcomRegMemoryRegion::GetAddress

1.  函数定义

获得首个内存池地址。

2.  实现说明

uintptr_t UBSHcomRegMemoryRegion::GetAddress()；

3.  参数说明

无

4.  返回值

地址值。

##### UBSHcomRegMemoryRegion::GetSize

1.  函数定义

获得首个内存池长度。

2.  实现说明

uint64_t UBSHcomRegMemoryRegion::GetSize()；

3.  参数说明

无

4.  返回值

内存池的长度。

##### UBSHcomRegMemoryRegion::GetHcomMrs

1.  函数定义

获得内存池组。

2.  实现说明

std::vector\<UBSHcomMemoryRegionPtr\>& UBSHcomRegMemoryRegion::GetHcomMrs()；

3.  参数说明

无

4.  返回值

返回std::vector\<UBSHcomMemoryRegionPtr\>类型的数组。

##### UBSHcomNewCallback

1.  函数定义

创建Callback函数。

2.  实现说明

template \<typename... Args\> Callback \*UBSHcomNewCallback(Args... args)；

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述           |
|--------|----------|----------|----------------|
| args   | Args     | 入参     | 回调函数入参。 |

4.  返回值

返回Callback \*函数。

#### 传输层

##### UBSHcomNetDriver::RegisterTLSCaCallback

1.  ?.1.接口使用方法

    函数定义

注册建链双向认证的回调函数，用于获取CA证书。

3.  实现方法

void UBSHcomNetDriver::RegisterTLSCaCallback(const UBSHcomTLSCaCallback &cb);

4.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| cb | [UBSHcomTLSCaCallback](#ZH-CN_TOPIC_0000002465536138) | 入参 | 获取CA证书的回调函数。 |

5.  返回值

无

6.  代码样例

int Verify(void \*x509, const char \*path)  
{  
return 0;  
}  

bool CACallback(const std::string &name, std::string &caPath, std::string &crlPath,  
UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)  
{  
caPath = certPath + "/CA/cacert.pem";  
cb = std::bind(&Verify, std::placeholders::\_1, std::placeholders::\_2);  
return true;  
}

driver-\>RegisterTLSCaCallback(std::bind(&CACallback, std::placeholders::\_1, std::placeholders::\_2, std::placeholders::\_3, std::placeholders::\_4, std::placeholders::\_5));

7.  ?.2.UBSHcomTLSCaCallback函数类型

    函数定义

using UBSHcomTLSCaCallback = std::function\<bool(const std::string &name, std::string &capath, std::string &crlPath, UBSHcomPeerCertVerifyType &verifyPeerCert, UBSHcomTLSCertVerifyCallback &cb)\>;

9.  参数说明

    1.  参数说明

[TABLE]

1.  返回值

Bool类型，表示回调函数是否执行成功。

- 返回值为true：表示成功。

- 返回值为false：表示失败。

  1.  代码样例

bool CACallback(const std::string &name, std::string &caPath, std::string &crlPath,  
UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)  
{  
caPath = certPath + "/CA/cacert.pem";  
cb = std::bind(&Verify, std::placeholders::\_1, std::placeholders::\_2);  
return true;  
}

2.  ?.3.UBSHcomTLSCertVerifyCallback函数类型

    函数定义

using UBSHcomTLSCertVerifyCallback = std::function\<int(void \*, const char \*)\>;

4.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述             |
|--------|----------|----------|------------------|
| \-     | void\*   | 入参     | 加载之后的证书。 |

5.  返回值

表示函数执行结果，返回值为0表示证书验证成功。

##### UBSHcomNetDriver::RegisterTLSCertificationCallback

1.  ?.1.接口使用方法

    函数定义

注册建链双向认证的回调函数，用于获取公钥证书。

3.  实现方法

void UBSHcomNetDriver::RegisterTLSCertificationCallback(const UBSHcomTLSCertificationCallback &cb);

4.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| cb | [UBSHcomTLSCertificationCallback](#ZH-CN_TOPIC_0000002498615437) | 入参 | 获取公钥的回调函数。 |

5.  返回值

无

6.  代码样例

bool CertCallback(const std::string &name, std::string &value)  
{  
value = certPath + "/client/cert.pem";  
return true;  
}

driver-\>RegisterTLSCertificationCallback( std::bind(&CertCallback, std::placeholders::\_1, std::placeholders::\_2));

7.  ?.2.UBSHcomTLSCertificationCallback函数类型

    函数定义

using UBSHcomTLSCertificationCallback = std::function\<bool(const std::string &name, std::string &path)\>;

9.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述                 |
|--------|----------|----------|----------------------|
| name   | String   | 入参     | 句柄名字。           |
| path   | String   | 出参     | 提供的公钥证书路径。 |

10. 返回值

Bool类型，表示回调函数是否执行成功。

- 返回值为true：表示成功。

- 返回值为false：表示失败。

  1.  代码样例

bool CertCallback(const std::string &name, std::string &value)  
{  
value = certPath + "/client/cert.pem";  
return true;  
}

##### UBSHcomNetDriver::RegisterTLSPrivateKeyCallback

1.  ?.1.接口使用方法

    函数定义

注册建链双向认证的回调函数，用户获取私钥证书。

3.  实现方法

void UBSHcomNetDriver::RegisterTLSPrivateKeyCallback(const UBSHcomTLSPrivateKeyCallback &cb);

4.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| cb | [UBSHcomTLSPrivateKeyCallback](#ZH-CN_TOPIC_0000002498615317) | 入参 | 私钥回调函数。 |

5.  返回值

无

6.  代码样例

void Erase(void \*pass, int len) {}  

bool PrivateKeyCallback(const std::string &name, std::string &value, void \*&keyPass, int &len, UBSHcomTLSEraseKeypass &erase)  
{  
static char content\[\] = "xxxx";  
keyPass = reinterpret_cast\<void \*\>(content);  
len = sizeof(content);  
value = certPath + "/client/key.pem";  
erase = std::bind(&Erase, std::placeholders::\_1, std::placeholders::\_2);  
return true;  
}

driver-\>RegisterTLSPrivateKeyCallback(std::bind(&PrivateKeyCallback, std::placeholders::\_1, std::placeholders::\_2, std::placeholders::\_3, std::placeholders::\_4, std::placeholders::\_5));

7.  ?.2.UBSHcomTLSPrivateKeyCallback函数类型

    函数定义

using UBSHcomTLSPrivateKeyCallback = std::function\<bool(const std::string &name, std::string &path, void \*&password, int &length, UBSHcomTLSEraseKeypass &erase)\>;

9.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| name | String | 入参 | 句柄名字。 |
| path | String | 出参 | 提供的私钥证书路径。 |
| password | void\* | 出参 | 私钥加载的明文密码。 |
| length | int | 出参 | 私钥加载的密码长度。 |
| erase | [UBSHcomTLSEraseKeypass](#ZH-CN_TOPIC_0000002498615289) | 出参 | 擦除私钥密码的回调函数，当加载完私钥的时候调用。 |

10. 返回值

Bool类型，表示回调函数是否执行成功。

- 返回值为true表示成功。

- 返回值为false表示失败。

  1.  代码样例

void Erase(void \*pass, int len) {}  

bool PrivateKeyCallback(const std::string &name, std::string &value, void \*&keyPass, int &len, UBSHcomTLSEraseKeypass &erase)  
{  
static char content\[\] = "xxxx";  
keyPass = reinterpret_cast\<void \*\>(content);  
len = sizeof(content);  
value = certPath + "/client/key.pem";  
erase = std::bind(&Erase, std::placeholders::\_1, std::placeholders::\_2);  
return true;  
}

2.  ?.3.UBSHcomTLSEraseKeypass函数类型

    函数定义

using UBSHcomTLSEraseKeypass = std::function\<void(void \*, int)\>;

4.  参数说明

    1.  参数说明

| 参数名   | 数据类型 | 参数类型 | 描述                 |
|----------|----------|----------|----------------------|
| password | void\*   | 出参     | 私钥加载的明文密码。 |
| length   | int      | 出参     | 私钥加载的密码长度。 |

5.  返回值

无

##### UBSHcomNetDriver::RegisterPskUseSessionCb

1.  ?.1.接口使用方法

    函数定义

供Client端注册PSK回调函数。

3.  实现方法

void UBSHcomNetDriver::RegisterPskUseSessionCb(const UBSHcomPskUseSessionCb &cb)

4.  参数说明

    1.  参数说明

| 参数名 | 数据类型               | 参数类型 | 描述                 |
|--------|------------------------|----------|----------------------|
| cb     | UBSHcomPskUseSessionCb | 入参     | 预共享密钥回调函数。 |

5.  返回值

无。

6.  ?.2.UBSHcomPskUseSessionCb函数类型

    函数定义

using UBSHcomPskUseSessionCb = std::function\<int(void \*ssl, const void \*md, const unsigned char \*\*id, size_t \*idlen, void \*\*sess)\>;

8.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述                     |
|--------|----------|----------|--------------------------|
| ssl    | void\*   | 入参     | SSL连接对象。            |
| md     | void\*   | 入参     | 摘要算法。               |
| id     | char \*  | 出参     | 预共享密钥身份标识。     |
| idlen  | size_t   | 出参     | 预共享密钥身份标识长度。 |
| sess   | void\*   | 出参     | SSL会话对象。            |

9.  返回值

int类型。

- 1：表示回调函数执行成功。

- 0：表示回调函数执行失败。

##### UBSHcomNetDriver::RegisterPskFindSessionCb

1.  ?.1.接口使用方法

    函数定义

供Server端注册PSK回调函数。

3.  实现方法

void UBSHcomNetDriver::RegisterPskFindSessionCb(const UBSHcomPskFindSessionCb &cb)

4.  参数说明

    1.  参数说明

| 参数名 | 数据类型                | 参数类型 | 描述                 |
|--------|-------------------------|----------|----------------------|
| cb     | UBSHcomPskFindSessionCb | 入参     | 预共享密钥回调函数。 |

5.  返回值

无。

6.  ?.2.UBSHcomPskFindSessionCb函数类型

    函数定义

using UBSHcomPskFindSessionCb = std::function\<int(void \*ssl, const unsigned char \*identity, size_t identity_len, void \*\*sess)\>;

8.  参数说明

    1.  参数说明

| 参数名       | 数据类型 | 参数类型 | 描述                     |
|--------------|----------|----------|--------------------------|
| ssl          | void\*   | 入参     | SSL连接对象。            |
| identity     | char \*  | 入参     | 预共享密钥身份标识。     |
| identity_len | size_t   | 入参     | 预共享密钥身份标识长度。 |
| sess         | void\*   | 出参     | SSL会话对象。            |

9.  返回值

int类型，1表示回调函数是执行成功，0表示执行失败。

##### UBSHcomNetDriver::RegisterEndpointSecInfoProvider

1.  函数定义

给UBSHcomNetDriver对象设置EP安全信息提供函数。

2.  实现方法

void UBSHcomNetDriver::RegisterEndpointSecInfoProvider(const UBSHcomNetDriverEndpointSecInfoProvider &provider)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| provider | UBSHcomNetDriverEndpointSecInfoProvider | 入参 | 安全信息提供函数。 |

4.  返回值

无

![](media/image8.png)

数据类型解释如下：

using UBSHcomNetDriverEndpointSecInfoProvider = std::function\<int(uint64_t ctx, int64_t &flag, UBSHcomNetDriverSecType &type, char \*&output, uint32_t &outLen, bool &needAutoFree)\>;

其中，outLen的有效范围为(0,2147483646\]。

##### UBSHcomNetDriver::RegisterEndpointSecInfoValidator

1.  函数定义

给UBSHcomNetDriver对象设置EP安全信息校验函数。

2.  实现方法

void UBSHcomNetDriver::RegisterEndpointSecInfoValidator(const UBSHcomNetDriverEndpointSecInfoValidator &validator)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| validator | UBSHcomNetDriverEndpointSecInfoValidator | 入参 | 安全信息校验函数。 |

4.  返回值

无

![](media/image8.png)

数据类型解释如下：

using UBSHcomNetDriverEndpointSecInfoValidator = std::function\<int(uint64_t ctx, int64_t flag, const char \*input, uint32_t inputLen)\>;

##### UBSHcomNetEndpoint::PostSendRawSgl

1.  函数定义

发送一个不带opcode和header的请求给对方，对方将触发新的请求回调，也不带opcode和header，当客户有自己定义的header时可以使用。

2.  实现方法

NResult UBSHcomNetEndpoint::PostSendRawSgl(const UBSHcomNetTransSglRequest &request,uint32_t seqNo)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| request | UBSHcomNetTransSglRequest | 入参 | [请求信息](#ZH-CN_TOPIC_0000002465376586)，填入本地注册MR，按本地MR顺序发送到同一个远端MR，调用后即可释放，rKey/rAddress不需要赋值。 |
| seqNo | uint32_t | 入参 | 对方要回复的seqNo必须大于0，对方可以从context.Header().seqNo中获取它。如果seqNo为0，则生成自动递增的数字。在同步发送消息的情况下，请求和响应的seqNo相等。 |

4.  返回值

返回值为0则表示发送消息成功。

![](media/image8.png)

- 如果NET_EP_SELF_POLLING未设置，则只发出发送请求，不等待发送请求完成情况。

- 如果NET_EP_SELF_POLLING设置，则发出发送请求并等待发送到达对端。

##### UBSHcomNetEndpoint::ReceiveRaw

1.  函数定义

获得发送请求应答的响应，不包含header和opCode，默认超时生效。

2.  实现方法

NResult UBSHcomNetEndpoint::ReceiveRaw(UBSHcomNetResponseContext &ctx)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型           | 参数类型 | 描述            |
|--------|--------------------|----------|-----------------|
| ctx    | UBSHcomNetResponseContext | 出参     | 响应消息的ctx。 |

4.  返回值

返回值为0则表示发送消息成功。

##### UBSHcomNetEndpoint::EstimatedEncryptLen

1.  函数定义

输入原始数据大小的估计加密长度。

2.  实现方法

uint64_t UBSHcomNetEndpoint::EstimatedEncryptLen(uint64_t rawLen)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述                                             |
|--------|----------|----------|--------------------------------------------------|
| rawLen | uint64_t | 入参     | 原始数据长度。范围是(0, 18446744073709551571\]。 |

4.  返回值

返回uint64_t类型，表示数据加密长度。

##### UBSHcomNetEndpoint::Encrypt

1.  函数定义

加密数据。

2.  实现方法

NResult UBSHcomNetEndpoint::Encrypt(const void \*rawData, uint64_t rawLen, void \*cipher, uint64_t &cipherLen)

3.  参数说明

    1.  参数说明

| 参数名    | 数据类型 | 参数类型 | 描述             |
|-----------|----------|----------|------------------|
| rawData   | void \*  | 入参     | 原始数据地址。   |
| rawLen    | uint64_t | 入参     | 原始数据长度。   |
| cipher    | void \*  | 出参     | 加密后数据地址。 |
| cipherLen | uint64_t | 出参     | 加密后数据长度。 |

4.  返回值

返回值为0则表示加密成功。

##### UBSHcomNetEndpoint::EstimatedDecryptLen

1.  函数定义

输出原始数据大小。

2.  实现方法

uint64_t UBSHcomNetEndpoint::EstimatedDecryptLen(uint64_t cipherLen)

3.  参数说明

    1.  参数说明

| 参数名    | 数据类型 | 参数类型 | 描述               |
|-----------|----------|----------|--------------------|
| cipherLen | uint64_t | 入参     | 加密后的数据长度。 |

4.  返回值

返回uint64_t类型，表示解密后的原始数据长度。

##### UBSHcomNetEndpoint::Decrypt

1.  函数定义

解密数据。

2.  实现方法

NResult UBSHcomNetEndpoint::Decrypt(const void \* cipher, uint64_t cipherLen, void \*rawData, uint64_t &rawLen)

3.  参数说明

    1.  参数说明

| 参数名    | 数据类型 | 参数类型 | 描述                   |
|-----------|----------|----------|------------------------|
| cipher    | void \*  | 入参     | 待解密的数据地址。     |
| cipherLen | uint64_t | 入参     | 待解密的数据长度。     |
| rawData   | void \*  | 出参     | 解密后，原始数据地址。 |
| rawLen    | uint64_t | 出参     | 解密后，原始数据长度。 |

4.  返回值

返回值为0则表示解密成功。

##### UBSHcomNetEndpoint::SendFds

1.  函数定义

发送共享文件的句柄，该接口只支持在SHM协议下使用。

2.  实现方法

NResult UBSHcomNetEndpoint::SendFds(int fds\[\], uint32_t len)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述                             |
|--------|----------|----------|----------------------------------|
| fds    | int\[\]  | 入参     | 需要发送的句柄数组。             |
| len    | uint32_t | 入参     | 发送的句柄数量。范围是\[1, 4\]。 |

4.  返回值

返回值为0则表示句柄发送成功。

##### UBSHcomNetEndpoint::ReceiveFds

1.  函数定义

接收共享文件的句柄，该接口只支持在SHM协议下使用。

2.  实现方法

NResult UBSHcomNetEndpoint::ReceiveFds(int fds\[\], uint32_t len)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述                             |
|--------|----------|----------|----------------------------------|
| fds    | int\[\]  | 入参     | 需要接收的句柄数组。             |
| len    | uint32_t | 入参     | 接收的句柄数量。范围是\[1, 4\]。 |

4.  返回值

返回值为0则表示句柄发送成功。

##### UBSHcomNetOutLogger::Instance

1.  函数定义

创建外部日志对象。

2.  实现方法

static UBSHcomNetOutLogger \*UBSHcomNetOutLogger::Instance()

3.  参数说明

无

4.  返回值

返回外部日志导入对象。

##### UBSHcomNetOutLogger::SetLogLevel

1.  函数定义

- 设置外部日志对象日志等级，设置为环境变量HCOM_SET_LOG_LEVEL。

- 设置外部日志对象日志等级，大于等于此等级的日志将被打印。

日志等级如下：

- 0：debug

- 1：info

- 2：warn

- 3：error

  1.  实现方法

&nbsp;

- static void UBSHcomNetOutLogger::SetLogLevel()

- static void UBSHcomNetOutLogger::SetLogLevel(int level)

  1.  参数说明

      1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述                       |
|--------|----------|----------|----------------------------|
| level  | int      | 入参     | 日志等级。范围是\[0, 3\]。 |

2.  返回值

无

##### UBSHcomNetOutLogger::SetExternalLogFunction

1.  函数定义

设置外部日志对象外部日志函数。

2.  实现方法

void UBSHcomNetOutLogger::SetExternalLogFunction(ExternalLog func)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型    | 参数类型 | 描述           |
|--------|-------------|----------|----------------|
| func   | ExternalLog | 入参     | 外部日志函数。 |

4.  返回值

无

![](media/image8.png)

数据类型解释如下：

typedef void (\*ExternalLog)(int level, const char \*msg).

##### UBSHcomNetOutLogger::Print

1.  函数定义

打印日志。

2.  实现方法

static inline void UBSHcomNetOutLogger::Print(int level, const char \*msg)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型      | 参数类型 | 描述                       |
|--------|---------------|----------|----------------------------|
| level  | int           | 入参     | 日志等级。范围是\[0, 3\]。 |
| msg    | const char \* | 入参     | 日志内容。                 |

4.  返回值

无

##### UBSHcomNetOutLogger::Log

1.  函数定义

打印日志，如果有外部日志函数，则使用外部日志函数。

2.  实现方法

void UBSHcomNetOutLogger::Log(int level, const std::ostringstream &oss) const

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型                 | 参数类型 | 描述                       |
|--------|--------------------------|----------|----------------------------|
| level  | int                      | 入参     | 日志等级。范围是\[0, 3\]。 |
| oss    | const std::ostringstream | 入参     | 日志内容。                 |

4.  返回值

无

##### UBSHcomNetOutLogger::GetLogLevel

1.  函数定义

获取当日志打印等级。

2.  实现方法

int UBSHcomNetOutLogger::GetLogLevel()

3.  参数说明

无

4.  返回值

返回日志等级。

##### UBSHcomNetAtomicState::Get

1.  函数定义

获得原子状态。

2.  实现方法

T Get() const

3.  参数说明

无

4.  返回值

获得原子状态。

##### UBSHcomNetAtomicState::Set

1.  函数定义

设置原子状态。

2.  实现方法

void Set(T newState)

3.  参数说明

    1.  参数说明

| 参数名   | 数据类型 | 参数类型 | 描述                 |
|----------|----------|----------|----------------------|
| newState | T        | 入参     | 需要设置的原子状态。 |

4.  返回值

无

##### UBSHcomNetAtomicState::CAS

1.  函数定义

原子状态比较并交换。

2.  实现方法

bool CAS(T oldState, T newState)

3.  参数说明

    1.  参数说明

| 参数名   | 数据类型 | 参数类型 | 描述         |
|----------|----------|----------|--------------|
| oldState | T        | 入参     | 旧原子状态。 |
| newState | T        | 入参     | 新原子状态。 |

4.  返回值

布尔值。检查mState是否等于oldState。如果是，则将其设置为newState，并返回true；否则不做任何修改，返回false。

##### UBSHcomNetAtomicState::Compare

1.  函数定义

原子状态比较。

2.  实现方法

bool Compare(T state)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述       |
|--------|----------|----------|------------|
| state  | T        | 入参     | 原子状态。 |

4.  返回值

布尔值。检查mState是否等于state，如果是返回true；否则返回false。

### C API

#### 服务层

##### ubs_hcom_service_add_workergroup

1.  函数定义

向Service中增加内存池。

2.  实现方法

void ubs_hcom_service_add_workergroup(ubs_hcom_service service, int8_t priority, uint16_t workerGroupId, uint32_t threadCount,

const char \*cpuIdsRange);

3.  参数说明

    1.  参数说明

| 参数名        | 数据类型         | 参数类型 | 描述                             |
|---------------|------------------|----------|----------------------------------|
| service       | ubs_hcom_service | 入参     | 之前创建的ubs_hcom_service对象。 |
| priority      | int8_t           | 入参     | 线程优先级。                     |
| workerGroupId | uint16_t         | 入参     | 线程组ID。                       |
| threadCount   | uint32_t         | 入参     | 组里的线程数。                   |
| cpuIdsRange   | const char \*    | 入参     | CPU ID范围。                     |

4.  返回值

无

##### ubs_hcom_service_add_listener

1.  函数定义

添加监听线程。

2.  实现方法

void ubs_hcom_service_add_listener(ubs_hcom_service service, const char \*url, uint16_t workerCount);

3.  参数说明

    1.  参数说明

| 参数名        | 数据类型         | 参数类型 | 描述                             |
|---------------|------------------|----------|----------------------------------|
| service       | ubs_hcom_service | 入参     | 之前创建的ubs_hcom_service对象。 |
| priority      | int8_t           | 入参     | 线程优先级。                     |
| workerGroupId | uint16_t         | 入参     | 线程组ID。                       |
| threadCount   | uint32_t         | 入参     | 组里的线程数。                   |
| cpuIdsRange   | const char \*    | 入参     | CPU ID范围。                     |

4.  返回值

无

##### ubs_hcom_service_set_lbpolicy

1.  函数定义

设置负载均衡策略。

2.  实现方法

void ubs_hcom_service_set_lbpolicy(ubs_hcom_service service, ubs_hcom_service_lb_policy lbPolicy);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| service | ubs_hcom_service | 入参 | 之前创建的ubs_hcom_service对象。 |
| lbPolicy | ubs_hcom_service_lb_policy | 入参 | 负载均衡策略。 |

4.  返回值

无

##### ubs_hcom_service_set_tls_opt

1.  函数定义

设置TLS配置项。

2.  实现方法

void ubs_hcom_service_set_tls_opt(ubs_hcom_service service, bool enableTls, ubs_hcom_service_tls_version version,

ubs_hcom_service_cipher_suite cipherSuite, ubs_hcom_tls_get_cert_cb certCb, ubs_hcom_tls_get_pk_cb priKeyCb, ubs_hcom_tls_get_ca_cb caCb);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| service | ubs_hcom_service | 入参 | 之前创建的ubs_hcom_service对象。 |
| enableTls | bool | 入参 | 是否开启TLS。 |
| version | ubs_hcom_service_tls_version | 入参 | TLS版本。 |
| cipherSuite | ubs_hcom_service_cipher_suite | 入参 | 加密方式。 |
| certCb | ubs_hcom_tls_get_cert_cb | 入参 | 获取TLS证书的回调。 |
| priKeyCb | ubs_hcom_tls_get_pk_cb | 入参 | 获取TLS私钥的回调。 |
| caCb | ubs_hcom_tls_get_ca_cb | 入参 | 获取TLS认证的回调。 |

![](media/image8.png)

使用UB自举建链时，暂不支持安全认证和安全加密。

4.  返回值

无

##### ubs_hcom_service_set_secure_opt

1.  函数定义

设置安全加密选项。

2.  实现方法

void ubs_hcom_service_set_secure_opt(ubs_hcom_service service, ubs_hcom_service_secure_type secType, ubs_hcom_secinfo_provider provider,

ubs_hcom_secinfo_validator validator, uint16_t magic, uint8_t version);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| service | ubs_hcom_service | 入参 | 之前创建的ubs_hcom_service对象。 |
| secType | ubs_hcom_service_secure_type | 入参 | 加密方式。 |
| provider | ubs_hcom_secinfo_provider | 入参 | 密钥提供回调。 |
| validator | ubs_hcom_secinfo_validator | 入参 | 密钥校验回调。 |
| magic | uint16_t | 入参 | 魔数。 |
| version | uint8_t | 入参 | 安全加密版本。 |

4.  返回值

无

##### ubs_hcom_service_set_tcp_usr_timeout

1.  函数定义

设置负载均衡策略。

2.  实现方法

void ubs_hcom_service_set_tcp_usr_timeout(ubs_hcom_service service, uint16_t timeOutSec);

3.  参数说明

    1.  参数说明

| 参数名     | 数据类型         | 参数类型 | 描述                             |
|------------|------------------|----------|----------------------------------|
| service    | ubs_hcom_service | 入参     | 之前创建的ubs_hcom_service对象。 |
| timeOutSec | uint16_t         | 入参     | TCP超时时间                      |

4.  返回值

无

##### ubs_hcom_service_set_tcp_send_zcopy

1.  函数定义

设置负载均衡策略。

2.  实现方法

void ubs_hcom_service_set_tcp_send_zcopy(ubs_hcom_service service, bool tcpSendZCopy);

3.  参数说明

    1.  参数说明

| 参数名       | 数据类型         | 参数类型 | 描述                             |
|--------------|------------------|----------|----------------------------------|
| service      | ubs_hcom_service | 入参     | 之前创建的ubs_hcom_service对象。 |
| tcpSendZCopy | bool             | 入参     | TCP是否开启ZCopy。               |

4.  返回值

无

##### ubs_hcom_service_set_ipmask

1.  函数定义

设置要监听的IP。

2.  实现方法

void ubs_hcom_service_set_ipmask(ubs_hcom_service service, const char \*ipMask);

3.  参数说明

    1.  参数说明

[TABLE]

4.  返回值

无

##### ubs_hcom_service_set_ipgroup

1.  函数定义

设置要监听的IP。

2.  实现方法

void ubs_hcom_service_set_ipgroup(ubs_hcom_service service, const char \*ipGroup);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| service | ubs_hcom_service | 入参 | 之前创建的ubs_hcom_service对象。 |
| ipGroup | const char \* | 入参 | 要监听的IP，如果明确指定了ipGroup，则直接使用对应的设备。 |

4.  返回值

无

##### ubs_hcom_service_set_cq_depth

1.  函数定义

设置cq队列的深度。

2.  实现方法

void ubs_hcom_service_set_cq_depth(ubs_hcom_service service, uint16_t depth);

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型         | 参数类型 | 描述                             |
|---------|------------------|----------|----------------------------------|
| service | ubs_hcom_service | 入参     | 之前创建的ubs_hcom_service对象。 |
| depth   | uint16_t         | 入参     | cq队列的深度。                   |

4.  返回值

无

##### ubs_hcom_service_set_sq_size

1.  函数定义

设置SQ队列的大小，默认256。

2.  实现方法

void ubs_hcom_service_set_sq_size(ubs_hcom_service service, uint32_t sqSize);

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型         | 参数类型 | 描述                             |
|---------|------------------|----------|----------------------------------|
| service | ubs_hcom_service | 入参     | 之前创建的ubs_hcom_service对象。 |
| sqSize  | uint32_t         | 入参     | SQ队列的大小，默认256。          |

4.  返回值

无

##### ubs_hcom_service_set_rq_size

1.  函数定义

设置RQ队列的大小，默认256。

2.  实现方法

void ubs_hcom_service_set_rq_size(ubs_hcom_service service, uint32_t rqSize);

3.  参数说明

    1.  参数说明

| 参数名  | 数据类型         | 参数类型 | 描述                             |
|---------|------------------|----------|----------------------------------|
| service | ubs_hcom_service | 入参     | 之前创建的ubs_hcom_service对象。 |
| rqSize  | uint32_t         | 入参     | RQ队列的大小，默认256。          |

4.  返回值

无

##### ubs_hcom_service_set_polling_batchsize

1.  函数定义

设置传输层worker单次poll的个数。

2.  实现方法

void ubs_hcom_service_set_polling_batchsize(ubs_hcom_service service, uint16_t pollSize);

3.  参数说明

    1.  参数说明

| 参数名   | 数据类型         | 参数类型 | 描述                             |
|----------|------------------|----------|----------------------------------|
| service  | ubs_hcom_service | 入参     | 之前创建的ubs_hcom_service对象。 |
| pollSize | uint16_t         | 入参     | 单次poll的个数。                 |

4.  返回值

无

##### ubs_hcom_service_set_polling_timeoutus

1.  函数定义

设置event polling的超时时间。

2.  实现说明

void ubs_hcom_service_set_polling_timeoutus(ubs_hcom_service service, uint16_t pollTimeout);

3.  参数说明

    1.  参数说明

| 参数名      | 数据类型         | 参数类型 | 描述                         |
|-------------|------------------|----------|------------------------------|
| service     | ubs_hcom_service | 入参     | 创建的ubs_hcom_service对象。 |
| pollTimeout | uint16_t         | 入参     | event polling超时时间。      |

4.  返回值

无

##### ubs_hcom_service_set_timeout_threadnum

1.  函数定义

设置周期任务处理线程数。

2.  实现说明

void ubs_hcom_service_set_timeout_threadnum(ubs_hcom_service service, uint32_t threadNum);

3.  参数说明

    1.  参数说明

| 参数名    | 数据类型         | 参数类型 | 描述                         |
|-----------|------------------|----------|------------------------------|
| service   | ubs_hcom_service | 入参     | 创建的ubs_hcom_service对象。 |
| threadNum | uint32_t         | 入参     | 周期任务处理线程数。         |

4.  返回值

无

##### ubs_hcom_service_set_max_connection_cnt

1.  函数定义

设置最大链接数。

2.  实现说明

void ubs_hcom_service_set_max_connection_cnt(ubs_hcom_service service, uint32_t maxConnCount);

3.  参数说明

    1.  参数说明

| 参数名       | 数据类型         | 参数类型 | 描述                         |
|--------------|------------------|----------|------------------------------|
| service      | ubs_hcom_service | 入参     | 创建的ubs_hcom_service对象。 |
| maxConnCount | uint32_t         | 入参     | 最大链接数。                 |

4.  返回值

无

##### ubs_hcom_service_set_heartbeat_opt

1.  函数定义

设置心跳参数配置项。

2.  实现说明

void ubs_hcom_service_set_heartbeat_opt(ubs_hcom_service service, uint16_t idleSec, uint16_t probeTimes, uint16_t intervalSec);

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| service | ubs_hcom_service | 入参 | 创建的ubs_hcom_service对象。 |
| idleSec | uint16_t | 入参 | 发送心跳保活消息间隔时间。 |
| probeTimes | uint16_t | 入参 | 发送心跳探测失败/没收到回复重试次数，超了认为连接已经断开。 |
| intervalSec | uint16_t | 入参 | 发送心跳后再次发送时间。 |

4.  返回值

无

##### ubs_hcom_service_set_multirail_opt

1.  函数定义

设置多路径参数配置项。

2.  实现说明

void ubs_hcom_service_set_multirail_opt(ubs_hcom_service service, bool enable, uint32_t threshold);

3.  参数说明

    1.  参数说明

[TABLE]

1.  返回值

无

##### ubs_hcom_set_log_handler

1.  函数定义

设置外部日志模板。

2.  实现方法

void ubs_hcom_set_log_handler(ubs_hcom_log_handler h)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型             | 参数类型 | 描述               |
|--------|----------------------|----------|--------------------|
| h      | ubs_hcom_log_handler | 入参     | 外部日志回调函数。 |

4.  返回值

无

![](media/image8.png)

数据类型解释如下：

typedef void (\*ubs_hcom_log_handler)(int level, const char \*msg).

#### 传输层

##### ubs_hcom_driver_register_tls_cb

1.  ?.1.接口使用方法

    函数定义

注册建链双向认证的回调函数，分别用于获取CA证书、获取公钥证书和私钥证书。

3.  实现方法

uintptr_t ubs_hcom_driver_register_tls_cb(ubs_hcom_driver driver, ubs_hcom_tls_get_cert_cb certCb, ubs_hcom_tls_get_pk_cb priKeyCb, ubs_hcom_tls_get_ca_cb caCb)

4.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| driver | ubs_hcom_driver | 入参 | 创建的ubs_hcom_driver对象。 |
| certCb | [Hcom_TlsGetCb](#ZH-CN_TOPIC_0000002465376234) | 入参 | 回调函数句柄。 |
| priKeyCb | [ubs_hcom_tls_get_pk_cb](#ZH-CN_TOPIC_0000002498615949) | 入参 | 回调函数句柄。 |
| caCb | [ubs_hcom_tls_get_ca_cb](#ZH-CN_TOPIC_0000002465535906) | 入参 | 回调函数句柄。 |

5.  返回值

uintptr_t，返回内部句柄地址。

6.  ?.2.ubs_hcom_tls_get_ca_cb函数类型

    函数定义

typedef int (\*ubs_hcom_tls_get_ca_cb)(const char \*name, char \*\*caPath, char \*\*crlPath, ubs_hcom_peer_cert_verify_type \*verifyType, ubs_hcom_tls_cert_verify \*verify)

8.  参数说明

    1.  参数说明

[TABLE]

1.  返回值

返回值为0则表示回调函数执行成功。

2.  ?.3.ubs_hcom_peer_cert_verify_type函数类型

    函数定义

typedef int (\*ubs_hcom_tls_cert_verify)(void \*x509, const char \*crlPath)

4.  参数说明

    1.  参数说明

| 参数名  | 数据类型 | 参数类型 | 描述                 |
|---------|----------|----------|----------------------|
| x509    | void\*   | 入参     | 加载之后的x509证书。 |
| crlPath | char \*  | 入参     | 提供的吊销列表路径。 |

5.  返回值

表示函数执行结果，返回值为0则表示证书验证成功。

6.  ?.4.Hcom_TlsGetCb

    函数定义

typedef int (\*ubs_hcom_tls_get_cert_cb)(const char \*name, char \*\*certPath)

8.  参数说明

    1.  参数说明

| 参数名     | 数据类型 | 参数类型 | 描述                 |
|------------|----------|----------|----------------------|
| name       | char \*  | 出参     | 句柄名字。           |
| \*certPath | char \*  | 出参     | 提供的公钥证书路径。 |

9.  返回值

Bool类型，表示回调函数是否执行成功

- 返回值为true：表示成功。

- 返回值为false：表示失败。

  1.  ?.5.ubs_hcom_tls_get_pk_cb

      函数定义

typedef int (\*ubs_hcom_tls_get_pk_cb)(const char \*name, char \*\*priKeyPath, char \*\*keyPass, ubs_hcom_tls_keypass_erase \*erase)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| name | char \* | 出参 | 句柄名字。 |
| \*priKeyPath | char \* | 出参 | 提供的私钥证书路径。 |
| \*keyPass | void\* | 出参 | 私钥加载的明文密码。 |
| erase | [ubs_hcom_tls_keypass_erase](#ZH-CN_TOPIC_0000002498495445) \* | 出参 | 擦除私钥密码的回调函数，当加载完私钥的时候调用。 |

4.  返回值

返回值为0则表示回调函数执行成功。

5.  ?.6.ubs_hcom_tls_keypass_erase

    函数定义

typedef void (\*ubs_hcom_tls_keypass_erase)(char \*keyPass, int len)

7.  参数说明

    1.  参数说明

| 参数名  | 数据类型 | 参数类型 | 描述                 |
|---------|----------|----------|----------------------|
| keyPass | char \*  | 入参     | 私钥加载的明文密码。 |
| len     | int      | 入参     | 私钥加载的密码长度。 |

8.  返回值

无

##### ubs_hcom_ep_post_send_raw

1.  函数定义

向对端发送一个带有op信息的请求。

2.  实现方法

int ubs_hcom_ep_post_send_raw(ubs_hcom_endpoint ep, ubs_hcom_send_request \*req, uint32_t seqNo)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| ep | ubs_hcom_endpoint | 入参 | 建链创建好的EP对象。 |
| \*req | [ubs_hcom_send_request](#ubs_hcom_send_request) | 入参 | 发送请求信息，使用本地内存来存储数据，数据会被复制，调用后可释放本地内存。 |
| seqNo | uint32_t | 入参 | 对端用于回复的序列号。 |

##### ubs_hcom_ep_post_send_raw_sgl

1.  函数定义

向对端发送请求。

2.  实现方法

int ubs_hcom_ep_post_send_raw_sgl(ubs_hcom_endpoint ep, ubs_hcom_readwrite_request_sgl \*req, uint32_t seqNo)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| ep | ubs_hcom_endpoint | 入参 | 建链创建好的EP对象。 |
| \*req | ubs_hcom_readwrite_request_sgl | 入参 | 请求信息。 |
| seqNo | uint32_t | 入参 | 对方要回复的seqNo必须大于0，对方可以从context.Header().seqNo中获取它；如果seqNo为0，则生成自动递增的数字。在同步发送消息的情况下，请求和响应的seqNo相等。 |

2.  ubs_hcom_readwrite_request_sgl结构体

[TABLE]

4.  返回值

返回值为0则表示发送请求成功。

##### ubs_hcom_ep_post_read_sgl

1.  函数定义

向对端发送一个读请求。

2.  实现方法

int ubs_hcom_ep_post_read_sgl(ubs_hcom_endpoint ep, ubs_hcom_readwrite_request_sgl \*req)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| ep | ubs_hcom_endpoint | 入参 | 建链创建好的EP对象。 |
| \*req | [ubs_hcom_readwrite_request_sgl](#ubs_hcom_readwrite_request_sgl) | 入参 | 读请求信息。 |

4.  返回值

返回值为0则表示读成功。

##### ubs_hcom_ep_post_write_sgl

1.  函数定义

向对端发送一个写请求。

2.  实现方法

int ubs_hcom_ep_post_write_sgl(ubs_hcom_endpoint ep, ubs_hcom_readwrite_request_sgl \*req)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| ep | ubs_hcom_endpoint | 入参 | 建链创建好的EP对象。 |
| \*req | [ubs_hcom_readwrite_request_sgl](#ubs_hcom_readwrite_request_sgl) | 入参 | 写请求信息。 |

4.  返回值

返回值为0则表示写成功。

##### ubs_hcom_ep_receive_raw

1.  函数定义

接收消息，仅对NET_C_EP_SELF_POLLING设置时使用。

2.  实现方法

int ubs_hcom_ep_receive_raw(ubs_hcom_endpoint ep, int32_t timeout, ubs_hcom_response_context \*\*ctx)

3.  参数说明

    1.  参数说明

[TABLE]

1.  返回值

返回值为0则表示接收消息成功。

##### ubs_hcom_ep_receive_raw_sgl

1.  函数定义

接收对端发送过来的SGL消息。

2.  实现方法

int ubs_hcom_ep_receive_raw_sgl(ubs_hcom_endpoint ep, int32_t timeout, ubs_hcom_response_context \*\*ctx)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| ep | ubs_hcom_endpoint | 入参 | EndPoint。 |
| timeout | int32_t | 入参 | 超时时间，单位是秒。0为立刻超时，负数为永不超时。 |
| ctx | [ubs_hcom_response_context](#ubs_hcom_response_context) | 出参 | 接收到的消息。 |

4.  返回值

返回值为0则表示成功。

##### ubs_hcom_estimate_encrypt_len

1.  函数定义

输入原始数据大小的估计加密长度。

2.  实现方法

uint64_t ubs_hcom_estimate_encrypt_len(ubs_hcom_endpoint ep, uint64_t rawLen)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| ep | ubs_hcom_endpoint | 入参 | 建链创建好的EP对象。 |
| rawLen | uint64_t | 入参 | 原始数据长度。范围是(0, 18446744073709551571\]。 |

4.  返回值

返回uint64_t类型，表示数据加密长度。

##### ubs_hcom_encrypt

1.  函数定义

加密数据。

2.  实现方法

int ubs_hcom_encrypt(ubs_hcom_endpoint ep, const void \*rawData, uint64_t rawLen, void \*cipher, uint64_t \*cipherLen)

3.  参数说明

    1.  参数说明

| 参数名    | 数据类型          | 参数类型 | 描述                 |
|-----------|-------------------|----------|----------------------|
| ep        | ubs_hcom_endpoint | 入参     | 建链创建好的EP对象。 |
| rawData   | void \*           | 入参     | 原始数据地址。       |
| rawLen    | uint64_t          | 入参     | 原始数据长度。       |
| cipher    | void \*           | 出参     | 加密后数据地址。     |
| cipherLen | uint64_t          | 出参     | 加密后数据长度。     |

4.  返回值

返回值为0则表示加密成功。

##### ubs_hcom_estimate_decrypt_len

1.  函数定义

输出原始数据大小。

2.  实现方法

uint64_t ubs_hcom_estimate_decrypt_len(ubs_hcom_endpoint ep, uint64_t cipherLen)

3.  参数说明

    1.  参数说明

| 参数名    | 数据类型          | 参数类型 | 描述                 |
|-----------|-------------------|----------|----------------------|
| ep        | ubs_hcom_endpoint | 入参     | 建链创建好的EP对象。 |
| cipherLen | uint64_t          | 入参     | 加密后的数据长度。   |

4.  返回值

返回uint64_t类型，表示解密后的原始数据长度。

##### ubs_hcom_decrypt

1.  函数定义

解密数据。

2.  实现方法

int ubs_hcom_decrypt(ubs_hcom_endpoint ep, const void \*cipher, uint64_t cipherLen, void \*rawData, uint64_t \*rawLen)

3.  参数说明

    1.  参数说明

| 参数名    | 数据类型          | 参数类型 | 描述                   |
|-----------|-------------------|----------|------------------------|
| ep        | ubs_hcom_endpoint | 入参     | 建链创建好的EP对象。   |
| cipher    | void \*           | 入参     | 待解密的数据地址。     |
| cipherLen | uint64_t          | 入参     | 待解密的数据长度。     |
| rawData   | void \*           | 出参     | 解密后，原始数据地址。 |
| rawLen    | uint64_t          | 出参     | 解密后，原始数据长度。 |

4.  返回值

返回值为0则表示解密成功。

##### ubs_hcom_send_fds

1.  函数定义

发送共享文件的句柄，该接口只支持在SHM协议下使用。

2.  实现方法

int ubs_hcom_send_fds(ubs_hcom_endpoint ep, int fds\[\], uint32_t len)

3.  参数说明

    1.  参数说明

| 参数名 | 数据类型          | 参数类型 | 描述                             |
|--------|-------------------|----------|----------------------------------|
| ep     | ubs_hcom_endpoint | 入参     | 建链创建好的EP对象。             |
| fds    | int\[\]           | 入参     | 需要发送的句柄数组。             |
| len    | uint32_t          | 入参     | 发送的句柄数量。范围是\[1, 4\]。 |

4.  返回值

返回值为0则表示句柄发送成功。

##### ubs_hcom_receive_fds

1.  函数定义

接收共享文件的句柄，该接口只支持在SHM协议下使用。

2.  实现方法

int ubs_hcom_receive_fds(ubs_hcom_endpoint ep, int fds\[\], uint32_t len, int timeoutSec)

3.  参数说明

    1.  参数说明

| 参数名     | 数据类型          | 参数类型 | 描述                               |
|------------|-------------------|----------|------------------------------------|
| ep         | ubs_hcom_endpoint | 入参     | 建链创建好的EP对象。               |
| fds        | int\[\]           | 出参     | 需要接收的句柄数组。               |
| len        | uint32_t          | 入参     | 接收的句柄数量。范围是\[1, 4\]。   |
| timeoutSec | int               | 入参     | 设置接收超时时间，-1表示不设超时。 |

4.  返回值

返回值为0则表示句柄发送成功。

## 结构体参考

### C++结构体

#### 服务层结构体

##### UBSHcomServiceOptions

1.  参数说明

[TABLE]

![](media/image8.png)

双边操作允许发送最大消息的长度，可结合使用场景通过maxSendRecvDataSize来配置。

##### UBSHcomConnectOptions

1.  参数说明

| 参数名 | 数据类型 | 默认值 | 描述 |
|----|----|----|----|
| clientGroupId | uint16_t | 0 | 客户端worker线程池ID。 |
| serverGroupId | uint16_t | 0 | 服务端worker线程池ID。 |
| linkCount | uint8_t | 1 | 链接数。 |
| mode | [UBSHcomClientPollingMode](#ubshcomclientpollingmode) | WORKER_POLL | 客户端调用通信接口时poll的模式。 |
| cbType | [UBSHcomChannelCallBackType](#ubshcomchannelcallbacktype) | CHANNEL_FUNC_CB | 回调类型。 |
| payload | std::string | 空 | 建链发送给服务端的payload。 |

##### UBSHcomRequest

1.  参数说明

| 配置项  | 数据类型 | 默认值  | 说明                                 |
|---------|----------|---------|--------------------------------------|
| address | void\*   | nullptr | 数据指针。该字段在内部有空指针校验。 |
| size    | uint32_t | 0       | 数据大小。范围是(0,UINT32_MAX\]。    |
| key     | uint64_t | 0       | 数据地址key值。                      |
| opcode  | uint16_t | 0       | 操作类型。                           |

##### UBSHcomResponse

1.  参数说明

| 配置项    | 类型     | 默认值  | 说明         |
|-----------|----------|---------|--------------|
| address   | void\*   | nullptr | 数据指针。   |
| size      | uint32_t | 0       | 数据大小。   |
| errorCode | int16_t  | 0       | 回复错误码。 |

##### UBSHcomReplyContext

1.  参数说明

| 参数名    | 数据类型  | 描述                                |
|-----------|-----------|-------------------------------------|
| rspCtx    | uintptr_t | 回复上下文，可从回调context中获取。 |
| errorCode | int16_t   | 回复的错误码。                      |

##### UBSHcomOneSideRequest

1.  参数说明

| 参数名   | 数据类型         | 描述                                         |
|----------|------------------|----------------------------------------------|
| lAddress | uintptr_t        | 单边通信本端内存地址。                       |
| rAddress | uintptr_t        | 单边通信，对端内存地址。                     |
| lKey     | UBSHcomMemoryKey | 单边通信，本端UBSHcomMemoryKey。             |
| rKey     | UBSHcomMemoryKey | 单边通信，对端UBSHcomMemoryKey。             |
| size     | uint32_t         | 单边通信，数据大小。范围是(0, UINT32_MAX\]。 |

2.  UBSHcomMemoryKey

| 参数名 | 数据类型 | 描述 |
|----|----|----|
| keys | uint64_t \[4\] | 内存注册后的内存区域key，MultiRail场景下多个设备有多个key（最多4个），非MultiRail场景下只需要1个。 |
| tokens | uint64_t | UBC场景下注册内存区域的token value，MultiRail场景下多个设备有多个token value（最多4个），非MultiRail场景下只需要1个。 |

##### UBSHcomFlowCtrlOptions

1.  参数说明

[TABLE]

##### UBSHcomTlsOptions

1.  ?.1.参数说明

    1.  参数说明

| 参数名 | 数据类型 | 默认值 | 描述 |
|----|----|----|----|
| caCb | UBSHcomTLSCaCallback | nullptr | 建链双向认证的回调函数，用于获取CA证书。 |
| cfCb | UBSHcomTLSCertificationCallback | nullptr | 建链双向认证的回调函数，用于获取公钥证书。 |
| pkCb | UBSHcomTLSPrivateKeyCallback | nullptr | 建链双向认证的回调函数，用于获取私钥证书。 |
| tlsVersion | UBSHcomTlsVersion | UBSHcomTlsVersion::TLS_1_3 | TLS版本，支持TLS1.3，不再支持TLS1.2。 |
| netCipherSuite | UBSHcomCipherSuite | UBSHcomCipherSuite::AES_GCM_128 | 加密算法，取值范围见[UBSHcomNetCipherSuite](#ubshcomnetciphersuite)。 |
| enableTls | bool | true | 是否开启TLS认证。 |

2.  ?.2.UBSHcomTLSCaCallback函数类型

    函数定义

using UBSHcomTLSCaCallback = std::function\<bool(const std::string &name, std::string &capath, std::string &crlPath, UBSHcomPeerCertVerifyType &verifyPeerCert, UBSHcomTLSCertVerifyCallback &cb)\>;

4.  参数说明

    1.  参数说明

[TABLE]

1.  返回值

Bool类型，表示回调函数是否执行成功。

- 返回值为true：表示成功。

- 返回值为false：表示失败。

  1.  代码样例

int Verify(void \*x509, const char \*path)  
{  
return 0;  
}  
bool CACallback(const std::string &name, std::string &caPath, std::string &crlPath,  
UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)  
{  
caPath = certPath + "/CA/cacert.pem";  
cb = std::bind(&Verify, std::placeholders::\_1, std::placeholders::\_2);  
return true;  
}

2.  ?.3.UBSHcomTLSCertVerifyCallback函数类型

    函数定义

using UBSHcomTLSCertificationCallback = std::function\<bool(const std::string &name, std::string &path)\>;

4.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述                 |
|--------|----------|----------|----------------------|
| name   | String   | 入参     | 句柄名字。           |
| path   | String   | 出参     | 提供的公钥证书路径。 |

5.  返回值

Bool类型，表示回调函数是否执行成功。

- 返回值为true：表示成功。

- 返回值为false：表示失败。

  1.  代码样例

bool CertCallback(const std::string &name, std::string &value)  
{  
value = certPath + "/client/cert.pem";  
return true;  
}

2.  ?.4.UBSHcomTLSPrivateKeyCallback函数类型

    函数定义

using UBSHcomTLSPrivateKeyCallback = std::function\<bool(const std::string &name, std::string &path, void \*&password, int &length, UBSHcomTLSEraseKeypass &erase)\>;

4.  参数说明

    1.  参数说明

| 参数名 | 数据类型 | 参数类型 | 描述 |
|----|----|----|----|
| name | String | 入参 | 句柄名字。 |
| path | String | 出参 | 提供的私钥证书路径。 |
| password | void\* | 出参 | 私钥加载的明文密码。 |
| length | int | 出参 | 私钥加载的密码长度。 |
| erase | [UBSHcomTLSEraseKeypass](#ZH-CN_TOPIC_0000002498615289) | 出参 | 擦除私钥密码的回调函数，当加载完私钥的时候调用。 |

5.  返回值

Bool类型，表示回调函数是否执行成功。

- 返回值为true：表示成功。

- 返回值为false：表示失败。

  1.  代码样例

void Erase(void \*pass, int len) {}  

bool PrivateKeyCallback(const std::string &name, std::string &value, void \*&keyPass, int &len, UBSHcomTLSEraseKeypass &erase)  
{  
static char content\[\] = "xxxx";  
keyPass = reinterpret_cast\<void \*\>(content);  
len = sizeof(content);  
value = certPath + "/client/key.pem";  
erase = std::bind(&Erase, std::placeholders::\_1, std::placeholders::\_2);  
return true;  
}

##### UBSHcomConnSecureOptions

1.  参数说明

[TABLE]

![](media/image8.png)

数据类型解释如下：

using UBSHcomDriverSecInfoProvider= std::function\<int(uint64_t ctx, int64_t &flag, UBSHcomNetDriverSecType &type, char \*&output, uint32_t &outLen, bool &needAutoFree)\>;

其中，outLen的有效范围为(0, 2147483646\]。

using UBSHcomNetDriverEndpointSecInfoValidator = std::function\<int(uint64_t ctx, int64_t flag, const char \*input, uint32_t inputLen)\>;

##### UBSHcomHeartBeatOptions

1.  参数说明

| 参数名 | 数据类型 | 描述 | 默认值 |
|----|----|----|----|
| heartBeatIdleSec | uint16_t | 发送心跳保活消息间隔时间。 | 60 |
| heartBeatProbeTimes | uint16_t | 发送心跳探测失败/没收到回复重试次数，超了认为连接已经断开。 | 7 |
| heartBeatProbeIntervalSec | uint16_t | 发送心跳后再次发送时间。 | 2 |

##### UBSHcomMultiRailOptions

1.  参数说明

[TABLE]

##### UBSHcomIov

1.  参数说明

| 参数名  | 数据类型 | 描述       | 默认值  |
|---------|----------|------------|---------|
| address | void \*  | 地址值。   | nullptr |
| size    | uint32_t | 数据大小。 | 0       |

##### UBSHcomOneSideSglRequest

1.  参数说明

| 参数名   | 数据类型              | 描述          | 默认值  |
|----------|-----------------------|---------------|---------|
| iov      | UBSHcomOneSideRequest | 单边iov数组。 | nullptr |
| iovCount | uint16_t              | iov数量。     | 0       |

##### UBSHcomMemoryKey

1.  参数说明

| 参数名      | 数据类型 | 默认值 | 描述                       |
|-------------|----------|--------|----------------------------|
| keys\[4\]   | uint64_t | \-     | key数组。                  |
| tokens\[4\] | uint64_t | \-     | UBC场景下的token value数组 |

##### UBSHcomSglRequest

1.  参数说明

| 参数名   | 数据类型       | 默认值  | 描述          |
|----------|----------------|---------|---------------|
| iov      | UBSHcomRequest | nullptr | 双边iov数组。 |
| iovCount | uint16_t       | 0       | iov数量。     |

##### UBSHcomTwoSideThreshold

1.  参数说明

| 参数名 | 数据类型 | 描述 | 默认值 |
|----|----|----|----|
| splitThreshold | uint32_t | UBC专用。此值表示拆包发送的阈值，也可以当做拆包发送时每个小包的最大长度(含额外头部)，一般将其配置成小于等于SegSize的值。可配置范围为 \[128, maxSendRecvDataSize\]，特别的配置成UINT32_MAX会禁用拆包功能。 | UINT32_MAX |
| rndvThreshold | uint32_t | rndv阈值，请求长度大于等于该值，则启用RNDV。 | UINT32_MAX |

#### 传输层结构体

##### UBSHcomNetDriverDeviceInfo

1.  参数说明

| 参数名 | 数据类型 | 描述                           |
|--------|----------|--------------------------------|
| maxSge | int      | 最大SGL数组元素个数，默认为4。 |

##### UBSHcomNetDriverOptions

1.  参数说明

[TABLE]

![](media/image8.png)

- UBS Comm默认开启TLS认证，关闭认证可能存在安全风险，用户可通过enableTls = false进行关闭。

- 双边操作允许发送最大消息的长度，可结合使用场景通过mrSendReceiveSegSize来配置。

##### UBSHcomNetOobListenerOptions

1.  参数说明

| 参数名 | 数据类型 | 描述 | 默认值 |
|----|----|----|----|
| ip | char\[16\] | 监听的IP。 | \- |
| port | uint16_t | 监听的端口号，默认是9980。范围是\[1024, 65535\]。 | 9980 |
| targetWorkerCount | uint16_t | 可用worker数量，0代表全部，默认是全部。 | UINT16_MAX |

##### UBSHcomNetOobUDSListenerOptions

1.  参数说明

| 参数名 | 数据类型 | 描述 | 默认值 |
|----|----|----|----|
| name | char\[96\] | 监听的UDS name。长度范围是(0, 96)。 | \- |
| perm | uint16_t | 0代表不使用文件，其他情况则使用文件，此参数为其权限，最高为0600。 | 0600 |
| targetWorkerCount | uint16_t | 可用worker数量，0代表全部，默认是全部。 | UINT16_MAX |
| isCheck | bool | 是否校验权限，默认值为true。 | true |

##### UBSHcomEpOptions

1.  参数说明

[TABLE]

##### UBSHcomNetTransRequest

1.  参数说明

| 配置项 | 类型 | 默认值 | 说明 |
|----|----|----|----|
| lAddress | uintptr_t | 0 | 本地缓存地址。 |
| rAddress | uintptr_t | 0 | 远程缓存地址。 |
| lKey | uint64_t | 0 | 本地内存区域key。 |
| rKey | uint64_t | 0 | 远程内存区域key。 |
| size | uint32_t | 0 | 缓存大小。有效范围为(0, UINT32_MAX\]。 |
| upCtxSize | uint16_t | 0 | 上下文大小。 |
| upCtxData | char\[64\] | \- | 上下文数据。 |
| srcSeg | void \* | nullptr | 仅UB场景使用，填写发送端的urma_target_seg_t \*指针。 |
| dstSeg | void \* | nullptr | 仅UB场景使用，填写目的端的urma_target_seg_t \*指针。 |

##### UBSHcomNetTransOpInfo

1.  参数说明

[TABLE]

##### UBSHcomNetUdsIdInfo

1.  参数说明

| 参数名 | 数据类型 | 描述     | 默认值 |
|--------|----------|----------|--------|
| pid    | uint32_t | 进程ID。 | 0      |
| uid    | uint32_t | 用户ID。 | 0      |
| gid    | uint32_t | 组ID。   | 0      |

##### UBSHcomNetMemoryAllocatorOptions

1.  参数说明

| 参数名 | 数据类型 | 描述 | 默认值 |
|----|----|----|----|
| address | uintptr_t | 内存地址。 | 0 |
| size | uint64_t | 内存大小。 | 0 |
| minBlockSize | uint32_t | 分配时最小单位大小(2的倍数)。范围是\[4096, 1073741824\]，单位是byte。 | 0 |
| bucketCount | uint32_t | 对齐的前提下，HashMap的桶数。 | 8192 |
| alignedAddress | bool | 是否对齐。 | false |
| cacheTierCount | uint16_t | 缓存器的层数。 | 8 |
| cacheBlockCountPerTier | uint16_t | 每层有多少个内存块。 | 16 |
| cacheTierPolicy | [UBSHcomNetMemoryAllocatorCacheTierPolicy](#ZH-CN_TOPIC_0000002465536242) | 分层策略，0为times，1为power。 | TIER_TIMES |

##### UBSHcomNetTransSglRequest

1.  参数说明

| 配置项 | 类型 | 默认值 | 说明 |
|----|----|----|----|
| \*iov | [UBSHcomNetTransSgeIov](#ZH-CN_TOPIC_0000002465376330) | Nullptr | 消息数组。该字段在内部有空指针校验。 |
| iovCount | uint16_t | 0 | 数组长度。最大为4。 |
| upCtxSize | uint16_t | 0 | 上下文大小。 |
| upCtxData\[16\] | char | \- | 上下文数据。 |

##### UBSHcomNetTransSgeIov

1.  参数说明

| 配置项 | 类型 | 默认值 | 说明 |
|----|----|----|----|
| lAddress | uintptr_t | 0 | 本端内存地址。 |
| rAddress | uintptr_t | 0 | 对端内存地址。 |
| lKey | uint64_t | 0 | 本端key。 |
| rKey | uint64_t | 0 | 对端key。 |
| size | uint32_t | 0 | 内存大小。 |
| memid | unsigned long | 0 | 显示Urmah在rndv中使用的obmm内存。 |
| srcSeg | void \* | nullptr | 仅UB场景使用，填写发送端的urma_target_seg_t \*指针。 |
| dstSeg | void \* | nullptr | 仅UB场景使用，填写目的端的urma_target_seg_t \*指针。 |

##### UBSHcomWorkerGroupInfo

1.  参数说明

| 配置项 | 类型 | 默认值 | 说明 |
|----|----|----|----|
| threadPriority | int8_t | 0 | 线程优先级。范围：\[-20, 19\] |
| threadCount | uint16_t | 1 | 线程总数。 |
| groupId | uint16_t | 0 | worker线程中的组ID。 |
| cpuIdsRange | std:pair\<uint32_t,uint32_t\> | \- | 指定worker线程CPU ID。 |

##### UBSHcomNetUdsIdInfo

1.  参数说明

| 配置项 | 类型     | 默认值 | 说明     |
|--------|----------|--------|----------|
| pid    | uint32_t | 0      | 进程ID。 |
| uid    | uint32_t | 0      | 用户ID。 |
| gid    | uint32_t | 0      | 组ID。   |

##### UBSHcomNetTransHeader

1.  参数说明

| 参数名 | 数据类型 | 描述 |
|----|----|----|
| headerCrc | uint32_t | crc值。 |
| opCode | int16_t | 用户定义的操作码。传输层范围\[0, 1023\]，service层范围\[0, 999\]。 |
| flags | uint16_t | 保留位。 |
| seqNo | uint32_t | 序列号。 |
| timeout | int16_t | 超时时间。 |
| errorCode | int16_t | 错误码。 |
| dataLength | uint32_t | 数据长度。 |
| immData | uint32_t | 立即数。 |
| extHeaderType | UBSHcomExtHeaderType | 传输层payload中是否存在服务层的头部，用户不使用。 |

1.  结构体函数定义

重置opcode、seqNo、errorCode和dataLenagth。

2.  实现说明

void Invalid();

3.  参数说明

无

4.  返回值

无

### C结构体

#### 服务层结构体

##### ubs_hcom_mr_info

1.  参数说明

| 参数名   | 数据类型             | 描述         |
|----------|----------------------|--------------|
| lAddress | uintptr_t            | mr内存地址。 |
| lKey     | ubs_hcom_oneside_key | mr key。     |
| size     | uint32_t             | mr内存大小。 |

##### ubs_hcom_channel_reply_context

1.  参数说明

| 参数名    | 数据类型 | 描述                     |
|-----------|----------|--------------------------|
| rspCtx    | void \*  | 用于回复的RSP上下文。    |
| errorCode | int16_t  | 失败场景下回复的错误码。 |

##### ubs_hcom_oneside_request

1.  参数说明

| 参数名称 | 数据类型             | 描述                                   |
|----------|----------------------|----------------------------------------|
| lAddress | uintptr_t            | 本地的地址。                           |
| rAddress | uintptr_t            | 远端的地址。                           |
| lKey     | ubs_hcom_oneside_key | 本地MR的key。                          |
| rKey     | ubs_hcom_oneside_key | 远端MR的key。                          |
| size     | uint32_t             | 数据大小。有效范围为(0, UINT32_MAX\]。 |

##### ubs_hcom_channel_callback

1.  参数说明

| 参数名 | 数据类型                 | 描述                 |
|--------|--------------------------|----------------------|
| cb     | ubs_hcom_channel_cb_func | 回调函数。           |
| arg    | void \*                  | 回调函数的参数指针。 |

##### ubs_hcom_flowctl_opts

1.  参数说明

[TABLE]

##### ubs_hcom_service_options

1.  参数说明

| 配置项 | 数据类型 | 默认值 | 说明 |
|----|----|----|----|
| mrSendReceiveSegSize | uint32_t | \- | 双边发送消息时，采用bcopy模式，发送端和接收端预留的内存大小。范围为(0, 524288000\]，单位byte。 |
| workerGroupId | uint16_t | \- | worker group编号。 |
| workerGroupThreadCount | uint16_t | \- | worker group内worker线程数量。 |
| workerGroupMode | ubs_hcom_service_worker_mode | \- | busy poll/event poll模式。 |
| workerThreadPriority | int8_t | \- | worker线程优先级设置，\[-20, 20\]，20为优先级最低，-20为优先级最高，0为不设置优先级。 |
| workerGroupCpuRange | char\[64\] | \- | worker group内worker线程cpu绑核id，例：'0-0'，为绑在cpu id 0上。ID为UINT32_MAX即为不绑。 |

![](media/image8.png)

UBS Comm默认开启TLS认证，关闭认证可能存在安全风险，用户可通过Service_SetUBSHcomTlsOptions函数进行关闭。

##### Service_UBSHcomConnectOptions

1.  参数说明

| 参数名 | 类型 | 默认值 | 说明 |
|----|----|----|----|
| clientGroupId | uint16_t | \- | client端worker group索引。 |
| serverGroupId | uint16_t | \- | server端worker group索引。 |
| linkCount | uint8_t | \- | channel内单个路径的ep数量。多路径场景下实际ep数量为linkCount \* 路径数。 |
| mode | ubs_hcom_service_polling_mode | \- | channel内ep poll模式。 |
| cbType | ubs_hcom_channel_cb_type | \- | cb方式，每次传入或全局同一个cb。 |
| payLoad | char\[512\] | \- | 用户可携带的自定义信息。 |

##### ubs_hcom_channel_request

1.  参数说明

| 参数名  | 类型     | 默认值 | 说明                 |
|---------|----------|--------|----------------------|
| address | void \*  | \-     | 消息内存首地址。     |
| size    | uint32_t | \-     | 消息大小。           |
| opcode  | uint16_t | \-     | 用户自定义的opcode。 |

##### ubs_hcom_channel_response

1.  参数说明

| 参数名    | 类型     | 默认值 | 说明                                          |
|-----------|----------|--------|-----------------------------------------------|
| address   | void \*  | \-     | 消息内存首地址。                              |
| size      | uint32_t | \-     | 消息大小。                                    |
| errorCode | uint16_t | \-     | 用户自定义的errorCode，对端回复时用户可填写。 |

##### Channel_UBSHcomTwoSideThreshold

1.  参数说明

[TABLE]

##### ubs_hcom_oneside_key

1.  参数说明

| 参数名 | 类型 | 默认值 | 说明 |
|----|----|----|----|
| keys | uint64_t\[4\] | \- | 已注册内存的key。多路径场景每个路径有一个key，单路径场景只使用key\[0\]。 |

#### 传输层结构体

##### ubs_hcom_send_request

1.  参数说明

| 参数名    | 类型       | 默认值 | 说明                       |
|-----------|------------|--------|----------------------------|
| data      | uintptr_t  | 0      | 准备发送给对方的数据地址。 |
| size      | uint32_t   | 0      | 数据大小。                 |
| upCtxSize | uint16_t   | 0      | 用户上下文大小。           |
| upCtxData | char\[16\] | \-     | 用户上下文。               |

##### ubs_hcom_opinfo

1.  参数说明

| 参数名参数说明 | 数据类型 | 描述 |
|----|----|----|
| seqNo | uint32_t | 序列号。范围是\[0, 1023\]。 |
| timeout | uint16_t | 超时时间，单位为秒。0为立刻超时，负数为永不超时。范围\[-1, 1200\]。 |
| errorCode | int16_t | 错误码。 |
| flags | uint8_t | 标志位。 |

##### ubs_hcom_device_info

1.  参数说明

| 参数名 | 数据类型 | 描述                                 |
|--------|----------|--------------------------------------|
| maxSge | int      | RDMA设备信息，最大的SGL的iov count。 |

##### ubs_hcom_readwrite_request

1.  参数说明

| 参数名    | 数据类型   | 描述               |
|-----------|------------|--------------------|
| lMRA      | uintptr_t  | 本端MR的地址。     |
| rMRA      | uintptr_t  | 远端MR的地址。     |
| lKey      | uint64_t   | 本端MR的密钥。     |
| rKey      | uint64_t   | 远端MR的密钥。     |
| size      | uint32_t   | 数据大小。         |
| upCtxSize | uint16_t   | 用户上下文的大小。 |
| upCtxData | char\[16\] | 用户上下文。       |

##### ubs_hcom_readwrite_sge

1.  参数说明

| 参数名   | 数据类型  | 描述           |
|----------|-----------|----------------|
| lAddress | uintptr_t | 本端MR的地址。 |
| rAddress | uintptr_t | 远端MR的地址。 |
| lKey     | uint64_t  | 本端MR的密钥。 |
| rKey     | uint64_t  | 远端MR的密钥。 |
| size     | uint32_t  | 数据大小。     |

##### ubs_hcom_readwrite_request_sgl

1.  参数说明

| 配置项 | 类型 | 默认值 | 说明 |
|----|----|----|----|
| \*iov | [Net_ReadWriteSge](#ZH-CN_TOPIC_0000002465376958) | \- | 消息数组。 |
| iovCount | uint16_t | \- | 小于max count(NET_SGE_MAX_IOV)。 |
| upCtxSize | uint16_t | \- | 上下文大小。 |
| upCtxData\[16\] | char | \- | 上下文数据。 |

##### ubs_hcom_memory_region_info

1.  参数说明

| 参数名   | 数据类型  | 描述       |
|----------|-----------|------------|
| lAddress | uintptr_t | MR的地址。 |
| lKey     | uint64_t  | MR的key。  |
| size     | uint32_t  | MR的大小。 |

##### ubs_hcom_request_context

1.  参数说明

| 参数名 | 数据类型 | 描述 |
|----|----|----|
| type | [Net_RequestType](#ZH-CN_TOPIC_0000002498615389) | 请求的操作类型\[0, 8\]。 |
| opCode | uint16_t | 操作码，取值范围\[0, 1023\]。 |
| flags | uint16_t | header中的标志位。 |
| timeout | int16_t | 超时时间。 |
| errorCode | int16_t | 错误码。 |
| result | int | 结果值0代表成功。 |
| msgData | void \* | 数据指针。用于接收操作。 |
| msgSize | uint32_t | 数据大小。用于接收操作。 |
| seqNo | uint32_t | 序列号。用于post send raw。 |
| ep | ubs_hcom_endpoint | 建链创建好的EP对象。 |
| originalSend | [Net_SendRequest](#ZH-CN_TOPIC_0000002465536686) | 用于C_OP_REQUEST_POSTED复制的结构体信息。 |
| originalReq | [Net_ReadWriteRequest](#ZH-CN_TOPIC_0000002465536694) | 用于C_OP_READWRITE_DONE复制的结构体信息。 |
| originalSglReq | [Net_ReadWriteSglRequest](#ZH-CN_TOPIC_0000002498495605) | 用于C_OP_READWRITE_DONE复制的结构体信息。 |

##### ubs_hcom_response_context

1.  参数说明

| 参数名  | 数据类型 | 描述           |
|---------|----------|----------------|
| opCode  | uint16_t | 操作编号。     |
| seqNo   | uint32_t | 序列号。       |
| msgData | void \*  | 接收到的消息。 |
| msgSize | uint32_t | 消息长度。     |

##### ubs_hcom_uds_id_info

1.  参数说明

| 参数名 | 数据类型 | 描述     |
|--------|----------|----------|
| pid    | uint32_t | 进程ID。 |
| uid    | uint32_t | 用户ID。 |
| gid    | uint32_t | 组ID。   |

##### ubs_hcom_driver_opts

1.  参数说明

[TABLE]

![](media/image8.png)

UBS Comm默认开启TLS认证，关闭认证可能存在安全风险，用户可通过enableTls = false进行关闭。

##### ubs_hcom_driver_listen_opts

1.  参数说明

| 参数名 | 数据类型 | 描述 |
|----|----|----|
| name | char\[16\] | 监听的IP地址。长度范围是(0, 16\]。 |
| port | uint16_t | 监听的端口号。范围是\[1024, 65535\]。 |
| targetWorkerCount | uint16_t | 可用worker数量，0代表全部，默认可使用worker数量为全部。 |

##### ubs_hcom_driver_uds_listen_opts

1.  参数说明

| 参数名 | 数据类型 | 描述 |
|----|----|----|
| name | char\[96\] | 监听的UDS name。长度范围(0, 96)。 |
| perm | uint16_t | 0代表不使用文件，其他情况则使用文件，此参数为其权限，最高为0600。 |
| targetWorkerCount | uint16_t | 可用worker数量，0代表全部，默认是全部。 |

##### ubs_hcom_memory_allocator_options

1.  参数说明

[TABLE]

## 枚举值参考

### C++枚举值

#### 服务层枚举值

##### UBSHcomChannelBrokenPolicy

1.  枚举说明

断链策略，如[表3-307](#d1e92463)所示。

1.  枚举说明

| 枚举名     | 数值 | 描述                                               |
|------------|------|----------------------------------------------------|
| BROKEN_ALL | 0    | 当一个EP断开则断开channel。                        |
| RECONNECT  | 1    | 当一个EP断开尝试重连，若失败则断开channel。        |
| KEEP_ALIVE | 2    | 当一个EP断开，保持其他EP正常功能，直至所有EP断开。 |

##### Operation

1.  枚举说明

此NetServiceContext所包含的操作类别，如[表3-308](#d1e92547)所示。

1.  枚举说明

| 枚举名              | 数值 | 描述              |
|---------------------|------|-------------------|
| SER_RECEIVED        | 0    | 接收到新消息。    |
| SER_RECEIVED_RAW    | 1    | 接收到新raw消息。 |
| SER_SENT            | 2    | 消息发送完成。    |
| SER_SENT_RAW        | 3    | raw消息发送完成。 |
| SER_ONE_SIDE        | 4    | 单边操作完成。    |
| SER_INVALID_OP_TYPE | 255  | 非法操作。        |

##### UBSHcomClientPollingMode

1.  枚举说明

客户端poll模式

1.  枚举说明

| 枚举名      | 数值 | 描述                         |
|-------------|------|------------------------------|
| WORKER_POLL | 0    | 使用worker线程poll。         |
| SELF_POLL   | 1    | 使用调用通信接口的线程poll。 |
| UNKNOWN     | 255  | 未知。                       |

##### UBSHcomChannelCallBackType

1.  枚举说明

Channel的回调函数类型，如[表3-310](#d1e92742)所示。

1.  枚举说明

| 枚举名            | 数值 | 描述                                       |
|-------------------|------|--------------------------------------------|
| CHANNEL_FUNC_CB   | 0    | 会使用用户传入到异步通信方法中的回调函数。 |
| CHANNEL_GLOBAL_CB | 1    | 会使用注册给NetService的回调函数。         |

##### UBSHcomFlowCtrlLevel

1.  枚举说明

Channel的流控等待策略，如[表3-311](#d1e92816)所示。

1.  枚举说明

| 枚举名           | 数值 | 描述               |
|------------------|------|--------------------|
| HIGH_LEVEL_BLOCK | 0    | 忙循环等待。       |
| LOW_LEVEL_BLOCK  | 1    | 睡眠指定时长等待。 |

##### UBSHcomChannelState

1.  枚举说明

Channel状态，如[表3-312](#d1e92890)所示。

1.  枚举说明

| 枚举名         | 数值 | 描述       |
|----------------|------|------------|
| CH_NEW         | 0    | 新建状态。 |
| CH_ESTABLISHED | 1    | 就绪状态。 |
| CH_CLOSE       | 2    | 关闭状态。 |
| CH_DESTROY     | 3    | 销毁状态。 |

##### UBSHcomOobType

1.  枚举说明

建链类型，如[表3-313](#d1e92984)所示。

1.  枚举说明

| 枚举名 | 数值 | 描述          |
|--------|------|---------------|
| TCP    | 0    | TCP建链方式。 |
| UDS    | 1    | UDS建链方式。 |

##### UBSHcomSecType

1.  枚举说明

建链安全校验，如[表3-314](#d1e93058)所示。

1.  枚举说明

| 枚举名                | 数值 | 描述       |
|-----------------------|------|------------|
| NET_SEC_DISABLED      | 0    | 不校验。   |
| NET_SEC_VALID_ONE_WAY | 1    | 单边校验。 |
| NET_SEC_VALID_TWO_WAY | 2    | 双边校验。 |

#### 传输层枚举值

##### UBSHcomNetEndPointState

1.  枚举说明

描述EP此时所处的状态，如[表3-315](#d1e93169)所示。

1.  枚举说明

| 枚举名          | 数值 | 描述       |
|-----------------|------|------------|
| NEP_NEW         | 0    | 新建状态。 |
| NEP_ESTABLISHED | 1    | 就绪状态。 |
| NEP_BROKEN      | 2    | 断开状态。 |
| NEP_BUFF        | 3    | \-         |

##### UBSHcomNetCipherSuite

1.  枚举说明

加密算法，如[表3-316](#d1e93263)所示。

1.  枚举说明

| 枚举名            | 数值 | 描述                |
|-------------------|------|---------------------|
| AES_GCM_128       | 0    | AES_GCM_128。       |
| AES_GCM_256       | 1    | AES_GCM_256。       |
| AES_CCM_128       | 2    | AES_CCM_128。       |
| CHACHA20_POLY1305 | 3    | CHACHA20_POLY1305。 |

##### UBSHcomTlsVersion

1.  枚举说明

TLS的版本信息，如[表3-317](#d1e93357)所示。

1.  枚举说明

| 枚举名  | 数值 | 描述      |
|---------|------|-----------|
| TLS_1_2 | 771  | 1.2版本。 |
| TLS_1_3 | 772  | 1.3版本。 |

##### NN_OpType

1.  枚举说明

此UBSHcomNetRequestContext所包含的操作类别，如[表3-318](#d1e93431)所示。

1.  枚举说明

| 枚举名             | 数值 | 描述              |
|--------------------|------|-------------------|
| NN_SENT            | 0    | 消息发送完成。    |
| NN_SENT_RAW        | 1    | raw消息发送完成。 |
| NN_SENT_RAW_SGL    | 2    | SGL消息发送完成。 |
| NN_RECEIVED        | 3    | 接收到新消息。    |
| NN_RECEIVED_RAW    | 4    | 接收到新raw消息。 |
| NN_WRITTEN         | 5    | 写操作完成。      |
| NN_READ            | 6    | 读操作完成。      |
| NN_SGL_WRITTEN     | 7    | SGL写操作完成。   |
| NN_SGL_READ        | 8    | SGL读操作完成。   |
| NN_INVALID_OP_TYPE | 255  | 非法操作。        |

##### UBSHcomNetMemoryAllocatorType

1.  枚举说明

内存分配器类型，如[表3-319](#d1e93585)所示。

1.  枚举说明

| 枚举名                  | 数值 | 描述                   |
|-------------------------|------|------------------------|
| DYNAMIC_SIZE            | 0    | 动态大小。             |
| DYNAMIC_SIZE_WITH_CACHE | 1    | 动态大小，配有缓存器。 |

##### UBSHcomNetMemoryAllocatorCacheTierPolicy

1.  枚举说明

内存分配器的缓存器分级策略，如[表3-320](#d1e93659)所示。

1.  枚举说明

| 枚举名     | 数值 | 描述                    |
|------------|------|-------------------------|
| TIER_TIMES | 0    | 基准值的倍数策略。      |
| TIER_POWER | 1    | 基准值乘以2的幂数策略。 |

##### UBSHcomPeerCertVerifyType

1.  枚举说明

对端校验类型，如[表3-321](#d1e93733)所示。

1.  枚举说明

| 枚举名                | 数值 | 描述                           |
|-----------------------|------|--------------------------------|
| VERIFY_BY_NONE        | 0    | 对端不需要校验。               |
| VERIFY_BY_DEFAULT     | 1    | 对端使用UBS Comm内部校验方式。 |
| VERIFY_BY_CUSTOM_FUNC | 2    | 对端使用用户定义的校验方式。   |

##### UBSHcomNetDriverSecType

1.  枚举说明

UBSHcomNetDriver校验类型，如[表3-322](#d1e93817)所示。

1.  枚举说明

| 枚举名                | 数值 | 描述                           |
|-----------------------|------|--------------------------------|
| NET_SEC_DISABLED      | 0    | 不需要校验。                   |
| NET_SEC_VALID_ONE_WAY | 1    | 单边校验，仅服务端校验客户端。 |
| NET_SEC_VALID_TWO_WAY | 2    | 双边校验。                     |

##### NetDriverOobType

1.  枚举说明

OOB建链时协议，如[表3-323](#d1e93902)所示。

1.  枚举说明

| 枚举名      | 数值 | 描述                             |
|-------------|------|----------------------------------|
| NET_OOB_TCP | 0    | TCP协议。                        |
| NET_OOB_UDS | 1    | UDS协议。                        |
| NET_OOB_UB  | 2    | UBC自举建链，仅支持UBC协议配置。 |

##### UBSHcomNetDriverWorkingMode

1.  枚举说明

worker线程工作模式，如[表3-324](#d1e93986)所示。

1.  枚举说明

| 枚举名            | 数值 | 描述            |
|-------------------|------|-----------------|
| NET_BUSY_POLLING  | 0    | busy polling。  |
| NET_EVENT_POLLING | 1    | event polling。 |

##### UBSHcomNetDriverLBPolicy

1.  枚举说明

worker线程分配策略，如[表3-325](#d1e94060)所示。

1.  枚举说明

| 枚举名           | 数值 | 描述                               |
|------------------|------|------------------------------------|
| NET_ROUND_ROBIN  | 0    | 轮询策略。                         |
| NET_HASH_IP_PORT | 1    | 根据IP和Port进行取哈希值分配策略。 |

##### UBSHcomNetDriverProtocol

1.  枚举说明

UBSHcomNetDriver通信时协议。

| 枚举名       | 数值 | 描述                 |
|--------------|------|----------------------|
| RDMA         | 0    | RDMA。               |
| TCP          | 1    | TCP。                |
| UDS          | 2    | UDS。                |
| SHM          | 3    | SHM。                |
| RDMA_MLX5_RC | 4    | 需求MLX5网卡的RDMA。 |
| UBC          | 7    | UBC。                |
| HSHMEM       | 8    | HSHMEM。             |
| UNKNOWN      | 255  | 不支持协议。         |

##### UBSHcomUbcMode

1.  枚举说明

UBC协议专用能力。UB-C 具有多路径能力，发送时使用多条路径可以增大带宽，对于带宽要求不高、时延敏感型业务又提供单路径直连模式。

| 枚举名        | 数值 | 描述                           |
|---------------|------|--------------------------------|
| Disabled      | -1   | 禁用多路径能力（默认）。       |
| LowLatency    | 0    | 低时延模式，使用单路径发送。   |
| HighBandwidth | 1    | 高带宽模式，使用多条路径发送。 |

### C枚举值

#### 服务层枚举值

##### ubs_hcom_channel_cb_type

1.  枚举说明

Channel的回调函数类型，如[表3-326](#d1e94396)所示。

1.  枚举说明

| 枚举名              | 数值 | 描述                                       |
|---------------------|------|--------------------------------------------|
| C_CHANNEL_FUNC_CB   | 0    | 会使用用户传入到异步通信方法中的回调函数。 |
| C_CHANNEL_GLOBAL_CB | 1    | 会使用注册给NetService的回调函数。         |

##### ubs_hcom_service_context_type

1.  枚举说明

此NetServiceContext所包含的操作类别，如[表3-327](#d1e94470)所示。

1.  枚举说明

| 枚举名              | 数值 | 描述              |
|---------------------|------|-------------------|
| SER_RECEIVED        | 0    | 接收到新消息。    |
| SER_RECEIVED_RAW    | 1    | 接收到新raw消息。 |
| SER_SENT            | 2    | 消息发送完成。    |
| SER_SENT_RAW        | 3    | raw消息发送完成。 |
| SER_ONE_SIDE        | 4    | 单边操作完成。    |
| SERVICE_RNDV        | 5    | rndv请求。        |
| SER_INVALID_OP_TYPE | 255  | 非法操作。        |

##### ubs_hcom_channel_flowctl_level

1.  枚举说明

Channel的流控等待策略，如[表3-328](#d1e94594)所示。

1.  枚举说明

| 枚举名           | 数值 | 描述               |
|------------------|------|--------------------|
| HIGH_LEVEL_BLOCK | 0    | 忙循环等待。       |
| LOW_LEVEL_BLOCK  | 1    | 睡眠指定时长等待。 |

##### ubs_hcom_service_worker_mode

1.  枚举说明

worker线程工作模式，如[表3-329](#d1e94668)所示。

1.  枚举说明

| 枚举名                  | 数值 | 描述            |
|-------------------------|------|-----------------|
| C_SERVICE_BUSY_POLLING  | 0    | busy polling。  |
| C_SERVICE_EVENT_POLLING | 1    | event polling。 |

##### ubs_hcom_service_lb_policy

1.  枚举说明

worker线程分配策略，如[表3-330](#d1e94742)所示。

1.  枚举说明

| 枚举名               | 数值 | 描述                               |
|----------------------|------|------------------------------------|
| SERVICE_ROUND_ROBIN  | 0    | 轮询策略。                         |
| SERVICE_HASH_IP_PORT | 1    | 根据IP和Port进行取哈希值分配策略。 |

##### ubs_hcom_service_cipher_suite

1.  枚举说明

加密算法，如[表3-331](#d1e94816)所示。

1.  枚举说明

| 枚举名                      | 数值 | 描述                |
|-----------------------------|------|---------------------|
| C_SERVICE_AES_GCM_128       | 0    | AES_GCM_128。       |
| C_SERVICE_AES_GCM_256       | 1    | AES_GCM_256。       |
| C_SERVICE_AES_CCM_128       | 2    | AES_CCM_128。       |
| C_SERVICE_CHACHA20_POLY1305 | 3    | CHACHA20_POLY1305。 |

##### ubs_hcom_service_tls_version

1.  枚举说明

TLS的版本信息，如[表3-332](#d1e94910)所示。

1.  枚举说明

| 枚举名            | 数值 | 描述      |
|-------------------|------|-----------|
| C_SERVICE_TLS_1_2 | 771  | 1.2版本。 |
| C_SERVICE_TLS_1_3 | 772  | 1.3版本。 |

##### ubs_hcom_service_secure_type

1.  枚举说明

NetService校验类型，如[表3-333](#d1e94984)所示。

1.  枚举说明

| 枚举名                          | 数值 | 描述                           |
|---------------------------------|------|--------------------------------|
| C_SERVICE_NET_SEC_DISABLED      | 0    | 不需要校验。                   |
| C_SERVICE_NET_SEC_VALID_ONE_WAY | 1    | 单边校验，仅服务端校验客户端。 |
| C_SERVICE_NET_SEC_VALID_TWO_WAY | 2    | 双边校验。                     |

##### ubs_hcom_service_channel_policy

1.  枚举说明

Channel断链策略，如[表3-334](#d1e95069)所示。

1.  枚举说明

| 枚举名 | 数值 | 描述 |
|----|----|----|
| C_CHANNEL_BROKEN_ALL | 0 | 当一个EP断开则断开channel。 |
| C_CHANNEL_RECONNECT | 1 | 当一个EP断开尝试重连，若失败则断开channel。 |
| C_CHANNEL_KEEP_ALIVE | 2 | 当一个EP断开，保持其他EP正常功能，直至所有EP断开。 |

##### ubs_hcom_service_channel_handler_type

1.  枚举说明

链路相关的回调函数类型，如[表3-335](#d1e95153)所示。

1.  枚举说明

| 枚举名           | 数值 | 描述               |
|------------------|------|--------------------|
| C_CHANNEL_NEW    | 0    | 新建链的回调函数。 |
| C_CHANNEL_BROKEN | 1    | 断链的回调函数。   |

##### ubs_hcom_service_handler_type

1.  枚举说明

通信相关的回调函数类型，如[表3-336](#d1e95227)所示。

1.  枚举说明

| 枚举名                     | 数值 | 描述                     |
|----------------------------|------|--------------------------|
| C_SERVICE_REQUEST_RECEIVED | 0    | 接收新消息的回调函数。   |
| C_SERVICE_REQUEST_POSTED   | 1    | 消息发送完成的回调函数。 |
| C_SERVICE_READWRITE_DONE   | 2    | 读写完成的回调函数。     |

##### ubs_hcom_service_type

1.  枚举说明

NetService通信时协议，如[表3-337](#d1e95311)所示。

1.  枚举说明

| 枚举名           | 数值 | 描述                         |
|------------------|------|------------------------------|
| C_SERVICE_RDMA   | 0    | RDMA。                       |
| C_SERVICE_TCP    | 1    | TCP。                        |
| C_SERVICE_UDS    | 2    | UDS。                        |
| C_SERVICE_SHM    | 3    | SHM。                        |
| C_SERVICE_UBC    | 6    | UBC。                        |
| C_SERVICE_HSHMEM | 7    | HSHMEM（北冥版本暂不支持）。 |

##### ubs_hcom_service_polling_mode

#### 传输层枚举值

##### ubs_hcom_request_type

1.  枚举说明

此UBSHcomNetRequestContext所包含的操作类别，如[表3-338](#d1e95479)所示。

1.  枚举说明

| 枚举名         | 数值 | 描述              |
|----------------|------|-------------------|
| C_SENT         | 0    | 消息发送完成。    |
| C_SENT_RAW     | 1    | raw消息发送完成。 |
| C_SENT_RAW_SGL | 2    | SGL消息发送完成。 |
| C_RECEIVED     | 3    | 接收到新消息。    |
| C_RECEIVED_RAW | 4    | 接收到新raw消息。 |
| C_WRITTEN      | 5    | 写操作完成。      |
| C_READ         | 6    | 读操作完成。      |
| C_SGL_WRITTEN  | 7    | SGL写操作完成。   |
| C_SGL_READ     | 8    | SGL读操作完成。   |

##### ubs_hcom_driver_working_mode

1.  枚举说明

worker线程工作模式，如[表3-339](#d1e95623)所示。

1.  枚举说明

| 枚举名          | 数值 | 描述            |
|-----------------|------|-----------------|
| C_BUSY_POLLING  | 0    | busy polling。  |
| C_EVENT_POLLING | 1    | event polling。 |

##### ubs_hcom_driver_type

1.  枚举说明

UBSHcomNetDriver通信时协议，如[表3-340](#d1e95697)所示。

1.  枚举说明

| 枚举名        | 数值 | 描述   |
|---------------|------|--------|
| C_DRIVER_RDMA | 0    | RDMA。 |
| C_DRIVER_TCP  | 1    | TCP。  |
| C_DRIVER_UDS  | 2    | UDS。  |
| C_DRIVER_SHM  | 3    | SHM。  |
| C_DRIVER_UBC  | 6    | UBC。  |

##### ubs_hcom_driver_oob_type

1.  枚举说明

OOB建链时协议，如[表3-341](#d1e95801)所示。

1.  枚举说明

| 枚举名        | 数值 | 描述      |
|---------------|------|-----------|
| C_NET_OOB_TCP | 0    | TCP协议。 |
| C_NET_OOB_UDS | 1    | UDS协议。 |

##### ubs_hcom_driver_sec_type

1.  枚举说明

UBSHcomNetDriver校验类型，如[表3-342](#d1e95875)所示。

1.  枚举说明

| 枚举名                  | 数值 | 描述                           |
|-------------------------|------|--------------------------------|
| C_NET_SEC_DISABLED      | 0    | 不需要校验。                   |
| C_NET_SEC_VALID_ONE_WAY | 1    | 单边校验，仅服务端校验客户端。 |
| C_NET_SEC_VALID_TWO_WAY | 2    | 双边校验。                     |

##### ubs_hcom_driver_tls_version

1.  枚举说明

TLS的版本信息，如[表3-343](#d1e95959)所示。

1.  枚举说明

| 枚举名    | 数值 | 描述      |
|-----------|------|-----------|
| C_TLS_1_2 | 771  | 1.2版本。 |
| C_TLS_1_3 | 772  | 1.3版本。 |

##### ubs_hcom_driver_cipher_suite

1.  枚举说明

加密算法，如[表3-344](#d1e96033)所示。

1.  枚举说明

| 枚举名              | 数值 | 描述                |
|---------------------|------|---------------------|
| C_AES_GCM_128       | 0    | AES_GCM_128。       |
| C_AES_GCM_256       | 1    | AES_GCM_256。       |
| C_AES_CCM_128       | 2    | AES_CCM_128。       |
| C_CHACHA20_POLY1305 | 3    | CHACHA20_POLY1305。 |

##### ubs_hcom_peer_cert_verify_type

1.  枚举说明

对端校验类型，如[表3-345](#d1e96127)所示。

1.  枚举说明

| 枚举名                  | 数值 | 描述                           |
|-------------------------|------|--------------------------------|
| C_VERIFY_BY_NONE        | 0    | 对端不需要校验。               |
| C_VERIFY_BY_DEFAULT     | 1    | 对端使用UBS Comm内部校验方式。 |
| C_VERIFY_BY_CUSTOM_FUNC | 2    | 对端使用用户定义的校验方式。   |

##### ubs_hcom_memory_allocator_cache_tier_policy

1.  枚举说明

内存分配器的缓存器分级策略，如[表3-346](#d1e96212)所示。

1.  枚举说明

| 枚举名       | 数值 | 描述                    |
|--------------|------|-------------------------|
| C_TIER_TIMES | 0    | 基准值的倍数策略。      |
| C_TIER_POWER | 1    | 基准值乘以2的幂数策略。 |

##### ubs_hcom_memory_allocator_type

1.  枚举说明

内存分配器类型，如[表3-347](#d1e96286)所示。

1.  枚举说明

| 枚举名                    | 数值 | 描述                   |
|---------------------------|------|------------------------|
| C_DYNAMIC_SIZE            | 0    | 动态大小。             |
| C_DYNAMIC_SIZE_WITH_CACHE | 1    | 动态大小，配有缓存器。 |

##### ubs_hcom_ep_handler_type

1.  枚举说明

链路相关的回调函数类型，如[表3-348](#d1e96360)所示。

1.  枚举说明

| 枚举名      | 数值 | 描述               |
|-------------|------|--------------------|
| C_EP_NEW    | 0    | 新建链的回调函数。 |
| C_EP_BROKEN | 1    | 断链的回调函数。   |

##### ubs_hcom_op_handler_type

1.  枚举说明

通信相关的回调函数类型，如[表3-349](#d1e96434)所示。

1.  枚举说明

| 枚举名                | 数值 | 描述                     |
|-----------------------|------|--------------------------|
| C_OP_REQUEST_RECEIVED | 0    | 接收新消息的回调函数。   |
| C_OP_REQUEST_POSTED   | 1    | 消息发送完成的回调函数。 |
| C_OP_READWRITE_DONE   | 2    | 读写完成的回调函数。     |

##### ubs_hcom_polling_mode

1.  枚举说明

| 枚举名                 | 数值 | 描述            |
|------------------------|------|-----------------|
| NET_C_EP_SELF_POLLING  | 0    | self ep模式。   |
| NET_C_EP_EVENT_POLLING | 1    | 非self ep模式。 |

##### ubs_hcom_service_polling_mode

1.  枚举说明

client poll信息模式，如下表所示。

1.  枚举说明

| 枚举名               | 数值 | 描述               |
|----------------------|------|--------------------|
| C_CLIENT_WORKER_POLL | 0    | 非self poll 模式。 |
| C_CLIENT_SELF_POLL   | 1    | self poll模式。    |

# 环境变量参考

1.  环境变量参数

[TABLE]

环境变量设置示例如下：

export HCOM_FILE_PATH_PREFIX="/home/uds/socket/file"  
export HCOM_OPENSSL_PATH="/home/openssl"  
export HCOM_TRACE_LEVEL=0  
export HCOM_QP_TRAFFIC_CLASS=106  
export HCOM_SHM_EXCHANGE_FD_QUEUE_SIZE=10  
export HCOM_CONNECTION_RETRY_TIMES=5  
export HCOM_CONNECTION_RETRY_INTERVAL_SEC=2  
export HCOM_SET_LOG_LEVEL=1

# 错误码

[5.1 服务层错误码](#服务层错误码)

[5.2 传输层错误码](#传输层错误码)

[5.3 RDMA协议错误码](#rdma协议错误码)

## 服务层错误码

1.  服务层错误码

| 错误码数 | 错误码                            | 含义                           |
|----------|-----------------------------------|--------------------------------|
| 0        | SER_OK                            | 成功。                         |
| 500      | SER_ERROR                         | 内部错误。                     |
| 501      | SER_INVALID_PARAM                 | 无效参数。                     |
| 502      | SER_NEW_OBJECT_FAILED             | 对象生成失败。                 |
| 503      | SER_CREATE_TIMEOUT_THREAD_FAILED  | 创建超时处理线程失败。         |
| 504      | SER_NEW_MESSAGE_DATA_FAILED       | 生成消息失败。                 |
| 505      | SER_NOT_ESTABLISHED               | NetChannel未建链。             |
| 506      | SER_STORE_SEQ_DUP                 | 序列号重复。                   |
| 507      | SER_STORE_SEQ_NO_FOUND            | 序列号不存在。                 |
| 508      | SER_RSP_SIZE_TOO_SMALL            | 消息大小不一致。               |
| 509      | SER_TIMEOUT                       | 超时。                         |
| 510      | SER_TIMER_NOT_WORK                | 超时处理线程开启失败。         |
| 511      | SER_NOT_ENABLE_RNDV               | 开启Rndv失败。                 |
| 512      | SER_RNDV_FAILED_BY_PEER           | 对端使用Rndv失败。             |
| 513      | SER_CHANNEL_ID_DUP                | Channel Id重复。               |
| 514      | SER_EP_NOT_BROKEN_ALL             | NetChannel中所有EP未发生断链。 |
| 515      | SER_CHANNEL_NOT_EXIST             | NetChannel不存在。             |
| 516      | SER_CHANNEL_RECONNECT_OVER_WINDOW | \-                             |
| 517      | SER_EP_BROKEN_DURING_CONNECTING   | NetChannel中所有EP均断链。     |
| 518      | SER_NOT_SUPPORT_SERVER_RECONNECT  | 不支持重建链。                 |
| 519      | SER_STOP                          | 服务停止。                     |
| 520      | SER_NULL_INSTANCE                 | 空指针。                       |
| 521      | SER_UNSUPPORTED                   | 不支持的操作。                 |
| 522      | SER_INVALID_IP                    | 非法IP。                       |
| 523      | SER_MALLOC_FAILED                 | 分配内存失败。                 |
| 524      | SER_SPLIT_INVALID_MSG             | 拆包消息无效。                 |

## 传输层错误码

1.  传输层错误码

[TABLE]

![](media/image8.png)

部分常见错误码详细说明：

- 114：在RDMA和TCP协议的双边非SGL通信方式时，为了发送消息的持久化和RDMA特性需求，需要把用户发送的消息内容拷贝到UBS Comm内部预申请的内存中。但是在并发很大的情况下，可能将预申请的内存耗尽，在耗尽的时候如果再发送双边非SGL消息时就会产生此错误码。解决方式可以是调大UBSHcomNetDriverOptions中的mrSendReceiveSegCount参数来扩大预申请内存；如果是对端接收压力过大导致本端发送也可以调整对端接收队列的长度prePostReceiveSizePerQP。

- 128：在进行建链的时候客户端建链失败时会返回此错误。请检查服务端是否启动并且启动监听线程，然后检查客户端发起建链的IP地址和端口是否和服务端监听的一致，推荐先启动服务端，再使用客户端去建链。

## RDMA协议错误码

1.  RDMA协议错误码

| 错误码数 | 错误码 | 含义 |
|----|----|----|
| 0 | RR_OK | 成功。 |
| 200 | RR_PARAM_INVALID | 参数无效。 |
| 201 | RR_MEMORY_ALLOCATE_FAILED | 分配内存失败。 |
| 202 | RR_NEW_OBJECT_FAILED | 创建对象失败。 |
| 203 | RR_OPEN_FILE_FAILED | 打开文件失败。 |
| 204 | RR_READ_FILE_FAILED | 读取文件失败。 |
| 205 | RR_DEVICE_FAILED_OPEN | 得到RDMA设备失败。 |
| 206 | RR_DEVICE_INDEX_OVERFLOW | RDMA设备序号异常。 |
| 207 | RR_DEVICE_OPEN_FAILED | 打开RDMA设备失败。 |
| 208 | RR_DEVICE_FAILED_GET_IF_ADDRESS | 获得网卡地址失败。 |
| 209 | RR_DEVICE_NO_IF_MATCHED | 获得符合IP地址的网卡地址失败。 |
| 210 | RR_DEVICE_NO_IF_TO_GID_MATCHED | 获得符合IP地址的RDMA设备GID。 |
| 211 | RR_DEVICE_INVALID_IP_MASK | IP地址掩码异常。 |
| 212 | RR_MR_REG_FAILED | Memory Region(MR)注册失败。 |
| 213 | RR_CQ_NOT_INITIALIZED | Completion Queue(CQ)初始化失败。 |
| 214 | RR_CQ_POLLING_FAILED | Poll CQ方法异常。 |
| 215 | RR_CQ_POLLING_TIMEOUT | Poll CQ超时。 |
| 216 | RR_CQ_POLLING_ERROR_RESULT | Poll CQ结果错误。 |
| 217 | RR_CQ_POLLING_UNMATCHED_OPCODE | Poll CQ结果opcode不匹配。 |
| 218 | RR_CQ_EVENT_GET_FAILED | Poll事件失败。 |
| 219 | RR_CQ_EVENT_NOTIFY_FAILED | 通知CQ失败。 |
| 220 | RR_CQ_WC_WRONG | poll CQ后的完成事件的状态异常。 |
| 221 | RR_CQ_EVENT_GET_TIMOUT | poll CQ超时。 |
| 222 | RR_QP_CREATE_FAILED | 创建Queue Pair(QP)失败。 |
| 223 | RR_QP_NOT_INITIALIZED | 初始化QP失败。 |
| 224 | RR_QP_CHANGE_STATE_FAILED | 更新QP状态失败。 |
| 225 | RR_QP_POST_RECEIVE_FAILED | 发起接收请求失败。 |
| 226 | RR_QP_POST_SEND_FAILED | 发起发送请求失败。 |
| 227 | RR_QP_POST_READ_FAILED | 发起读取请求失败。 |
| 228 | RR_QP_POST_WRITE_FAILED | 发起写请求失败。 |
| 229 | RR_QP_RECEIVE_CONFIG_ERR | 收发相关参数设定失败。 |
| 230 | RR_QP_POST_SEND_WR_FULL | 发送队列满。 |
| 231 | RR_QP_ONE_SIDE_WR_FULL | 单边请求队列满。 |
| 232 | RR_QP_CTX_FULL | 上下文耗尽。 |
| 233 | RR_QP_CHANGE_ERR | 更新QP状态至停止失败。 |
| 234 | RR_OOB_LISTEN_SOCKET_ERROR | 带外链路监听开启失败。 |
| 235 | RR_OOB_CONN_SEND_ERROR | 带外链路发送失败。 |
| 236 | RR_OOB_CONN_RECEIVE_ERROR | 带外链路接收失败。 |
| 237 | RR_OOB_CONN_CB_NOT_SET | 带外链路连接回调未设置。 |
| 238 | RR_OOB_CLIENT_SOCKET_ERROR | 带外链路客户端发起连接失败。 |
| 239 | RR_OOB_SSL_INIT_ERROR | 加密初始化失败。 |
| 240 | RR_OOB_SSL_WRITE_ERROR | 加密写失败。 |
| 241 | RR_OOB_SSL_READ_ERROR | 加密读失败。 |
| 242 | RR_EP_NOT_INITIALIZED | EP未初始化。 |
| 243 | RR_WORKER_NOT_INITIALIZED | Worker未初始化。 |
| 244 | RR_WORKER_BIND_CPU_FAILED | Worker线程绑定CPU失败。 |
| 245 | RR_WORKER_REQUEST_HANDLER_NOT_SET | Worker的新消息回调函数未注册。 |
| 246 | RR_WORKER_SEND_POSTED_HANDLER_NOT_SET | Worker的消息发送回调函数未注册。 |
| 247 | RR_WORKER_ONE_SIDE_DONE_HANDLER_NOT_SET | Worker的单边消息回调函数未注册。 |
| 248 | RR_WORKER_FAILED_ADD_QP | Worker线程添加QP失败。 |
| 249 | RR_HEARTBEAT_CREATE_EPOLL_FAILED | 心跳检测创建失败。 |
| 250 | RR_HEARTBEAT_SET_SOCKET_OPT_FAILED | 心跳检测设置失败。 |
| 251 | RR_HEARTBEAT_IP_ALREADY_EXISTED | 心跳检测IP地址已存在。 |
| 252 | RR_HEARTBEAT_IP_ADD_FAILED | 心跳检测IP地址添加失败。 |
| 253 | RR_HEARTBEAT_IP_ADD_EPOLL_FAILED | 心跳检测IP地址添加失败。 |
| 254 | RR_HEARTBEAT_IP_REMOVE_EPOLL_FAILED | 心跳检测IP地址移除失败。 |
| 255 | RR_HEARTBEAT_IP_NO_FOUND | 心跳检测IP地址未匹配。 |

![](media/image8.png)

部分常见错误码详细说明：

230：RDMA的双边请求发起时，有限制长度的发送队列来限制并发，如果并发过大时，可能耗尽队列导致出现此错误。解决方式可以通过调大UBSHcomNetDriverOptions中的prePostReceiveSizePerQP和qpSendQueueSize来扩大队列，这个队列的值是取上述两个参数的较小值。
