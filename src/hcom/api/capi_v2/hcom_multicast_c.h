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
#ifndef HCOM_CAPI_V2_HCOM_MULTICAST_C_H_
#define HCOM_CAPI_V2_HCOM_MULTICAST_C_H_

#include <stdbool.h>
#include <stdint.h>
#include "hcom_c.h"
#include "hcom_service_c.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * @brief Handle of a multicast publisher service, created by
 * ubs_hcom_publisher_service_create and destroyed by ubs_hcom_publisher_service_destroy
 */
typedef uintptr_t ubs_hcom_publisher_service;

/*
 * @brief Handle of a multicast subscriber service, created by
 * ubs_hcom_subscriber_service_create and destroyed by ubs_hcom_subscriber_service_destroy
 */
typedef uintptr_t ubs_hcom_subscriber_service;

/*
 * @brief Handle of a publisher, created by ubs_hcom_publisher_service_create_publisher and destroyed by
 * ubs_hcom_publisher_service_destroy_publisher
 */
typedef uintptr_t ubs_hcom_publisher;

/*
 * @brief Handle of a subscriber, created by ubs_hcom_subscriber_service_create_subscriber and destroyed by
 * ubs_hcom_subscriber_service_destroy_subscriber
 */
typedef uintptr_t ubs_hcom_subscriber;

/*
 * @brief Borrowed handle of a multicast config, obtained from
 * ubs_hcom_publisher_service_get_config / ubs_hcom_subscriber_service_get_config.
 * It is only valid during the lifetime of the owning service and must not be destroyed by the caller
 */
typedef uintptr_t ubs_hcom_multicast_config;

/*
 * @brief Borrowed handle of a subscription info, owned by the publisher.
 * It is valid while the subscription exists in the publisher, do not use it after the subscription is deleted
 */
typedef uintptr_t ubs_hcom_subscription_info;

/*
 * @brief Borrowed handle of a publisher context, only valid within the invoking callback
 */
typedef uintptr_t ubs_hcom_publisher_context;

/*
 * @brief Protocol supported by the multicast driver
 */
typedef enum
{
    C_MULTICAST_RDMA = 0,
    C_MULTICAST_TCP = 1,
    C_MULTICAST_UNKNOWN = 255,
} ubs_hcom_multicast_protocol;

/*
 * @brief Response status of one subscriber for a multicast request
 */
typedef enum
{
    C_MULTICAST_SUB_RSP_SUCCESS = 0,       /* replied with data */
    C_MULTICAST_SUB_RSP_INIT = 1,          /* not sent yet */
    C_MULTICAST_SUB_RSP_SEND_ERROR = 2,    /* send failed */
    C_MULTICAST_SUB_RSP_TIMEOUT = 3,       /* reply timed out */
    C_MULTICAST_SUB_RSP_BROKEN = 4,        /* subscriber is offline */
    C_MULTICAST_SUB_RSP_UNKNOWN_ERROR = 5, /* other unknown error */
} ubs_hcom_multicast_subscriber_rsp_status;

/*
 * @brief Options used to create a multicast service, see MulticastServiceOptions
 *
 * Note: fields with a non-zero default fall back to the default when set to 0, the others are used as they are
 */
typedef struct {
    uint32_t maxSendRecvDataSize;     /* max size of a data block */
    uint32_t maxSendRecvDataCount;    /* max count of data blocks sent at the same time */
    uint32_t multicastIoContextCount; /* in-flight multicast io context count */
    uint16_t workerGroupId;           /* worker group id, starts from 0 and must be unique */
    uint16_t workerGroupThreadCount;  /* worker thread count, no worker is started if it is 0 */
    ubs_hcom_service_worker_mode workerGroupMode;
    int8_t workerThreadPriority;           /* thread priority [-20, 19], 19 is the lowest and -20 the highest */
    uint32_t workerGroupCpuIdsRangeFirst;  /* worker bound cpu range, first value, 0 means unbound */
    uint32_t workerGroupCpuIdsRangeSecond; /* worker bound cpu range, second value, 0 means unbound */
    ubs_hcom_multicast_protocol protocol;  /* multicast driver protocol, RDMA/TCP are supported */
    uint32_t qpSendQueueSize;              /* qp send queue size */
    uint32_t qpRecvQueueSize;              /* qp recv queue size */
    uint32_t qpPrePostSize;                /* qp pre-post size */
    uint32_t qpBatchRePostSize;            /* qp batch re-post size */
    uint16_t completionQueueDepth;         /* cq depth */
    uint32_t maxSubscriberNum;             /* max subscriber count of one publisher */
    uint8_t publisherWrkGroupNo;           /* publisher worker group no the subscriber subscribes to */
    bool enableTls;                        /* whether tls auth and encrypted transport is enabled */
    ubs_hcom_service_cipher_suite cipherSuite;
    int32_t periodicCpuId; /* cpu id bound to the publisher timeout timer thread, -1 unbound */
} ubs_hcom_multicast_service_options;

