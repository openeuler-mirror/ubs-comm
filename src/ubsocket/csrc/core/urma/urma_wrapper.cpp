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
            UBS_VLOG_ERR("[URMA_API] Open device failed, errno %d", errno);
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

/**
 * Functions for urma context
 */
std::map<std::pair<std::string, uint32_t>, UrmaContextPtr> UrmaContext::ALL_CONTEXTS;
std::mutex UrmaContext::ALL_CONTEXTS_MUTEX;
std::atomic<uint32_t> UrmaContext::AUTO_INCREASE_JETTY_ID(0);

Result UrmaContext::CreateContext(const std::string &devName, uint32_t eidIndex, UrmaContextPtr &context)
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
        context = context_iter->second;
        return UBS_OK;
    }

    /* step4: no previously created context, create one and put into all contexts */
    auto raw_dev = UrmaApi::urma_get_device_by_name(const_cast<char *>(target_dev_name.c_str()));
    if (raw_dev == nullptr) {
        UBS_VLOG_ERR("[URMA_API] Open urma device '%s' failed, errno: %d", target_dev_name.c_str(), errno);
        return UBS_UB_DEV_ERROR;
    }

    /* step5: create raw context */
    auto raw_context = UrmaApi::urma_create_context(raw_dev, eidIndex);
    if (raw_context == nullptr) {
        UBS_VLOG_ERR("[URMA_API] Create urma context with dev name '%s' and eid index '%d' failed, errno: %d",
                     target_dev_name.c_str(), eidIndex, errno);
        return UBS_UB_DEV_ERROR;
    }

    /* step6: create our context */
    auto tmp_context = MakeRef<UrmaContext>(raw_context, target_dev);
    if (tmp_context == nullptr) {
        UrmaApi::urma_delete_context(raw_context);
        UBS_VLOG_ERR("Create UrmaContext object failed, probaly out of memory");
        return UBS_MALLOC_FAILED;
    }

    /* step7: insert into all context */
    ALL_CONTEXTS.emplace(tmp_key, tmp_context);

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
    if (context_ != nullptr) {
        UrmaApi::urma_delete_context(context_);
        context_ = nullptr;
    }

    OBJ_DEC_COUNT(URMA_CONTEXT);
}

Result UrmaContext::CreateJfc(urma_jfc_cfg_t &cfg, UrmaJfcPollingType pollingType, UrmaJfcPtr &out)
{
    UBS_VLOG_DEBUG("enter");
    if (UNLIKELY(context_ == nullptr)) {
        UBS_VLOG_ERR("Create jfc failed, as context is null");
        return UBS_ERROR;
    }

    if (pollingType == EVENT_POLLING) {
        auto raw_jfce = UrmaApi::urma_create_jfce(this->context_);
        if (raw_jfce == nullptr) {
            UBS_VLOG_ERR("[URMA_API] Create jfce failed, errno %d", errno);
            return UBS_ERROR;
        }

        cfg.jfce = raw_jfce;
        auto raw_jfc = UrmaApi::urma_create_jfc(this->context_, &cfg);
        if (raw_jfc == nullptr) {
            UrmaApi::urma_delete_jfce(raw_jfce);
            UBS_VLOG_ERR("[URMA_API] Create jfc failed, errno %d", errno);
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
            UBS_VLOG_ERR("[URMA_API] create jfc failed as set jfc fd to non-blocking failed, errno %d", errno);
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
        auto raw_jfc = UrmaApi::urma_create_jfc(this->context_, &cfg);
        if (raw_jfc == nullptr) {
            UBS_VLOG_ERR("[URMA_API] Create jfc failed, errno %d", errno);
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
    if (UNLIKELY(context_ == nullptr)) {
        UBS_VLOG_ERR("Create jfs failed, as context is null");
        return UBS_ERROR;
    }

    if (UNLIKELY(jfc == nullptr || jfc->raw_jfc_ == nullptr)) {
        UBS_VLOG_ERR("Create jfs failed, as jfc is null");
        return UBS_ERROR;
    }

    cfg.jfc = jfc->raw_jfc_;
    auto raw_jfs = UrmaApi::urma_create_jfs(this->context_, &cfg);
    if (raw_jfs == nullptr) {
        UBS_VLOG_ERR("[URMA_API] Create jfs failed, errno %d", errno);
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
    if (UNLIKELY(context_ == nullptr)) {
        UBS_VLOG_ERR("Create jfr failed, as context is null");
        return UBS_ERROR;
    }

    if (UNLIKELY(jfc == nullptr || jfc->raw_jfc_ == nullptr)) {
        UBS_VLOG_ERR("Create jfr failed, as jfc is null");
        return UBS_ERROR;
    }

    cfg.jfc = jfc->raw_jfc_;
    auto raw_jfr = UrmaApi::urma_create_jfr(this->context_, &cfg);
    if (raw_jfr == nullptr) {
        UBS_VLOG_ERR("[URMA_API] Create jfr failed, errno %d", errno);
        return UBS_ERROR;
    }

    auto jfr = MakeRef<UrmaJfr>(raw_jfr, this, jfc);
    if (jfr == nullptr) {
        UrmaApi::urma_delete_jfr(raw_jfr);
        UBS_VLOG_ERR("Create UrmaJfr object failed, probably out of memory");
        return UBS_MALLOC_FAILED;
    }

    out = jfr;

    UBS_VLOG_DEBUG("jfr created");
    return UBS_OK;
}

Result UrmaContext::CreateJetty(urma_jetty_cfg_t &cfg, const UrmaJfsPtr &jfs, const UrmaJfrPtr &jfr, UrmaJettyPtr &out)
{
    UBS_VLOG_DEBUG("enter");
    if (UNLIKELY(context_ == nullptr)) {
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
    auto raw_jetty = UrmaApi::urma_create_jetty(this->context_, &cfg);
    if (raw_jetty == nullptr) {
        UBS_VLOG_ERR("[URMA_API] Create jetty failed, errno %d", errno);
        return UBS_ERROR;
    }

    auto jetty = MakeRef<UrmaJetty>(raw_jetty, this, jfs, jfr);
    if (jetty == nullptr) {
        UrmaApi::urma_delete_jetty(raw_jetty);
        UBS_VLOG_ERR("Create UrmaJetty object failed, probably out of memory");
        return UBS_MALLOC_FAILED;
    }

    out = jetty;

    UBS_VLOG_DEBUG("jetty created");
    return UBS_OK;
}

/* urma jfc related function */
UrmaJfc::~UrmaJfc()
{
    Destroy();

    OBJ_DEC_COUNT(URMA_JFC);
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

    OBJ_DEC_COUNT(URMA_JFS);
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

    OBJ_DEC_COUNT(URMA_JFR);
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

    OBJ_DEC_COUNT(URMA_CONTEXT);
}

Result UrmaJetty::ImportRemoteJetty(urma_rjetty_t &remote, urma_token_t &token) noexcept
{
    return UBS_OK;
}

void UrmaJetty::Destroy() noexcept
{
    if (raw_jetty_ != nullptr) {
        auto result = UrmaApi::urma_delete_jetty(raw_jetty_);
        if (result != URMA_SUCCESS) {
            UBS_VLOG_INFO("[URMA_API] Delete jetty failed, errno: %d", errno);
        }
        raw_jetty_ = nullptr;
    }
}
} // namespace urma
} // namespace ubs
} // namespace ock