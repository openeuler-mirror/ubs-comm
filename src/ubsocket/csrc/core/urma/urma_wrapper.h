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
#ifndef UBS_COMM_URMA_WRAPPER_H
#define UBS_COMM_URMA_WRAPPER_H

#include "common/ubsocket_common_includes.h"
#include "under_api/urma/urma_api_dl.h"
#include "under_api/urma/urma_types.h"
#include "urma_socket_types.h"

namespace ock {
namespace ubs {
namespace urma {
struct UBEId {
    uint16_t dev_index = 0;
    uint16_t eid_index = 0;
    urma_eid_t urma_eid{};
    uint8_t bandwidth = 0;
} __attribute__((packed));

class UrmaDevice;
class UrmaContext;
class UrmaJetty;
class UrmaJfs;
class UrmaJfr;
class UrmaJfc;
class UrmaSegment;
using UrmaDevicePtr = Ref<UrmaDevice>;
using UrmaContextPtr = Ref<UrmaContext>;
using UrmaJettyPtr = Ref<UrmaJetty>;
using UrmaJfsPtr = Ref<UrmaJfs>;
using UrmaJfrPtr = Ref<UrmaJfr>;
using UrmaJfcPtr = Ref<UrmaJfc>;
using UrmaSegmentPtr = Ref<UrmaSegment>;

struct UrmaDeviceBandWidth {
    std::string str;
    uint16_t intValue;

    UrmaDeviceBandWidth(const std::string &s, uint16_t v) : str(s), intValue(v) {}
};

class UrmaDevice : public Referable {
public:
    static void Init() noexcept;

    static const std::map<std::string, UrmaDevicePtr> &AllDevices() noexcept;

    static std::string DeviceState2Str(urma_port_state_t s);
    static std::string DeviceMTU2Str(urma_mtu_t s);

public:
    UrmaDevice(const std::string &name, const std::string &sys_path, const urma_device_attr_t &attr);
    ~UrmaDevice() override = default;

    const std::string &DeviceName() const noexcept;

    std::string ToString(bool whole = false, const std::string &prefix = "",
                         const std::string &seperator = "") const noexcept;

private:
    std::string device_name_;
    std::string device_sys_path_;
    urma_device_attr_t attributes_{};
    std::vector<urma_eid_info_t> eid_list_;

private:
    static std::map<std::string, UrmaDevicePtr> ALL_DEVICES;
    static bool LOADED;
    static std::mutex MUTEX;
    static std::map<urma_speed_t, UrmaDeviceBandWidth> URMA_BANDWIDTHS;
};

ALWAYS_INLINE const std::string &UrmaDevice::DeviceName() const noexcept
{
    return device_name_;
}

ALWAYS_INLINE std::string UrmaDevice::DeviceState2Str(urma_port_state_t s)
{
    switch (s) {
        case URMA_PORT_DOWN:
            return "down";
        case URMA_PORT_ACTIVE:
            return "active";
        case URMA_PORT_NOP:
        case URMA_PORT_INIT:
        case URMA_PORT_ARMED:
        case URMA_PORT_ACTIVE_DEFER:
            break;
    }
    return "unknown";
}

ALWAYS_INLINE std::string UrmaDevice::DeviceMTU2Str(urma_mtu_t s)
{
    switch (s) {
        case URMA_MTU_256:
            return "256bytes";
        case URMA_MTU_512:
            return "512bytes";
        case URMA_MTU_1024:
            return "1024bytes";
        case URMA_MTU_2048:
            return "2048bytes";
        case URMA_MTU_4096:
            return "4096bytes";
        case URMA_MTU_8192:
            return "8192bytes";
    }

    return "unknown";
}

class UrmaContext : public Referable {
public:
private:
    urma_context_t *context_ = nullptr;
    urma_device_attr_t *dev_attr_ = nullptr;
    uint32_t max_jfs_ = 0;
    uint32_t max_jfr = 0;
    int cpt_priority_ = 0;
    int rtp_priority_ = 0;
    int max_sge_ = 16;
    uint8_t port_count_ = 1;
    UBEId ub_eid_{};
};

class UrmaJetty : public Referable {};
class UrmaJfs : public Referable {};
class UrmaJfr : public Referable {};
class UrmaJfc : public Referable {};
class UrmaSegment : public Referable {};
} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_WRAPPER_H
