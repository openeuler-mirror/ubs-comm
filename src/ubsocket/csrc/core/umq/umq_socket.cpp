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
#include "umq_socket.h"

namespace ock {
namespace ubs {
namespace umq {
Result UmqSocket::Initialize() noexcept
{
    return UBS_OK;
}

void UmqSocket::UnInitialize() noexcept {}

Result UmqSocket::CreateLocalUmq(umq_eid_t *conn_eid, umq_used_ports_t &used_ports)
{
    if (umq_handle_ != UMQ_INVALID_HANDLE) {
        UBS_VLOG_ERR("Create umq on a created umq.\n");
        return UBS_ERROR;
    }

    umq_create_option_t queue_cfg;
    memset(&queue_cfg, 0, sizeof(queue_cfg));
    // TODO：增加环境变量
    queue_cfg.trans_mode = UMQ_TRANS_MODE_UB;
    queue_cfg.create_flag = UMQ_CREATE_FLAG_TX_DEPTH | UMQ_CREATE_FLAG_RX_DEPTH | UMQ_CREATE_FLAG_RX_BUF_SIZE |
                            UMQ_CREATE_FLAG_TX_BUF_SIZE | UMQ_CREATE_FLAG_QUEUE_MODE | UMQ_CREATE_FLAG_TP_MODE |
                            UMQ_CREATE_FLAG_TP_TYPE | UMQ_CREATE_FLAG_UMQ_CTX;

    queue_cfg.rx_depth = GlobalSetting::UBS_RX_DEPTH;
    queue_cfg.tx_depth = GlobalSetting::UBS_TX_DEPTH;
    queue_cfg.rx_buf_size = UmqSetting::GetIOBufSize();
    queue_cfg.tx_buf_size = UmqSetting::GetIOBufSize();
    queue_cfg.mode = UMQ_MODE_INTERRUPT;
    // 共享 JFR、AE 事件依赖 umq_ctx.
    queue_cfg.umq_ctx = raw_socket_;
    if (IsBonding() == 1) {
        queue_cfg.create_flag |= UMQ_CREATE_FLAG_USED_PORTS;
        queue_cfg.used_ports = used_ports;
    }

    // TODO: 设置队列优先级
    // if (context->GetLinkPriority() != DEFAULT_LINK_PRIORITY) {
    //     queue_cfg.priority = context->GetLinkPriority();
    //     queue_cfg.create_flag |= UMQ_CREATE_FLAG_PRIORITY;
    // }

    int n = snprintf(queue_cfg.name, UMQ_NAME_MAX_LEN, "fd: %d", raw_socket_);
    if ((((int)UMQ_NAME_MAX_LEN - 1) < n) || (n < 0)) {
        UBS_VLOG_ERR("Failed to set umq name\n");
        return UBS_SET_DEV_INFO;
    }

    // TODO: 待补充指定 ip 和 bonging name 的情况
    umq_eid_t local_eid;
#ifdef ENABLED
    if (context->GetDevIpStr() != nullptr) {
        if (context->IsDevIpv6()) {
            queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_IPV6;
            if (strcpy_s(queue_cfg.dev_info.ipv6.ip_addr, UMQ_IPV6_SIZE, context->GetDevIpStr()) != EOK) {
                UBS_VLOG_ERR("Failed to strcpy_s device ipv6 address\n");
                return ubsocket::Error::kUBSOCKET_SET_DEV_INFO;
            }
        } else {
            queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_IPV4;
            if (strcpy_s(queue_cfg.dev_info.ipv4.ip_addr, UMQ_IPV4_SIZE, context->GetDevIpStr()) != EOK) {
                UBS_VLOG_ERR("Failed to strcpy_s device ipv4 address\n");
                return ubsocket::Error::kUBSOCKET_SET_DEV_INFO;
            }
        }
    } else if (context->GetDevNameStr() != nullptr) {
        if (strcpy_s(queue_cfg.dev_info.dev.dev_name, UMQ_DEV_NAME_SIZE, context->GetDevNameStr()) != EOK) {
            UBS_VLOG_ERR("Failed to strcpy_s device name\n");
            return ubsocket::Error::kUBSOCKET_SET_DEV_INFO;
        }
        if (!context->IsBonding()) {
            queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_DEV;
            queue_cfg.dev_info.dev.eid_idx = context->GetEidIdx();

            if (GetDevEid(queue_cfg.dev_info.dev.dev_name, context->GetEidIdx(), &local_eid) != 0) {
                UBS_VLOG_ERR("Failed to get eid by dev name:%s and eid index:%d \n", context->GetDevNameStr(),
                             context->GetEidIdx());
            }
            m_conn_info.conn_eid = local_eid;
        } else {
            // init use bonding dev
            queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_EID;
            queue_cfg.dev_info.eid.eid = *conn_eid;
            local_eid = *conn_eid;
        }
    } else {
        if (strcpy(queue_cfg.dev_info.dev.dev_name, UMQ_DEV_NAME_SIZE, "bonding_dev_0") != EOK) {
            UBS_VLOG_ERR("Failed to strcpy device name\n");
            return UBS_SET_DEV_INFO;
        }
        IsBonding if (context->IsBonding())
        {
            queue_cfg.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_EID;
            queue_cfg.dev_info.eid.eid = *conn_eid;
        }
    }
#endif

    if (strcpy(queue_cfg.dev_info.dev.dev_name, "bonding_dev_0") != 0) {
        UBS_VLOG_ERR("Failed to strcpy device name\n");
        return UBS_SET_DEV_INFO;
    }

    static const char *trans_mode_str[RC_CTP + 1] = {"RC_TP", "RM_TP", "RM_CTP", "RC_CTP"};
    UBS_VLOG_INFO("trans_mode result is: %s\n", trans_mode_str[trans_mode_]);
    if (trans_mode_ == RC_TP) {
        queue_cfg.tp_mode = UMQ_TM_RC;
        queue_cfg.tp_type = UMQ_TP_TYPE_RTP;
    } else if (trans_mode_ == RM_TP) {
        queue_cfg.tp_mode = UMQ_TM_RM;
        queue_cfg.tp_type = UMQ_TP_TYPE_RTP;
    } else if (trans_mode_ == RM_CTP) {
        queue_cfg.tp_mode = UMQ_TM_RM;
        queue_cfg.tp_type = UMQ_TP_TYPE_CTP;
    } else if (trans_mode_ == RC_CTP) {
        queue_cfg.tp_mode = UMQ_TM_RC;
        queue_cfg.tp_type = UMQ_TP_TYPE_CTP;
    }

    umq_handle_ = CreateSubUmq(&queue_cfg, &local_eid);
    if (umq_handle_ == UMQ_INVALID_HANDLE) {
        UBS_VLOG_ERR("CreateSubUmq() failed, ret: %llu\n", static_cast<unsigned long long>(umq_handle_));
        return UBS_ERROR;
    }

    // TODO: 增加环境变量和共享jfr
    // uint64_t share_jfr_rx_queue_depth = 1024ULL;
    // uint64_t share_jfr_rx_queue_depth = context == nullptr ? DEFAULT_SHARE_JFR_RX_QUEUE_DEPTH :
    //                                                          context->GetShareJfrRxQueueDepth();
    // rxQueue = new (std::nothrow) QbufQueue<umq_buf_t *>(share_jfr_rx_queue_depth);
    // if (rxQueue == nullptr) {
    //     UBS_VLOG_ERR("Failed to init share jfr rx queue for fd: %d \n", raw_socket_);
    //     return UBS_INIT_SHARED_JFR_RX_QUEUE;
    // }

    // Context::FetchAdd();
    return UBS_OK;
}

uint64_t UmqSocket::CreateSubUmq(umq_create_option_t *cfg, umq_eid_t *local_eid)
{
    //TODO: 待增加环境变量
    bool enable_share_jfr = false;
    if (!enable_share_jfr) {
        return UmqApi::umq_create(cfg);
    }
    return UmqApi::umq_create(cfg);

    // TODO: 待增加共享jfr
#ifdef ENABLED
    ScopedUbExclusiveLocker sLock(EidUmqTable::GetMainMutex());
    uint64_t main_umq = GetOrCreateMainUmq(cfg, local_eid);
    if (main_umq == UMQ_INVALID_HANDLE) {
        UBS_VLOG_ERR("GetOrCreateMainUmq() failed, ret: %llu\n", static_cast<unsigned long long>(main_umq));
        return UMQ_INVALID_HANDLE;
    }

    cfg->create_flag |= UMQ_CREATE_FLAG_SHARE_RQ | UMQ_CREATE_FLAG_UMQ_CTX | UMQ_CREATE_FLAG_SUB_UMQ;
    cfg->share_rq_umqh = main_umq;
    cfg->umq_ctx = (uint64_t)m_fd;
    uint64_t sub_umq = umq_create(cfg);
    if (sub_umq == UMQ_INVALID_HANDLE) {
        UBS_VLOG_ERR("umq_create() failed for sub umq, ret: %llu\n", static_cast<unsigned long long>(sub_umq));
        return UMQ_INVALID_HANDLE;
    }

    EidUmqTable::Add(*local_eid, main_umq);
    MainSubUmqTable::Add(main_umq, sub_umq);

    m_main_umqh = main_umq;
    return sub_umq;
#endif
}

Result UmqSocket::PrefillRx()
{
    // bool enable_share_jfr = context == nullptr ? true : context->EnableShareJfr();
    bool enable_share_jfr = false;
    uint64_t umq_handle = enable_share_jfr ? umq_handle_ : local_umq_handle_;
    uint32_t left_post_rx_num = getLeftPostRxNum(umq_handle);
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
            UBS_VLOG_ERR("umq_buf_alloc() failed, RX depth: %u, ret: %p\n", rx_window_capacity, rx_buf_list);
            return UBS_ERROR;
        }