/*
 * @brief Heartbeat options, see MulticastHeartBeatOptions
 */
typedef struct {
    uint16_t heartBeatIdleSec;          /* interval of sending heartbeat keepalive */
    uint16_t heartBeatProbeTimes;       /* probe times before the connection is treated as broken */
    uint16_t heartBeatProbeIntervalSec; /* interval of resending a probe */
} ubs_hcom_multicast_heartbeat_options;

/*
 * @brief Worker group info, see UBSHcomWorkerGroupInfo
 */
typedef struct {
    int8_t threadPriority;      /* [-20, 19], 19 is the lowest and -20 the highest */
    uint16_t threadCount;       /* total thread count in the worker group */
    uint16_t groupId;           /* worker group id */
    uint32_t cpuIdsRangeFirst;  /* cpu range, first value */
    uint32_t cpuIdsRangeSecond; /* cpu range, second value */
} ubs_hcom_multicast_worker_group_info;

/*
 * @brief A multicast request, see MultiRequest
 */
typedef struct {
    void *data;    /* data address */
    uint32_t size; /* data size */
    uint64_t lkey; /* lkey of the registered memory */
} ubs_hcom_multicast_request;

/*
 * @brief A multicast response, see MultiResponse
 */
typedef struct {
    void *data;    /* data address */
    uint32_t size; /* data size */
} ubs_hcom_multicast_response;

/*
 * @brief Transfer operation info of a multicast request, see UBSHcomNetTransOpInfo
 */
typedef struct {
    uint32_t seqNo;    /* seq no */
    int16_t timeout;   /* timeout in seconds */
    int16_t errorCode; /* error code */
    uint8_t flags;     /* user flags */
} ubs_hcom_multicast_op_info;

/*
 * @brief Response snapshot of one subscriber within a publisher context
 */
typedef struct {
    ubs_hcom_subscription_info subInfo; /* borrowed, valid within the current callback */
    ubs_hcom_multicast_subscriber_rsp_status status;
    void *rspData;    /* borrowed reply data, valid within the current callback */
    uint32_t rspSize; /* reply data size */
} ubs_hcom_multicast_subscriber_rsp_info;

/*
 * @brief Callback invoked when a new subscription request is received, return 0 to accept and non-zero to reject
 *
 * @param info             [in] borrowed subscription info, only valid within the callback
 * @param usrArg           [in] user argument passed on registration
 *
 * @return 0 to accept, non-zero to reject
 */
typedef int (*ubs_hcom_multicast_new_subscription_handler)(ubs_hcom_subscription_info info, void *usrArg);

/*
 * @brief Callback invoked when a subscription is exceptional, such as reply failed or timed out
 *
 * @param info             [in] borrowed subscription info, only valid within the callback
 * @param usrArg           [in] user argument passed on registration
 */
typedef void (*ubs_hcom_multicast_subscription_exception_handler)(ubs_hcom_subscription_info info, void *usrArg);

/*
 * @brief Callback invoked when an endpoint is broken
 *
 * @param ep               [in] borrowed endpoint, only valid within the callback
 * @param usrArg           [in] user argument passed on registration
 */
typedef void (*ubs_hcom_multicast_ep_broken_handler)(ubs_hcom_endpoint ep, void *usrArg);

/*
 * @brief Callback invoked when a subscriber replies to a multicast request
 *
 * @param ctx              [in] borrowed publisher context, only valid within the callback
 * @param usrArg           [in] user argument passed on registration
 *
 * @return 0 if successful, error code if failed
 */
typedef int (*ubs_hcom_multicast_pub_recv_handler)(ubs_hcom_publisher_context ctx, void *usrArg);

