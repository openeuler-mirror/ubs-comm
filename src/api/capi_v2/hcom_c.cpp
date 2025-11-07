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
#include "hcom_c.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>
#include "securec.h"

#include "hcom_def.h"
#include "hcom_err.h"
#include "hcom_log.h"
#include "hcom_def_inner_c.h"
#include "net_load_balance.h"

using namespace ock::hcom;

#define VALIDATE_DRIVER(driver)                                                     \
    do {                                                                            \
        if (NN_UNLIKELY((driver) == 0)) {                                           \
            NN_LOG_ERROR("Invalid param, driver must be correct driver address");   \
            return NN_INVALID_PARAM;                                                \
        }                                                                           \
    } while (0)

#define VALIDATE_DRIVER_NO_RET(driver)                                            \
    do {                                                                          \
        if (NN_UNLIKELY((driver) == 0)) {                                         \
            NN_LOG_ERROR("Invalid param, driver must be correct driver address"); \
            return;                                                               \
        }                                                                         \
    } while (0)

#define VALIDATE_MR(mr)                                                   \
    do {                                                                  \
        if (NN_UNLIKELY((mr) == 0)) {                                     \
            NN_LOG_ERROR("Invalid param, mr must be correct mr address"); \
            return NN_INVALID_PARAM;                                      \
        }                                                                 \
    } while (0)

#define VALIDATE_MR_NO_RET(mr)                                            \
    do {                                                                  \
        if (NN_UNLIKELY((mr) == 0)) {                                     \
            NN_LOG_ERROR("Invalid param, mr must be correct mr address"); \
            return;                                                       \
        }                                                                 \
    } while (0)

#define VALIDATE_MR_POINT(mr)                                                   \
    do {                                                                        \
        if (NN_UNLIKELY((mr) == 0)) {                                           \
            NN_LOG_ERROR("Invalid param, mr point must be correct mr address"); \
            return NN_INVALID_PARAM;                                            \
        }                                                                       \
    } while (0)

#define VALIDATE_EP(ep)                                                      \
    do {                                                                     \
        if (NN_UNLIKELY((ep) == 0)) {                                        \
            NN_LOG_ERROR("Invalid param, endpoint must be correct address"); \
            return NN_INVALID_PARAM;                                         \
        }                                                                    \
    } while (0)

#define VALIDATE_EP_NO_RET(ep)                                               \
    do {                                                                     \
        if (NN_UNLIKELY((ep) == 0)) {                                        \
            NN_LOG_ERROR("Invalid param, endpoint must be correct address"); \
            return;                                                          \
        }                                                                    \
    } while (0)

#define VALIDATE_REQ(req)                               \
    do {                                                \
        if (NN_UNLIKELY((req) == nullptr)) {            \
            NN_LOG_ERROR("Invalid param, req is null"); \
            return NN_INVALID_PARAM;                    \
        }                                               \
    } while (0)

#define VALIDATE_SEQ(seqNo)                             \
    do {                                                \
        if (NN_UNLIKELY((seqNo) == 0)) {                \
            NN_LOG_ERROR("Invalid param, seqNo is 0");  \
            return NN_INVALID_PARAM;                    \
        }                                               \
    } while (0)

#define VALIDATE_NAME_NO_RET(name)                                          \
    do {                                                                    \
        if (NN_UNLIKELY((name) == nullptr)) {                               \
            NN_LOG_ERROR("Invalid param, name must be correct address");    \
            return;                                                         \
        }                                                                   \
    } while (0)

#define VALIDATE_ALLOCATOR(allocator)                   \
    do {                                                \
        if (NN_UNLIKELY((allocator) == 0)) {            \
            NN_LOG_ERROR("Invalid allocator ptr");      \
            return NN_INVALID_PARAM;                    \
        }                                               \
    } while (0)

#define VALIDATE_OFFSET(offset)                         \
    do {                                                \
        if (NN_UNLIKELY((offset) == 0)) {               \
            NN_LOG_ERROR("Invalid offset ptr");         \
            return NN_INVALID_PARAM;                    \
        }                                               \
    } while (0)

#define VALIDATE_SIZE(size)                             \
    do {                                                \
        if (NN_UNLIKELY((size) == 0)) {                 \
            NN_LOG_ERROR("Invalid size ptr");           \
            return NN_INVALID_PARAM;                    \
        }                                               \
    } while (0)

#define VALIDATE_NOT_NULL(ptr, errorMsg)                \
    do {                                                \
        if (NN_UNLIKELY((ptr) == nullptr)) {            \
            NN_LOG_ERROR(errorMsg);                     \
            return NN_INVALID_PARAM;                    \
        }                                               \
    } while (0)                                         \

#define VALIDATE_DRIVER_GET_OOB_IP_AND_PORT_PARAM(driver, ipArray, portArray, length)               \
    do {                                                                                            \
        if (NN_UNLIKELY((driver) == 0)) {                                                           \
            NN_LOG_ERROR("Invalid param, driver must be correct driver address");                   \
            return false;                                                                           \
        }                                                                                           \
        if (NN_UNLIKELY((ipArray) == nullptr || (portArray) == nullptr || (length) == nullptr)) {   \
            NN_LOG_ERROR("Invalid param, ipArray/portArray/length cann't be nullptr");              \
            return false;                                                                           \
        }                                                                                           \
    } while (0)                                                                                     \

static int ChangeAllocatorType(ubs_hcom_memory_allocator_options *options, UBSHcomNetMemoryAllocatorOptions &out)
{
    if (NN_UNLIKELY(options->address == NN_NO0)) {
        NN_LOG_ERROR("Invalid allocator address ");
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(options->size == NN_NO0)) {
        NN_LOG_ERROR("Invalid allocator memory size " << options->size);
        return NN_INVALID_PARAM;
    }

    out.size = options->size;
    out.address = options->address;
    out.minBlockSize = options->minBlockSize != 0 ? options->minBlockSize : NN_NO4096;
    out.bucketCount = options->bucketCount != 0 ? options->bucketCount : NN_NO8192;
    out.alignedAddress = options->alignedAddress != 0;
    out.cacheTierCount = options->cacheTierCount != 0 ? options->cacheTierCount : NN_NO8;
    out.cacheBlockCountPerTier = options->cacheBlockCountPerTier != 0 ? options->cacheBlockCountPerTier : NN_NO16;
    out.cacheTierPolicy = static_cast<UBSHcomNetMemoryAllocatorCacheTierPolicy>(options->cacheTierPolicy);
    return SER_OK;
}

inline UBSHcomNetDriverProtocol ChangeDriverTypeToDriverProto(ubs_hcom_driver_type type)
{
    UBSHcomNetDriverProtocol protocol = UBSHcomNetDriverProtocol::UNKNOWN;
    switch (type) {
        case C_DRIVER_RDMA:
            protocol = UBSHcomNetDriverProtocol::RDMA;
            break;
        case C_DRIVER_TCP:
            protocol = UBSHcomNetDriverProtocol::TCP;
            break;
        case C_DRIVER_UDS:
            protocol = UBSHcomNetDriverProtocol::UDS;
            break;
        case C_DRIVER_SHM:
            protocol = UBSHcomNetDriverProtocol::SHM;
            break;
        case C_SERVICE_UBC:
            protocol = UBSHcomNetDriverProtocol::UBC;
            break;
        default:
            NN_LOG_ERROR("Invalid driver protocol type");
            protocol = UBSHcomNetDriverProtocol::UNKNOWN;
    }
    return protocol;
}

