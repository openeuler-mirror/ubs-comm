/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the utility for umq buffer, iov, etc
 * Author:
 * Create: 2026-03-19
 * Note:
 * History: 2026-03-19
*/
#ifndef UB_LOCK_DEF_H
#define UB_LOCK_DEF_H

#ifdef __cplusplus
extern "C" {
#endif
typedef void* u_external_mutex_t;
typedef void* u_rw_lock_t;
typedef void* u_semaphore_t;

typedef enum {
    LT_EXCLUSIVE = 0,
    LT_RECURSIVE,
    LT_BUTT,
} u_external_mutex_type;

typedef struct {
    u_external_mutex_t* (*create)(u_external_mutex_type type);
    int (*destroy)(u_external_mutex_t *m);
    int (*lock)(u_external_mutex_t *m);
    int (*unlock)(u_external_mutex_t *m);
    int (*try_lock)(u_external_mutex_t *m);
} u_external_lock_ops_t;

typedef struct {
    u_rw_lock_t* (*create)();
    int (*destroy)(u_rw_lock_t *m);
    int (*lock_read)(u_rw_lock_t *m);
    int (*lock_write)(u_rw_lock_t *m);
    int (*unlock_rw)(u_rw_lock_t *m);
    int (*try_lock_read)(u_rw_lock_t *m);
    int (*try_lock_write)(u_rw_lock_t *m);
} u_rw_lock_ops_t;

typedef struct {
    u_semaphore_t* (*create)();
    int (*destroy)(u_semaphore_t *s);
    int (*init)(u_semaphore_t *s, int shared, unsigned int value);
    int (*wait)(u_semaphore_t *s);
    int (*post)(u_semaphore_t *s);
} u_semaphore_ops_t;
#ifdef __cplusplus
}
#endif

#endif // UB_LOCK_DEF_H