/*
 * @brief Callback invoked when a multicast request is received at the subscriber side
 *
 * @param ctx              [in] borrowed service context, only valid within the callback
 * @param usrArg           [in] user argument passed on registration
 *
 * @return 0 if successful, error code if failed
 */
typedef int (*ubs_hcom_multicast_req_recv_handler)(ubs_hcom_service_context ctx, void *usrArg);

/*
 * @brief Callback invoked when a multicast request is posted
 *
 * @param ctx              [in] borrowed service context, only valid within the callback
 * @param usrArg           [in] user argument passed on registration
 *
 * @return 0 if successful, error code if failed
 */
typedef int (*ubs_hcom_multicast_req_posted_handler)(ubs_hcom_service_context ctx, void *usrArg);

/*
 * @brief Callback invoked when a multicast call is completed or timed out
 *
 * @param ctx              [in] borrowed publisher context, only valid within the callback
 * @param usrArg           [in] user argument passed to ubs_hcom_publisher_call
 */
typedef void (*ubs_hcom_multicast_call_done_handler)(ubs_hcom_publisher_context ctx, void *usrArg);

/*
 * @brief Create a multicast publisher service
 *
 * @param name             [in] name of the service
 * @param options          [in] options used to create the service, nullptr for defaults
 * @param service          [out] created service, set to 0 if failed
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_publisher_service_create(const char *name, const ubs_hcom_multicast_service_options *options,
                                      ubs_hcom_publisher_service *service);

/*
 * @brief Destroy a multicast publisher service
 *
 * @param service          [in] service handler
 * @param name             [in] name of the instance to be destroyed
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_publisher_service_destroy(ubs_hcom_publisher_service service, const char *name);

/*
 * @brief Start the publisher service
 *
 * @param service          [in] service handler
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_publisher_service_start(ubs_hcom_publisher_service service);

/*
 * @brief Stop the publisher service
 *
 * @param service          [in] service handler
 */
void ubs_hcom_publisher_service_stop(ubs_hcom_publisher_service service);

/*
 * @brief Create a publisher
 *
 * @param service          [in] service handler
 * @param publisher        [out] created publisher, set to 0 if failed
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_publisher_service_create_publisher(ubs_hcom_publisher_service service, ubs_hcom_publisher *publisher);

/*
 * @brief Destroy a publisher
 *
 * @param service          [in] service handler
 * @param publisher        [in] publisher to be destroyed
 */
void ubs_hcom_publisher_service_destroy_publisher(ubs_hcom_publisher_service service, ubs_hcom_publisher publisher);

/*
 * @brief Bind the listener url and specify the handler invoked when a subscription request is received
 *
 * @param service          [in] service handler
 * @param listenerUrl      [in] url to listen on, for tcp: tcp://127.0.0.1:9981
 * @param handler          [in] handler invoked when a new subscription request is received
 * @param usrArg           [in] user argument passed through to the handler
 * @param cpuId            [in] cpu id bound to the listening thread, -1 means unbound
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_publisher_service_bind(ubs_hcom_publisher_service service, const char *listenerUrl,
                                    ubs_hcom_multicast_new_subscription_handler handler, void *usrArg, int32_t cpuId);

/*
 * @brief Get the multicast config used for advanced configuration
 *
 * @param service          [in] service handler
 *
 * @return borrowed config handle, 0 if failed
 */
ubs_hcom_multicast_config ubs_hcom_publisher_service_get_config(ubs_hcom_publisher_service service);

/*
 * @brief Register the handler invoked when a subscription is exceptional
 *
 * @param service          [in] service handler
 * @param handler          [in] handler to be registered
 * @param usrArg           [in] user argument passed through to the handler
 */
void ubs_hcom_publisher_service_register_subscription_exception_handler(
    ubs_hcom_publisher_service service, ubs_hcom_multicast_subscription_exception_handler handler, void *usrArg);

/*
 * @brief Register the handler invoked when an endpoint is broken
 *
 * @param service          [in] service handler
 * @param handler          [in] handler to be registered
 * @param usrArg           [in] user argument passed through to the handler
 */
void ubs_hcom_publisher_service_register_broken_handler(ubs_hcom_publisher_service service,
                                                        ubs_hcom_multicast_ep_broken_handler handler, void *usrArg);

/*
 * @brief Register the handler invoked when a multicast request is posted
 *
 * @param service          [in] service handler
 * @param handler          [in] handler to be registered
 * @param usrArg           [in] user argument passed through to the handler
 */