int ubs_hcom_mem_allocator_create(ubs_hcom_memory_allocator_type t, ubs_hcom_memory_allocator_options *options,
    ubs_hcom_memory_allocator *allocator)
{
    if (NN_UNLIKELY(options == nullptr || allocator == nullptr)) {
        NN_LOG_ERROR("Invalid options " << options << " or allocator " << allocator);
        return NN_INVALID_PARAM;
    }

    auto allocatorType = static_cast<UBSHcomNetMemoryAllocatorType>(t);
    UBSHcomNetMemoryAllocatorOptions allocatorOptions;
    UBSHcomNetMemoryAllocatorPtr innerAllocator;

    auto result = ChangeAllocatorType(options, allocatorOptions);
    if (NN_UNLIKELY(result != SER_OK)) {
        return result;
    }

    result = UBSHcomNetMemoryAllocator::Create(allocatorType, allocatorOptions, innerAllocator);
    if (NN_UNLIKELY(result != SER_OK)) {
        return result;
    }

    *allocator = reinterpret_cast<uintptr_t>(innerAllocator.Get());
    innerAllocator->IncreaseRef();
    return SER_OK;
}

int ubs_hcom_mem_allocator_destroy(ubs_hcom_memory_allocator allocator)
{
    VALIDATE_ALLOCATOR(allocator);

    auto innerAllocator = reinterpret_cast<UBSHcomNetMemoryAllocator *>(allocator);
    innerAllocator->DecreaseRef();
    return SER_OK;
}

int ubs_hcom_mem_allocator_set_mr_key(ubs_hcom_memory_allocator allocator, uint64_t mrKey)
{
    VALIDATE_ALLOCATOR(allocator);

    auto innerAllocator = reinterpret_cast<UBSHcomNetMemoryAllocator *>(allocator);
    innerAllocator->MrKey(mrKey);
    return SER_OK;
}

int ubs_hcom_mem_allocator_get_offset(ubs_hcom_memory_allocator allocator, uintptr_t address, uintptr_t *offset)
{
    VALIDATE_ALLOCATOR(allocator);
    VALIDATE_OFFSET(offset);

    auto innerAllocator = reinterpret_cast<UBSHcomNetMemoryAllocator *>(allocator);
    *offset = innerAllocator->MemOffset(address);
    return SER_OK;
}

int ubs_hcom_mem_allocator_get_free_size(ubs_hcom_memory_allocator allocator, uintptr_t *size)
{
    VALIDATE_ALLOCATOR(allocator);
    VALIDATE_SIZE(size);

    auto innerAllocator = reinterpret_cast<UBSHcomNetMemoryAllocator *>(allocator);
    *size = innerAllocator->FreeSize();
    return SER_OK;
}

int ubs_hcom_mem_allocator_allocate(ubs_hcom_memory_allocator allocator, uint64_t size, uintptr_t *address, uint64_t *key)
{
    VALIDATE_ALLOCATOR(allocator);
    VALIDATE_NOT_NULL(address, "Invalid out address");
    VALIDATE_NOT_NULL(key, "Invalid key ptr");
    auto innerAllocator = reinterpret_cast<UBSHcomNetMemoryAllocator *>(allocator);
    *key = innerAllocator->MrKey();
    return innerAllocator->Allocate(size, *address);
}

int ubs_hcom_mem_allocator_free(ubs_hcom_memory_allocator allocator, uintptr_t address)
{
    VALIDATE_ALLOCATOR(allocator);

    auto innerAllocator = reinterpret_cast<UBSHcomNetMemoryAllocator *>(allocator);
    return innerAllocator->Free(address);
}

static HdlMgr g_epHandlerManager;

void ubs_hcom_set_log_handler(ubs_hcom_log_handler h)
{
    NetLogger::Instance()->SetExternalLogFunction(h);
}

int ubs_hcom_check_local_support(ubs_hcom_driver_type t, ubs_hcom_device_info *info)
{
    if (NN_UNLIKELY(info == nullptr)) {
        NN_LOG_ERROR("Invalid param info");
        return 0;
    }

    UBSHcomNetDriverProtocol driverProto = ChangeDriverTypeToDriverProto(t);
    if (NN_UNLIKELY(driverProto == UBSHcomNetDriverProtocol::UNKNOWN)) {
        NN_LOG_ERROR("Unsupport driver type, type:" << t);
        return 0;
    }

    UBSHcomNetDriverDeviceInfo deviceInfo;
    /* return 1 if support, otherwise return 0 */
    if (UBSHcomNetDriver::LocalSupport(driverProto, deviceInfo)) {
        info->maxSge = deviceInfo.maxSge;
        return 1;
    }

    return 0;
}

// driver api
int ubs_hcom_driver_create(ubs_hcom_driver_type t, const char *name, uint8_t startOobSvr, ubs_hcom_driver *driver)
{
    if (NN_UNLIKELY((name == nullptr) || (driver == nullptr))) {
        NN_LOG_ERROR("Invalid param, name or driver is null");
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY((startOobSvr != 0) && (startOobSvr != 1))) {
        NN_LOG_ERROR("Invalid param, startOobSvr must be 0 or 1");
        return NN_INVALID_PARAM;
    }

    if (strlen(name) > NN_NO64) {
        NN_LOG_ERROR("Invalid param, name length is larger than " << NN_NO64);
        return NN_INVALID_PARAM;
    }

    UBSHcomNetDriverProtocol driverProtocol = ChangeDriverTypeToDriverProto(t);
    if (NN_UNLIKELY(driverProtocol == UBSHcomNetDriverProtocol::UNKNOWN)) {
        NN_LOG_ERROR("Unsupport driver type, type:" << t);
        return NN_INVALID_PARAM;
    }

    auto tmpDriver = UBSHcomNetDriver::Instance(driverProtocol, name, startOobSvr == 1);
    if (tmpDriver == nullptr) {
        return NN_NEW_OBJECT_FAILED;
    }

    *driver = reinterpret_cast<ubs_hcom_driver>(tmpDriver);

    return NN_OK;
}

void ubs_hcom_driver_set_ipport(ubs_hcom_driver driver, const char *ip, uint16_t port)
{
    VALIDATE_DRIVER_NO_RET(driver);
    if (ip == nullptr) {
        NN_LOG_ERROR("Invalid param, ip is empty");
        return;
    }

    reinterpret_cast<UBSHcomNetDriver *>(driver)->OobIpAndPort(ip, port);
}

static void ClearIpAndPortArray(char ***ipArray, uint16_t **portArray, int *length)
{
    if (length == nullptr || *length == 0) {
        return;
    }

    if ((portArray != nullptr) && (*portArray != nullptr)) {
        free(*portArray);
        *portArray = nullptr;
    }

    if ((ipArray == nullptr) || (*ipArray == nullptr)) {
        return;
    }

    for (int i = 0; i != *length; ++i) {
        if (**(ipArray + i) != nullptr) {
            free(**(ipArray + i));
            **(ipArray + i) = nullptr;
        }
    }
    free(*ipArray);
    *ipArray = nullptr;
    *length = 0;
    return;
}

