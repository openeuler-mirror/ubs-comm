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
#ifndef UBS_COMM_UBSOCKET_LOCK_H
#define UBS_COMM_UBSOCKET_LOCK_H

#include "ubsocket_def.h"
#include "ubsocket_defines.h"
#include "ubsocket_errno.h"

namespace ock {
namespace ubs {

class LockRegistry {
public:
    static Result RegisterLockOps(u_external_lock_ops_t *ops);

    static Result RegisterRwLockOps(u_external_rw_lock_ops_t *ops);

    static Result RegisterSemOps(u_external_semaphore_ops_t *ops);

    static u_external_lock_ops_t LOCK_OPS;
    static u_external_rw_lock_ops_t RW_LOCK_OPS;
    static u_external_semaphore_ops_t SEM_OPS;
};
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_LOCK_H
