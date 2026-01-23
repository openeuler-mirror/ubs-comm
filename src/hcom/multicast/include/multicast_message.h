/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef HCOM_MULTICAST_MESSAGE_H
#define HCOM_MULTICAST_MESSAGE_H

#include "hcom_service.h"

namespace ock {
namespace hcom {
class PublisherContext;
struct MultiRequest {
    void *data = nullptr; /* 数据地址 */
    uint32_t size = 0;    /* 数据大小 */
    uint64_t lkey = 0;    /* 已注册内存的lkey */

    MultiRequest(void *d, uint32_t s) : data(d), size(s) {}
    MultiRequest(void *d, uint32_t s, uint64_t lk) : data(d), size(s), lkey(lk) {}
};

struct MultiResponse {
    void *data = nullptr; /* 数据地址 */
    uint32_t size = 0;    /* 数据大小 */

    MultiResponse() {};
    MultiResponse(void *d, uint32_t s) : data(d), size(s) {}
};

using MulticastNewEpHandler = std::function<int(const std::string &ipPort,
    const ock::hcom::UBSHcomNetEndpointPtr &ep, const std::string &payload)>;
using MulticastEpBrokenHandler = std::function<void(const ock::hcom::UBSHcomNetEndpointPtr &ep)>;

using MulticastPubReqRecvHandler = std::function<int(ock::hcom::PublisherContext &ctx)>;
using MulticastReqRecvHandler = std::function<int(ock::hcom::UBSHcomServiceContext &ctx)>;
using MulticastReqPostedHandler = std::function<int(ock::hcom::UBSHcomServiceContext &ctx)>;
}
}

#endif