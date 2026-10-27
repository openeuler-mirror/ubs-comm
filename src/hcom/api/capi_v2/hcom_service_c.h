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
#ifndef HCOM_CAPI_V2_HCOM_SERVICE_C_V2_H_
#define HCOM_CAPI_V2_HCOM_SERVICE_C_V2_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hcom_c.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * @brief service, which include oob & multi protocols(TCP/RDMA/SHM) workers & callback etc
 */
typedef uintptr_t ubs_hcom_service;

/*
 * @brief Channel, represent multi connections(EPs) of one protocol
 *
 * two side operation, ubs_hcom_channel_send
 * read operation from remote, ubs_hcom_channel_get
 * write operation from remote, ubs_hcom_channel_put
 */
typedef uintptr_t ubs_hcom_channel;

/*
 * @brief Service context, which used for callback as param
 */
typedef uintptr_t ubs_hcom_service_context;

/*
 * Callback function which will be invoked by async use mode
 */
typedef void (*ubs_hcom_channel_cb_func)(void *arg, ubs_hcom_service_context context);
/*
 * @brief Callback function definition
 * 1) new channel connected from client, only need to register this at server side
 *    ipPort: peer address in the form of "ip:port"
 *    usrCtx: user context, always 0 when the handler is registered by ubs_hcom_service_bind
 *    returning a value other than SER_OK makes the server destroy the channel
 * 2) channel is broken, called when RDMA qp detection error or broken
 *    ipPort: always an empty string, the peer address is not available in this case
 *    usrCtx: user context passed when registering the handler
 */
typedef int (*ubs_hcom_service_channel_handler)(ubs_hcom_channel channel, const char *ipPort, uint64_t usrCtx,
                                                const char *payLoad);
typedef void (*ubs_hcom_service_idle_handler)(uint8_t wkrGrpIdx, uint16_t idxInGrp, uint64_t usrCtx);
typedef int (*ubs_hcom_service_request_handler)(ubs_hcom_service_context ctx, uint64_t usrCtx);

/*
 * @brief keyPass          [in] erase function
 * @param keyPass          [in] the memory address of keyPass
 */
typedef void (*ubs_hcom_tls_keypass_erase)(char *keyPass, int len);

/*
 * @brief The cert verify function
 *
 * @param x509             [in] the x509 object of CA
 * @param crlPath          [in] the crl file path
 *
 * @return -1 for failed, and 1 for success
 */
typedef int (*ubs_hcom_tls_cert_verify)(void *x509, const char *crlPath);

/*
 * @brief Get the certificate file of public key
 *
 * @param name             [out] the name
 * @param certPath         [out] the path of certificate
 */
typedef int (*ubs_hcom_tls_get_cert_cb)(const char *name, char **certPath);

/*
 * @brief Get private key file's path and length, and get the keyPass
 * @param name             [out] the name
 * @param priKeyPath       [out] the path of private key
 * @param keyPass          [out] the keyPass
 * @param keyPassLen       [out] the length of keyPass
 * @param erase            [out] the erase function
 */
typedef int (*ubs_hcom_tls_get_pk_cb)(const char *name, char **priKeyPath, char **keyPass, int *keyPassLen,
                                      ubs_hcom_tls_keypass_erase *erase);

/*
 * @brief Get the CA and verify
 * @param name             [out] the name
 * @param caPath           [out] the path of CA file
 * @param crlPath          [out] the crl file path
 * @param verifyType       [out] the type of verify in[VERIFY_BY_NONE,VERIFY_BY_DEFAULT, VERIFY_BY_CUSTOM_FUNC]
 * @param verify           [out] the verify function, only effect in VERIFY_BY_CUSTOM_FUNC mode
 */
typedef int (*ubs_hcom_tls_get_ca_cb)(const char *name, char **caPath, char **crlPath,
                                      ubs_hcom_peer_cert_verify_type *verifyType, ubs_hcom_tls_cert_verify *verify);

/*
 * @brief Callback function of PSK check, set for client
 *
 * @param ssl              [in] SSL connection pointer
 * @param md               [in] digest algorithm
 * @param id               [out] the identity that the client gives to server uses to find the psk
 * @param idlen            [out] the id length
 * @param sess             [out] SSL session
 *
 * @return 1 on success or 0 on failure
 */
typedef int (*ubs_hcom_psk_use_session_cb)(void *ssl, const void *md, const unsigned char **id, size_t *idlen,
                                           void **sess);

/*
 * @brief Callback function of PSK check, set for server
 *
 * @param ssl              [in] SSL connection pointer
 * @param identity         [in] client's identity (provided by the client)
 * @param identityLen      [in] length of the client's identity
 * @param sess             [out] SSL session
 *
 * @return 1 on success or 0 on failure
 */
typedef int (*ubs_hcom_psk_find_session_cb)(void *ssl, const unsigned char *identity, size_t identityLen, void **sess);

/*
 * @brief Sec callback function, when oob connect build, this function will be called to generate auth info.
 * if this function not set secure type is C_NET_SEC_NO_VALID and oob will not send secure info
 *
 * @param ctx              [in] ctx from connect param ctx, and will send in auth process
 * @param flag             [out] flag to sent in auth process
 * @param type             [out] secure type, value should set in oob client, and should in [C_NET_SEC_ONE_WAY,
 *                               C_NET_SEC_TWO_WAY]
 * @param output           [out] secure info created
 * @param outLen           [out] secure info length
 * @param needAutoFree     [out] secure info need to auto free in hcom or not
 */
