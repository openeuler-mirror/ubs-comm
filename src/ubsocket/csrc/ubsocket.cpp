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
#include "common/ubsocket_fd_table.h"
#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_signal_handler.h"
#include "common/ubsocket_version.h"
#include "core/ubsocket_conn_reactor.h"
#include "core/ubsocket_event_epoll.h"
#include "core/ubsocket_tx_cqe_poller.h"
/* ubsocket backend selection: urma only (see CMakeLists.txt) */
#ifdef URMA_DLOPEN_BACKEND_ENABLED
    #include "core/urma/urma_backend.h"
    #define UBS_BACKEND_INIT()            urma::UrmaBackend::Instance().Init()
    #define UBS_BACKEND_UNINIT()          urma::UrmaBackend::Instance().UnInit()
    #define UBS_BACKEND_SET_LOG_LEVEL(l)   urma::UrmaBackend::Instance().SetLogLevel(l)
    #define UBS_BACKEND_GET_ALLOCATOR()   urma::UrmaBackend::Instance().GetAllocator()
    #define UBS_LOAD_FLAGS                (LOAD_LIBC | LOAD_URMA)
#else
    #error "No backend enabled. Enable BUILD_URMA_DLOPEN_BACKEND"
#endif
#include "include/ubsocket.h"
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
    options->async_connector_enable = false;      /* client fully-async connect: off by default */
    options->async_epoll_thread_count = 1;        /* tune on by default */
    options->server_async_enable = 0;             /* server fully-async(reactor) conn: off by default */
    options->lock_ops = nullptr;
    options->rw_lock_ops = nullptr;
    options->sem_ops = nullptr;
    options->rpc_id_ops = nullptr;
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
    // Conn modes unified into the single UBS_CONNECTS_MODE switch (sync/half_async/full_async).
    // Legacy API overrides (async_acceptor/connector/epoll thread counts, server_async/connector_enable) removed accordingly.

    auto result = GlobalSetting::VerifySetting();
    if (result != UBS_OK) {
        UBS_VLOG_ERR("initialize failed as options are invalid");
        errno = EINVAL;
        return UBS_ERROR;
    }

    /* fd 表预扩容：必须排在下面所有会打开 fd、会起线程的步骤之前——进程此刻若还是单线程，扩容是微秒级；
     * 即便已是多线程也只等一次 RCU 宽限期，后面 urma 初始化和建链期间不再扩容。失败只告警，不影响初始化。 */
    (void)PreExpandFdTable(GlobalSetting::UBS_PRE_ALLOC_SYSTEM_FD_NUM);

    /* step2: load under api */
    UBS_VLOG_DEBUG("load under api");
    result = DlApi::Load(UBS_LOAD_FLAGS);
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
    if (g_socket_epoll_lock == nullptr) {
        UBS_VLOG_ERR("create socket epoll rw lock failed\n");
        errno = ENOMEM;
        return UBS_ERROR;
    }
    ArraySet<EventPoll>::GetInstance().Init();

    /* step5: backend init */
    result = UBS_BACKEND_INIT();
    if (result != UBS_OK) {
        UBS_VLOG_ERR("backend init failed, ret: %d\n", result);
        return UBS_ERROR;
    }

    /* step5.5: Server/Client shared conn-establishment poll thread pool start (each thread creates its own
     * event fd registered into ConnEventFdTable). Server accept side and client connect side submit to the same
     * pool with unified load balancing. Heavy work is inlined in poll threads; thread count must satisfy
     * throughput constraints. Only the full_async tier needs this pool. */
    if (GlobalSetting::GetConnMode() == ConnMode::FULL_ASYNC) {
        result = ConnDispatcher::Instance().Start(GlobalSetting::UBS_CONNECTS_POLL_THREAD_NUM);
        if (result != UBS_OK) {
            UBS_VLOG_WARN("conn dispatcher start failed, ret: %d, fallback to sync conn\n", result);
        }
    }

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
#ifdef URMA_DLOPEN_BACKEND_ENABLED
        (void)Statistics::GlobalStatsMgr::GetGlobalStatsMgr(UBS_TRANS_MODE_UB);
