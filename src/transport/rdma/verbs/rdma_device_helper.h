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

#ifndef HCOM_RDMA_DEVICE_HELPER_H
#define HCOM_RDMA_DEVICE_HELPER_H
#ifdef RDMA_BUILD_ENABLED

#include "rdma_common.h"

namespace ock {
namespace hcom {

struct RDMADeviceSimpleInfo {
    uint16_t devIndex = 0;
    char devName[IBV_SYSFS_NAME_MAX] {};
    bool active = false;
    UBSHcomNetDriverDeviceInfo deviceInfo;
};

enum RDMARoCEVersion {
    RoCE_UNKNOWN = 0,
    RoCE_V1 = 1,
    RoCE_V15 = 2,
    RoCE_V2 = 3,
};

struct RDMAGId {
    uint16_t devIndex = 0;
    uint16_t gid = 0;
    union ibv_gid ibvGid {};
    RDMARoCEVersion RoCEVersion = RDMARoCEVersion::RoCE_UNKNOWN;
    uint8_t bandWidth = 0;
} __attribute__((packed));

class RDMADeviceHelper {
public:
    /*
     * @brief, loop all device, and gid table
     */
    static RResult Initialize();
    static void UnInitialize();
    static RResult Update();

    static RResult GetDeviceCount(uint16_t &deviceCount, std::vector<RDMADeviceSimpleInfo> &enabledDevices);

    static RResult GetDeviceByIp(const std::string &ip, RDMAGId &gid);

    static const char *RoCEVersionToStr(RDMARoCEVersion v);
    static RDMARoCEVersion StrToRoCEVersion(const std::string &value);

    static std::string DeviceInfo();

    static RResult GetEnableDeviceCount(std::string ipMask, uint16_t &enableDevCount,
        std::vector<std::string> &enableIps, std::string ipGroup);

private:
    static RResult DoInitialize();
    static RResult DoUpdate();
    static void GetGidVec(ibv_context *context, const std::string &devName, uint16_t devIndex, uint8_t bandWidth,
        uint32_t gidTableLen, std::vector<RDMAGId> &outGidVec);

    static RResult GetIfAddressByIp(const std::string &ip, struct sockaddr_in &address);
    static RResult GetDeviceByAddress(const std::string &ip, struct sockaddr_in &address, RDMAGId &gid);

private:
    static std::unordered_map<uint16_t, RDMADeviceSimpleInfo> G_RDMADevMap;
    static std::unordered_map<std::string, std::vector<RDMAGId>> G_RDMADevGidTable;
    static std::mutex G_Mutex;
    static bool G_Inited;

    static uint32_t PORT_NUMBER;
};
}
}
#endif
#endif // HCOM_RDMA_DEVICE_HELPER_H