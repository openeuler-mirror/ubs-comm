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

#include <fcntl.h>
#include <unistd.h>
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
        UBS_VLOG_DEBUG("[URMA_API] No urma device found in the system, please use 'urma_admin show' to double check");
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
        } else {
            UBS_VLOG_ERR("[URMA_API] urma_query_device() failed, errno %d", errno);
        }
    }

    /* free device list */
    UrmaApi::urma_free_device_list(device_list);

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
        oss << prefix << std::left << std::setw(width) << "max jfr sge: " << attributes_.dev_cap.max_jfr_sge
            << seperator;
        oss << prefix << std::left << std::setw(width) << "max jfs sge: " << attributes_.dev_cap.max_jfs_sge
            << seperator;
        oss << prefix << std::left << std::setw(width) << "max jfs rsge: " << attributes_.dev_cap.max_jfs_rsge
            << seperator;
        oss << prefix << std::left << std::setw(width) << "max msg size: " << attributes_.dev_cap.max_msg_size
            << seperator;
        oss << prefix << std::left << std::setw(width) << "max inline size: " << attributes_.dev_cap.max_jfs_inline_len
            << seperator;
        oss << prefix << std::left << std::setw(width) << "max read: " << attributes_.dev_cap.max_read_size
            << seperator;
        oss << prefix << std::left << std::setw(width) << "max write: " << attributes_.dev_cap.max_write_size
            << seperator;

        uint32_t eid_index = 0;
        for (auto &eid : eid_list_) {
            oss << prefix << std::left << std::setw(width) << "eid  " << eid_index++ << ": " << eid.eid.raw
                << seperator;
        }
    }

    return oss.str();
}

/**
 * Functions for urma context
 */
std::map<std::pair<std::string, uint32_t>, UrmaContextPtr> UrmaContext::ALL_CONTEXTS;
std::mutex UrmaContext::ALL_CONTEXTS_MUTEX;
std::atomic<uint32_t> UrmaContext::AUTO_INCREASE_JETTY_ID(0);

Result UrmaContext::CreateContext(const std::string &devName, uint32_t eidIndex, UrmaContextPtr &out)
{
    UBS_VLOG_DEBUG("enter dev=%s eid index=%d", devName.c_str(), eidIndex);
    if (devName.empty()) {
        UBS_VLOG_ERR("Create urma context failed, as param 'devName' is empty");
        return UBS_INVALID_PARAM;
    }

    /* double confirm uarm device is initialized */
    UrmaDevice::Init();

    /* step1: loop cache device with name */
    std::string target_dev_name;
    UrmaDevicePtr target_dev;
    auto devices = UrmaDevice::AllDevices();
    for (auto &iter : devices) {
        if (iter.first.find(devName) != std::string::npos) {
            /* use the first matched dev */
            target_dev_name = iter.first;
            target_dev = iter.second;
            break;
        }
    }

    /* step2: check if there is any right dev */
    if (target_dev_name.empty()) {
        UBS_VLOG_ERR("Not found device name matched '%s'", devName.c_str());
        return UBS_INVALID_PARAM;
    }

    UBS_VLOG_DEBUG("found device %s", target_dev_name.c_str());

    /* step3: find context from previously created context, return if found */
    auto tmp_key = std::pair<std::string, uint32_t>(target_dev_name, eidIndex);
    /* take mutex */
    std::lock_guard<std::mutex> guard(ALL_CONTEXTS_MUTEX);
    auto context_iter = ALL_CONTEXTS.find(tmp_key);
    if (context_iter != ALL_CONTEXTS.end()) {
        out = context_iter->second;
        return UBS_OK;
    }

    /* step4: no previously created context, create one and put into all contexts */
    auto raw_dev = UrmaApi::urma_get_device_by_name(const_cast<char *>(target_dev_name.c_str()));
    if (raw_dev == nullptr) {
        UBS_VLOG_ERR("[URMA_API] urma_get_device_by_name() failed for '%s', errno: %d", target_dev_name.c_str(), errno);
        return UBS_UB_DEV_ERROR;
    }

    /* step5: create raw context */
    auto raw_context = UrmaApi::urma_create_context(raw_dev, eidIndex);
    if (raw_context == nullptr) {
        UBS_VLOG_ERR("[URMA_API] Create urma context with dev name '%s' and eid index '%d' failed, errno: %d",
                     target_dev_name.c_str(), eidIndex, errno);
        return UBS_UB_DEV_ERROR;
    }

    uint32_t eid_count = 0;
    auto eid_list = UrmaApi::urma_get_eid_list(raw_dev, &eid_count);
    if (eid_list == nullptr || eid_count == 0) {
        UrmaApi::urma_delete_context(raw_context);
        UBS_VLOG_ERR("Eid list is null or eid count is %d", eid_count);
        return UBS_UB_DEV_ERROR;
    }

    if (eidIndex >= eid_count) {
        UrmaApi::urma_free_eid_list(eid_list);
        UrmaApi::urma_delete_context(raw_context);
        UBS_VLOG_ERR("Eid index %d is out of range, eid count is %d", eidIndex, eid_count);
        return UBS_UB_DEV_ERROR;
    }

    auto eid_info = eid_list[eidIndex];

    /* step6: create our context */
    auto tmp_context = MakeRef<UrmaContext>(raw_context, target_dev, eid_info);
    UrmaApi::urma_free_eid_list(eid_list);

    if (tmp_context == nullptr) {
        UrmaApi::urma_delete_context(raw_context);
        UBS_VLOG_ERR("Create UrmaContext object failed, probaly out of memory");
        return UBS_MALLOC_FAILED;
    }

    /* step7: insert into all context */
    ALL_CONTEXTS.emplace(tmp_key, tmp_context);

    /* step8: assign to out */
    out = tmp_context;

    UBS_VLOG_DEBUG("created");
    return UBS_OK;
}

