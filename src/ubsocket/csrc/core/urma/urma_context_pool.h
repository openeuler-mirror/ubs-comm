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

#ifndef UBS_COMM_URMA_CONTEXT_POOL_H
#define UBS_COMM_URMA_CONTEXT_POOL_H

#include "urma_socket_types.h"
#include "urma_wrapper.h"

namespace ock {
namespace ubs {
namespace urma {
class UrmaContextPool;
using UrmaContextPoolPtr = Ref<UrmaContextPool>;

enum UrmaPortSelectionPolicy {
    PSP_ROUND_ROBIN = 0,
    PSP_RANDOM,

    PSP_COUNT,
};

class UrmaContextPool : public Referable {
public:
    UrmaContextPool() = default;
    ~UrmaContextPool() override = default;

    /**
     * @brief Auto find the dev name and eid index
     *
     * @return 0 if successful
     */
    Result AutoAddEidIndex(bool bondingDevice = true) noexcept;

    /**
     * @brief Add one eid index with dev name for creating context
     * 
     * @param devName      [in] name of device 
     * @param eidIndex    [in] index of eid
     * 
     * @return UBS_OK if no duplicated existed and inserted, 
     * UBS_ERROR return if already existed or already initialized
     */
    Result AddEidIndex(const std::string &devName, uint32_t eidIndex) noexcept;

    /**
     * @brief Clear all eid with index
     */
    void ClearEidIndex() noexcept;

    /**
     * @brief Initialize the context with dev name and eid index added by @see AddEidIndex()
     * 
     * @return UBS_OK if successful
     */
    Result Init() noexcept;

    /**
     * @brief Clear all created context
     */
    void UnInit() noexcept;

    /**
     * @brief Get context by dev name and eid index
     * 
     * @param devName      [in] name of device
     * @param eidIndex     [in] index of eid
     * @param ctx          [out] context got
     * @return UBS_OK if got, UBS_ERROR is not found
     * 
     */
    Result GetContext(const std::string &devName, uint32_t eidIndex, UrmaContextPtr &ctx) noexcept;

    /**
     * @brief Get all contexts
     */
    std::map<std::pair<std::string, uint32_t>, UrmaContextPtr> Contexts() noexcept;

    /**
     * @brief Get sorted contexts (same-chip first, then different-chip)
     */
    std::vector<UrmaContextPtr> SortedContexts() noexcept;

    /**
     * @brief Select a context by policy
     */
    UrmaContextPtr SelectContext(UrmaPortSelectionPolicy policy, int32_t *selected_index = nullptr,
                                  const UrmaContextHealthEntry *context_health = nullptr,
                                  uint32_t context_count = 0) noexcept;

private:
    void InitSortedContexts() noexcept;

    static int16_t GetMyChipId() noexcept;

private:
    std::mutex mutex_;                                                        /* mutex for protect all members */
    bool inited_ = false;                                                     /* init flag */
    int16_t my_chip_id_ = 0;                                                  /* which chip this process running on */
    std::map<std::pair<std::string, uint32_t>, UrmaContextPtr> contexts_;     /* eidIndex to context map */
    std::vector<UrmaContextPtr> sorted_contexts_;                             /* sorted contexts */
    std::atomic<uint64_t> seq_{0};
    const bool SKIP_PRIMARY_EID = true;                                       /* skip primary eid */
};
using UrmaContextPoolPtr = Ref<UrmaContextPool>;

ALWAYS_INLINE Result UrmaContextPool::AutoAddEidIndex(bool bondingDevice) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (inited_) {
        UBS_VLOG_ERR("Add eid index is NOT allowed after context pool initialized");
        return UBS_ERROR;
    }

    UrmaDevice::Init();

    auto &devices = UrmaDevice::AllDevices();

    if (devices.empty()) {
        UBS_VLOG_ERR("No ub device found");
        return UBS_ERROR;
    }

    UBS_VLOG_DEBUG("There are %d devices found", devices.size());

    for (auto &iter : devices) {
        auto &dev_name = iter.first;
        /* case 1: skip non bonding device */
        if (bondingDevice && dev_name.find("bonding") == std::string::npos) {
            continue;
        }

        /* case 2: skip bonding device */
        if (!bondingDevice && dev_name.find("bonding") != std::string::npos) {
            continue;
        }

        UBS_VLOG_TRACE("found device (%s)", dev_name.c_str());
        auto &device = iter.second;
        if (UNLIKELY(device == nullptr)) {
            UBS_VLOG_ERR("Un-reachable path, device is null");
            continue;
        }

        auto &eid_list = device->EidList();
        for (auto &item : eid_list) {
            UBS_VLOG_TRACE("adding device(%s) with eid index(%d)", dev_name.c_str(), item.eid_index);

            if (contexts_.find(std::make_pair(dev_name, item.eid_index)) != contexts_.end()) {
                UBS_VLOG_DEBUG("Eid index already exists");
                return UBS_ERROR;
            }

            /* insert an empty context */
            contexts_[std::make_pair(dev_name, item.eid_index)] = nullptr;
        }
    }