typedef int (*ubs_hcom_secinfo_provider)(uint64_t ctx, int64_t *flag, ubs_hcom_driver_sec_type *type, char **output,
                                         uint32_t *outLen, int *needAutoFree);

/*
 * @brief ValidateSecInfo callback function, when oob connect build, this function will be called to validate auth info
 * if this function not set oob will not validate secure info
 *
 * @param flag             [in] flag received in auth process
 * @param ctx              [in] ctx received in auth process
 * @param input            [in] secure info received
 * @param inputLen         [in] secure info length
 */
typedef int (*ubs_hcom_secinfo_validator)(uint64_t ctx, int64_t flag, const char *input, uint32_t inputLen);

/*
 * @brief External log callback function
 *
 * @param level            [in] level, 0/1/2/3 represent debug/info/warn/error
 * @param msg              [in] message, log message with name:code-line-number
 */
typedef void (*ubs_hcom_log_handler)(int level, const char *msg);

/*
 * @brief Worker polling type
 * 1 For RDMA:
 * C_BUSY_POLLING, means cpu 100% polling no matter there is request cb, better performance but cost dedicated CPU
 * C_EVENT_POLLING, waiting on OS kernel for request cb
 * 2 For TCP/UDS
 * only event pooling is supported
 */
typedef enum
{
    C_SERVICE_BUSY_POLLING = 0,
    C_SERVICE_EVENT_POLLING = 1,
} ubs_hcom_service_worker_mode;

typedef enum
{
    C_CLIENT_WORKER_POLL = 0,
    C_CLIENT_SELF_POLL_BUSY = 1,
    C_CLIENT_SELF_POLL_EVENT = 2,
    C_CLIENT_UNKNOWN = 255,
} ubs_hcom_service_polling_mode;

typedef enum
{
    C_CHANNEL_FUNC_CB = 0,   // use channel function param (const NetCallback *cb)
    C_CHANNEL_GLOBAL_CB = 1, // use service RegisterOpHandler
} ubs_hcom_channel_cb_type;

typedef enum
{
    HIGH_LEVEL_BLOCK, /* spin-wait by busy loop */
    LOW_LEVEL_BLOCK,  /* full sleep */
} ubs_hcom_channel_flowctl_level;

typedef enum
{
    C_SERVICE_RDMA = 0,
    C_SERVICE_TCP = 1,
    C_SERVICE_UDS = 2,
    C_SERVICE_SHM = 3,
    C_SERVICE_UBC = 7,
    C_SERVICE_UNKNOWN = 255,
} ubs_hcom_service_type;

typedef enum
{
    C_CHANNEL_BROKEN_ALL = 0, /* when one ep broken, all eps broken */
    C_CHANNEL_RECONNECT = 1,  /* when one ep broken, try re-connect first. If re-connect fail, broken all eps */
    C_CHANNEL_KEEP_ALIVE = 2, /* when one ep broken, keep left eps alive until all eps broken */
} ubs_hcom_service_channel_policy;

/*
 * @brief Enum for callback register [new endpoint connected or endpoint broken]
 */
typedef enum
{
    C_CHANNEL_NEW = 0,
    C_CHANNEL_BROKEN = 1,
} ubs_hcom_service_channel_handler_type;

typedef enum
{
    C_SERVICE_REQUEST_RECEIVED = 0,
    C_SERVICE_REQUEST_POSTED = 1,
    C_SERVICE_READWRITE_DONE = 2,
} ubs_hcom_service_handler_type;

typedef enum
{
    SERVICE_ROUND_ROBIN = 0,
    SERVICE_HASH_IP_PORT = 1,
} ubs_hcom_service_lb_policy;

typedef enum
{
    C_SERVICE_TLS_1_2 = 771,
    C_SERVICE_TLS_1_3 = 772,
} ubs_hcom_service_tls_version;

typedef enum
{
    C_SERVICE_AES_GCM_128 = 0,
    C_SERVICE_AES_GCM_256 = 1,
    C_SERVICE_AES_CCM_128 = 2,
    C_SERVICE_CHACHA20_POLY1305 = 3,
} ubs_hcom_service_cipher_suite;

typedef enum
{
    C_SERVICE_NET_SEC_DISABLED = 0,
    C_SERVICE_NET_SEC_ONE_WAY = 1,
    C_SERVICE_NET_SEC_TWO_WAY = 2,
} ubs_hcom_service_secure_type;

/*
 * @brief Enum for UBC mode
 */
typedef enum
{
    C_SERVICE_LOWLATENCY = 0,
    C_SERVICE_HIGHBANDWIDTH = 1,
} ubs_hcom_service_ubc_mode;

/*
 * @brief Context type, part of ubs_hcom_service_context, sync mode is not aware most of them
 */