void ubs_hcom_publisher_service_register_send_handler(ubs_hcom_publisher_service service,
                                                      ubs_hcom_multicast_req_posted_handler handler, void *usrArg);

/*
 * @brief Register the handler invoked when a subscriber replies to a multicast request
 *
 * @param service          [in] service handler
 * @param handler          [in] handler to be registered
 * @param usrArg           [in] user argument passed through to the handler
 */
void ubs_hcom_publisher_service_register_pub_recv_handler(ubs_hcom_publisher_service service,
                                                          ubs_hcom_multicast_pub_recv_handler handler, void *usrArg);

/*
 * @brief Register the TLS callbacks of the publisher service
 *
 * @param service          [in] service handler
 * @param certCb           [in] callback to get the certificate file
 * @param priKeyCb         [in] callback to get the private key file and the keyPass
 * @param caCb             [in] callback to get the CA and verify
 */
void ubs_hcom_publisher_service_register_tls_callbacks(ubs_hcom_publisher_service service,
                                                       ubs_hcom_tls_get_cert_cb certCb, ubs_hcom_tls_get_pk_cb priKeyCb,
                                                       ubs_hcom_tls_get_ca_cb caCb);

/*
 * @brief Add a worker group to the publisher service
 *
 * @param service          [in] service handler
 * @param workerGroupId    [in] id of the worker group
 * @param threadCount      [in] thread count of the worker group
 * @param cpuIdsRangeFirst [in] cpu range first value
 * @param cpuIdsRangeSecond[in] cpu range second value
 * @param priority         [in] thread nice value, [-20, 19], -20 is the highest and 19 the lowest
 */
void ubs_hcom_publisher_service_add_workergroup(ubs_hcom_publisher_service service, uint16_t workerGroupId,
                                                uint32_t threadCount, uint32_t cpuIdsRangeFirst,
                                                uint32_t cpuIdsRangeSecond, int8_t priority);

/*
 * @brief Register a memory region, the memory is allocated internally
 *
 * @param service          [in] service handler
 * @param size             [in] size of the memory region
 * @param mr               [out] registered memory region, set to 0 if failed
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_publisher_service_register_memory_region(ubs_hcom_publisher_service service, uint64_t size,
                                                      ubs_hcom_memory_region *mr);

/*
 * @brief Register a memory region with the memory allocated by the caller
 *
 * @param service          [in] service handler
 * @param address          [in] start address of the memory to be registered as MR
 * @param size             [in] size of the memory region
 * @param mr               [out] registered memory region, set to 0 if failed
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_publisher_service_register_assign_memory_region(ubs_hcom_publisher_service service, uintptr_t address,
                                                             uint64_t size, ubs_hcom_memory_region *mr);

/*
 * @brief Unregister a memory region
 *
 * @param service          [in] service handler
 * @param mr               [in] memory region to be unregistered
 */
void ubs_hcom_publisher_service_destroy_memory_region(ubs_hcom_publisher_service service, ubs_hcom_memory_region mr);

/*
 * @brief Create a multicast subscriber service
 *
 * @param name             [in] name of the service
 * @param options          [in] options used to create the service, nullptr for defaults
 * @param service          [out] created service, set to 0 if failed
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_subscriber_service_create(const char *name, const ubs_hcom_multicast_service_options *options,
                                       ubs_hcom_subscriber_service *service);

/*
 * @brief Destroy a multicast subscriber service
 *
 * @param service          [in] service handler
 * @param name             [in] name of the instance to be destroyed
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_subscriber_service_destroy(ubs_hcom_subscriber_service service, const char *name);

/*
 * @brief Start the subscriber service
 *
 * @param service          [in] service handler
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_subscriber_service_start(ubs_hcom_subscriber_service service);

/*
 * @brief Stop the subscriber service
 *
 * @param service          [in] service handler
 */
void ubs_hcom_subscriber_service_stop(ubs_hcom_subscriber_service service);

