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
#include "umq_conn_helper.h"
#include "core/ubsocket_event_epoll.h"
#include "iobuf/ubsocket_iobuf.h"
#include "umq_errno_converter.h"
#include "umq_share_jfr_epoll_runner_ops.h"

namespace ock {
namespace ubs {
namespace umq {

Result UmqConnHelper::GetDevEid(char *dev_name, uint32_t eid_idx, umq_eid_t *eid)
{
    umq_dev_info_t umq_dev_info = {};
    int ret = UmqApi::umq_dev_info_get(dev_name, UMQ_TRANS_MODE_UB, &umq_dev_info);
    if (ret != 0) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, ret, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_dev_info_get() failed, "
                     "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     ret, errno, UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, ret), savedErrno);
        return UBS_ERROR;
    }

    for (uint32_t i = 0; i < umq_dev_info.ub.eid_cnt; ++i) {
        if (umq_dev_info.ub.eid_list[i].eid_index == eid_idx) {
            *eid = umq_dev_info.ub.eid_list[i].eid;
            return UBS_OK;
        }
    }

    UBS_VLOG_ERR("Failed to find eid index in device info, eid_idx: %u, ret: %d\n", eid_idx, -1);
    return UBS_INVALID_PARAM;
}

Result UmqConnHelper::PrefillRx(uint64_t umq_handle)
{
    uint32_t left_post_rx_num = UmqConnHelper::GetLeftPostRxNum(umq_handle);
    if (left_post_rx_num == 0) {
        UBS_VLOG_ERR("Failed to set rx window capacity\n");
        return UBS_ERROR;
    }

    uint32_t cur_post_rx_num = 0;
    umq_alloc_option_t option = {UMQ_ALLOC_FLAG_HEAD_ROOM_SIZE, sizeof(Block)};
    do {
        cur_post_rx_num = left_post_rx_num > UmqSetting::UMQ_POST_BATCH_MAX ? UmqSetting::UMQ_POST_BATCH_MAX :
                                                                              left_post_rx_num;
        umq_buf_t *rx_buf_list =
            UmqApi::umq_buf_alloc(UmqSetting::GetIOBufSize(), cur_post_rx_num, UMQ_INVALID_HANDLE, &option);
        if (rx_buf_list == nullptr) {
            int rx_window_capacity = 0;
            UBS_VLOG_ERR("[UMQ_API] umq_buf_alloc() failed, RX depth: %d, ret: %p\n", rx_window_capacity, rx_buf_list);
            return UBS_ERROR;
        }

        umq_buf_t *bad_qbuf = nullptr;
        umq_io_option_t io_rx_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_RX,
                                        UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
        int umq_ret = UmqApi::umq_post(umq_handle, rx_buf_list, &io_rx_option, &bad_qbuf);
        if (umq_ret != UMQ_SUCCESS) {
            int savedErrno = errno;
            errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, umq_ret, savedErrno);
            int rx_window_capacity = 0;
            UBS_VLOG_ERR("[UMQ_API] umq_post() failed, RX depth: %u, ret: %d, mapped: %d(%s), original: %d\n",
                         rx_window_capacity, umq_ret, errno,
                         UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, umq_ret), savedErrno);
            return UBS_ERROR;
        }
        UBS_VLOG_DEBUG("Post RX depth: %u\n", cur_post_rx_num);
    } while ((left_post_rx_num -= cur_post_rx_num) > 0);

    return UBS_OK;
}

uint32_t UmqConnHelper::GetLeftPostRxNum(uint64_t umq_handle)
{
    uint32_t left_post_rx_num = 0;
    umq_cfg_get_t cfg;
    memset(&cfg, 0, sizeof(umq_cfg_get_t));
    int res = UmqApi::umq_cfg_get(umq_handle, &cfg);
    if (res != 0) {
        UBS_VLOG_ERR("[UMQ_API] umq_cfg_get() failed, umq handle: %llu, ret: %d\n",
                     static_cast<unsigned long long>(umq_handle), res);
    } else {
        left_post_rx_num = cfg.rqe_post_factor * cfg.rx_depth;
        UBS_VLOG_DEBUG("Successfully get umq cfg, left_post_rx_num = %u\n", left_post_rx_num);
    }
    return left_post_rx_num;
}

Result UmqConnHelper::NewBaseUmqCreateOptions(umq_create_option_t &umq_create_option, ub_trans_mode trans_mode)
{
    umq_create_option.trans_mode = UmqSetting::UMQ_TRANS_MODE;
    umq_create_option.create_flag = UMQ_CREATE_FLAG_TX_DEPTH | UMQ_CREATE_FLAG_RX_DEPTH | UMQ_CREATE_FLAG_RX_BUF_SIZE |
                                    UMQ_CREATE_FLAG_TX_BUF_SIZE | UMQ_CREATE_FLAG_QUEUE_MODE | UMQ_CREATE_FLAG_TP_MODE |
                                    UMQ_CREATE_FLAG_TP_TYPE | UMQ_CREATE_FLAG_UMQ_CTX;

    umq_create_option.rx_depth = GlobalSetting::UBS_RX_DEPTH;
    umq_create_option.tx_depth = GlobalSetting::UBS_TX_DEPTH;
    umq_create_option.rx_buf_size = UmqSetting::GetIOBufSize();
    umq_create_option.tx_buf_size = UmqSetting::GetIOBufSize();
    umq_create_option.mode = UMQ_MODE_INTERRUPT;

    UBS_VLOG_DEBUG("UBSOCKET_LINK_PRIORITY: %d\n", UmqSetting::UMQ_LINK_PRIORITY);
    if (UmqSetting::UMQ_LINK_PRIORITY != UBSOCKET_LINK_PRIORITY_DEFAULT) {
        umq_create_option.priority = static_cast<uint8_t>(UmqSetting::UMQ_LINK_PRIORITY);
        umq_create_option.create_flag |= UMQ_CREATE_FLAG_PRIORITY;
    }

    static const char *trans_mode_str[RC_CTP + 1] = {"RC_TP", "RM_TP", "RM_CTP", "RC_CTP"};
    UBS_VLOG_DEBUG("trans_mode result is: %s\n", trans_mode_str[trans_mode]);
    if (GetTpInfo(umq_create_option.tp_mode, umq_create_option.tp_type) != UBS_OK) {
        return UBS_ERROR;
    }

    if (UmqSetting::UMQ_TP_TYPE == POOL) {
        umq_create_option.create_flag |= UMQ_CREATE_FLAG_SHARE_TRANSPORT;
    }

    return UBS_OK;
}

