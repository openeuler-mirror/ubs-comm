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
#include "urma_blocking_waiter.h"

#include "common/ubsocket_defines.h"

namespace ock {
namespace ubs {
namespace urma {

BlockingWaiter::BlockingWaiter(UrmaSocket *sock, uint32_t interestFlags, int timeoutOptName) noexcept
    : sock_(sock),
      interestFlags_(interestFlags),
      timeoutOptName_(timeoutOptName)
{
}

BlockingWaiter::~BlockingWaiter() noexcept
{
    if (registered_) {
        sock_->UnregisterBlockingWaiter();
    }
}

WaitOutcome BlockingWaiter::Wait() noexcept
{
    if (UNLIKELY(sock_->State() == SOCK_STAT_CLOSE)) {
        return WaitOutcome::LOCAL_CLOSED;
    }
    if (!timeoutQueried_) {
        hasDeadline_ = sock_->GetBlockTimeoutDeadline(timeoutOptName_, deadline_);
        timeoutQueried_ = true;
    }
    if (!registered_) { /* retry once after registering to eat state changes that raced registration */
        sock_->RegisterBlockingWaiter();
        registered_ = true;
        return WaitOutcome::RETRY;
    }

    const uint32_t signalled = sock_->WaitBlockingSignal(interestFlags_, hasDeadline_ ? &deadline_ : nullptr);
    if ((signalled & BLOCKING_FLAG_CLOSED) != 0) {
        return WaitOutcome::LOCAL_CLOSED;
    }
    if ((signalled & BLOCKING_FLAG_PEER_CLOSED) != 0) {
        return WaitOutcome::PEER_CLOSED;
    }
    return (signalled == 0) ? WaitOutcome::TIMEOUT : WaitOutcome::RETRY;
}

} // namespace urma
} // namespace ubs
} // namespace ock