/*
 * @brief Create a subscriber
 *
 * @param service          [in] service handler
 * @param serverUrl        [in] target url, for tcp: tcp://127.0.0.1:9981
 * @param subscriber       [out] created subscriber, set to 0 if failed
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_subscriber_service_create_subscriber(ubs_hcom_subscriber_service service, const char *serverUrl,
                                                  ubs_hcom_subscriber *subscriber);

/*
 * @brief Create a subscriber with the peer publisher worker group specified
 *
 * @param service          [in] service handler
 * @param serverUrl        [in] target url, for tcp: tcp://127.0.0.1:9981
 * @param publisherGroupIdx[in] peer publisher worker group index
 * @param subscriber       [out] created subscriber, set to 0 if failed
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_subscriber_service_create_subscriber_with_group(ubs_hcom_subscriber_service service, const char *serverUrl,
                                                             uint16_t publisherGroupIdx,
                                                             ubs_hcom_subscriber *subscriber);

/*
 * @brief Destroy a subscriber
 *
 * @param service          [in] service handler
 * @param subscriber       [in] subscriber to be destroyed
 */
void ubs_hcom_subscriber_service_destroy_subscriber(ubs_hcom_subscriber_service service,
                                                    ubs_hcom_subscriber subscriber);

/*
 * @brief Get the multicast config used for advanced configuration
 *
 * @param service          [in] service handler
 *
 * @return borrowed config handle, 0 if failed
 */
ubs_hcom_multicast_config ubs_hcom_subscriber_service_get_config(ubs_hcom_subscriber_service service);

/*
 * @brief Register the handler invoked when an endpoint is broken
 *
 * @param service          [in] service handler
 * @param handler          [in] handler to be registered
 * @param usrArg           [in] user argument passed through to the handler
 */
void ubs_hcom_subscriber_service_register_broken_handler(ubs_hcom_subscriber_service service,
                                                         ubs_hcom_multicast_ep_broken_handler handler, void *usrArg);

/*
 * @brief Register the handler invoked when a multicast request is received
 *
 * @param service          [in] service handler
 * @param handler          [in] handler to be registered
 * @param usrArg           [in] user argument passed through to the handler
 */
void ubs_hcom_subscriber_service_register_recv_handler(ubs_hcom_subscriber_service service,
                                                       ubs_hcom_multicast_req_recv_handler handler, void *usrArg);

/*
 * @brief Register the TLS callbacks of the subscriber service
 *
 * @param service          [in] service handler
 * @param certCb           [in] callback to get the certificate file
 * @param priKeyCb         [in] callback to get the private key file and the keyPass
 * @param caCb             [in] callback to get the CA and verify
 */
void ubs_hcom_subscriber_service_register_tls_callbacks(ubs_hcom_subscriber_service service,
                                                        ubs_hcom_tls_get_cert_cb certCb,
                                                        ubs_hcom_tls_get_pk_cb priKeyCb, ubs_hcom_tls_get_ca_cb caCb);

/*
 * @brief Publish a multicast message to all subscribers, which need to reply
 *
 * @param publisher        [in] publisher handler
 * @param opInfo           [in] operation info, mainly the timeout
 * @param req              [in] multicast request
 * @param done             [in] callback invoked when all subscribers replied or the call timed out, must not be nullptr
 * @param usrArg           [in] user argument passed through to the callback
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_publisher_call(ubs_hcom_publisher publisher, ubs_hcom_multicast_op_info opInfo,
                            ubs_hcom_multicast_request req, ubs_hcom_multicast_call_done_handler done, void *usrArg);

/*
 * @brief Publish a multicast message to the subscriber with the given ip, which needs to reply
 *
 * @param publisher        [in] publisher handler
 * @param opInfo           [in] operation info, mainly the timeout
 * @param remoteIp         [in] ip of the target subscriber
 * @param req              [in] multicast request
 * @param done             [in] callback invoked when the subscriber replied or the call timed out, must not be nullptr
 * @param usrArg           [in] user argument passed through to the callback
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_publisher_call_to(ubs_hcom_publisher publisher, ubs_hcom_multicast_op_info opInfo, const char *remoteIp,
                               ubs_hcom_multicast_request req, ubs_hcom_multicast_call_done_handler done, void *usrArg);

/*
 * @brief Add a subscription to the publisher
 *
 * @param publisher        [in] publisher handler
 * @param info             [in] subscription info, usually obtained from the new subscription callback
 *
 * @return true if successful, false if failed
 */
bool ubs_hcom_publisher_add_subscription(ubs_hcom_publisher publisher, ubs_hcom_subscription_info info);

/*
 * @brief Delete a subscription from the publisher
 *
 * @param publisher        [in] publisher handler
 * @param info             [in] subscription info to be deleted
 *
 * @return true if successful, false if failed
 */
