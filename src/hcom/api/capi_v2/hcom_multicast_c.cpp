/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "hcom_multicast_c.h"

#include <new>
#include <string>
#include <utility>
#include <vector>

#include "api/hcom_service_context.h"
#include "hcom_def_inner_c.h"
#include "hcom_err.h"
#include "multicast/include/multicast_publisher.h"
#include "multicast/include/multicast_publisher_service.h"
#include "multicast/include/multicast_subscriber.h"
#include "multicast/include/multicast_subscriber_service.h"

using namespace ock::hcom;

#define VALIDATE_MULTICAST_ERR(handle)                                           \
    if (NN_UNLIKELY((handle) == 0)) {                                            \
        NN_LOG_ERROR("Invalid param, multicast handle must be correct address"); \
        return SER_INVALID_PARAM;                                                \
    }

#define VALIDATE_MULTICAST_VOID(handle)                                          \
    if (NN_UNLIKELY((handle) == 0)) {                                            \
        NN_LOG_ERROR("Invalid param, multicast handle must be correct address"); \
        return;                                                                  \
    }

#define VALIDATE_MULTICAST_ZERO(handle)                                          \
    if (NN_UNLIKELY((handle) == 0)) {                                            \
        NN_LOG_ERROR("Invalid param, multicast handle must be correct address"); \
        return 0;                                                                \
    }

#define VALIDATE_MULTICAST_BOOL(handle)                                          \
    if (NN_UNLIKELY((handle) == 0)) {                                            \
        NN_LOG_ERROR("Invalid param, multicast handle must be correct address"); \
        return false;                                                            \
    }

#define VALIDATE_MULTICAST_PTR(handle)                                           \
    if (NN_UNLIKELY((handle) == 0)) {                                            \
        NN_LOG_ERROR("Invalid param, multicast handle must be correct address"); \
        return nullptr;                                                          \
    }

/*
 * The following global managers keep the lifetime of the TLS handlers registered to a multicast service,
 * they are released when the service is destroyed.
 */
static ServiceHdlMgr<EpTLSHdlAdp> g_multicastPubTlsHdl;
static ServiceHdlMgr<EpTLSHdlAdp> g_multicastSubTlsHdl;

static UBSHcomNetDriverProtocol ConvertMulticastProtocolToInner(ubs_hcom_multicast_protocol protocol)
{
    switch (protocol) {
        case C_MULTICAST_RDMA:
            return UBSHcomNetDriverProtocol::RDMA;
        case C_MULTICAST_TCP:
            return UBSHcomNetDriverProtocol::TCP;
        default:
            return UBSHcomNetDriverProtocol::RDMA;
    }
}

static ubs_hcom_multicast_protocol ConvertProtocolToC(UBSHcomNetDriverProtocol protocol)
{
    switch (protocol) {
        case UBSHcomNetDriverProtocol::RDMA:
            return C_MULTICAST_RDMA;
        case UBSHcomNetDriverProtocol::TCP:
            return C_MULTICAST_TCP;
        default:
            return C_MULTICAST_UNKNOWN;
    }
}

static UBSHcomNetDriverWorkingMode ConvertWorkerModeToInner(ubs_hcom_service_worker_mode mode)
{
    return mode == C_SERVICE_EVENT_POLLING ? NET_EVENT_POLLING : NET_BUSY_POLLING;
}

static UBSHcomNetCipherSuite ConvertCipherSuiteToInner(ubs_hcom_service_cipher_suite suite)
{
    switch (suite) {
        case C_SERVICE_AES_GCM_128:
            return AES_GCM_128;
        case C_SERVICE_AES_GCM_256:
            return AES_GCM_256;
        case C_SERVICE_AES_CCM_128:
            return AES_CCM_128;
        case C_SERVICE_CHACHA20_POLY1305:
            return CHACHA20_POLY1305;
        default:
            return AES_GCM_128;
    }
}

/*
 * Fields with a non-zero default fall back to the default when the C value is 0, the other fields are always
 * applied as they are, so that a zero-initialized struct can still be used with the default settings.
 */
static void ConvertMulticastOptions(const ubs_hcom_multicast_service_options &options, MulticastServiceOptions &inner)
{
    if (options.maxSendRecvDataSize != 0) {
        inner.maxSendRecvDataSize = options.maxSendRecvDataSize;
    }
    if (options.maxSendRecvDataCount != 0) {
        inner.maxSendRecvDataCount = options.maxSendRecvDataCount;
    }
    if (options.multicastIoContextCount != 0) {
        inner.multicastIoContextCount = options.multicastIoContextCount;
    }
    inner.workerGroupId = options.workerGroupId;
    if (options.workerGroupThreadCount != 0) {
        inner.workerGroupThreadCount = options.workerGroupThreadCount;
    }
    inner.workerGroupMode = ConvertWorkerModeToInner(options.workerGroupMode);
    inner.workerThreadPriority = options.workerThreadPriority;
    if (options.workerGroupCpuIdsRangeFirst != 0 || options.workerGroupCpuIdsRangeSecond != 0) {
        inner.workerGroupCpuIdsRange = {options.workerGroupCpuIdsRangeFirst, options.workerGroupCpuIdsRangeSecond};
    }
    inner.protocol = ConvertMulticastProtocolToInner(options.protocol);
    if (options.qpSendQueueSize != 0) {
        inner.qpSendQueueSize = options.qpSendQueueSize;
    }
    if (options.qpRecvQueueSize != 0) {
        inner.qpRecvQueueSize = options.qpRecvQueueSize;
    }
    if (options.qpPrePostSize != 0) {
        inner.qpPrePostSize = options.qpPrePostSize;
    }
    if (options.qpBatchRePostSize != 0) {
        inner.qpBatchRePostSize = options.qpBatchRePostSize;
    }
    if (options.completionQueueDepth != 0) {
        inner.completionQueueDepth = options.completionQueueDepth;
    }
    if (options.maxSubscriberNum != 0) {
        inner.maxSubscriberNum = options.maxSubscriberNum;
    }
    inner.publisherWrkGroupNo = options.publisherWrkGroupNo;
    inner.enableTls = options.enableTls;
    inner.cipherSuite = ConvertCipherSuiteToInner(options.cipherSuite);
    inner.periodicCpuId = options.periodicCpuId;
}

