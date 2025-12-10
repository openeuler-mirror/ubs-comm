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
#ifndef HCOM_API_HCOM_CLASSES_H_
#define HCOM_API_HCOM_CLASSES_H_

#include <cstddef>
#include <cstdint>
#include <vector>
#include "securec.h"

#include "hcom.h"
#include "hcom_def.h"
#include "hcom_num_def.h"

namespace ock {
namespace hcom {

constexpr uint16_t MAX_MULTI_RAIL_NUM = 4;

using SerResult = int;
using UBSHcomDriverSecInfoProvider = std::function<int(uint64_t ctx, int64_t &flag, UBSHcomNetDriverSecType &type,
    char *&output, uint32_t &outLen, bool &needAutoFree)>;
using UBSHcomDriverSecInfoValidator =
    std::function<int(uint64_t ctx, int64_t flag, const char *input, uint32_t inputLen)>;
using UBSHcomWorkerMode = UBSHcomNetDriverWorkingMode;
using UBSHcomChTypeIndex = uint16_t;
using UBSHcomCipherSuite = UBSHcomNetCipherSuite;
using UBSHcomMemoryRegionPtr = UBSHcomNetMemoryRegionPtr;

enum class UBSHcomChannelBrokenPolicy : uint8_t {
    BROKEN_ALL, /* when one ep broken, all eps broken */
    RECONNECT, /* when one ep broken, try re-connect first. If re-connect fail, broken all eps */
    KEEP_ALIVE, /* when one ep broken, keep left eps alive until all eps broken */
};

enum class UBSHcomClientPollingMode : uint8_t {
    WORKER_POLL = 0,
    SELF_POLL_BUSY = 1,
    SELF_POLL_EVENT = 2,
    UNKNOWN = 255,
};

enum class UBSHcomChannelCallBackType : uint8_t {
    CHANNEL_FUNC_CB,
    CHANNEL_GLOBAL_CB,
};

enum class UBSHcomOobType : uint8_t {
    TCP,
    UDS,
};

enum class UBSHcomSecType : uint8_t {
    NET_SEC_DISABLED,
    NET_SEC_VALID_ONE_WAY,
    NET_SEC_VALID_TWO_WAY,
};
struct UBSHcomRequest {
    void *address = nullptr;                    /* pointer of data */
    uint32_t size = 0;                          /* size of data */
    uint64_t key = 0;
    uint16_t opcode = 0;                        /* operation code of request */

    UBSHcomRequest() = default;
    UBSHcomRequest(void *addr, uint32_t sz, uint16_t op) : address(addr), size(sz), opcode(op) {}
};

struct UBSHcomResponse {
    void *address = nullptr;                    /* pointer of data */
    uint32_t size = 0;                          /* size of data */
    int16_t errorCode = 0;                      /* error code of response */