bool ubs_hcom_publisher_del_subscription(ubs_hcom_publisher publisher, ubs_hcom_subscription_info info);

/*
 * @brief Get the subscriber count of the publisher
 *
 * @param publisher        [in] publisher handler
 *
 * @return subscriber count
 */
uint32_t ubs_hcom_publisher_get_subscriber_num(ubs_hcom_publisher publisher);

/*
 * @brief Get all subscriber info of the publisher, only used once after initialization
 *
 * Note: the returned handles are borrowed from the publisher and must not be destroyed by the caller
 *
 * @param publisher        [in] publisher handler
 * @param infos            [out] array filled with subscription info handles, may be nullptr to query the count
 * @param count            [in,out] in: capacity of the array, out: actual subscriber count
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_publisher_get_all_subscriber_info(ubs_hcom_publisher publisher, ubs_hcom_subscription_info *infos,
                                               uint32_t *count);

/*
 * @brief Query the subscription info by the endpoint id
 *
 * Note: the returned handle is borrowed from the publisher and must not be destroyed by the caller
 *
 * @param publisher        [in] publisher handler
 * @param epId             [in] endpoint id
 * @param info             [out] subscription info handle, set to 0 if not found
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_publisher_get_subscribe_by_ep_id(ubs_hcom_publisher publisher, uint64_t epId,
                                              ubs_hcom_subscription_info *info);

/*
 * @brief Get the id of a subscription
 *
 * @param info             [in] subscription info handle
 *
 * @return subscription id
 */
uint64_t ubs_hcom_subscription_info_get_id(ubs_hcom_subscription_info info);

/*
 * @brief Get the name of a subscription, borrowed and only valid while the subscription info is alive
 *
 * @param info             [in] subscription info handle
 *
 * @return subscription name
 */
const char *ubs_hcom_subscription_info_get_name(ubs_hcom_subscription_info info);

/*
 * @brief Get the ip of a subscription, borrowed and only valid while the subscription info is alive
 *
 * @param info             [in] subscription info handle
 *
 * @return subscription ip
 */
const char *ubs_hcom_subscription_info_get_ip(ubs_hcom_subscription_info info);

/*
 * @brief Get the port of a subscription
 *
 * @param info             [in] subscription info handle
 *
 * @return subscription port
 */
uint16_t ubs_hcom_subscription_info_get_port(ubs_hcom_subscription_info info);

/*
 * @brief Get the subscriber response count of a publisher context
 *
 * @param ctx              [in] borrowed publisher context
 *
 * @return subscriber response count
 */
uint32_t ubs_hcom_publisher_context_get_subscriber_count(ubs_hcom_publisher_context ctx);

/*
 * @brief Get the count of subscribers that have already replied within a publisher context
 *
 * @param ctx              [in] borrowed publisher context
 *
 * @return replied subscriber count
 */
uint32_t ubs_hcom_publisher_context_get_reply_count(ubs_hcom_publisher_context ctx);

/*
 * @brief Get the count of subscribers the multicast request was sent to within a publisher context
 *
 * @param ctx              [in] borrowed publisher context
 *
 * @return sent subscriber count
 */
uint32_t ubs_hcom_publisher_context_get_send_count(ubs_hcom_publisher_context ctx);

/*
 * @brief Get the subscriber response snapshot of a publisher context by index
 *
 * @param ctx              [in] borrowed publisher context
 * @param index            [in] index in [0, count)
 * @param info             [out] filled subscriber response snapshot
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_publisher_context_get_subscriber_rsp_info(ubs_hcom_publisher_context ctx, uint32_t index,
                                                       ubs_hcom_multicast_subscriber_rsp_info *info);

/*
 * @brief Close a subscriber
 *
 * @param subscriber       [in] subscriber handler
 */
void ubs_hcom_subscriber_close(ubs_hcom_subscriber subscriber);

/*
 * @brief Get the ip of a subscriber, borrowed and only valid while the subscriber is alive
 *
 * @param subscriber       [in] subscriber handler
 *
 * @return subscriber ip
 */
const char *ubs_hcom_subscriber_get_ip(ubs_hcom_subscriber subscriber);

/*
 * @brief Get the port of a subscriber
 *
 * @param subscriber       [in] subscriber handler
 *
 * @return subscriber port
 */
uint16_t ubs_hcom_subscriber_get_port(ubs_hcom_subscriber subscriber);