template <typename Service>
static void RegisterMulticastTlsCallbacks(ServiceHdlMgr<EpTLSHdlAdp> &mgr, Service *service,
                                          ubs_hcom_tls_get_cert_cb certCb, ubs_hcom_tls_get_pk_cb priKeyCb,
                                          ubs_hcom_tls_get_ca_cb caCb)
{
    const uintptr_t key = reinterpret_cast<uintptr_t>(service);
    if (certCb != nullptr) {
        auto *adp = new (std::nothrow) EpTLSHdlAdp();
        if (adp != nullptr) {
            adp->SetTLSCertCb(certCb);
            service->RegisterTLSCertificationCallback(std::bind(&EpTLSHdlAdp::UBSHcomTLSCertificationCallback, adp,
                                                                std::placeholders::_1, std::placeholders::_2));
            mgr.AddHdlAdp(key, reinterpret_cast<uintptr_t>(adp));
        }
    }
    if (priKeyCb != nullptr) {
        auto *adp = new (std::nothrow) EpTLSHdlAdp();
        if (adp != nullptr) {
            adp->SetTLSPrivateKeyCb(priKeyCb);
            service->RegisterTLSPrivateKeyCallback(
                std::bind(&EpTLSHdlAdp::UBSHcomTLSPrivateKeyCallback, adp, std::placeholders::_1, std::placeholders::_2,
                          std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
            mgr.AddHdlAdp(key, reinterpret_cast<uintptr_t>(adp));
        }
    }
    if (caCb != nullptr) {
        auto *adp = new (std::nothrow) EpTLSHdlAdp();
        if (adp != nullptr) {
            adp->SetTLSCaCb(caCb);
            service->RegisterTLSCaCallback(std::bind(&EpTLSHdlAdp::UBSHcomTLSCaCallback, adp, std::placeholders::_1,
                                                     std::placeholders::_2, std::placeholders::_3,
                                                     std::placeholders::_4, std::placeholders::_5));
            mgr.AddHdlAdp(key, reinterpret_cast<uintptr_t>(adp));
        }
    }
}

static int MulticastNewSubscriptionBinder(ubs_hcom_multicast_new_subscription_handler handler, void *usrArg,
                                          SubscriptionInfoPtr &info)
{
    if (NN_UNLIKELY(handler == nullptr || info.Get() == nullptr)) {
        return SER_INVALID_PARAM;
    }
    return handler(reinterpret_cast<ubs_hcom_subscription_info>(info.Get()), usrArg);
}

static void MulticastSubscriptionExceptionBinder(ubs_hcom_multicast_subscription_exception_handler handler,
                                                 void *usrArg, SubscriptionInfo &info)
{
    if (NN_UNLIKELY(handler == nullptr)) {
        return;
    }
    handler(reinterpret_cast<ubs_hcom_subscription_info>(&info), usrArg);
}

static void MulticastEpBrokenBinder(ubs_hcom_multicast_ep_broken_handler handler, void *usrArg,
                                    const UBSHcomNetEndpointPtr &ep)
{
    if (NN_UNLIKELY(handler == nullptr)) {
        return;
    }
    handler(reinterpret_cast<ubs_hcom_endpoint>(ep.Get()), usrArg);
}

static int MulticastPubRecvBinder(ubs_hcom_multicast_pub_recv_handler handler, void *usrArg, PublisherContext &ctx)
{
    if (NN_UNLIKELY(handler == nullptr)) {
        return SER_INVALID_PARAM;
    }
    return handler(reinterpret_cast<ubs_hcom_publisher_context>(&ctx), usrArg);
}

static int MulticastReqRecvBinder(ubs_hcom_multicast_req_recv_handler handler, void *usrArg, UBSHcomServiceContext &ctx)
{
    if (NN_UNLIKELY(handler == nullptr)) {
        return SER_INVALID_PARAM;
    }
    return handler(reinterpret_cast<ubs_hcom_service_context>(&ctx), usrArg);
}

static int MulticastReqPostedBinder(ubs_hcom_multicast_req_posted_handler handler, void *usrArg,
                                    UBSHcomServiceContext &ctx)
{
    if (NN_UNLIKELY(handler == nullptr)) {
        return SER_INVALID_PARAM;
    }
    return handler(reinterpret_cast<ubs_hcom_service_context>(&ctx), usrArg);
}

static void MulticastCallDoneBinder(ubs_hcom_multicast_call_done_handler handler, void *usrArg, PublisherContext &ctx)
{
    if (NN_UNLIKELY(handler == nullptr)) {
        return;
    }
    handler(reinterpret_cast<ubs_hcom_publisher_context>(&ctx), usrArg);
}

int ubs_hcom_publisher_service_create(const char *name, const ubs_hcom_multicast_service_options *options,
                                      ubs_hcom_publisher_service *service)
{
    if (NN_UNLIKELY(name == nullptr || service == nullptr)) {
        NN_LOG_ERROR("Invalid param, name or service must be correct address");
        return SER_INVALID_PARAM;
    }

    MulticastServiceOptions innerOptions;
    if (options != nullptr) {
        ConvertMulticastOptions(*options, innerOptions);
    }

    auto *svc = PublisherService::Create(std::string(name), innerOptions);
    if (NN_UNLIKELY(svc == nullptr)) {
        NN_LOG_ERROR("Failed to create multicast publisher service: " << name);
        *service = 0;
        return SER_NEW_OBJECT_FAILED;
    }

    *service = reinterpret_cast<ubs_hcom_publisher_service>(svc);
    return SER_OK;
}

int ubs_hcom_publisher_service_destroy(ubs_hcom_publisher_service service, const char *name)
{
    if (NN_UNLIKELY(name == nullptr)) {
        NN_LOG_ERROR("Invalid param, name must be correct address");
        return SER_INVALID_PARAM;
    }

    auto result = PublisherService::Destroy(std::string(name));
    if (service != 0) {
        g_multicastPubTlsHdl.RemoveAll(service);
    }
    return result;
}

int ubs_hcom_publisher_service_start(ubs_hcom_publisher_service service)
{
    VALIDATE_MULTICAST_ERR(service);
    return reinterpret_cast<PublisherService *>(service)->Start();
}

void ubs_hcom_publisher_service_stop(ubs_hcom_publisher_service service)
{
    VALIDATE_MULTICAST_VOID(service);
    reinterpret_cast<PublisherService *>(service)->Stop();
}

int ubs_hcom_publisher_service_create_publisher(ubs_hcom_publisher_service service, ubs_hcom_publisher *publisher)
{
    VALIDATE_MULTICAST_ERR(service);
    if (NN_UNLIKELY(publisher == nullptr)) {
        NN_LOG_ERROR("Invalid param, publisher must be correct address");
        return SER_INVALID_PARAM;
    }

    auto *refPtr = new (std::nothrow) NetRef<Publisher>;
    if (NN_UNLIKELY(refPtr == nullptr)) {
        NN_LOG_ERROR("Create publisher malloc memory failed");
        return SER_NEW_OBJECT_FAILED;
    }

    auto result = reinterpret_cast<PublisherService *>(service)->CreatePublisher(*refPtr);
    if (NN_UNLIKELY(result != SER_OK)) {
        delete refPtr;
        *publisher = 0;
        return result;
    }

    *publisher = reinterpret_cast<ubs_hcom_publisher>(refPtr);
    return SER_OK;
}

void ubs_hcom_publisher_service_destroy_publisher(ubs_hcom_publisher_service service, ubs_hcom_publisher publisher)
{
    VALIDATE_MULTICAST_VOID(service);
    VALIDATE_MULTICAST_VOID(publisher);

    auto *refPtr = reinterpret_cast<NetRef<Publisher> *>(publisher);
    reinterpret_cast<PublisherService *>(service)->DestroyPublisher(*refPtr);
    delete refPtr;
}

int ubs_hcom_publisher_service_bind(ubs_hcom_publisher_service service, const char *listenerUrl,
                                    ubs_hcom_multicast_new_subscription_handler handler, void *usrArg, int32_t cpuId)
{
    VALIDATE_MULTICAST_ERR(service);
    if (NN_UNLIKELY(listenerUrl == nullptr || handler == nullptr)) {
        NN_LOG_ERROR("Invalid param, listenerUrl or handler must be correct address");
        return SER_INVALID_PARAM;
    }

    NewSubscriptionHandler innerHandler =
        std::bind(&MulticastNewSubscriptionBinder, handler, usrArg, std::placeholders::_1);
    return reinterpret_cast<PublisherService *>(service)->Bind(std::string(listenerUrl), innerHandler,
                                                               static_cast<int>(cpuId));
}

ubs_hcom_multicast_config ubs_hcom_publisher_service_get_config(ubs_hcom_publisher_service service)
{
    VALIDATE_MULTICAST_ZERO(service);
    return reinterpret_cast<ubs_hcom_multicast_config>(&reinterpret_cast<PublisherService *>(service)->GetConfig());
}

void ubs_hcom_publisher_service_register_subscription_exception_handler(
    ubs_hcom_publisher_service service, ubs_hcom_multicast_subscription_exception_handler handler, void *usrArg)
{
    VALIDATE_MULTICAST_VOID(service);
    if (NN_UNLIKELY(handler == nullptr)) {
        NN_LOG_ERROR("Invalid param, handler must be correct address");
        return;
    }

    SubscriptionExceptionHandler innerHandler =
        std::bind(&MulticastSubscriptionExceptionBinder, handler, usrArg, std::placeholders::_1);
    reinterpret_cast<PublisherService *>(service)->RegisterSubscriptionExceptionHandler(innerHandler);
}

void ubs_hcom_publisher_service_register_broken_handler(ubs_hcom_publisher_service service,
                                                        ubs_hcom_multicast_ep_broken_handler handler, void *usrArg)
{
    VALIDATE_MULTICAST_VOID(service);
    if (NN_UNLIKELY(handler == nullptr)) {
        NN_LOG_ERROR("Invalid param, handler must be correct address");
        return;
    }

    MulticastEpBrokenHandler innerHandler = std::bind(&MulticastEpBrokenBinder, handler, usrArg, std::placeholders::_1);
    reinterpret_cast<PublisherService *>(service)->RegisterBrokenHandler(innerHandler);
}

void ubs_hcom_publisher_service_register_send_handler(ubs_hcom_publisher_service service,
                                                      ubs_hcom_multicast_req_posted_handler handler, void *usrArg)
{
    VALIDATE_MULTICAST_VOID(service);
    if (NN_UNLIKELY(handler == nullptr)) {
        NN_LOG_ERROR("Invalid param, handler must be correct address");
        return;
    }

    MulticastReqPostedHandler innerHandler =
        std::bind(&MulticastReqPostedBinder, handler, usrArg, std::placeholders::_1);
    reinterpret_cast<PublisherService *>(service)->RegisterSendHandler(innerHandler);
}

void ubs_hcom_publisher_service_register_pub_recv_handler(ubs_hcom_publisher_service service,
                                                          ubs_hcom_multicast_pub_recv_handler handler, void *usrArg)
{
    VALIDATE_MULTICAST_VOID(service);
    if (NN_UNLIKELY(handler == nullptr)) {
        NN_LOG_ERROR("Invalid param, handler must be correct address");
        return;
    }

    MulticastPubReqRecvHandler innerHandler =
        std::bind(&MulticastPubRecvBinder, handler, usrArg, std::placeholders::_1);
    reinterpret_cast<PublisherService *>(service)->RegisterPubRecvHandler(innerHandler);
}

void ubs_hcom_publisher_service_register_tls_callbacks(ubs_hcom_publisher_service service,
                                                       ubs_hcom_tls_get_cert_cb certCb, ubs_hcom_tls_get_pk_cb priKeyCb,
                                                       ubs_hcom_tls_get_ca_cb caCb)
{
    VALIDATE_MULTICAST_VOID(service);
    RegisterMulticastTlsCallbacks(g_multicastPubTlsHdl, reinterpret_cast<PublisherService *>(service), certCb, priKeyCb,
                                  caCb);
}

void ubs_hcom_publisher_service_add_workergroup(ubs_hcom_publisher_service service, uint16_t workerGroupId,
                                                uint32_t threadCount, uint32_t cpuIdsRangeFirst,
                                                uint32_t cpuIdsRangeSecond, int8_t priority)
{
    VALIDATE_MULTICAST_VOID(service);
    std::pair<uint32_t, uint32_t> cpuIdsRange = {cpuIdsRangeFirst, cpuIdsRangeSecond};
    reinterpret_cast<PublisherService *>(service)->AddWorkerGroup(workerGroupId, threadCount, cpuIdsRange, priority);
}

int ubs_hcom_publisher_service_register_memory_region(ubs_hcom_publisher_service service, uint64_t size,
                                                      ubs_hcom_memory_region *mr)
{
    VALIDATE_MULTICAST_ERR(service);
    if (NN_UNLIKELY(mr == nullptr)) {
        NN_LOG_ERROR("Invalid param, mr must be correct address");
        return SER_INVALID_PARAM;
    }

    auto *mrPtr = new (std::nothrow) UBSHcomNetMemoryRegionPtr;
    if (NN_UNLIKELY(mrPtr == nullptr)) {
        NN_LOG_ERROR("Register memory region malloc memory failed");
        return SER_NEW_OBJECT_FAILED;
    }

    auto result = reinterpret_cast<PublisherService *>(service)->RegisterMemoryRegion(size, *mrPtr);
    if (NN_UNLIKELY(result != SER_OK)) {
        delete mrPtr;
        *mr = 0;
        return result;
    }

    *mr = reinterpret_cast<ubs_hcom_memory_region>(mrPtr);
    return SER_OK;
}

int ubs_hcom_publisher_service_register_assign_memory_region(ubs_hcom_publisher_service service, uintptr_t address,
                                                             uint64_t size, ubs_hcom_memory_region *mr)
{
    VALIDATE_MULTICAST_ERR(service);
    if (NN_UNLIKELY(mr == nullptr)) {
        NN_LOG_ERROR("Invalid param, mr must be correct address");
        return SER_INVALID_PARAM;
    }

    auto *mrPtr = new (std::nothrow) UBSHcomNetMemoryRegionPtr;
    if (NN_UNLIKELY(mrPtr == nullptr)) {
        NN_LOG_ERROR("Register memory region malloc memory failed");
        return SER_NEW_OBJECT_FAILED;
    }

    auto result = reinterpret_cast<PublisherService *>(service)->RegisterMemoryRegion(address, size, *mrPtr);
    if (NN_UNLIKELY(result != SER_OK)) {
        delete mrPtr;
        *mr = 0;
        return result;
    }

    *mr = reinterpret_cast<ubs_hcom_memory_region>(mrPtr);
    return SER_OK;
}

void ubs_hcom_publisher_service_destroy_memory_region(ubs_hcom_publisher_service service, ubs_hcom_memory_region mr)
{
    VALIDATE_MULTICAST_VOID(service);
    VALIDATE_MULTICAST_VOID(mr);

    auto *mrPtr = reinterpret_cast<UBSHcomNetMemoryRegionPtr *>(mr);
    reinterpret_cast<PublisherService *>(service)->DestroyMemoryRegion(*mrPtr);
    delete mrPtr;
}

int ubs_hcom_subscriber_service_create(const char *name, const ubs_hcom_multicast_service_options *options,
                                       ubs_hcom_subscriber_service *service)
{
    if (NN_UNLIKELY(name == nullptr || service == nullptr)) {
        NN_LOG_ERROR("Invalid param, name or service must be correct address");
        return SER_INVALID_PARAM;
    }

    MulticastServiceOptions innerOptions;
    if (options != nullptr) {
        ConvertMulticastOptions(*options, innerOptions);
    }

    auto *svc = SubscriberService::Create(std::string(name), innerOptions);
    if (NN_UNLIKELY(svc == nullptr)) {
        NN_LOG_ERROR("Failed to create multicast subscriber service: " << name);
        *service = 0;
        return SER_NEW_OBJECT_FAILED;
    }

    *service = reinterpret_cast<ubs_hcom_subscriber_service>(svc);
    return SER_OK;
}

int ubs_hcom_subscriber_service_destroy(ubs_hcom_subscriber_service service, const char *name)
{
    if (NN_UNLIKELY(name == nullptr)) {
        NN_LOG_ERROR("Invalid param, name must be correct address");
        return SER_INVALID_PARAM;
    }

    auto result = SubscriberService::Destroy(std::string(name));
    if (service != 0) {
        g_multicastSubTlsHdl.RemoveAll(service);
    }
    return result;
}

int ubs_hcom_subscriber_service_start(ubs_hcom_subscriber_service service)
{
    VALIDATE_MULTICAST_ERR(service);
    return reinterpret_cast<SubscriberService *>(service)->Start();
}

void ubs_hcom_subscriber_service_stop(ubs_hcom_subscriber_service service)
{
    VALIDATE_MULTICAST_VOID(service);
    reinterpret_cast<SubscriberService *>(service)->Stop();
}

int ubs_hcom_subscriber_service_create_subscriber(ubs_hcom_subscriber_service service, const char *serverUrl,
                                                  ubs_hcom_subscriber *subscriber)
{
    VALIDATE_MULTICAST_ERR(service);
    if (NN_UNLIKELY(serverUrl == nullptr || subscriber == nullptr)) {
        NN_LOG_ERROR("Invalid param, serverUrl or subscriber must be correct address");
        return SER_INVALID_PARAM;
    }

    auto *refPtr = new (std::nothrow) NetRef<Subscriber>;
    if (NN_UNLIKELY(refPtr == nullptr)) {
        NN_LOG_ERROR("Create subscriber malloc memory failed");
        return SER_NEW_OBJECT_FAILED;
    }

    auto result = reinterpret_cast<SubscriberService *>(service)->CreateSubscriber(std::string(serverUrl), *refPtr);
    if (NN_UNLIKELY(result != SER_OK)) {
        delete refPtr;
        *subscriber = 0;
        return result;
    }

    *subscriber = reinterpret_cast<ubs_hcom_subscriber>(refPtr);
    return SER_OK;
}

int ubs_hcom_subscriber_service_create_subscriber_with_group(ubs_hcom_subscriber_service service, const char *serverUrl,
                                                             uint16_t publisherGroupIdx,
                                                             ubs_hcom_subscriber *subscriber)
{
    VALIDATE_MULTICAST_ERR(service);
    if (NN_UNLIKELY(serverUrl == nullptr || subscriber == nullptr)) {
        NN_LOG_ERROR("Invalid param, serverUrl or subscriber must be correct address");
        return SER_INVALID_PARAM;
    }

    auto *refPtr = new (std::nothrow) NetRef<Subscriber>;
    if (NN_UNLIKELY(refPtr == nullptr)) {
        NN_LOG_ERROR("Create subscriber malloc memory failed");
        return SER_NEW_OBJECT_FAILED;
    }

    auto result = reinterpret_cast<SubscriberService *>(service)->CreateSubscriber(std::string(serverUrl),
                                                                                   publisherGroupIdx, *refPtr);
    if (NN_UNLIKELY(result != SER_OK)) {
        delete refPtr;
        *subscriber = 0;
        return result;
    }

    *subscriber = reinterpret_cast<ubs_hcom_subscriber>(refPtr);
    return SER_OK;
}

void ubs_hcom_subscriber_service_destroy_subscriber(ubs_hcom_subscriber_service service, ubs_hcom_subscriber subscriber)
{
    VALIDATE_MULTICAST_VOID(service);
    VALIDATE_MULTICAST_VOID(subscriber);

    auto *refPtr = reinterpret_cast<NetRef<Subscriber> *>(subscriber);
    reinterpret_cast<SubscriberService *>(service)->DestroySubscriber(*refPtr);
    delete refPtr;
}

ubs_hcom_multicast_config ubs_hcom_subscriber_service_get_config(ubs_hcom_subscriber_service service)
{
    VALIDATE_MULTICAST_ZERO(service);
    return reinterpret_cast<ubs_hcom_multicast_config>(&reinterpret_cast<SubscriberService *>(service)->GetConfig());
}

void ubs_hcom_subscriber_service_register_broken_handler(ubs_hcom_subscriber_service service,
                                                         ubs_hcom_multicast_ep_broken_handler handler, void *usrArg)
{
    VALIDATE_MULTICAST_VOID(service);
    if (NN_UNLIKELY(handler == nullptr)) {
        NN_LOG_ERROR("Invalid param, handler must be correct address");
        return;
    }

    MulticastEpBrokenHandler innerHandler = std::bind(&MulticastEpBrokenBinder, handler, usrArg, std::placeholders::_1);
    reinterpret_cast<SubscriberService *>(service)->RegisterBrokenHandler(innerHandler);
}

void ubs_hcom_subscriber_service_register_recv_handler(ubs_hcom_subscriber_service service,
                                                       ubs_hcom_multicast_req_recv_handler handler, void *usrArg)
{
    VALIDATE_MULTICAST_VOID(service);
    if (NN_UNLIKELY(handler == nullptr)) {
        NN_LOG_ERROR("Invalid param, handler must be correct address");
        return;
    }

    MulticastReqRecvHandler innerHandler = std::bind(&MulticastReqRecvBinder, handler, usrArg, std::placeholders::_1);
    reinterpret_cast<SubscriberService *>(service)->RegisterRecvHandler(innerHandler);
}

void ubs_hcom_subscriber_service_register_tls_callbacks(ubs_hcom_subscriber_service service,
                                                        ubs_hcom_tls_get_cert_cb certCb,
                                                        ubs_hcom_tls_get_pk_cb priKeyCb, ubs_hcom_tls_get_ca_cb caCb)
{
    VALIDATE_MULTICAST_VOID(service);
    RegisterMulticastTlsCallbacks(g_multicastSubTlsHdl, reinterpret_cast<SubscriberService *>(service), certCb,
                                  priKeyCb, caCb);
}

int ubs_hcom_publisher_call(ubs_hcom_publisher publisher, ubs_hcom_multicast_op_info opInfo,
                            ubs_hcom_multicast_request req, ubs_hcom_multicast_call_done_handler done, void *usrArg)
{
    VALIDATE_MULTICAST_ERR(publisher);
    if (NN_UNLIKELY(done == nullptr)) {
        NN_LOG_ERROR("Invalid param, done handler must be correct address");
        return SER_INVALID_PARAM;
    }

    UBSHcomNetTransOpInfo innerOpInfo(opInfo.seqNo, opInfo.timeout, opInfo.errorCode, opInfo.flags);
    MultiRequest innerReq(req.data, req.size, req.lkey);
    MultiCastCallback *cb = NewMultiCastCallback(&MulticastCallDoneBinder, done, usrArg, std::placeholders::_1);
    if (NN_UNLIKELY(cb == nullptr)) {
        NN_LOG_ERROR("Failed to create multicast callback");
        return SER_NEW_OBJECT_FAILED;
    }

    return reinterpret_cast<NetRef<Publisher> *>(publisher)->Get()->Call(innerOpInfo, innerReq, cb);
}

int ubs_hcom_publisher_call_to(ubs_hcom_publisher publisher, ubs_hcom_multicast_op_info opInfo, const char *remoteIp,
                               ubs_hcom_multicast_request req, ubs_hcom_multicast_call_done_handler done, void *usrArg)
{
    VALIDATE_MULTICAST_ERR(publisher);
    if (NN_UNLIKELY(remoteIp == nullptr || done == nullptr)) {
        NN_LOG_ERROR("Invalid param, remoteIp or done handler must be correct address");
        return SER_INVALID_PARAM;
    }

    UBSHcomNetTransOpInfo innerOpInfo(opInfo.seqNo, opInfo.timeout, opInfo.errorCode, opInfo.flags);
    MultiRequest innerReq(req.data, req.size, req.lkey);
    MultiCastCallback *cb = NewMultiCastCallback(&MulticastCallDoneBinder, done, usrArg, std::placeholders::_1);
    if (NN_UNLIKELY(cb == nullptr)) {
        NN_LOG_ERROR("Failed to create multicast callback");
        return SER_NEW_OBJECT_FAILED;
    }

    return reinterpret_cast<NetRef<Publisher> *>(publisher)->Get()->Call(innerOpInfo, std::string(remoteIp), innerReq,
                                                                         cb);
}

bool ubs_hcom_publisher_add_subscription(ubs_hcom_publisher publisher, ubs_hcom_subscription_info info)
{
    VALIDATE_MULTICAST_BOOL(publisher);
    VALIDATE_MULTICAST_BOOL(info);

    SubscriptionInfoPtr ref(reinterpret_cast<SubscriptionInfo *>(info));
    return reinterpret_cast<NetRef<Publisher> *>(publisher)->Get()->AddSubscription(ref);
}

bool ubs_hcom_publisher_del_subscription(ubs_hcom_publisher publisher, ubs_hcom_subscription_info info)
{
    VALIDATE_MULTICAST_BOOL(publisher);
    VALIDATE_MULTICAST_BOOL(info);

    SubscriptionInfoPtr ref(reinterpret_cast<SubscriptionInfo *>(info));
    return reinterpret_cast<NetRef<Publisher> *>(publisher)->Get()->DelSubscription(ref);
}

uint32_t ubs_hcom_publisher_get_subscriber_num(ubs_hcom_publisher publisher)
{
    VALIDATE_MULTICAST_ZERO(publisher);
    return reinterpret_cast<NetRef<Publisher> *>(publisher)->Get()->GetSubscriberNum();
}

int ubs_hcom_publisher_get_all_subscriber_info(ubs_hcom_publisher publisher, ubs_hcom_subscription_info *infos,
                                               uint32_t *count)
{
    VALIDATE_MULTICAST_ERR(publisher);
    if (NN_UNLIKELY(count == nullptr)) {
        NN_LOG_ERROR("Invalid param, count must be correct address");
        return SER_INVALID_PARAM;
    }

    auto subscriptionVec = reinterpret_cast<NetRef<Publisher> *>(publisher)->Get()->GetAllSubscriberInfo();
    uint32_t capacity = *count;
    *count = static_cast<uint32_t>(subscriptionVec.size());
    if (infos == nullptr) {
        return SER_OK;
    }

    uint32_t fillCount = capacity < *count ? capacity : *count;
    for (uint32_t i = 0; i < fillCount; i++) {
        infos[i] = reinterpret_cast<ubs_hcom_subscription_info>(subscriptionVec[i].Get());
    }
    return SER_OK;
}

int ubs_hcom_publisher_get_subscribe_by_ep_id(ubs_hcom_publisher publisher, uint64_t epId,
                                              ubs_hcom_subscription_info *info)
{
    VALIDATE_MULTICAST_ERR(publisher);
    if (NN_UNLIKELY(info == nullptr)) {
        NN_LOG_ERROR("Invalid param, info must be correct address");
        return SER_INVALID_PARAM;
    }

    auto ref = reinterpret_cast<NetRef<Publisher> *>(publisher)->Get()->GetSubscribeByEpId(epId);
    *info = reinterpret_cast<ubs_hcom_subscription_info>(ref.Get());
    return SER_OK;
}

uint64_t ubs_hcom_subscription_info_get_id(ubs_hcom_subscription_info info)
{
    VALIDATE_MULTICAST_ZERO(info);
    return reinterpret_cast<SubscriptionInfo *>(info)->GetId();
}

const char *ubs_hcom_subscription_info_get_name(ubs_hcom_subscription_info info)
{
    VALIDATE_MULTICAST_PTR(info);
    return reinterpret_cast<SubscriptionInfo *>(info)->GetName().c_str();
}

const char *ubs_hcom_subscription_info_get_ip(ubs_hcom_subscription_info info)
{
    VALIDATE_MULTICAST_PTR(info);
    return reinterpret_cast<SubscriptionInfo *>(info)->GetIp().c_str();
}

uint16_t ubs_hcom_subscription_info_get_port(ubs_hcom_subscription_info info)
{
    VALIDATE_MULTICAST_ZERO(info);
    return reinterpret_cast<SubscriptionInfo *>(info)->GetPort();
}

uint32_t ubs_hcom_publisher_context_get_subscriber_count(ubs_hcom_publisher_context ctx)
{
    VALIDATE_MULTICAST_ZERO(ctx);
    return static_cast<uint32_t>(reinterpret_cast<PublisherContext *>(ctx)->GetSubscriberRspInfo().size());
}

uint32_t ubs_hcom_publisher_context_get_reply_count(ubs_hcom_publisher_context ctx)
{
    VALIDATE_MULTICAST_ZERO(ctx);
    return reinterpret_cast<PublisherContext *>(ctx)->GetReplyCount();
}

uint32_t ubs_hcom_publisher_context_get_send_count(ubs_hcom_publisher_context ctx)
{
    VALIDATE_MULTICAST_ZERO(ctx);
    return reinterpret_cast<PublisherContext *>(ctx)->GetSendCount();
}

int ubs_hcom_publisher_context_get_subscriber_rsp_info(ubs_hcom_publisher_context ctx, uint32_t index,
                                                       ubs_hcom_multicast_subscriber_rsp_info *info)
{
    VALIDATE_MULTICAST_ERR(ctx);
    if (NN_UNLIKELY(info == nullptr)) {
        NN_LOG_ERROR("Invalid param, info must be correct address");
        return SER_INVALID_PARAM;
    }

    const auto &rspList = reinterpret_cast<PublisherContext *>(ctx)->GetSubscriberRspInfo();
    if (NN_UNLIKELY(index >= rspList.size())) {
        NN_LOG_ERROR("Invalid index " << index << " for subscriber rsp list size " << rspList.size());
        return SER_INVALID_PARAM;
    }

    const SubscriberRspInfo &item = rspList[index];
    info->subInfo = reinterpret_cast<ubs_hcom_subscription_info>(item.GetSubInfos().Get());
    info->status = static_cast<ubs_hcom_multicast_subscriber_rsp_status>(item.GetStatus());
    const MultiResponse &response = item.GetMultiResponse();
    info->rspData = response.data;
    info->rspSize = response.size;
    return SER_OK;
}

void ubs_hcom_subscriber_close(ubs_hcom_subscriber subscriber)
{
    VALIDATE_MULTICAST_VOID(subscriber);
    reinterpret_cast<NetRef<Subscriber> *>(subscriber)->Get()->Close();
}

const char *ubs_hcom_subscriber_get_ip(ubs_hcom_subscriber subscriber)
{
    VALIDATE_MULTICAST_PTR(subscriber);
    return reinterpret_cast<NetRef<Subscriber> *>(subscriber)->Get()->GetIp().c_str();
}

uint16_t ubs_hcom_subscriber_get_port(ubs_hcom_subscriber subscriber)
{
    VALIDATE_MULTICAST_ZERO(subscriber);
    return reinterpret_cast<NetRef<Subscriber> *>(subscriber)->Get()->GetPort();
}

int ubs_hcom_multicast_context_reply(ubs_hcom_service_context ctx, ubs_hcom_multicast_request req)
{
    VALIDATE_MULTICAST_ERR(ctx);

    auto *subCtx = dynamic_cast<SubscriberContext *>(reinterpret_cast<UBSHcomServiceContext *>(ctx));
    if (NN_UNLIKELY(subCtx == nullptr)) {
        NN_LOG_ERROR("Invalid param, context is not a subscriber context");
        return SER_INVALID_PARAM;
    }

    MultiRequest innerReq(req.data, req.size, req.lkey);
    return subCtx->Reply(innerReq);
}

const char *ubs_hcom_multicast_config_get_name(ubs_hcom_multicast_config config)
{
    VALIDATE_MULTICAST_PTR(config);
    return reinterpret_cast<MulticastConfig *>(config)->GetName().c_str();
}

void ubs_hcom_multicast_config_set_device_ip_mask(ubs_hcom_multicast_config config, const char *const *ipMasks,
                                                  uint32_t count)
{
    VALIDATE_MULTICAST_VOID(config);

    std::vector<std::string> masks;
    if (ipMasks != nullptr) {
        masks.reserve(count);
        for (uint32_t i = 0; i < count; i++) {
            if (ipMasks[i] != nullptr) {
                masks.emplace_back(ipMasks[i]);
            }
        }
    }
    reinterpret_cast<MulticastConfig *>(config)->SetDeviceIpMask(masks);
}

uint32_t ubs_hcom_multicast_config_get_device_ip_mask_count(ubs_hcom_multicast_config config)
{
    VALIDATE_MULTICAST_ZERO(config);
    return static_cast<uint32_t>(reinterpret_cast<MulticastConfig *>(config)->GetDeviceIpMask().size());
}

const char *ubs_hcom_multicast_config_get_device_ip_mask(ubs_hcom_multicast_config config, uint32_t index)
{
    VALIDATE_MULTICAST_PTR(config);

    const auto &masks = reinterpret_cast<MulticastConfig *>(config)->GetDeviceIpMask();
    if (NN_UNLIKELY(index >= masks.size())) {
        NN_LOG_ERROR("Invalid index " << index << " for device ip mask size " << masks.size());
        return nullptr;
    }
    return masks[index].c_str();
}

void ubs_hcom_multicast_config_set_cq_depth(ubs_hcom_multicast_config config, uint16_t depth)
{
    VALIDATE_MULTICAST_VOID(config);
    reinterpret_cast<MulticastConfig *>(config)->SetCompletionQueueDepth(depth);
}

uint16_t ubs_hcom_multicast_config_get_cq_depth(ubs_hcom_multicast_config config)
{
    VALIDATE_MULTICAST_ZERO(config);
    return reinterpret_cast<MulticastConfig *>(config)->GetCompletionQueueDepth();
}

void ubs_hcom_multicast_config_set_sq_size(ubs_hcom_multicast_config config, uint32_t sqSize)
{
    VALIDATE_MULTICAST_VOID(config);
    reinterpret_cast<MulticastConfig *>(config)->SetSendQueueSize(sqSize);
}

uint32_t ubs_hcom_multicast_config_get_sq_size(ubs_hcom_multicast_config config)
{
    VALIDATE_MULTICAST_ZERO(config);
    return reinterpret_cast<MulticastConfig *>(config)->GetSendQueueSize();
}

void ubs_hcom_multicast_config_set_rq_size(ubs_hcom_multicast_config config, uint32_t rqSize)
{
    VALIDATE_MULTICAST_VOID(config);
    reinterpret_cast<MulticastConfig *>(config)->SetRecvQueueSize(rqSize);
}

uint32_t ubs_hcom_multicast_config_get_rq_size(ubs_hcom_multicast_config config)
{
    VALIDATE_MULTICAST_ZERO(config);
    return reinterpret_cast<MulticastConfig *>(config)->GetRecvQueueSize();
}

void ubs_hcom_multicast_config_set_prepost_size(ubs_hcom_multicast_config config, uint32_t prePostSize)
{
    VALIDATE_MULTICAST_VOID(config);
    reinterpret_cast<MulticastConfig *>(config)->SetQueuePrePostSize(prePostSize);
}

uint32_t ubs_hcom_multicast_config_get_prepost_size(ubs_hcom_multicast_config config)
{
    VALIDATE_MULTICAST_ZERO(config);
    return reinterpret_cast<MulticastConfig *>(config)->GetQueuePrePostSize();
}

void ubs_hcom_multicast_config_set_polling_batchsize(ubs_hcom_multicast_config config, uint16_t pollSize)
{
    VALIDATE_MULTICAST_VOID(config);
    reinterpret_cast<MulticastConfig *>(config)->SetPollingBatchSize(pollSize);
}

uint16_t ubs_hcom_multicast_config_get_polling_batchsize(ubs_hcom_multicast_config config)
{
    VALIDATE_MULTICAST_ZERO(config);
    return reinterpret_cast<MulticastConfig *>(config)->GetPollingBatchSize();
}

void ubs_hcom_multicast_config_set_polling_timeoutus(ubs_hcom_multicast_config config, uint16_t pollTimeout)
{
    VALIDATE_MULTICAST_VOID(config);
    reinterpret_cast<MulticastConfig *>(config)->SetEventPollingTimeOutUs(pollTimeout);
}

uint16_t ubs_hcom_multicast_config_get_polling_timeoutus(ubs_hcom_multicast_config config)
{
    VALIDATE_MULTICAST_ZERO(config);
    return reinterpret_cast<MulticastConfig *>(config)->GetEventPollingTimeOutUs();
}

void ubs_hcom_multicast_config_set_heartbeat(ubs_hcom_multicast_config config, ubs_hcom_multicast_heartbeat_options opt)
{
    VALIDATE_MULTICAST_VOID(config);

    MulticastHeartBeatOptions innerOpt;
    innerOpt.heartBeatIdleSec = opt.heartBeatIdleSec;
    innerOpt.heartBeatProbeTimes = opt.heartBeatProbeTimes;
    innerOpt.heartBeatProbeIntervalSec = opt.heartBeatProbeIntervalSec;
    reinterpret_cast<MulticastConfig *>(config)->SetHeartBeatOptions(innerOpt);
}

ubs_hcom_multicast_heartbeat_options ubs_hcom_multicast_config_get_heartbeat(ubs_hcom_multicast_config config)
{
    ubs_hcom_multicast_heartbeat_options out = {0, 0, 0};
    if (NN_UNLIKELY(config == 0)) {
        return out;
    }

    const auto &innerOpt = reinterpret_cast<MulticastConfig *>(config)->GetHeartBeatOptions();
    out.heartBeatIdleSec = innerOpt.heartBeatIdleSec;
    out.heartBeatProbeTimes = innerOpt.heartBeatProbeTimes;
    out.heartBeatProbeIntervalSec = innerOpt.heartBeatProbeIntervalSec;
    return out;
}

void ubs_hcom_multicast_config_set_max_send_recv_data_count(ubs_hcom_multicast_config config, uint32_t count)
{
    VALIDATE_MULTICAST_VOID(config);
    reinterpret_cast<MulticastConfig *>(config)->SetMaxSendRecvDataCount(count);
}

uint32_t ubs_hcom_multicast_config_get_max_send_recv_data_count(ubs_hcom_multicast_config config)
{
    VALIDATE_MULTICAST_ZERO(config);
    return reinterpret_cast<MulticastConfig *>(config)->GetMaxSendRecvDataCount();
}

void ubs_hcom_multicast_config_set_max_send_recv_data_size(ubs_hcom_multicast_config config, uint32_t size)
{
    VALIDATE_MULTICAST_VOID(config);
    reinterpret_cast<MulticastConfig *>(config)->SetMaxSendRecvDataSize(size);
}

uint32_t ubs_hcom_multicast_config_get_max_send_recv_data_size(ubs_hcom_multicast_config config)
{
    VALIDATE_MULTICAST_ZERO(config);
    return reinterpret_cast<MulticastConfig *>(config)->GetMaxSendRecvDataSize();
}

void ubs_hcom_multicast_config_set_worker_group_info(ubs_hcom_multicast_config config,
                                                     const ubs_hcom_multicast_worker_group_info *info, uint32_t count)
{
    VALIDATE_MULTICAST_VOID(config);

    std::vector<UBSHcomWorkerGroupInfo> groups;
    if (info != nullptr) {
        groups.reserve(count);
        for (uint32_t i = 0; i < count; i++) {
            UBSHcomWorkerGroupInfo group;
            group.threadPriority = info[i].threadPriority;
            group.threadCount = info[i].threadCount;
            group.groupId = info[i].groupId;
            group.cpuIdsRange = {info[i].cpuIdsRangeFirst, info[i].cpuIdsRangeSecond};
            groups.emplace_back(group);
        }
    }
    reinterpret_cast<MulticastConfig *>(config)->SetWorkerGroupInfo(groups);
}

uint32_t ubs_hcom_multicast_config_get_worker_group_info_count(ubs_hcom_multicast_config config)
{
    VALIDATE_MULTICAST_ZERO(config);
    return static_cast<uint32_t>(reinterpret_cast<MulticastConfig *>(config)->GetWorkerGroupInfo().size());
}

int ubs_hcom_multicast_config_get_worker_group_info(ubs_hcom_multicast_config config, uint32_t index,
                                                    ubs_hcom_multicast_worker_group_info *info)
{
    VALIDATE_MULTICAST_ERR(config);
    if (NN_UNLIKELY(info == nullptr)) {
        NN_LOG_ERROR("Invalid param, info must be correct address");
        return SER_INVALID_PARAM;
    }

    const auto &groups = reinterpret_cast<MulticastConfig *>(config)->GetWorkerGroupInfo();
    if (NN_UNLIKELY(index >= groups.size())) {
        NN_LOG_ERROR("Invalid index " << index << " for worker group info size " << groups.size());
        return SER_INVALID_PARAM;
    }

    const UBSHcomWorkerGroupInfo &group = groups[index];
    info->threadPriority = group.threadPriority;
    info->threadCount = group.threadCount;
    info->groupId = group.groupId;
    info->cpuIdsRangeFirst = group.cpuIdsRange.first;
    info->cpuIdsRangeSecond = group.cpuIdsRange.second;
    return SER_OK;
}

void ubs_hcom_multicast_config_set_periodic_thread_num(ubs_hcom_multicast_config config, uint32_t threadNum)
{
    VALIDATE_MULTICAST_VOID(config);
    reinterpret_cast<MulticastConfig *>(config)->SetPeriodicThreadNum(threadNum);
}

uint32_t ubs_hcom_multicast_config_get_periodic_thread_num(ubs_hcom_multicast_config config)
{
    VALIDATE_MULTICAST_ZERO(config);
    return reinterpret_cast<MulticastConfig *>(config)->GetPeriodicThreadNum();
}

void ubs_hcom_multicast_config_set_max_subscriber_num(ubs_hcom_multicast_config config, uint32_t maxSubscriberNum)
{
    VALIDATE_MULTICAST_VOID(config);
    reinterpret_cast<MulticastConfig *>(config)->SetMaxSubscriberNum(maxSubscriberNum);
}

uint32_t ubs_hcom_multicast_config_get_max_subscriber_num(ubs_hcom_multicast_config config)
{
    VALIDATE_MULTICAST_ZERO(config);
    return reinterpret_cast<MulticastConfig *>(config)->GetMaxSubscriberNum();
}

void ubs_hcom_multicast_config_set_periodic_cpu_id(ubs_hcom_multicast_config config, int32_t cpuId)
{
    VALIDATE_MULTICAST_VOID(config);
    reinterpret_cast<MulticastConfig *>(config)->SetPeriodicCpuId(static_cast<int>(cpuId));
}

int32_t ubs_hcom_multicast_config_get_periodic_cpu_id(ubs_hcom_multicast_config config)
{
    VALIDATE_MULTICAST_ZERO(config);
    return static_cast<int32_t>(reinterpret_cast<MulticastConfig *>(config)->GetPeriodicCpuId());
}

ubs_hcom_multicast_protocol ubs_hcom_multicast_config_get_protocol(ubs_hcom_multicast_config config)
{
    if (NN_UNLIKELY(config == 0)) {
        return C_MULTICAST_UNKNOWN;
    }
    return ConvertProtocolToC(reinterpret_cast<MulticastConfig *>(config)->GetProtocol());
}