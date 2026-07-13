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
#include "common/ubsocket_mpsc_ring_queue.h"
#include "umq_socket.h"

namespace ock {
namespace ubs {
namespace umq {

enum UmqTpWaitQueueElementType : uint64_t
{
    UMQ_SOCKET = 0,
    UMQ_HANDLE,
    TYPE_COUNT
};

class UmqTpWaitQueueElement {
public:
    UmqTpWaitQueueElement() : sock_(nullptr), umq_handle_(0), type_(TYPE_COUNT) {}

    UmqTpWaitQueueElement(const SocketPtr &sock) : sock_(sock), umq_handle_(UMQ_INVALID_HANDLE), type_(UMQ_SOCKET) {}

    UmqTpWaitQueueElement(uint64_t umq_handle) : sock_(nullptr), umq_handle_(umq_handle), type_(UMQ_HANDLE) {}

    UmqTpWaitQueueElement(UmqTpWaitQueueElement &&) noexcept = default;
    UmqTpWaitQueueElement &operator=(UmqTpWaitQueueElement &&) noexcept = default;
    UmqTpWaitQueueElement(const UmqTpWaitQueueElement &) = delete;
    UmqTpWaitQueueElement &operator=(const UmqTpWaitQueueElement &) = delete;

    ALWAYS_INLINE UmqTpWaitQueueElementType GetType()
    {
        return type_;
    }

    ALWAYS_INLINE SocketPtr GetSockPtr()
    {
        return sock_;
    }

    ALWAYS_INLINE uint64_t GetUmqHandle()
    {
        return umq_handle_;
    }

private:
    SocketPtr sock_;
    uint64_t umq_handle_;
    UmqTpWaitQueueElementType type_;
};

class UmqTpWaitQueue
    : public LeakySingleton<UmqTpWaitQueue>
    , public MPSCRingQueue<UmqTpWaitQueueElement> {
    friend LeakySingleton<UmqTpWaitQueue>;

public:
    static constexpr size_t DEFAULT_CAPACITY = 1024;

    int Enqueue(const SocketPtr &sock);

    int Enqueue(uint64_t umq_handle);

    int TryWakeupOne();

    uint32_t WakeUp(uint32_t wakeUpNum);

private:
    UmqTpWaitQueue() : MPSCRingQueue(GetCapacity()) {}

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