        umq_buf_t *bad_qbuf = nullptr;
        int umq_ret = UmqApi::umq_post(umq_handle, rx_buf_list, UMQ_IO_RX, &bad_qbuf);
        if (umq_ret != UMQ_SUCCESS) {
            int rx_window_capacity = 0;
            UBS_VLOG_ERR("umq_post() failed, RX depth: %u, ret: %d\n", rx_window_capacity, umq_ret);
            // TODO：处理bad_qbuf
            // m_rx.m_window_size += HandleBadQBuf(rx_buf_list, bad_qbuf);
            return UBS_ERROR;
        }
        // TODO: 待增加rx window size
        // m_rx.m_window_size += cur_post_rx_num;
        UBS_VLOG_DEBUG("Post RX depth: %u\n", cur_post_rx_num);
    } while ((left_post_rx_num -= cur_post_rx_num) > 0);

    // TODO：待确认如何调用 PollTx
    // uint32_t poll_cnt = 0;
    // do {
    //     PollTx(m_tx.m_retrieve_threshold);
    //     if (umq_state_get(m_local_umqh) != QUEUE_STATE_IDLE) {
    //         break;
    //     }
    //     usleep(WAIT_UMQ_READY_TIMEOUT_US);
    // } while (poll_cnt++ < WAIT_UMQ_READY_ROUND);

    // int local_umq_state = QUEUE_STATE_IDLE;
    // local_umq_state = umq_state_get(m_local_umqh);
    // if (local_umq_state != QUEUE_STATE_READY) {
    //     UBS_VLOG_ERR("umq_state_get() failed to reach ready, ret: %d\n", local_umq_state);
    //     return -1;
    // }

    // m_rx.m_window_size = context->GetRxDepth();
    return 0;
}

uint32_t UmqSocket::getLeftPostRxNum(uint64_t umq_handle)
{
    uint32_t left_post_rx_num = 0;
    umq_cfg_get_t cfg;
    memset(&cfg, 0, sizeof(umq_cfg_get_t));
    int res = UmqApi::umq_cfg_get(umq_handle, &cfg);
    if (res != 0) {
        UBS_VLOG_ERR("umq_cfg_get() failed, umq handle: %llu, ret: %d\n", static_cast<unsigned long long>(umq_handle),
                     res);
    } else {
        left_post_rx_num = cfg.rqe_post_factor * cfg.rx_depth;
        UBS_VLOG_INFO("Successfully get umq cfg, left_post_rx_num = %u\n", left_post_rx_num);
    }
    return left_post_rx_num;
}

} // namespace umq
} // namespace ubs
} // namespace ock