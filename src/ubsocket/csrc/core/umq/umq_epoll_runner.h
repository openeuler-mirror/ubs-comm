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
#ifndef UBS_COMM_UMQ_EPOLL_RUNNER_H
#define UBS_COMM_UMQ_EPOLL_RUNNER_H

#include "ubsocket_event_epoll.h"
#include "ubsocket_socket.h"
#include "umq_setting.h"

namespace ock {
namespace ubs {
namespace umq {
class UmqEpollRunner : public EpollRunner {
public:
    /**
     * @brief add epoll_event to EpollRunner
     * @param socket_fd socket fd added
     * @param event event of socket fd
     * @return int -1: failed; 0: success
     */
    int AddEpollEvent(const Socket *const socket, struct epoll_event *event) override;

    /**
     * @brief delete epoll_event from EpollRunner
     * @param socket_fd socket fd removed
     * @return int -1: failed; 0: success
     */
    int RemoveEpollEvent(const Socket *const socket) override;

    /**
     * @brief process epoll_wait event
     * @param event event to process
     */
    void ProcessOneEvent(const struct epoll_event &event) override;

private:
    std::unordered_map<int, uint64_t> jfr_main_umq_;
};
} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_EPOLL_RUNNER_H