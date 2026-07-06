/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef UBS_COMM_UMQ_TRANSPORT_POOL_H
#define UBS_COMM_UMQ_TRANSPORT_POOL_H

#include "common/ubsocket_common_includes.h"
#include "include/ubsocket_def.h"
#include "umq_setting.h"

namespace ock {
namespace ubs {
namespace umq {

using TpIdx2FdMap = std::unordered_map<uint32_t, std::vector<int>>;
using Umqh2TpIdxMap = std::unordered_map<uint64_t, TpIdx2FdMap>;

class UmqTransportPool {
public:
    static UmqTransportPool &Instance()
    {
        static UmqTransportPool instance;
        return instance;
    }

    Result WarmUp(uint64_t main_umqh);

    Result CreatePool(uint64_t main_umqh, int pool_size);

    Result RebuildTp(uint64_t main_umqh, uint32_t old_tp_idx);

    Result Clean();

    size_t PoolSize(uint64_t main_umqh) const;

    UmqTransportPool(const UmqTransportPool &) = delete;
    UmqTransportPool &operator=(const UmqTransportPool &) = delete;

private:
    UmqTransportPool() : mutex_(LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE)) {}

    ~UmqTransportPool()
    {
        LockRegistry::LOCK_OPS.destroy(mutex_);
    }

    Result CreateOneTp(uint64_t main_umqh);

    Result AddPollTxEvent(uint64_t umq_handle);

    Result AddTransportEpollEvent(uint64_t umq_handle);

    u_mutex_t *mutex_;
    Umqh2TpIdxMap umq_tp_pool{};
    uint32_t aff_rr_num_;
    uint32_t rr_num_;
    umq_route_list_t route_list_tp_;
};

} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_TRANSPORT_POOL_H
