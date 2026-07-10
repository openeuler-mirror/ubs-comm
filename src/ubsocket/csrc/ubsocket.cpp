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
#include "common/ubsocket_signal_handler.h"
#include "common/ubsocket_version.h"
#include "core/ubsocket_event_epoll.h"
#include "core/ubsocket_tx_cqe_poller.h"
#include "core/umq/umq_backend.h"
#include "core/umq/umq_setting.h"
#include "include/ubsocket.h"
#include "profiling/probe/probe_manager.h"
#include "profiling/statistics/statistics.h"
#include "profiling/statistics/ubsocket_print_stats_mgr.h"
#include "profiling/trace/ubsocket_trace.h"
#include "ubsocket_struct_helper.h"
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
    options->rpc_id_ops = nullptr;
    options->poller_ops = nullptr;
    return UBS_OK;
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
    if (options->poller_ops != nullptr) {
        GlobalSetting::UBS_POLLER_OPS = options->poller_ops;
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

    if (options->rpc_id_ops != nullptr) {
        result = TraceRegistry::RegisterRpcIdOps(options->rpc_id_ops);
        if (result != UBS_OK) {
            errno = EBADF;
            return UBS_ERROR;
        }
    }

    /* step3: socket related initialization */
    ArraySet<Socket>::GetInstance().Init();
    g_socket_epoll_lock = LockRegistry::RW_LOCK_OPS.create();
    ArraySet<EventPoll>::GetInstance().Init();

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
        (void)Statistics::ProbeManager::GetInstance().Start(GlobalSetting::UBS_PROBE_MS, GlobalSetting::UBS_PROBE_BATCH,
                                                            -1);
    }

    /* last step: set initialized */
    GlobalSetting::UBS_INITED = true;

    UBS_VLOG_DEBUG("UBSOCKET_ASYNC_ACCEPT:%d\n", GlobalSetting::AsyncAcceptorEnabled() ? 1 : 0);
    /* do trace log initial */
    if (GlobalSetting::UBS_TRACE_ENABLED) {
        umq_trans_mode_t transMode = umq::UmqSetting::UMQ_TRANS_MODE;
        Statistics::PrintStatsMgr::StartStatsCollection(GlobalSetting::UBS_TRACE_TIME,
                                                        GlobalSetting::UBS_TRACE_FILE_PATH,
                                                        GlobalSetting::UBS_TRACE_FILE_SIZE, transMode);
    }
#ifdef UBS_SPLIT_TRACE_ENABLED_COMPILE
    if (GlobalSetting::UBS_SPLIT_TRACE_ENABLED) {
        if ((GlobalSetting::UBS_SPLIT_TRACE_LEVEL & SplitTraceLevel::LEVEL_UBSOCKET) != SplitTraceLevel::LEVEL_NONE) {
            TracePrintThread::Instance().Start();
        }

        if ((GlobalSetting::UBS_SPLIT_TRACE_LEVEL & SplitTraceLevel::LEVEL_UMQ) != SplitTraceLevel::LEVEL_NONE) {
            umq_trace_cfg_t cfg = {};
            cfg.flag = UMQ_TRACE_FLAG_RECORD_NUM | UMQ_TRACE_FLAG_OUTPUT_LIMIT;
            cfg.record_num = GlobalSetting::UBS_SPLIT_TRACE_BUF_CAPACITY;
            cfg.output_limit = 1;

            int trace_ret = UmqApi::umq_stats_trace_start(&cfg);
            if (trace_ret != 0) {
                UBS_VLOG_WARN("umq stats trace start failed, ret: %d\n", trace_ret);
            }
        }
    }
#endif

    return UBS_OK;
}

