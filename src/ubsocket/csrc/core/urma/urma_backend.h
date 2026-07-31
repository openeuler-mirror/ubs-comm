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
#include "urma_io_buf_pool.h"
#include "urma_jetty_pool.h"
#include "urma_poller.h"
#include "urma_port_selector.h"
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
    Result Init() noexcept;
    void UnInit() noexcept;

    void SetLogLevel(int level) noexcept;

    Result CreateSocket(int fd, SocketType t, SocketPtr &outSocket) noexcept;
    static Result CreateTxOps(SocketType value, const SocketPtr &sock, DataTxOps *&ops) noexcept;
    static Result CreateRxOps(SocketType value, const SocketPtr &sock, DataRxOps *&ops) noexcept;
    static Result CreateAcceptorOps(SocketType value, const SocketPtr &sock, AcceptorOps *&acceptor) noexcept;
    static Result CreateConnectorOps(SocketType value, const SocketPtr &sock, ConnectorOps *&connector) noexcept;

    UrmaIoBufferPool *GetAllocator() noexcept;
    const IdentifyNode &GetLocalIdentifyNode() noexcept;
    const UrmaContextPtr &GetUrmaContext() noexcept;

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

    Result InitLocalIdentifyNodeInfo() noexcept;

    Result InitObjectPool() noexcept;
    void UnInitObjectPool() noexcept;

    Result HandleCqe(uint32_t flags, int cr_count, urma_cr_t *cr);

private:
    /* put the hottest used variables on the head */
    UrmaIoBufferPoolPtr io_buff_pool_;    /* io buffer pool */
    UrmaContextPoolPtr context_pool_;     /* urma context pool */
    UrmaPortSelectorPtr ub_port_selector; /* ub port selector for HA, 1 master 3 backup */
    UrmaContextPtr best_context_;         /* best urma context */
    IdentifyNode local_identify_node_;    /* local identify node info */
    UrmaJettyPoolPtr jetty_pool_;         /* jetty pool */

    /* non-hot variables */
    std::mutex mutex_;         /* mutex for things */
    bool inited_ = false;      /* init flag for whole backend */
    bool urma_inited_ = false; /* init flag for urma init */
};

} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_BACKEND_H