bool ubs_hcom_driver_get_ipport(ubs_hcom_driver driver, char ***ipArray, uint16_t **portArray, int *length)
{
    VALIDATE_DRIVER_GET_OOB_IP_AND_PORT_PARAM(driver, ipArray, portArray, length);
    std::vector<std::pair<std::string, uint16_t>> res;
    if (!reinterpret_cast<UBSHcomNetDriver *>(driver)->GetOobIpAndPort(res)) {
        NN_LOG_ERROR("Invalid param, get oob ip port failed");
        return false;
    }
    if (res.size() == 0) {
        NN_LOG_ERROR("The Working oob ip and port cannot be found");
        return false;
    }
    // prepare length result
    *length = static_cast<int>(res.size());
    // prepare port result
    *portArray = static_cast<uint16_t *>(malloc(res.size() * sizeof(uint16_t)));
    if (*portArray == nullptr) {
        NN_LOG_ERROR("Failed to malloc portArray");
        ClearIpAndPortArray(ipArray, portArray, length);
        return false;
    }
    for (auto i = 0; i < static_cast<int>(res.size()); ++i) {
        **(portArray + i) = res[i].second;
    }
    // prepare ip result
    *ipArray = static_cast<char**>(malloc(res.size() * sizeof(char *)));
    if (*ipArray == nullptr) {
        NN_LOG_ERROR("malloc ipArray failed!");
        ClearIpAndPortArray(ipArray, portArray, length);
        return false;
    }
    bzero(*ipArray, res.size() * sizeof(char *));
    for (int i = 0; i < static_cast<int>(res.size()); ++i) {
        auto temp = static_cast<char*>(malloc(MAX_IP_LENGTH * sizeof(char)));
        if (temp == nullptr) {
            NN_LOG_ERROR("malloc ipArray[" << i << "] failed!");
            ClearIpAndPortArray(ipArray, portArray, length);
            return false;
        }
        bzero(temp, MAX_IP_LENGTH * sizeof(char));
        if (memcpy_s(temp, MAX_IP_LENGTH, res[i].first.c_str(), res[i].first.size()) != 0) {
            NN_LOG_ERROR("copy ipArray" << i << "] failed!");
            free(temp);
            ClearIpAndPortArray(ipArray, portArray, length);
            return false;
        }
        **(ipArray + i) = temp;
    }
    return true;
}

void ubs_hcom_driver_set_udsname(ubs_hcom_driver driver, const char *name)
{
    VALIDATE_DRIVER_NO_RET(driver);
    VALIDATE_NAME_NO_RET(name);
    reinterpret_cast<UBSHcomNetDriver *>(driver)->OobUdsName(name);
}

void ubs_hcom_driver_add_uds_opt(ubs_hcom_driver driver, ubs_hcom_driver_uds_listen_opts option)
{
    VALIDATE_DRIVER_NO_RET(driver);
    UBSHcomNetOobUDSListenerOptions innerOpt {};
    innerOpt.Name(option.name);
    innerOpt.perm = option.perm;
    innerOpt.targetWorkerCount = option.targetWorkerCount;
    reinterpret_cast<UBSHcomNetDriver *>(driver)->AddOobUdsOptions(innerOpt);
}

void ubs_hcom_driver_add_oob_opt(ubs_hcom_driver driver, ubs_hcom_driver_listen_opts options)
{
    VALIDATE_DRIVER_NO_RET(driver);

    UBSHcomNetOobListenerOptions innerOpt {};
    std::string ip = { options.ip, strlen(options.ip) <= sizeof(options.ip) ? strlen(options.ip) : sizeof(options.ip) };
    innerOpt.Set(ip, options.port, options.targetWorkerCount);
    reinterpret_cast<UBSHcomNetDriver *>(driver)->AddOobOptions(innerOpt);
}

int ubs_hcom_driver_initialize(ubs_hcom_driver driver, ubs_hcom_driver_opts options)
{
    VALIDATE_DRIVER(driver);

    UBSHcomNetDriverOptions driverOps {};
    driverOps.mode = NET_BUSY_POLLING;
    if (options.mode == C_EVENT_POLLING) {
        driverOps.mode = ock::hcom::NET_EVENT_POLLING;
    }

    driverOps.mrSendReceiveSegSize = options.mrSendReceiveSegSize != 0 ? options.mrSendReceiveSegSize : NN_NO1024;
    driverOps.mrSendReceiveSegCount = options.mrSendReceiveSegCount != 0 ? options.mrSendReceiveSegCount : NN_NO8192;
    driverOps.SetNetDeviceIpMask(options.netDeviceIpMask);
    driverOps.SetNetDeviceIpGroup(options.netDeviceIpGroup);
    driverOps.completionQueueDepth = options.completionQueueDepth != 0 ? options.completionQueueDepth : NN_NO2048;
    driverOps.maxPostSendCountPerQP = options.maxPostSendCountPerQP != 0 ? options.maxPostSendCountPerQP : NN_NO64;
    driverOps.prePostReceiveSizePerQP =
        options.prePostReceiveSizePerQP != 0 ? options.prePostReceiveSizePerQP : NN_NO64;
    driverOps.pollingBatchSize = options.pollingBatchSize != 0 ? options.pollingBatchSize : NN_NO4;
    driverOps.qpSendQueueSize = options.qpSendQueueSize != 0 ? options.qpSendQueueSize : NN_NO256;
    driverOps.qpReceiveQueueSize = options.qpReceiveQueueSize != 0 ? options.qpReceiveQueueSize : NN_NO256;
    driverOps.version = options.version != 0 ? options.version : NN_NO0;
    driverOps.dontStartWorkers = (options.dontStartWorkers == 1);
    driverOps.tcpSendBufSize = options.tcpSendBufSize != 0 ? NN_NextPower2(options.tcpSendBufSize) : 0;
    driverOps.tcpReceiveBufSize = options.tcpReceiveBufSize != 0 ? NN_NextPower2(options.tcpReceiveBufSize) : 0;

    if (NN_UNLIKELY(memcpy_s(driverOps.workerGroups, sizeof(driverOps.workerGroups), options.workerGroups,
        sizeof(options.workerGroups)) != NN_OK)) {
        NN_LOG_ERROR("Failed to copy worker groups");
        return NN_INVALID_PARAM;
    }
    if (NN_UNLIKELY(memcpy_s(driverOps.workerGroupsCpuSet, sizeof(driverOps.workerGroupsCpuSet),
        options.workerGroupsCpuSet, sizeof(options.workerGroupsCpuSet)) != NN_OK)) {
        NN_LOG_ERROR("Failed to copy worker cpu set");
        return NN_INVALID_PARAM;
    }
    driverOps.workerThreadPriority = options.workerThreadPriority;
    driverOps.tcpUserTimeout = options.tcpUserTimeout;
    driverOps.tcpEnableNoDelay = options.tcpEnableNoDelay;
    driverOps.tcpSendZCopy = options.tcpSendZCopy;
    driverOps.heartBeatIdleTime = options.heartBeatIdleTime != 0 ? options.heartBeatIdleTime : NN_NO60;
    driverOps.heartBeatProbeTimes = options.heartBeatProbeTimes != 0 ? options.heartBeatProbeTimes : NN_NO7;
    driverOps.heartBeatProbeInterval = options.heartBeatProbeInterval != 0 ? options.heartBeatProbeInterval : NN_NO2;
    driverOps.enableTls = options.enableTls;
    driverOps.tlsVersion =
        options.tlsVersion != 0 ? static_cast<UBSHcomTlsVersion>(options.tlsVersion) : (ock::hcom::TLS_1_3);
    driverOps.cipherSuite = static_cast<UBSHcomNetCipherSuite>(options.cipherSuite);
    driverOps.oobType = NET_OOB_TCP;
    driverOps.tcpSendBufSize = options.tcpSendBufSize;
    driverOps.tcpReceiveBufSize = options.tcpReceiveBufSize;
    driverOps.maxConnectionNum = options.maxConnectionNum != 0 ? options.maxConnectionNum : NN_NO250;
    if (options.oobType == C_NET_OOB_UDS) {
        driverOps.oobType = ock::hcom::NET_OOB_UDS;
    }
    if (NN_UNLIKELY(memcpy_s(driverOps.oobPortRange, sizeof(driverOps.oobPortRange), options.oobPortRange,
        sizeof(options.oobPortRange)) != 0)) {
        NN_LOG_ERROR("Failed to copy oob port range");
        return NN_INVALID_PARAM;
    }
    return reinterpret_cast<UBSHcomNetDriver *>(driver)->Initialize(driverOps);
}