    UBSHcomResponse() = default;
    UBSHcomResponse(void *addr, uint32_t sz) : address(addr), size(sz) {}
};

struct UBSHcomSglRequest {
    UBSHcomRequest *iov = nullptr;
    uint16_t iovCount = 0;
};

struct UBSHcomMemoryKey {
    uint64_t keys[4];
    uint64_t tokens[4];
    uint8_t eid[16];
};

struct UBSHcomOneSideRequest {
    uintptr_t lAddress = 0;
    uintptr_t rAddress = 0;
    UBSHcomMemoryKey lKey;
    UBSHcomMemoryKey rKey;
    uint32_t size = 0;
};

struct UBSHcomOneSideSglRequest {
    UBSHcomOneSideRequest *iov  = nullptr;
    uint16_t iovCount = 0;
};

struct UBSHcomReplyContext {
    uintptr_t rspCtx = 0;
    int16_t errorCode = 0;
    UBSHcomReplyContext() = default;
    UBSHcomReplyContext(uintptr_t ctx, int16_t errCode) : rspCtx(ctx), errorCode(errCode) {}
};

struct UBSHcomIov {
    void *address = nullptr;
    uint32_t size = 0;
};

struct UBSHcomServiceOptions {
    uint32_t maxSendRecvDataSize = 1024;    // 发送数据块最大值
    uint16_t workerGroupId = 0;     // group id of the worker group, must increment from 0 and be unique
    uint16_t workerGroupThreadCount = 1;    // worker线程数，如果设置为0的话，不启动worker线程
    UBSHcomWorkerMode workerGroupMode = NET_BUSY_POLLING;  // worker线程工作模式，默认busy_polling
    int8_t workerThreadPriority = 0;    // 线程优先级[-20,19]，19优先级最低，-20优先级最高，同nice值
    std::pair<uint32_t, uint32_t> workerGroupCpuIdsRange = {UINT32_MAX, UINT32_MAX};  // default not bind
};

struct UBSHcomConnectOptions {
    uint16_t clientGroupId = 0;     // worker group id of client
    uint16_t serverGroupId = 0;     // worker group id of server
    uint8_t linkCount = 1;     // actual link count of the channel
    UBSHcomClientPollingMode mode = UBSHcomClientPollingMode::WORKER_POLL;
    UBSHcomChannelCallBackType cbType = UBSHcomChannelCallBackType::CHANNEL_FUNC_CB;
    std::string payload;
};

struct UBSHcomMultiRailOptions {
    uint32_t threshold = 8192;     // threshold of multirail
    bool enable = true;                 // multi switch
};

struct UBSHcomTlsOptions {
    UBSHcomTLSCaCallback caCb = nullptr;
    UBSHcomTLSCertificationCallback cfCb = nullptr;
    UBSHcomTLSPrivateKeyCallback pkCb = nullptr;
    UBSHcomPskUseSessionCb pskUseCb = nullptr;
    UBSHcomPskFindSessionCb pskFindCb = nullptr;
    UBSHcomTlsVersion tlsVersion = UBSHcomTlsVersion::TLS_1_3;
    UBSHcomCipherSuite netCipherSuite = UBSHcomCipherSuite::AES_GCM_128;
    bool enableTls = true;
};

struct UBSHcomConnSecureOptions {
    UBSHcomDriverSecInfoProvider provider = nullptr;
    UBSHcomDriverSecInfoValidator validator = nullptr;
    uint16_t magic = 256;
    uint8_t version = 0;
    UBSHcomNetDriverSecType secType = UBSHcomNetDriverSecType::NET_SEC_DISABLED;
};

struct UBSHcomHeartBeatOptions {
    uint16_t heartBeatIdleSec = 60;    // 发送心跳保活消息间隔时间
    uint16_t heartBeatProbeTimes = 7;  // 发送心跳探测失败/没收到回复重试次数，超了认为连接已经断开
    uint16_t heartBeatProbeIntervalSec = 2;    // 发送心跳后再次发送时间
};

enum class UBSHcomFlowCtrlLevel : uint8_t {
    HIGH_LEVEL_BLOCK, /* spin-wait by busy loop */
    LOW_LEVEL_BLOCK,  /* full sleep */
};

struct UBSHcomFlowCtrlOptions {
    uint16_t intervalTimeMs = 0;
    uint64_t thresholdByte = 0;
    UBSHcomFlowCtrlLevel flowCtrlLevel = UBSHcomFlowCtrlLevel::LOW_LEVEL_BLOCK;
};

struct UBSHcomTwoSideThreshold {
    uint32_t splitThreshold = UINT32_MAX;  // UBC 专用。此值表示拆包发送的阈值，也可以当做拆包发送时每个小包的
                                      // 最大长度(含额外头部). 一般将其配置成小于等于 SegSize 的值，可配置范围
                                      // 为 [128, maxSendRecvDataSize]. 特别的配置成 UINT32_MAX 会禁用拆包功能。
    uint32_t rndvThreshold = UINT32_MAX;  // rndv阈值，请求长度大于等于该值,则启用RNDV。
};

class UBSHcomRegMemoryRegion {
public:
    inline void GetMemoryKey(UBSHcomMemoryKey &mrKey)
    {
        for (uint32_t i = 0; i < mHcomMrs.size(); i++) {
            if (i >= MAX_MULTI_RAIL_NUM) {
                break;
            }
            mrKey.keys[i] = mHcomMrs[i]->GetLKey();
            mrKey.tokens[i] = reinterpret_cast<uint64_t>(mHcomMrs[i]->GetMemorySeg());
        }
        if (mHcomMrs.size() > 0 && mHcomMrs[0]->GetEidRaw() != nullptr) {
            auto ret = memcpy_s(mrKey.eid, sizeof(mrKey.eid), mHcomMrs[0]->GetEidRaw(), sizeof(mrKey.eid));
            if (ret != 0) {
                NN_LOG_WARN("memcpy eid failed");
            }
        } else {
            NN_LOG_DEBUG("get eid not supported");
        }
    }

    inline uintptr_t GetAddress()
    {
        if (mHcomMrs.empty() || mHcomMrs[0] == nullptr) {
            return 0;
        }
        return mHcomMrs[0]->GetAddress();
    }

    inline uint64_t GetSize()
    {
        if (mHcomMrs.empty() || mHcomMrs[0] == nullptr) {
            return 0;
        }
        return mHcomMrs[0]->Size();
    }

    inline std::vector<UBSHcomMemoryRegionPtr>& GetHcomMrs()
    {
        return mHcomMrs;
    }

private:
    std::vector<UBSHcomMemoryRegionPtr> mHcomMrs;
};
}
}
#endif // HCOM_API_HCOM_CLASSES_H_