void ubsocket_uninit()
{
    if (GlobalSetting::UBS_PROF_ENABLE) {
        Profiling::Uninit();
    }
#ifdef UBS_SPLIT_TRACE_ENABLED_COMPILE
    /* do trace log destroy */
    if (GlobalSetting::UBS_SPLIT_TRACE_ENABLED) {
        if ((GlobalSetting::UBS_SPLIT_TRACE_LEVEL & SplitTraceLevel::LEVEL_UBSOCKET) != SplitTraceLevel::LEVEL_NONE) {
            TracePrintThread::Instance().Stop();
            ArraySet<Socket>::GetInstance().ForEach([](int, Socket *sock) { TRACE_FLUSH(sock->split_trace_); });
        }

        if ((GlobalSetting::UBS_SPLIT_TRACE_LEVEL & SplitTraceLevel::LEVEL_UMQ) != SplitTraceLevel::LEVEL_NONE) {
            UmqApi::umq_stats_trace_stop();
        }
    }
#endif
    if (GlobalSetting::UBS_TRACE_ENABLED) {
        Statistics::PrintStatsMgr::StopStatsCollection();
    }

    // 用户需要在程序退出时调用 ubsocket_uninit 保证所有的 socket ref 释放，否则会延迟至 ArraySet 单例析构。在 brpc 场
    // 景下，由于 brpc worker 不会主动 join, worker 可能仍会尝试访问 ArraySet<Socket> 或者 ArraySet<EventPoll>
    TxCqePoller::Instance().Stop();
    ArraySet<Socket>::GetInstance().ReleaseAll();
    ArraySet<EventPoll>::GetInstance().ReleaseAll();

    // EpollRunner 是 LeakySingleton, 进程退出时不会自动析构、后台 poller 线程不会自动 join。必须在 umq_uninit 之前
    // 停止这些 runner, 否则线程仍会对已释放的 umq/mempool/tseg 执行 umq_poll, 触发 "mempool tseg not exist"。
    // 注意顺序: TRANSPORT_POOL_TX_RUNNER 必须在 ReleaseAll(Socket) 之后停止, 因为 socket 析构链
    // (UmqSocket::UnInitialize) 会调用其 DelEpollEvent, 而 Stop() 会将 ops_ 置空, 提前 stop 会导致空指针。
    EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER).Stop();
    EpollRunnerFactory::GetInstance(EpollRunnerType::TRANSPORT_POOL_TX_RUNNER).Stop();
    EpollRunnerFactory::GetInstance(EpollRunnerType::TRANSPORT_POOL_EVENT_RUNNER).Stop();

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

void UmqExtLogger(int level, const char *file, const char *function, int line, char *log_msg)
{
    auto new_level = umq_log_level::UMQ_LOG_LEVEL_DEBUG - level;
    if (new_level <= LogLevel::LEVEL_ERR) {
        UBS_LOG_STREAM_EXT_RAW(new_level, file, function, line, log_msg);
    } else {
        static const char *OTHER_LEVEL[] = {"EMERG", "ALERT", "CRIT"};
        UBS_LOG_STREAM_EXT_RAW(LogLevel::LEVEL_ERR, file, function, line,
                               OTHER_LEVEL[level % (sizeof(OTHER_LEVEL))] << ", " << log_msg);
    }
}

UBS_API int ubsocket_set_logger(void (*func)(int level, const char *msg, const char *filename, int line))
{
    Logger::Instance().SetExternalLogFunction(func);
    umq_log_config_t log_cfg = {
        .log_flag = UMQ_LOG_FLAG_LEVEL | UMQ_LOG_FLAG_EXT_FUNC,
        .func = nullptr,
        .ext_func = UmqExtLogger,
        .level = static_cast<umq_log_level_t>(umq_log_level::UMQ_LOG_LEVEL_DEBUG - Logger::Instance().GetLogLevel())};
    return umq_log_config_set(&log_cfg);
}

UBS_API int ubsocket_set_log_level(int level)
{
    Logger::Instance().SetLogLevel(level);
    umq_log_config_t log_cfg = {.log_flag = UMQ_LOG_FLAG_LEVEL,
                                .func = nullptr,
                                .ext_func = nullptr,
                                .level = static_cast<umq_log_level_t>(umq_log_level::UMQ_LOG_LEVEL_DEBUG - level)};
    return umq_log_config_set(&log_cfg);
}

UBS_API void *ubsocket_iobuf_allocate(size_t size, const ubs_iobuf_alloc_option_t *option)
{
    if (g_zcopy_allocator != nullptr) {
        return g_zcopy_allocator->allocate(size, option);
    }

    uint32_t type = GlobalSetting::UBS_ALLOWED_PROTOCOL;
    if (type == UBS_PROTOCOL_UB_RM_RTP || type == UBS_PROTOCOL_UB_RC_RTP) {
        // delete at ubsocket_uninit
        g_zcopy_allocator = new (std::nothrow) umq::UmqZeroCopyAllocator();
    } else {
        UBS_VLOG_WARN("unknown zcopy allocator type");
        return nullptr;
    }

    if (g_zcopy_allocator == nullptr) {
        return nullptr;
    }
    return g_zcopy_allocator->allocate(size, option);
}

UBS_API void ubsocket_iobuf_deallocate(void *addr)
{
    if (addr == nullptr || g_zcopy_allocator == nullptr) {
        return;
    }
    g_zcopy_allocator->deallocate(addr);
}
