/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef OCK_HCOM_NET_LOAD_BALANCE_H
#define OCK_HCOM_NET_LOAD_BALANCE_H

#include "hcom.h"

namespace ock {
namespace hcom {
struct NetWorkerGroupLbInfo {
    uint16_t wrkCntInGrp = 0;    /* number of worker in this group */
    uint16_t wrkOffsetInAll = 0; /* the offset of first workers in total flat workers, as all workers in stored flat */
    uint16_t grpRRIdx = 0;       /* index counter for round-robin in one group */
    uint16_t wrkCntLimited = 0;  /* only limited number of worker can be chosen */
    std::vector<uint16_t> wrkLimited; /* limited workers, offset in this group */
};

class NetWorkerLB {
public:
    NetWorkerLB(const std::string &name, UBSHcomNetDriverLBPolicy policy, uint16_t wrkLimitedCnt)
        : mName(name), mPolicy(policy), mWorkerLimitedCnt(wrkLimitedCnt)
    {}

    ~NetWorkerLB() = default;

    /*
     * @brief Add worker groups
     */
    inline NResult AddWorkerGroups(const std::vector<std::pair<uint16_t, uint16_t>> &groups)
    {
        NN_ASSERT_LOG_RETURN(!groups.empty(), NN_INVALID_PARAM);
        time_t currentTime = time(nullptr);
        if (currentTime == -1) {
            NN_LOG_ERROR("Failed to get current time when adding worker groups");
            return NN_ERROR;
        }
        /* this srand value is not used for security related thing */
        srand(static_cast<int64_t>(currentTime));
        for (const auto &item : groups) {
            if (NN_LIKELY(item.second == 0)) {
                return NN_INVALID_PARAM;
            }

            AddWorkerGroup(item.first, item.second);
        }

        return NN_OK;
    }

    /*
     * @brief Choose a worker
     *
     * @param grpIdx           [in] group index transferred from client
     * @param peerIpPort       [in] ip and port new connection
     * @param flatWrkIdx       [out] index of the worker in the flatted workers
     *
     * @return true if chosen
     */
    inline bool ChooseWorker(uint16_t grpIdx, const std::string &peerIpPort, uint16_t &flatWrkIdx)
    {
        const uint16_t groupCount = mWrkGroups.size();
        if (NN_UNLIKELY(grpIdx >= groupCount)) {
            NN_LOG_ERROR("Invalid group no " << grpIdx << " from client " << peerIpPort << " in lb " << mName);
            return false;
        }

        /* if worker count is not equal to limited worker count */
        if (NN_UNLIKELY(mWrkGroups[grpIdx].wrkCntLimited != mWrkGroups[grpIdx].wrkCntInGrp)) {
            return ChooseWorkerLimited(grpIdx, peerIpPort, flatWrkIdx);
        }

        if (mPolicy == NET_ROUND_ROBIN) {
            auto innerIdx = __sync_fetch_and_add(&(mWrkGroups[grpIdx].grpRRIdx), 1) % mWrkGroups[grpIdx].wrkCntInGrp;
            flatWrkIdx = mWrkGroups[grpIdx].wrkOffsetInAll + innerIdx;
            return true;
        } else if (mPolicy == NET_HASH_IP_PORT) {
            auto innerIdx = std::hash<std::string> {}(peerIpPort) % mWrkGroups[grpIdx].wrkCntInGrp;
            flatWrkIdx = mWrkGroups[grpIdx].wrkOffsetInAll + innerIdx;
            return true;
        }

        NN_LOG_ERROR("Un-supported load balance policy");
        return false;
    }

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "name: " << mName << ", policy: " << UBSHcomNetDriverLBPolicyToString(mPolicy) <<
            ", choose-able-count: " << (mWorkerLimitedCnt == UINT16_MAX ? "all" : std::to_string(mWorkerLimitedCnt)) <<
            ", worker-groups: " << mWrkGroups.size();
        return oss.str();
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    inline bool ChooseWorkerLimited(uint16_t grpIdx, const std::string &peerIpPort, uint16_t &flatWrkIdx)
    {
        const uint16_t groupCount = mWrkGroups.size();
        if (NN_UNLIKELY(grpIdx >= groupCount)) {
            NN_LOG_ERROR("Invalid group Idx");
            return false;
        }

        if (mPolicy == NET_ROUND_ROBIN) {
            auto innerIdx = __sync_fetch_and_add(&(mWrkGroups[grpIdx].grpRRIdx), 1) % mWrkGroups[grpIdx].wrkCntLimited;
            flatWrkIdx = mWrkGroups[grpIdx].wrkOffsetInAll + mWrkGroups[grpIdx].wrkLimited[innerIdx];
            return true;
        } else if (mPolicy == NET_HASH_IP_PORT) {
            auto innerIdx = std::hash<std::string> {}(peerIpPort) % mWrkGroups[grpIdx].wrkCntLimited;
            flatWrkIdx = mWrkGroups[grpIdx].wrkOffsetInAll + mWrkGroups[grpIdx].wrkLimited[innerIdx];
            return true;
        }

        return false;
    }

    /*
     * @brief Added one worker group
     *
     * @param offsetWorker     [in] offset of the first worker's offset in flat workers
     * @param wrkCntInGrp      [in] the worker count in this group
     *
     */
    inline void AddWorkerGroup(uint16_t offsetWorker, uint16_t wrkCntInGrp)
    {
        NetWorkerGroupLbInfo info {};
        info.wrkCntInGrp = wrkCntInGrp;
        info.wrkOffsetInAll = offsetWorker;
        info.grpRRIdx = 0;
        info.wrkCntLimited = wrkCntInGrp;

        /*
         * worker number in group is large than limited number of worker
         * generate random offset
         */
        if (wrkCntInGrp > mWorkerLimitedCnt) {
            info.wrkCntLimited = mWorkerLimitedCnt;
            /* this rand value is not used for security related thing */
            auto randIndex = rand();
            for (uint16_t i = 0; i < mWorkerLimitedCnt; i++) {
                info.wrkLimited.emplace_back((randIndex + i) % wrkCntInGrp);
            }
        }

        mWrkGroups.push_back(info);
    }

private:
    std::string mName;
    UBSHcomNetDriverLBPolicy mPolicy = NET_ROUND_ROBIN;  /* policy */
    uint16_t mWorkerLimitedCnt = 0;               /* means this can be  less than total workers in one group */
    std::vector<NetWorkerGroupLbInfo> mWrkGroups; /* worker group info */
    std::vector<uint16_t> mEpCntPerWorkers;       /* for even distributed policy */

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};
using NetWorkerLBPtr = NetRef<NetWorkerLB>;
}
}

#endif // OCK_HCOM_NET_LOAD_BALANCE_H