    UBS_VLOG_TRACE("Total added items count(%d)", contexts_.size());

    return UBS_OK;
}

ALWAYS_INLINE Result UrmaContextPool::AddEidIndex(const std::string &devName, uint32_t eidIndex) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (inited_) {
        UBS_VLOG_ERR("Add eid index is NOT allowed after context pool initialized");
        return UBS_ERROR;
    }

    /* insert an empty context */
    if (contexts_.find(std::make_pair(devName, eidIndex)) != contexts_.end()) {
        UBS_VLOG_DEBUG("Eid index already exists with dev name");
        return UBS_ERROR;
    }

    contexts_[std::make_pair(devName, eidIndex)] = nullptr;
    return UBS_OK;
}

ALWAYS_INLINE void UrmaContextPool::ClearEidIndex() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    contexts_.clear();
}

ALWAYS_INLINE Result UrmaContextPool::Init() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (inited_) {
        UBS_VLOG_DEBUG("UrmaContextPool already initialized");
        return UBS_OK;
    }

    for (auto &iter : contexts_) {
        /* skip the context that already created */
        if (iter.second != nullptr) {
            UBS_VLOG_TRACE("context for device name(%s) eid index(%d) already created", iter.first.first.c_str(),
                           iter.first.second);
            continue;
        }

        /* create context */
        auto result = UrmaContext::CreateContext(iter.first.first, iter.first.second, iter.second);
        if (result != UBS_OK) {
            UBS_VLOG_ERR("Create urma context failed with dev name: %s, eid index: %d", iter.first.first.c_str(),
                         iter.first.second);
        }

        UBS_VLOG_TRACE("created context for device name(%s) eid index(%d) context id(%d)", iter.first.first.c_str(),
                       iter.first.second, iter.second->Id());
    }
    InitSortedContexts();
    my_chip_id_ = GetMyChipId();
    inited_ = true;
    UBS_VLOG_TRACE("initialized, context size(%d)", contexts_.size());
    return UBS_OK;
}

ALWAYS_INLINE void UrmaContextPool::UnInit() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!inited_) {
        UBS_VLOG_DEBUG("UrmaContextPool is NOT initialized");
        return;
    }
    sorted_contexts_.clear();
    contexts_.clear();
    inited_ = false;
}

ALWAYS_INLINE void UrmaContextPool::InitSortedContexts() noexcept
{
    const std::string my_preferred_udma_device_name = "udmac" + std::to_string(my_chip_id_);
    UBS_VLOG_TRACE("preferred device prefix(%s), context pool size(%d),  chip id(%d)",
                   my_preferred_udma_device_name.c_str(), contexts_.size(), my_chip_id_);
    std::vector<UrmaContextPtr> candidateOnSameChip;
    std::vector<UrmaContextPtr> candidateOnDifferentChip;
    candidateOnSameChip.reserve(contexts_.size());
    candidateOnDifferentChip.reserve(contexts_.size());

    uint32_t first_eid_index = UINT32_MAX;
    for (auto &item : contexts_) {
        const auto dev_name = item.first.first;
        const auto eid_index = item.first.second;
        /* skip the first one */
        if (SKIP_PRIMARY_EID && UrmaSetting::UBS_DEV_NAME.empty() && first_eid_index == UINT32_MAX) {
            UBS_VLOG_TRACE("Skip primary eid, dev name (%s) eid (%d)", dev_name.c_str(), eid_index);
            first_eid_index = eid_index;
            continue;
        }

        /* skip the same eid index for other devices */
        if (first_eid_index == eid_index) {
            UBS_VLOG_TRACE("Skip primary eid, dev name (%s) eid (%d)", dev_name.c_str(), eid_index);
            continue;
        }

        /* skip bad ports */
        if (item.second == nullptr || item.second->IsBadPort()) {
            UBS_VLOG_TRACE("Skip bad port, dev name (%s) eid (%d)", dev_name.c_str(), eid_index);
            continue;
        }

        /* add to candidate */
        if (item.first.first.find(my_preferred_udma_device_name) == std::string::npos) {
            item.second->SetAffinity(false);
            candidateOnDifferentChip.emplace_back(item.second);
            UBS_VLOG_TRACE("Added dev(%s) eid_idx(%d) into different chip candidate list", dev_name.c_str(), eid_index);
        } else {
            item.second->SetAffinity(true);
            candidateOnSameChip.emplace_back(item.second);
            UBS_VLOG_TRACE("Added dev(%s) eid_idx(%d) into same chip candidate list", dev_name.c_str(), eid_index);
        }
    }
    sorted_contexts_.swap(candidateOnSameChip);
    sorted_contexts_.insert(sorted_contexts_.end(),
                           candidateOnDifferentChip.begin(), candidateOnDifferentChip.end());
}

