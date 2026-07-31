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
     * @param eid_index    [in] index of eid
     * 
     * @return UBS_OK if no duplicated existed and inserted, 
     * UBS_ERROR return if already existed or already initialized
     */
    Result AddEidIndex(const std::string &devName, uint32_t eid_index) noexcept;

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
     * @param eid_index    [in] index of eid
     * @param ctx          [out] context got
     * @return UBS_OK if got, UBS_ERROR is not found
     * 
     */
    Result GetContext(const std::string &devName, uint32_t eid_index, UrmaContextPtr &ctx) noexcept;

private:
    std::mutex mutex_;                                                    /* mutex for protect all members */
    bool inited_ = false;                                                 /* init flag */
    int16_t my_chip_id_ = 0;                                              /* which chip this process running on */
    std::map<std::pair<std::string, uint32_t>, UrmaContextPtr> contexts_; /* eid_index to context map */

    friend class UrmaPortSelector;
};
using UrmaContextPoolPtr = Ref<UrmaContextPool>;

/***********************************************************************************/

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

        UBS_VLOG_TRACE("found bonding device (%s)", dev_name.c_str());
        auto &device = iter.second;
        if (UNLIKELY(device == nullptr)) {
            UBS_VLOG_ERR("Un-reachable path, device is null");
            continue;
        }

        auto &eid_list = device->EidList();
        for (auto &item : eid_list) {
            UBS_VLOG_TRACE("Adding device(%d) with eid index(%d)", dev_name.c_str(), item.eid_index);

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

ALWAYS_INLINE Result UrmaContextPool::AddEidIndex(const std::string &devName, uint32_t eid_index) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (inited_) {
        UBS_VLOG_ERR("Add eid index is NOT allowed after context pool initialized");
        return UBS_ERROR;
    }

    /* insert an empty context */
    if (contexts_.find(std::make_pair(devName, eid_index)) != contexts_.end()) {
        UBS_VLOG_DEBUG("Eid index already exists with dev name");
        return UBS_ERROR;
    }

    contexts_[std::make_pair(devName, eid_index)] = nullptr;
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

        UBS_VLOG_TRACE("created context for device name(%s) eid index(%d) ", iter.first.first.c_str(),
                       iter.first.second);
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

    inited_ = false;
}

ALWAYS_INLINE Result UrmaContextPool::GetBestContext(UrmaContextPtr &ctx) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (UNLIKELY(!inited_)) {
        UBS_VLOG_DEBUG("UrmaContextPool not initialized");
        return UBS_ERROR;
    }

    for (auto &iter : contexts_) {
        if (iter.second != nullptr) {
            ctx = iter.second;
            UBS_VLOG_DEBUG("Found the urma context with dev name: %s, eid index: %d", iter.first.first.c_str(),
                           iter.first.second);
            return UBS_OK;
        }
    }

    UBS_VLOG_ERR("Failed to find any urma context in pool, context map size: %d", contexts_.size());
    return UBS_ERROR;
}

ALWAYS_INLINE Result UrmaContextPool::GetContext(const std::string &devName, uint32_t eid_index,
                                                 UrmaContextPtr &ctx) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (UNLIKELY(!inited_)) {
        UBS_VLOG_DEBUG("UrmaContextPool not initialized");
        return UBS_ERROR;
    }

    auto iter = contexts_.find(std::make_pair(devName, eid_index));
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
    /* case1: no context */
    if (pool.contexts_.size() == 0) {
        return nullptr;
    }

    /* case2: only one context */
    if (pool.contexts_.size() == 1) {
        return pool.contexts_.begin()->second;
    }

    if (policy == PSP_ROUND_ROBIN) {
        return RoundRobin(pool, affinityFirst, preferredChipId);
    } else if (policy == PSP_RANDOM) {
        return Random(pool, affinityFirst, preferredChipId);
    } else {
        UBS_VLOG_ERR("Invalid policy, un-reachable path");
        return nullptr;
    }
}

ALWAYS_INLINE UrmaContextPtr UrmaPortSelector::RoundRobin(const UrmaContextPool &pool, bool affinityFirst,
                                                          int16_t preferredChipId) noexcept
{
    // TODO
    return nullptr;
}

ALWAYS_INLINE UrmaContextPtr UrmaPortSelector::Random(const UrmaContextPool &pool, bool affinityFirst,
                                                      int16_t preferredChipId) noexcept
{
    // TODO
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
