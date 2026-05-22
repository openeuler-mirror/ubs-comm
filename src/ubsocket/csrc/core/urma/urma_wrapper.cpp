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
std::vector<UrmaDevicePtr> UrmaDevice::ALL_DEVICES;
bool UrmaDevice::LOADED = false;
std::mutex UrmaDevice::MUTEX;

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

        auto result = UrmaApi::urma_query_device(dev, &tmp_attr);
        if (LIKELY(result == URMA_SUCCESS)) {
            auto new_dev = MakeRef<UrmaDevice>(dev->name, dev->path, tmp_attr);
            if (new_dev == nullptr) {
                UBS_VLOG_ERR("New object failed, probably out of memory");
                UrmaApi::urma_free_device_list(device_list);
                return;
            }

            ALL_DEVICES.emplace_back(new_dev);
        }
    }

    LOADED = true;
}

const std::vector<UrmaDevicePtr> &UrmaDevice::AllDevices() noexcept
{
    return ALL_DEVICES;
}

UrmaDevice::UrmaDevice(const std::string &name, const std::string &sys_path, const urma_device_attr_t &attr)
    : device_name_(name),
      device_sys_path_(sys_path),
      attributes_(attr)
{
}

std::string UrmaDevice::ToString(const std::string &prefix, const std::string &seperator) const noexcept
{
    constexpr int width = 10;
    std::ostringstream oss;
    oss << prefix << std::setw(width) << "device name: " << device_name_ << seperator;
    oss << prefix << std::setw(width) << "sys path: " << device_sys_path_ << seperator;
    oss << prefix << std::setw(width) << "guid: " << attributes_.guid.raw << seperator;
    oss << prefix << std::setw(width) << "port count: " << attributes_.port_cnt << seperator;
    oss << prefix << std::setw(width) << "max jetty: " << attributes_.dev_cap.max_jetty << seperator;
    oss << prefix << std::setw(width) << "max jfs: " << attributes_.dev_cap.max_jfs << seperator;
    oss << prefix << std::setw(width) << "max jfs depth: " << attributes_.dev_cap.max_jfs_depth << seperator;
    oss << prefix << std::setw(width) << "max jfr: " << attributes_.dev_cap.max_jfr << seperator;
    oss << prefix << std::setw(width) << "max jfr depth: " << attributes_.dev_cap.max_jfr_depth << seperator;
    oss << prefix << std::setw(width) << "max jfc: " << attributes_.dev_cap.max_jfc << seperator;
    oss << prefix << std::setw(width) << "max jfc depth: " << attributes_.dev_cap.max_jfc_depth << seperator;

    return oss.str();
}
} // namespace urma
} // namespace ubs
} // namespace ock