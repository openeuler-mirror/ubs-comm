/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <iostream>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_version.h"
#include "core/ubsocket_socket_set.h"
#include "core/umq/umq_backend.h"
#include "include/ubsocket.h"
#include "ubsocket_struct_helper.h"
#include "ubsocket_version.h"
#include "umq_setting.h"
#include "ubsocket_event_epoll.h"
#include "under_api/dl_api.h"

using namespace ock::ubs;

UBS_API int ubsocket_init_options(u_init_options_t *options)
{
    if (options == nullptr) {
        errno = EINVAL;
        return UBS_ERROR;
    }

    UBS_SLOG_DEBUG(*options);

    options->allowed_protocol = UBS_PROTOCOL_TCP; /* use raw tcp by default */
    options->async_acceptor_thread_count = 0;     /* tune off by default */
    options->async_connector_thread_count = 0;    /* tune off by default */
    options->async_epoll_thread_count = 1;        /* tune on by default */
    options->lock_ops = nullptr;
    options->rw_lock_ops = nullptr;
    options->sem_ops = nullptr;
    return UBS_OK;
}

void ZeroCopyPrepare()
{
    // new real zcopy allocator
    uint32_t type = GlobalSetting::UBS_ALLOWED_PROTOCOL;
    if (type == UBS_PROTOCOL_UB_RM_RTP || type == UBS_PROTOCOL_UB_RC_RTP) {
        // delete at ubsocket_uninit
        g_zcopy_allocator = new (std::nothrow) umq::UmqZeroCopyAllocator();
    } else {
        UBS_VLOG_WARN("unknown zcopy allocator type");
        return;
    }

    // load brpc symbol for zcopy
    UbsZcopyAdapter adapter;
    if (!adapter.Intercept(GlobalSetting::UBS_BRPC_ALLOC_SYM_STR.c_str(),
                           GlobalSetting::UBS_BRPC_DEALLOC_SYM_STR.c_str())) {
        // intercept failed，fallback to TCP
        UBS_VLOG_WARN("Failed to hook brpc allocator, fallback to TCP mode");
        GlobalSetting::UBS_NATIVE_TCP_MODE = true;
    }
    UBS_VLOG_INFO("Successfully hooked brpc zero-copy allocator");
}

UBS_API int ubsocket_init(u_init_options_t *options)
{
    if (options == nullptr) {
        errno = EINVAL;
        return UBS_ERROR;
    }

    /* do this under mutex */
    std::lock_guard<std::mutex> guard(GlobalSetting::MUTEX);
    if (GlobalSetting::UBS_INITED) {
        return UBS_OK;
    }

    /* do initialization */
    /* step1: global setting */
    UBS_VLOG_DEBUG("init global setting");
    GlobalSetting::AddRules();

    GlobalSetting::UBS_ALLOWED_PROTOCOL = options->allowed_protocol;
    GlobalSetting::UBS_ACCEPTOR_ASYNC_THREAD_COUNT = options->async_acceptor_thread_count;
    GlobalSetting::UBS_CONNECTOR_ASYNC_THREAD_COUNT = options->async_connector_thread_count;
    GlobalSetting::UBS_EPOLL_ASYNC_THREAD_COUNT = options->async_epoll_thread_count;

    auto result = GlobalSetting::VerifySetting();
    if (result != UBS_OK) {
        UBS_VLOG_ERR("initialize failed as options are invalid");
        errno = EINVAL;
        return UBS_ERROR;
    }

    /* step2: load under api */
    UBS_VLOG_DEBUG("load under api");
    result = DlApi::Load(LOAD_LIBC);
    if (result != UBS_OK) {
        errno = EBADF;
        return UBS_ERROR;
    }

    /* step3: register lock */
    UBS_VLOG_DEBUG("register mutex and sem ops");
    (void)LockRegistry::RegisterDefaultOps();

    if (options->lock_ops != nullptr) {
        result = LockRegistry::RegisterLockOps(options->lock_ops);
        if (result != UBS_OK) {
            errno = EBADF;
            return UBS_ERROR;
        }
    }

    if (options->rw_lock_ops != nullptr) {
        result = LockRegistry::RegisterRwLockOps(options->rw_lock_ops);
        if (result != UBS_OK) {
            errno = EBADF;
            return UBS_ERROR;
        }
    }

    if (options->sem_ops != nullptr) {
        result = LockRegistry::RegisterSemOps(options->sem_ops);
        if (result != UBS_OK) {
            errno = EBADF;
            return UBS_ERROR;
        }
    }

    /* step3: socket related initialization */
    SocketSet::Instance().Init();

    /* step4: umq backend init */
#ifdef UMQ_BACKEND_ENABLED
    umq::UmqBackend::Init();
#endif

    /* step5: load brpc symbol for zcopy */
    if (GlobalSetting::USE_BRPC_ZCOPY) {
        ZeroCopyPrepare();
    }

    /* last step: set initialized */
    GlobalSetting::UBS_INITED = true;

    return UBS_OK;
}

UBS_API void ubsocket_uninit(int flags)
{
    return;
}

UBS_API const char *ubsocket_version()
{
    /* log full version */
    std::cout << "full version: " << UBS_LIB_VERSION_FULL << std::endl;
    /* return short version */
    return UBS_LIB_VERSION;
}

UBS_API int ubsocket_set_logger(void (*func)(int level, const char *msg, const char *filename, int line))
{
    Logger::Instance().SetExternalLogFunction(func);
    return UBS_OK;
}

UBS_API int ubsocket_set_log_level(int level)
{
    Logger::Instance().SetLogLevel(level);
    return UBS_OK;
}