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

#ifndef UBS_COMM_UMQ_TP_TX_EPOLL_RUNNER_OPS_H
#define UBS_COMM_UMQ_TP_TX_EPOLL_RUNNER_OPS_H

#include "core/ubsocket_event_epoll.h"

namespace ock {
namespace ubs {
namespace umq {

class UmqTpTxEpollRunnerOps : public EpollRunnerOps {
public:
    struct TpTxExtContext : public ExtContext {
        uint32_t tp_idx;
    };

    struct TxEpollEvent {
        uint64_t type;
        uint64_t umq_handle;
        uint32_t tp_idx;
        int timer_fd;
    };

    UmqTpTxEpollRunnerOps()
    {
        mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
    }
    ~UmqTpTxEpollRunnerOps()
    {
        LockRegistry::LOCK_OPS.destroy(mutex_);
    }

    /**
     * @brief process epoll_wait event
     * @param event event to process
     */
    int ProcessOneEvent(const struct epoll_event &event) override;

    int AddEventToRunner(int epoll_fd, int fd, struct epoll_event *event, ExtContext *ctx) override;

    int DelEpollEvent(int epoll_fd, int fd) override;

    ALWAYS_INLINE bool IsSocketEventDataExist(int fd) noexcept
    {
        return socket_data_.find(fd) != socket_data_.end();
    }

    ALWAYS_INLINE bool InsertSocketEventData(int fd, TxEpollEvent *data) noexcept
    {
        auto pos = socket_data_.find(fd);
        if (UNLIKELY(pos != socket_data_.end())) {
            return false;
        }
        socket_data_.emplace(fd, data);
        return true;
    }

    ALWAYS_INLINE bool RemoveSocketEventData(int fd) noexcept
    {
        auto pos = socket_data_.find(fd);
        if (UNLIKELY(pos == socket_data_.end())) {
            return false;
        }
        auto removed = pos->second;
        socket_data_.erase(pos);
        if (removed != nullptr) {
            delete removed;
        }
        return true;
    }

    ALWAYS_INLINE TxEpollEvent *GetSocketEventData(int fd) noexcept
    {
        auto pos = socket_data_.find(fd);
        if (UNLIKELY(pos == socket_data_.end())) {
            return nullptr;
        }
        auto res = pos->second;
        return res;
    }

private:
    u_mutex_t *mutex_{nullptr};
    std::unordered_map<int, TxEpollEvent *> socket_data_;
};

} // namespace umq
} // namespace ubs
} // namespace ock
#endif // UBS_COMM_UMQ_TP_TX_EPOLL_RUNNER_OPS_H
