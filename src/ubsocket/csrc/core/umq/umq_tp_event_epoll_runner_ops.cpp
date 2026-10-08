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

#include "umq_tp_event_epoll_runner_ops.h"
#include "umq_errno_converter.h"
#include "umq_tp_wait_queue.h"
#include "under_api/dl_umq_api.h"

namespace ock {
namespace ubs {
namespace umq {

int UmqTpEventEpollRunnerOps::ProcessOneEvent(const struct epoll_event &event)
{
    RunnerEventData event_data{};
    event_data.u64 = event.data.u64;

    if (event_data.event_data.type == RUNNER_EVENT_TYPE_TP_EVENT) {
        Locker slock(mutex_);
        uint64_t cnt;
        if (eventfd_read(event_data.event_data.data, &cnt) == -1) {
            char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
            UBS_VLOG_ERR("eventfd_read() failed, fd: %d, errno: %d, errmsg: %s\n", event_data.event_data.data, errno,
                         strerror(errno));
        }
        UmqTpWaitQueue::Instance().TryWakeupOne();
        return UBS_OK;
    } else {
        UBS_VLOG_ERR("async_epoll unknown event:(events:%x, data.type:%lu)\n", event.events,
                     event_data.event_data.type);
        return UBS_ERROR;
    }
}

int UmqTpEventEpollRunnerOps::AddEventToRunner(int epoll_fd, int fd, struct epoll_event *event, ExtContext *ctx)
{
    Locker sLock(mutex_);
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, event) < 0) {
        return UBS_ERROR;
    }
    return UBS_OK;
}

} // namespace umq
} // namespace ubs
} // namespace ock