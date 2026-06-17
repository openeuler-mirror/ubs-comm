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
struct UmqPollTraceTime {
    uint64_t umq_poll_start_timestamp_;
    uint64_t umq_poll_end_timestamp_;
    uint64_t umq_rearm_start_timestamp_;
    uint64_t umq_rearm_end_timestamp_;
    uint64_t umq_alloc_start_timestamp_;
    uint64_t umq_alloc_end_timestamp_;
    uint64_t umq_post_start_timestamp_;
    uint64_t umq_post_end_timestamp_;
    uint64_t process_share_jfr_end_timestamp_;
};

class UmqEpollRunnerOps : public EpollRunnerOps {
public:
    UmqEpollRunnerOps()
    {
        mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
    }
    ~UmqEpollRunnerOps()
    {
        LockRegistry::LOCK_OPS.destroy(mutex_);
    }

    /**
     * @brief process epoll_wait event
     * @param event event to process
     */
    int ProcessOneEvent(const struct epoll_event &event) override;

    int ProcessShareJfrEvent(const struct epoll_event &event, uint64_t main_umq);

    int ProcessMainUmqRearm(uint64_t main_umq);

    std::unordered_set<Socket *> SiftSocketEventsWithUmqBuffers(umq_buf_t **buf, int count);

    int InsertJfrMainUmq(int share_jfr_fd, uint64_t main_umq, int epoll_fd, struct epoll_event *shared_jfr_event)
    {
        Locker sLock(mutex_);
        if (UNLIKELY(jfr_main_umq_.count(share_jfr_fd) == 0)) {
            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, share_jfr_fd, shared_jfr_event) < 0) {
                return -1;
            }
            jfr_main_umq_.emplace(share_jfr_fd, main_umq);
        }

        return 0;
    }

private:
    void HandleSubUmqPollBuffers(Socket *socketObject, umq_buf_t **buf, int pollNum);
    uint32_t event_num_{0};
    std::unordered_map<int, uint64_t> jfr_main_umq_{};
    u_mutex_t *mutex_{nullptr};
    UmqPollTraceTime traceTime_{};
};

} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_EPOLL_RUNNER_H