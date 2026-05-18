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

#include "umq_data_rx_ops.h"

namespace ock {
namespace ubs {
namespace umq {

int UmqRxOps::PollRx(bool flow_control_failed)
{
    if (!GlobalSetting::UBS_ENABLE_SHARE_JFR && get_and_ack_event_) {
        if (GetAndAckEvent(UMQ_IO_RX) < 0) {
            errno = EIO;
            UBS_VLOG_ERR("ReadV GetAndAckEvent() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
                         Func::Error2Str(errno));
            return -1;
        }
        get_and_ack_event_ = false;
    }

    umq_buf_t *buf[POLL_BATCH_MAX];
    int poll_num = 0;
    if (poll_) {
        poll_num = GetQbuf(buf, POLL_BATCH_MAX);
        if (poll_num < 0) {
            errno = EIO;
            UBS_VLOG_ERR("ReadV GetQbuf() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
                         Func::Error2Str(errno));
            return -1;
        } else if (poll_num == 0) {
            /* might be useful for qps performance by
             * (1) avoid redundant poll operations when handing cache;
             * (2) aggregating RX requests; */
            poll_ = false;
        }
    }

    uint32_t polled_size = 0;
    for (int i = 0; i < poll_num; ++i) {
        umq_buf_pro_t *buf_pro = reinterpret_cast<umq_buf_pro_t *>(buf[i]->qbuf_ext);
        if (buf_pro->opcode == UMQ_OPC_SEND_IMM && buf_pro->imm.user_data == 1) {
            // 处理探测包
            //   Statistics::ProbeManager::GetInstance().HandleReceivedPacket(fd_, buf[i]);
            if (QBUF_LIST_NEXT(buf[i]) != nullptr) {
                UBS_VLOG_WARN("probe buf next not null\n");
            }
            UmqApi::umq_buf_free(buf[i]);
            continue;
        }
        // currently, umq over IB return IB cr status directly, successful = 0
        if (buf[i]->status != 0) {
            if (buf[i]->status != UMQ_FAKE_BUF_FC_UPDATE) {
                if (buf[i]->status == UMQ_FAKE_BUF_FC_ERR) {
                    flow_control_failed = true;
                }
                HandleErrorRxCqe(buf[i]);
            } else {
                rx_queue_avail_num_ += 1;
                // try to wake up tx if necessary
                bool need_fc_awake = need_fc_awake_.exchange(false, std::memory_order_relaxed);
                if (need_fc_awake && NotifyReadable() == -1) {
                    /**UBS_VLOG_ERR("eventfd_write() failed, event fd: %d, peer eid:" EID_FMT
                                 ", peer ip: %s, errno: %d, errmsg: %s\n",
                                 event_fd_, EID_ARGS(GetPeerEid()), GetPeerIp().c_str(), errno, Func::Error2Str(errno));**/
                }
            }

            QBUF_LIST_NEXT(buf[i]) = nullptr;
            UmqApi::umq_buf_free(buf[i]);
            continue;
        }
        if (GlobalSetting::UBS_TRACE_ENABLED) {
            //   UpdateTraceStats(StatsMgr::RX_PACKET_COUNT, 1);
        }
        block_cache_.Insert((char *)(buf[i]->buf_data), buf[i]->data_size);
        polled_size += buf[i]->data_size;
    }
    return 0;
}

void *UmqRxOps::PtrFloorToBoundary(void *ptr)
{
    return (void *)((uint64_t)ptr & ~UmqSetting::FloorMask());
}

int UmqRxOps::GetQbuf(umq_buf_t **buf, int max_num)
{
    // TODO
    return 0;
}

int UmqRxOps::GetAndAckEvent(umq_io_direction_t io_dir)
{
    // TODO
    return 0;
}
int UmqRxOps::NotifyReadable()
{
    // TODO
    return 0;
}

void UmqRxOps::HandleErrorRxCqe(umq_buf_t *buf)
{
    // TODO
}
} // namespace umq
} // namespace ubs
} // namespace ock