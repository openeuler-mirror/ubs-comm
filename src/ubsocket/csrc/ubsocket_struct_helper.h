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
#ifndef UBS_COMM_UBSOCKET_STRUCT_HELPER_H
#define UBS_COMM_UBSOCKET_STRUCT_HELPER_H

#include "ubsocket_common_includes.h"
#include "ubsocket_def.h"

namespace ock {
namespace ubs {
static inline std::ostream &operator<<(std::ostream &os, const u_init_options_t &o)
{
    os << "u_init_options_t [allowed_protocol: " << o.allowed_protocol
       << ", async_acceptor_thread_count: " << o.async_acceptor_thread_count
       << ", async_connector_thread_count: " << o.async_connector_thread_count
       << ", async_epoll_thread_count: " << o.async_epoll_thread_count << ", lock_ops: " << std::hex << o.lock_ops
       << ", rw_lock_ops: " << o.rw_lock_ops << ", sem_ops: " << o.sem_ops << "]";
    return os;
}

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_STRUCT_HELPER_H