int ubs_hcom_driver_start(ubs_hcom_driver driver)
{
    VALIDATE_DRIVER(driver);
    return reinterpret_cast<UBSHcomNetDriver *>(driver)->Start();
}

int ubs_hcom_driver_connect(ubs_hcom_driver driver, const char *payloadData, ubs_hcom_endpoint *ep, uint32_t flags)
{
    return ubs_hcom_driver_connect_with_grpno(driver, payloadData, ep, flags, 0, 0);
}

int ubs_hcom_driver_connect_with_grpno(ubs_hcom_driver driver, const char *payloadData, ubs_hcom_endpoint *ep,
    uint32_t flags, uint8_t serverGrpNo, uint8_t clientGrpNo)
{
    VALIDATE_DRIVER(driver);
    VALIDATE_EP(ep);

    std::string payload = payloadData != nullptr ? payloadData : "";

    UBSHcomNetEndpointPtr realEp;
    auto result = reinterpret_cast<UBSHcomNetDriver *>(driver)->Connect(payload, realEp, flags, serverGrpNo,
        clientGrpNo);
    if (NN_UNLIKELY(result != NN_OK)) {
        return result;
    }

    // increase ref, need to call ubs_hcom_ep_destroy() to decrease ref
    realEp->IncreaseRef();

    *ep = reinterpret_cast<ubs_hcom_endpoint>(realEp.Get());

    return NN_OK;
}

int ubs_hcom_driver_connect_to_ipport(ubs_hcom_driver driver, const char *serverIp, uint16_t serverPort,
    const char *payloadData, ubs_hcom_endpoint *ep, uint32_t flags)
{
    return ubs_hcom_driver_connect_to_ipport_with_groupno(driver, serverIp, serverPort, payloadData, ep, flags, 0, 0);
}

int ubs_hcom_driver_connect_to_ipport_with_groupno(ubs_hcom_driver driver, const char *serverIp, uint16_t serverPort,
    const char *payloadData, ubs_hcom_endpoint *ep, uint32_t flags, uint8_t serverGrpNo, uint8_t clientGrpNo)
{
    return ubs_hcom_driver_connect_to_ipport_with_ctx(driver, serverIp, serverPort, payloadData, ep, flags,
        serverGrpNo, clientGrpNo, 0);
}

int ubs_hcom_driver_connect_to_ipport_with_ctx(ubs_hcom_driver driver, const char *serverIp, uint16_t serverPort,
    const char *payloadData, ubs_hcom_endpoint *ep, uint32_t flags, uint8_t serverGrpNo, uint8_t clientGrpNo,
    uint64_t ctx)
{
    if (serverIp == nullptr || serverPort == 0) {
        NN_LOG_ERROR("Failed to connect as server ip is null or port " << serverPort << " is invalid");
        return NN_INVALID_PARAM;
    }

    VALIDATE_DRIVER(driver);
    VALIDATE_EP(ep);

    std::string payload = payloadData != nullptr ? payloadData : "";

    UBSHcomNetEndpointPtr realEp;
    auto result = reinterpret_cast<UBSHcomNetDriver *>(driver)->Connect(serverIp, serverPort, payload, realEp, flags,
        serverGrpNo, clientGrpNo, ctx);
    if (NN_UNLIKELY(result != NN_OK)) {
        return result;
    }

    // increase ref, need to call ubs_hcom_ep_destroy() to decrease ref
    realEp->IncreaseRef();

    *ep = reinterpret_cast<ubs_hcom_endpoint>(realEp.Get());
    return NN_OK;
}

void ubs_hcom_driver_stop(ubs_hcom_driver driver)
{
    VALIDATE_DRIVER_NO_RET(driver);
    reinterpret_cast<UBSHcomNetDriver *>(driver)->Stop();
}

void ubs_hcom_driver_uninitialize(ubs_hcom_driver driver)
{
    VALIDATE_DRIVER_NO_RET(driver);
    reinterpret_cast<UBSHcomNetDriver *>(driver)->UnInitialize();
}

int ubs_hcom_driver_destroy(ubs_hcom_driver driver)
{
    VALIDATE_DRIVER(driver);
    std::string name = reinterpret_cast<UBSHcomNetDriver *>(driver)->Name();
    return UBSHcomNetDriver::DestroyInstance(name);
}