uint32_t UrmaContext::NewJettyId() noexcept
{
    ++AUTO_INCREASE_JETTY_ID;
    return AUTO_INCREASE_JETTY_ID.load();
}

UrmaContext::~UrmaContext()
{
    if (raw_context_ != nullptr) {
        UrmaApi::urma_delete_context(raw_context_);
        raw_context_ = nullptr;
    }

    OBJ_DEC_COUNT(UBS_URMA_CONTEXT);
}

Result UrmaContext::CreateJfc(urma_jfc_cfg_t &cfg, UrmaJfcPollingType pollingType, UrmaJfcPtr &out)
{
    UBS_VLOG_DEBUG("enter");
    if (UNLIKELY(raw_context_ == nullptr)) {
        UBS_VLOG_ERR("Create jfc failed, as context is null");
        return UBS_ERROR;
    }

    if (pollingType == EVENT_POLLING) {
        auto raw_jfce = UrmaApi::urma_create_jfce(this->raw_context_);
        if (raw_jfce == nullptr) {
            UBS_VLOG_ERR("[URMA_API] Create jfce failed, errno %d", errno);
            return UBS_ERROR;
        }

        cfg.jfce = raw_jfce;
        UBS_SLOG_DEBUG(cfg);

        auto raw_jfc = UrmaApi::urma_create_jfc(this->raw_context_, &cfg);
        if (raw_jfc == nullptr) {
            UrmaApi::urma_delete_jfce(raw_jfce);
            UBS_SLOG_ERR("[URMA_API] Create jfc failed, errno " << errno << ", " << cfg);
            return UBS_ERROR;
        }

        if (UrmaApi::urma_rearm_jfc(raw_jfc, 0) != URMA_SUCCESS) {
            UrmaApi::urma_delete_jfce(raw_jfce);
            UrmaApi::urma_delete_jfc(raw_jfc);
            UBS_VLOG_ERR("[URMA_API] create jfc failed as rearm jfc failed, errno %d", errno);
            return UBS_ERROR;
        }

        auto tmp_ctl = fcntl(raw_jfce->fd, F_GETFL);
        if (fcntl(raw_jfce->fd, F_SETFL, static_cast<uint32_t>(tmp_ctl) | O_NONBLOCK) < 0) {
            UrmaApi::urma_delete_jfce(raw_jfce);
            UrmaApi::urma_delete_jfc(raw_jfc);
            UBS_VLOG_ERR("fcntl() set jfc fd to non-blocking failed, errno %d", errno);
            return UBS_ERROR;
        }

        auto jfc = MakeRef<UrmaJfc>(pollingType, raw_jfc, raw_jfce, this);
        if (jfc == nullptr) {
            UrmaApi::urma_delete_jfce(raw_jfce);
            UrmaApi::urma_delete_jfc(raw_jfc);
            UBS_VLOG_ERR("Create UrmaJfc object failed, probably out of memory");
            return UBS_MALLOC_FAILED;
        }

        out = jfc;
    } else {
        cfg.jfce = nullptr;
        UBS_SLOG_DEBUG(cfg);

        auto raw_jfc = UrmaApi::urma_create_jfc(this->raw_context_, &cfg);
        if (raw_jfc == nullptr) {
            UBS_SLOG_ERR("[URMA_API] Create jfc failed, errno " << errno << ", " << cfg);
            return UBS_ERROR;
        }

        auto jfc = MakeRef<UrmaJfc>(pollingType, raw_jfc, nullptr, this);
        if (jfc == nullptr) {
            UrmaApi::urma_delete_jfc(raw_jfc);
            UBS_VLOG_ERR("Create UrmaJfc object failed, probably out of memory");
            return UBS_MALLOC_FAILED;
        }
    }

    UBS_VLOG_DEBUG("jfc created");
    return UBS_OK;
}

