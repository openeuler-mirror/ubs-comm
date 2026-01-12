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
#ifdef UB_BUILD_ENABLED

#include "ub_device_helper.h"

namespace ock {
namespace hcom {


uint32_t UBDeviceHelper::G_InitRef = 0;
std::unordered_map<uint16_t, UBDeviceSimpleInfo> UBDeviceHelper::G_UBDevMap;
std::unordered_map<std::string, std::vector<UBEId>> UBDeviceHelper::G_UBDevEidTable;
std::unordered_map<urma_speed_t, uint8_t> UBDeviceHelper::G_UBDevBWTable;
std::mutex UBDeviceHelper::G_Mutex;
uint32_t UBDeviceHelper::PORT_NUMBER = 1;

UResult UBDeviceHelper::Initialize()
{
    UResult ret = UB_OK;
    std::lock_guard<std::mutex> guard(G_Mutex);
    if (G_InitRef != 0) {
        // 第二次进来直接加引用计数，防止mUBContext析构的时候调用UnInitialize时把资源直接释放
        G_InitRef++;
        return ret;
    }
    ret = DoInitialize();
    return ret;
}

void UBDeviceHelper::UnInitialize()
{
    std::lock_guard<std::mutex> guard(G_Mutex);
    G_InitRef--;
    if (G_InitRef != 0) {
        return;
    }
    // HcomUrma::Uninit() 每个进程只能调用一次，防止一个进程多个service多次调用
    HcomUrma::Uninit();
    G_UBDevMap.clear();
    G_UBDevEidTable.clear();
    G_UBDevBWTable.clear();
}

UResult UBDeviceHelper::DoInitialize()
{
    // 后续HCOM重构时重新定义此处数值换算，目前为了不修改头文件中uint8_t bandWidth(范围0~2555)的定义,只做大致比例换算。
    G_UBDevBWTable = { { URMA_SP_10M, 1 },    { URMA_SP_100M, 1 },  { URMA_SP_1G, 1 },     { URMA_SP_2_5G, 3 },
        { URMA_SP_5G, 5 },     { URMA_SP_10G, 10 },  { URMA_SP_14G, 14 },   { URMA_SP_25G, 25 },
        { URMA_SP_40G, 40 },   { URMA_SP_50G, 50 },  { URMA_SP_100G, 100 }, { URMA_SP_200G, 200 },
        { URMA_SP_400G, 255 }, { URMA_SP_800G, 255 } };
    auto ret = DoUpdate();
    if (NN_UNLIKELY(ret != UB_OK)) {
        G_UBDevBWTable.clear();
        return ret;
    }
    // 第一次成功DoInitialize增加引用计数
    G_InitRef++;
    return UB_OK;
}

UResult UBDeviceHelper::DoUpdate()
{
    UResult ret = UB_OK;
    bool isFindDevice = false;
    urma_init_attr_t initAttr{};
    ret = HcomUrma::Init(&initAttr);
    if (ret != URMA_SUCCESS && ret != URMA_EEXIST) {
        NN_LOG_ERROR("Failed to initialize urma environment");
        return ret;
    }
    G_UBDevMap.clear();
    G_UBDevEidTable.clear();

    urma_device_t **devList = nullptr;
    int devCount = 0;
    devList = HcomUrma::GetDeviceList(&devCount);
    NN_LOG_TRACE_INFO("UB Device count:" << devCount);
    if (devList == nullptr) {
        NN_LOG_ERROR("Failed to call get urma device list, errno " << errno);
        return UB_DEVICE_FAILED_OPEN;
    }
    auto guard = MakeScopeExit([&devList]() { HcomUrma::FreeDeviceList(devList); });
    G_UBDevMap.reserve(devCount);
    G_UBDevEidTable.reserve(devCount);

    urma_device_attr_t *devAttr = reinterpret_cast<urma_device_attr_t *>(malloc(sizeof(urma_device_attr_t)));
    if (devAttr == nullptr) {
        NN_LOG_ERROR("Failed to malloc get urma device attr.");
        return UB_NEW_OBJECT_FAILED;
    }
    auto guard1 = MakeScopeExit([&devAttr]() { free(devAttr); });
    for (int i = 0; i < devCount; i++) {
        if (devList[i] == nullptr) { // should not happen
            NN_LOG_TRACE_INFO("UB Device " << i << " is null");
            continue;
        }

        UBDeviceSimpleInfo info;
        info.devIndex = i;
        if (NN_UNLIKELY(strcpy_s(info.devName, URMA_MAX_NAME, reinterpret_cast<const char *>(devList[i]->name)) !=
            UB_OK)) {
            NN_LOG_ERROR("Failed to copy device name when initializing device");
            return UB_PARAM_INVALID;
        }
        NN_LOG_TRACE_INFO("UB Device " << i << " name " << devList[i]->name);

        urma_context_t *ctx = HcomUrma::CreateContext(devList[i], 0);
        if (ctx == nullptr) {
            NN_LOG_WARN("Unable to create urma context");
            continue;
        }
        uint32_t eidCnt = 0;
        urma_eid_info_t *eidInfoList = HcomUrma::GetEidList(devList[i], &eidCnt);
        if (eidInfoList == nullptr) {
            NN_LOG_ERROR("Failed to get eid list");
            HcomUrma::DeleteContext(ctx);
            return UB_PARAM_INVALID;
        }

        // Query and process device info
        if ((ret = HcomUrma::QueryDevice(devList[i], devAttr)) != 0) {
            NN_LOG_ERROR("Failed to query urma device");
            HcomUrma::FreeEidList(eidInfoList);
            HcomUrma::DeleteContext(ctx);
            return ret;
        }

        info.active = devAttr->port_attr[0].state == URMA_PORT_ACTIVE;
        auto it = G_UBDevBWTable.find(devAttr->port_attr[0].active_speed);
        if (it == G_UBDevBWTable.end()) {
            NN_LOG_ERROR("UB failed to query urma device bandwidth.");
            HcomUrma::FreeEidList(eidInfoList);
            HcomUrma::DeleteContext(ctx);
            return UB_PARAM_INVALID;
        }

        uint32_t bandWidth = it->second;
        std::vector<UBEId> eidVec;
        eidVec.reserve(eidCnt);
        GetEidVec(info.devName, i, eidCnt, eidInfoList, eidVec, bandWidth);

        info.deviceInfo.maxSge = std::min(devAttr->dev_cap.max_jfs_sge, devAttr->dev_cap.max_jfr_sge);

        G_UBDevMap.emplace(i, info);
        G_UBDevEidTable.emplace(info.devName, eidVec);
        isFindDevice = true;
        HcomUrma::FreeEidList(eidInfoList);
        HcomUrma::DeleteContext(ctx);
    }
    if (!isFindDevice) {
        NN_LOG_ERROR("Failed to get urma device.");
        return UB_PARAM_INVALID;
    }
    return UB_OK;
}

UResult UBDeviceHelper::Update()
{
    std::lock_guard<std::mutex> guard(G_Mutex);
    return DoUpdate();
}

void UBDeviceHelper::GetEidVec(const std::string &devName, uint16_t devIndex, uint32_t eidCnt,
    urma_eid_info_t *eidInfoList, std::vector<UBEId> &outGidVec, uint8_t bandWidth)
{
    UBEId eid{};
    for (uint32_t i = 0; i < eidCnt; i++) {
        if (eidInfoList[i].eid.in6.interface_id == 0) {
            continue;
        }
        eid.devIndex = devIndex;
        eid.eidIndex = eidInfoList[i].eid_index;
        eid.urmaEid = eidInfoList[i].eid;
        eid.bandWidth = bandWidth;
        outGidVec.push_back(eid);
    }
}

UResult UBDeviceHelper::GetDeviceCount(uint16_t &deviceCount, std::vector<UBDeviceSimpleInfo> &enabledDevices)
{
    UResult ret = UB_OK;
    if ((ret = Initialize()) != UB_OK) {
        return ret;
    }

    {
        std::lock_guard<std::mutex> guard(G_Mutex);
        deviceCount = G_UBDevMap.size();
        for (auto &item : G_UBDevMap) {
            if (item.second.active) {
                enabledDevices.push_back(item.second);
            }
        }
    }

    return UB_OK;
}

UResult UBDeviceHelper::GetEnableDeviceCount(std::string ipMask, uint16_t &enableDevCount,
    std::vector<std::string> &enableIps, std::string ipGroup)
{
    UResult result = UB_OK;
    std::vector<std::string> matchIps;
    // filter ip by mask
    NetFunc::NN_SplitStr(ipGroup, ";", matchIps);
    if (matchIps.empty()) {
        std::vector<std::string> filters;
        NetFunc::NN_SplitStr(ipMask, ",", filters);
        if (filters.empty()) {
            NN_LOG_ERROR("[UB] Invalid ip mask '" << ipMask << "' by set, example '192.168.0.0/24'");
            return NN_INVALID_IP;
        }
        for (auto &mask : filters) {
            result = FilterIp(mask, matchIps);
        }
        if (matchIps.empty()) {
            NN_LOG_ERROR("[UB] No matched ip found with ipGroup or ipMask.");
            return UB_DEVICE_NO_IP_MATCHED;
        }
    }
    // init urma devices
    if ((result = Initialize()) != 0) {
        NN_LOG_ERROR("[UB] Failed to init devices");
        return result;
    }

    NN_LOG_INFO(DeviceInfo());

    uint16_t enableCount = 0;
    std::vector<std::string> findIps;
    // choose the  matched ip and port active
    for (uint16_t i = 0; i < static_cast<uint16_t>(matchIps.size()); ++i) {
        UBEId tmpEid{};
        if ((GetDeviceByIp(matchIps[i], tmpEid)) != 0) {
            NN_LOG_WARN("[UB] Failed to get device by ip " << matchIps[i]);
            continue;
        }
        // active or not
        if (G_UBDevMap[tmpEid.devIndex].active) {
            enableCount++;
            findIps.emplace_back(matchIps[i]);
        }
        NN_LOG_DEBUG("gid found devIndex " << tmpEid.devIndex << ", gidIndex " << tmpEid.eidIndex);
    }

    if (findIps.empty()) {
        NN_LOG_ERROR("[UB] NoMatched Device found with ip");
        return UB_DEVICE_NO_IP_MATCHED;
    }
    enableDevCount = enableCount;
    enableIps = findIps;
    return result;
}

UResult UBDeviceHelper::GetDeviceByIp(const std::string &ip, UBEId &gid)
{
    UResult ret = UB_OK;
    struct sockaddr_in address {};
    if ((ret = GetIfAddressByIp(ip, address)) != UB_OK) {
        return ret;
    }

    return GetDeviceByAddress(ip, address, gid);
}

UResult UBDeviceHelper::GetDeviceByEid(const uint8_t eid[], UBEId &gid)
{
    std::lock_guard<std::mutex> guard(G_Mutex);
    for (auto &item : G_UBDevEidTable) {
        for (auto &gItem : item.second) {
            if (std::memcmp(eid, gItem.urmaEid.raw, URMA_EID_SIZE) == 0) {
                gid = gItem;
                return UB_OK;
            }
        }
    }

    NN_LOG_ERROR("Failed to get proper gid by eid " << eid);
    return UB_DEVICE_NO_IP_TO_GID_MATCHED;
}

UResult UBDeviceHelper::GetDeviceByName(const char name[], uint8_t len, UBEId &gid)
{
    std::lock_guard<std::mutex> guard(G_Mutex);
    for (auto &item : G_UBDevEidTable) {
        if (item.second.empty()) {
            continue;
        }
        if (strncmp(name, item.first.c_str(), len) == 0) {
            gid = item.second[0];
            return UB_OK;
        }
    }

    NN_LOG_ERROR("Failed to get proper gid by name " << name);
    return UB_DEVICE_NO_IP_TO_GID_MATCHED;
}

UResult UBDeviceHelper::GetIfAddressByIp(const std::string &ip, struct sockaddr_in &address)
{
    struct ifaddrs *addresses = nullptr;
    if (getifaddrs(&addresses) != 0) {
        NN_LOG_ERROR("Failed to get interface addresses");
        return UB_DEVICE_FAILED_GET_IP_ADDRESS;
    }

    char ipStr[INET_ADDRSTRLEN] = {0};
    bool found = false;

    struct ifaddrs *iter = addresses;
    while (iter != nullptr) {
        if (iter->ifa_addr != nullptr && iter->ifa_addr->sa_family == AF_INET) {
            inet_ntop(AF_INET, &((reinterpret_cast<struct sockaddr_in *>(iter->ifa_addr))->sin_addr), ipStr,
                INET_ADDRSTRLEN);
            if (ip == std::string(ipStr)) {
                address = *(reinterpret_cast<struct sockaddr_in *>(iter->ifa_addr));
                found = true;
                break;
            }
        }
        iter = iter->ifa_next;
    }
    freeifaddrs(addresses);

    if (!found) {
        NN_LOG_ERROR("Failed to get interface address for ip " << ip);
        return UB_DEVICE_NO_IP_MATCHED;
    }

    return UB_OK;
}

UResult UBDeviceHelper::GetDeviceByAddress(const std::string &ip, struct sockaddr_in &address, UBEId &eid)
{
    UResult result = UB_OK;
    if ((result = Initialize()) != UB_OK) {
        return result;
    }

    UBEId tmpEid{};
    bool found = false;

    std::lock_guard<std::mutex> lock(G_Mutex);
    for (auto &item : G_UBDevEidTable) {
        for (auto &gItem : item.second) {
            auto devI6Address = reinterpret_cast<struct in6_addr *>(gItem.urmaEid.raw);
            auto targetAddress = address.sin_addr.s_addr;

            auto judge1 = ((devI6Address->s6_addr32[NN_NO0] | devI6Address->s6_addr32[NN_NO1]) |
                (devI6Address->s6_addr32[NN_NO2] ^ htonl(0x0000ffff))) == 0UL;
            /* IPv4 encoded multicast addresses */
            auto judge2 = devI6Address->s6_addr32[NN_NO0] == htonl(0xff0e0000) &&
                ((devI6Address->s6_addr32[NN_NO1] | (devI6Address->s6_addr32[NN_NO2] ^ htonl(0x0000ffff))) == 0UL);
            if (!((judge1 || judge2) && devI6Address->s6_addr32[NN_NO3] == targetAddress)) {
                // doesn't match
                continue;
            }

            // match
            if (!found) { // first found
                tmpEid = gItem;
                found = true;
            } else {
                // found new one then compare the version, higher version is better
                tmpEid = gItem;
            }
        }
    }

    if (!found) {
        NN_LOG_ERROR("Failed to get proper gid by address for ip " << ip);
        return UB_DEVICE_NO_IP_TO_GID_MATCHED;
    }

    eid = tmpEid;
    return UB_OK;
}

std::string UBDeviceHelper::DeviceInfo()
{
    std::ostringstream oss;
    std::lock_guard<std::mutex> guard(G_Mutex);
    if (!G_InitRef) {
        oss << "UBDeviceHelper has not been initialized";
        return oss.str();
    }

    // dump device info
    oss << "UBDeviceHelper device info, devices: count " << G_UBDevMap.size() << ", ";
    for (auto &item : G_UBDevMap) {
        oss << "[" << item.second.devIndex << "," << item.second.devName << "," << item.second.active << "] ";
    }

    oss << ", gidTable: count " << G_UBDevEidTable.size() << ", ";
    for (auto &item : G_UBDevEidTable) {
        oss << "[deviceName " << item.first << ", ";
        for (auto &eid : item.second) {
            oss << "[" << eid.devIndex << "," << eid.eidIndex << "] ";
        }
        oss << "] ";
    }

    return oss.str();
}

uint32_t UBDeviceHelper::GetPortNumber()
{
    return PORT_NUMBER;
}
}
}
#endif