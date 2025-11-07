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
#include "hcom_service.h"

#include <cstddef>
#include <mutex>
#include <new>
#include <linux/limits.h>

#include "hcom.h"
#include "hcom_log.h"
#include "hcom_def.h"
#include "hcom_err.h"
#include "hcom_service_def.h"
#include "service_imp.h"
#include "net_param_validator.h"

namespace ock {
namespace hcom {

using namespace ock::hcom;

static std::map<std::string, UBSHcomService *> g_serviceMap;
static std::mutex g_mutex;

static inline bool HcomServiceCreateCheck(const UBSHcomServiceOptions &opt)
{
    if (NN_UNLIKELY(opt.maxSendRecvDataSize == 0)) {
        NN_LOG_ERROR("Invalid maxSendDataSize: " << opt.maxSendRecvDataSize);
        return false;
    }
    if (NN_UNLIKELY(opt.workerGroupMode != NET_BUSY_POLLING
            && opt.workerGroupMode != NET_EVENT_POLLING)) {
        NN_LOG_ERROR("Invalid workerGroupMode: " << static_cast<uint32_t>(opt.workerGroupMode));
        return false;
    }
    if (NN_UNLIKELY(opt.workerThreadPriority < NN_NOF20 || opt.workerThreadPriority > NN_NO19)) {
        NN_LOG_ERROR("Invalid workerThreadPriority: " << opt.workerThreadPriority << ", must be [-20, 19]");
        return false;
    }
    return true;
}

UBSHcomService *UBSHcomService::Create(UBSHcomServiceProtocol t, const std::string &name,
    const UBSHcomServiceOptions &opt)
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
    auto iter = g_serviceMap.find(name);
    if (iter != g_serviceMap.end()) {
        return iter->second;
    }

    UBSHcomService *service = new (std::nothrow) HcomServiceImp(t, name, opt);
    if (service == nullptr) {
        NN_LOG_ERROR("failed to create netServiceImp for service");
        return nullptr;
    }

    SerResult result = HcomServiceGlobalObject::Initialize();
    if (NN_UNLIKELY(result != SER_OK)) {
        delete service;
        service = nullptr;
        NN_LOG_ERROR("Failed to create serviceNetServiceGlobalObject initialize ");
        return nullptr;
    }

    g_serviceMap.emplace(name, service);
    service->IncreaseRef();
    return service;
}

int32_t UBSHcomService::Destroy(const std::string &name)
{
    std::lock_guard<std::mutex> locker(g_mutex);
    auto iter = g_serviceMap.find(name);
    if (NN_UNLIKELY(iter == g_serviceMap.end())) {
        NN_LOG_ERROR("Failed to destroy service, because service is not found or does not exist");
        return SER_ERROR;
    }

    UBSHcomService *service = iter->second;
    if (service == nullptr) {
        NN_LOG_ERROR("Failed to destroy service, because service empty");
        return SER_ERROR;
    }
    int32_t res = service->DoDestroy(name);
    if (NN_UNLIKELY(res != SER_OK)) {
        NN_LOG_ERROR("Failed to destroy service, DoDestroy failed");
        return res;
    }
    HcomServiceGlobalObject::UnInitialize();
    g_serviceMap.erase(iter);
    service->DecreaseRef();
    return SER_OK;
}

}
}