#endif
    }

    /* last step: set initialized */
    GlobalSetting::UBS_INITED = true;

    UBS_VLOG_DEBUG("UBS_CONNECTS_MODE:%s, connects poll thread num:%u\n",
                   GlobalSetting::ConnModeToStr(GlobalSetting::UBS_CONNECTS_MODE),
                   GlobalSetting::UBS_CONNECTS_POLL_THREAD_NUM);
    /* do trace log initial */
    if (GlobalSetting::UBS_TRACE_ENABLED) {
        UbsTransMode transMode = UBS_TRANS_MODE_UB;
        Statistics::PrintStatsMgr::StartStatsCollection(GlobalSetting::UBS_TRACE_TIME,
                                                        GlobalSetting::UBS_TRACE_FILE_PATH,
                                                        GlobalSetting::UBS_TRACE_FILE_SIZE, transMode);
    }
#ifdef UBS_SPLIT_TRACE_ENABLED_COMPILE
    if (GlobalSetting::UBS_SPLIT_TRACE_ENABLED) {
        if ((GlobalSetting::UBS_SPLIT_TRACE_LEVEL & SplitTraceLevel::LEVEL_UBSOCKET) != SplitTraceLevel::LEVEL_NONE) {
            TracePrintThread::Instance().Start();
        }

        if ((GlobalSetting::UBS_SPLIT_TRACE_LEVEL & SplitTraceLevel::LEVEL_URMA) != SplitTraceLevel::LEVEL_NONE) {
#ifdef URMA_DLOPEN_BACKEND_ENABLED
            /* liburma 当前无 trace API，no-op；未来 liburma 如新增 trace API 可在此补充 */
#endif
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

        if ((GlobalSetting::UBS_SPLIT_TRACE_LEVEL & SplitTraceLevel::LEVEL_URMA) != SplitTraceLevel::LEVEL_NONE) {
#ifdef URMA_DLOPEN_BACKEND_ENABLED
            /* liburma 无 trace stop API，no-op */
#endif
        }
    }
#endif
    if (GlobalSetting::UBS_TRACE_ENABLED) {
        Statistics::PrintStatsMgr::StopStatsCollection();
    }

    // 用户需要在程序退出时调用 ubsocket_uninit 保证所有的 socket ref 释放，否则会延迟至 ArraySet 单例析构。在 brpc 场
    // 景下，由于 brpc worker 不会主动 join, worker 可能仍会尝试访问 ArraySet<Socket> 或者 ArraySet<EventPoll>
    // server 全异步建链线程池必须先于 socket 资源回收停止：poll 线程与未完成建链的 UrmaConnOps
    // 析构（→ UrmaSocket::UnInitialize）都会访问 urma/mempool 资源，晚于 UrmaBackend::UnInit 停止会触发"释放后仍 poll"。
    // Stop() 幂等，开关关闭时是 no-op。
    ConnDispatcher::Instance().Stop();
    TxCqePoller::Instance().Stop();
    /*
     * Graceful URMA close before teardown: post a CLOSE WR on every still-open urma socket so the
     * peer receives an orderly EOF. The pending-close registry holds the socket (and the imported
     * target jetty that represents the peer's jetty) alive until the CLOSE send CQE arrives, so an
     * in-flight CLOSE WR is never aborted by an early jetty destroy. UrmaBackend::UnInit() then
     * waits for those CQEs before force-releasing any leftover holds.
     */
    ArraySet<Socket>::GetInstance().ForEach([](int /*fd*/, Socket *sock) {
        if (sock != nullptr && sock->Type() == SocketType::SOCK_TYPE_URMA) {
            (void)sock->PostCloseToPeer();
        }
    });
    ArraySet<Socket>::GetInstance().ReleaseAll();
    ArraySet<EventPoll>::GetInstance().ReleaseAll();
    CleanAllSocketEpollMappers();

    UBS_BACKEND_UNINIT();
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
    UBS_BACKEND_SET_LOG_LEVEL(level);
    return UBS_OK;
}

UBS_API void *ubsocket_iobuf_allocate(size_t size, const ubs_iobuf_alloc_option_t *option)
{
    if (g_zcopy_allocator != nullptr) {
        return g_zcopy_allocator->allocate(size, option);
    }

    uint32_t type = GlobalSetting::UBS_ALLOWED_PROTOCOL;
    if (type == UBS_PROTOCOL_UB_RM_RTP || type == UBS_PROTOCOL_UB_RC_RTP) {
        // delete at ubsocket_uninit
        g_zcopy_allocator = UBS_BACKEND_GET_ALLOCATOR();
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