Result UrmaContext::CreateJfs(urma_jfs_cfg_t &cfg, const UrmaJfcPtr &jfc, UrmaJfsPtr &out)
{
    UBS_VLOG_DEBUG("enter");
    if (UNLIKELY(raw_context_ == nullptr)) {
        UBS_VLOG_ERR("Create jfs failed, as context is null");
        return UBS_ERROR;
    }

    if (UNLIKELY(jfc == nullptr || jfc->raw_jfc_ == nullptr)) {
        UBS_VLOG_ERR("Create jfs failed, as jfc is null");
        return UBS_ERROR;
    }

    cfg.jfc = jfc->raw_jfc_;
    UBS_SLOG_DEBUG(cfg);

    auto raw_jfs = UrmaApi::urma_create_jfs(this->raw_context_, &cfg);
    if (raw_jfs == nullptr) {
        UBS_SLOG_ERR("[URMA_API] Create jfs failed, errno " << errno << ", " << cfg);
        return UBS_ERROR;
    }

    auto jfs = MakeRef<UrmaJfs>(raw_jfs, this, jfc);
    if (jfs == nullptr) {
        UrmaApi::urma_delete_jfs(raw_jfs);
        UBS_VLOG_ERR("Create UrmaJfs object failed, probably out of memory");
        return UBS_MALLOC_FAILED;
    }

    out = jfs;

    UBS_VLOG_DEBUG("jfs created");
    return UBS_OK;
}

Result UrmaContext::CreateJfr(urma_jfr_cfg_t &cfg, const UrmaJfcPtr &jfc, UrmaJfrPtr &out)
{
    UBS_VLOG_DEBUG("enter");
    if (UNLIKELY(raw_context_ == nullptr)) {
        UBS_VLOG_ERR("Create jfr failed, as context is null");
        return UBS_ERROR;
    }

    if (UNLIKELY(jfc == nullptr || jfc->raw_jfc_ == nullptr)) {
        UBS_VLOG_ERR("Create jfr failed, as jfc is null");
        return UBS_ERROR;
    }

    cfg.jfc = jfc->raw_jfc_;
    UBS_SLOG_DEBUG(cfg);

    auto raw_jfr = UrmaApi::urma_create_jfr(this->raw_context_, &cfg);
    if (raw_jfr == nullptr) {
        UBS_SLOG_ERR("[URMA_API] Create jfr failed, errno: " << errno << ", " << cfg);
        return UBS_ERROR;
    }

    auto jfr = MakeRef<UrmaJfr>(raw_jfr, cfg.token_value.token, this, jfc);
    if (jfr == nullptr) {
        UrmaApi::urma_delete_jfr(raw_jfr);
        UBS_VLOG_ERR("Create UrmaJfr object failed, probably out of memory");
        return UBS_MALLOC_FAILED;
    }

    out = jfr;

    UBS_VLOG_DEBUG("jfr created");
    return UBS_OK;
}