Result UmqConnHelper::GetTpInfo(umq_tp_mode_t &tp_mode, umq_tp_type_t &tp_type, ub_trans_mode trans_mode)
{
    if (trans_mode == RC_TP) {
        tp_mode = UMQ_TM_RC;
        tp_type = UMQ_TP_TYPE_RTP;
    } else if (trans_mode == RM_TP) {
        tp_mode = UMQ_TM_RM;
        tp_type = UMQ_TP_TYPE_RTP;
    } else if (trans_mode == RM_CTP) {
        tp_mode = UMQ_TM_RM;
        tp_type = UMQ_TP_TYPE_CTP;
    } else if (trans_mode == RC_CTP) {
        tp_mode = UMQ_TM_RC;
        tp_type = UMQ_TP_TYPE_CTP;
    } else {
        UBS_VLOG_ERR("Unsupported trans mode: %d", static_cast<int>(trans_mode));
        return UBS_ERROR;
    }
    return UBS_OK;
}

Result UmqConnHelper::GetRouteList(umq_route_list_t &route_list, const umq_eid_t &src_eid, const umq_eid_t &dst_eid)
{
    umq_route_key_t route;
    (void)std::memcpy(&route.src_bonding_eid, &src_eid, sizeof(umq_eid_t));
    (void)std::memcpy(&route.dst_bonding_eid, &dst_eid, sizeof(umq_eid_t));

    umq_tp_mode_t tp_mode;
    umq_tp_type_t tp_type;
    if (UmqConnHelper::GetTpInfo(tp_mode, tp_type) != UBS_OK) {
        UBS_VLOG_ERR("Failed to get urma topo info.");
        return UBS_ERROR;
    }
    route.tp_type = tp_type;

    int ret = UmqApi::umq_get_route_list(&route, UMQ_TRANS_MODE_UB, &route_list);
    if (ret != 0) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, ret, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_get_route_list() failed, ret: %d, mapped errno: %d(%s), original errno: %d\n", ret,
                     errno, UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, ret), savedErrno);
        return UBS_ERROR;
    }

    if (route_list.route_num == 0) {
        UBS_VLOG_ERR("Route list is empty.\n");
        return UBS_ERROR;
    }
    return UBS_OK;
}

Result UmqConnHelper::RegisterSharedJfrForRead(uint64_t main_umq_handle)
{
    EpollRunnerBase &epoll_runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    int result = epoll_runner.Start();
    if (result != UBS_OK) {
        return result;
    }
    umq_interrupt_option_t main_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX, UMQ_FD_IO};
    auto share_jfr_fd = UmqApi::umq_interrupt_fd_get(main_umq_handle, &main_option);
    if (UNLIKELY(share_jfr_fd < 0)) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, share_jfr_fd, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] Failed to get share jfr RX interrupt fd, main umq: %llu, "
                     "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(main_umq_handle), share_jfr_fd, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, share_jfr_fd), savedErrno);
        return UBS_ERROR;
    }

    RunnerEventData event_data{};
    event_data.event_data.type = RUNNER_EVENT_TYPE_SHARE_JFR;
    event_data.event_data.data = share_jfr_fd;

    struct epoll_event share_jfr_event = {
        .events = EPOLLIN | EPOLLET,
        .data = {.u64 = event_data.u64},
    };

    UmqShareJfrEpollRunnerOps::ExtContext ctx;
    ctx.umq_handle = main_umq_handle;
    if (UNLIKELY(epoll_runner.AddEpollEvent(share_jfr_fd, &share_jfr_event, &ctx))) {
        UBS_VLOG_ERR("async_epoll epoll_ctl(ADD) share jfr event failed: %d : %s\n", errno, strerror(errno));
        return UBS_ERROR;
    }
    return UBS_OK;
}

uint32_t UmqConnHelper::GetTargetChipId(const std::vector<uint32_t> &socket_ids,
                                        const std::vector<uint32_t> &chip_id_list, int processSocketId)
{
    auto it = std::find(socket_ids.begin(), socket_ids.end(), processSocketId);
    if (it == socket_ids.end()) {
        return UINT32_MAX; // 错误标识
    }

    size_t index = std::distance(socket_ids.begin(), it);
    if (index >= chip_id_list.size()) {
        return UINT32_MAX; // 索引越界
    }

    return chip_id_list[index];
}

} // namespace umq
} // namespace ubs
} // namespace ock
