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
#ifndef UBS_COMM_UBSOCKET_DEF_H
#define UBS_COMM_UBSOCKET_DEF_H

#include <stdint.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 
 * structures for external mutex and semaphore
 */
typedef void *u_mutex_t;
typedef void *u_rw_lock_t;
typedef void *u_semaphore_t;

typedef enum
{
    LT_EXCLUSIVE = 0,
    LT_RECURSIVE,
    LT_BUTT,
} u_mutex_type_t;

typedef struct {
    u_mutex_t *(*create)(u_mutex_type_t type);
    int (*destroy)(u_mutex_t *m);
    int (*lock)(u_mutex_t *m);
    int (*unlock)(u_mutex_t *m);
    int (*try_lock)(u_mutex_t *m);
} u_external_lock_ops_t;

typedef struct {
    u_rw_lock_t *(*create)();
    int (*destroy)(u_rw_lock_t *m);
    int (*lock_read)(u_rw_lock_t *m);
    int (*lock_write)(u_rw_lock_t *m);
    int (*unlock_rw)(u_rw_lock_t *m);
    int (*try_lock_read)(u_rw_lock_t *m);
    int (*try_lock_write)(u_rw_lock_t *m);
} u_external_rw_lock_ops_t;

typedef struct {
    u_semaphore_t *(*create)();
    int (*destroy)(u_semaphore_t *s);
    int (*init)(u_semaphore_t *s, int shared, unsigned int value);
    int (*wait)(u_semaphore_t *s);
    int (*post)(u_semaphore_t *s);
} u_external_semaphore_ops_t;

typedef struct {
    void *(*get_rpc_id)();
    void *(*get_rpc_call_timestamp)();
} u_external_rpc_id_ops_t;

typedef void (*u_poller_event_cb_t)(void *arg);

/*
 * callback to process one epoll event polled by the external direct poller.
 * return 0 to continue polling; return 1 when a STOP event is received and
 * the external poller should exit its poll loop.
 */
typedef int (*u_poller_process_event_cb_t)(void *arg, const struct epoll_event *event);

/*
 * direct dispatch mode: deliver one socket's ready event (epoll_data is the
 * value registered by the external epoll_ctl caller, events is EPOLLIN /
 * EPOLLOUT) straight to the external poller for in-place handling, instead
 * of queueing it and waking the external epoll_wait via the readable eventfd.
 */
typedef void (*u_poller_dispatch_event_cb_t)(uint64_t epoll_data, uint32_t events);

typedef struct {
    int (*add_consumer)(int fd, void *arg, u_poller_event_cb_t callback, void **consumer);
    void (*remove_consumer)(void *consumer, int fd);
    /*
     * direct poller mode: the external poller does blocking epoll_wait on
     * epoll_fd directly and invokes process_cb for each polled event, instead
     * of nesting epoll_fd into another epoll and draining via callback.
     */
    int (*add_direct_poller)(int epoll_fd, void *arg, u_poller_process_event_cb_t process_cb, void **poller);
    void (*remove_direct_poller)(void *poller, int epoll_fd);
    /* optional, see u_poller_dispatch_event_cb_t; NULL falls back to the readable eventfd path */
    u_poller_dispatch_event_cb_t dispatch_event;
    /*
     * optional; when the external poller defers handler wakeups inside
     * dispatch_event (enqueue without signaling), this is invoked after a
     * batch of dispatch_event calls to wake up all queued handlers at once.
     */
    void (*dispatch_flush)();
    /*
     * Optional host-provided cooperative yield. It may be called from a
     * poller loop or synchronous teardown, on either a worker fiber or a
     * regular pthread, and must not depend on poller-local state. NULL falls
     * back to sched_yield (dedicated-pthread semantics).
     */
    void (*poller_yield)();
} u_external_poller_ops_t;

/*
 * structures for ubsocket
 */
#define UBS_PROTOCOL_TCP 1 << 0L
#define UBS_PROTOCOL_UB_RM_RTP 1 << 1L
#define UBS_PROTOCOL_UB_RC_RTP 1 << 2L

typedef struct {
    uint32_t allowed_protocol;             /* allowed underlay protocol */
    uint32_t async_acceptor_thread_count;  /* thread count of async acceptor, 0 means async disabled */
    uint32_t async_connector_thread_count; /* thread count of async connector, 0 means async disabled */
    uint32_t async_epoll_thread_count;     /* thread count of async epoll_wait, 0 means async disabled */
    u_external_lock_ops_t *lock_ops;       /* external lock operations, for example brpc's butex */
    u_external_rw_lock_ops_t *rw_lock_ops; /* external lock operations, for example brpc's butex */
    u_external_semaphore_ops_t *sem_ops;   /* external lock operations, for example brpc's sem */
    u_external_rpc_id_ops_t *rpc_id_ops;
    u_external_poller_ops_t *poller_ops; /* external poller operations, for example brpc's EventDispatcher */
} u_init_options_t;

enum class UbsocketLevel : int
{
    // Does not conflict with the native level of the system
    SOL_UB = 0x1000,
};

enum class UbSocketOpt : int
{
    UBS_OPT_PROTOCOL = 1,
    UBS_OPT_RPC_TIMEOUT_MS = 2, /* design §4.2: set connection-level RPC timeout (uint32, ms) */
};

#define UB_API_WRAP(FUNC) ubsocket_##FUNC

#ifdef __cplusplus
}
#endif

#endif // UBS_COMM_UBSOCKET_DEF_H
