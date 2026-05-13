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
#include "ubsocket_global_setting.h"

namespace ock {
namespace ubs {
std::mutex GlobalSetting::MUTEX;
uint32_t GlobalSetting::UBS_ALLOWED_PROTOCOL = 0;
bool GlobalSetting::UBS_TRACE_ENABLED = false;
bool GlobalSetting::UBS_INITED = false;
bool GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = false;
bool GlobalSetting::UBS_CONNECTOR_ASYNC_ENABLED = false;
bool GlobalSetting::UBS_EPOLL_ASYNC_ENABLED = false;
u_external_lock_ops_t *GlobalSetting::lock_ops = nullptr;
u_external_rw_lock_ops_t *GlobalSetting::rw_lock_ops = nullptr;
u_external_semaphore_ops_t *GlobalSetting::sem_ops = nullptr;

/* envs */
#define ENV_TRACE_ENABLED "UBSOCKET_TRACE_ENABLE"
#define ENV_ASYNC_ACCEPTOR "UBSOCKET_ASYNC_ACCEPTOR_THREAD_COUNT"
#define ENV_ASYNC_CONNECTOR "UBSOCKET_ASYNC_CONNECTOR_THREAD_COUNT"
#define ENV_ASYNC_EPOLL "UBSOCKET_ASYNC_EPOLL_WAIT_THREAD_COUNT"

void GlobalSetting::AddRules() noexcept {}

Result GlobalSetting::LoadEnv() noexcept
{
    /* load from env */
    return UBS_OK;
}

} // namespace ubs
} // namespace ock