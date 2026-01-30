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
std::unordered_map<urma_speed_t, uint8_t> UBDeviceHelper::G_UBDevBWTable;
std::mutex UBDeviceHelper::G_Mutex;
uint32_t UBDeviceHelper::PORT_NUMBER = 1;

UResult UBDeviceHelper::Initialize(urma_device_attr_t *devAttr, urma_context_t *&ctx, UBEId &eid)
{
    UResult ret = UB_OK;
    std::lock_guard<std::mutex> guard(G_Mutex);
    G_InitRef++;
    if (G_InitRef != 1) {
        // 第二次进来直接加引用计数，防止mUBContext析构的时候调用UnInitialize时把资源直接释放
        return ret;
    }
    ret = DoInitialize(devAttr, ctx, eid);
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
    G_UBDevBWTable.clear();
}

UResult UBDeviceHelper::DoInitialize(urma_device_attr_t *devAttr, urma_context_t *&ctx, UBEId &eid)
{
    // 后续HCOM重构时重新定义此处数值换算，目前为了不修改头文件中uint8_t bandWidth(范围0~2555)的定义,只做大致比例换算。
    G_UBDevBWTable = { { URMA_SP_10M, 1 },    { URMA_SP_100M, 1 },  { URMA_SP_1G, 1 },     { URMA_SP_2_5G, 3 },
        { URMA_SP_5G, 5 },     { URMA_SP_10G, 10 },  { URMA_SP_14G, 14 },   { URMA_SP_25G, 25 },
        { URMA_SP_40G, 40 },   { URMA_SP_50G, 50 },  { URMA_SP_100G, 100 }, { URMA_SP_200G, 200 },
        { URMA_SP_400G, 255 }, { URMA_SP_800G, 255 } };
    auto ret = DoUpdate(devAttr, ctx, eid);
    if (NN_UNLIKELY(ret != UB_OK)) {
        G_UBDevBWTable.clear();
        return ret;
    }
    return UB_OK;
}

int UBDeviceHelper::CompareName(const char name[], size_t nameLen, urma_device_t **devList, int devCount)
{
    for (int i = 0; i < devCount; i++) {
        if (devList[i] == nullptr) { // should not happen
            NN_LOG_TRACE_INFO("UB Device " << i << " is null");
            continue;
        }

        if (strncmp(reinterpret_cast<const char *>(devList[i]->name), name, nameLen) == 0) {
            return i;
        }
    }
    return -1;
}

UResult UBDeviceHelper::DoUpdate(urma_device_attr_t *devAttr, urma_context_t *&ctx, UBEId &eid)
{
    UResult ret = UB_OK;
    bool isFindDevice = false;
    urma_init_attr_t initAttr{};
    ret = HcomUrma::Init(&initAttr);
    if (ret != URMA_SUCCESS && ret != URMA_EEXIST) {
        NN_LOG_ERROR("Failed to initialize urma environment");
        return ret;
    }

    urma_device_t **devList = nullptr;
    int devCount = 0;
    devList = HcomUrma::GetDeviceList(&devCount);
    NN_LOG_TRACE_INFO("UB Device count:" << devCount);
    if (devList == nullptr) {
        NN_LOG_ERROR("Failed to call get urma device list, errno " << errno);
        return UB_DEVICE_FAILED_OPEN;
    }
    auto guard = MakeScopeExit([&devList]() { HcomUrma::FreeDeviceList(devList); });
    char name[] = "bonding_dev_0";
    char nameBonding[] = "bonding";
    int devIdx = CompareName(name, sizeof(name), devList, devCount);
    if (devIdx == -1) {
        devIdx = CompareName(nameBonding, sizeof(nameBonding), devList, devCount);
    }
    if (devIdx == -1) {
        NN_LOG_ERROR("Failed to get proper gid by name " << name << ", or name " << nameBonding);
        return UB_DEVICE_FAILED_OPEN;
    }
    
    NN_LOG_INFO("Choosing UB Device " << devIdx << " name " << devList[devIdx]->name);
    uint32_t eidCnt = 0;
    urma_eid_info_t *eidInfoList = HcomUrma::GetEidList(devList[devIdx], &eidCnt);
    if (eidInfoList == nullptr) {
        NN_LOG_ERROR("Failed to get eid list");
        return UB_PARAM_INVALID;
    }
    auto guard2 = MakeScopeExit([&eidInfoList]() { HcomUrma::FreeEidList(eidInfoList); });

    // Query and process device info
    if ((ret = HcomUrma::QueryDevice(devList[devIdx], devAttr)) != 0) {
        NN_LOG_ERROR("Failed to query urma device");
        return ret;
    }

    auto it = G_UBDevBWTable.find(devAttr->port_attr[0].active_speed);
    if (it == G_UBDevBWTable.end()) {
        NN_LOG_ERROR("UB failed to query urma device bandwidth.");
        return UB_PARAM_INVALID;
    }
    uint32_t bw = it->second;

    int eidIndex = 0;
    urma_context_t *tmpCtx = nullptr;
    if ((tmpCtx = HcomUrma::CreateContext(devList[devIdx], eidIndex)) == nullptr) {
        NN_LOG_ERROR("Invalid device index is set for Device " << devList[devIdx]->name << ", errno " << errno);
        return UB_DEVICE_OPEN_FAILED;
    }
    eid.devIndex = devIdx;
    eid.eidIndex = eidInfoList[0].eid_index;
    eid.urmaEid = eidInfoList[0].eid;
    eid.bandWidth = bw;
    ctx = tmpCtx;
    return UB_OK;
}

uint32_t UBDeviceHelper::GetPortNumber()
{
    return PORT_NUMBER;
}
}
}
#endif