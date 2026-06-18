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

#ifndef UBS_COMM_UMQ_TP_EVENT_EPOLL_RUNNER_OPS_H
#define UBS_COMM_UMQ_TP_EVENT_EPOLL_RUNNER_OPS_H

#include "core/ubsocket_event_epoll.h"

namespace ock {
namespace ubs {
namespace umq {

class UmqTpEventEpollRunnerOps : public EpollRunnerOps {
public:
    UmqTpEventEpollRunnerOps()
    {
        mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
    }
    ~UmqTpEventEpollRunnerOps()
    {
        LockRegistry::LOCK_OPS.destroy(mutex_);
    }

    /**
     * @brief process epoll_wait event
     * @param event event to process
     */
    int ProcessOneEvent(const struct epoll_event &event) override;

    int AddEventToRunner(int epoll_fd, int fd, struct epoll_event *event, ExtContext *ctx) override;

private:
    u_mutex_t *mutex_{nullptr};
};

} // namespace umq
} // namespace ubs
} // namespace ock
#endif // UBS_COMM_UMQ_TP_EVENT_EPOLL_RUNNER_OPS_H
