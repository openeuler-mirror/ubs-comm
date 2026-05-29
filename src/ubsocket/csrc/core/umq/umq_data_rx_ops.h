/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef UBS_COMM_UMQ_DATA_RX_H
#define UBS_COMM_UMQ_DATA_RX_H

#include <cstdint>
#include <cstdlib>

#include "core/ubsocket_data_rx.h"
#include "common/ubsocket_qbuf_queue.h"
#include "umq_backend.h"
#include "umq_setting.h"

namespace ock {
namespace ubs {
namespace umq {

class UmqRxOps : public DataRxOps {
public:
    explicit UmqRxOps(int fd, uint64_t umq_handle = UMQ_INVALID_HANDLE) : local_umqh_(umq_handle)
    {
        fd_ = fd;
    }

    ~UmqRxOps() override = default;

    int PollRx(const SocketPtr &sock) override;

    int RearmRxInterrupt() override;

    void HandleErrorRxCqe(umq_buf_t *buf);

    void FlushRx(const SocketPtr &sock, uint32_t timeout_ms = FLUSH_TIMEOUT_MS);

private:
    int GetQbuf(const SocketPtr &sock, umq_buf_t **buf, int max_num);
    int UmqPollAndRefillRx(umq_buf_t **buf, uint32_t max_buf_size);
    uint32_t HandleBadQBuf(umq_buf_t *head_qbuf, umq_buf_t *bad_qbuf);
    int GetAndPopQbuf(umq_buf_t **buf, uint32_t max_buf_size);
    int GetAndAckEvent();
    void *PtrFloorToBoundary(void *ptr) override;
    bool PollSubUmqRx(umq_buf_t *buf[], int i) const;

private:
    // umq 相关的句柄
    uint64_t local_umqh_ = UMQ_INVALID_HANDLE;
};
} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_DATA_RX_H