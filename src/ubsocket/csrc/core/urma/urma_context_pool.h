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

struct UrmaBadPortRecord {
    int16_t last_error = 0;
    uint64_t error_occurred_time = 0;
};

class UrmaPortSelector {
public:
    /**
     * @brief Select a port by policy
     *
     * @param affinityFirst   [in] first consider affinity
     * @param preferredChipId [in] preferred chip id, only enabled when affinityFirst is true
     * @param policy          [in] policy
     * @return index
     */
    static UrmaContextPtr SelectPort(const UrmaContextPool &pool, bool affinityFirst, int16_t preferredChipId = -1,
                                     UrmaPortSelectionPolicy policy = PSP_ROUND_ROBIN) noexcept;

    /**
     * @brief Get the chip id that current thread running on
     *
     * @return chip id
     */
    static int16_t GetMyChipId() noexcept;

private:
    static UrmaContextPtr RoundRobin(const UrmaContextPool &pool, bool affinity, int16_t preferredChipId) noexcept;
    static UrmaContextPtr Random(const UrmaContextPool &pool, bool affinity, int16_t preferredChipId) noexcept;
    static void GetCandidates(const UrmaContextPool &pool, int16_t preferredChipId,
                              std::vector<UrmaContextPtr> &candidateOnSameChip,
                              std::vector<UrmaContextPtr> &candidateOnDifferentChip);
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
     * @brief Get the best context
     * 
     * @param ctx          [out] the context got
     * 
     * @return UBS_OK if got good context 
     */
    Result GetBestContext(UrmaContextPtr &ctx) noexcept;

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
     * @brief Record port as bad one
     *
     * @param devName      [in] name of the device
     * @param eidIndex     [in] index of eid
     * @param errorCode    [in] error code
     */
    void RecordBadPort(const std::string &devName, uint32_t eidIndex, int16_t errorCode) noexcept
    {
        // TODO
    }

    /**
     * @param Check if the port is error one
     *
     * @param devName      [in] device name
     * @param eidIndex     [in] index of eid
     * @return true if bad port
     */
    bool IsBadPort(const std::string &devName, uint32_t eidIndex) noexcept;

private:
    std::mutex mutex_;                                                        /* mutex for protect all members */
    bool inited_ = false;                                                     /* init flag */
    UrmaPortSelectionPolicy select_policy_ = PSP_ROUND_ROBIN;                 /* select port policy */
    int16_t my_chip_id_ = 0;                                                  /* which chip this process running on */
    std::map<std::pair<std::string, uint32_t>, UrmaContextPtr> contexts_;     /* eidIndex to context map */
    std::map<std::pair<std::string, uint32_t>, UrmaBadPortRecord> bad_ports_; /* bad port records */
    const bool SKIP_PRIMARY_EID = true;                                       /* skip primary eid */

    friend class UrmaPortSelector;
};
using UrmaContextPoolPtr = Ref<UrmaContextPool>;

/***********************************************************************************/
ALWAYS_INLINE bool UrmaContextPool::IsBadPort(const std::string &devName, uint32_t eidIndex) noexcept
{
    return bad_ports_.find(std::pair<std::string, uint32_t>(devName, eidIndex)) != bad_ports_.end();
}

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

    my_chip_id_ = UrmaPortSelector::GetMyChipId();
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

    contexts_.clear();
    bad_ports_.clear();

    inited_ = false;
}

ALWAYS_INLINE Result UrmaContextPool::GetBestContext(UrmaContextPtr &ctx) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (UNLIKELY(!inited_)) {
        UBS_VLOG_DEBUG("UrmaContextPool not initialized");
        return UBS_ERROR;
    }

    auto chosen_ctx = UrmaPortSelector::SelectPort(*this, true, -1, select_policy_);
    if (chosen_ctx != nullptr) {
        UBS_VLOG_TRACE("Choose context dev name (%s) eid index (%d)",
                       (chosen_ctx->Device() != nullptr ? chosen_ctx->Device()->DeviceName().c_str() : "null"),
                       chosen_ctx->EidInfo().eid_index);
        ctx = chosen_ctx;
        return UBS_OK;
    }

    UBS_VLOG_ERR("Failed to find any urma context in pool, context map size: %d", contexts_.size());
    return UBS_ERROR;
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

/***********************************************************************************/