uintptr_t ubs_hcom_driver_register_ep_handler(ubs_hcom_driver driver, ubs_hcom_ep_handler_type t,
    ubs_hcom_ep_handler h, uint64_t usrCtx)
{
    if (NN_UNLIKELY(driver == 0)) {
        NN_LOG_ERROR("Invalid param, driver must be correct driver address");
        return 0;
    }

    if (NN_UNLIKELY(h == nullptr)) {
        NN_LOG_ERROR("Invalid param, ubs_hcom_ep_handler is null");
        return 0;
    }

    auto tmpHandle = new (std::nothrow) EpHdlAdp(t, h, usrCtx);
    if (NN_UNLIKELY(tmpHandle == nullptr)) {
        NN_LOG_ERROR("Failed to new Endpoint handler adapter, probably out of memory");
        return 0;
    }

    if (t == C_EP_NEW) {
        reinterpret_cast<UBSHcomNetDriver *>(driver)->RegisterNewEPHandler(std::bind(&EpHdlAdp::NewEndPoint, tmpHandle,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    } else if (t == C_EP_BROKEN) {
        reinterpret_cast<UBSHcomNetDriver *>(driver)->RegisterEPBrokenHandler(
            std::bind(&EpHdlAdp::EndPointBroken, tmpHandle, std::placeholders::_1));
    } else {
        NN_LOG_ERROR("Unreachable");
        delete tmpHandle;
        return 0;
    }

    g_epHandlerManager.AddHdlAdp(reinterpret_cast<uintptr_t>(tmpHandle));

    return reinterpret_cast<uintptr_t>(tmpHandle);
}

uintptr_t ubs_hcom_driver_register_op_handler(ubs_hcom_driver driver, ubs_hcom_op_handler_type t,
    ubs_hcom_request_handler h, uint64_t usrCtx)
{
    if (NN_UNLIKELY(driver == 0)) {
        NN_LOG_ERROR("Invalid param, driver must be correct driver address");
        return 0;
    }

    if (NN_UNLIKELY(h == nullptr)) {
        NN_LOG_ERROR("Invalid param, ubs_hcom_ep_handler is null");
        return 0;
    }

    auto tmpHandle = new (std::nothrow) EpOpHdlAdp(h, usrCtx);
    if (NN_UNLIKELY(tmpHandle == nullptr)) {
        NN_LOG_ERROR("Failed to new Endpoint handler adapter, probably out of memory");
        return 0;
    }

    if (t == C_OP_REQUEST_RECEIVED) {
        reinterpret_cast<UBSHcomNetDriver *>(driver)->RegisterNewReqHandler(
            std::bind(&EpOpHdlAdp::Requested, tmpHandle, std::placeholders::_1));
    } else if (t == C_OP_REQUEST_POSTED) {
        reinterpret_cast<UBSHcomNetDriver *>(driver)->RegisterReqPostedHandler(
            std::bind(&EpOpHdlAdp::Requested, tmpHandle, std::placeholders::_1));
    } else if (t == C_OP_READWRITE_DONE) {
        reinterpret_cast<UBSHcomNetDriver *>(driver)->RegisterOneSideDoneHandler(
            std::bind(&EpOpHdlAdp::Requested, tmpHandle, std::placeholders::_1));
    } else {
        NN_LOG_ERROR("Unreachable");
        delete tmpHandle;
        return 0;
    }

    g_epHandlerManager.AddHdlAdp(reinterpret_cast<uintptr_t>(tmpHandle));

    return reinterpret_cast<uintptr_t>(tmpHandle);
}

uintptr_t ubs_hcom_driver_register_idle_handler(ubs_hcom_driver driver, ubs_hcom_idle_handler h, uint64_t usrCtx)
{
    if (NN_UNLIKELY(driver == 0)) {
        NN_LOG_ERROR("Invalid param, driver must be correct driver address");
        return 0;
    }

    if (NN_UNLIKELY(h == nullptr)) {
        NN_LOG_ERROR("Invalid param, ubs_hcom_ep_handler is null");
        return 0;
    }

    auto tmpHandle = new (std::nothrow) EpIdleHdlAdp(h, usrCtx);
    if (NN_UNLIKELY(tmpHandle == nullptr)) {
        NN_LOG_ERROR("Failed to new Endpoint handler adapter, probably out of memory");
        return 0;
    }

    reinterpret_cast<UBSHcomNetDriver *>(driver)->RegisterIdleHandler(
        std::bind(&EpIdleHdlAdp::Idle, tmpHandle, std::placeholders::_1));

    g_epHandlerManager.AddHdlAdp(reinterpret_cast<uintptr_t>(tmpHandle));

    return reinterpret_cast<uintptr_t>(tmpHandle);
}

uintptr_t ubs_hcom_driver_register_secinfo_provider(ubs_hcom_driver driver, ubs_hcom_secinfo_provider provider)
{
    if (NN_UNLIKELY(driver == 0)) {
        NN_LOG_ERROR("Invalid param, driver must be correct driver address");
        return 0;
    }

    if (NN_UNLIKELY(provider == nullptr)) {
        NN_LOG_ERROR("Invalid param, ubs_hcom_secinfo_provider is null");
        return 0;
    }

    auto tmpHandle = new (std::nothrow) OOBSecInfoProviderAdp(provider);
    if (NN_UNLIKELY(tmpHandle == nullptr)) {
        NN_LOG_ERROR("Register ubs_hcom_secinfo_provider failed, probably out of memory");
        return 0;
    }

    reinterpret_cast<UBSHcomNetDriver *>(driver)->RegisterEndpointSecInfoProvider(
        std::bind(&OOBSecInfoProviderAdp::CreateSecInfo, tmpHandle, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));

    return reinterpret_cast<uintptr_t>(tmpHandle);
}

uintptr_t ubs_hcom_driver_register_secinfo_validator(ubs_hcom_driver driver, ubs_hcom_secinfo_validator validator)
{
    if (NN_UNLIKELY(driver == 0)) {
        NN_LOG_ERROR("Invalid param, driver must be correct driver address");
        return 0;
    }

    if (NN_UNLIKELY(validator == nullptr)) {
        NN_LOG_ERROR("Invalid param, ubs_hcom_secinfo_validator is null");
        return 0;
    }

    auto tmpHandle = new (std::nothrow) OOBSecInfoValidatorAdp(validator);
    if (NN_UNLIKELY(tmpHandle == nullptr)) {
        NN_LOG_ERROR("Register ubs_hcom_secinfo_validator failed, probably out of memory");
        return 0;
    }

    reinterpret_cast<UBSHcomNetDriver *>(driver)->RegisterEndpointSecInfoValidator(
        std::bind(&OOBSecInfoValidatorAdp::SecInfoValidate, tmpHandle, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4));

    return reinterpret_cast<uintptr_t>(tmpHandle);
}

uintptr_t ubs_hcom_driver_register_tls_cb(ubs_hcom_driver driver, ubs_hcom_tls_get_cert_cb certCb,
    ubs_hcom_tls_get_pk_cb priKeyCb, ubs_hcom_tls_get_ca_cb caCb)
{
    if (NN_UNLIKELY(driver == 0)) {
        NN_LOG_ERROR("Invalid param, driver must be correct driver address");
        return 0;
    }

    if (NN_UNLIKELY(certCb == nullptr) || NN_UNLIKELY(priKeyCb == nullptr || NN_LIKELY(caCb == nullptr))) {
        NN_LOG_ERROR("Failed to reg driver tls cb by invalid param or handler");
        return 0;
    }

    auto tmpHandle = new (std::nothrow) EpTLSHdlAdp();
    if (NN_UNLIKELY(tmpHandle == nullptr)) {
        NN_LOG_ERROR("Failed to new driver tls handler adapter, probably out of memory");
        return 0;
    }

    tmpHandle->SetTLSCertCb(certCb);
    tmpHandle->SetTLSPrivateKeyCb(priKeyCb);
    tmpHandle->SetTLSCaCb(caCb);
    reinterpret_cast<UBSHcomNetDriver *>(driver)->RegisterTLSCertificationCallback(
        (std::bind(&EpTLSHdlAdp::UBSHcomTLSCertificationCallback, tmpHandle, std::placeholders::_1,
        std::placeholders::_2)));

    reinterpret_cast<UBSHcomNetDriver *>(driver)->RegisterTLSPrivateKeyCallback(
        (std::bind(&EpTLSHdlAdp::UBSHcomTLSPrivateKeyCallback, tmpHandle, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5)));

    reinterpret_cast<UBSHcomNetDriver *>(driver)->RegisterTLSCaCallback(
        (std::bind(&EpTLSHdlAdp::UBSHcomTLSCaCallback, tmpHandle, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5)));

    return reinterpret_cast<uintptr_t>(tmpHandle);
}

void ubs_hcom_driver_unregister_ep_handler(ubs_hcom_ep_handler_type t, uintptr_t handle)
{
    g_epHandlerManager.RemoveHdlAdp<EpHdlAdp>(handle);
}

void ubs_hcom_driver_unregister_op_handler(ubs_hcom_op_handler_type t, uintptr_t handle)
{
    g_epHandlerManager.RemoveHdlAdp<EpOpHdlAdp>(handle);
}

void ubs_hcom_driver_unregister_idle_handler(uintptr_t handle)
{
    g_epHandlerManager.RemoveHdlAdp<EpIdleHdlAdp>(handle);
}

int ubs_hcom_driver_create_memory_region(ubs_hcom_driver driver, uint64_t size, ubs_hcom_memory_region *mr)
{
    VALIDATE_DRIVER(driver);
    VALIDATE_MR_POINT(mr);

    auto tmpMr = new (std::nothrow) UBSHcomNetMemoryRegionPtr;
    if (tmpMr == nullptr) {
        NN_LOG_ERROR("Create memory region malloc memory failed");
        return NN_NEW_OBJECT_FAILED;
    }

    auto result = reinterpret_cast<UBSHcomNetDriver *>(driver)->CreateMemoryRegion(size, *tmpMr);
    if (result != NN_OK) {
        delete tmpMr;
        return result;
    }

    *mr = reinterpret_cast<ubs_hcom_memory_region>(tmpMr);
    return NN_OK;
}

int ubs_hcom_driver_create_assign_memory_region(ubs_hcom_driver driver, uintptr_t address, uint64_t size,
    ubs_hcom_memory_region *mr)
{
    VALIDATE_DRIVER(driver);
    VALIDATE_MR_POINT(mr);

    auto tmpMr = new (std::nothrow) UBSHcomNetMemoryRegionPtr;
    if (tmpMr == nullptr) {
        NN_LOG_ERROR("Create memory region malloc memory failed");
        return NN_NEW_OBJECT_FAILED;
    }
    auto result = reinterpret_cast<UBSHcomNetDriver *>(driver)->CreateMemoryRegion(address, size, *tmpMr);
    if (result != NN_OK) {
        delete tmpMr;
        return result;
    }

    *mr = reinterpret_cast<ubs_hcom_memory_region>(tmpMr);
    return NN_OK;
}

void ubs_hcom_driver_destroy_memory_region(ubs_hcom_driver driver, ubs_hcom_memory_region mr)
{
    VALIDATE_DRIVER_NO_RET(driver);
    VALIDATE_MR_NO_RET(mr);

    auto tmpMr = reinterpret_cast<UBSHcomNetMemoryRegionPtr *>(mr);
    reinterpret_cast<UBSHcomNetDriver *>(driver)->DestroyMemoryRegion(*tmpMr);
    delete tmpMr;
}

int ubs_hcom_driver_get_memory_region_info(ubs_hcom_memory_region mr, ubs_hcom_memory_region_info *info)
{
    VALIDATE_MR(mr);

    if (NN_UNLIKELY(info == nullptr)) {
        NN_LOG_ERROR("Param info is empty");
        return NN_PARAM_INVALID;
    }

    auto tmpMrPtr = reinterpret_cast<UBSHcomNetMemoryRegionPtr *>(mr);
    auto tmpMr = tmpMrPtr->ToChild<UBSHcomNetMemoryRegion>();
    if (NN_UNLIKELY(tmpMr == nullptr)) {
        NN_LOG_ERROR("ToChild failed");
        return NN_ERROR;
    }
    info->lAddress = tmpMr->GetAddress();
    info->lKey = tmpMr->GetLKey();
    info->size = tmpMr->Size();
    return NN_OK;
}

void ubs_hcom_ep_set_context(ubs_hcom_endpoint ep, uint64_t ctx)
{
    VALIDATE_EP_NO_RET(ep);
    reinterpret_cast<UBSHcomNetEndpoint *>(ep)->UpCtx(ctx);
}

uint64_t ubs_hcom_ep_get_context(ubs_hcom_endpoint ep)
{
    VALIDATE_EP(ep);
    return reinterpret_cast<UBSHcomNetEndpoint *>(ep)->UpCtx();
}

uint16_t ubs_hcom_ep_get_worker_idx(ubs_hcom_endpoint ep)
{
    if (NN_UNLIKELY(ep == 0)) {
        NN_LOG_ERROR("Invalid param, endpoint must be correct address");
        return NET_INVALID_WORKER_INDEX;
    }

    return reinterpret_cast<UBSHcomNetEndpoint *>(ep)->WorkerIndex().idxInGrp;
}

uint8_t ubs_hcom_ep_get_workergroup_idx(ubs_hcom_endpoint ep)
{
    if (NN_UNLIKELY(ep == 0)) {
        NN_LOG_ERROR("Invalid param, endpoint must be correct address");
        return NET_INVALID_WORKER_GROUP_INDEX;
    }

    return reinterpret_cast<UBSHcomNetEndpoint *>(ep)->WorkerIndex().grpIdx;
}

uint32_t ubs_hcom_ep_get_listen_port(ubs_hcom_endpoint ep)
{
    if (NN_UNLIKELY(ep == 0)) {
        NN_LOG_ERROR("Invalid param, endpoint must be correct address");
        return 0;
    }

    return reinterpret_cast<UBSHcomNetEndpoint *>(ep)->ListenPort();
}

uint8_t ubs_hcom_ep_version(ubs_hcom_endpoint ep)
{
    if (NN_UNLIKELY(ep == 0)) {
        NN_LOG_ERROR("Invalid param, endpoint must be correct address");
        return 0;
    }

    return reinterpret_cast<UBSHcomNetEndpoint *>(ep)->Version();
}

void ubs_hcom_ep_set_timeout(ubs_hcom_endpoint ep, int32_t timeout)
{
    VALIDATE_EP_NO_RET(ep);
    reinterpret_cast<UBSHcomNetEndpoint *>(ep)->DefaultTimeout(timeout);
}

/* Caller have to make sure iov and src is NOT null */
static inline int CopySglInfo(UBSHcomNetTransSgeIov *iov, uint16_t iovCnt, ubs_hcom_readwrite_request_sgl *src)
{
    if (NN_UNLIKELY(src == nullptr)) {
        NN_LOG_ERROR("Invalid param, src is NULL");
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(src->iov == nullptr)) {
        NN_LOG_ERROR("Invalid param src iov");
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(src->iovCount > iovCnt)) {
        NN_LOG_ERROR("Invalid iov count, src iovCount: " << src->iovCount << ", iovCnt: " << iovCnt);
        return NN_INVALID_PARAM;
    }

    for (uint16_t i = 0; i < src->iovCount; i++) {
        iov[i].rAddress = src->iov[i].rAddress;
        iov[i].rKey = src->iov[i].rKey;
        iov[i].lAddress = src->iov[i].lAddress;
        iov[i].lKey = src->iov[i].lKey;
        iov[i].size = src->iov[i].size;
    }

    return NN_OK;
}

int ubs_hcom_ep_post_send(ubs_hcom_endpoint ep, uint16_t opcode, ubs_hcom_send_request *req)
{
    VALIDATE_EP(ep);
    VALIDATE_REQ(req);

    UBSHcomNetTransRequest transReq(reinterpret_cast<void *>(req->data), req->size, req->upCtxSize);
    if (NN_UNLIKELY(memcpy_s(transReq.upCtxData, sizeof(transReq.upCtxData), req->upCtxData, sizeof(req->upCtxData)) !=
        NN_OK)) {
        NN_LOG_ERROR("Failed to copy up ctx data");
        return NN_INVALID_PARAM;
    }

    return reinterpret_cast<UBSHcomNetEndpoint *>(ep)->PostSend(opcode, transReq);
}

int ubs_hcom_ep_post_send_with_opinfo(ubs_hcom_endpoint ep, uint16_t opcode, ubs_hcom_send_request *req, 
    ubs_hcom_opinfo *opInfo)
{
    VALIDATE_EP(ep);
    VALIDATE_REQ(req);

    if (NN_UNLIKELY(opInfo == nullptr)) {
        NN_LOG_ERROR("Invalid param opInfo");
        return NN_INVALID_PARAM;
    }

    UBSHcomNetTransRequest transReq(reinterpret_cast<void *>(req->data), req->size, req->upCtxSize);
    if (NN_UNLIKELY(memcpy_s(transReq.upCtxData, sizeof(transReq.upCtxData), req->upCtxData, sizeof(req->upCtxData)) !=
        NN_OK)) {
        NN_LOG_ERROR("Failed to copy up ctx data");
        return NN_INVALID_PARAM;
    }

    UBSHcomNetTransOpInfo innerOpInfo(opInfo->seqNo, opInfo->timeout, opInfo->errorCode, opInfo->flags);

    return reinterpret_cast<UBSHcomNetEndpoint *>(ep)->PostSend(opcode, transReq, innerOpInfo);
}

int ubs_hcom_ep_post_send_raw(ubs_hcom_endpoint ep, ubs_hcom_send_request *req, uint32_t seqNo)
{
    VALIDATE_EP(ep);
    VALIDATE_REQ(req);
    VALIDATE_SEQ(seqNo);

    UBSHcomNetTransRequest transReq(reinterpret_cast<void *>(req->data), req->size, req->upCtxSize);
    if (NN_UNLIKELY(memcpy_s(transReq.upCtxData, sizeof(transReq.upCtxData), req->upCtxData, sizeof(req->upCtxData)) !=
        NN_OK)) {
        NN_LOG_ERROR("Failed to copy up ctx data");
        return NN_INVALID_PARAM;
    }

    return reinterpret_cast<UBSHcomNetEndpoint *>(ep)->PostSendRaw(transReq, seqNo);
}

int ubs_hcom_ep_post_send_raw_sgl(ubs_hcom_endpoint ep, ubs_hcom_readwrite_request_sgl *req, uint32_t seqNo)
{
    VALIDATE_EP(ep);
    VALIDATE_REQ(req);
    VALIDATE_SEQ(seqNo);
    UBSHcomNetTransSglRequest transReq {};
    UBSHcomNetTransSgeIov iov[C_NET_SGE_MAX_IOV];
    bzero(&transReq, sizeof(UBSHcomNetTransSglRequest));

    if (CopySglInfo(iov, C_NET_SGE_MAX_IOV, req) != NN_OK) {
        return NN_INVALID_PARAM;
    }
    transReq.iov = iov;
    transReq.iovCount = req->iovCount;
    transReq.upCtxSize = req->upCtxSize;
    if (NN_UNLIKELY(memcpy_s(transReq.upCtxData, sizeof(transReq.upCtxData), req->upCtxData, sizeof(req->upCtxData)) !=
        NN_OK)) {
        NN_LOG_ERROR("Failed to copy up ctx data");
        return NN_INVALID_PARAM;
    }

    return reinterpret_cast<UBSHcomNetEndpoint *>(ep)->PostSendRawSgl(transReq, seqNo);
}

int ubs_hcom_ep_post_send_with_seqno(ubs_hcom_endpoint ep, uint16_t opcode, ubs_hcom_send_request *req,
    uint32_t replySeqNo)
{
    VALIDATE_EP(ep);
    VALIDATE_REQ(req);

    UBSHcomNetTransRequest transReq(reinterpret_cast<void *>(req->data), req->size, req->upCtxSize);
    if (NN_UNLIKELY(memcpy_s(transReq.upCtxData, sizeof(transReq.upCtxData), req->upCtxData, sizeof(req->upCtxData)) !=
        NN_OK)) {
        NN_LOG_ERROR("Failed to copy up ctx data");
        return NN_INVALID_PARAM;
    }

    return reinterpret_cast<UBSHcomNetEndpoint *>(ep)->PostSend(opcode, transReq, replySeqNo);
}

int ubs_hcom_ep_post_read(ubs_hcom_endpoint ep, ubs_hcom_readwrite_request *req)
{
    VALIDATE_EP(ep);
    VALIDATE_REQ(req);

    UBSHcomNetTransRequest transReq(req->lMRA, req->rMRA, req->lKey, req->rKey, req->size, req->upCtxSize);
    if (NN_UNLIKELY(memcpy_s(transReq.upCtxData, sizeof(transReq.upCtxData), req->upCtxData, sizeof(req->upCtxData)) !=
        NN_OK)) {
        NN_LOG_ERROR("Failed to post read by copy up ctx data err");
        return NN_INVALID_PARAM;
    }

    return reinterpret_cast<UBSHcomNetEndpoint *>(ep)->PostRead(transReq);
}

int ubs_hcom_ep_post_read_sgl(ubs_hcom_endpoint ep, ubs_hcom_readwrite_request_sgl *req)
{
    VALIDATE_EP(ep);
    VALIDATE_REQ(req);
    UBSHcomNetTransSgeIov iov[C_NET_SGE_MAX_IOV];
    if (CopySglInfo(iov, C_NET_SGE_MAX_IOV, req) != NN_OK) {
        NN_LOG_ERROR("Failed to post read sgl by copy sgl info err");
        return NN_INVALID_PARAM;
    }

    UBSHcomNetTransSglRequest transReq { iov, req->iovCount, req->upCtxSize };
    if (NN_UNLIKELY(memcpy_s(transReq.upCtxData, sizeof(transReq.upCtxData), req->upCtxData, sizeof(req->upCtxData)) !=
        NN_OK)) {
        NN_LOG_ERROR("Failed to post read sgl by copy up ctx data err");
        return NN_INVALID_PARAM;
    }

    return reinterpret_cast<UBSHcomNetEndpoint *>(ep)->PostRead(transReq);
}

int ubs_hcom_ep_post_write(ubs_hcom_endpoint ep, ubs_hcom_readwrite_request *req)
{
    VALIDATE_EP(ep);
    VALIDATE_REQ(req);

    UBSHcomNetTransRequest transReq(req->lMRA, req->rMRA, req->lKey, req->rKey, req->size, req->upCtxSize);
    if (NN_UNLIKELY(memcpy_s(transReq.upCtxData, sizeof(transReq.upCtxData), req->upCtxData, sizeof(req->upCtxData)) !=
        NN_OK)) {
        NN_LOG_ERROR("Failed to post write by copy up ctx data err");
        return NN_INVALID_PARAM;
    }

    return reinterpret_cast<UBSHcomNetEndpoint *>(ep)->PostWrite(transReq);
}

int ubs_hcom_ep_post_write_sgl(ubs_hcom_endpoint ep, ubs_hcom_readwrite_request_sgl *req)
{
    VALIDATE_EP(ep);
    VALIDATE_REQ(req);

    UBSHcomNetTransSgeIov iov[C_NET_SGE_MAX_IOV];
    if (CopySglInfo(iov, C_NET_SGE_MAX_IOV, req) != NN_OK) {
        NN_LOG_ERROR("Failed to post write sgl by copy sgl info err");
        return NN_INVALID_PARAM;
    }

    UBSHcomNetTransSglRequest transReq { iov, req->iovCount, req->upCtxSize };
    if (NN_UNLIKELY(memcpy_s(transReq.upCtxData, sizeof(transReq.upCtxData), req->upCtxData, sizeof(req->upCtxData)) !=
        NN_OK)) {
        NN_LOG_ERROR("Failed to post write sgl by copy up ctx data err");
        return NN_INVALID_PARAM;
    }

    return reinterpret_cast<UBSHcomNetEndpoint *>(ep)->PostWrite(transReq);
}

int ubs_hcom_ep_wait_completion(ubs_hcom_endpoint ep, int32_t timeout)
{
    VALIDATE_EP(ep);

    return reinterpret_cast<UBSHcomNetEndpoint *>(ep)->WaitCompletion(timeout);
}

int ubs_hcom_ep_receive(ubs_hcom_endpoint ep, int32_t timeout, ubs_hcom_response_context **ctx)
{
    if (ctx == nullptr) {
        return NN_INVALID_PARAM;
    }

    VALIDATE_EP(ep);

    static thread_local ubs_hcom_response_context context;
    UBSHcomNetResponseContext rspContext;
    auto result = reinterpret_cast<UBSHcomNetEndpoint *>(ep)->Receive(timeout, rspContext);
    if (NN_LIKELY(result == NN_OK)) {
        context.opCode = rspContext.Header().opCode;
        context.seqNo = rspContext.Header().seqNo;
        context.msgData = rspContext.Message()->Data();
        context.msgSize = rspContext.Header().dataLength;

        *ctx = &context;
    }

    return result;
}

int ubs_hcom_ep_receive_raw(ubs_hcom_endpoint ep, int32_t timeout, ubs_hcom_response_context **ctx)
{
    if (ctx == nullptr) {
        return NN_INVALID_PARAM;
    }

    VALIDATE_EP(ep);

    static thread_local ubs_hcom_response_context context;
    UBSHcomNetResponseContext rspContext;
    auto result = reinterpret_cast<UBSHcomNetEndpoint *>(ep)->ReceiveRaw(timeout, rspContext);
    if (NN_LIKELY(result == NN_OK)) {
        context.opCode = rspContext.Header().opCode;
        context.seqNo = rspContext.Header().seqNo;
        context.msgData = rspContext.Message()->Data();
        context.msgSize = rspContext.Header().dataLength;

        *ctx = &context;
    }

    return result;
}

int ubs_hcom_ep_receive_raw_sgl(ubs_hcom_endpoint ep, int32_t timeout, ubs_hcom_response_context **ctx)
{
    VALIDATE_EP(ep);
    return ubs_hcom_ep_receive_raw(ep, timeout, ctx);
}

void ubs_hcom_ep_refer(ubs_hcom_endpoint ep)
{
    VALIDATE_EP_NO_RET(ep);
    reinterpret_cast<UBSHcomNetEndpoint *>(ep)->IncreaseRef();
}

void ubs_hcom_ep_close(ubs_hcom_endpoint ep)
{
    VALIDATE_EP_NO_RET(ep);
    reinterpret_cast<UBSHcomNetEndpoint *>(ep)->Close();
}

void ubs_hcom_ep_destroy(ubs_hcom_endpoint ep)
{
    VALIDATE_EP_NO_RET(ep);
    reinterpret_cast<UBSHcomNetEndpoint *>(ep)->DecreaseRef();
}

const char *ubs_hcom_err_str(int16_t errCode)
{
    return ock::hcom::UBSHcomNetErrStr(errCode);
}

uint64_t ubs_hcom_estimate_encrypt_len(ubs_hcom_endpoint ep, uint64_t rawLen)
{
    VALIDATE_EP(ep);
    return reinterpret_cast<UBSHcomNetEndpoint *>(ep)->EstimatedEncryptLen(rawLen);
}

int ubs_hcom_encrypt(ubs_hcom_endpoint ep, const void *rawData, uint64_t rawLen, void *cipher, uint64_t *cipherLen)
{
    VALIDATE_EP(ep);
    if (NN_UNLIKELY(cipherLen == nullptr)) {
        NN_LOG_ERROR("Failed to encrypt as cipherLen is nullptr");
        return SER_INVALID_PARAM;
    }
    return reinterpret_cast<UBSHcomNetEndpoint *>(ep)->Encrypt(rawData, rawLen, cipher, *cipherLen);
}

uint64_t ubs_hcom_estimate_decrypt_len(ubs_hcom_endpoint ep, uint64_t cipherLen)
{
    VALIDATE_EP(ep);
    return reinterpret_cast<UBSHcomNetEndpoint *>(ep)->EstimatedDecryptLen(cipherLen);
}

int ubs_hcom_decrypt(ubs_hcom_endpoint ep, const void *cipher, uint64_t cipherLen, void *rawData, uint64_t *rawLen)
{
    VALIDATE_EP(ep);
    if (NN_UNLIKELY(rawLen == nullptr)) {
        NN_LOG_ERROR("Failed to descrypt as rawLen is nullptr");
        return SER_INVALID_PARAM;
    }
    if (NN_UNLIKELY(rawData == nullptr)) {
        NN_LOG_ERROR("Failed to descrypt as rawData is nullptr");
        return SER_INVALID_PARAM;
    }
    return reinterpret_cast<UBSHcomNetEndpoint *>(ep)->Decrypt(cipher, cipherLen, rawData, *rawLen);
}

int ubs_hcom_send_fds(ubs_hcom_endpoint ep, int fds[], uint32_t len)
{
    VALIDATE_EP(ep);
    return reinterpret_cast<UBSHcomNetEndpoint *>(ep)->SendFds(fds, len);
}

int ubs_hcom_receive_fds(ubs_hcom_endpoint ep, int fds[], uint32_t len, int timeoutSec)
{
    VALIDATE_EP(ep);
    return reinterpret_cast<UBSHcomNetEndpoint *>(ep)->ReceiveFds(fds, len, timeoutSec);
}

int ubs_hcom_get_remote_uds_info(ubs_hcom_endpoint ep, ubs_hcom_uds_id_info *idInfo)
{
    VALIDATE_EP(ep);

    if (NN_UNLIKELY(idInfo == nullptr)) {
        return NN_INVALID_PARAM;
    }

    UBSHcomNetUdsIdInfo udsIdInfo {};
    auto result = reinterpret_cast<UBSHcomNetEndpoint *>(ep)->GetRemoteUdsIdInfo(udsIdInfo);
    if (NN_UNLIKELY(result != NN_OK)) {
        return result;
    }
    idInfo->pid = udsIdInfo.pid;
    idInfo->uid = udsIdInfo.uid;
    idInfo->gid = udsIdInfo.gid;
    return result;
}
