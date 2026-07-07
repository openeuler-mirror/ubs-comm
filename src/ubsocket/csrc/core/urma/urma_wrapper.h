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
#include "under_api/urma/dl_urma_api.h"
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

    const urma_device_attr_t &DeviceAttributes() const noexcept;

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

ALWAYS_INLINE const urma_device_attr_t &UrmaDevice::DeviceAttributes() const noexcept
{
    return attributes_;
}

enum UrmaJfcPollingType
{
    BUSY_POLLING = 0,
    EVENT_POLLING
};

constexpr uint8_t URMA_TOKEN_POLICY_PLAIN_TEXT = URMA_TOKEN_PLAIN_TEXT;
constexpr uint8_t URMA_JFR_RNR_TIMER_DEFAULT = 19; /* RNR single retransmission time: 2us*2^19 = 1.049s */
constexpr uint8_t URMA_JFS_RNR_RETRY_DEFAULT = 6;
constexpr uint8_t URMA_JFS_ERROR_TIMEOUT = 2;

class UrmaContext : public Referable {
public:
    static Result CreateContext(const std::string &devName, uint32_t eidIndex, UrmaContextPtr &out);
    static uint32_t NewJettyId() noexcept;

public:
    UrmaContext(urma_context_t *urmaContext, const UrmaDevicePtr &dev, const urma_eid_info_t &eidInfo)
        : raw_context_(urmaContext),
          device_(dev),
          eid_info_(eidInfo)
    {
        UBS_ASSERT(dev != nullptr);
        OBJ_INC_COUNT(UBS_URMA_CONTEXT);
    }

    ~UrmaContext() override;

    urma_jfc_cfg_t CreateJfcCfg(uint32_t queueDepth, uint64_t userCtx = 0);
    urma_jfs_cfg_t CreateJfsCfg(uint32_t queueDepth, urma_transport_mode_t transMode, uint64_t userCtx = 0,
                                uint8_t priority = UINT8_MAX);
    urma_jfr_cfg_t CreateJfrCfg(uint32_t queueDepth, urma_transport_mode_t transMode, uint32_t tokenValue = 0,
                                uint64_t userCtx = 0);
    urma_jetty_cfg_t CreateJettyCfg(uint64_t userCtx = 0);

    Result CreateJfc(urma_jfc_cfg_t &cfg, UrmaJfcPollingType pollingType, UrmaJfcPtr &out);
    Result CreateJfs(urma_jfs_cfg_t &cfg, const UrmaJfcPtr &jfc, UrmaJfsPtr &out);
    Result CreateJfr(urma_jfr_cfg_t &cfg, const UrmaJfcPtr &jfc, UrmaJfrPtr &out);
    Result CreateJetty(urma_jetty_cfg_t &cfg, urma_tp_type_t rtp_ctp_utp, const UrmaJfsPtr &jfs, const UrmaJfrPtr &jfr,
                       UrmaJettyPtr &out);

    const urma_eid_info_t &EidInfo() const noexcept;

private:
    urma_context_t *raw_context_{nullptr};
    UrmaDevicePtr device_{nullptr};
    urma_eid_info_t eid_info_{};

private:
    static std::map<std::pair<std::string, uint32_t>, UrmaContextPtr> ALL_CONTEXTS;
    static std::mutex ALL_CONTEXTS_MUTEX;
    static std::atomic<uint32_t> AUTO_INCREASE_JETTY_ID;

    friend class UrmaJetty;
};

ALWAYS_INLINE const urma_eid_info_t &UrmaContext::EidInfo() const noexcept
{
    return eid_info_;
}

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
        OBJ_INC_COUNT(UBS_URMA_JFC);
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
        OBJ_INC_COUNT(UBS_URMA_JFS);
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
    UrmaJfr(urma_jfr_t *jfr, uint32_t token, const UrmaContextPtr &ctx, const UrmaJfcPtr &jfc)
        : raw_jfr_(jfr),
          context_(ctx),
          jfc_(jfc),
          token_(token)
    {
        UBS_ASSERT(jfr != nullptr);
        OBJ_INC_COUNT(UBS_URMA_JFR);
    }

    ~UrmaJfr() override;

    void Destroy() noexcept;

