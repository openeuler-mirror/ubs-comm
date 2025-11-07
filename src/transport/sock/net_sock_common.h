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
#ifndef OCK_HCOM_NET_SOCK_COMMON_H_234234
#define OCK_HCOM_NET_SOCK_COMMON_H_234234

#include <ifaddrs.h>
#include <vector>

#include "hcom.h"
#include "net_common.h"
#include "net_memory_region.h"
#include "net_oob.h"
#include "sock_worker.h"

namespace ock {
namespace hcom {
class NetAsyncEndpointSock;
class NetSyncEndpointSock;
class NetDriverSockWithOOB;

enum SockExchangeOp : int16_t {
    REAL_CONNECT = -1,
};

}
}

#endif // OCK_HCOM_NET_SOCK_COMMON_H_234234
