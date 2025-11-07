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


#ifdef RDMA_BUILD_ENABLED

#include "rdma_device_helper.h"

namespace ock {
namespace hcom {

static const char* RDMARoCEVersionStrTable[] = {
    "Unknown",
    "IB/RoCE v1",
    "RoCE v1.5",
    "RoCE v2",
};

bool RDMADeviceHelper::G_Inited = false;
std::unordered_map<uint16_t, RDMADeviceSimpleInfo> RDMADeviceHelper::G_RDMADevMap;
std::unordered_map<std::string, std::vector<RDMAGId>> RDMADeviceHelper::G_RDMADevGidTable;
std::mutex RDMADeviceHelper::G_Mutex;
uint32_t RDMADeviceHelper::PORT_NUMBER = 1;

RResult RDMADeviceHelper::Initialize()
{
    if (!G_Inited) {
        std::lock_guard<std::mutex> guard(G_Mutex);
        if (!G_Inited) {
            // double check
            return DoInitialize();
        }
    }

    return RR_OK;
}

void RDMADeviceHelper::UnInitialize()
{
    std::lock_guard<std::mutex> guard(G_Mutex);
    G_RDMADevMap.clear();
    G_RDMADevGidTable.clear();
    G_Inited = false;
}

RResult RDMADeviceHelper::DoInitialize()
{
    auto ret = DoUpdate();
    if (NN_UNLIKELY(ret != RR_OK)) {
        return ret;
    }

    G_Inited = true;
    return RR_OK;
}

RResult RDMADeviceHelper::DoUpdate()
{
    HcomIbv::ForkInit();
    G_RDMADevMap.clear();
    G_RDMADevGidTable.clear();

    struct ibv_device **devList = nullptr;
    int devCount = 0;
    devList = HcomIbv::GetDevList(&devCount);

    NN_LOG_TRACE_INFO("RDMA Device count:" << devCount);
    if (devList == nullptr) {
        NN_LOG_ERROR("Failed to call get ibv device list, errno " << errno);
        return RR_DEVICE_FAILED_OPEN;
    }
    auto guard = MakeScopeExit([&devList]() { HcomIbv::FreeDevList(devList); });
    G_RDMADevMap.reserve(devCount);
    G_RDMADevGidTable.reserve(devCount);

    struct ibv_port_attr portAttr {};
    for (int i = 0; i < devCount; i++) {
        if (devList[i] == nullptr) { // should not happen
            NN_LOG_WARN("RDMA Device " << i << " is null");
            continue;
        }

        RDMADeviceSimpleInfo info;
        info.devIndex = i;
        if (NN_UNLIKELY(strcpy_s(info.devName, IBV_SYSFS_NAME_MAX, reinterpret_cast<const char *>(devList[i]->name)) !=
            RR_OK)) {
            NN_LOG_ERROR("Failed to copy devName in initializing device");
            return RR_PARAM_INVALID;
        }
        NN_LOG_TRACE_INFO("RDMA Device " << i << " name " << devList[i]->name);
        std::vector<RDMAGId> gidVec;
        gidVec.reserve(NN_NO16);

        auto ctx = HcomIbv::OpenDevice(devList[i]);
        if (ctx != nullptr && HcomIbv::QueryPort(ctx, PORT_NUMBER, &portAttr) == 0) {
            info.active = (portAttr.state == IBV_PORT_ACTIVE);
            GetGidVec(ctx, info.devName, i, portAttr.active_speed, portAttr.gid_tbl_len, gidVec);
        }

        struct ibv_device_attr attr {};
        if (info.active && HcomIbv::QueryDevice(ctx, &attr) == 0) {
            info.deviceInfo.maxSge = attr.max_sge;
        }

        G_RDMADevMap.emplace(i, info);
        G_RDMADevGidTable.emplace(info.devName, gidVec);
        if (ctx != nullptr) {
            HcomIbv::CloseDev(ctx);
        }
    }
    return RR_OK;
}

RResult RDMADeviceHelper::Update()
{
    std::lock_guard<std::mutex> guard(G_Mutex);
    return DoUpdate();
}

void RDMADeviceHelper::GetGidVec(ibv_context *context, const std::string &devName, uint16_t devIndex, uint8_t bandWidth,
    uint32_t gidTableLen, std::vector<RDMAGId> &outGidVec)
{
    if (context == nullptr) {
        return;
    }

    union ibv_gid tmpIbvGid {};
    std::string RoCEVersion;
    RDMAGId gid {};
    for (uint32_t i = 0; i < gidTableLen; i++) {
        if (HcomIbv::QueryGid(context, PORT_NUMBER, i, &tmpIbvGid) != 0) {
            continue;
        }

        if (tmpIbvGid.global.interface_id == 0) {
            continue;
        }

        if (ReadRoCEVersionFromFile(devName, PORT_NUMBER, i, RoCEVersion) != RR_OK) {
            continue;
        }

        gid.devIndex = devIndex;
        gid.gid = i;
        gid.ibvGid = tmpIbvGid;
        gid.RoCEVersion = StrToRoCEVersion(RoCEVersion);
        gid.bandWidth = bandWidth;
        outGidVec.push_back(gid);
    }
}

RResult RDMADeviceHelper::GetDeviceCount(uint16_t &deviceCount, std::vector<RDMADeviceSimpleInfo> &enabledDevices)
{
    RResult result = RR_OK;
    if ((result = Initialize()) != RR_OK) {
        return result;
    }

    {
        std::lock_guard<std::mutex> guard(G_Mutex);
        deviceCount = G_RDMADevMap.size();
        for (auto &item : G_RDMADevMap) {
            if (item.second.active) {
                enabledDevices.push_back(item.second);
            }
        }
    }

    return RR_OK;
}

RResult RDMADeviceHelper::GetEnableDeviceCount(std::string ipMask, uint16_t &enableDevCount,
    std::vector<std::string> &enableIps, std::string ipGroup)
{
    /* ipMask and ipGroup may be null */
    if (ipMask.size() > NN_NO256 || ipGroup.size() > NN_NO1024) {
        NN_LOG_ERROR("[RDMA] ip mask size cannot exceed 256, ip group size cannot exceed 1024. ");
        return NN_INVALID_IP;
    }
    RResult result = RR_OK;
    std::vector<std::string> matchIps;
    // filter ip by mask
    NetFunc::NN_SplitStr(ipGroup, ";", matchIps);
    if (matchIps.empty()) {
        std::vector<std::string> filters;
        NetFunc::NN_SplitStr(ipMask, ",", filters);
        if (filters.empty()) {
            NN_LOG_ERROR("[RDMA] Invalid ip mask '" << ipMask << "' by set, example '192.168.0.0/24'");
            return NN_INVALID_IP;
        }
        for (auto &mask : filters) {
            result = FilterIp(mask, matchIps);
        }
        if (matchIps.empty()) {
            NN_LOG_ERROR("[RDMA] No matched ip found with ipGroup or ipMask.");
            return NN_INVALID_IP;
        }
    }
    // init RoCE devices
    if ((result = Initialize()) != 0) {
        NN_LOG_ERROR("[RDMA] Failed to init devices");
        return result;
    }

    NN_LOG_INFO(DeviceInfo());

    uint16_t enableCount = 0;
    std::vector<std::string> findIps;
    // choose the  matched ip and port active
    for (uint16_t i = 0; i < static_cast<uint16_t>(matchIps.size()); ++i) {
        RDMAGId tmpGid{};
        if ((GetDeviceByIp(matchIps[i], tmpGid)) != 0) {
            NN_LOG_WARN("[RDMA] Unable to get device by ip " << matchIps[i]);
            continue;
        }
        // active or not
        if (G_RDMADevMap[tmpGid.devIndex].active) {
            enableCount++;
            findIps.emplace_back(matchIps[i]);
        }
        NN_LOG_DEBUG("gid found devIndex " << tmpGid.devIndex << ", gidIndex " << tmpGid.gid << ", RoCEVersion " <<
            RoCEVersionToStr(tmpGid.RoCEVersion));
    }
    enableDevCount = enableCount;
    enableIps = findIps;
    return result;
}

RResult RDMADeviceHelper::GetDeviceByIp(const std::string &ip, RDMAGId &gid)
{
    RResult result = RR_OK;
    struct sockaddr_in address {};
    if ((result = GetIfAddressByIp(ip, address)) != RR_OK) {
        return result;
    }

    return GetDeviceByAddress(ip, address, gid);
}

RResult RDMADeviceHelper::GetIfAddressByIp(const std::string &ip, struct sockaddr_in &address)
{
    struct ifaddrs *addresses = nullptr;
    if (getifaddrs(&addresses) != 0) {
        NN_LOG_ERROR("Failed to get interface addresses");
        return RR_DEVICE_FAILED_GET_IF_ADDRESS;
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
        return RR_DEVICE_NO_IF_MATCHED;
    }

    return RR_OK;
}


RResult RDMADeviceHelper::GetDeviceByAddress(const std::string &ip, struct sockaddr_in &address, RDMAGId &gid)
{
    RResult result = RR_OK;
    if ((result = Initialize()) != RR_OK) {
        return result;
    }

    RDMAGId tmpGid {};
    bool found = false;

    std::lock_guard<std::mutex> lock(G_Mutex);
    for (auto &item : G_RDMADevGidTable) {
        for (auto &gItem : item.second) {
            auto devI6Address = reinterpret_cast<struct in6_addr *>(gItem.ibvGid.raw);
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
                tmpGid = gItem;
                found = true;
            } else if (gItem.RoCEVersion > tmpGid.RoCEVersion) {
                // found new one then compare the version, higher version is better
                tmpGid = gItem;
            }
        }
    }

