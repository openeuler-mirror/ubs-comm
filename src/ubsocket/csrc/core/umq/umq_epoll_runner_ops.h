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
#ifndef UBS_COMM_UMQ_EPOLL_RUNNER_OPS_H
#define UBS_COMM_UMQ_EPOLL_RUNNER_OPS_H

#include "core/ubsocket_event_epoll.h"
#include "core/ubsocket_socket.h"
#include "umq_types.h"

namespace ock {
namespace ubs {
namespace umq {

class UmqEpollRunnerOps : public EpollRunnerOps {
public:
    UmqEpollRunnerOps() = default;
    virtual ~UmqEpollRunnerOps() = default;

    /**
     * @brief process epoll_wait event
     * @param event event to process
     */
    int ProcessOneEvent(const struct epoll_event &event) override;

    int ProcessShareJfrEvent(const struct epoll_event &event, uint64_t main_umq);

    int ProcessMainUmqRearm(uint64_t main_umq);

    std::unordered_set<Socket *> SiftSocketEventsWithUmqBuffers(umq_buf_t **buf, int count);

private:
    uint32_t event_num_{ 0 };
};
} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_EPOLL_RUNNER_H