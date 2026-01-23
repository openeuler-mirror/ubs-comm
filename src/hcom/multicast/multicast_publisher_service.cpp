/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "multicast_publisher_service_imp.h"
#include "multicast_periodic_manager.h"
#include "net_common.h"
#include "net_oob.h"
#include "utils/multicast_utils.h"
#include "include/multicast_publisher_service.h"

namespace ock {
namespace hcom {
static std::map<std::string, PublisherService *> g_multicastServiceMap;
static std::mutex g_mutex;

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

PublisherService *PublisherService::Create(const std::string &name, const MulticastServiceOptions &opt)
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

    PublisherService *service = new (std::nothrow) PublisherServiceImp();
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

int32_t PublisherService::Destroy(const std::string &name)
{
    std::lock_guard<std::mutex> locker(g_mutex);
    auto iter = g_multicastServiceMap.find(name);
    if (NN_UNLIKELY(iter == g_multicastServiceMap.end())) {
        NN_LOG_ERROR("Failed to destroy service, because service is not found or does not exist");
        return SER_ERROR;
    }

    PublisherService *service = iter->second;
    if (service == nullptr) {
        NN_LOG_ERROR("Failed to destroy service, because service empty");
        return SER_ERROR;
    }
    service->Stop();
    g_multicastServiceMap.erase(iter);
    delete service;
    return SER_OK;
}


static int DefaultNewEndPoint(const std::string &ipPort, const ock::hcom::UBSHcomNetEndpointPtr &ep,
    const std::string &payload)
{
    NN_LOG_INFO("new ep request!");
    return 0;
}

static void DefaultEndPointBroken(const ock::hcom::UBSHcomNetEndpointPtr &ep)
{
    NN_LOG_INFO("ep broken!");
    return;
}

static int DefaultRequestReceived(const ock::hcom::UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("request received!");
    return 0;
}

static int DefaultRequestPosted(const ock::hcom::UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("request posted!");
    return 0;
}

static int DefaultOneSideDone(const ock::hcom::UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("request oneside done!");
    return 0;
}
}
}