    if (!found) {
        NN_LOG_ERROR("Failed to get proper gid by address for ip " << ip);
        return RR_DEVICE_NO_IF_TO_GID_MATCHED;
    }

    gid = tmpGid;
    return RR_OK;
}

RDMARoCEVersion RDMADeviceHelper::StrToRoCEVersion(const std::string &value)
{
    if (value == "IB/RoCE v1") {
        return RoCE_V1;
    } else if (value == "RoCE v2") {
        return RoCE_V2;
    }

    // rare case
    if (value.length() > 1 && value.at(value.length() - 1) == '5') {
        return RoCE_V15;
    }

    return RoCE_UNKNOWN;
}

const char *RDMADeviceHelper::RoCEVersionToStr(RDMARoCEVersion v)
{
    return RDMARoCEVersionStrTable[v];
}

std::string RDMADeviceHelper::DeviceInfo()
{
    std::ostringstream oss;
    std::lock_guard<std::mutex> guard(G_Mutex);
    if (!G_Inited) {
        oss << "RDMADeviceHelper has not been initialized";
        return oss.str();
    }

    // dump device info
    oss << "RDMADeviceHelper device info, devices: count " << G_RDMADevMap.size() << ", ";
    for (auto &item : G_RDMADevMap) {
        oss << "[" << item.second.devIndex << "," << item.second.devName << "," << item.second.active << "] ";
    }

    oss << ", gidTable: count " << G_RDMADevGidTable.size() << ", ";
    for (auto &item : G_RDMADevGidTable) {
        oss << "[deviceName " << item.first << ", ";
        for (auto &gid : item.second) {
            oss << "[" << gid.devIndex << "," << gid.gid << "," << RoCEVersionToStr(gid.RoCEVersion) << "] ";
        }
        oss << "] ";
    }

    return oss.str();
}
}
}
#endif