Result UrmaContext::CreateJetty(urma_jetty_cfg_t &cfg, urma_tp_type_t rtp_ctp_utp, const UrmaJfsPtr &jfs,
                                const UrmaJfrPtr &jfr, UrmaJettyPtr &out)
{
    UBS_VLOG_DEBUG("enter");
    if (UNLIKELY(raw_context_ == nullptr)) {
        UBS_VLOG_ERR("Create jetty failed, as context is null");
        return UBS_ERROR;
    }

    if (UNLIKELY(jfs == nullptr || jfs->raw_jfs_ == nullptr || jfs->jfc_ == nullptr ||
                 jfs->jfc_->raw_jfc_ == nullptr)) {
        UBS_VLOG_ERR("Create jetty failed, as jfs or it jfc is null");
        return UBS_ERROR;
    }

    if (UNLIKELY(jfr == nullptr || jfr->raw_jfr_ == nullptr || jfr->jfc_ == nullptr ||
                 jfr->jfc_->raw_jfc_ == nullptr)) {
        UBS_VLOG_ERR("Create jetty failed, as jfr or it jfc is null");
        return UBS_ERROR;
    }

    cfg.jfs_cfg = jfs->raw_jfs_->jfs_cfg;
    cfg.shared.jfr = jfr->raw_jfr_;
    cfg.shared.jfc = jfr->jfc_->raw_jfc_;

    UBS_SLOG_DEBUG(cfg);
    UBS_SLOG_DEBUG(jfr->raw_jfr_->jfr_cfg);

    auto raw_jetty = UrmaApi::urma_create_jetty(this->raw_context_, &cfg);
    if (raw_jetty == nullptr) {
        UBS_SLOG_ERR("[URMA_API] Create jetty failed, errno " << errno << ", " << cfg);
        return UBS_ERROR;
    }

    auto jetty = MakeRef<UrmaJetty>(rtp_ctp_utp, raw_jetty->jetty_id.id, raw_jetty, this, jfs, jfr);
    if (jetty == nullptr) {
        UrmaApi::urma_delete_jetty(raw_jetty);
        UBS_VLOG_ERR("Create UrmaJetty object failed, probably out of memory");
        return UBS_MALLOC_FAILED;
    }

    out = jetty;

    UBS_VLOG_DEBUG("jetty created");
    return UBS_OK;
}

urma_jfc_cfg_t UrmaContext::CreateJfcCfg(uint32_t queueDepth, uint64_t userCtx)
{
    urma_jfc_cfg_t cfg{};
    bzero(&cfg, sizeof(urma_jfc_cfg_t));

    cfg.depth = queueDepth;
    cfg.user_ctx = userCtx;

    return cfg;
}

urma_jfs_cfg_t UrmaContext::CreateJfsCfg(uint32_t queueDepth, urma_transport_mode_t transMode, uint64_t userCtx,
                                         uint8_t priority)
{
    urma_jfs_cfg_t cfg{};
    bzero(&cfg, sizeof(urma_jfs_cfg_t));

    if (UNLIKELY(raw_context_ == nullptr || device_ == nullptr)) {
        UBS_VLOG_ERR("Create jfs cfg failed as raw context or device is nullptr");
        return cfg;
    }

    cfg.flag.bs.order_type = URMA_DEF_ORDER;
    cfg.trans_mode = transMode;
    cfg.depth = queueDepth;
    cfg.max_sge = device_->DeviceAttributes().dev_cap.max_jfs_sge;
    cfg.max_rsge = device_->DeviceAttributes().dev_cap.max_jfs_rsge;
    cfg.max_inline_data = device_->DeviceAttributes().dev_cap.max_jfs_inline_len;
    cfg.rnr_retry = URMA_JFS_RNR_RETRY_DEFAULT;
    cfg.err_timeout = URMA_JFS_ERROR_TIMEOUT;
    cfg.user_ctx = userCtx;
    if (priority != UINT8_MAX) {
        cfg.priority = priority;
    }

    return cfg;
}