/*
 * @brief Reply a multicast request, used by the subscriber recv handler together with ubs_hcom_publisher_call
 *
 * @param ctx              [in] borrowed service context from the subscriber recv handler
 * @param req              [in] reply data
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_multicast_context_reply(ubs_hcom_service_context ctx, ubs_hcom_multicast_request req);

/*
 * @brief Get the name of the multicast config, borrowed and only valid while the config is alive
 *
 * @param config           [in] config handle
 *
 * @return config name
 */
const char *ubs_hcom_multicast_config_get_name(ubs_hcom_multicast_config config);

/*
 * @brief Set the device ipMsks used by rdma/ub
 *
 * @param config           [in] config handle
 * @param ipMasks          [in] array of ip masks
 * @param count            [in] count of ip masks
 */
void ubs_hcom_multicast_config_set_device_ip_mask(ubs_hcom_multicast_config config, const char *const *ipMasks,
                                                  uint32_t count);

/*
 * @brief Get the count of the device ipMasks
 *
 * @param config           [in] config handle
 *
 * @return count of ip masks
 */
uint32_t ubs_hcom_multicast_config_get_device_ip_mask_count(ubs_hcom_multicast_config config);

/*
 * @brief Get the device ipMask by index, borrowed and only valid while the config is alive
 *
 * @param config           [in] config handle
 * @param index            [in] index in [0, count)
 *
 * @return ip mask, nullptr if the index is invalid
 */
const char *ubs_hcom_multicast_config_get_device_ip_mask(ubs_hcom_multicast_config config, uint32_t index);

/*
 * @brief Set the depth of the cq queue
 *
 * @param config           [in] config handle
 * @param depth            [in] cq queue depth
 */
void ubs_hcom_multicast_config_set_cq_depth(ubs_hcom_multicast_config config, uint16_t depth);

/*
 * @brief Get the depth of the cq queue
 *
 * @param config           [in] config handle
 *
 * @return cq queue depth
 */
uint16_t ubs_hcom_multicast_config_get_cq_depth(ubs_hcom_multicast_config config);

/*
 * @brief Set the size of the SQ queue
 *
 * @param config           [in] config handle
 * @param sqSize           [in] queue size
 */
void ubs_hcom_multicast_config_set_sq_size(ubs_hcom_multicast_config config, uint32_t sqSize);

/*
 * @brief Get the size of the SQ queue
 *
 * @param config           [in] config handle
 *
 * @return queue size
 */
uint32_t ubs_hcom_multicast_config_get_sq_size(ubs_hcom_multicast_config config);

/*
 * @brief Set the size of the RQ queue
 *
 * @param config           [in] config handle
 * @param rqSize           [in] queue size
 */
void ubs_hcom_multicast_config_set_rq_size(ubs_hcom_multicast_config config, uint32_t rqSize);

/*
 * @brief Get the size of the RQ queue
 *
 * @param config           [in] config handle
 *
 * @return queue size
 */
uint32_t ubs_hcom_multicast_config_get_rq_size(ubs_hcom_multicast_config config);

/*
 * @brief Set the number of pre-posted wr
 *
 * @param config           [in] config handle
 * @param prePostSize      [in] number of pre-posted wr
 */
void ubs_hcom_multicast_config_set_prepost_size(ubs_hcom_multicast_config config, uint32_t prePostSize);

/*
 * @brief Get the number of pre-posted wr
 *
 * @param config           [in] config handle
 *
 * @return number of pre-posted wr
 */
uint32_t ubs_hcom_multicast_config_get_prepost_size(ubs_hcom_multicast_config config);

/*
 * @brief Set the polling batch size
 *
 * @param config           [in] config handle
 * @param pollSize         [in] batch size
 */
void ubs_hcom_multicast_config_set_polling_batchsize(ubs_hcom_multicast_config config, uint16_t pollSize);

/*
 * @brief Get the polling batch size
 *
 * @param config           [in] config handle
 *
 * @return batch size
 */
uint16_t ubs_hcom_multicast_config_get_polling_batchsize(ubs_hcom_multicast_config config);

/*
 * @brief Set the event polling timeout in us
 *
 * @param config           [in] config handle
 * @param pollTimeout      [in] timeout
 */
void ubs_hcom_multicast_config_set_polling_timeoutus(ubs_hcom_multicast_config config, uint16_t pollTimeout);

