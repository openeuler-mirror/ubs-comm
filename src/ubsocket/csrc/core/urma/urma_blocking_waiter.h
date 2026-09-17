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
#ifndef UBS_COMM_URMA_BLOCKING_WAITER_H
#define UBS_COMM_URMA_BLOCKING_WAITER_H

#include <cstdint>
#include <ctime>

#include "urma_socket.h"

namespace ock {
namespace ubs {
namespace urma {

/*
 * Result of one wait round. errno stays with the caller: the same closure maps to EPIPE for a
 * blocked writev() and to EBADF/ECONNRESET for a blocked readv().
 */
enum class WaitOutcome
{
    RETRY,   /* registered, or the interest flag fired: call the ops again */
    TIMEOUT, /* SO_SNDTIMEO/SO_RCVTIMEO budget expired */
    LOCAL_CLOSED,
    PEER_CLOSED,
};

/*
 * Blocking-wait bookkeeping shared by UrmaTxOps::WriteV() and UrmaRxOps::ReadV(): keeps the waiter
 * registration and the SO_*TIMEO budget across the attempts of one call, so the two loops only
 * differ in the flag they wait for. The destructor unregisters the waiter.
 */
class BlockingWaiter {
public:
    BlockingWaiter(UrmaSocket *sock, uint32_t interestFlags, int timeoutOptName) noexcept;
    ~BlockingWaiter() noexcept;

    BlockingWaiter(const BlockingWaiter &) = delete;
    BlockingWaiter &operator=(const BlockingWaiter &) = delete;

    /*
     * One wait round: the first call only registers the waiter and reports RETRY so the caller
     * retries the ops once (eats state changes that raced the registration), later calls sleep
     * until the interest flag fires (RETRY) or the deadline (TIMEOUT) or a close is seen.
     */
    WaitOutcome Wait() noexcept;

private:
    UrmaSocket *sock_;
    uint32_t interestFlags_;
    int timeoutOptName_;
    bool registered_ = false;     /* waiter registered on the socket, unregistered by the destructor */
    bool timeoutQueried_ = false; /* SO_*TIMEO is read once per call, not per attempt */
    bool hasDeadline_ = false;
    struct timespec deadline_ = {0, 0};
};

} // namespace urma
} // namespace ubs
} // namespace ock
#endif // UBS_COMM_URMA_BLOCKING_WAITER_H
