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
#include "hcom.h"

#include <csignal>
#include <cstdlib>
#include <cstdint>
#include <algorithm>

#include "hcom_def.h"
#include "hcom_log.h"
#include "securec.h"
#include "code_msg.h"
#include "common/net_common.h"
#include "net_mem_allocator.h"
#include "net_mem_allocator_cache.h"
#include "net_oob.h"
#include "net_oob_ssl.h"
#include "hcom_obj_statistics.h"
#include "net_rdma_driver_oob.h"
#include "net_trace.h"
#include "net_sock_driver_oob.h"
#include "net_shm_driver_oob.h"
#include "verbs_api_wrapper.h"
#include "trace/htracer.h"

#ifdef UB_BUILD_ENABLED
#include "net_ub_driver_oob.h"
#endif

namespace ock {
namespace hcom {
constexpr const int KP_ID = 0x48;
namespace {
// SIGPIPE will be triggered when sending data to a closed socket
struct HcomInit {
    HcomInit() noexcept
    {
        std::signal(SIGPIPE, SIG_IGN);
    }
} g_hcomInitializer;
} // namespace

const UBSHcomNetWorkerIndex &UBSHcomNetEndpoint::WorkerIndex() const
{
    return mWorkerIndex;
}

bool UBSHcomNetEndpoint::IsEstablished()
{
    return mState.Compare(NEP_ESTABLISHED);
}

const std::string &UBSHcomNetEndpoint::PeerConnectPayload() const
{
    return mPayload;
}

uint32_t UBSHcomNetEndpoint::LocalIp() const
{
    return mLocalIp;
}

uint16_t UBSHcomNetEndpoint::ListenPort() const
{
    return mListenPort;
}

uint8_t UBSHcomNetEndpoint::Version() const
{
    return mVersion;
}

void UBSHcomNetEndpoint::DefaultTimeout(int32_t timeout)
{
    if (NN_UNLIKELY(timeout > static_cast<int32_t>(NN_NO65536))) {
        NN_LOG_WARN("Invalid operation to set timeout, the time is less than 65536.");
        return;
    }
    mDefaultTimeout = timeout;
}

void UBSHcomNetEndpoint::StoreConnInfo(uint32_t localIp, uint16_t listenPort, uint8_t version,
    const std::string &payload)
{
    mLocalIp = localIp;
    mListenPort = listenPort;
    mVersion = version;
    mPayload = payload;
}

void UBSHcomNetEndpoint::Payload(const std::string &payload)
{
    mPayload = payload;
}

void UBSHcomNetEndpoint::RemoteUdsIdInfo(uint32_t pid, uint32_t uid, uint32_t gid)
{
    mRemoteUdsIdInfo = UBSHcomNetUdsIdInfo(pid, uid, gid);
}

NResult UBSHcomNetMemoryAllocator::Create(UBSHcomNetMemoryAllocatorType t,
    const UBSHcomNetMemoryAllocatorOptions &options, UBSHcomNetMemoryAllocatorPtr &out)
{
    if (t == DYNAMIC_SIZE) {
        NetLocalAutoDecreasePtr<NetMemAllocator> alloc(new (std::nothrow) NetMemAllocator());
        if (alloc.Get() == nullptr) {
            NN_LOG_ERROR("Failed to new memory allocator obj with type '" <<
                UBSHcomNetMemoryAllocatorTypeToString(t) << "'");
            return NN_NEW_OBJECT_FAILED;
        }

        auto ret = alloc.Get()->Initialize(options.address, options.size, options.minBlockSize, options.alignedAddress);
        if (ret != NN_OK) {
            NN_LOG_ERROR("Failed to initialize allocator obj with type '" <<
                UBSHcomNetMemoryAllocatorTypeToString(t) << "'");
            return NN_ERROR;
        }

        out.Set(alloc.Get());

        return NN_OK;
    } else if (t == DYNAMIC_SIZE_WITH_CACHE) {
        NetLocalAutoDecreasePtr<NetMemAllocator> alloc(new (std::nothrow) NetMemAllocator());
        if (alloc.Get() == nullptr) {
            NN_LOG_ERROR("Failed to new memory allocator with type '" <<
                UBSHcomNetMemoryAllocatorTypeToString(t) << "'");
            return NN_NEW_OBJECT_FAILED;
        }

        auto ret = alloc.Get()->Initialize(options.address, options.size, options.minBlockSize, options.alignedAddress);
        if (ret != NN_OK) {
            NN_LOG_ERROR("Failed to initialize allocator with type '" <<
                UBSHcomNetMemoryAllocatorTypeToString(t) << "'");
            return NN_ERROR;
        }

        NetLocalAutoDecreasePtr<NetAllocatorCache> cache(new (std::nothrow) NetAllocatorCache(alloc.Get()));
        if (cache.Get() == nullptr) {
            NN_LOG_ERROR("Failed to new memory allocator cache with type '" <<
                UBSHcomNetMemoryAllocatorTypeToString(t) << "'");
            return NN_NEW_OBJECT_FAILED;
        }

        ret = cache.Get()->Initialize(options);
        if (ret != NN_OK) {
            NN_LOG_ERROR("Failed to initialize allocator cache with type '" <<
                UBSHcomNetMemoryAllocatorTypeToString(t) <<
                "'");
            return NN_ERROR;
        }

        out.Set(cache.Get());

        return NN_OK;
    }

    NN_LOG_ERROR("Invalid net memory allocator type " << t);
    return NN_ERROR;
}

bool UBSHcomNetOobListenerOptions::SetEid(const std::string &eid, uint16_t id, uint16_t twc)
{
    port = id;
    targetWorkerCount = twc;
    return HexStringToBuff(eid, NN_NO16, ip);
}

bool UBSHcomNetOobListenerOptions::Set(const std::string &pIp, uint16_t pp, uint16_t twc)
{
    if (NN_UNLIKELY(Ip(pIp) == NN_ERROR)) {
        return false;
    }
    port = pp;
    targetWorkerCount = twc;
    return true;
}

bool UBSHcomNetOobListenerOptions::Set(const std::string &pIp, uint16_t pp)
{
    return Set(pIp, pp, UINT16_MAX);
}

bool UBSHcomNetOobListenerOptions::Set(uint16_t pp, uint16_t twc)
{
    port = pp;
    targetWorkerCount = twc;
    return true;
}

NResult UBSHcomNetOobListenerOptions::Ip(const std::string &value)
{
    if (NN_LIKELY(UBSHcomNetCloneStringToArray(ip, sizeof(ip), value))) {
        return NN_OK;
    }

    return NN_ERROR;
}

std::string UBSHcomNetOobListenerOptions::Ip() const
{
    return NN_CHAR_ARRAY_TO_STRING(ip);
}

bool UBSHcomNetOobUDSListenerOptions::Set(const std::string &pName, uint16_t twc)
{
    if (NN_UNLIKELY(!Name(pName))) {
        return false;
    }
    targetWorkerCount = twc;
    return true;
}

bool UBSHcomNetOobUDSListenerOptions::Name(const std::string &value)
{
    NN_SET_CHAR_ARRAY_FROM_STRING(name, value);
}

std::string UBSHcomNetOobUDSListenerOptions::Name() const
{
    return NN_CHAR_ARRAY_TO_STRING(name);
}

uint32_t UBSHcomNetDriver::gMaxListenPort = NN_NO16;
uint8_t UBSHcomNetDriver::gDriverIndex = 0;
std::mutex UBSHcomNetDriver::gDriverMapMutex;
std::map<std::string, UBSHcomNetDriver *> UBSHcomNetDriver::gDriverMap;
int32_t UBSHcomNetDriver::gOSMaxFdCount = -1;

NResult UBSHcomNetDriver::ValidateKunpeng()
{
    std::ifstream file;
    file.open("/sys/devices/system/cpu/cpu0/regs/identification/midr_el1");
    if (!file) {
        NN_LOG_ERROR("Failed to new driver, sys file cannot be open");
        return NN_ERROR;
    }
    std::string line;
    getline(file, line);
    int machineID = 0;
    try {
        machineID = std::stoi(line, nullptr, NN_NO16) >> NN_NO24;
    } catch (...) {
        NN_LOG_ERROR("Failed to new driver, as stoi failed");
    }
    file.close();
    if (machineID != KP_ID) {
        NN_LOG_ERROR("Failed to new driver, CPU company id is invalid");
        return NN_ERROR;
    }

    return NN_OK;
}

UBSHcomNetDriver *UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol t, const std::string &name, bool startOobSvr)
{
#ifdef ENABLE_ARM_KP
    if (NN_UNLIKELY(ValidateKunpeng() != NN_OK)) {
        return nullptr;
    }
#endif
    if (NN_UNLIKELY(NetFunc::NN_ValidateName(name) != NN_OK)) {
        return nullptr;
    }

    UBSHcomNetDriver *driver = nullptr;

    auto envString = getenv("HCOM_ENABLE_TRACE");
    long level = 0;
    if (envString != nullptr && NetFunc::NN_Stol(envString, level) && level > LEVEL0) {
        NetTrace::Instance();
        NetTrace::HtraceInit(name);
    }

    std::lock_guard<std::mutex> locker(gDriverMapMutex);
    auto iter = gDriverMap.find(name);
    if (iter != gDriverMap.end()) {
        NN_LOG_WARN("Driver named " << name << " is already existed, the existed one will be returned");
        return iter->second;
    }

    switch (t) {
        case UBSHcomNetDriverProtocol::RDMA:
#ifdef RDMA_BUILD_ENABLED
            if (HcomIbv::Load() != 0) {
                NN_LOG_ERROR("Failed to load verbs API");
                return nullptr;
            }

            driver = new (std::nothrow) NetDriverRDMAWithOob(name, startOobSvr, t);
            break;
#else
            NN_LOG_ERROR("Failed to new driver, RDMA not enabled");
            return nullptr;
#endif
        case UBSHcomNetDriverProtocol::UBC:
#ifdef UB_BUILD_ENABLED
            if (HcomUrma::Load() != 0) {
                NN_LOG_ERROR("Failed to load urma API");
                return nullptr;
            }
            driver = new (std::nothrow) NetDriverUBWithOob(name, startOobSvr, t);
            break;
#else
            NN_LOG_ERROR("Failed to new driver, UB not enabled");
            return nullptr;
#endif

        case UBSHcomNetDriverProtocol::TCP:
#ifdef SOCK_BUILD_ENABLED
            driver = new (std::nothrow) NetDriverSockWithOOB(name, startOobSvr, t, SockType::SOCK_TCP);
            break;
#else
            NN_LOG_ERROR("Failed to new driver, TCP not enabled");
            return nullptr;
#endif
        case UBSHcomNetDriverProtocol::UDS:
#ifdef SOCK_BUILD_ENABLED
            driver = new (std::nothrow) NetDriverSockWithOOB(name, startOobSvr, t, SockType::SOCK_UDS);
            break;
#else
            NN_LOG_ERROR("Failed to new driver, UDS not enabled");
            return nullptr;
#endif
        case UBSHcomNetDriverProtocol::SHM:
#ifdef SHM_BUILD_ENABLED
            driver = new (std::nothrow) NetDriverShmWithOOB(name, startOobSvr, t);
            break;
#else
            NN_LOG_ERROR("Failed to new driver, SHM not enabled");
            return nullptr;
#endif
        default:
            NN_LOG_ERROR("Failed to new driver " << name << " for " << UBSHcomNetDriverProtocolToString(t) <<
                ", not implemented yet");
            break;
    }

    if (driver != nullptr) {
        driver->IncreaseRef();
        driver->mIndex = gDriverIndex++;
        std::tie(iter, std::ignore) = gDriverMap.emplace(name, driver);
    } else {
        NN_LOG_ERROR("Failed to new driver " << name << " for " << UBSHcomNetDriverProtocolToString(t) <<
            ", probably out of memory");
        return nullptr;
    }

#ifdef HCOM_COMMIT_ID
    NN_LOG_INFO("hcom build commit: " << HCOM_COMMIT_ID);
#endif

#ifdef HCOM_COMPONENT_VERSION
    NN_LOG_INFO("Hcom version :" << HCOM_COMPONENT_VERSION);
    std::string ComponentVersion = HCOM_COMPONENT_VERSION;
    std::vector<std::string> versions;
    NetFunc::NN_SplitStr(ComponentVersion, ".", versions);

    if (versions.size() < NN_NO2) {
        NN_LOG_ERROR("parsing version failed!");
        gDriverMap.erase(iter);
        delete driver;
        driver = nullptr;
        return nullptr;
    }

    long version;
    if (NetFunc::NN_Stol(versions[0], version)) {
        driver->mMajorVersion = version;
    }

    if (NetFunc::NN_Stol(versions[1], version)) {
        driver->mMinorVersion = version;
    }
#endif

    gOSMaxFdCount = static_cast<int32_t>(sysconf(_SC_OPEN_MAX));
    if (NN_UNLIKELY(gOSMaxFdCount == -1)) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_WARN("Unable to get limit of open files, errno: " <<
            NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
    } else {
        NN_LOG_INFO("Limit of open files is " << gOSMaxFdCount << ", please check if it is big enough");
    }

    return driver;
}

NResult UBSHcomNetDriver::DestroyInstance(const std::string &name)
{
    if (NN_UNLIKELY(NetFunc::NN_ValidateName(name) != NN_OK)) {
        return NN_ERROR;
    }
    UBSHcomNetDriver *driver = nullptr;
    {
        std::lock_guard<std::mutex> locker(gDriverMapMutex);
        auto iter = gDriverMap.find(name);
        if (NN_UNLIKELY((iter == gDriverMap.end()) || (iter->second == nullptr))) {
            NN_LOG_ERROR("Failed to destroy driver, because " << name << "driver was not found or does not exist");
            return NN_ERROR;
        }

        driver = iter->second;
        if (NN_UNLIKELY(driver->IsInited() || driver->IsStarted())) {
            NN_LOG_ERROR("Please stop or unInitialize the driver " << name <<
                " first, the current driver status cannot be destroyed");
            return NN_ERROR;
        }
        gDriverMap.erase(iter);
    }
    driver->DecreaseRef();
    HTracerExit();
    return NN_OK;
}

bool UBSHcomNetDriver::LocalSupport(UBSHcomNetDriverProtocol t, UBSHcomNetDriverDeviceInfo &deviceInfo)
{
    UBSHcomNetDriverDeviceInfo tmpInfo {};
#ifdef RDMA_BUILD_ENABLED
    std::vector<RDMADeviceSimpleInfo> enabledDevice;
    uint16_t devCount = 0;
#endif
    std::lock_guard<std::mutex> locker(gDriverMapMutex);
    switch (t) {
        case UBSHcomNetDriverProtocol::RDMA:
#ifdef RDMA_BUILD_ENABLED
            if (HcomIbv::Load() != 0) {
                NN_LOG_WARN("Unable to load verbs API, therefore cannot run RDMA app");
                return false;
            }

            if (RDMADeviceHelper::GetDeviceCount(devCount, enabledDevice) != NN_OK || enabledDevice.empty()) {
                NN_LOG_WARN("Unable to get RDMA devices or no active device found, therefore cannot run RDMA app");
                return false;
            }

            for (auto &iter : enabledDevice) {
                tmpInfo.maxSge = iter.deviceInfo.maxSge < tmpInfo.maxSge ? iter.deviceInfo.maxSge : tmpInfo.maxSge;
            }
            NN_LOG_TRACE_INFO("device count " << devCount << ", active devices count " << enabledDevice.size());

            return true;
#else
            NN_LOG_WARN("Unable to get RDMA devices or no active device found, rdma compilation not enabled");
            return false;
#endif

        case UBSHcomNetDriverProtocol::TCP:
        case UBSHcomNetDriverProtocol::UDS:
        case UBSHcomNetDriverProtocol::SHM:
            return true;
        default:
            NN_LOG_WARN("Un-supported protocol");
            break;
    }

    deviceInfo = tmpInfo;
    return false;
}

bool UBSHcomNetDriver::MultiRailGetDevCount(UBSHcomNetDriverProtocol t, std::string ipMask, uint16_t &enableDevCount,
    std::string ipGroup)
{
#if defined(RDMA_BUILD_ENABLED) || defined(UB_BUILD_ENABLED)
    uint16_t devCount = 0;
    std::vector<std::string> enableIps;
#endif
    std::lock_guard<std::mutex> locker(gDriverMapMutex);
    switch (t) {
        case UBSHcomNetDriverProtocol::RDMA:
#ifdef RDMA_BUILD_ENABLED
            if (HcomIbv::Load() != 0) {
                NN_LOG_WARN("Unable to load verbs API, therefore cannot run RDMA app");
                return false;
            }

            if (RDMADeviceHelper::GetEnableDeviceCount(ipMask, devCount, enableIps, ipGroup) != NN_OK ||
                devCount == 0) {
                NN_LOG_WARN("Unable to get RDMA devices or no active device found, therefore cannot run RDMA app");
                return false;
            }
            enableDevCount = devCount;

            return true;
#else
            NN_LOG_WARN("Unable to get RDMA devices or no active device found, rdma compilation not enabled");
            return false;
#endif

        case UBSHcomNetDriverProtocol::TCP:
        case UBSHcomNetDriverProtocol::UDS:
        case UBSHcomNetDriverProtocol::SHM:
            return true;
        case UBSHcomNetDriverProtocol::UBC:
#ifdef UB_BUILD_ENABLED
            if (HcomUrma::Load() != 0) {
                NN_LOG_WARN("Failed to load verbs API, unable to run RDMA app");
                return false;
            }

            if (UBDeviceHelper::GetEnableDeviceCount(ipMask, devCount, enableIps, ipGroup) != UB_OK || devCount == 0) {
                NN_LOG_WARN("Failed to get URMA devices or no active device found, unable to run URMA app");
                return false;
            }
            enableDevCount = devCount;
            return true;
#endif
            NN_LOG_WARN("Failed to get URMA devices or no active device found, URMA compilation not enabled");
            return false;

        default:
            NN_LOG_WARN("Un-supported protocol");
            break;
    }

    return false;
}

/*
 * @brief Create listeners, must be called after workers created and need to set new conn handler *
 */
NResult UBSHcomNetDriver::CreateListeners(bool enableMultiRail)
{
    if (enableMultiRail) {
        return CreateServerLB();
    }
    if (mOptions.oobType != NET_OOB_UDS && mOptions.oobType != NET_OOB_TCP) {
        NN_LOG_ERROR("Un-supported oob type " << mOptions.oobType << " is set in driver " << mName);
        return NN_INVALID_PARAM;
    } else if (mOptions.oobType == NET_OOB_UDS) {
        return CreateUdsListeners();
    }

    if (mOobListenOptions.empty()) {
        NN_LOG_ERROR("No listen info is set for oob type " << UBSHcomNetDriverOobTypeToString(mOptions.oobType) <<
            " in driver " << mName);
        return NN_INVALID_PARAM;
    }

    uint16_t oobIndex = 0;
    for (auto &lOpt : mOobListenOptions) {
        NetOOBServerPtr oobServer = nullptr;
        /* create oob server */
        if (mEnableTls) {
            auto oobSSLServer = new (std::nothrow) OOBSSLServer(mOptions.oobType, lOpt.Ip(), lOpt.port,
                mTlsPrivateKeyCB, mTlsCertCB, mTlsCaCallback);
            NN_ASSERT_LOG_RETURN(oobSSLServer != nullptr, NN_NEW_OBJECT_FAILED)
            oobSSLServer->SetTlsOptions(mOptions.cipherSuite, mOptions.tlsVersion);
            oobSSLServer->SetPSKCallback(mPskFindSessionCb, mPskUseSessionCb);
            oobServer = oobSSLServer;
        } else {
            oobServer = new (std::nothrow) OOBTCPServer(mOptions.oobType, lOpt.Ip(), lOpt.port);
            NN_ASSERT_LOG_RETURN(oobServer.Get() != nullptr, NN_NEW_OBJECT_FAILED)
        }

        if (lOpt.port == 0) {
            if (oobServer->EnableAutoPortSelection(mPortRange.first, mPortRange.second)) {
                return NN_INVALID_PARAM;
            }
        }

        NN_LOG_TRACE_INFO(lOpt.second.Ip());

        oobServer->Index({ mIndex, oobIndex++ });
        oobServer->SetMaxConntionNum(mOptions.maxConnectionNum);

        /* create load balancer for each oob server */
        auto twc = lOpt.targetWorkerCount == 0 ? UINT16_MAX : lOpt.targetWorkerCount;
        NetWorkerLBPtr lb = new (std::nothrow) NetWorkerLB(mName, mOptions.lbPolicy, twc);
        if (NN_UNLIKELY(lb == nullptr)) {
            NN_LOG_ERROR("Failed to new oob load balancer in driver " << mName);
            return NN_NEW_OBJECT_FAILED;
        }

        /* attach lb to oob server in case of leak */
        oobServer->SetWorkerLb(lb.Get());

        /* add worker groups to lb */
        if (NN_UNLIKELY(lb->AddWorkerGroups(mWorkerGroups) != NN_OK)) {
            NN_LOG_ERROR("Failed to added worker groups into load balancer in driver " << mName);
            return NN_NEW_OBJECT_FAILED;
        }

        oobServer->IncreaseRef();
        mOobServers.emplace_back(oobServer.Get());
    }

    if (mOobListenOptions.size() != mOobServers.size()) {
        NN_LOG_ERROR("Created oob server count " << mOobServers.size() << " is not equal to listener options size " <<
            mOobListenOptions.size() << " in driver " << mName);
        return NN_ERROR;
    }

    return NN_OK;
}

NResult UBSHcomNetDriver::CreateUdsListeners()
{
    if (mOobUdsListenOptions.empty()) {
        NN_LOG_ERROR("No listen info is set in driver " << mName);
        return NN_INVALID_PARAM;
    }

    uint16_t oobIndex = 0;
    for (auto &lOpt : mOobUdsListenOptions) {
        NetOOBServerPtr oobServer = nullptr;
        /* create oob server */
        if (mEnableTls) {
            auto oobSSLServer = new (std::nothrow) OOBSSLServer(mOptions.oobType, lOpt.second.Name(), lOpt.second.perm,
                lOpt.second.isCheck, mTlsPrivateKeyCB, mTlsCertCB, mTlsCaCallback);
            NN_ASSERT_LOG_RETURN(oobSSLServer != nullptr, NN_NEW_OBJECT_FAILED)
            oobSSLServer->SetTlsOptions(mOptions.cipherSuite, mOptions.tlsVersion);
            oobSSLServer->SetPSKCallback(mPskFindSessionCb, mPskUseSessionCb);
            oobServer = oobSSLServer;
        } else {
            oobServer = new (std::nothrow)
                OOBTCPServer(mOptions.oobType, lOpt.second.Name(), lOpt.second.perm, lOpt.second.isCheck);
            NN_ASSERT_LOG_RETURN(oobServer.Get() != nullptr, NN_NEW_OBJECT_FAILED)
        }

        NN_LOG_TRACE_INFO(lOpt.second.Name());

        oobServer->Index({ mIndex, oobIndex++ });
        oobServer->SetMaxConntionNum(mOptions.maxConnectionNum);

        /* create load balancer ptr for each oob server */
        auto twc = lOpt.second.targetWorkerCount == 0 ? UINT16_MAX : lOpt.second.targetWorkerCount;
        NetWorkerLBPtr lb = new (std::nothrow) NetWorkerLB(mName, mOptions.lbPolicy, twc);
        if (NN_UNLIKELY(lb == nullptr)) {
            NN_LOG_ERROR("Failed to new oob load balancer in uds driver " << mName);
            return NN_NEW_OBJECT_FAILED;
        }

        /* attach lb to oob server in case of leak */
        oobServer->SetWorkerLb(lb.Get());

        /* add worker groups to lb */
        if (NN_UNLIKELY(lb->AddWorkerGroups(mWorkerGroups) != NN_OK)) {
            NN_LOG_ERROR("Failed to added worker groups into load balancer in uds driver " << mName);
            return NN_NEW_OBJECT_FAILED;
        }

        oobServer->IncreaseRef();
        mOobServers.emplace_back(oobServer.Get());
    }

    if (mOobUdsListenOptions.size() != mOobServers.size()) {
        NN_LOG_ERROR("Created oob server count " << mOobServers.size() << " is not equal to listener options size " <<
            mOobUdsListenOptions.size() << " in uds driver " << mName);
        return NN_ERROR;
    }

    return NN_OK;
}

NResult UBSHcomNetDriver::CreateServerLB()
{
    /* create load balancer for each oob server */
    NetWorkerLBPtr lb = new (std::nothrow) NetWorkerLB(mName, mOptions.lbPolicy, UINT16_MAX);
    if (NN_UNLIKELY(lb == nullptr)) {
        NN_LOG_ERROR("Failed to new oob load balancer in driver " << mName);
        return NN_NEW_OBJECT_FAILED;
    }

    /* add worker groups to lb */
    if (NN_UNLIKELY(lb->AddWorkerGroups(mWorkerGroups) != NN_OK)) {
        NN_LOG_ERROR("Failed to added worker groups into load balancer in driver " << mName);
        return NN_NEW_OBJECT_FAILED;
    }

    lb->IncreaseRef();
    mServerLb = lb.Get();

    return NN_OK;
}

NResult UBSHcomNetDriver::StartListeners()
{
    NResult result = NN_OK;
    for (uint64_t i = 0; i < mOobServers.size(); i++) {
        if (NN_UNLIKELY(mOobServers[i] == nullptr)) {
            NN_LOG_WARN("index " << i << "of oobServer is null");
            continue;
        }
        if ((result = mOobServers[i]->Start()) != NN_OK) {
            for (uint64_t j = 0; j < i; j++) {
                mOobServers[j]->Stop();
            }
            return result;
        }
    }

    // get auto selected listen port
    for (uint64_t i = 0; i < mOobListenOptions.size(); i++) {
        if (mOobListenOptions[i].port == 0) {
            uint16_t port = 0;
            // for tcp oob, mOobServers.size() must be equal to mOobListenOptions.size()
            if (mOobServers[i]->GetListenPort(port) == NN_OK) {
                mOobListenOptions[i].port = port;
            } else {
                NN_LOG_WARN("Invalid to get real listen port for " << mOobListenOptions[i].Ip() << ":" <<
                    mOobListenOptions[i].port);
            }
        }
    }

    return NN_OK;
}

NResult UBSHcomNetDriver::StopListeners(bool clear)
{
    for (auto &item : mOobServers) {
        item->Stop();
        if (clear) {
            item->DecreaseRef();
        }
    }

    if (clear) {
        mOobServers.clear();
    }

    return NN_OK;
}

NResult UBSHcomNetDriver::CreateClientLB()
{
    NResult result = NN_OK;
    NetWorkerLBPtr lb = new (std::nothrow) NetWorkerLB(mName, mOptions.lbPolicy, UINT16_MAX);
    if (NN_UNLIKELY(lb.Get() == nullptr)) {
        NN_LOG_ERROR("Failed to new lb object in driver " << mName << ", probably out of memory");
        return NN_NEW_OBJECT_FAILED;
    }

    if (NN_UNLIKELY((result = lb->AddWorkerGroups(mWorkerGroups)) != NN_OK)) {
        NN_LOG_ERROR("Failed to add worker into load balancer result " << result << " in driver " << mName);
        return result;
    }

    lb->IncreaseRef();
    mClientLb = lb.Get();
    return NN_OK;
}

void UBSHcomNetDriver::DestroyClientLB()
{
    if (mClientLb != nullptr) {
        mClientLb->DecreaseRef();
        mClientLb = nullptr;
    }
}

void UBSHcomNetDriver::DumpObjectStatistics()
{
    NetObjStatistic::Dump();
}

void UBSHcomNetDriver::OobIpAndPort(const std::string &ip, uint16_t port)
{
    if (mStartOobSvr) {
        if (inet_addr(ip.c_str()) == 0) {
            NN_LOG_ERROR("SetOobIpAndPort failed, ip addr is 0.0.0.0");
            return;
        }

        UBSHcomNetOobListenerOptions opt{};
        if (NN_UNLIKELY(!opt.Set(ip, port, UINT16_MAX))) {
            NN_LOG_ERROR("set UBSHcomNetOobListenerOptions failed");
            return;
        }
        AddOobOptions(opt);
        return;
    }

    mOobIp = ip;
    mOobPort = port;
}

void UBSHcomNetDriver::OobEidAndJettyId(const std::string &eid, uint16_t id)
{
    std::string s;
    std::remove_copy(eid.begin(), eid.end(), std::back_inserter(s), ':');
    if (s.length() != NN_NO32) {
        NN_LOG_ERROR("Ensure the eid is of 128b size after erasing the colon sign");
        return;
    }
    if (id < NN_NO2 || id > NN_NO1023) {
        NN_LOG_ERROR("Ensure the jetty id in range 2~1023");
        return;
    }
    if (mStartOobSvr) {
        UBSHcomNetOobListenerOptions opt{};
        if (NN_UNLIKELY(!opt.SetEid(s, id, UINT16_MAX))) {
            NN_LOG_ERROR("set UBSHcomNetOobListenerOptions failed");
            return;
        }
        AddOobOptions(opt);
        return;
    }

    mOobIp = eid;
    mOobPort = id;
}

bool UBSHcomNetDriver::GetOobIpAndPort(std::vector<std::pair<std::string, uint16_t>> &result)
{
    if (!mStartOobSvr) {
        NN_LOG_ERROR("GetOobIpAndPort failed, it is not server");
        return false;
    }

    if (!mStarted) {
        NN_LOG_ERROR("GetOobIpAndPort failed, net driver is not started");
        return false;
    }

    result.clear();
    for (const auto& item : mOobListenOptions) {
        result.emplace_back(item.Ip(), item.port);
    }
    return true;
}

NResult UBSHcomNetDriver::ValidateAndParseOobPortRange(const char* oobPortRange)
{
    if (oobPortRange == nullptr || oobPortRange[0] == '\0') {
        return NN_OK;
    }

    std::vector<std::string> portStr;
    std::string strPortRange(oobPortRange);
    NetFunc::NN_SplitStr(oobPortRange, "-", portStr);

    const int portSize = 2;
    if (portStr.size() != portSize) {
        NN_LOG_ERROR("oobPortRange is invalid, oobPortRange consists of two numbers connected by '-'");
        return NN_ERROR;
    }

    long lowerLimit = 0;
    if (!NetFunc::NN_Stol(portStr[0], lowerLimit)) {
        NN_LOG_ERROR("parse lower limit of oobPortRange(" << portStr[0] << ") failed");
        return NN_ERROR;
    }
    if (lowerLimit < NN_NO1024 || lowerLimit > NN_NO65535) {
        NN_LOG_ERROR("lower limit of oobPortRange invalid, port number must be in the range 1024-65535");
        return NN_ERROR;
    }

    long upperLimit = 0;
    if (!NetFunc::NN_Stol(portStr[1], upperLimit)) {
        NN_LOG_ERROR("parse upper limit of oobPortRange(" << portStr[1] << ") failed");
        return NN_ERROR;
    }
    if (upperLimit < NN_NO1024 || upperLimit > NN_NO65535) {
        NN_LOG_ERROR("upper limit of oobPortRange invalid, port number must be in the range 1024-65535");
        return NN_ERROR;
    }

    if (lowerLimit > upperLimit) {
        NN_LOG_ERROR("lower limit of oobPortRange is bigger than the upper limit");
        return NN_ERROR;
    }

    mPortRange.first = static_cast<uint16_t>(lowerLimit);
    mPortRange.second = static_cast<uint16_t>(upperLimit);
    return NN_OK;
}

NResult UBSHcomNetDriver::ParseUrl(const std::string &url, NetDriverOobType &type, std::string &ip, uint16_t &port)
{
    NetProtocol protocal;
    std::string urlSuffix;
    if (NN_UNLIKELY(!NetFunc::NN_SplitProtoUrl(url, protocal, urlSuffix))) {
        NN_LOG_ERROR("Invalid url: "<< url <<" should be like tcp://127.0.0.1:9981 or uds://name or ubc://eid:jettyId");
        return NN_PARAM_INVALID;
    }

    if (protocal == NetProtocol::NET_UBC) {
        type = NetDriverOobType::NET_OOB_UB;

        if (NN_UNLIKELY(!NetFunc::NN_ConvertEidAndJettyId(urlSuffix, ip, port))) {
            NN_LOG_ERROR("Invalid url: " << url << " should be like 1111:1111:0000:0000:0000:0000:4444:0000:888");
            return NN_PARAM_INVALID;
        }
        return SER_OK;
    }

    if (protocal == NetProtocol::NET_UDS) {
        type = NetDriverOobType::NET_OOB_UDS;
        ip = urlSuffix;
        return SER_OK;
    }

    type = NetDriverOobType::NET_OOB_TCP;
    if (NN_UNLIKELY(!NetFunc::NN_ConvertIpAndPort(urlSuffix, ip, port))) {
        NN_LOG_ERROR("Invalid url: " << url <<" should be like 127.0.0.1:9981");
        return NN_PARAM_INVALID;
    }

    return SER_OK;
}

void UBSHcomNetDriver::AddOobOptions(const UBSHcomNetOobListenerOptions &option)
{
    {
        std::lock_guard<std::mutex> guard(mInitMutex);
        if (NN_UNLIKELY(mOobListenOptions.size() >= gMaxListenPort)) {
            NN_LOG_ERROR("Only " << gMaxListenPort << " listeners is allowed in driver");
            return;
        }

        // The same port number cannot be used for two identical IP addresses
        // The same port number can be used for two different IP addresses
        for (const auto& opt : mOobListenOptions) {
            if (opt.Ip() == option.Ip() && opt.port == option.port && opt.port != 0) {
                NN_LOG_WARN("Duplicated listen '" << option.Ip() << ":" << option.port << "' adding to driver " <<
                    mName << ", ignored");
                return;
            }
        }

        mOobListenOptions.emplace_back(option);
    }
}

void UBSHcomNetDriver::OobUdsName(const std::string &name)
{
    if (name.length() >= sizeof(UBSHcomNetOobUDSListenerOptions::name)) {
        NN_LOG_ERROR("Uds name is too long for driver " << mName);
        return;
    }

    if (mStartOobSvr) {
        UBSHcomNetOobUDSListenerOptions opt{};
        if (NN_UNLIKELY(!opt.Set(name, UINT16_MAX))) {
            NN_LOG_ERROR("set UBSHcomNetOobUDSListenerOptions failed");
            return;
        }
        AddOobUdsOptions(opt);
        return;
    }

    mUdsName = name;
}

void UBSHcomNetDriver::AddOobUdsOptions(const UBSHcomNetOobUDSListenerOptions &option)
{
    std::lock_guard<std::mutex> guard(mInitMutex);
    if (NN_UNLIKELY(mOobUdsListenOptions.size() >= gMaxListenPort)) {
        NN_LOG_ERROR("Only " << gMaxListenPort << " listeners is allowed in driver");
        return;
    }
    if (NN_UNLIKELY(NetFunc::NN_ValidateUrl(option.Name()) != NN_OK)) {
        NN_LOG_ERROR("Invalid uds name");
        return;
    }

    auto iter = mOobUdsListenOptions.find(option.Name());
    if (NN_UNLIKELY(iter != mOobUdsListenOptions.end())) {
        NN_LOG_WARN("Duplicated listen name '" << option.Name() << "' adding to driver " << mName << ", ignored");
        return;
    }

    mOobUdsListenOptions[option.Name()] = option;
}

NResult UBSHcomNetDriver::ValidateHandlesCheck()
{
    if (mReceivedRequestHandler == nullptr) {
        NN_LOG_ERROR("Failed to do start in Driver " << mName << ", as receivedRequestHandler is null");
        return NN_INVALID_PARAM;
    }

    if (mRequestPostedHandler == nullptr) {
        NN_LOG_ERROR("Failed to do start in Driver " << mName << ", as requestPostedHandler is null");
        return NN_INVALID_PARAM;
    }

    if (mOneSideDoneHandler == nullptr) {
        NN_LOG_ERROR("Failed to do start in Driver " << mName << ", as oneSideDoneHandler is null");
        return NN_INVALID_PARAM;
    }
    // SHM self polling mode not register ep handler
    if (mProtocol != SHM && mEndPointBrokenHandler == nullptr) {
        NN_LOG_ERROR("Failed to do start in Driver " << mName << ", as endPointBrokenHandler is null");
        return NN_INVALID_PARAM;
    }
    return NN_OK;
}

NResult UBSHcomNetDriver::ValidateOptionsOobType()
{
    if (mProtocol != UBC && mOptions.oobType == NET_OOB_UB) {
        NN_LOG_ERROR("Failed to do start in Driver " << mName << ", only the UBC protocol can be set NET_OOB_UB.");
        return NN_INVALID_PARAM;
    }
    if (mOptions.oobType == NET_OOB_UB && mOptions.enableTls) {
        NN_LOG_ERROR("Failed to do start in Driver " << mName << ", as oobType NET_OOB_UB does not support enableTls.");
        return NN_INVALID_PARAM;
    }
    return NN_OK;
}

void UBSHcomNetDriver::RegisterNewEPHandler(const UBSHcomNetDriverNewEndPointHandler &handler)
{
    mNewEndPointHandler = handler;
}
void UBSHcomNetDriver::RegisterEPBrokenHandler(const UBSHcomNetDriverEndpointBrokenHandler &handler)
{
    mEndPointBrokenHandler = handler;
}

void UBSHcomNetDriver::RegisterNewReqHandler(const UBSHcomNetDriverReceivedHandler &handler)
{
    mReceivedRequestHandler = handler;
}

void UBSHcomNetDriver::RegisterReqPostedHandler(const UBSHcomNetDriverSentHandler &handler)
{
    mRequestPostedHandler = handler;
}

void UBSHcomNetDriver::RegisterOneSideDoneHandler(const UBSHcomNetDriverOneSideDoneHandler &handler)
{
    mOneSideDoneHandler = handler;
}

void UBSHcomNetDriver::RegisterIdleHandler(const UBSHcomNetDriverIdleHandler &handler)
{
    mIdleHandler = handler;
}

void UBSHcomNetDriver::RegisterTLSCaCallback(const UBSHcomTLSCaCallback &cb)
{
    mTlsCaCallback = cb;
}

void UBSHcomNetDriver::RegisterTLSCertificationCallback(const UBSHcomTLSCertificationCallback &cb)
{
    mTlsCertCB = cb;
}

void UBSHcomNetDriver::RegisterTLSPrivateKeyCallback(const UBSHcomTLSPrivateKeyCallback &cb)
{
    mTlsPrivateKeyCB = cb;
}

void UBSHcomNetDriver::RegisterEndpointSecInfoProvider(const UBSHcomNetDriverEndpointSecInfoProvider &provider)
{
    mSecInfoProvider = provider;
}

void UBSHcomNetDriver::RegisterEndpointSecInfoValidator(const UBSHcomNetDriverEndpointSecInfoValidator &validator)
{
    mSecInfoValidator = validator;
}

void UBSHcomNetDriver::RegisterPskUseSessionCb(const UBSHcomPskUseSessionCb &cb)
{
    mPskUseSessionCb = cb;
}

void UBSHcomNetDriver::RegisterPskFindSessionCb(const UBSHcomPskFindSessionCb &cb)
{
    mPskFindSessionCb = cb;
}

constexpr int16_t ERROR_CODE_100 = 100;
constexpr int16_t ERROR_CODE_200 = 200;
constexpr int16_t ERROR_CODE_300 = 300;
constexpr int16_t ERROR_CODE_400 = 400;
constexpr int16_t ERROR_CODE_500 = 500;
constexpr int16_t ERROR_CODE_600 = 600;

const char *UBSHcomNetErrStr(int16_t errCode)
{
    if (errCode == 0) {
        return "OK";
    }
    int32_t index = 0;
    if (errCode >= ERROR_CODE_100 && errCode < ERROR_CODE_200) {
        index = errCode - ERROR_CODE_100;
        if (index < NNCodeArrayLength) {
            return NNCodeArray[index];
        } else {
            return "ILLEGAL_CODE";
        }
    }

    if (errCode >= ERROR_CODE_200 && errCode < ERROR_CODE_300) {
        index = errCode - ERROR_CODE_200;
        if (index < RRCodeArrayLength) {
            return RRCodeArray[index];
        } else {
            return "ILLEGAL_CODE";
        }
    }

    if (errCode >= ERROR_CODE_300 && errCode < ERROR_CODE_400) {
        index = errCode - ERROR_CODE_300;
        if (index < ShCodeArrayLength) {
            return ShCodeArray[index];
        } else {
            return "ILLEGAL_CODE";
        }
    }

    if (errCode >= ERROR_CODE_400 && errCode < ERROR_CODE_500) {
        index = errCode - ERROR_CODE_400;
        if (index < SCodeArrayLength) {
            return SCodeArray[index];
        } else {
            return "ILLEGAL_CODE";
        }
    }

    if (errCode >= ERROR_CODE_500 && errCode < ERROR_CODE_600) {
        index = errCode - ERROR_CODE_500;
        if (index < SevCodeArrayLength) {
            return SevCodeArray[index];
        } else {
            return "ILLEGAL_CODE";
        }
    }

    return "ILLEGAL_CODE";
}

std::string &UBSHcomNEPStateToString(UBSHcomNetEndPointState v)
{
    static std::string nepStateString[NEP_BUFF] = {"new", "established", "broken"};
    static std::string unknown = "UNKNOWN EP STATE";
    if (v != NEP_NEW && v != NEP_ESTABLISHED && v != NEP_BROKEN) {
        return unknown;
    }
    return nepStateString[v];
}

std::string &UBSHcomRequestStatusToString(UBSHcomNetRequestStatus status)
{
    static std::string requestStatus[NN_NO5] = {"Called", "In HCOM", "In URMA", "Polled", "Success"};
    static std::string invalid = "INVALID STATUS";
    if (status > UBSHcomNetRequestStatus::SUCCESS) {
        return invalid;
    }
    int value = static_cast<int>(status);
    return requestStatus[value];
}

void SetTraceIdInner(const std::string &traceId)
{
#ifdef UB_BUILD_ENABLED
    if (HcomUrma::IsLoaded()) {
        HcomUrma::LogSetThreadTag(traceId.c_str());
        return;
    }
#endif
    NN_LOG_WARN("failed to set trace id, urma api is not loaded");
}

std::string &UBSHcomNetMemoryAllocatorTypeToString(UBSHcomNetMemoryAllocatorType v)
{
    static std::string allocatorType[NN_NO2] = {"Dynamic size allocator", "Dynamic size allocator with cache"};
    static std::string unknown = "UNKNOWN ALLOCATOR TYPE";
    if (v != DYNAMIC_SIZE && v != DYNAMIC_SIZE_WITH_CACHE) {
        return unknown;
    }
    return allocatorType[v];
}

std::string UBSHcomNetMemoryAllocatorOptions::ToString() const
{
    std::ostringstream oss;
    oss << ", size " << size << ", minBlockSize " << minBlockSize <<
        ", cacheTierCount " << cacheTierCount << ", cacheBlockCountPerTier " <<
        cacheBlockCountPerTier << ", cacheTierPolicy " << cacheTierPolicy;
    return oss.str();
}

std::string &UBSHcomNetDriverOobTypeToString(NetDriverOobType v)
{
    static std::string oobType[NN_NO3] = {"Tcp", "UDS", "URMA"};
    static std::string unknown = "UNKNOWN OOB TYPE";
    if (v != NET_OOB_TCP && v != NET_OOB_UDS && v != NET_OOB_UB) {
        return unknown;
    }
    return oobType[v];
}

std::string &UBSHcomNetDriverSecTypeToString(UBSHcomNetDriverSecType v)
{
    static std::string secType[NN_NO3] = {"SecNoValid", "SecValidOneWay", "SecValidTwoWay", };
    static std::string unknown = "UNKNOWN SEC TYPE";
    if (v != NET_SEC_VALID_ONE_WAY && v != NET_SEC_VALID_TWO_WAY) {
        return unknown;
    }
    return secType[v];
}

std::string &UBSHcomNetDriverLBPolicyToString(UBSHcomNetDriverLBPolicy v)
{
    static std::string driverLB[NN_NO2] = {"RR", "Hash", };
    static std::string unknown = "UNKNOWN POLICY";
    if (v != NET_ROUND_ROBIN && v != NET_HASH_IP_PORT) {
        return unknown;
    }
    return driverLB[v];
}

std::string &UBSHcomNetDriverProtocolToString(UBSHcomNetDriverProtocol v)
{
    static std::string driverProtocol[NN_NO8] = {"RDMA", "TCP", "UDS", "SHM", "UNKNOWN PROTOCOL", "UNKNOWN PROTOCOL",
        "UNKNOWN PROTOCOL", "UBC"};
    static std::string unknown = "UNKNOWN PROTOCOL";
    if (v >= NN_NO8) {
        return unknown;
    }
    return driverProtocol[v];
}

bool UBSHcomNetCloneStringToArray(char *dest, size_t destMax, const std::string &src)
{
    if (NN_LIKELY(src.length() < destMax)) {
        int ret = strcpy_s(dest, destMax, src.c_str());
        if (NN_UNLIKELY(ret != EOK)) {
            NN_LOG_ERROR("copy string failed, ret " << ret);
            return false;
        }
        return true;
    }

    NN_LOG_ERROR("Invalid src length " << src.length() + NN_NO1 << " clone to dest length" << destMax);
    return false;
}

NResult ValidateWorkerOptions(UBSHcomNetDriverWorkingMode mode, char *workerGroups, char *workerGroupsCpuSet,
    UBSHcomNetDriverLBPolicy lbPolicy, int workerThreadPriority)
{
    /* validate param related to poll mode for RDMA, Sock and SHM */
    if (NN_UNLIKELY(mode != NET_BUSY_POLLING && mode != NET_EVENT_POLLING)) {
        NN_LOG_ERROR("Option 'mode' is invalid, " << mode <<
            " is set in driver, valid value is NET_BUSY_POLLING(0) or NET_EVENT_POLLING(1)");
        return NN_INVALID_PARAM;
    }

    /* validate params related to worker group for RDMA, Sock and SHM */
    if (NN_UNLIKELY(!ValidateArrayOptions(workerGroups, NN_NO64))) {
        NN_LOG_ERROR("Option 'workerGroups' is invalid, the Array max length is 64.");
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(!ValidateArrayOptions(workerGroupsCpuSet, NN_NO128))) {
        NN_LOG_ERROR("Option 'workerGroupsCpuSet' is invalid, the Array max length is 128.");
        return NN_INVALID_PARAM;
    }

    /* validate param related to load balance policy for RDMA, Sock and SHM */
    if (NN_UNLIKELY(lbPolicy != NET_ROUND_ROBIN && lbPolicy != NET_HASH_IP_PORT)) {
        NN_LOG_ERROR("Option 'oobType' is invalid, " << lbPolicy <<
            " is set in driver, valid value is NET_ROUND_ROBIN(0) or NET_HASH_IP_PORT(1)");
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(workerThreadPriority > static_cast<int>(NN_NO20) ||
        workerThreadPriority < -static_cast<int>(NN_NO20))) {
        NN_LOG_ERROR("Option 'workerThreadPriority' is invalid, it should be set from -20 to 20 closed, 0 means do not "
            "set priority");
        return NN_INVALID_PARAM;
    }

    return NN_OK;
}

NResult ValidateOobOptions(NetDriverOobType oobType)
{
    /* validate param related to net driver oobType for RDMA, Sock and SHM */
    if (NN_UNLIKELY(oobType > NET_OOB_UB)) {
        NN_LOG_ERROR("Option 'oobType' is invalid, " << oobType <<
            " is set in driver, valid value is NET_OOB_TCP(0) or NET_OOB_UDS(1) or NET_OOB_UB(2)");
        return NN_INVALID_PARAM;
    }
    return NN_OK;
}

NResult ValidateHeartbeatOptions(uint16_t heartBeatIdleTime, uint16_t heartBeatProbeTimes,
    uint16_t heartBeatProbeInterval)
{
    if (NN_UNLIKELY(heartBeatIdleTime == 0 || heartBeatIdleTime > NN_NO10000)) {
        NN_LOG_ERROR("Option 'heartBeatIdleTime' is invalid, " << heartBeatIdleTime <<
            " is set in driver, the valid value range is 1s ~ 10000s");
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(heartBeatProbeTimes == 0 || heartBeatProbeTimes > NN_NO1024)) {
        NN_LOG_ERROR("Option 'heartBeatProbeTime' is invalid, " << heartBeatProbeTimes <<
            " is set in driver, the valid value range is 1s ~ 1024s");
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(heartBeatProbeInterval > NN_NO1024)) {
        NN_LOG_ERROR("Option 'heartBeatProbeInterval' is invalid, " << heartBeatProbeInterval <<
            " is set in driver, the valid value range is 1s ~ 1024s");
        return NN_INVALID_PARAM;
    }
    return NN_OK;
}

NResult ValidateQueueOptions(uint32_t qpSendQueueSize, uint32_t qpReceiveQueueSize, uint16_t completionQueueDepth)
{
    /* validate params related to send queue and receive queue size for RDMA and Sock */
    if (NN_UNLIKELY(qpSendQueueSize < NN_NO16 || qpSendQueueSize > NN_NO65535)) {
        NN_LOG_ERROR("Option 'qpSendQueueSize' is invalid, " << qpSendQueueSize <<
            " is set in driver, the valid value range is 16 ~ 65535");
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(qpReceiveQueueSize < NN_NO16 || qpReceiveQueueSize > NN_NO65535)) {
        NN_LOG_ERROR("Option 'qpReceiveQueueSize' is invalid " << qpReceiveQueueSize <<
            " is set in driver, the valid value range is 16 ~ 65535");
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(completionQueueDepth == NN_NO0 || completionQueueDepth > NN_NO65535)) {
        NN_LOG_ERROR("Option 'completionQueueDepth' is invalid " << completionQueueDepth <<
            " is set in driver, the valid value range is 1 ~ 65535");
        return NN_INVALID_PARAM;
    }
    return NN_OK;
}

NResult ValidatePollingOptions(uint16_t pollingBatchSize, uint32_t eventPollingTimeout)
{
    /* validate params related to poll for RDMA, Sock and SHM */
    if (NN_UNLIKELY(pollingBatchSize == 0 || pollingBatchSize > NN_NO1024)) {
        NN_LOG_ERROR("Option 'pollingBatchSize' is invalid, " << pollingBatchSize <<
            " is set in driver, the valid value range is 1 ~ 1024");
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(eventPollingTimeout == 0 || eventPollingTimeout > NN_NO2000000)) {
        NN_LOG_ERROR("Option 'eventPollingTimeout' is invalid, " << eventPollingTimeout <<
            " is set in driver, the valid value range is 1ms ~ 2000000ms");
        return NN_INVALID_PARAM;
    }
    return NN_OK;
}

NResult ValidateSegOptions(uint32_t mrSendReceiveSegSize, uint32_t mrSendReceiveSegCount)
{
    if (mrSendReceiveSegSize < NN_NO1 || mrSendReceiveSegSize > NET_SGE_MAX_SIZE) {
        NN_LOG_ERROR("Option 'mrSendReceiveSegSize' is invalid, " << mrSendReceiveSegSize <<
            " is set in driver, the valid value range is 1 byte ~ 524288000 byte");
        return NN_INVALID_PARAM;
    }

    if (mrSendReceiveSegCount < NN_NO1 || mrSendReceiveSegCount > NN_NO65535) {
        NN_LOG_ERROR("Option 'mrSendReceiveSegCount' is invalid, " << mrSendReceiveSegCount <<
            " is set in driver, the valid value range is 1 ~ 65535");
        return NN_INVALID_PARAM;
    }
    return NN_OK;
}

NResult ValidateCipherOptions(bool enableTls, UBSHcomTlsVersion tlsVersion, UBSHcomNetCipherSuite cipherSuite)
{
    if (!enableTls) {
        return NN_OK;
    }

    if ((cipherSuite < AES_GCM_128) || (cipherSuite > CHACHA20_POLY1305)) {
        NN_LOG_ERROR("Option 'cipherSuite' is invalid, " << cipherSuite <<
            " is set in driver, the valid value range is AES_GCM_128:" << AES_GCM_128 << " and CHACHA20_POLY1305:" <<
            CHACHA20_POLY1305);
        return NN_INVALID_PARAM;
    }

    if ((tlsVersion != TLS_1_3)) {
        NN_LOG_ERROR("Currently only supports TLS 1.3 version");
        return NN_INVALID_PARAM;
    }

    return NN_OK;
}

NResult ValidateMaxConnectionOptions(uint32_t maxConnectionNum)
{
    if (maxConnectionNum == NN_NO0) {
        NN_LOG_ERROR("Option 'maxConnectionNum' is invalid, " << maxConnectionNum <<
            " is set in driver, the valid value range is > 0");
        return NN_INVALID_PARAM;
    }
    return NN_OK;
}

NResult UBSHcomNetDriverOptions::ValidateCommonOptions()
{
    /* validate params related to heart beat for RDMA, Sock and SHM */
    if (NN_UNLIKELY(ValidateWorkerOptions(mode, workerGroups, workerGroupsCpuSet, lbPolicy, workerThreadPriority) !=
        NN_OK)) {
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(ValidatePollingOptions(pollingBatchSize, eventPollingTimeout) != NN_OK)) {
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(ValidateOobOptions(oobType) != NN_OK)) {
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(ValidateHeartbeatOptions(heartBeatIdleTime, heartBeatProbeTimes, heartBeatProbeInterval) !=
        NN_OK)) {
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(ValidateQueueOptions(qpSendQueueSize, qpReceiveQueueSize, completionQueueDepth) != NN_OK)) {
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(ValidateSegOptions(mrSendReceiveSegSize, mrSendReceiveSegCount) != NN_OK)) {
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(ValidateCipherOptions(enableTls, tlsVersion, cipherSuite) != NN_OK)) {
        return NN_INVALID_PARAM;
    }

    if (!POWER_OF_2(tcpSendBufSize)) {
        tcpSendBufSize = NN_NextPower2(tcpSendBufSize);
    }

    if (!POWER_OF_2(tcpReceiveBufSize)) {
        tcpReceiveBufSize = NN_NextPower2(tcpReceiveBufSize);
    }

    if (!POWER_OF_2(qpSendQueueSize)) {
        qpSendQueueSize = NN_NextPower2(qpSendQueueSize);
    }

    if (!POWER_OF_2(qpReceiveQueueSize)) {
        qpReceiveQueueSize = NN_NextPower2(qpReceiveQueueSize);
    }

    if (NN_UNLIKELY(ValidateMaxConnectionOptions(maxConnectionNum) != NN_OK)) {
        return NN_INVALID_PARAM;
    }

    return NN_OK;
}

std::string UBSHcomNetDriverOptions::NetDeviceIpMask() const
{
    return NN_CHAR_ARRAY_TO_STRING(netDeviceIpMask);
}

std::string UBSHcomNetDriverOptions::NetDeviceIpGroup() const
{
    return NN_CHAR_ARRAY_TO_STRING(netDeviceIpGroup);
}

std::string UBSHcomNetDriverOptions::WorkGroups() const
{
    return NN_CHAR_ARRAY_TO_STRING(workerGroups);
}

std::string UBSHcomNetDriverOptions::WorkerGroupCpus() const
{
    return NN_CHAR_ARRAY_TO_STRING(workerGroupsCpuSet);
}

std::string UBSHcomNetDriverOptions::WorkerGroupThreadPriority() const
{
    return NN_CHAR_ARRAY_TO_STRING(workerGroupsThreadPriority);
}

bool UBSHcomNetDriverOptions::SetNetDeviceIpMask(const std::string &mask)
{
    NN_SET_CHAR_ARRAY_FROM_STRING(netDeviceIpMask, mask);
}

bool UBSHcomNetDriverOptions::SetNetDeviceIpMask(const std::vector<std::string> &mask)
{
    std::string ipMasksStr;
    NetFunc::NN_VecStrToStr(mask, ",", ipMasksStr);
    NN_SET_CHAR_ARRAY_FROM_STRING(netDeviceIpMask, ipMasksStr);
}

bool UBSHcomNetDriverOptions::SetNetDeviceEid(const std::string &eid)
{
    std::string s;
    std::remove_copy(eid.begin(), eid.end(), std::back_inserter(s), ':');
    if (s.length() != NN_NO32) {
        NN_LOG_ERROR("Ensure the eid is of 128b size after erasing the colon sign");
        return false;
    }

    return HexStringToBuff(s, NN_NO16, netDeviceEid);
}

bool UBSHcomNetDriverOptions::SetNetDeviceIpGroup(const std::string &ipGroup)
{
    NN_SET_CHAR_ARRAY_FROM_STRING(netDeviceIpGroup, ipGroup);
}

bool UBSHcomNetDriverOptions::SetNetDeviceIpGroup(const std::vector<std::string> &ipGroup)
{
    std::string ipGroupStr;
    NetFunc::NN_VecStrToStr(ipGroup, ";", ipGroupStr);
    NN_SET_CHAR_ARRAY_FROM_STRING(netDeviceIpGroup, ipGroupStr);
}

bool UBSHcomNetDriverOptions::SetWorkerGroups(const std::string &groups)
{
    NN_SET_CHAR_ARRAY_FROM_STRING(workerGroups, groups);
}

bool UBSHcomNetDriverOptions::SetWorkerGroupsCpuSet(const std::string &value)
{
    NN_SET_CHAR_ARRAY_FROM_STRING(workerGroupsCpuSet, value);
}

bool UBSHcomNetDriverOptions::SetWorkerGroupThreadPriority(const std::string &value)
{
    NN_SET_CHAR_ARRAY_FROM_STRING(workerGroupsThreadPriority, value);
}

std::string UBSHcomNetDriverOptions::ToString() const
{
    std::ostringstream oss;
    oss << "UBSHcomNetDriverOptions mode: " << static_cast<int>(mode) << ", send/receive-mr-seg-count: " <<
        mrSendReceiveSegCount << ", send/receive-mr-seg-size: " << mrSendReceiveSegSize << ", device-mask: " <<
        NetDeviceIpMask() << ", cq-size " << completionQueueDepth << ", max-post-send: " << maxPostSendCountPerQP <<
        ", pre-post-receive-count: " << prePostReceiveSizePerQP << ", polling-batch-size: " << pollingBatchSize <<
        ", qp-send-queue-size: " << qpSendQueueSize << ", qp-receive-queue-size: " << qpReceiveQueueSize <<
        ", worker-groups: " << WorkGroups() << ", worker-groups-cpu-set: " << WorkerGroupCpus() <<
        ", start-workers: " << dontStartWorkers << ", tls-enabled: " << enableTls << ", oob-type: " <<
        UBSHcomNetDriverOobTypeToString(oobType) << ", lb-policy: " << UBSHcomNetDriverLBPolicyToString(lbPolicy);
    return oss.str();
}

std::string UBSHcomNetDriverOptions::ToStringForSock() const
{
    std::ostringstream oss;
    oss << "UBSHcomNetDriverOptions mode: " << static_cast<int>(mode) << ", send/receive-mr-seg-count: " <<
        mrSendReceiveSegCount << ", send/receive-mr-seg-size: " << mrSendReceiveSegSize << ", device-mask: " <<
        NetDeviceIpMask() << ", cq-size " << completionQueueDepth << ", max-post-send: " << maxPostSendCountPerQP <<
        ", pre-post-receive-count: " << prePostReceiveSizePerQP << ", polling-batch-size: " << pollingBatchSize <<
        ", qp-send-queue-size: " << qpSendQueueSize << ", qp-receive-queue-size: " << qpReceiveQueueSize <<
        ", worker-groups: " << WorkGroups() << ", worker-groups-cpu-set: " << WorkerGroupCpus() <<
        ", start-workers: " << dontStartWorkers << ", tls-enabled: " << enableTls << ", oob-type: " <<
        UBSHcomNetDriverOobTypeToString(oobType) << ", lb-policy: " << UBSHcomNetDriverLBPolicyToString(lbPolicy) <<
        ", tcp-keepalive-idle-time: " << heartBeatIdleTime << " seconds, tcp-keepalive-probe-times: " <<
        heartBeatProbeTimes << ", tcp-keepalive-probe-interval: " << heartBeatProbeInterval <<
        " seconds, tcp-send-buffer-size: " << tcpSendBufSize << ", tcp-receive-buffer-size: " << tcpReceiveBufSize;
    return oss.str();
}

void UnParseWorkerGroups(const std::vector<UBSHcomWorkerGroupInfo> &workerGroups, std::string &strRes)
{
    strRes.clear();
    for (const auto &workerGroup : workerGroups) {
        if (NN_UNLIKELY(strRes.empty())) {
            strRes += std::to_string(workerGroup.threadCount);
        } else {
            strRes += ("," + std::to_string(workerGroup.threadCount));
        }
    }
}

void UnParseWorkerGroupsCpus(const std::vector<UBSHcomWorkerGroupInfo> &workerGroups, std::string &strRes)
{
    strRes.clear();
    for (const auto &workerGroup : workerGroups) {
        std::string item = "na";
        if (NN_UNLIKELY(workerGroup.cpuIdsRange.first != UINT32_MAX)) {
            item = std::to_string(workerGroup.cpuIdsRange.first) + "-"
                + std::to_string(workerGroup.cpuIdsRange.second);
        }
        if (NN_UNLIKELY(strRes.empty())) {
            strRes += item;
        } else {
            strRes += ("," + item);
        }
    }
}

bool UBSHcomNetDriverOptions::SetWorkerGroupsInfo(const std::vector<UBSHcomWorkerGroupInfo> &workerGroupInfos)
{
    if (NN_UNLIKELY(workerGroupInfos.empty())) {
        NN_LOG_ERROR("SetWorkerGroupsInfo failed, workerGroups is empty");
        return false;
    }
    workerThreadPriority = workerGroupInfos[0].threadPriority;
    std::string wGsStr;
    std::string wGsCpuSetStr;
    UnParseWorkerGroups(workerGroupInfos, wGsStr);
    UnParseWorkerGroupsCpus(workerGroupInfos, wGsCpuSetStr);
    NN_SET_CHAR_ARRAY_FROM_STRING_VOID(workerGroups, wGsStr);
    NN_SET_CHAR_ARRAY_FROM_STRING_VOID(workerGroupsCpuSet, wGsCpuSetStr);
    return true;
}

}
}