typedef enum
{
    SERVICE_RECEIVED = 0,     /* support invoke all functions */
    SERVICE_RECEIVED_RAW = 1, /* support invoke most functions except Service_GetOpInfo() */
    SERVICE_SENT = 2,         /* support invoke basic functions except
                                 Service_GetMessage() * 3、Service_GetRspCtx()、 */
    SERVICE_SENT_RAW = 3,     /* support invoke basic functions except
                                 Service_GetMessage() * 3、、Service_GetRspCtx()、Service_GetOpInfo() */
    SERVICE_ONE_SIDE = 4,     /* support invoke basic functions except
                                 Service_GetMessage() * 3、、Service_GetRspCtx()、Service_GetOpInfo() */
    SERVICE_RNDV = 5,
    SERVICE_RNDV_SGL = 6,
    SERVICE_MULTIRAIL_RNDV_RAW = 7,
    SERVICE_INVALID_OP_TYPE = 255,
} ubs_hcom_service_context_type;

typedef struct {
    uint32_t maxSendRecvDataSize;
    uint16_t workerGroupId;
    uint16_t workerGroupThreadCount;
    ubs_hcom_service_worker_mode workerGroupMode;
    int8_t workerThreadPriority;
    char workerGroupCpuRange[64];      // worker group cpu range, for example 6-10
    uint8_t disableMemPoolThreadCache; // 0 = mem pool thread-local cache on (default)
} ubs_hcom_service_options;

typedef struct {
    ubs_hcom_channel_cb_func cb; // User callback function
    void *arg;                   // Argument of callback
} ubs_hcom_channel_callback;

typedef struct {
    uint16_t clientGroupId; // worker group id of client
    uint16_t serverGroupId; // worker group id of server
    uint8_t linkCount;      // actual link count of the channel
    ubs_hcom_service_polling_mode mode;
    ubs_hcom_channel_cb_type cbType;
    char payLoad[512];
} ubs_hcom_service_connect_options;

typedef struct {
    void *address; /* pointer of data */
    uint32_t size; /* size of data */
    uint64_t key;  /* memory key of the data, used by RNDV, 0 if not used */
    uint16_t opcode;
} ubs_hcom_channel_request;

typedef struct {
    void *address;     /* pointer of data */
    uint32_t size;     /* size of data */
    int16_t errorCode; /* error code of response */
} ubs_hcom_channel_response;

typedef struct {
    void *rspCtx;
    int16_t errorCode;
} ubs_hcom_channel_reply_context;

typedef struct {
    uint64_t keys[4];
    uint64_t tokens[4];
    uint8_t eid[16];
} ubs_hcom_oneside_key;

/*
 * @brief Read/write mr info for one side rdma operation
 */
typedef struct {
    uintptr_t lAddress;        // local memory region address
    ubs_hcom_oneside_key lKey; // local memory region key
    uint64_t size;             // data size
} ubs_hcom_mr_info;

typedef struct {
    void *lAddress;
    void *rAddress;
    ubs_hcom_oneside_key lKey;
    ubs_hcom_oneside_key rKey;
    uint32_t size;
} ubs_hcom_oneside_request;

typedef struct {
    ubs_hcom_oneside_request iov[4];
    uint16_t iovCount;
} ubs_hcom_onesidesgl_request;

typedef struct {
    uint16_t intervalTimeMs;
    uint64_t thresholdByte;
    ubs_hcom_channel_flowctl_level flowCtrlLevel;
} ubs_hcom_flowctl_opts;

typedef struct {
    uint32_t splitThreshold;
    uint32_t rndvThreshold;
} ubs_hcom_twoside_threshold;

/*
 * @brief Create a service
 *
 * @param t                [in] underlying communication protocol of the service
 * @param name             [in] name of the service
 * @param options          [in] options used to create the service
 * @param service          [out] created service, set to 0 if failed
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_service_create(ubs_hcom_service_type t, const char *name, ubs_hcom_service_options options,
                            ubs_hcom_service *service);

/*
 * @brief Bind the listener url, which specifies the type and the url to listen on, a client may skip Bind
 *
 * @param service          [in] service handler
 * @param listenerUrl      [in] url to listen on
 *                              for tcp: tcp://127.0.0.1:9981
 *                              for uds: uds://file:perm (a real file is used if :perm is given, e.g. 0600, otherwise an
 *                              abstract file is used)
 *                              for ubc: ubc://eid:jettyId
 * @param h                [in] handler invoked when a connection request is received
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_service_bind(ubs_hcom_service service, const char *listenerUrl, ubs_hcom_service_channel_handler h);

/*
 * @brief Start the service, listening is started as well if Bind has been called, otherwise no listening
 *
 * @param service          [in] service handler
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_service_start(ubs_hcom_service service);

/*
 * @brief Destroy the service
 *
 * @param service          [in] service handler
 * @param name             [in] name of the instance to be destroyed
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_service_destroy(ubs_hcom_service service, const char *name);

/*
 * @brief Establish a connection
 *
 * Note: 0 only means the transport link is established. If the server side channel handler (h)
 * returns a value other than SER_OK, the server destroys the channel, and later operations
 * report SER_NOT_ESTABLISHED(505)
 *
 * @param service          [in] service handler
 * @param serverUrl        [in] url of the server to connect
 *                              for tcp: tcp://127.0.0.1:9981
 *                              for uds: uds://file (file name / abstract namespace)
 *                              for ubc: ubc://eid:jettyId
 * @param channel          [out] created channel if the connection is established, set to 0 if failed
 * @param options          [in] options used to establish the connection
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_service_connect(ubs_hcom_service service, const char *serverUrl, ubs_hcom_channel *channel,
                             ubs_hcom_service_connect_options options);

/*
 * @brief Disconnect the channel
 *
 * @param service          [in] service handler
 * @param channel          [in] channel to be disconnected
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_service_disconnect(ubs_hcom_service service, ubs_hcom_channel channel);

/*
 * @brief Register a memory region, the memory is allocated internally
 *
 * @param service          [in] service handler
 * @param size             [in] size of the memory region
 * @param mr               [out] registered memory region, set to 0 if failed
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_service_register_memory_region(ubs_hcom_service service, uint64_t size, ubs_hcom_memory_region *mr);

/*
 * @brief Import an urma seg, used by one side read/write
 *
 * @param service          [in] service handler
 * @param address          [in] start address of the memory segment
 * @param size             [in] size of the memory segment
 * @param key              [in,out] in: key and eid of the segment to import, out: token of the imported segment
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_reg_seg(ubs_hcom_service service, uintptr_t address, uint64_t size, ubs_hcom_oneside_key *key);

/*
 * @brief Get the local address, key and size of a memory region
 *
 * @param mr               [in] memory region handler
 * @param info             [out] filled mr info
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_service_get_memory_region_info(ubs_hcom_memory_region mr, ubs_hcom_mr_info *info);

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
int ubs_hcom_service_register_assign_memory_region(ubs_hcom_service service, uintptr_t address, uint64_t size,
                                                   ubs_hcom_memory_region *mr);

/*
 * @brief Unregister a memory region
 *
 * @param service          [in] service handler
 * @param mr               [in] memory region to be unregistered
 */
