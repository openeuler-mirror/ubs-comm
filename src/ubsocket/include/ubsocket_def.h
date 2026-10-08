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

typedef struct {
    int (*add_consumer)(int fd, void *arg, u_poller_event_cb_t callback, void **consumer);
    void (*remove_consumer)(void *consumer, int fd);
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
};

#define UB_API_WRAP(FUNC) ubsocket_##FUNC

#ifdef __cplusplus
}
#endif

#endif // UBS_COMM_UBSOCKET_DEF_H