/*
 * @brief Get the event polling timeout in us
 *
 * @param config           [in] config handle
 *
 * @return timeout
 */
uint16_t ubs_hcom_multicast_config_get_polling_timeoutus(ubs_hcom_multicast_config config);

/*
 * @brief Set the heartbeat options
 *
 * @param config           [in] config handle
 * @param opt              [in] heartbeat options
 */
void ubs_hcom_multicast_config_set_heartbeat(ubs_hcom_multicast_config config,
                                             ubs_hcom_multicast_heartbeat_options opt);

/*
 * @brief Get the heartbeat options
 *
 * @param config           [in] config handle
 *
 * @return heartbeat options
 */
ubs_hcom_multicast_heartbeat_options ubs_hcom_multicast_config_get_heartbeat(ubs_hcom_multicast_config config);

/*
 * @brief Set the max count of data blocks to send
 *
 * @param config           [in] config handle
 * @param count            [in] max count of data blocks
 */
void ubs_hcom_multicast_config_set_max_send_recv_data_count(ubs_hcom_multicast_config config, uint32_t count);

/*
 * @brief Get the max count of data blocks to send
 *
 * @param config           [in] config handle
 *
 * @return max count of data blocks
 */
uint32_t ubs_hcom_multicast_config_get_max_send_recv_data_count(ubs_hcom_multicast_config config);

/*
 * @brief Set the max size of a data block to send
 *
 * @param config           [in] config handle
 * @param size             [in] max size of a data block
 */
void ubs_hcom_multicast_config_set_max_send_recv_data_size(ubs_hcom_multicast_config config, uint32_t size);

/*
 * @brief Get the max size of a data block to send
 *
 * @param config           [in] config handle
 *
 * @return max size of a data block
 */
uint32_t ubs_hcom_multicast_config_get_max_send_recv_data_size(ubs_hcom_multicast_config config);

/*
 * @brief Set the worker group info
 *
 * @param config           [in] config handle
 * @param info             [in] array of worker group info
 * @param count            [in] count of worker group info
 */
void ubs_hcom_multicast_config_set_worker_group_info(ubs_hcom_multicast_config config,
                                                     const ubs_hcom_multicast_worker_group_info *info, uint32_t count);

/*
 * @brief Get the count of the worker group info
 *
 * @param config           [in] config handle
 *
 * @return count of worker group info
 */
uint32_t ubs_hcom_multicast_config_get_worker_group_info_count(ubs_hcom_multicast_config config);

/*
 * @brief Get the worker group info by index
 *
 * @param config           [in] config handle
 * @param index            [in] index in [0, count)
 * @param info             [out] filled worker group info
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_multicast_config_get_worker_group_info(ubs_hcom_multicast_config config, uint32_t index,
                                                    ubs_hcom_multicast_worker_group_info *info);

/*
 * @brief Set the thread number of periodic task processing
 *
 * @param config           [in] config handle
 * @param threadNum        [in] thread number
 */
void ubs_hcom_multicast_config_set_periodic_thread_num(ubs_hcom_multicast_config config, uint32_t threadNum);

/*
 * @brief Get the thread number of periodic task processing
 *
 * @param config           [in] config handle
 *
 * @return thread number
 */
uint32_t ubs_hcom_multicast_config_get_periodic_thread_num(ubs_hcom_multicast_config config);

/*
 * @brief Set the max subscriber count of one publisher
 *
 * @param config           [in] config handle
 * @param maxSubscriberNum [in] max subscriber count
 */
void ubs_hcom_multicast_config_set_max_subscriber_num(ubs_hcom_multicast_config config, uint32_t maxSubscriberNum);

/*
 * @brief Get the max subscriber count of one publisher
 *
 * @param config           [in] config handle
 *
 * @return max subscriber count
 */
uint32_t ubs_hcom_multicast_config_get_max_subscriber_num(ubs_hcom_multicast_config config);

/*
 * @brief Set the cpu id bound to the periodic timer thread
 *
 * @param config           [in] config handle
 * @param cpuId            [in] cpu id, -1 unbound
 */
void ubs_hcom_multicast_config_set_periodic_cpu_id(ubs_hcom_multicast_config config, int32_t cpuId);

#ifdef __cplusplus
}
#endif

#endif // HCOM_CAPI_V2_HCOM_MULTICAST_C_H_