int ubs_hcom_service_destroy_memory_region(ubs_hcom_service service, ubs_hcom_memory_region mr);

/*
 * @brief Register the channel broken handler
 *
 * @param service          [in] service handler
 * @param h                [in] channel broken handler
 * @param policy           [in] channel broken policy
 * @param usrCtx           [in] user context, passed through to the handler
 */
void ubs_hcom_service_register_broken_handler(ubs_hcom_service service, ubs_hcom_service_channel_handler h,
                                              ubs_hcom_service_channel_policy policy, uint64_t usrCtx);

/*
 * @brief Register the handler invoked when pollCq or epoll_wait times out
 *
 * @param service          [in] service handler
 * @param h                [in] handler to be registered
 * @param usrCtx           [in] user context, passed through to the handler
 */
void ubs_hcom_service_register_idle_handler(ubs_hcom_service service, ubs_hcom_service_idle_handler h, uint64_t usrCtx);

/*
 * @brief Register the request handler, t specifies the handler type, which corresponds to the
 * recv/send/one side handlers of the C++ interface
 *
 * @param service          [in] service handler
 * @param t                [in] handler type, C_SERVICE_REQUEST_RECEIVED / C_SERVICE_REQUEST_POSTED /
 *                              C_SERVICE_READWRITE_DONE
 * @param h                [in] handler to be registered
 * @param usrCtx           [in] user context, passed through to the handler
 */
void ubs_hcom_service_register_handler(ubs_hcom_service service, ubs_hcom_service_handler_type t,
                                       ubs_hcom_service_request_handler h, uint64_t usrCtx);

/*
 * @brief Add a workerGroup
 *
 * @param service          [in] service handler
 * @param priority         [in] same as the thread nice value, range [-20, 19], -20 is the highest and 19 the lowest
 * @param workerGroupId    [in] id of the workerGroup
 * @param threadCount      [in] thread count of the workerGroup
 * @param cpuIdsRange      [in] cpu id range bound to the workerGroup, for example 1-2 means cpu 1 to cpu 2
 * @param multirailIdx     [in] rail index the workerGroup is bound to, only used in multirail scenario
 */
void ubs_hcom_service_add_workergroup(ubs_hcom_service service, int8_t priority, uint16_t workerGroupId,
                                      uint32_t threadCount, const char *cpuIdsRange, uint16_t multirailIdx);

/*
 * @brief Add a listener, multiple urls are supported
 *
 * @param service          [in] service handler
 * @param url              [in] url to listen on, for tcp: tcp://127.0.0.1:9981
 *                              for uds: uds://file (file name / abstract namespace)
 * @param workerCount      [in] workerCount threads are selected from the corresponding workerGroup according
 *                              to lbPolicy, and bound to the asyncEp after a connection request is listened
 */
void ubs_hcom_service_add_listener(ubs_hcom_service service, const char *url, uint16_t workerCount);

/*
 * @brief Set the load balance policy of connection establishment. A worker thread must be selected
 * to complete an active/passive connection, and lbPolicy is the policy to select the worker thread
 *
 * @param service          [in] service handler
 * @param lbPolicy         [in] round robin / hash by ip and port
 */
void ubs_hcom_service_set_lbpolicy(ubs_hcom_service service, ubs_hcom_service_lb_policy lbPolicy);

/*
 * @brief TLS related options, TLS is disabled by default if not configured
 *
 * @param service          [in] service handler
 * @param enableTls        [in] whether to enable TLS
 * @param version          [in] TLS version
 * @param cipherSuite      [in] cipher suite
 * @param certCb           [in] callback to get the certificate file, required unless PSK mode is used
 * @param priKeyCb         [in] callback to get the private key file and the keyPass, required unless PSK mode is used
 * @param caCb             [in] callback to get the CA and verify, required unless PSK mode is used
 * @param pskUseCb         [in] PSK callback for client, could be nullptr
 * @param pskFindCb        [in] PSK callback for server, could be nullptr
 *
 * Note: when enableTls is true, at least one of the cert callbacks group (certCb/priKeyCb/caCb) and the
 * PSK callbacks group (pskUseCb/pskFindCb) must be provided, otherwise the call is ignored
 */