urma_jfr_cfg_t UrmaContext::CreateJfrCfg(uint32_t queueDepth, urma_transport_mode_t transMode, uint32_t tokenValue,
                                         uint64_t userCtx)
{
    urma_jfr_cfg_t cfg{};
    bzero(&cfg, sizeof(urma_jfr_cfg_t));

    if (UNLIKELY(raw_context_ == nullptr || device_ == nullptr)) {
        UBS_VLOG_ERR("Create jfr cfg failed as raw context or device is nullptr");
        return cfg;
    }

    cfg.depth = queueDepth;
    cfg.trans_mode = transMode;
    cfg.max_sge = device_->DeviceAttributes().dev_cap.max_jfr_sge;
    cfg.min_rnr_timer = URMA_JFR_RNR_TIMER_DEFAULT;
    cfg.user_ctx = userCtx;
    cfg.flag.bs.token_policy = URMA_TOKEN_NONE;
    if (tokenValue != 0) {
        cfg.flag.bs.token_policy = URMA_TOKEN_POLICY_PLAIN_TEXT;
        cfg.token_value.token = tokenValue;
    }

    return cfg;
}

urma_jetty_cfg_t UrmaContext::CreateJettyCfg(uint64_t userCtx)
{
    urma_jetty_cfg_t cfg{};
    bzero(&cfg, sizeof(urma_jetty_cfg_t));

    cfg.flag.bs.share_jfr = 1;
    cfg.user_ctx = userCtx;

    return cfg;
}

/* urma jfc related function */
UrmaJfc::~UrmaJfc()
{
    Destroy();

    OBJ_DEC_COUNT(UBS_URMA_JFC);
}

void UrmaJfc::Destroy() noexcept
{
    if (raw_jfc_ != nullptr) {
        auto result = UrmaApi::urma_delete_jfc(raw_jfc_);
        if (result != URMA_SUCCESS) {
            UBS_VLOG_INFO("[URMA_API] Delete jfc, errno: %d", errno);
        }
        raw_jfc_ = nullptr;
    }

    if (raw_jfce_ != nullptr) {
        auto result = UrmaApi::urma_delete_jfce(raw_jfce_);
        if (result != URMA_SUCCESS) {
            UBS_VLOG_INFO("[URMA_API] Delete jfce, errno: %d", errno);
        }
        raw_jfce_ = nullptr;
    }
}

/* urma jfs function */
UrmaJfs::~UrmaJfs()
{
    Destroy();

    OBJ_DEC_COUNT(UBS_URMA_JFS);
}
void UrmaJfs::Destroy() noexcept
{
    if (raw_jfs_ != nullptr) {
        auto result = UrmaApi::urma_delete_jfs(raw_jfs_);
        if (result != URMA_SUCCESS) {
            UBS_VLOG_INFO("[URMA_API] Delete jfs failed, errno: %d", errno);
        }
        raw_jfs_ = nullptr;
    }
}

/* urma jfr function */
UrmaJfr::~UrmaJfr()
{
    Destroy();

    OBJ_DEC_COUNT(UBS_URMA_JFR);
}

void UrmaJfr::Destroy() noexcept
{
    if (raw_jfr_ != nullptr) {
        auto result = UrmaApi::urma_delete_jfr(raw_jfr_);
        if (result != URMA_SUCCESS) {
            UBS_VLOG_INFO("[URMA_API] Delete jfr, errno: %d", errno);
        }
        raw_jfr_ = nullptr;
    }
}

/* urma jetty function */
UrmaJetty::~UrmaJetty()
{
    Destroy();

    OBJ_DEC_COUNT(UBS_URMA_CONTEXT);
}

void UrmaJetty::Destroy() noexcept
{
    /* unbind if trans mode is rc */
    if (raw_jetty_ != nullptr && raw_jetty_->jetty_cfg.jfs_cfg.trans_mode == URMA_TM_RC &&
        raw_target_jetty_ != nullptr) {
        auto result = UrmaApi::urma_unbind_jetty(raw_jetty_);
        if (result != URMA_SUCCESS) {
            UBS_VLOG_INFO("[URMA_API] urma_unbind_jetty() failed, errno: %d", errno);
        }
    }

    /* un-import target jetty if imported */
    if (raw_target_jetty_ != nullptr) {
        auto result = UrmaApi::urma_unimport_jetty(raw_target_jetty_);
        if (result != URMA_SUCCESS) {
            UBS_VLOG_INFO("[URMA_API] urma_unimport_jetty() failed, errno: %d", errno);
        }
        raw_target_jetty_ = nullptr;
    }

    /* delete raw jetty */
    if (raw_jetty_ != nullptr) {
        auto result = UrmaApi::urma_delete_jetty(raw_jetty_);
        if (result != URMA_SUCCESS) {
            UBS_VLOG_INFO("[URMA_API] Delete jetty failed, errno: %d", errno);
        }
        raw_jetty_ = nullptr;
    }
}

