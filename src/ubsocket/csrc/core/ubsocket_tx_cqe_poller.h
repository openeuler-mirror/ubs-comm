/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef UBS_COMM_UBSOCKET_TX_CQE_POLLER_H
#define UBS_COMM_UBSOCKET_TX_CQE_POLLER_H

#include <atomic>
#include <thread>
#include <vector>

#include "common/ubsocket_leaky_singleton.h"
#include "ubsocket_core_types.h"

namespace ock {
namespace ubs {
class TxCqePoller : public LeakySingleton<TxCqePoller> {
    friend LeakySingleton<TxCqePoller>;

public:
    TxCqePoller(const TxCqePoller &) = delete;
    TxCqePoller &operator=(const TxCqePoller &) = delete;

    ~TxCqePoller();

    /// @brief Start the poller thread.
    /// @return 0 if successful, -1 otherwise.
    int Start();

    /// @brief Stop the poller thread.
    void Stop();

    /// @brief Add a socket to the detection queue.
    /// @param sock The socket pointer.
    void AddSocket(const SocketPtr &sock);

    /// @brief Remove a socket from the detection queue.
    /// @param sock The socket pointer.
    void DelSocket(const SocketPtr &sock);

private:
    TxCqePoller() = default;

    void RunInThread() noexcept;

private:
    std::thread poll_thread_;
    u_mutex_t *mutex_ = nullptr;
    std::vector<SocketPtr> sockets_;
    std::atomic<bool> stopped_{false};
    std::atomic<bool> started_{false};

    int epoll_fd_ = -1;
    int timer_fd_ = -1;
};

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_TX_CQE_POLLER_H
