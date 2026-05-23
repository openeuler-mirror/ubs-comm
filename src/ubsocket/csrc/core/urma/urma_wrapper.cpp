/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "urma_wrapper.h"

#include <iomanip>

namespace ock {
namespace ubs {
namespace urma {
std::map<std::string, UrmaDevicePtr> UrmaDevice::ALL_DEVICES;
bool UrmaDevice::LOADED = false;
std::mutex UrmaDevice::MUTEX;
std::map<urma_speed_t, UrmaDeviceBandWidth> UrmaDevice::URMA_BANDWIDTHS = {
    {URMA_SP_10M, {"10Mb", 1}},     {URMA_SP_100M, {"100Mb", 1}},   {URMA_SP_1G, {"1Gb", 1}},
    {URMA_SP_2_5G, {"2.5Gb", 5}},   {URMA_SP_5G, {"5Gb", 5}},       {URMA_SP_10G, {"10Gb", 10}},
    {URMA_SP_14G, {"14Gb", 14}},    {URMA_SP_25G, {"15Gb", 25}},    {URMA_SP_40G, {"40Gb", 40}},
    {URMA_SP_50G, {"50Gb", 50}},    {URMA_SP_100G, {"100Gb", 100}}, {URMA_SP_200G, {"200Gb", 200}},
    {URMA_SP_400G, {"400Gb", 255}}, {URMA_SP_800G, {"800Gb", 255}}};

typedef enum urma_port_state {
    URMA_PORT_NOP = 0,
    URMA_PORT_DOWN,
    URMA_PORT_INIT,
    URMA_PORT_ARMED,
    URMA_PORT_ACTIVE,
    URMA_PORT_ACTIVE_DEFER,
} urma_port_state_t;

void UrmaDevice::Init() noexcept
{
    std::lock_guard<std::mutex> lock(MUTEX);
    if (LOADED) {
        UBS_VLOG_DEBUG("already inited");
        return;
    }

    urma_device_t **device_list = nullptr;
    int num_devices = 0;
    device_list = UrmaApi::urma_get_device_list(&num_devices);
    if (device_list == nullptr) {
        UBS_VLOG_DEBUG("No urma device found in the system, please use 'urma_admin show' to double check");
        return;
    }

    urma_device_attr_t tmp_attr{};
    for (int i = 0; i < num_devices; i++) {
        auto dev = device_list[i];
        if (UNLIKELY(dev == nullptr)) {
            UBS_VLOG_DEBUG("Invalid device in device list, which should not happen");
            continue;
        }

        /* get device attributes and get eid list */
        auto result = UrmaApi::urma_query_device(dev, &tmp_attr);
        if (LIKELY(result == URMA_SUCCESS)) {
            /* create new device and add into all devices */
            auto new_dev = MakeRef<UrmaDevice>(dev->name, dev->path, tmp_attr);
            if (new_dev == nullptr) {
                UBS_VLOG_ERR("New object failed, probably out of memory");
                UrmaApi::urma_free_device_list(device_list);
                return;
            }

            ALL_DEVICES.emplace(dev->name, new_dev);
#ifdef ENABLED
            /* get eids */
            uint32_t eid_count = 0;
            auto eid_list = UrmaApi::urma_get_eid_list(dev, &eid_count);
            if (UNLIKELY(eid_list == nullptr)) {
                UBS_VLOG_DEBUG("Get eid list failed");
                continue;
            }

            for (uint32_t j = 0; j < eid_count; j++) {
                auto eid = eid_list[j];
                new_dev->eid_list_.push_back(eid);
            }
#endif
        }
    }

    LOADED = true;
}

UrmaDevice::UrmaDevice(const std::string &name, const std::string &sys_path, const urma_device_attr_t &attr)
    : device_name_(name),
      device_sys_path_(sys_path),
      attributes_(attr)
{
}

std::string UrmaDevice::ToString(bool whole, const std::string &prefix, const std::string &seperator) const noexcept
{
    constexpr int width = 20;
    std::ostringstream oss;
    oss << prefix << std::left << std::setw(width) << "device name: " << device_name_ << seperator;
    oss << prefix << std::left << std::setw(width) << "sys path: " << device_sys_path_ << seperator;
    oss << prefix << std::left << std::setw(width) << "port count: " << (uint32_t)attributes_.port_cnt << seperator;

    if (attributes_.port_cnt > 0) {
        /* bandwidth */
        auto speed = attributes_.port_attr[0].active_speed;
        auto iter = URMA_BANDWIDTHS.find(speed);
        if (iter == URMA_BANDWIDTHS.end()) {
            oss << prefix << std::left << std::setw(width) << "bandwidth: invalid" << seperator;
        } else {
            oss << prefix << std::left << std::setw(width) << "bandwidth: " << iter->second.str << seperator;
        }

        /* state */
        oss << prefix << std::left << std::setw(width) << "state: " << DeviceState2Str(attributes_.port_attr[0].state)
            << seperator;

        /* mtu */
        oss << prefix << std::left << std::setw(width)
            << "active mtu: " << DeviceMTU2Str(attributes_.port_attr[0].active_mtu) << seperator;
    }

    if (whole) {
        oss << prefix << std::left << std::setw(width) << "max jetty: " << attributes_.dev_cap.max_jetty << seperator;
        oss << prefix << std::left << std::setw(width) << "max jfs: " << attributes_.dev_cap.max_jfs << seperator;
        oss << prefix << std::left << std::setw(width) << "max jfs depth: " << attributes_.dev_cap.max_jfs_depth
            << seperator;
        oss << prefix << std::left << std::setw(width) << "max jfr: " << attributes_.dev_cap.max_jfr << seperator;
        oss << prefix << std::left << std::setw(width) << "max jfr depth: " << attributes_.dev_cap.max_jfr_depth
            << seperator;
        oss << prefix << std::left << std::setw(width) << "max jfc: " << attributes_.dev_cap.max_jfc << seperator;
        oss << prefix << std::left << std::setw(width) << "max jfc depth: " << attributes_.dev_cap.max_jfc_depth
            << seperator;
        oss << prefix << std::left << std::setw(width) << "max sge: " << attributes_.dev_cap.max_jfr_sge << seperator;
        oss << prefix << std::left << std::setw(width) << "max msg size: " << attributes_.dev_cap.max_msg_size
            << seperator;
        oss << prefix << std::left << std::setw(width) << "max inline size: " << attributes_.dev_cap.max_jfs_inline_len
            << seperator;

        uint32_t eid_index = 0;
        for (auto &eid : eid_list_) {
            oss << prefix << std::left << std::setw(width) << "eid  " << eid_index++ << ": " << eid.eid.raw
                << seperator;
        }
    }

    return oss.str();
}
} // namespace urma
} // namespace ubs
} // namespace ock