Result UrmaJetty::ImportRemoteJetty(urma_jetty_id_t &jettyId, uint32_t &token) noexcept
{
    UBS_VLOG_DEBUG("enter");

    /* step1: verify */
    UBS_ASSERT_RETURN(context_ != nullptr && context_->raw_context_ != nullptr, UBS_ERROR);
    UBS_ASSERT_RETURN(raw_jetty_ != nullptr, UBS_ERROR);

    UBS_SLOG_DEBUG(*raw_jetty_);

    /* step2: create remote jetty and set related property */
    urma_rjetty_t remote_jetty{};
    remote_jetty.type = URMA_JETTY;
    remote_jetty.trans_mode = raw_jetty_->jetty_cfg.jfs_cfg.trans_mode;
    remote_jetty.jetty_id = jettyId;
    remote_jetty.trans_mode = raw_jetty_->jetty_cfg.jfs_cfg.trans_mode;
    urma_token_t raw_token{token};
    if (token != 0) {
        remote_jetty.flag.bs.token_policy = URMA_TOKEN_POLICY_PLAIN_TEXT;
    }
    remote_jetty.tp_type = rtp_ctp_utp_;

    /* step3: import jetty and get target jetty */
    auto target_jetty = UrmaApi::urma_import_jetty(context_->raw_context_, &remote_jetty, &raw_token);
    if (target_jetty == nullptr) {
        UBS_SLOG_ERR("Import remote jetty failed, errno " << errno << ", remote " << jettyId);
        return UBS_ERROR;
    }

    /* step4: skip bind if not rc mocde */
    if (raw_jetty_->jetty_cfg.jfs_cfg.trans_mode != URMA_TM_RC) {
        raw_target_jetty_ = target_jetty;
        UBS_VLOG_DEBUG("imported");
        return UBS_OK;
    }

    /* step5: bind jetty if rc */
    auto result = UrmaApi::urma_bind_jetty(raw_jetty_, target_jetty);
    if (result != URMA_SUCCESS) {
        UrmaApi::urma_unimport_jetty(target_jetty);
        UBS_SLOG_ERR("Bind local jetty and remote jetty failed, errno " << errno << ", remote " << jettyId);
        return UBS_ERROR;
    }

    /* step6: set raw target jetty */
    raw_target_jetty_ = target_jetty;
    UBS_VLOG_DEBUG("imported and bound");
    return UBS_OK;
}

/* ostream functions */
std::ostream &operator<<(std::ostream &os, const urma_eid_t &o)
{
    char buf[64l]{};
    snprintf(buf, sizeof(buf),
             "%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x", o.raw[0],
             o.raw[1], o.raw[2], o.raw[3], o.raw[4], o.raw[5], o.raw[6], o.raw[7], o.raw[8], o.raw[9], o.raw[10],
             o.raw[11], o.raw[12], o.raw[13], o.raw[14], o.raw[15]);
    os << "eid: " << buf;
    return os;
}

std::ostream &operator<<(std::ostream &os, const urma_jetty_id_t &o)
{
    os << "jetty_id [id: " << o.id << ", uasid: " << o.uasid << ", " << o.eid << "]";
    return os;
}

