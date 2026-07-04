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

namespace ock {
namespace ubs {
namespace umq {

int UmqTpWaitQueue::Enqueue(const SocketPtr &sock)
{
    if (sock == nullptr) {
        UBS_VLOG_WARN("Sock is null, no need to wait jetty resource.");
        return UBS_ERROR;
    }
    UmqSocketPtr umqSock = RefConvert<Socket, UmqSocket>(sock);
    if (umqSock->GetJettyAllocState() == JettyAllocState::IDLE) {
        // sock状态为IDLE，入队
        if (Push(sock)) {
            umqSock->SetJettyAllocState(JettyAllocState::WAITING);
        } else {
            return UBS_ERROR;
        }
    }
    return UBS_OK;
}

int UmqTpWaitQueue::TryWakeupOne()
{
    SocketPtr sock;
    if (!Pop(sock)) {
        UBS_VLOG_ERR("Failed to wake up socket for available jetty.");
        return UBS_ERROR;
    }
    // optimize：sock状态更改为等待资源，并唤醒
    UmqSocketPtr umqSock = RefConvert<Socket, UmqSocket>(sock);
    umqSock->SetJettyAllocState(JettyAllocState::READY);
    return UBS_OK;
}

uint32_t UmqTpWaitQueue::WakeUp(uint32_t wakeUpNum)
{
    uint64_t queue_size = Size();
    if (wakeUpNum > Size()) {
        UBS_VLOG_DEBUG("Failed to wake up socket caused by invalid cnt(%u) but size(%u).\n", wakeUpNum, queue_size);
        return 0;
    }
    SocketPtr wakeUpBatch[wakeUpNum];
    auto count = MultiPop(wakeUpBatch, wakeUpNum);
    for (uint32_t i = 0; i < count; ++i) {
        SocketPtr sock = wakeUpBatch[i];
        // optimize：sock状态更改为等待资源，并唤醒
        UmqSocketPtr umqSock = RefConvert<Socket, UmqSocket>(sock);
        umqSock->SetJettyAllocState(JettyAllocState::READY);
    }
    return static_cast<uint32_t>(count);
}

} // namespace umq
} // namespace ubs
} // namespace ock