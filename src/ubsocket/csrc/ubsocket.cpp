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
#include "common/ubsocket_print_stats_mgr.h"
#include "common/ubsocket_signal_handler.h"
#include "common/ubsocket_version.h"
#include "core/ubsocket_event_epoll.h"
#include "core/ubsocket_socket_set.h"
#include "core/umq/umq_backend.h"
#include "core/umq/umq_setting.h"
#include "include/ubsocket.h"
#include "ubsocket_struct_helper.h"
#include "under_api/dl_api.h"
#include "core/umq/umq_setting.h"
#include "cli/statistics.h"
#include "cli/probe_manager.h"

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
    GlobalSetting::LoadEnv();

    GlobalSetting::UBS_ALLOWED_PROTOCOL = options->allowed_protocol;
    // 只有当命令行参数非默认值时，才覆盖环境变量的值
    // 0 是默认值，表示用户没有显式设置
    if (options->async_acceptor_thread_count > 0) {
        GlobalSetting::UBS_ACCEPTOR_ASYNC_THREAD_COUNT = options->async_acceptor_thread_count;
    }
    if (options->async_connector_thread_count > 0) {
        GlobalSetting::UBS_CONNECTOR_ASYNC_THREAD_COUNT = options->async_connector_thread_count;
    }
    if (options->async_epoll_thread_count > 0) {
        GlobalSetting::UBS_EPOLL_ASYNC_THREAD_COUNT = options->async_epoll_thread_count;
    }

    auto result = GlobalSetting::VerifySetting();
    if (result != UBS_OK) {
        UBS_VLOG_ERR("initialize failed as options are invalid");
        errno = EINVAL;
        return UBS_ERROR;
    }

    /* step2: load under api */
    UBS_VLOG_DEBUG("load under api");
    result = DlApi::Load(LOAD_LIBC | LOAD_UMQ);
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
    g_socket_epoll_lock = LockRegistry::RW_LOCK_OPS.create();
    ArraySet<EventPoll>::GetInstance().Init();

    /* step5: load brpc symbol for zcopy */
    // TODO: UbsZcopyAdapter 中成员变量原在 context 中, 重构后需要在一个地方保存
    if (GlobalSetting::USE_BRPC_ZCOPY) {
        ZeroCopyPrepare();
    }

    /* step5: umq backend init */
    //#ifdef UMQ_ADAPTER_BACKEND_ENABLED
    result = umq::UmqBackend::Init();
    if (result != UBS_OK) {
        // ResetBrpcAllocator();
        // SetSocketFdTransMode(SOCKET_FD_TRANS_MODE_TCP);
        UBS_VLOG_ERR("umq backend init failed, ret: %d\n", result);
        return UBS_ERROR;
    }
    //#endif

    /* step6: register signal handler */
    std::signal(SIGUSR2, ubsocket_handle_signal);

    if (GlobalSetting::UBS_PROF_ENABLE) {
        result = Profiling::Init(ProfilingTPId::UBSOCKET_PROF_COUNT, GlobalSetting::UBS_PROF_DUMP_PATH.c_str(),
                                 GlobalSetting::UBS_PROF_DUMP_INTERVAL_MIN);
        if (result != UBS_OK) {
            UBS_VLOG_WARN("Profiling is not initialize \n");
        }
    }

    if (GlobalSetting::UBS_CLI_ENABLED) {
        (void)Statistics::GlobalStatsMgr::GetGlobalStatsMgr(umq::UmqSetting::UMQ_TRANS_MODE);
    }

    if (GlobalSetting::UBS_PROBE_ENABLED) {
        (void)Statistics::ProbeManager::GetInstance().Start(GlobalSetting::UBS_PROBE_MS, GlobalSetting::UBS_PROBE_BATCH, -1);
    }

    /* last step: set initialized */
    GlobalSetting::UBS_INITED = true;

    /* do trace log initial */
    if (GlobalSetting::UBS_TRACE_ENABLED) {
        ubsocket_trace_statistic_init();
    }

    return UBS_OK;
}

void ubsocket_uninit()
{
    if (GlobalSetting::UBS_PROF_ENABLE) {
        Profiling::Uninit();
    }
    /* do trace log destroy */
    if (GlobalSetting::UBS_TRACE_ENABLED) {
        ubsocket_trace_statistic_destroy();
    }
    umq::UmqBackend::UnInit();
    return;
}

UBS_API const char *ubsocket_version()
{
    /* log full version */
    std::cout << "full version: " << UBS_LIB_VERSION_FULL << std::endl;
    /* return short version */
    return UBS_LIB_VERSION;
}

void UmqLogger(int level, char *log_msg)
{
    auto new_level = umq_log_level::UMQ_LOG_LEVEL_DEBUG - level;
    if (new_level <= LogLevel::LEVEL_ERR) {
        UBS_LOG_STREAM_RAW(new_level, log_msg);
    } else {
        static const char *OTHER_LEVEL[] = {"EMERG", "ALERT", "CRIT"};
        UBS_LOG_STREAM_RAW(LogLevel::LEVEL_ERR, OTHER_LEVEL[level % (sizeof(OTHER_LEVEL))] << ", " << log_msg);
    }
}

UBS_API int ubsocket_set_logger(void (*func)(int level, const char *msg, const char *filename, int line))
{
    Logger::Instance().SetExternalLogFunction(func);
    umq_log_config_t log_cfg = {
        .log_flag = UMQ_LOG_FLAG_FUNC | UMQ_LOG_FLAG_LEVEL,
        .func = UmqLogger,
        .level = static_cast<umq_log_level_t>(umq_log_level::UMQ_LOG_LEVEL_DEBUG - Logger::Instance().GetLogLevel())
    };
    return umq_log_config_set(&log_cfg);
}

UBS_API int ubsocket_set_log_level(int level)
{
    Logger::Instance().SetLogLevel(level);
    return UBS_OK;
}

UBS_API void *ubsocket_iobuf_allocate(size_t size)
{
    // new real zcopy allocator
    uint32_t type = GlobalSetting::UBS_ALLOWED_PROTOCOL;
    if (type == UBS_PROTOCOL_UB_RM_RTP || type == UBS_PROTOCOL_UB_RC_RTP) {
        // delete at ubsocket_uninit
        if (g_zcopy_allocator == nullptr) {
            g_zcopy_allocator = new (std::nothrow) umq::UmqZeroCopyAllocator();
        }
    } else {
        UBS_VLOG_WARN("unknown zcopy allocator type");
        return nullptr;
    }

    return blockmem_allocate_zero_copy(size);
}

UBS_API void ubsocket_iobuf_deallocate(void *addr)
{
    blockmem_deallocate_zero_copy(addr);
}

UBS_API void ubsocket_trace_statistic_init(void)
{
    umq_trans_mode_t transMode = umq::UmqSetting::UMQ_TRANS_MODE;
    Statistics::PrintStatsMgr::StartStatsCollection(
        GlobalSetting::UBS_TRACE_TIME, GlobalSetting::UBS_TRACE_FILE_PATH,
        GlobalSetting::UBS_TRACE_FILE_SIZE, transMode);
}

UBS_API void ubsocket_trace_statistic_destroy(void)
{
    Statistics::PrintStatsMgr::StopStatsCollection();
}