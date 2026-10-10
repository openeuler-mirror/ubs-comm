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
#ifndef UBS_COMM_URMA_BACKEND_H
#define UBS_COMM_URMA_BACKEND_H

#include <mutex>
#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_defines.h"
#include "common/ubsocket_obj_pool.h"
#include "core/ubsocket_socket.h"
#include "urma_alive_manager.h"
#include "urma_context_pool.h"
#include "urma_io_buf_pool.h"
#include "urma_jetty_pool.h"
#include "urma_poller_agg.h"
#include "urma_setting.h"
#include "urma_wrapper.h"

namespace ock {
namespace ubs {
namespace urma {

class UrmaBackend {
public:
    static UrmaBackend &Instance()
    {
        static UrmaBackend instance;
        return instance;
    }

public:
    Result Init(bool initUrmaOnly = false) noexcept;
    void UnInit() noexcept;

    void SetLogLevel(int level) noexcept;

    Result CreateSocket(int fd, SocketType t, SocketPtr &outSocket) noexcept;
    static Result CreateTxOps(SocketType value, const SocketPtr &sock, DataTxOps *&ops) noexcept;
    static Result CreateRxOps(SocketType value, const SocketPtr &sock, DataRxOps *&ops) noexcept;
    static Result CreateAcceptorOps(SocketType value, const SocketPtr &sock, AcceptorOps *&acceptor) noexcept;
    static Result CreateConnectorOps(SocketType value, const SocketPtr &sock, ConnectorOps *&connector) noexcept;

    UrmaIoBufferPool *GetAllocator() noexcept;

    UrmaContextPoolPtr GetContextPoll() noexcept;

    UrmaAggPollerSetPtr GetPollerSet() noexcept;

    UrmaAliveManagerPtr GetAliveManager() noexcept;

    /*
     * Pending-close hold: after PostCloseToPeer succeeds, the socket's fd has
     * been removed from ArraySet but the CLOSE WR is not yet flushed to the peer. Hold a
     * SocketPtr reference so the jetty stays alive until the CLOSE send CQE arrives
     * (or an error CQE / UnInit sweep releases it). Otherwise ~UrmaSocket would destroy
     * the jetty and flush the in-flight CLOSE WR — the peer would see an error CQE
     * (ECONNRESET) instead of the graceful EOF.
     */
    void HoldPendingClose(int fd, Socket *sock) noexcept;
    void ReleasePendingClose(int fd) noexcept;
    bool HasPendingClose(int fd) noexcept;
    void ClearPendingClose() noexcept; /* sweep all holds (UnInit safety net) */

    /*
     * fd placeholder: atomically swap the socket fd for a shared /dev/null via
     * dup3 so the fd number stays occupied.
     * @return true: fd taken over (original TCP socket closed by dup3, caller must
     *               NOT close the fd again); false: degraded (entry already released
     *               by a racing CQE, or dup3 failed) — caller still owns the fd.
     */
    bool TakeFdPlaceholder(int fd) noexcept;

    void RecordBrokenConn(uint64_t conn_addr_id, uint64_t conn_id) noexcept;

    uint64_t TakeBrokenConnId(uint64_t conn_addr_id) noexcept;

    void EraseBrokenConn(uint64_t conn_addr_id) noexcept;

    /* 全局单调递增连接号分配器（同步/异步建链共用，保证 conn_id 全局唯一） */
    uint64_t AllocConnId() noexcept;

private:
    void UnInitInner() noexcept;

    Result LoadUrmaApi() noexcept;
    void UnLoadUrmaApi() noexcept;

    Result InitUrma() noexcept;
    void UnInitUrma() noexcept;

    Result InitContextPool() noexcept;
    void UnInitContextPool() noexcept;

    Result InitJettyPool() noexcept;
    void UnInitJettyPool() noexcept;

    Result InitPollerSet() noexcept;
    void UnInitPollerSet() noexcept;

    Result InitIoBufPool() noexcept;
    void UnInitIoBufPool() noexcept;

    Result InitObjectPool() noexcept;
    void UnInitObjectPool() noexcept;

    Result InitKeepAliveManager();
    void UnInitKeepAliveManager();

    Result HandleCqe(uint32_t flags, int cr_count, urma_cr_t *cr);
    void HandleErrorCqe(urma_cr_t *cr);

private:
    /* put the hottest used variables on the head */
    UrmaIoBufferPoolPtr io_buff_pool_; /* io buffer pool */
    UrmaContextPoolPtr context_pool_;  /* urma context pool */
    UrmaAggPollerSetPtr poller_set_;   /* urma poller set */
    UrmaJettyPoolPtr jetty_pool_;      /* jetty pool */
    UrmaAliveManagerPtr alive_manager_;  /* alive manager */

    /* non-hot variables */
    std::mutex mutex_;         /* mutex for things */
    bool inited_ = false;      /* init flag for whole backend */
    bool urma_inited_ = false; /* init flag for urma init */
    bool init_urma_only_ = false;

    /* pending-close hold registry: fd → held SocketPtr (guards in-flight CLOSE WR);
     * placeholder_taken marks the fd number being held by a /dev/null placeholder */
    struct PendingCloseEntry {
        SocketPtr sock;
        bool placeholder_taken = false;
    };
    std::unordered_map<int, PendingCloseEntry> pending_close_holds_;
    std::mutex pending_close_mutex_;

    std::mutex broken_conn_mutex_;                   /* guards broken_conns_ */
    std::map<uint64_t, uint64_t> broken_conns_{};    /* peer addr id -> last broken conn_id */
};

} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_BACKEND_H