ALWAYS_INLINE Result UrmaContextPool::GetContext(const std::string &devName, uint32_t eidIndex,
                                                 UrmaContextPtr &ctx) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (UNLIKELY(!inited_)) {
        UBS_VLOG_DEBUG("UrmaContextPool not initialized");
        return UBS_ERROR;
    }

    auto iter = contexts_.find(std::make_pair(devName, eidIndex));
    if (iter != contexts_.end()) {
        ctx = iter->second;
        return UBS_OK;
    }

    return UBS_ERROR;
}

ALWAYS_INLINE std::map<std::pair<std::string, uint32_t>, UrmaContextPtr> UrmaContextPool::Contexts() noexcept
{
    std::lock_guard<std::mutex> guard(mutex_);
    return contexts_;
}

ALWAYS_INLINE std::vector<UrmaContextPtr> UrmaContextPool::SortedContexts() noexcept
{
    std::lock_guard<std::mutex> guard(mutex_);
    return sorted_contexts_;
}

ALWAYS_INLINE int16_t UrmaContextPool::GetMyChipId() noexcept
{
    int logic_cpu_id = 0;
    /* step1: get logic cpu id that this thread running with */
    std::string path = "/proc/self/stat";
    std::ifstream fs_logic_pid(path);
    if (!fs_logic_pid.is_open()) {
        UBS_VLOG_INFO("Failed to get my chip id as open file (%s) failed", path.c_str());
        return -1;
    }

    /* read the content into line string */
    std::string line;
    std::getline(fs_logic_pid, line);

    /* read field one by one */
    std::istringstream iss(line);
    std::string tmp;

    /* the 38 field is the last logic cpu id that scheduled */
    for (int i = 0; i <= 38; ++i) {
        iss >> tmp;
    }
    logic_cpu_id = std::atoi(tmp.c_str());

    UBS_VLOG_TRACE("this thread running on logic cpu id(%d)", logic_cpu_id);

    /* step2: get physical socket id by logic cpu id */
    path = "/sys/devices/system/cpu/cpu" + std::to_string(logic_cpu_id) + "/topology/physical_package_id";
    std::ifstream fs_chip(path);
    if (!fs_chip.is_open()) {
        UBS_VLOG_INFO("Failed to get my chip id as open file (%s) failed", path.c_str());
        return -1;
    }

    int physical_cpu_id = -1;
    fs_chip >> physical_cpu_id;
    fs_chip.close();

    UBS_VLOG_TRACE("this thread running on physical cpu id(%d)", physical_cpu_id);

    return physical_cpu_id;
}

inline UrmaContextPtr UrmaContextPool::SelectContext(UrmaPortSelectionPolicy policy, int32_t *selected_index,
                                                      const UrmaContextHealthEntry *context_health,
                                                      uint32_t context_count) noexcept
{
    uint32_t context_size = sorted_contexts_.size();
    if (context_health != nullptr && context_count > 0) {
        context_size = std::min(context_size, context_count);
    }

    /* local selection by policy */
    std::vector<UrmaContextPtr> optionalContexts;
    std::vector<int32_t> contextsIndexes;
    optionalContexts.reserve(context_size);
    std::lock_guard<std::mutex> guard(mutex_);
    for (uint32_t i = 0; i < context_size; ++i) {
        auto ctx = sorted_contexts_[i];
        bool isBad = ctx->IsBadPort();
        if (context_health != nullptr && i < context_count) {
            UBS_VLOG_TRACE("Context select info, peer_eid_index: %d, peer_context_bad: %d, local_eid_index: %d, local_context_bad: %d",
                           context_health[i].eid_index, context_health[i].is_bad, ctx->EidInfo().eid_index, isBad);
            if (context_health[i].is_bad) {
                continue;
            }
        }
        if (ctx->IsAffinity() && !isBad) {
            contextsIndexes.emplace_back(i);
            optionalContexts.emplace_back(ctx);
        } else if (!ctx->IsAffinity() && !optionalContexts.empty()) {
            break;
        } else if (!isBad) {
            contextsIndexes.emplace_back(i);
            optionalContexts.emplace_back(ctx);
        }
    }
    if (UNLIKELY(optionalContexts.empty())) {
        return nullptr;
    }
    uint32_t pos;
    if (optionalContexts.size() == 1) {
        pos = 0;
    } else if (policy == PSP_ROUND_ROBIN) {
        pos = seq_.fetch_add(1) % optionalContexts.size();
    } else if (policy == PSP_RANDOM) {
        pos = Func::SecureRandUInt32() % optionalContexts.size();
    } else {
        UBS_VLOG_ERR("Invalid policy, un-reachable path");
        return nullptr;
    }
    if (selected_index != nullptr) {
        *selected_index = contextsIndexes[pos];
    }
    UBS_VLOG_INFO("Selected context success, pos: %d, selected_index: %d, peer_eid: %d, local_eid: %d",
                  pos, *selected_index, context_health[*selected_index].eid_index, optionalContexts[pos]->EidInfo().eid_index);
    return optionalContexts[pos];
}
} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_CONTEXT_POOL_H
