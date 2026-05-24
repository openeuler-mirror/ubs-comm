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
#include "dl_umq_api.h"

#ifdef UMQ_DLOPEN_BACKEND_ENABLED

namespace ock {
namespace ubs {
DL_API_DEFINE(UmqApi, umq_init)
DL_API_DEFINE(UmqApi, umq_uninit);
DL_API_DEFINE(UmqApi, umq_create);
DL_API_DEFINE(UmqApi, umq_destroy);
DL_API_DEFINE(UmqApi, umq_bind_info_get);
DL_API_DEFINE(UmqApi, umq_bind);
DL_API_DEFINE(UmqApi, umq_unbind);
DL_API_DEFINE(UmqApi, umq_state_set);
DL_API_DEFINE(UmqApi, umq_state_get);
DL_API_DEFINE(UmqApi, umq_buf_alloc);
DL_API_DEFINE(UmqApi, umq_buf_free);
DL_API_DEFINE(UmqApi, umq_buf_break_and_free);
DL_API_DEFINE(UmqApi, umq_buf_headroom_reset);
DL_API_DEFINE(UmqApi, umq_buf_reset);
DL_API_DEFINE(UmqApi, umq_data_to_head);
DL_API_DEFINE(UmqApi, umq_enqueue);
DL_API_DEFINE(UmqApi, umq_dequeue);
DL_API_DEFINE(UmqApi, umq_notify);
DL_API_DEFINE(UmqApi, umq_rearm_interrupt);
DL_API_DEFINE(UmqApi, umq_wait_interrupt);
DL_API_DEFINE(UmqApi, umq_ack_interrupt);
DL_API_DEFINE(UmqApi, umq_buf_split);
DL_API_DEFINE(UmqApi, umq_async_event_fd_get);
DL_API_DEFINE(UmqApi, umq_get_async_event);
DL_API_DEFINE(UmqApi, umq_ack_async_event);
DL_API_DEFINE(UmqApi, umq_log_config_set);
DL_API_DEFINE(UmqApi, umq_log_config_get);
DL_API_DEFINE(UmqApi, umq_dev_add);
DL_API_DEFINE(UmqApi, umq_get_route_list);
DL_API_DEFINE(UmqApi, umq_user_ctl);
DL_API_DEFINE(UmqApi, umq_mempool_state_get);
DL_API_DEFINE(UmqApi, umq_mempool_state_refresh);
DL_API_DEFINE(UmqApi, umq_dev_info_get);
DL_API_DEFINE(UmqApi, umq_dev_info_list_get);
DL_API_DEFINE(UmqApi, umq_dev_info_list_free);
DL_API_DEFINE(UmqApi, umq_cfg_get);
DL_API_DEFINE(UmqApi, umq_external_mutex_lock_ops_register);
DL_API_DEFINE(UmqApi, umq_external_rwlock_ops_register);
DL_API_DEFINE(UmqApi, umq_post);
DL_API_DEFINE(UmqApi, umq_poll);
DL_API_DEFINE(UmqApi, umq_interrupt_fd_get);
DL_API_DEFINE(UmqApi, umq_get_cq_event);

std::mutex UmqApi::LOAD_MUTEX;
bool UmqApi::LOADED = false;

Result UmqApi::Load() noexcept
{
    std::lock_guard<std::mutex> guard(LOAD_MUTEX);
    if (LOADED) {
        return UBS_OK;
    }

    /* step1: open library file */
    void *handle = dlopen("libumq.so", RTLD_NOW | RTLD_NODELETE | RTLD_GLOBAL);
    if (handle == nullptr) {
        UBS_VLOG_ERR("Open libumq failed, error: %s\n", dlerror());
        return UBS_DL_OPEN_LIB_FAILED;
    }

    /* step2: load functions */
    DL_API_LOAD(umq_init);
    DL_API_LOAD(umq_uninit);
    DL_API_LOAD(umq_create);
    DL_API_LOAD(umq_destroy);
    DL_API_LOAD(umq_bind_info_get);
    DL_API_LOAD(umq_bind);
    DL_API_LOAD(umq_unbind);
    DL_API_LOAD(umq_state_set);
    DL_API_LOAD(umq_state_get);
    DL_API_LOAD(umq_buf_alloc);
    DL_API_LOAD(umq_buf_free);
    DL_API_LOAD(umq_buf_break_and_free);
    DL_API_LOAD(umq_buf_headroom_reset);
    DL_API_LOAD(umq_buf_reset);
    DL_API_LOAD(umq_data_to_head);
    DL_API_LOAD(umq_enqueue);
    DL_API_LOAD(umq_dequeue);
    DL_API_LOAD(umq_notify);
    DL_API_LOAD(umq_rearm_interrupt);
    DL_API_LOAD(umq_wait_interrupt);
    DL_API_LOAD(umq_ack_interrupt);
    DL_API_LOAD(umq_buf_split);
    DL_API_LOAD(umq_async_event_fd_get);
    DL_API_LOAD(umq_get_async_event);
    DL_API_LOAD(umq_ack_async_event);
    DL_API_LOAD(umq_log_config_set);
    DL_API_LOAD(umq_log_config_get);
    DL_API_LOAD(umq_dev_add);
    DL_API_LOAD(umq_get_route_list);
    DL_API_LOAD(umq_user_ctl);
    DL_API_LOAD(umq_mempool_state_get);
    DL_API_LOAD(umq_mempool_state_refresh);
    DL_API_LOAD(umq_dev_info_get);
    DL_API_LOAD(umq_dev_info_list_get);
    DL_API_LOAD(umq_dev_info_list_free);
    DL_API_LOAD(umq_cfg_get);
    DL_API_LOAD(umq_external_mutex_lock_ops_register);
    DL_API_LOAD(umq_external_rwlock_ops_register);
    DL_API_LOAD(umq_post);
    DL_API_LOAD(umq_poll);
    DL_API_LOAD(umq_interrupt_fd_get);
    DL_API_LOAD(umq_get_cq_event);

    /* step3: close handle */
    dlclose(handle);

    /* step4: set loaded */
    LOADED = true;
    return UBS_OK;
}

void UmqApi::UnLoad() noexcept
{
    std::lock_guard<std::mutex> guard(LOAD_MUTEX);
    if (!LOADED) {
        return;
    }

    UnLoadInner();
    LOADED = false;
}

void UmqApi::UnLoadInner() noexcept
{
    DL_API_SET_NULL(umq_init);
    DL_API_SET_NULL(umq_uninit);
    DL_API_SET_NULL(umq_create);
    DL_API_SET_NULL(umq_destroy);
    DL_API_SET_NULL(umq_bind_info_get);
    DL_API_SET_NULL(umq_bind);
    DL_API_SET_NULL(umq_unbind);
    DL_API_SET_NULL(umq_state_set);
    DL_API_SET_NULL(umq_state_get);
    DL_API_SET_NULL(umq_buf_alloc);
    DL_API_SET_NULL(umq_buf_free);
    DL_API_SET_NULL(umq_buf_break_and_free);
    DL_API_SET_NULL(umq_buf_headroom_reset);
    DL_API_SET_NULL(umq_buf_reset);
    DL_API_SET_NULL(umq_data_to_head);
    DL_API_SET_NULL(umq_enqueue);
    DL_API_SET_NULL(umq_dequeue);
    DL_API_SET_NULL(umq_notify);
    DL_API_SET_NULL(umq_rearm_interrupt);
    DL_API_SET_NULL(umq_wait_interrupt);
    DL_API_SET_NULL(umq_ack_interrupt);
    DL_API_SET_NULL(umq_buf_split);
    DL_API_SET_NULL(umq_async_event_fd_get);
    DL_API_SET_NULL(umq_get_async_event);
    DL_API_SET_NULL(umq_ack_async_event);
    DL_API_SET_NULL(umq_log_config_set);
    DL_API_SET_NULL(umq_log_config_get);
    DL_API_SET_NULL(umq_dev_add);
    DL_API_SET_NULL(umq_get_route_list);
    DL_API_SET_NULL(umq_user_ctl);
    DL_API_SET_NULL(umq_mempool_state_get);
    DL_API_SET_NULL(umq_mempool_state_refresh);
    DL_API_SET_NULL(umq_dev_info_get);
    DL_API_SET_NULL(umq_dev_info_list_get);
    DL_API_SET_NULL(umq_dev_info_list_free);
    DL_API_SET_NULL(umq_cfg_get);
    DL_API_SET_NULL(umq_external_mutex_lock_ops_register);
    DL_API_SET_NULL(umq_external_rwlock_ops_register);
    DL_API_SET_NULL(umq_post);
    DL_API_SET_NULL(umq_poll);
    DL_API_SET_NULL(umq_interrupt_fd_get);
    DL_API_SET_NULL(umq_get_cq_event);
}
} // namespace ubs
} // namespace ock

#else /* UMQ_ADAPTER_BACKEND_ENABLED */

namespace ock {
namespace ubs {
} // namespace ubs
} // namespace ock

#endif /* UMQ_DLOPEN_BACKEND_ENABLED */