void ubs_hcom_service_set_tls_opt(ubs_hcom_service service, bool enableTls, ubs_hcom_service_tls_version version,
                                  ubs_hcom_service_cipher_suite cipherSuite, ubs_hcom_tls_get_cert_cb certCb,
                                  ubs_hcom_tls_get_pk_cb priKeyCb, ubs_hcom_tls_get_ca_cb caCb,
                                  ubs_hcom_psk_use_session_cb pskUseCb, ubs_hcom_psk_find_session_cb pskFindCb);

/*
 * @brief Set the options related to connection secure authentication
 *
 * @param service          [in] service handler
 * @param secType          [in] secure type (disabled / one way / two way)
 * @param provider         [in] callback to generate the auth info when a connection is established
 * @param validator        [in] callback to validate the peer auth info when a connection is established
 * @param magic            [in] magic value
 * @param version          [in] version
 */
void ubs_hcom_service_set_secure_opt(ubs_hcom_service service, ubs_hcom_service_secure_type secType,
                                     ubs_hcom_secinfo_provider provider, ubs_hcom_secinfo_validator validator,
                                     uint16_t magic, uint8_t version);

/*
 * @brief Set the TCP_USER_TIMEOUT socket option, the tcp timeout, [0, 1024], 0 means never timeout
 *
 * @param service          [in] service handler
 * @param timeOutSec       [in] timeout in seconds
 */
void ubs_hcom_service_set_tcp_usr_timeout(ubs_hcom_service service, uint16_t timeOutSec);

/*
 * @brief Set whether TCP send copies the data (hcom internal memory)
 *
 * @param service          [in] service handler
 * @param tcpSendZCopy     [in] whether to copy the data
 */
void ubs_hcom_service_set_tcp_send_zcopy(ubs_hcom_service service, bool tcpSendZCopy);

/*
 * @brief Set the device ipMask, used by rdma/ub, the GID and UBEId of the network segment are
 * obtained by the ipMask
 *
 * @param service          [in] service handler
 * @param ipMask           [in] ipMask used to filter
 */
void ubs_hcom_service_set_ipmask(ubs_hcom_service service, const char *ipMask);

/*
 * @brief Set the device ipGroup, the corresponding device is used directly if ipGroup is specified explicitly
 *
 * @param service          [in] service handler
 * @param ipGroup          [in] ipGroup
 */
void ubs_hcom_service_set_ipgroup(ubs_hcom_service service, const char *ipGroup);

/*
 * @brief Set the depth of the cq queue
 *
 * @param service          [in] service handler
 * @param depth            [in] cq queue depth
 */
void ubs_hcom_service_set_cq_depth(ubs_hcom_service service, uint16_t depth);

/*
 * @brief Set the size of the SQ queue, 256 by default
 *
 * @param service          [in] service handler
 * @param sqSize           [in] queue size
 */
void ubs_hcom_service_set_sq_size(ubs_hcom_service service, uint32_t sqSize);

/*
 * @brief Set the size of the RQ queue, 256 by default
 *
 * @param service          [in] service handler
 * @param rqSize           [in] queue size
 */
void ubs_hcom_service_set_rq_size(ubs_hcom_service service, uint32_t rqSize);

/*
 * @brief Set the number of pre-posted wr, 64 by default
 *
 * @param service          [in] service handler
 * @param prePostSize      [in] number of pre-posted wr
 */
void ubs_hcom_service_set_prepost_size(ubs_hcom_service service, uint32_t prePostSize);

/*
 * @brief Set the polling batch size, 4 by default
 *
 * @param service          [in] service handler
 * @param pollSize         [in] batch size
 */
void ubs_hcom_service_set_polling_batchsize(ubs_hcom_service service, uint16_t pollSize);

/*
 * @brief Set the polling timeout in us, 500 by default
 *
 * @param service          [in] service handler
 * @param pollTimeout      [in] timeout
 */
void ubs_hcom_service_set_polling_timeoutus(ubs_hcom_service service, uint16_t pollTimeout);

/*
 * @brief Set the thread number of periodic task processing, which is mainly used for internal
 * async timeout check, 1 by default
 *
 * @param service          [in] service handler
 * @param threadNum        [in] thread number
 */
void ubs_hcom_service_set_timeout_threadnum(ubs_hcom_service service, uint32_t threadNum);

/*
 * @brief Set the max connection count, 250 by default
 *
 * @param service          [in] service handler
 * @param maxConnCount     [in] max connection count
 */
void ubs_hcom_service_set_max_connection_cnt(ubs_hcom_service service, uint32_t maxConnCount);

/*
 * @brief Set the heartbeat options
 *
 * @param service          [in] service handler
 * @param idleSec          [in] heartbeat probing starts after being idle for idleSec seconds
 * @param probeTimes       [in] probe times
 * @param intervalSec      [in] probe interval in seconds
 */
void ubs_hcom_service_set_heartbeat_opt(ubs_hcom_service service, uint16_t idleSec, uint16_t probeTimes,
                                        uint16_t intervalSec);

