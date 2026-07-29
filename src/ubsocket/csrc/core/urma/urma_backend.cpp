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
#include "urma_data_rx_ops.h"
#include "urma_data_tx_ops.h"
#include "urma_poller.h"
#include "urma_socket.h"
#include "urma_socket_acceptor_ops.h"
#include "urma_socket_connector_ops.h"
#include "urma_wrapper.h"

namespace ock {
namespace ubs {
namespace urma {
std::mutex UrmaBackend::urma_mutex_;
bool UrmaBackend::urma_inited_ = false;
UrmaContextPtr UrmaBackend::urma_context_{nullptr};
IdentifyNode UrmaBackend::local_identify_node_;
UrmaIoBufferPool UrmaBackend::urma_allocator_;

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

Result UrmaBackend::Init() noexcept
{
    UBS_SLOG_INFO("enter");

    std::lock_guard<std::mutex> guard(urma_mutex_);
    if (urma_inited_) {
        UBS_SLOG_DEBUG("urma already initialized");
        return UBS_OK;
    }

    Result ret = LoadUrmaApi();
    if (ret != UBS_OK) {
        return ret;
    }

    ret = InitUrmaCore();
    if (ret != UBS_OK) {
        return ret;
    }

    UrmaContextPtr ctx;
    ret = UrmaContext::CreateContext(UrmaSetting::UB_DEV_NAME, UrmaSetting::UB_EID_INDEX, ctx);
    if (ret != UBS_OK) {
        UBS_SLOG_ERR("Create urma context failed, dev: " << UrmaSetting::UB_DEV_NAME << ", eid: "
                                                         << UrmaSetting::UB_EID_INDEX << ", result: " << ret);
        return ret;
    }

    ret = InitPoller(ctx);
    if (ret != UBS_OK) {
        return ret;
    }

    ret = InitZeroCopyAllocator(ctx);
    if (ret != UBS_OK) {
        return ret;
    }

    ret = InitLocalIdentifyNodeInfo(ctx);
    if (ret != UBS_OK) {
        return ret;
    }

    urma_context_ = ctx;
    urma_inited_ = true;

    UBS_SLOG_INFO("leave, inited = " << urma_inited_);
    return UBS_OK;
}

Result UrmaBackend::CreateSocket(int fd, SocketType t, SocketPtr &outSocket)
{
    return UBS_OK;
}

Result UrmaBackend::CreateTxOps(SocketType value, const SocketPtr &sock, DataTxOps *&ops)
{
    return UBS_OK;
}

Result UrmaBackend::CreateRxOps(SocketType value, const SocketPtr &sock, DataRxOps *&ops)
{
    return UBS_OK;
}

Result UrmaBackend::CreateAcceptorOps(SocketType value, const SocketPtr &sock, AcceptorOps *&acceptor)
{
    return UBS_OK;
}

Result UrmaBackend::CreateConnectorOps(SocketType value, const SocketPtr &sock, ConnectorOps *&connector)
{
    return UBS_OK;
}

Result UrmaBackend::LoadUrmaApi()
{
    auto result = DlApi::Load(LOAD_URMA);
    if (result != UBS_OK) {
        UBS_SLOG_ERR("Load urma api failed, result: " << result);
        return result;
    }

    UrmaSetting::Init();
    return UBS_OK;
}

Result UrmaBackend::InitUrmaCore()
{
    urma_init_attr_t init_attr;
    bzero(&init_attr, sizeof(init_attr));
    auto result = UrmaApi::urma_init(&init_attr);
    if (result != 0) {
        UBS_SLOG_ERR("Init urma failed, result: " << result);
        return result;
    }

    result = UrmaApi::urma_register_log_func(UrmaLog);
    if (result != URMA_SUCCESS) {
        UBS_SLOG_WARN("Set urma log function failed, result " << result);
    }

    UrmaApi::urma_log_set_level(UrmaGetLogLevel());
    return UBS_OK;
}

Result UrmaBackend::InitPoller(const UrmaContextPtr &ctx)
{
    UrmaPollerOptions poller_opts;
    poller_opts.context_ = ctx;
    poller_opts.cqe_handler = HandleCqe;
    poller_opts.jfc_depth = 1024;
    poller_opts.poller_count = 2;

    auto result = UrmaPollerSet::Instance().Init(poller_opts);
    if (result != UBS_OK) {
        UBS_SLOG_ERR("Init urma poller set failed, result: " << result);
        return result;
    }
    return UBS_OK;
}

Result UrmaBackend::InitZeroCopyAllocator(const UrmaContextPtr &ctx)
{
    auto res = urma_allocator_.init(ctx);
    if (res != UBS_OK) {
        UBS_SLOG_ERR("urma allocator initialize failed: " << res);
        return res;
    }

    g_zcopy_allocator = &urma_allocator_;

    return UBS_OK;
}

Result UrmaBackend::InitLocalIdentifyNodeInfo(const UrmaContextPtr &ctx)
{
    auto len = readlink("/proc/self/ns/net", local_identify_node_.ns, NAME_MAX);
    if (len < 0) {
        UBS_SLOG_ERR("readlink /proc/self/ns/net failed: " << errno << " : " << strerror(errno));
        return UBS_ERROR;
    }

    local_identify_node_.eid = ctx->EidInfo().eid;
    local_identify_node_.pid = getpid();
    local_identify_node_.ns[len] = '\0';
    return UBS_OK;
}

Result UrmaBackend::HandleCqe(uint32_t flags, int cr_count, urma_cr_t *cr)
{
    (void)flags;
    for (auto i = 0; i < cr_count; i++) {
        if (cr[i].status != URMA_CR_SUCCESS) {
            // TODO: add process for error status
            UBS_VLOG_ERR("handle error cr: status=%u, user_ctx=0x%lx, op_code=%u, flag=0x%hhx", cr[i].status,
                         cr[i].user_ctx, cr[i].opcode, cr[i].flag.value);
            continue;
        }

        if (cr[i].flag.bs.s_r == 0) { // sender side
            continue;
        }

        // receiver side
        if (cr[i].opcode == URMA_CR_OPC_WRITE_WITH_IMM) {
            auto fd = static_cast<int>(cr[i].user_ctx);
            auto sock = ArraySet<Socket>::GetInstance().GetItem(fd);
            if (UNLIKELY(sock == nullptr)) {
                UBS_VLOG_ERR("socket is nullptr for fd: %d", fd);
                continue;
            }

            auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
            urma_sock->HandleRecvCqe(cr[i].imm_data);
        } else {
            // TODO: other opcode
        }
    }
    return UBS_OK;
}

void UrmaBackend::UnInit() noexcept
{
    UBS_SLOG_DEBUG("enter");

    std::lock_guard<std::mutex> guard(urma_mutex_);
    if (!urma_inited_) {
        UBS_SLOG_DEBUG("urma not initialized");
        return;
    }

    urma_allocator_.un_init();
    UrmaPollerSet::Instance().UnInit();
    UrmaApi::urma_uninit();
    urma_inited_ = false;
    urma_context_ = nullptr;

    UBS_SLOG_DEBUG("leave, inited = " << urma_inited_);
}

Result UrmaBackendImpl::Init() noexcept
{
    std::lock_guard<std::mutex> locker(mutex_);
    if (inited_) {
        UBS_VLOG_ERR("Already initialize, skip initialization");
        return UBS_OK;
    }

    /* step0: init setting */
    UrmaSetting::Init();

    /* step1: init context pool */
    auto result = LoadUrmaApi();
    if (UNLIKELY(result != UBS_OK)) {
        UnInitInner();
        UBS_VLOG_ERR("Failed to initialize backend as load urma api failed");
        return UBS_ERROR;
    }

    /* step2: init context pool */
    result = InitUrma();
    if (UNLIKELY(result != UBS_OK)) {
        UnInitInner();
        UBS_VLOG_ERR("Failed to initialize backend as init urma failed");
        return UBS_ERROR;
    }

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

    return UBS_OK;
}

void UrmaBackendImpl::UnInit() noexcept
{
    std::lock_guard<std::mutex> locker(mutex_);
    if (!inited_) {
        UBS_VLOG_DEBUG("Not initialized");
        return;
    }

    UnInitInner();

    inited_ = false;
    UBS_VLOG_DEBUG("Un-initialized");
}

void UrmaBackendImpl::UnInitInner() noexcept
{
    /* do reverse step with init */
    UnInitIoBufPool();

    UnInitJettyPool();

    UnInitPollerSet();

    UnInitContextPool();
    best_context_ = nullptr;

    UnInitUrma();

    UnLoadUrmaApi();
}

Result UrmaBackendImpl::LoadUrmaApi() noexcept
{
    auto result = DlApi::Load(LOAD_URMA);
    if (result != UBS_OK) {
        UBS_VLOG_ERR("Load urma api failed, result: %d", result);
        return result;
    }

    UBS_VLOG_DEBUG("urma api loaded");

    return UBS_OK;
}

void UrmaBackendImpl::UnLoadUrmaApi() noexcept
{
    DlApi::UnLoad(LOAD_URMA);

    UBS_VLOG_DEBUG("urma api unloaded");
}

Result UrmaBackendImpl::InitUrma() noexcept
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

void UrmaBackendImpl::UnInitUrma() noexcept
{
    if (!urma_inited_) {
        return;
    }

    UrmaApi::urma_uninit();
    urma_inited_ = false;
}

Result UrmaBackendImpl::InitContextPool() noexcept
{
    auto tmp_context_pool = MakeRef<UrmaContextPool>();
    if (tmp_context_pool == nullptr) {
        UBS_VLOG_ERR("Create context pool object failed, probably out of memory");
        return UBS_ERROR;
    }

    auto tmp_ub_port_selector = MakeRef<UrmaPortSelector>(tmp_context_pool);
    if (tmp_ub_port_selector == nullptr) {
        UBS_VLOG_ERR("Create ub port selector failed, probably out of memory");
        return UBS_ERROR;
    }

    /* add dev and eid index
     * case1: if dev name and eid index if set in ENV
     * case2: if not set in ENV, get first available dev
     */
    Result result = UBS_OK;
    if (UrmaSetting::UB_DEV_NAME.empty()) {
        result = tmp_context_pool->AddEidIndex(UrmaSetting::UB_DEV_NAME, UrmaSetting::UB_EID_INDEX);
    } else {
        result = tmp_context_pool->AutoAddBoundingDevIndex();
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

    /* get best context */
    result = context_pool_->GetBestContext(best_context_);
    if (result != UBS_OK) {
        UBS_VLOG_ERR("Failed to get best urma context pool, result: %d", result);
        return result;
    }

    /* set object to member variables */
    context_pool_ = tmp_context_pool;
    ub_port_selector = tmp_ub_port_selector;

    return UBS_OK;
}

void UrmaBackendImpl::UnInitContextPool() noexcept
{
    /* release the ref of context pool firstly */
    ub_port_selector = nullptr;

    /* un-init context pool */
    if (context_pool_ != nullptr) {
        context_pool_->UnInit();
        context_pool_ = nullptr;
    }
}

UrmaIoBufferPool *UrmaBackend::GetAllocator() noexcept
{
    return &urma_allocator_;
}

Result UrmaBackendImpl::InitJettyPool() noexcept
{
    if (best_context_ == nullptr) {
        UBS_VLOG_ERR("No best context set, probably not init context pool");
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
    options.context_ = best_context_;

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

void UrmaBackendImpl::UnInitJettyPool() noexcept
{
    if (jetty_pool_ != nullptr) {
        jetty_pool_->UnInit();
        jetty_pool_ = nullptr;
    }
}

Result UrmaBackendImpl::InitPollerSet() noexcept
{
    if (best_context_ == nullptr) {
        UBS_VLOG_ERR("No best context set, probably not init poller pool");
        return UBS_ERROR;
    }

    UrmaPollerOptions options;
    options.poller_count = UrmaSetting::UB_POLLER_COUNT;
    options.jfc_depth = UrmaSetting::UB_POLLER_JFC_DEPTH;
    options.context_ = best_context_;
    options.polling_type = UrmaSetting::UB_POLLER_BUSY_POLLING ? BUSY_POLLING : EVENT_POLLING;
    options.cqe_handler = std::bind(&UrmaBackendImpl::HandleCqe, this, std::placeholders::_1, std::placeholders::_2,
                                    std::placeholders::_3);

    auto tmp_poller_set = MakeRef<UrmaPollerSet>();
    if (tmp_poller_set == nullptr) {
        UBS_VLOG_ERR("Failed to create Urma poller set, probably out of memory");
        return UBS_ERROR;
    }

    auto result = tmp_poller_set->Init(options);
    if (result != UBS_OK) {
        UBS_VLOG_ERR("Failed to init Urma poller set, result %d", result);
        return UBS_ERROR;
    }

    poller_set_ = tmp_poller_set;

    return UBS_OK;
}

void UrmaBackendImpl::UnInitPollerSet() noexcept
{
    if (poller_set_ != nullptr) {
        poller_set_->UnInit();
        poller_set_ = nullptr;
    }
}

Result UrmaBackendImpl::InitIoBufPool() noexcept
{
    if (best_context_ == nullptr) {
        UBS_VLOG_ERR("No best context set, probably not init io buff pool");
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

    auto result = tmp_io_buf_pool->init(options, best_context_);
    if (result != UBS_OK) {
        UBS_VLOG_ERR("Failed to init io buff pool, result %d", result);
        return UBS_ERROR;
    }

    io_buff_pool_ = tmp_io_buf_pool;

    return UBS_OK;
}

void UrmaBackendImpl::UnInitIoBufPool() noexcept
{
    if (io_buff_pool_ != nullptr) {
        io_buff_pool_->un_init();
        io_buff_pool_ = nullptr;
    }
}

void UrmaBackendImpl::SetLogLevel(int level) noexcept
{
    std::lock_guard<std::mutex> locker(mutex_);
    if (!inited_) {
        return;
    }

    UrmaApi::urma_log_set_level(UrmaGetLogLevel());
}

Result UrmaBackendImpl::HandleCqe(uint32_t flags, int cr_count, urma_cr_t *cr)
{
    (void)flags;
    for (auto i = 0; i < cr_count; i++) {
        if (cr[i].status != URMA_CR_SUCCESS) {
            // TODO: add process for error status
            UBS_VLOG_ERR("handle error cr: status=%u, user_ctx=0x%lx, op_code=%u, flag=0x%hhx", cr[i].status,
                         cr[i].user_ctx, cr[i].opcode, cr[i].flag.value);
            continue;
        }

        if (cr[i].flag.bs.s_r == 0) { // sender side
            continue;
        }

        // receiver side
        if (cr[i].opcode == URMA_CR_OPC_WRITE_WITH_IMM) {
            auto fd = static_cast<int>(cr[i].user_ctx);
            auto sock = ArraySet<Socket>::GetInstance().GetItem(fd);
            if (UNLIKELY(sock == nullptr)) {
                UBS_VLOG_ERR("socket is nullptr for fd: %d", fd);
                continue;
            }

            auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
            urma_sock->HandleRecvCqe(cr[i].imm_data);
        } else {
            // TODO: other opcode
        }
    }
    return UBS_OK;
}

Result UrmaBackendImpl::CreateSocket(int fd, SocketType t, SocketPtr &outSocket) noexcept
{
    return UBS_OK;
}

Result UrmaBackendImpl::CreateTxOps(SocketType value, const SocketPtr &sock, DataTxOps *&ops) noexcept
{
    auto tx_ops = new (std::nothrow) UrmaTxOps(sock->raw_socket_);
    if (UNLIKELY(tx_ops == nullptr)) {
        UBS_VLOG_ERR("Failed to create urma tx ops, probably out of memory");
        return UBS_MALLOC_FAILED;
    }

    ops = tx_ops;
    return UBS_OK;
}

Result UrmaBackendImpl::CreateRxOps(SocketType value, const SocketPtr &sock, DataRxOps *&ops) noexcept
{
    auto rx_ops = new (std::nothrow) UrmaRxOps(sock->raw_socket_);
    if (UNLIKELY(rx_ops == nullptr)) {
        UBS_VLOG_ERR("Failed to create urma rx ops, probably out of memory");
        return UBS_MALLOC_FAILED;
    }

    ops = rx_ops;
    return UBS_OK;
}

Result UrmaBackendImpl::CreateAcceptorOps(SocketType value, const SocketPtr &sock, AcceptorOps *&ops) noexcept
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

Result UrmaBackendImpl::CreateConnectorOps(SocketType value, const SocketPtr &sock, ConnectorOps *&ops) noexcept
{
    auto tmp_ops = new (std::nothrow) UrmaConnectorOps(sock->raw_socket_);
    if (UNLIKELY(tmp_ops == nullptr)) {
        UBS_VLOG_ERR("Failed to create urma connector ops, probably out of memory");
        return UBS_MALLOC_FAILED;
    }

    ops = tmp_ops;
    return UBS_OK;
}

} // namespace urma
} // namespace ubs
} // namespace ock
