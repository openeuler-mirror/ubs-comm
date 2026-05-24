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
#ifndef UBS_COMM_DL_UMQ_API_H
#define UBS_COMM_DL_UMQ_API_H

#include "common/ubsocket_defines.h"
#include "common/ubsocket_errno.h"
#include "umq_api.h"

namespace ock {
namespace ubs {
class UmqApi {
public:
    static Result Load() noexcept
    {
        return UBS_OK;
    }

    static void UnLoad() noexcept {}

    static int umq_init(umq_init_cfg_t *cfg)
    {
        return ::umq_init(cfg);
    }

    static void umq_uninit(void)
    {
        ::umq_uninit();
    }

    static uint64_t umq_create(umq_create_option_t *option)
    {
        return ::umq_create(option);
    }

    static int umq_destroy(uint64_t umqh)
    {
        return ::umq_destroy(umqh);
    }

    static uint32_t umq_bind_info_get(uint64_t umqh, uint8_t *bind_info, uint32_t bind_info_size)
    {
        return ::umq_bind_info_get(umqh, bind_info, bind_info_size);
    }

    static int umq_bind(uint64_t umqh, uint8_t *bind_info, uint32_t bind_info_size)
    {
        return ::umq_bind(umqh, bind_info, bind_info_size);
    }

    static int umq_unbind(uint64_t umqh)
    {
        return ::umq_unbind(umqh);
    }

    static int umq_state_set(uint64_t umqh, umq_state_t state)
    {
        return ::umq_state_set(umqh, state);
    }

    static umq_state_t umq_state_get(uint64_t umqh)
    {
        return ::umq_state_get(umqh);
    }

    static umq_buf_t *umq_buf_alloc(uint32_t request_size, uint32_t request_qbuf_num, uint64_t umqh,
                                    umq_alloc_option_t *option)
    {
        return ::umq_buf_alloc(request_size, request_qbuf_num, umqh, option);
    }

    static void umq_buf_free(umq_buf_t *qbuf)
    {
        ::umq_buf_free(qbuf);
    }

    static umq_buf_t *umq_buf_break_and_free(umq_buf_t *qbuf)
    {
        return ::umq_buf_break_and_free(qbuf);
    }

    static int umq_buf_headroom_reset(umq_buf_t *qbuf, uint16_t headroom_size)
    {
        return ::umq_buf_headroom_reset(qbuf, headroom_size);
    }

    static int umq_buf_reset(umq_buf_t *qbuf)
    {
        return ::umq_buf_reset(qbuf);
    }

    static umq_buf_t *umq_data_to_head(void *data)
    {
        return ::umq_data_to_head(data);
    }

    static int umq_enqueue(uint64_t umqh, umq_buf_t *qbuf, umq_buf_t **bad_qbuf)
    {
        return ::umq_enqueue(umqh, qbuf, bad_qbuf);
    }

    static umq_buf_t *umq_dequeue(uint64_t umqh)
    {
        return ::umq_dequeue(umqh);
    }

    static void umq_notify(uint64_t umqh)
    {
        ::umq_notify(umqh);
    }

    static int umq_rearm_interrupt(uint64_t umqh, bool solicated, umq_interrupt_option_t *option)
    {
        return ::umq_rearm_interrupt(umqh, solicated, option);
    }

    static int32_t umq_wait_interrupt(uint64_t wait_umqh, int time_out, umq_interrupt_option_t *option)
    {
        return ::umq_wait_interrupt(wait_umqh, time_out, option);
    }

    static void umq_ack_interrupt(uint64_t umqh, uint32_t nevents, umq_interrupt_option_t *option)
    {
        ::umq_ack_interrupt(umqh, nevents, option);
    }

    static int umq_buf_split(umq_buf_t *head, umq_buf_t *node)
    {
        return ::umq_buf_split(head, node);
    }

    static int umq_async_event_fd_get(umq_trans_info_t *trans_info)
    {
        return ::umq_async_event_fd_get(trans_info);
    }

    static int umq_get_async_event(umq_trans_info_t *trans_info, umq_async_event_t *event)
    {
        return ::umq_get_async_event(trans_info, event);
    }

    static void umq_ack_async_event(umq_async_event_t *event)
    {
        ::umq_ack_async_event(event);
    }

    static int umq_log_config_set(umq_log_config_t *config)
    {
        return ::umq_log_config_set(config);
    }

    static int umq_log_config_get(umq_log_config_t *config)
    {
        return ::umq_log_config_get(config);
    }

    static int umq_dev_add(umq_trans_info_t *trans_info)
    {
        return ::umq_dev_add(trans_info);
    }

    static int umq_get_route_list(const umq_route_key_t *route_key, umq_trans_mode_t umq_trans_mode,
                                  umq_route_list_t *route_list)
    {
        return ::umq_get_route_list(route_key, umq_trans_mode, route_list);
    }

    static int umq_user_ctl(uint64_t umqh, umq_user_ctl_in_t *in, umq_user_ctl_out_t *out)
    {
        return ::umq_user_ctl(umqh, in, out);
    }

    static int umq_mempool_state_get(uint64_t umqh, uint32_t mempool_id, umq_mempool_state_t *mempool_state)
    {
        return ::umq_mempool_state_get(umqh, mempool_id, mempool_state);
    }

    static int umq_mempool_state_refresh(uint64_t umqh, uint32_t mempool_id)
    {
        return ::umq_mempool_state_refresh(umqh, mempool_id);
    }

    static int umq_dev_info_get(char *dev_name, umq_trans_mode_t umq_trans_mode, umq_dev_info_t *umq_dev_info)
    {
        return ::umq_dev_info_get(dev_name, umq_trans_mode, umq_dev_info);
    }

    static umq_dev_info_t *umq_dev_info_list_get(umq_trans_mode_t umq_trans_mode, int *dev_num)
    {
        return ::umq_dev_info_list_get(umq_trans_mode, dev_num);
    }

    static void umq_dev_info_list_free(umq_trans_mode_t umq_trans_mode, umq_dev_info_t *umq_dev_info)
    {
        ::umq_dev_info_list_free(umq_trans_mode, umq_dev_info);
    }

    static int umq_cfg_get(uint64_t umqh, umq_cfg_get_t *cfg)
    {
        return ::umq_cfg_get(umqh, cfg);
    }

    static int umq_external_mutex_lock_ops_register(umq_external_mutex_lock_ops_t *ops)
    {
        return ::umq_external_mutex_lock_ops_register(ops);
    }

    static int umq_external_rwlock_ops_register(umq_external_rwlock_ops_t *ops)
    {
        return ::umq_external_rwlock_ops_register(ops);
    }

    static int umq_post(uint64_t umqh, umq_buf_t *qbuf, umq_io_direction_t io_direction, umq_buf_t **bad_qbuf)
    {
        return ::umq_post(umqh, qbuf, io_direction, bad_qbuf);
    }

    static int umq_poll(uint64_t umqh, umq_io_direction_t io_direction, umq_buf_t **buf, uint32_t max_buf_count)
    {
        return ::umq_poll(umqh, io_direction, buf, max_buf_count);
    }

    static int umq_interrupt_fd_get(uint64_t umqh, umq_interrupt_option_t *option)
    {
        return ::umq_interrupt_fd_get(umqh, option);
    }

    static int umq_get_cq_event(uint64_t umqh, umq_interrupt_option_t *option)
    {
        return ::umq_get_cq_event(umqh, option);
    }
};
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_DL_UMQ_API_H