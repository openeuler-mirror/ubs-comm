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

#include "umq_tp_wait_queue.h"
#include "umq_tx_helper.h"

namespace ock {
namespace ubs {
namespace umq {

int UmqTpWaitQueue::Enqueue(const SocketPtr &sock)
{
    if (sock == nullptr) {
        UBS_VLOG_WARN("Sock is null, no need to wait jetty resource.\n");
        return UBS_ERROR;
    }
    UmqSocketPtr umqSock = RefConvert<Socket, UmqSocket>(sock);
    if (umqSock->TryAcquireForWaiting()) {
        UmqTpWaitQueueElement element(sock);
        if (Push(std::move(element))) {
            return UBS_OK;
        } else {
            umqSock->ResetToIdle();
            return UBS_ERROR;
        }
    }

    return UBS_OK;
}

int UmqTpWaitQueue::TryWakeupOne()
{
    UmqTpWaitQueueElement element;
    if (Empty()) {
        UBS_VLOG_DEBUG("[Debug] umq tp wait queue is empty.\n");
        return UBS_OK;
    }
    if (!Pop(element)) {
        UBS_VLOG_ERR("Failed to wake up socket for available jetty.\n");
        return UBS_ERROR;
    }

    if (element.GetType() == UMQ_SOCKET) {
        SocketPtr sock = element.GetSockPtr();
        // optimize：sock状态更改为等待资源，并唤醒
        UmqSocketPtr umqSock = RefConvert<Socket, UmqSocket>(sock);
        umqSock->ResetToIdle();
        if (!umqSock->RxQueueEmpty()) {
            umqSock->NotifyReadable();
        }
        UBS_VLOG_DEBUG("[Debug] umq tp wait queue wake up socket fd: %d.\n", sock->raw_socket_);
    } else if (element.GetType() == UMQ_HANDLE) {
        uint64_t umq_handle = element.GetUmqHandle();
        UmqTxHelper::PollUmqTxForFcReturn(umq_handle);
        UBS_VLOG_DEBUG("[Debug] umq tp wait queue wake up umq handle: %lu.\n", umq_handle);
    } else {
        UBS_VLOG_WARN("Unsupported element type.\n");
    }
    return UBS_OK;
}

uint32_t UmqTpWaitQueue::WakeUp(uint32_t wakeUpNum)
{
    if (wakeUpNum > UmqSetting::UMQ_TP_POOL_SIZE) {
        return 0;
    }
    std::vector<UmqTpWaitQueueElement> wakeUpBatch(wakeUpNum);
    auto count = MultiPop(wakeUpBatch.data(), wakeUpNum);
    for (uint32_t i = 0; i < count; ++i) {
        auto &element = wakeUpBatch[i];
        if (element.GetType() == UMQ_SOCKET) {
            SocketPtr sock = element.GetSockPtr();
            // optimize：sock状态更改为等待资源，并唤醒
            UmqSocketPtr umqSock = RefConvert<Socket, UmqSocket>(sock);
            umqSock->ResetToIdle();
            if (!umqSock->RxQueueEmpty()) {
                umqSock->NotifyReadable();
            }
            UBS_VLOG_DEBUG("[Debug] umq tp wait queue wake up socket fd: %d.\n", sock->raw_socket_);
        } else if (element.GetType() == UMQ_HANDLE) {
            uint64_t umq_handle = element.GetUmqHandle();
            UmqTxHelper::PollUmqTxForFcReturn(umq_handle);
            UBS_VLOG_DEBUG("[Debug] umq tp wait queue wake up umq handle: %lu.\n", umq_handle);
        } else {
            UBS_VLOG_WARN("Unsupported element type.\n");
        }
    }
    return static_cast<uint32_t>(count);
}

int UmqTpWaitQueue::Enqueue(const uint64_t umq_handle)
{
    if (umq_handle != UMQ_INVALID_HANDLE) {
        UmqTpWaitQueueElement element(umq_handle);
        if (!Push(std::move(element))) {
            return UBS_ERROR;
        }
    } else {
        UBS_VLOG_WARN("Invalid umq handle, no need to wait jetty resource.\n");
        return UBS_ERROR;
    }

    return UBS_OK;
}

} // namespace umq
} // namespace ubs
} // namespace ock