/*
 * @brief Set the multirail options
 *
 * @param service          [in] service handler
 * @param enable           [in] whether to enable multirail
 * @param threshold        [in] multirail threshold
 */
void ubs_hcom_service_set_multirail_opt(ubs_hcom_service service, bool enable, uint32_t threshold);

/*
 * @brief Set the UB-C multipath mode
 *
 * @param service          [in] service handler
 * @param ubcMode          [in] UB-C multipath mode
 */
void ubs_hcom_service_set_ubcmode(ubs_hcom_service service, ubs_hcom_service_ubc_mode ubcMode);

/*
 * @brief Set the max count of data blocks to send
 *
 * @param service          [in] service handler
 * @param maxSendRecvDataCount [in] max count of data blocks to send
 */
void ubs_hcom_service_set_max_send_recv_data_cnt(ubs_hcom_service service, uint32_t maxSendRecvDataCount);

/*
 * @brief Set whether RegisterMemoryRegion puts the mr info into the pgTable, set it to true if RNDV is used
 *
 * @param service          [in] service handler
 * @param enableMrCache    [in] true means putting into the pgTable, false means not, false by default
 */
void ubs_hcom_service_set_enable_mrcache(ubs_hcom_service service, bool enableMrCache);

/*
 * @brief Set the SL priority
 *
 * @param service          [in] service handler
 * @param ubPriority       [in] SL priority, range [0, 15]
 */
void ubs_hcom_service_set_ub_priority(ubs_hcom_service service, uint32_t ubPriority);

/*
 * @brief Set the eps active/backup switch
 *
 * @param service          [in] service handler
 * @param enable           [in] true means enabling the active/backup switch, false means disabling
 */
void ubs_hcom_service_set_active_backup(ubs_hcom_service service, bool enable);

/*
 * @brief Set the ctx store capacity
 *
 * @param service          [in] service handler
 * @param ctxStoreCapacity [in] ctx store capacity, range [128, 16777216], 2097152 by default
 */
void ubs_hcom_service_set_ctx_store_capacity(ubs_hcom_service service, uint32_t ctxStoreCapacity);

/*
 * @brief Set the tcp epoll mode
 *
 * @param service          [in] service handler
 * @param isTcpEpollLT     [in] ET by default, true means LT
 */
void ubs_hcom_service_set_tcp_epoll_mode(ubs_hcom_service service, bool isTcpEpollLT);

/*
 * @brief Set the memory pool thread local cache
 *
 * @param service          [in] service handler
 * @param enable           [in] memory pool thread local cache switch, enabled by default
 */
void ubs_hcom_service_set_enable_mem_pool_thread_cache(ubs_hcom_service service, bool enable);

/*
 * @brief Increase the reference count of the channel by one
 *
 * @param channel          [in] channel handler
 */
void ubs_hcom_channel_refer(ubs_hcom_channel channel);

/*
 * @brief Decrease the reference count of the channel by one
 *
 * @param channel          [in] channel handler
 */
void ubs_hcom_channel_derefer(ubs_hcom_channel channel);

