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
#include "urma_backend.h"

#include "under_api/dl_api.h"
#include "under_api/dl_libc_api.h"
#include "urma_data_rx_ops.h"
#include "urma_data_tx_ops.h"
#include "urma_socket.h"
#include "urma_socket_acceptor_ops.h"
#include "urma_socket_connector_ops.h"
#include "urma_wrapper.h"

namespace ock {
namespace ubs {
namespace urma {

void UrmaLog(int level, char *message)
{
    /*
    URMA_VLOG_LEVEL_EMERG = 0,
    URMA_VLOG_LEVEL_ALERT = 1,
    URMA_VLOG_LEVEL_CRIT = 2,
    URMA_VLOG_LEVEL_ERR = 3,
    URMA_VLOG_LEVEL_WARNING = 4,
    URMA_VLOG_LEVEL_NOTICE = 5,
    URMA_VLOG_LEVEL_INFO = 6,
    URMA_VLOG_LEVEL_DEBUG = 7,
    URMA_VLOG_LEVEL_MAX = 8,
    */
    auto new_level = URMA_VLOG_LEVEL_DEBUG - level;
    if (new_level <= LogLevel::LEVEL_ERR) {
        UBS_LOG_STREAM_RAW(new_level, message);
    } else {
        static const char *OTHER_LEVEL[] = {"EMERG", "ALERT", "CRIT"};
        UBS_LOG_STREAM_RAW(LogLevel::LEVEL_ERR, OTHER_LEVEL[level % (sizeof(OTHER_LEVEL))] << ", " << message);
    }
}

urma_vlog_level_t UrmaGetLogLevel()
{
    return static_cast<urma_vlog_level_t>(URMA_VLOG_LEVEL_DEBUG - Logger::Instance().GetLogLevel());
}

Result UrmaBackend::Init(bool initUrmaOnly) noexcept
{
    std::lock_guard<std::mutex> locker(mutex_);
    if (inited_) {
        UBS_VLOG_ERR("Already initialize, skip initialization");
        return UBS_OK;
    }

    init_urma_only_ = initUrmaOnly;

    /* step0: init setting */
    UrmaSetting::Init();

    Result result = UBS_OK;
    /*
     * Urma core may already be up when Init(true) ran first; skip the load/init steps so a
     * following full Init() only builds the upper layers instead of re-initializing urma.
     */
    if (!urma_inited_) {
        /* step1: load urma api */
        result = LoadUrmaApi();
        if (UNLIKELY(result != UBS_OK)) {
            UnInitInner();
            UBS_VLOG_ERR("Failed to initialize backend as load urma api failed");
            return UBS_ERROR;
        }

        /* step2: init urma core */
        result = InitUrma();
        if (UNLIKELY(result != UBS_OK)) {
            UnInitInner();
            UBS_VLOG_ERR("Failed to initialize backend as init urma failed");
            return UBS_ERROR;
        }
    }

    if (!initUrmaOnly) {
        /* step3: init context pool */
        result = InitContextPool();
        if (UNLIKELY(result != UBS_OK)) {
            UnInitInner();
            UBS_VLOG_ERR("Failed to initialize backend as init context pool failed");
            return UBS_ERROR;
        }

        /* step4: init poller pool for cqe */
        result = InitPollerSet();
        if (UNLIKELY(result != UBS_OK)) {
            UnInitInner();
            UBS_VLOG_ERR("Failed to initialize backend as init poller set failed");
            return UBS_ERROR;
        }

        /* step5: init jetty pool with shared mode or dedicated mode */
        result = InitJettyPool();
        if (UNLIKELY(result != UBS_OK)) {
            UnInitInner();
            UBS_VLOG_ERR("Failed to initialize backend as init jetty pool failed");
            return UBS_ERROR;
        }

        /* step6: init io buffer pool and allocator */
        result = InitIoBufPool();
        if (UNLIKELY(result != UBS_OK)) {
            UnInitInner();
            UBS_VLOG_ERR("Failed to initialize backend as init jetty pool failed");
            return UBS_ERROR;
        }

        /* step7: init object pool */
        result = InitObjectPool();
        if (UNLIKELY(result != UBS_OK)) {
            UnInitInner();
            UBS_VLOG_ERR("Failed to initialize backend as init object pool failed");
            return UBS_ERROR;
        }

        /* step8: init keep-alive manager */
        result = InitKeepAliveManager();
        if (UNLIKELY(result != UBS_OK)) {
            UnInitInner();
            UBS_VLOG_ERR("Failed to initialize backend as init keep_alive manager failed");
            return UBS_ERROR;
        }
        /* only a full init (context/poller/jetty/object pools) marks the backend usable */
        inited_ = true;
    }

    return UBS_OK;
}

void UrmaBackend::UnInit() noexcept
{
    std::lock_guard<std::mutex> locker(mutex_);
    if (!inited_) {
        UBS_VLOG_DEBUG("Not initialized");
        return;
    }


    ClearPendingClose();

    UnInitInner();

    inited_ = false;
    UBS_VLOG_DEBUG("Un-initialized");
}

void UrmaBackend::UnInitInner() noexcept
{
    /* do reverse step with init; upper layers exist only after a full (non urma-only) init */
    if (!init_urma_only_) {
        UnInitKeepAliveManager();
        /*
         * Stop the CQE pollers first: while they run they may still AcquireSlot/Free from the
         * object pools below, so the pools must not be torn down under a live poller thread.
         */
        UnInitPollerSet();

        UnInitObjectPool();

        UnInitIoBufPool();
        g_zcopy_allocator = nullptr;

        UnInitJettyPool();

        UnInitContextPool();
    }

    UrmaContext::ClearAllContexts();

    UnInitUrma();

    UnLoadUrmaApi();
}

Result UrmaBackend::LoadUrmaApi() noexcept
{
    auto result = DlApi::Load(LOAD_URMA);
    if (result != UBS_OK) {
        UBS_VLOG_ERR("Load urma api failed, result: %d", result);
        return result;
    }

    UBS_VLOG_DEBUG("urma api loaded");

    return UBS_OK;
}

void UrmaBackend::UnLoadUrmaApi() noexcept
{
    DlApi::UnLoad(LOAD_URMA);

    UBS_VLOG_DEBUG("urma api unloaded");
}

Result UrmaBackend::InitKeepAliveManager()
{
    auto tmp_alive_manager = MakeRef<UrmaAliveManager>();
    if (tmp_alive_manager == nullptr) {
        UBS_VLOG_ERR("Create alive_manager object failed, probably out of memory");
        return UBS_ERROR;
    }

    UrmaAliveConf conf;
    conf.keepalive_time = UrmaSetting::UB_KEEPALIVE_TIME;
    conf.keepalive_intvl = UrmaSetting::UB_KEEPALIVE_INTVL;
    conf.keepalive_probes = UrmaSetting::UB_KEEPALIVE_PROBES;
    Result result = tmp_alive_manager->Init(conf);
    if (result != UBS_OK) {
        UBS_VLOG_ERR("Failed to initialize urma context pool, result: %d", result);
        return result;
    }

    alive_manager_ = tmp_alive_manager;

    return UBS_OK;
}

Result UrmaBackend::InitUrma() noexcept
{
    UBS_VLOG_DEBUG("enter");

    /* init urma */
    urma_init_attr_t init_attr;
    bzero(&init_attr, sizeof(init_attr));
    auto result = UrmaApi::urma_init(&init_attr);
    if (result != 0) {
        UBS_VLOG_ERR("Init urma failed, result: %d", result);
        return result;
    }

    /* register log function */
    result = UrmaApi::urma_register_log_func(UrmaLog);
    if (result != URMA_SUCCESS) {
        UBS_VLOG_WARN("Set urma log function failed, result %d", result);
    }

    /* set log level */
    UrmaApi::urma_log_set_level(UrmaGetLogLevel());

    urma_inited_ = true;

    UBS_VLOG_DEBUG("Init urma succesfully, urma log level %d", UrmaGetLogLevel());
    return UBS_OK;
}

void UrmaBackend::UnInitUrma() noexcept
{
    if (!urma_inited_) {
        return;
    }

    UrmaApi::urma_uninit();
    urma_inited_ = false;
}

void UrmaBackend::UnInitKeepAliveManager()
{
    if (alive_manager_ != nullptr) {
        alive_manager_->UnInit();
        alive_manager_ = nullptr;
    }
}

Result UrmaBackend::InitContextPool() noexcept
{
    auto tmp_context_pool = MakeRef<UrmaContextPool>();
    if (tmp_context_pool == nullptr) {
        UBS_VLOG_ERR("Create context pool object failed, probably out of memory");
        return UBS_ERROR;
    }

    /* add dev and eid index
     * case1: if dev name and eid index if set in ENV
     * case2: if not set in ENV, get first available dev
     */
    Result result = UBS_OK;
    if (!UrmaSetting::UB_DEV_NAME.empty()) {
        result = tmp_context_pool->AddEidIndex(UrmaSetting::UB_DEV_NAME, UrmaSetting::UB_EID_INDEX);
    } else if (UrmaSetting::UB_USING_BOUNDING_DEV) {
        result = tmp_context_pool->AutoAddEidIndex();
    } else {
        result = tmp_context_pool->AutoAddEidIndex(false);
    }

    if (result != UBS_OK) {
        UBS_VLOG_ERR("Add urma device failed, result: %d", result);
        return result;
    }

    /* create context in context pool */
    result = tmp_context_pool->Init();
    if (result != UBS_OK) {
        UBS_VLOG_ERR("Failed to initialize urma context pool, result: %d", result);
        return result;
    }

    /* set object to member variables */
    context_pool_ = tmp_context_pool;

    return UBS_OK;
}

void UrmaBackend::UnInitContextPool() noexcept
{
    /* un-init context pool */
    if (context_pool_ != nullptr) {
        context_pool_->UnInit();
        context_pool_ = nullptr;
    }
}

Result UrmaBackend::InitJettyPool() noexcept
{
    if (context_pool_ == nullptr) {
        UBS_VLOG_ERR("No context_pool_ set, probably not init context pool");
        return UBS_ERROR;
    }

    UrmaJettyPoolOptions options;
    options.pool_type = UrmaSetting::UB_JETTY_SHARED_ENABLED ? PT_SHARED : PT_DEDICATED;
    options.init_jetty_count = UrmaSetting::UB_JETTY_INIT_COUNT;
    options.max_jetty_count = UrmaSetting::UB_JETTY_MAX_COUNT;
    options.jfr_count = UrmaSetting::UB_JETTY_JFR_COUNT;
    options.jfs_depth = UrmaSetting::UB_JETTY_JFS_DEPTH;
    options.jfr_depth = UrmaSetting::UB_JETTY_JFR_DEPTH;
    options.transport_mode = UrmaSetting::UB_JETTY_TRANS_MODE;
    options.tp_type = UrmaSetting::UB_JETTY_TP_TYPE;
    options.context_pool = context_pool_;

    auto tmp_jetty_pool = MakeRef<UrmaJettyPool>(options);
    if (tmp_jetty_pool == nullptr) {
        UBS_VLOG_ERR("Failed to create Urma jetty pool, probably out of memory");
        return UBS_ERROR;
    }

    auto result = tmp_jetty_pool->Init();
    if (result != UBS_OK) {
        UBS_VLOG_ERR("Failed to init Urma jetty pool, result %d", result);
        return UBS_ERROR;
    }

    jetty_pool_ = tmp_jetty_pool;

    return UBS_OK;
}

void UrmaBackend::UnInitJettyPool() noexcept
{
    if (jetty_pool_ != nullptr) {
        jetty_pool_->UnInit();
        jetty_pool_ = nullptr;
    }
}

Result UrmaBackend::InitPollerSet() noexcept
{
    if (context_pool_ == nullptr) {
        UBS_VLOG_ERR("No context_pool_ probably not init poller pool");
        return UBS_ERROR;
    }

    auto tmp_poller_set = MakeRef<UrmaAggPollerSet>();
    if (tmp_poller_set == nullptr) {
        UBS_VLOG_ERR("Failed to create Urma poller set, probably out of memory");
        return UBS_ERROR;
    }

    UrmaAggPollerOptions options;
    options.context_pool = context_pool_;
    options.run_in_thread = UrmaSetting::UB_EPOLL_HANDLE_MODE == THREAD_PER_CONNECTION;
    options.polling_type = UrmaSetting::UB_POLLER_BUSY_POLLING ? BUSY_POLLING : EVENT_POLLING;;
    options.poller_count_per_context = UrmaSetting::UB_POLLER_COUNT;
    options.jfc_depth = UrmaSetting::UB_POLLER_JFC_DEPTH;
    options.cqe_handler = std::bind(&UrmaBackend::HandleCqe, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

    auto result = tmp_poller_set->Init(options);
    if (result != UBS_OK) {
        UBS_VLOG_ERR("Failed to init Urma poller set, result %d", result);
        return UBS_ERROR;
    }
    poller_set_ = tmp_poller_set;
    return UBS_OK;
}

void UrmaBackend::UnInitPollerSet() noexcept
{
    if (poller_set_ == nullptr) {
        return;
    }
    poller_set_->UnInit();
}

Result UrmaBackend::InitIoBufPool() noexcept
{
    if (context_pool_ == nullptr) {
        UBS_VLOG_ERR("No context_pool_ probably not init poller pool");
        return UBS_ERROR;
    }

    auto tmp_io_buf_pool = MakeRef<UrmaIoBufferPool>();
    if (tmp_io_buf_pool == nullptr) {
        UBS_VLOG_ERR("Failed to create Urma io buff pool, probably out of memory");
        return UBS_ERROR;
    }

    allocator::FixedAllocatorOptions options;
    options.pool_space_size_gb = UrmaSetting::UB_BUF_POOL_SPACE_SIZE;
    options.block_size_mb = UrmaSetting::UB_BUF_BLOCK_SIZE_MB;
    options.init_block_count = UrmaSetting::UB_BUF_INIT_MEM_SIZE_MB / options.block_size_mb;
    options.mini_block_size_kb = UrmaSetting::UB_BUF_PAGE_SIZE_KB;
    options.async_expand = UrmaSetting::UB_BUF_ASYNC_EXPAND;

    auto result = tmp_io_buf_pool->init(options, context_pool_);
    if (result != UBS_OK) {
        UBS_VLOG_ERR("Failed to init io buff pool, result %d", result);
        return UBS_ERROR;
    }

    io_buff_pool_ = tmp_io_buf_pool;
    g_zcopy_allocator = io_buff_pool_.Get();

    return UBS_OK;
}

void UrmaBackend::UnInitIoBufPool() noexcept
{
    if (io_buff_pool_ != nullptr) {
        io_buff_pool_->un_init();
        io_buff_pool_ = nullptr;
    }
}

void UrmaBackend::SetLogLevel(int level) noexcept
{
    std::lock_guard<std::mutex> locker(mutex_);
    if (!inited_) {
        return;
    }

    UrmaApi::urma_log_set_level(UrmaGetLogLevel());
}

void UrmaBackend::HoldPendingClose(int fd, Socket *sock) noexcept
{
    std::lock_guard<std::mutex> locker(pending_close_mutex_);
    /* SocketPtr(raw) constructor takes a reference; re-hold on same fd replaces stale entry */
    pending_close_holds_[fd] = PendingCloseEntry{SocketPtr(sock), false};
}

void UrmaBackend::ReleasePendingClose(int fd) noexcept
{
    std::lock_guard<std::mutex> locker(pending_close_mutex_);
    auto it = pending_close_holds_.find(fd);
    if (it != pending_close_holds_.end()) {
        if (it->second.placeholder_taken) {
            /* return the placeholder-held fd number to the kernel pool */
            (void)LibcApi::close(fd);
        }
        /* erase drops the held reference; refcount reaching 0 triggers ~UrmaSocket safely */
        pending_close_holds_.erase(it);
        UBS_VLOG_TRACE("pending-close hold released for fd(%d)", fd);
    }
}

bool UrmaBackend::HasPendingClose(int fd) noexcept
{
    std::lock_guard<std::mutex> locker(pending_close_mutex_);
    return pending_close_holds_.find(fd) != pending_close_holds_.end();
}

void UrmaBackend::ClearPendingClose() noexcept
{
    std::lock_guard<std::mutex> locker(pending_close_mutex_);
    for (auto &kv : pending_close_holds_) {
        if (kv.second.placeholder_taken) {
            (void)LibcApi::close(kv.first);
        }
    }
    pending_close_holds_.clear();
}

bool UrmaBackend::TakeFdPlaceholder(int fd) noexcept
{
    std::lock_guard<std::mutex> locker(pending_close_mutex_);
    auto it = pending_close_holds_.find(fd);
    if (it == pending_close_holds_.end()) {
        /* send CQE already arrived and released the hold (race window): the WR is
         * done, the caller closing the fd itself */
        return false;
    }
    if (it->second.placeholder_taken) {
        return true;
    }
    /* shared /dev/null placeholder source, opened lazily once per process */
    static const int devnull_fd = LibcApi::open("/dev/null", O_RDWR | O_CLOEXEC);
    if (devnull_fd < 0) {
        const int openErrno = errno;
        UBS_VLOG_WARN("open /dev/null for fd(%d) placeholder failed (errno=%d), degrade to no placeholder",
                      fd, openErrno);
        return false;
    }
    /* dup3 atomically replaces the TCP socket with /dev/null under the fd number:
     * the original socket is closed by the kernel (FIN sent), while the number
     * stays occupied until the hold release path closes it.
     * NOTE: dup3 returns newfd (== fd) on success and -1 on error — never 0 */
    const int dupRet = LibcApi::dup3(devnull_fd, fd, O_CLOEXEC);
    if (dupRet != fd) {
        const int dupErrno = errno; /* save errno before logging */
        UBS_VLOG_WARN("dup3 /dev/null placeholder onto fd(%d) failed (devnull_fd=%d, ret=%d, errno=%d), "
                      "degrade to no placeholder",
                      fd, devnull_fd, dupRet, dupErrno);
        return false;
    }
    it->second.placeholder_taken = true;
    return true;
}

void UrmaBackend::RecordBrokenConn(uint64_t conn_addr_id, uint64_t conn_id) noexcept
{
    if (conn_addr_id == 0 || conn_id == 0) {
        return;
    }
    std::lock_guard<std::mutex> locker(broken_conn_mutex_);
    broken_conns_[conn_addr_id] = conn_id;
}

uint64_t UrmaBackend::TakeBrokenConnId(uint64_t conn_addr_id) noexcept
{
    std::lock_guard<std::mutex> locker(broken_conn_mutex_);
    auto iter = broken_conns_.find(conn_addr_id);
    if (iter == broken_conns_.end()) {
        return 0;
    }
    return iter->second;
}

void UrmaBackend::EraseBrokenConn(uint64_t conn_addr_id) noexcept
{
    std::lock_guard<std::mutex> locker(broken_conn_mutex_);
    broken_conns_.erase(conn_addr_id);
}

/* global monotonic connection id generator, shared by sync/async accept paths, 0 is reserved as invalid */
static std::atomic<uint64_t> g_conn_id{0};

uint64_t UrmaBackend::AllocConnId() noexcept
{
    return g_conn_id.fetch_add(1, std::memory_order_relaxed) + 1;
}

void UrmaBackend::HandleErrorCqe(urma_cr_t *cr)
{
    int sock_fd = -1;
    if (cr->user_ctx != 0) {
        sock_fd = static_cast<int>(SenderCRContext{cr->user_ctx}.fd);
    }
    if (sock_fd < 0) {
        UBS_VLOG_TRACE("error cr has no valid user_ctx, skip notify: status=%u", cr->status);
        return;
    }

    auto sock = ArraySet<Socket>::GetInstance().GetItem(sock_fd);
    if (UNLIKELY(sock == nullptr || sock->Type() != SocketType::SOCK_TYPE_URMA)) {
        /* pending-close hold */
        if (sock == nullptr) {
            ReleasePendingClose(sock_fd);
        }
        UBS_VLOG_ERR("handle error cr failed for fd invalid: status=%u, user_ctx=0x%lx, op_code=%u, flag=0x%hhx, fd: %d",
                cr->status, cr->user_ctx, cr->opcode, cr->flag.value, sock_fd);
        return;
    }

    auto urma_sock = RefStaticCastToRaw<UrmaSocket>(sock);
    SenderCRContext cr_context{cr->user_ctx}; /* 错误 CQE 的 sender 上下文（type/fd/seq_no） */
    if (cr_context.type == KEEP_ALIVE_PROBE) {
        // no need switch route for keep_alive probe
        UBS_VLOG_WARN("handle error cr for keepalive probe: status=%u, user_ctx=0x%lx, op_code=%u, flag=0x%hhx", cr->status,
                      cr->user_ctx, cr->opcode, cr->flag.value);
        return;
    }
    switch (cr->status) {
        case URMA_CR_SUCCESS:
            break;
        // case need switch route
        case URMA_CR_LOC_LEN_ERR:
        case URMA_CR_LOC_ACCESS_ERR:
        case URMA_CR_ACK_TIMEOUT_ERR:
        case URMA_CR_RNR_RETRY_CNT_EXC_ERR:
        case URMA_CR_LOC_DATA_POISON:
        {
            if (!urma_sock->LinkSwitchEnable()) {
                UBS_VLOG_ERR("handle error cr: status=%u, user_ctx=0x%lx, op_code=%u, flag=0x%hhx, fd: %d", cr->status,
                             cr->user_ctx, cr->opcode, cr->flag.value, sock_fd);
                break;
            }
            if (urma_sock->State() == SocketState::SOCK_STAT_BROKEN) {
                UBS_VLOG_TRACE("handle error cr success: status=%u, user_ctx=0x%lx, op_code=%u, flag=0x%hhx, fd=%d, sender_type=%d", cr->status,
                               cr->user_ctx, cr->opcode, cr->flag.value, sock_fd, static_cast<int>(cr_context.type));
                break;
            }
            urma_sock->GetContext()->RecordPortError(cr->status);
            if (urma_sock->create_type_ == SOCK_CREATE_TYPE_CONNECT) {
                UrmaBackend::Instance().RecordBrokenConn(urma_sock->GetPeerAddrId(), urma_sock->GetConnId());
            }
            io_buff_pool_->rm_remote_segments(urma_sock->GetContext(), urma_sock->GetPeerNode());
            urma_sock->State(SocketState::SOCK_STAT_BROKEN);
            LibcApi::shutdown(urma_sock->Fd(), SHUT_RD);
            urma_sock->NotifySocketReadable();
            UBS_VLOG_WARN("handle error cr for channel broken: status=%u, user_ctx=0x%lx, op_code=%u, flag=0x%hhx, fd=%d, sender_type=%d, conn_id=%lu", cr->status,
                           cr->user_ctx, cr->opcode, cr->flag.value, sock_fd, static_cast<int>(cr_context.type),
                           (unsigned long)urma_sock->GetConnId());
            break;
        }
        /* connection-level error: reset the socket, inject HUP|RDHUP and release waiters */
        case URMA_CR_UNSUPPORTED_OPCODE_ERR:
        case URMA_CR_LOC_OPERATION_ERR:
        case URMA_CR_REM_RESP_LEN_ERR:
        case URMA_CR_REM_UNSUPPORTED_REQ_ERR:
        case URMA_CR_REM_OPERATION_ERR:
        case URMA_CR_REM_ACCESS_ABORT_ERR:
        case URMA_CR_WR_FLUSH_ERR:
        case URMA_CR_WR_SUSPEND_DONE:
        case URMA_CR_WR_FLUSH_ERR_DONE:
        case URMA_CR_WR_UNHANDLED:
        case URMA_CR_REM_DATA_POISON:
        default:
            UBS_VLOG_ERR("handle error cr: status=%u, user_ctx=0x%lx, op_code=%u, flag=0x%hhx", cr->status,
                         cr->user_ctx, cr->opcode, cr->flag.value);
            urma_sock->OnPeerReset();
            break;
    }
}

Result UrmaBackend::HandleCqe(uint32_t flags, int cr_count, urma_cr_t *cr)
{
    (void)flags;
    for (auto i = 0; i < cr_count; i++) {
        if (cr[i].status != URMA_CR_SUCCESS) {
            HandleErrorCqe(&cr[i]);
            continue;
        }

        if (cr[i].flag.bs.s_r == 0) { // sender side
            if (cr[i].user_ctx != 0) {
                SenderCRContext cr_context{cr[i].user_ctx};
                if (cr_context.type == CLOSE_NOTIFY) {
                    /* pending-close hold: the CLOSE WR has been flushed to the peer —
                     * the held socket (fd already left ArraySet) can now be released */
                    ReleasePendingClose(cr_context.fd);
                    continue;
                }
                if (cr_context.type == SHUTDOWN_NOTIFY || cr_context.type == KEEP_ALIVE_PROBE) {
                    // release the ctrl slot
                    auto sock = ArraySet<Socket>::GetInstance().GetItem(cr_context.fd);
                    if (UNLIKELY(sock == nullptr || sock->Type() != SocketType::SOCK_TYPE_URMA)) {
                        UBS_VLOG_ERR("socket is invalid for fd: %d", cr_context.fd);
                        continue;
                    }
                    RefStaticCastToRaw<UrmaSocket>(sock)->OnPostCtrlOnlyNotificationFinished(cr_context.seq_no);
                    continue;
                }
                if (cr_context.type == CTRL_DATA_REQUEST && alive_manager_->KeepAliveEnable()) {
                    // send and update active time
                    auto sock = ArraySet<Socket>::GetInstance().GetItem(cr_context.fd);
                    if (UNLIKELY(sock == nullptr || sock->Type() != SocketType::SOCK_TYPE_URMA)) {
                        UBS_VLOG_ERR("socket is invalid for fd: %d", cr_context.fd);
                        continue;
                    }
                    RefStaticCastToRaw<UrmaSocket>(sock)->GetAliveInfo()->UpdateActiveTime();
                    continue;
                }
                if (cr_context.type != READ_DATA_REQUEST) {
                    continue;
                }
                auto sock_fd = cr_context.fd;
                auto seq_no = cr_context.seq_no;
                UBS_VLOG_TRACE("send side cqe user ctx(0x%lx) socket(%d) seq_no(%u)", cr[i].user_ctx, sock_fd, seq_no);
                auto sock = ArraySet<Socket>::GetInstance().GetItem(sock_fd);
                if (UNLIKELY(sock == nullptr || sock->Type() != SocketType::SOCK_TYPE_URMA)) {
                    UBS_VLOG_ERR("socket is invalid for fd: %d", sock_fd);
                    continue;
                }
                auto urma_sock = RefStaticCastToRaw<UrmaSocket>(sock);
                urma_sock->DataReadFinished(seq_no);
                continue;
            }
            continue;
        }

        // receiver side
        if (cr[i].opcode == URMA_CR_OPC_WRITE_WITH_IMM) {
            if (cr[i].user_ctx == 0) { // TODO: why write imm receiver user ctx = 0
                continue;
            }
            SenderCRContext cr_context{cr[i].user_ctx};
            auto fd = static_cast<int>(cr_context.fd);
            auto sock = ArraySet<Socket>::GetInstance().GetItem(fd);
            if (UNLIKELY(sock == nullptr || sock->Type() != SocketType::SOCK_TYPE_URMA)) {
                UBS_VLOG_ERR("socket is invalid for fd: %d", fd);
                continue;
            }

            auto urma_sock = RefStaticCastToRaw<UrmaSocket>(sock);
            urma_sock->HandleRecvCqe(cr[i].imm_data);
            if (alive_manager_->KeepAliveEnable()) {
                // receive and update active time
                urma_sock->GetAliveInfo()->UpdateActiveTime();
            }
            urma_sock->PostEmptyRecvWorkerRecords();
        } else {
            // TODO: other opcode
        }
    }
    return UBS_OK;
}

Result UrmaBackend::CreateSocket(int fd, SocketType t, SocketPtr &outSocket) noexcept
{
    return UBS_OK;
}

Result UrmaBackend::CreateTxOps(SocketType value, const SocketPtr &sock, DataTxOps *&ops) noexcept
{
    auto tx_ops = new (std::nothrow) UrmaTxOps(sock->raw_socket_);
    if (UNLIKELY(tx_ops == nullptr)) {
        UBS_VLOG_ERR("Failed to create urma tx ops, probably out of memory");
        return UBS_MALLOC_FAILED;
    }

    ops = tx_ops;
    return UBS_OK;
}

Result UrmaBackend::CreateRxOps(SocketType value, const SocketPtr &sock, DataRxOps *&ops) noexcept
{
    auto rx_ops = new (std::nothrow) UrmaRxOps(sock->raw_socket_);
    if (UNLIKELY(rx_ops == nullptr)) {
        UBS_VLOG_ERR("Failed to create urma rx ops, probably out of memory");
        return UBS_MALLOC_FAILED;
    }

    ops = rx_ops;
    return UBS_OK;
}

Result UrmaBackend::CreateAcceptorOps(SocketType value, const SocketPtr &sock, AcceptorOps *&ops) noexcept
{
    (void)value;
    auto tmp_ops = new (std::nothrow) UrmaAcceptorOps(sock->raw_socket_);
    if (UNLIKELY(tmp_ops == nullptr)) {
        UBS_VLOG_ERR("Failed to create urma acceptor ops, probably out of memory");
        return UBS_MALLOC_FAILED;
    }

    ops = tmp_ops;
    return UBS_OK;
}

Result UrmaBackend::CreateConnectorOps(SocketType value, const SocketPtr &sock, ConnectorOps *&ops) noexcept
{
    auto tmp_ops = new (std::nothrow) UrmaConnectorOps(sock->raw_socket_);
    if (UNLIKELY(tmp_ops == nullptr)) {
        UBS_VLOG_ERR("Failed to create urma connector ops, probably out of memory");
        return UBS_MALLOC_FAILED;
    }

    ops = tmp_ops;
    return UBS_OK;
}

UrmaIoBufferPool *UrmaBackend::GetAllocator() noexcept
{
    if (UNLIKELY(io_buff_pool_ == nullptr)) {
        io_buff_pool_ = MakeRef<UrmaIoBufferPool>();
    }
    return io_buff_pool_.Get();
}

Result UrmaBackend::InitObjectPool() noexcept
{
    auto result = UrmaDataMetaPool::Instance().Init();
    if (UNLIKELY(result != UBS_OK)) {
        return result;
    }
    return UrmaDataSlotPool::Instance().Init();
}

void UrmaBackend::UnInitObjectPool() noexcept
{
    UrmaDataSlotPool::Instance().UnInit();
    UrmaDataMetaPool::Instance().UnInit();
}

UrmaContextPoolPtr UrmaBackend::GetContextPoll() noexcept
{
    return context_pool_;
}

UrmaAggPollerSetPtr UrmaBackend::GetPollerSet() noexcept
{
    return poller_set_;
}

UrmaAliveManagerPtr UrmaBackend::GetAliveManager() noexcept
{
    return alive_manager_;
}
} // namespace urma
} // namespace ubs
} // namespace ock
