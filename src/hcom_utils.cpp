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

#include <thread>
#include <arpa/inet.h>
#include <unistd.h>

#include "hcom_utils.h"

namespace ock {
namespace hcom {

NetSpinLock NetUuid::gLock;
uint32_t NetUuid::gSeqNo = 0;
 
uint64_t NetUuid::GenerateUuid(const std::string& ip)
{
    struct Uuid {
        union {
            struct {
                uint64_t ip : 8;
                uint64_t pid : 20;
                uint64_t tid : 20;
                uint64_t seqNo : 16;
            };
            uint64_t value;
        };
    };

    if (ip.empty()) {
        return GenerateUuid();
    }

    // 仅使用最低位ip地址，如"xx1.xx2.xx3.xx4"中的"xx4"
    uint32_t ipNum = inet_addr(ip.c_str());
    ipNum >>= 0x18;

    int32_t pid = getpid();
    auto tid = pthread_self();

    struct Uuid res;
    res.ip = ipNum;
    res.tid = tid;
    res.pid = static_cast<uint64_t>(pid);

    gLock.Lock();
    gSeqNo += 1;
    res.seqNo = gSeqNo;
    gLock.Unlock();

    return res.value;
}

}
}