std::ostream &operator<<(std::ostream &os, const urma_jfc_cfg_t &o)
{
    /* level 0 members */
    os << "jfc cfg [depth: " << o.depth << ", ceqn: " << o.ceqn << ", user ctx: " << o.user_ctx;

    /* urma_jfc_flag_t flag */
    os << ", flag [lock free:" << o.flag.bs.lock_free << ", inline: " << o.flag.bs.jfc_inline
       << ", non-blocking: " << o.flag.bs.non_blocking << "]";

    /* jfce */
    os << ", jfce: " << std::hex << o.jfce << std::dec;
    if (o.jfce != nullptr) {
        os << ", jfce-fd: " << o.jfce->fd;
    }

    /* end */
    os << "]";
    return os;
}
std::ostream &operator<<(std::ostream &os, const urma_jfr_cfg_t &o)
{
    /* level 0 members */
    os << "jfr cfg [id: " << o.id << ", depth: " << o.depth << ", trans mode: " << o.trans_mode
       << ", max sge: " << (uint32_t)o.max_sge << ", min rnr timer: " << (uint32_t)o.min_rnr_timer
       << ", user ctx: " << o.user_ctx << std::hex << ", jfc: " << o.jfc << std::dec;

    /* urma_jfr_flag_t flag */
    os << ", flag [token policy: " << o.flag.bs.token_policy << ", tag matching: " << o.flag.bs.tag_matching
       << ", lock free: " << o.flag.bs.lock_free << ", order type: " << o.flag.bs.order_type
       << ", non blocking: " << o.flag.bs.non_blocking << ", ext: " << o.flag.bs.has_drv_ext << "]";

    /* end */
    os << "]";
    return os;
}

std::ostream &operator<<(std::ostream &os, const urma_jfs_cfg_t &o)
{
    /* level 0 members */
    os << "jfs cfg [depth: " << o.depth << ", trans mode: " << o.trans_mode << ", priority: " << (uint32_t)o.priority
       << ", max sge: " << (uint32_t)o.max_sge << ", max rsge: " << (uint32_t)o.max_rsge
       << ", max inline data: " << o.max_inline_data << ", rnr retry: " << (uint32_t)o.rnr_retry
       << ", err timeout: " << (uint32_t)o.err_timeout << ", user ctx: " << o.user_ctx << std::hex << ", jfc: " << o.jfc
       << std::dec;

    /* urma_jfs_flag_t flag */
    os << ", flag [lock free: " << o.flag.bs.lock_free << ", error suspend: " << o.flag.bs.error_suspend
       << ", outorder completion: " << o.flag.bs.outorder_comp << ", order type: " << o.flag.bs.order_type
       << ", multi path: " << o.flag.bs.multi_path << ", ctp rc multi mode: " << o.flag.bs.ctp_rc_mul_path_mode
       << ", non blocking: " << o.flag.bs.non_blocking << ", ext: " << o.flag.bs.has_drv_ext << "]";

    /* end */
    os << "]";
    return os;
}

std::ostream &operator<<(std::ostream &os, const urma_jetty_cfg_t &o)
{
    /* level 0 member */
    os << "jetty cfg [id: " << o.id << std::hex << ", jfc: " << o.shared.jfc << ", jfr: " << o.shared.jfr
       << ", jetty group: " << o.jetty_grp << ", user ctx: " << o.user_ctx << std::dec;

    /* urma_jetty_flag_t flag */
    os << ", flag [shared jfr:" << o.flag.bs.share_jfr << ", non-blocking: " << o.flag.bs.non_blocking
       << ", ext: " << o.flag.bs.has_drv_ext << "]";

    /* jfs cfg and end */
    os << ", " << o.jfs_cfg << "]";
    return os;
}

std::ostream &operator<<(std::ostream &os, const urma_jetty_t &o)
{
    os << "jetty [" << o.jetty_id << ", " << o.jetty_cfg << ", handle: " << o.handle << ", urma ctx: " << std::hex
       << o.urma_ctx << std::dec << "]";
    return os;
}

std::ostream &operator<<(std::ostream &os, const UrmaJetty &o)
{
    os << "jetty id: " << o.jetty_id_ << std::hex << " raw_jetty: " << o.raw_jetty_ << ", context: " << o.context_.Get()
       << ", jfs: " << o.jfs_.Get() << ", jfr: " << o.jfr_.Get() << std::dec;

    if (o.raw_jetty_ != nullptr) {
        os << ", " << *(o.raw_jetty_);
    }

    os << "]";

    return os;
}

} // namespace urma
} // namespace ubs
} // namespace ock