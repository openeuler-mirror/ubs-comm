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

ALWAYS_INLINE const std::map<std::string, UrmaDevicePtr> &UrmaDevice::AllDevices() noexcept
{
    return ALL_DEVICES;
}

enum UrmaJfcPollingType {
    BUSY_POLLING = 0,
    EVENT_POLLING
};

class UrmaContext : public Referable {
public:
    static Result CreateContext(const std::string &devName, uint32_t eidIndex, UrmaContextPtr &context);
    static uint32_t NewJettyId() noexcept;

public:
    UrmaContext(urma_context_t *urmaContext, const UrmaDevicePtr &dev) : context_(urmaContext), device_(dev)
    {
        OBJ_INC_COUNT(URMA_CONTEXT);
    }

    ~UrmaContext() override;

    Result CreateJfc(urma_jfc_cfg_t &cfg, UrmaJfcPollingType pollingType, UrmaJfcPtr &out);
    Result CreateJfs(urma_jfs_cfg_t &cfg, const UrmaJfcPtr &jfc, UrmaJfsPtr &out);
    Result CreateJfr(urma_jfr_cfg_t &cfg, const UrmaJfcPtr &jfc, UrmaJfrPtr &out);
    Result CreateJetty(urma_jetty_cfg_t &cfg, const UrmaJfsPtr &jfs, const UrmaJfrPtr &jfr, UrmaJettyPtr &out);

private:
    urma_context_t *context_{nullptr};
    UrmaDevicePtr device_{nullptr};

private:
    static std::map<std::pair<std::string, uint32_t>, UrmaContextPtr> ALL_CONTEXTS;
    static std::mutex ALL_CONTEXTS_MUTEX;
    static std::atomic<uint32_t> AUTO_INCREASE_JETTY_ID;
};

class UrmaJfc : public Referable {
public:
    UrmaJfc(UrmaJfcPollingType pollingType, urma_jfc_t *jfc, urma_jfce_t *jfce, const UrmaContextPtr &ctx)
        : raw_jfc_(jfc),
          raw_jfce_(jfce),
          polling_type_(pollingType),
          context_(ctx)
    {
        UBS_ASSERT(jfc != nullptr);
        UBS_ASSERT(pollingType == EVENT_POLLING && jfce != nullptr);
        UBS_ASSERT(ctx != nullptr);
        OBJ_INC_COUNT(URMA_JFC);
    }

    ~UrmaJfc() override;

    void Destroy() noexcept;

private:
    urma_jfc_t *raw_jfc_ = nullptr;         /* raw jfc */
    urma_jfce_t *raw_jfce_ = nullptr;       /* raw jfce */
    const UrmaJfcPollingType polling_type_; /* polling type */
    const UrmaContextPtr context_;          /* hold the reference */

    friend class UrmaContext;
};

class UrmaJfs : public Referable {
public:
    UrmaJfs(urma_jfs_t *jfs, const UrmaContextPtr &ctx, const UrmaJfcPtr &jfc) : raw_jfs_(jfs), context_(ctx), jfc_(jfc)
    {
        UBS_ASSERT(jfs != nullptr);
        OBJ_INC_COUNT(URMA_JFS);
    }

    ~UrmaJfs() override;

    void Destroy() noexcept;

private:
    urma_jfs_t *raw_jfs_ = nullptr; /* raw jfs */
    const UrmaContextPtr context_;  /* hold the reference */
    const UrmaJfcPtr jfc_;          /* hold the reference */

    friend class UrmaContext;
};

class UrmaJfr : public Referable {
public:
    UrmaJfr(urma_jfr_t *jfr, const UrmaContextPtr &ctx, const UrmaJfcPtr &jfc) : raw_jfr_(jfr), context_(ctx), jfc_(jfc)
    {
        UBS_ASSERT(jfr != nullptr);
        OBJ_INC_COUNT(URMA_JFR);
    }

    ~UrmaJfr() override;

    void Destroy() noexcept;

private:
    urma_jfr_t *raw_jfr_ = nullptr; /* raw jfr */
    const UrmaContextPtr context_;  /* hold the reference */
    const UrmaJfcPtr jfc_;          /* hold the reference */

    friend class UrmaContext;
};

class UrmaJetty : public Referable {
public:
    UrmaJetty(urma_jetty_t *jetty, const UrmaContextPtr &ctx, const UrmaJfsPtr &jfs, const UrmaJfrPtr &jfr)
        : raw_jetty_(jetty),
          context_(ctx),
          jfs_(jfs),
          jfr_(jfr)
    {
        UBS_ASSERT(jetty != nullptr);
        OBJ_INC_COUNT(URMA_JETTY);
    }

    ~UrmaJetty() override;

    void Destroy() noexcept;

    Result ImportRemoteJetty(urma_rjetty_t &remote, urma_token_t &token) noexcept;

private:
    urma_jetty_t *raw_jetty_ = nullptr; /* raw jetty */
    const UrmaContextPtr context_;      /* hold the reference */
    const UrmaJfsPtr jfs_;              /* hold the reference */
    const UrmaJfrPtr jfr_;              /* hold the reference */
};

class UrmaSegment : public Referable {};
} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_WRAPPER_H
