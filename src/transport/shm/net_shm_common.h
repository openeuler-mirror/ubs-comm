/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef HCOM_NET_SHM_COMMON_H
#define HCOM_NET_SHM_COMMON_H

#include "hcom.h"
#include "net_common.h"
#include "net_memory_region.h"
#include "net_mem_pool_fixed.h"
#include "net_oob.h"
#include "net_oob_ssl.h"
#include "shm_common.h"
#include "shm_channel.h"
#include "shm_worker.h"

namespace ock {
namespace hcom {
class NetAsyncEndpointShm;
class NetSyncEndpointShm;

class NetDriverShmWithOOB;

}
}

#endif // HCOM_NET_SHM_COMMON_H