/*
 * @brief Send a two side message, no reply is needed
 *
 * @param channel          [in] channel handler
 * @param req              [in] request of the two side message
 * @param cb               [in] nullptr for synchronous send, non-nullptr for asynchronous send with the
 *                              handler invoked after sent
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_channel_send(ubs_hcom_channel channel, ubs_hcom_channel_request req, ubs_hcom_channel_callback *cb);

/*
 * @brief Send a two side message which needs a reply
 *
 * @param channel          [in] channel handler
 * @param req              [in] request of the two side message
 * @param rsp              [out] reply of the peer after the two side message is sent
 * @param cb               [in] nullptr for synchronous send, non-nullptr for asynchronous send with the
 *                              handler invoked after sent
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_channel_call(ubs_hcom_channel channel, ubs_hcom_channel_request req, ubs_hcom_channel_response *rsp,
                          ubs_hcom_channel_callback *cb);

/*
 * @brief Reply a two side message, used by the receiver together with Call
 *
 * @param channel          [in] channel handler
 * @param req              [in] reply data
 * @param ctx              [in] reply context
 * @param cb               [in] nullptr for synchronous send, non-nullptr for asynchronous send with the
 *                              handler invoked after sent
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_channel_reply(ubs_hcom_channel channel, ubs_hcom_channel_request req, ubs_hcom_channel_reply_context ctx,
                           ubs_hcom_channel_callback *cb);

/*
 * @brief Send a two side message which needs a reply, dedicated to HLC preemption
 *
 * Blocking mode and zero copy are always used, so an epoll LT mode is recommended
 *
 * @param channel          [in] channel handler
 * @param req              [in] request of the two side message
 * @param rsp              [out] reply of the peer after the two side message is sent
 * @param cb               [in] nullptr for synchronous send, non-nullptr for asynchronous send with the
 *                              handler invoked after sent
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_channel_call_with_hlc(ubs_hcom_channel channel, ubs_hcom_channel_request req,
                                   ubs_hcom_channel_response *rsp, ubs_hcom_channel_callback *cb);

/*
 * @brief Reply a two side message, used by the receiver together with Call, dedicated to HLC preemption
 *
 * Blocking mode and zero copy are always used, so an epoll LT mode is recommended
 *
 * @param channel          [in] channel handler
 * @param req              [in] reply data
 * @param ctx              [in] reply context
 * @param cb               [in] nullptr for synchronous send, non-nullptr for asynchronous send with the
 *                              handler invoked after sent
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_channel_reply_with_hlc(ubs_hcom_channel channel, ubs_hcom_channel_request req,
                                    ubs_hcom_channel_reply_context ctx, ubs_hcom_channel_callback *cb);

/*
 * @brief Send a one side write request
 *
 * @param channel          [in] channel handler
 * @param req              [in] one side write request
 * @param cb               [in] nullptr for synchronous send of one side requests, non-nullptr for asynchronous
 *                              send with the handler invoked after sent
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_channel_put(ubs_hcom_channel channel, ubs_hcom_oneside_request req, ubs_hcom_channel_callback *cb);

/*
 * @brief Send a one side read request
 *
 * @param channel          [in] channel handler
 * @param req              [in] one side read request
 * @param cb               [in] nullptr for synchronous send of one side requests, non-nullptr for asynchronous
 *                              send with the handler invoked after sent
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_channel_get(ubs_hcom_channel channel, ubs_hcom_oneside_request req, ubs_hcom_channel_callback *cb);

/*
 * @brief Send a one side write SGL request
 *
 * @param channel          [in] channel handler
 * @param req              [in] one side write SGL request
 * @param cb               [in] nullptr for synchronous send of one side requests, non-nullptr for asynchronous
 *                              send with the handler invoked after sent
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_channel_putv(ubs_hcom_channel channel, ubs_hcom_onesidesgl_request req, ubs_hcom_channel_callback *cb);

/*
 * @brief Send a one side read SGL request
 *
 * @param channel          [in] channel handler
 * @param req              [in] one side read SGL request
 * @param cb               [in] nullptr for synchronous send of one side requests, non-nullptr for asynchronous
 *                              send with the handler invoked after sent
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_channel_getv(ubs_hcom_channel channel, ubs_hcom_onesidesgl_request req, ubs_hcom_channel_callback *cb);

/*
 * @brief Only used to receive RNDV requests, and a reply is mandatory after an RNDV request is received
 *
 * @param channel          [in] channel handler
 * @param ctx              [in] received service context
 * @param address          [in] address of the received data
 * @param size             [in] length of the received data
 * @param cb               [in] nullptr means that the synchronous receive needs to switch threads, non-nullptr
 *                              means receiving asynchronously with the handler invoked after received
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_channel_recv(ubs_hcom_channel channel, ubs_hcom_service_context ctx, uintptr_t address, uint32_t size,
                          ubs_hcom_channel_callback *cb);

/*
 * @brief Send file descriptors
 *
 * @param channel          [in] channel handler
 * @param fds              [in] array of file descriptors
 * @param len              [in] count of file descriptors
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_channel_send_fds(ubs_hcom_channel channel, int fds[], uint32_t len);

/*
 * @brief Receive file descriptors
 *
 * @param channel          [in] channel handler
 * @param fds              [out] array of file descriptors
 * @param len              [in] capacity of the array
 * @param timeoutSec       [in] receive timeout in seconds
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_channel_recv_fds(ubs_hcom_channel channel, int fds[], uint32_t len, int32_t timeoutSec);

/*
 * @brief Set the flow control configuration
 *
 * @param channel          [in] channel handler
 * @param opt              [in] flow control options
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_channel_set_flowctl_cfg(ubs_hcom_channel channel, ubs_hcom_flowctl_opts opt);

/*
 * @brief Set the timeout
 *
 * @param channel          [in] channel handler
 * @param oneSideTimeout   [in] timeout of one side requests
 * @param twoSideTimeout   [in] timeout of two side requests
 */
void ubs_hcom_channel_set_timeout(ubs_hcom_channel channel, int16_t oneSideTimeout, int16_t twoSideTimeout);

/*
 * @brief Set the two side operation threshold
 *
 * @param channel          [in] channel handler
 * @param threshold        [in] two side operation threshold
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_channel_set_twoside_threshold(ubs_hcom_channel channel, ubs_hcom_twoside_threshold threshold);

/*
 * @brief Get the id of the channel
 *
 * @param channel          [in] channel handler
 *
 * @return id of the channel, 0 if the channel is invalid
 */
uint64_t ubs_hcom_channel_get_id(ubs_hcom_channel channel);

/*
 * @brief Get the process/user/group id of the peer of an UDS channel
 *
 * Note 1: only valid for UDS channels. The peer pid can be used to tell whether the peer process
 * has restarted
 * Note 2: the error code returned on failure comes from the underlying transport layer, use
 * ubs_hcom_err_str to decode it
 *
 * @param channel          [in] channel handler
 * @param idInfo           [out] peer pid/uid/gid
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_channel_get_remote_uds_id_info(ubs_hcom_channel channel, ubs_hcom_uds_id_info *idInfo);

/*
 * @brief Set the trace id
 *
 * @param channel          [in] channel handler
 * @param traceId          [in] trace id, must not be nullptr
 */
void ubs_hcom_channel_set_trace_id(ubs_hcom_channel channel, const char *traceId);

/*
 * @brief Get the payload carried by the peer when the connection is established
 *
 * @param channel          [in] channel handler
 * @param payload          [in,out] payload buffer, only the needed length is filled back if it is nullptr
 * @param len              [in,out] in: capacity of the buffer, including the ending '\0'
 *                                  out: actual payload length excluding '\0' if successful, or the
 *                                  needed capacity if the buffer is too small
 *
 * @return 0 if successful, SER_RSP_SIZE_TOO_SMALL(508) if the buffer is too small, error code if failed
 */
