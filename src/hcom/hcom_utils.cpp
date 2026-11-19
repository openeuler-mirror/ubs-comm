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

#include <unistd.h>
#include <fstream>
#include <thread>

#include "hcom_utils.h"

namespace ock {
namespace hcom {

NetSpinLock NetUuid::gLock;
uint32_t NetUuid::gSeqNo = 0;

namespace {

/* Process-unique prefix: prefer /dev/urandom, fall back to pid and start time on failure.
 * It does not depend on pid or time buckets, so duplicate ids cannot be produced when pids
 * collide inside containers or the sequence restarts from zero after a process restart. */
uint32_t GenerateUuidProcPrefix()
{
    uint32_t prefix = 0;
    std::ifstream urandom("/dev/urandom", std::ios::in | std::ios::binary);
    if (urandom.is_open()) {
        urandom.read(reinterpret_cast<char *>(&prefix), sizeof(prefix));
        if (!urandom) {
            prefix = 0;
        }
        urandom.close();
    }

    if (prefix == NN_NO0) {
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        prefix = static_cast<uint32_t>(now) ^ (static_cast<uint32_t>(getpid()) << NN_NO16);
    }
    return prefix;
}

} // namespace

uint32_t NetUuid::ProcPrefix()
{
    return GenerateUuidProcPrefix();
}

uint64_t NetUuid::GenerateUuid(const std::string & /* ip */)
{
    /* Share the same generator with the no-arg version: ip is carried separately by SerUuid.ip and
     * is no longer encoded into channelId, avoiding periodic id duplication caused by bit-field
     * truncation (ip last octet only, pid/tid 20 bits each, seqNo 16 bits). */
    return GenerateUuid();
}

} // namespace hcom
} // namespace ock
