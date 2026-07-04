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

#ifndef UBS_COMM_UMQ_TP_WAIT_QUEUE_H
#define UBS_COMM_UMQ_TP_WAIT_QUEUE_H

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_leaky_singleton.h"
#include "common/ubsocket_spsc_ring_queue.h"
#include "umq_socket.h"

namespace ock {
namespace ubs {
namespace umq {

class UmqTpWaitQueue
    : public LeakySingleton<UmqTpWaitQueue>
    , public SPSCRingQueue<SocketPtr> {
    friend LeakySingleton<UmqTpWaitQueue>;

public:
    static constexpr size_t DEFAULT_CAPACITY = 1024;

    int Enqueue(const SocketPtr &sock);

    int TryWakeupOne();

    uint32_t WakeUp(uint32_t wakeUpNum);

private:
    UmqTpWaitQueue() : SPSCRingQueue(GetCapacity()) {}

    static size_t GetCapacity()
    {
        size_t cap = UmqSetting::UMQ_SHARE_JFR_RX_QUEUE_DEPTH;
        cap = (cap <= 1) ? 1 : size_t(1) << (sizeof(size_t) * 8 - __builtin_clzl(cap - 1));
        if (cap == 0) {
            return DEFAULT_CAPACITY;
        }
        return cap;
    }
};

} // namespace umq
} // namespace ubs
} // namespace ock

#endif //UBS_COMM_UMQ_TP_WAIT_QUEUE_H
