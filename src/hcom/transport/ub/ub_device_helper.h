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

#ifndef HCOM_UB_WRAPPER_DEVICE_HELPER_H
#define HCOM_UB_WRAPPER_DEVICE_HELPER_H
#ifdef UB_BUILD_ENABLED

#include "ub_common.h"

namespace ock {
namespace hcom {

struct UBDeviceSimpleInfo {
    uint16_t devIndex = 0;
    char devName[URMA_MAX_NAME]{};
    bool active = false;
    UBSHcomNetDriverDeviceInfo deviceInfo;
};

struct UBEId {
    uint16_t devIndex = 0;
    uint16_t eidIndex = 0;
    urma_eid_t urmaEid;
    uint8_t bandWidth = 0;
} __attribute__((packed));

class UBDeviceHelper {
public:
    static UResult Initialize(urma_device_attr_t *devAttr, uint32_t &bandWidth);
    static void UnInitialize();
    static UResult Update();

    static UResult GetDeviceCount(uint16_t &deviceCount, std::vector<UBDeviceSimpleInfo> &enabledDevices);

    static UResult GetDeviceByIp(const std::string &ip, UBEId &gid);
    static UResult GetDeviceByEid(const uint8_t eid[], UBEId &gid);
    static UResult GetDeviceByName(const char name[], uint8_t len, UBEId &gid);

    static uint32_t GetPortNumber();

    static std::string DeviceInfo();

    static UResult GetEnableDeviceCount(std::string ipMask, uint16_t &enableDevCount,
        std::vector<std::string> &enableIps, std::string ipGroup);

private:
    static UResult DoInitialize(urma_device_attr_t *devAttr, uint32_t &bandWidth);
    static UResult DoUpdate(urma_device_attr_t *devAttr, uint32_t &bandWidth);
    static void GetEidVec(const std::string &devName, uint16_t devIndex, uint32_t eidCnt, urma_eid_info_t *eidInfoList,
        std::vector<UBEId> &outGidVec, uint8_t bandWidth);

    static UResult GetIfAddressByIp(const std::string &ip, struct sockaddr_in &address);
    static UResult GetDeviceByAddress(const std::string &ip, struct sockaddr_in &address, UBEId &gid);

private:
    static std::unordered_map<uint16_t, UBDeviceSimpleInfo> G_UBDevMap;
    static std::unordered_map<std::string, std::vector<UBEId>> G_UBDevEidTable;
    static std::mutex G_Mutex;
    static uint32_t G_InitRef;

    static uint32_t PORT_NUMBER;
    static std::unordered_map<urma_speed_t, uint8_t> G_UBDevBWTable;
};
}
}
#endif
#endif // HCOM_UB_WRAPPER_DEVICE_HELPER_H