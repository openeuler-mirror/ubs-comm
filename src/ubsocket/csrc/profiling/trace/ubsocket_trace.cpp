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
#include <pthread.h>
#include <semaphore.h>
#include <chrono>

#include "common/ubsocket_global_setting.h"
#include "core/ubsocket_core_types.h"
#include "ubsocket_trace.h"

namespace ock {
namespace ubs {
u_external_rpc_id_ops_t TraceRegistry::RPC_ID_OPS;

Result TraceRegistry::RegisterRpcIdOps(u_external_rpc_id_ops_t *ops)
{
    if (!ops || !ops->get_rpc_id || !ops->get_rpc_call_timestamp) {
        return UBS_INVALID_PARAM;
    }

    RPC_ID_OPS = *ops;
    return 0;
}

TracePrintThread &TracePrintThread::Instance()
{
    static TracePrintThread instance;
    return instance;
}

void TracePrintThread::Start()
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        UBS_VLOG_WARN("TracePrintThread already running, skip Start()\n");
        return;
    }
    UBS_VLOG_DEBUG("TracePrintThread starting...\n");
    thread_ = std::thread(&TracePrintThread::Run, this);
}

void TracePrintThread::Stop()
{
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        UBS_VLOG_WARN("TracePrintThread not running, skip Stop()\n");
        return;
    }
    UBS_VLOG_DEBUG("TracePrintThread stopping...\n");
    if (thread_.joinable()) {
        thread_.join();
    }
    UBS_VLOG_DEBUG("TracePrintThread stopped\n");
}

void TracePrintThread::Run()
{
    pthread_setname_np(pthread_self(), "ubs_trace");
    UBS_VLOG_DEBUG("TracePrintThread running, tid: %lu, drain_interval: %u ms\n", pthread_self(),
                   GlobalSetting::UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS);
    while (running_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(GlobalSetting::UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS));
        DrainAllSockets();
    }
    DrainAllSockets();
}

void TracePrintThread::DrainAllSockets()
{
    int socket_count = 0;
    ArraySet<Socket>::GetInstance().ForEach([&socket_count](int, Socket *sock) {
        socket_count++;
        if (sock->split_trace_ != nullptr) {
            sock->split_trace_->DrainAndPrint();
        }
    });
}
} // namespace ubs
} // namespace ock