private:
    urma_jfr_t *raw_jfr_ = nullptr; /* raw jfr */
    const UrmaContextPtr context_;  /* hold the reference */
    const UrmaJfcPtr jfc_;          /* hold the reference */
    const uint32_t token_;          /* token */

    friend class UrmaContext;
};

class UrmaJetty : public Referable {
public:
    UrmaJetty(urma_tp_type_t rtp_ctp_utp, uint32_t jettyId, urma_jetty_t *jetty, const UrmaContextPtr &ctx,
              const UrmaJfsPtr &jfs, const UrmaJfrPtr &jfr)
        : raw_jetty_(jetty),
          context_(ctx),
          jfs_(jfs),
          jfr_(jfr),
          jetty_id_(jettyId),
          rtp_ctp_utp_(rtp_ctp_utp)
    {
        UBS_ASSERT(jetty != nullptr);

        OBJ_INC_COUNT(UBS_URMA_JETTY);

        UBS_SLOG_DEBUG(*this);
    }

    ~UrmaJetty() override;

    /**
     * @brief Get int jetty id
     */
    uint32_t JettyId() const noexcept;

    /**
     * @brief Get raw jetty id struct
     */
    urma_jetty_id_t RawJettyId() const noexcept;

    /**
     * @brief Get token of jetty from jfr
     */
    uint32_t Token() const noexcept;

    /**
     * @brief Destroy inner raw jetty
     */
    void Destroy() noexcept;

    /**
     * @brief Import peer jetty
     *
     * @param jettyId      [in] jetty struct of peer
     * @param token        [in] token value of peer
     * @return 0 if sucessful
     */
    Result ImportRemoteJetty(urma_jetty_id_t &jettyId, uint32_t &token) noexcept;

    friend std::ostream &operator<<(std::ostream &os, const UrmaJetty &o);

private:
    urma_jetty_t *raw_jetty_ = nullptr;               /* raw jetty */
    urma_target_jetty_t *raw_target_jetty_ = nullptr; /* imported jetty */
    const UrmaContextPtr context_;                    /* hold the reference */
    const UrmaJfsPtr jfs_;                            /* hold the reference */
    const UrmaJfrPtr jfr_;                            /* hold the reference */
    const uint32_t jetty_id_;                         /* generated jetty id */
    const urma_tp_type_t rtp_ctp_utp_;                /* tp type */

    friend class UrmaContext;
};

ALWAYS_INLINE uint32_t UrmaJetty::JettyId() const noexcept
{
    return jetty_id_;
}

ALWAYS_INLINE urma_jetty_id_t UrmaJetty::RawJettyId() const noexcept
{
    urma_jetty_id_t tmp{};

    if (LIKELY(raw_jetty_ != nullptr)) {
        tmp = raw_jetty_->jetty_id;
    }

    return tmp;
}

ALWAYS_INLINE uint32_t UrmaJetty::Token() const noexcept
{
    uint32_t tmp = 0;
    if (LIKELY(raw_jetty_ != nullptr)) {
        tmp = raw_jetty_->jetty_cfg.jfr_cfg->token_value.token;
    }

    return tmp;
}

std::ostream &operator<<(std::ostream &os, const urma_eid_t &o);
std::ostream &operator<<(std::ostream &os, const urma_jetty_id_t &o);
std::ostream &operator<<(std::ostream &os, const urma_jfc_cfg_t &o);
std::ostream &operator<<(std::ostream &os, const urma_jfr_cfg_t &o);
std::ostream &operator<<(std::ostream &os, const urma_jfs_cfg_t &o);
std::ostream &operator<<(std::ostream &os, const urma_jetty_cfg_t &o);
std::ostream &operator<<(std::ostream &os, const urma_jetty_t &o);

class UrmaSegment : public Referable {
};
} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_WRAPPER_H
