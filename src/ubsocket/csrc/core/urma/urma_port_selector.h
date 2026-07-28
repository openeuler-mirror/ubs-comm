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

#ifndef UBS_COMM_URMA_PORT_SELECTOR_H
#define UBS_COMM_URMA_PORT_SELECTOR_H

#include "urma_socket_types.h"
#include "urma_wrapper.h"

namespace ock {
namespace ubs {
namespace urma {
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
    Result AutoAddBoundingDevIndex() noexcept;

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
    std::map<std::pair<std::string, uint32_t>, UrmaContextPtr> contexts_; /* eid_index to context map */
};
using UrmaContextPoolPtr = Ref<UrmaContextPool>;

class UrmaPortSelector : public Referable {
public:
    explicit UrmaPortSelector(const UrmaContextPoolPtr &contextPool) noexcept : contextPool_(contextPool) {}

    ~UrmaPortSelector() override = default;

    /**
     * @brief Select a port by policy
     *
     * @param policy       [in] policy
     * @return index
     */
    int16_t SelectPort(UrmaPortSelectionPolicy policy = PSP_ROUND_ROBIN) noexcept;

private:
    int16_t SelectWithRoundRobin() noexcept;
    int16_t SelectWithRandom() noexcept;
    int16_t CreatePortCpuRelationship() noexcept;

private:
    const UrmaContextPoolPtr &contextPool_;
    std::map<int16_t, int16_t> port_cpu_map_;
    std::mutex mutex_;
};
using UrmaPortSelectorPtr = Ref<UrmaPortSelector>;

/***********************************************************************************/

ALWAYS_INLINE Result UrmaContextPool::AutoAddBoundingDevIndex() noexcept
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

    /* get the first one */
    auto first = devices.begin();
    auto &dev_name = first->first;
    auto eid_index = 0;

    /* insert an empty context */
    if (contexts_.find(std::make_pair(dev_name, eid_index)) != contexts_.end()) {
        UBS_VLOG_DEBUG("Eid index already exists");
        return UBS_ERROR;
    }

    contexts_[std::make_pair(dev_name, eid_index)] = nullptr;

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
            continue;
        }

        /* create context */
        auto result = UrmaContext::CreateContext(iter.first.first, iter.first.second, iter.second);
        if (result != UBS_OK) {
            UBS_VLOG_ERR("Create urma context failed with dev name: %s, eid index: %d", iter.first.first.c_str(),
                         iter.first.second);
        }
    }

    inited_ = true;
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
ALWAYS_INLINE int16_t UrmaPortSelector::CreatePortCpuRelationship() noexcept
{
    if (UNLIKELY(!port_cpu_map_.empty())) {
        return 0;
    }

    // TODO

    return -1;
}

ALWAYS_INLINE int16_t UrmaPortSelector::SelectPort(UrmaPortSelectionPolicy policy) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = CreatePortCpuRelationship();
    if (UNLIKELY(result < 0)) {
        return result;
    }

    if (policy == PSP_ROUND_ROBIN) {
        return SelectWithRoundRobin();
    } else if (policy == PSP_RANDOM) {
        return SelectWithRandom();
    } else {
        UBS_VLOG_ERR("Invalid policy, un-reachable path");
        return -1;
    }
}

ALWAYS_INLINE int16_t UrmaPortSelector::SelectWithRoundRobin() noexcept
{
    // TODO
    return -1;
}

ALWAYS_INLINE int16_t UrmaPortSelector::SelectWithRandom() noexcept
{
    // TODO
    return -1;
}
} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_PORT_SELECTOR_H
