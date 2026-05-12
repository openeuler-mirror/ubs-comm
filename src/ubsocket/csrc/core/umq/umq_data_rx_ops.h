/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef UBS_COMM_UMQ_DATA_RX_H
#define UBS_COMM_UMQ_DATA_RX_H

#include <cstdlib>
#include <cstdint>
#include "../ubsocket_data_rx.h"
#include "../../common/ubsocket_defines.h"
#include "../../../hcom/umq/include/umq/umq_api.h"
#include "../../../hcom/umq/include/umq/umq_types.h"
#include "../../../hcom/umq/include/umq/umq_dfx_api.h"
#include "../../../hcom/umq/include/umq/umq_dfx_types.h"
#include "../../../hcom/umq/include/umq/umq_errno.h"
#include "../../../hcom/umq/include/umq/umq_pro_api.h"
#include "../../../hcom/umq/src/qbuf/qbuf_list.h"

namespace ock {
namespace ubs {
class UmqDataRxOps : public DataRxOps {
public:
    explicit UmqDataRxOps(uint64_t umq_handle = UMQ_INVALID_HANDLE) : m_local_umqh(umq_handle) {}

    ~UmqDataRxOps() override = default ;

    int PollRx(bool flow_control_failed) override;

private:
    // --- 私有成员函数 ---
    int GetQbuf(umq_buf_t **buf, int max_num);
    void HandleErrorRxCqe(umq_buf_t *buf);
    int NotifyReadable();
    int GetAndAckEvent(umq_io_direction_t io_dir);

private:
    // --- 私有成员变量 ---
    // umq 相关的句柄
    uint64_t m_local_umqh = UMQ_INVALID_HANDLE;
};
} // namespace ubs
} // namespace ock


#endif // UBS_COMM_UMQ_DATA_RX_H