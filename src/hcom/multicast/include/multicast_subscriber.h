/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef HCOM_MULTICAST_SUBSCRIBER_H
#define HCOM_MULTICAST_SUBSCRIBER_H

#include <utility>

#include "hcom.h"
#include "hcom_def.h"
#include "multicast_message.h"
#include "hcom_service.h"
#include "hcom_service_context.h"

namespace ock {
namespace hcom {
class Subscriber;
using SubscriberPtr = NetRef<Subscriber>;
class SubscriberContext : public UBSHcomServiceContext {
public:
    explicit SubscriberContext(const UBSHcomNetRequestContext &context) : UBSHcomServiceContext(context, nullptr)
    {
        if (context.EndPoint() == nullptr) {
            NN_LOG_ERROR("ep in context is null");
            return;
        }
        mEp = context.EndPoint();
    }

    int Reply(const MultiRequest &req);

private:
    UBSHcomNetEndpointPtr mEp = nullptr;
};

class Subscriber {
public:
    Subscriber(std::string ip, uint16_t port, UBSHcomNetEndpointPtr ep)
        : mIp(std::move(ip)), mPort(port), mEp(std::move(ep)) {}

    // 仅用于SubscriberService::DestorySubscriber
    inline UBSHcomNetEndpointPtr &GetEp()
    {
        return mEp;
    }

    inline const std::string& GetIp() const
    {
        return mIp;
    }

    inline const uint16_t GetPort()
    {
        return mPort;
    }

    void Close();

    DEFINE_RDMA_REF_COUNT_FUNCTIONS;

private:
    std::string mIp;
    uint16_t mPort;
    UBSHcomNetEndpointPtr mEp;

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};

}  // namespace hcom
}  // namespace ock
#endif