ALWAYS_INLINE UrmaContextPtr UrmaPortSelector::SelectPort(const UrmaContextPool &pool, bool affinityFirst,
                                                          int16_t preferredChipId,
                                                          UrmaPortSelectionPolicy policy) noexcept
{
    UBS_VLOG_TRACE("Select port with param affinityFirst(%d), preferredChipId(%d), policy(%d)", affinityFirst,
                   preferredChipId, policy);

    /* case1: no context */
    if (UNLIKELY(pool.contexts_.size() == 0)) {
        return nullptr;
    }

    /* case2: only one context */
    if (pool.contexts_.size() == 1) {
        return pool.contexts_.begin()->second;
    }

    /* case3: based on policy */
    if (policy == PSP_ROUND_ROBIN) {
        return RoundRobin(pool, affinityFirst, preferredChipId);
    } else if (policy == PSP_RANDOM) {
        return Random(pool, affinityFirst, preferredChipId);
    } else {
        UBS_VLOG_ERR("Invalid policy, un-reachable path");
        return nullptr;
    }
}

ALWAYS_INLINE void UrmaPortSelector::GetCandidates(const UrmaContextPool &pool, int16_t preferredChipId,
                                                   std::vector<UrmaContextPtr> &candidateOnSameChip,
                                                   std::vector<UrmaContextPtr> &candidateOnDifferentChip)
{
    const auto chip_id = (preferredChipId == -1) ? pool.my_chip_id_ : preferredChipId;
    const std::string my_preferred_udma_device_name = "udmac" + std::to_string(chip_id);
    UBS_VLOG_TRACE("preferred device prefix(%s), context pool size(%d),  preferred chip id(%d)",
                   my_preferred_udma_device_name.c_str(), pool.contexts_.size(), preferredChipId);

    candidateOnSameChip.reserve(pool.contexts_.size());
    candidateOnDifferentChip.reserve(pool.contexts_.size());

    uint32_t first_eid_index = UINT32_MAX;
    for (auto &item : pool.contexts_) {
        const auto dev_name = item.first.first;
        const auto eid_index = item.first.second;
        /* skip the first one */
        if (pool.SKIP_PRIMARY_EID && first_eid_index == UINT32_MAX) {
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
        if (pool.bad_ports_.find(std::pair<std::string, uint32_t>(dev_name, eid_index)) != pool.bad_ports_.end()) {
            UBS_VLOG_TRACE("Skip bad port, dev name (%s) eid (%d)", dev_name.c_str(), eid_index);
            continue;
        }

        /* add to candidate */
        if (item.first.first.find(my_preferred_udma_device_name) == std::string::npos) {
            candidateOnDifferentChip.emplace_back(item.second);
            UBS_VLOG_TRACE("Added dev(%s) eid_idx(%d) into different chip candidate list", dev_name.c_str(), eid_index);
        } else {
            candidateOnSameChip.emplace_back(item.second);
            UBS_VLOG_TRACE("Added dev(%s) eid_idx(%d) into same chip candidate list", dev_name.c_str(), eid_index);
        }
    }
}

ALWAYS_INLINE UrmaContextPtr UrmaPortSelector::RoundRobin(const UrmaContextPool &pool, bool affinityFirst,
                                                          int16_t preferredChipId) noexcept
{
    static uint32_t ROUND_ROBIN_INDEX = 0;
    const auto this_round = ROUND_ROBIN_INDEX++;

    /* get candidate */
    std::vector<UrmaContextPtr> candidate_on_same_chip;
    std::vector<UrmaContextPtr> candidate_on_different_chip;
    GetCandidates(pool, preferredChipId, candidate_on_same_chip, candidate_on_different_chip);

    /* do round-robin in on same chip candidates */
    if (!candidate_on_same_chip.empty()) {
        return candidate_on_same_chip.at(this_round % candidate_on_same_chip.size());
    }

    /* do round-robin on different chip candidate */
    if (!candidate_on_different_chip.empty()) {
        return candidate_on_different_chip.at(this_round % candidate_on_different_chip.size());
    }

    return nullptr;
}

ALWAYS_INLINE UrmaContextPtr UrmaPortSelector::Random(const UrmaContextPool &pool, bool affinityFirst,
                                                      int16_t preferredChipId) noexcept
{
    const auto randon = Func::SecureRandUInt32();

    /* get candidate */
    std::vector<UrmaContextPtr> candidate_on_same_chip;
    std::vector<UrmaContextPtr> candidate_on_different_chip;
    GetCandidates(pool, preferredChipId, candidate_on_same_chip, candidate_on_different_chip);

    /* do round-robin in on same chip candidates */
    if (!candidate_on_same_chip.empty()) {
        return candidate_on_same_chip.at(randon % candidate_on_same_chip.size());
    }

    /* do round-robin on different chip candidate */
    if (!candidate_on_different_chip.empty()) {
        return candidate_on_different_chip.at(randon % candidate_on_different_chip.size());
    }

    return nullptr;
}

ALWAYS_INLINE int16_t UrmaPortSelector::GetMyChipId() noexcept
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
} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_CONTEXT_POOL_H