int ubs_hcom_channel_get_peer_connect_payload(ubs_hcom_channel channel, char *payload, uint32_t *len);

/*
 * @brief Bind a user context to the channel, which is not used inside hcom
 *
 * @param channel          [in] channel handler
 * @param ctx              [in] user context
 */
void ubs_hcom_channel_set_up_ctx(ubs_hcom_channel channel, uint64_t ctx);

/*
 * @brief Get the user context bound by ubs_hcom_channel_set_up_ctx
 *
 * @param channel          [in] channel handler
 *
 * @return user context, 0 if the channel is invalid or no user context is set
 */
uint64_t ubs_hcom_channel_get_up_ctx(ubs_hcom_channel channel);

/*
 * @brief Get the reply context, used to send a reply message in another thread
 *
 * Note: only SERVICE_RECEIVED/SERVICE_RECEIVED_RAW support this invocation
 *
 * @param context          [in] service context
 * @param rspCtx           [out] reply context
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_context_get_rspctx(ubs_hcom_service_context context, ubs_hcom_channel_reply_context *rspCtx);

/*
 * @brief Get the channel which the context belongs to
 *
 * @param context          [in] service context
 * @param channel          [out] channel handler
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_context_get_channel(ubs_hcom_service_context context, ubs_hcom_channel *channel);

/*
 * @brief Get the operation type
 *
 * @param context          [in] service context
 * @param type             [out] operation type, SERVICE_INVALID_OP_TYPE if failed
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_context_get_type(ubs_hcom_service_context context, ubs_hcom_service_context_type *type);

/*
 * @brief Get the result of the operation
 *
 * Note: unlike ubs_hcom_context_get_error_code, this interface returns the int result of the
 * operation itself
 *
 * @param context          [in] service context
 * @param result           [out] result of the operation
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_context_get_result(ubs_hcom_service_context context, int *result);

/*
 * @brief Get the error code
 *
 * Unlike ubs_hcom_context_get_result, this interface returns an error code (such as
 * SER_NOT_ESTABLISHED(505)/SER_TIMEOUT(509)), which helps the caller distinguish the
 * failure reason (reconnect on timeout, fix the code on invalid param)
 *
 * @param context          [in] context handler
 * @param errorCode        [out] error code
 *
 * @return 0 if successful, error code if failed
 */
int ubs_hcom_context_get_error_code(ubs_hcom_service_context context, int32_t *errorCode);

/*
 * @brief Get the opcode input by the user
 *
 * Note: only SERVICE_SENT/SERVICE_RECEIVED support this invocation
 *
 * @param context          [in] service context
 *
 * @return 0~999 if successful, other values if failed
 */
uint16_t ubs_hcom_context_get_opcode(ubs_hcom_service_context context);

/*
 * @brief Get the received message data, which is only valid within the callback lifetime
 *
 * Note 1: only SERVICE_RECEIVED/SERVICE_RECEIVED_RAW support this invocation
 * Note 2: copy the data or invoke clone by yourself if the message data is used in another thread
 *
 * @param context          [in] service context
 *
 * @return valid address if successful, nullptr if failed
 */
void *ubs_hcom_context_get_data(ubs_hcom_service_context context);

/*
 * @brief Get the length of the received message data
 *
 * Note: only SERVICE_RECEIVED/SERVICE_RECEIVED_RAW support this invocation
 *
 * @param context          [in] service context
 *
 * @return valid length if successful, 0 if failed
 */
uint32_t ubs_hcom_context_get_datalen(ubs_hcom_service_context context);

/*
 * @brief Check whether the context is timeout
 *
 * @param context          [in] service context
 *
 * @return true if timeout, false if not timeout or the context is invalid
 */
bool ubs_hcom_context_is_timeout(ubs_hcom_service_context context);

/*
 * @brief Invalidate the context, the channel held by the context will be released
 *
 * Note: it is called automatically when the context is destroyed, so it is only needed when the
 * caller wants to release the context resources in advance
 *
 * @param context          [in] service context
 */
void ubs_hcom_context_invalidate(ubs_hcom_service_context context);

/*
 * @brief Clone a service context (equivalent to clone_ex with copyData=true, the received data is copied)
 *
 * @param context          [in] source context
 *
 * @return new context handler if successful, 0 if failed
 */
ubs_hcom_service_context ubs_hcom_context_clone(ubs_hcom_service_context context);

/*
 * @brief Clone a service context
 *
 * @param context          [in] source context
 * @param copyData         [in] true means malloc and copy the received data, false means only the meta data is cloned
 *
 * @return new context handler if successful, 0 if failed
 */
ubs_hcom_service_context ubs_hcom_context_clone_ex(ubs_hcom_service_context context, bool copyData);

/*
 * @brief Free a service context
 *
 * @param context          [in] context handler to be freed
 */
void ubs_hcom_context_free(ubs_hcom_service_context context);

/*
 * @brief Set external logger function
 *
 * @param h                [in] the log function ptr
 */
void ubs_hcom_set_log_handler(ubs_hcom_log_handler h);

#ifdef __cplusplus
}
#endif

#endif // HCOM_HCOM_SERVICE_C_V2_H
