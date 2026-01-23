 /*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "include/multicast_subscriber_service.h"
#include "multicast_subscriber_service_imp.h"
#include "net_common.h"
#include "net_oob.h"
#include "utils/multicast_utils.h"

namespace ock {
namespace hcom {
static std::map<std::string, SubscriberService *> g_multicastServiceMap;
static std::mutex g_mutex;

/****************************************************************
 * 创建SubscriberService
 ****************************************************************/

static inline bool HcomServiceCreateCheck(const MulticastServiceOptions &opt)
{
    if (NN_UNLIKELY(opt.maxSendRecvDataSize == 0)) {
        NN_LOG_ERROR("Invalid maxSendDataSize: " << opt.maxSendRecvDataSize);
        return false;
    }
    if (NN_UNLIKELY(opt.workerGroupMode != NET_BUSY_POLLING && opt.workerGroupMode != NET_EVENT_POLLING)) {
        NN_LOG_ERROR("Invalid workerGroupMode: " << static_cast<uint32_t>(opt.workerGroupMode));
        return false;
    }
    if (NN_UNLIKELY(opt.workerThreadPriority < NN_NOF20 || opt.workerThreadPriority > NN_NO19)) {
        NN_LOG_ERROR("Invalid workerThreadPriority: " << opt.workerThreadPriority << ", must be [-20, 19]");
        return false;
    }
    return true;
}

SubscriberService *SubscriberService::Create(const std::string &name, const MulticastServiceOptions &opt)
{
    if (NN_UNLIKELY(!HcomServiceCreateCheck(opt))) {
        NN_LOG_ERROR("invalid options for service create");
        return nullptr;
    }

    if (name.length() > NN_NO64) {
        NN_LOG_ERROR("Invalid param, name length must be less than " << NN_NO64);
        return nullptr;
    }
    std::lock_guard<std::mutex> locker(g_mutex);
    auto iter = g_multicastServiceMap.find(name);
    if (iter != g_multicastServiceMap.end()) {
        return iter->second;
    }

    SubscriberService *service = new (std::nothrow) SubscriberServiceImp();
    if (service == nullptr) {
        NN_LOG_ERROR("failed to create netServiceImp for service");
        return nullptr;
    }

    if (!service->GetConfig().Init(name, opt)) {
        NN_LOG_ERROR("Init service config failed!");
        delete service;
        return nullptr;
    }

    g_multicastServiceMap.emplace(name, service);
    return service;
}

int32_t SubscriberService::Destroy(const std::string &name)
{
    std::lock_guard<std::mutex> locker(g_mutex);
    auto iter = g_multicastServiceMap.find(name);
    if (NN_UNLIKELY(iter == g_multicastServiceMap.end())) {
        NN_LOG_ERROR("Failed to destroy service, because service is not found or does not exist");
        return SER_ERROR;
    }

    SubscriberService *service = iter->second;
    if (service == nullptr) {
        NN_LOG_ERROR("Failed to destroy service, because service empty");
        return SER_ERROR;
    }
    service->Stop();
    g_multicastServiceMap.erase(iter);
    delete service;
    return SER_OK;
}
}
}
