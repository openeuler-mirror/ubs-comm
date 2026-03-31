/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the utility for umq buffer, iov, etc
 * Author:
 * Create: 2026-03-28
 * Note:
 * History: 2026-03-28
*/
#include <iostream>
#include <semaphore.h>

#include "rpc_adpt_vlog.h"
#include "ub_lock_ops.h"
#include "umq_api.h"

static u_external_mutex_t* external_lock_create(u_external_mutex_type type)
{
    auto* mutex = new(std::nothrow) pthread_mutex_t();
    if (mutex == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Error when create mutex");
        return nullptr;
    }
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    
    if (type == LT_RECURSIVE) {
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    } else {
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
    }
    
    pthread_mutex_init(mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    return reinterpret_cast<u_external_mutex_t*>(mutex);
}

static int external_lock_destroy(u_external_mutex_t *m)
{
    if (m == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Error to execute external_lock_destroy for the pointer is nullptr \n");
        return -1;
    }
    if (int ret = pthread_mutex_destroy(reinterpret_cast<pthread_mutex_t*>(m)) != 0) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Error to execute pthread_mutex_destroy, ret: %d \n", ret);
        return ret;
    }
    delete reinterpret_cast<pthread_mutex_t*>(m);
    return 0;
}

static int external_lock_lock(u_external_mutex_t *m)
{
    if (m == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Error to execute external_lock_lock for the pointer is nullptr \n");
        return -1;
    }
    return pthread_mutex_lock(reinterpret_cast<pthread_mutex_t*>(m));
}

static int external_lock_unlock(u_external_mutex_t *m)
{
    if (m == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Error to execute external_lock_unlock for the pointer is nullptr \n");
        return -1;
    }
    return pthread_mutex_unlock(reinterpret_cast<pthread_mutex_t*>(m));
}

static int external_lock_try_lock(u_external_mutex_t *m)
{
    if (m == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Error to execute external_lock_try_lock for the pointer is nullptr \n");
        return -1;
    }
    return pthread_mutex_trylock(reinterpret_cast<pthread_mutex_t*>(m));
}

static u_rw_lock_t* rw_lock_create()
{
    auto* rwlock = new(std::nothrow) pthread_rwlock_t();
    if (rwlock == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Error when create rwlock");
        return nullptr;
    }
    pthread_rwlock_init(rwlock, nullptr);
    return reinterpret_cast<u_rw_lock_t*>(rwlock);
}

static int rw_lock_destroy(u_rw_lock_t *m)
{
    if (m == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Error to execute rw_lock_destroy for the pointer is nullptr \n");
        return -1;
    }
    if (int ret = pthread_rwlock_destroy(reinterpret_cast<pthread_rwlock_t*>(m)) != 0) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Error to execute pthread_rwlock_destroy, ret: %d \n", ret);
        return ret;
    }
    delete reinterpret_cast<pthread_rwlock_t*>(m);
    return 0;
}

static int rw_lock_lock_read(u_rw_lock_t *m)
{
    if (m == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Error to execute rw_lock_lock_read for the pointer is nullptr \n");
        return -1;
    }
    return pthread_rwlock_rdlock(reinterpret_cast<pthread_rwlock_t*>(m));
}

static int rw_lock_lock_write(u_rw_lock_t *m)
{
    if (m == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Error to execute rw_lock_lock_write for the pointer is nullptr \n");
        return -1;
    }
    return pthread_rwlock_wrlock(reinterpret_cast<pthread_rwlock_t*>(m));
}

static int rw_lock_unlock_rw(u_rw_lock_t *m)
{
    if (m == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Error to execute rw_lock_unlock_rw for the pointer is nullptr \n");
        return -1;
    }
    return pthread_rwlock_unlock(reinterpret_cast<pthread_rwlock_t*>(m));
}

static int rw_lock_try_lock_read(u_rw_lock_t *m)
{
    if (m == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Error to execute rw_lock_try_lock_read for the pointer is nullptr \n");
        return -1;
    }
    return pthread_rwlock_tryrdlock(reinterpret_cast<pthread_rwlock_t*>(m));
}

static int rw_lock_try_lock_write(u_rw_lock_t *m)
{
    if (m == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Error to execute rw_lock_try_lock_write for the pointer is nullptr \n");
        return -1;
    }
    return pthread_rwlock_trywrlock(reinterpret_cast<pthread_rwlock_t*>(m));
}

static u_semaphore_t* semaphore_create()
{
    auto* sem = new(std::nothrow) sem_t();
    if (sem == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Error when create sem");
        return nullptr;
    }
    return reinterpret_cast<u_semaphore_t*>(sem);
}

static int semaphore_destroy(u_semaphore_t *s)
{
    if (s == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Error to execute semaphore_destroy for the pointer is nullptr \n");
        return -1;
    }
    if (int ret = sem_destroy(reinterpret_cast<sem_t*>(s)) != 0) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Error to execute sem_destroy, ret: %d \n", ret);
        return ret;
    }
    delete reinterpret_cast<sem_t*>(s);
    return 0;
}

static int semaphore_init(u_semaphore_t *s, int shared, unsigned int value)
{
    if (s == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Error to execute semaphore_init for the pointer is nullptr \n");
        return -1;
    }
    return sem_init(reinterpret_cast<sem_t*>(s), shared, value);
}

static int semaphore_wait(u_semaphore_t *s)
{
    if (s == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Error to execute semaphore_wait for the pointer is nullptr \n");
        return -1;
    }
    return sem_wait(reinterpret_cast<sem_t*>(s));
}

static int semaphore_post(u_semaphore_t *s)
{
    if (s == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Error to execute semaphore_post for the pointer is nullptr \n");
        return -1;
    }
    return sem_post(reinterpret_cast<sem_t*>(s));
}

u_external_lock_ops_t g_external_lock_ops = {
    .create = external_lock_create,
    .destroy = external_lock_destroy,
    .lock = external_lock_lock,
    .unlock = external_lock_unlock,
    .try_lock = external_lock_try_lock
};

u_rw_lock_ops_t g_rw_lock_ops = {
    .create = rw_lock_create,
    .destroy = rw_lock_destroy,
    .lock_read = rw_lock_lock_read,
    .lock_write = rw_lock_lock_write,
    .unlock_rw = rw_lock_unlock_rw,
    .try_lock_read = rw_lock_try_lock_read,
    .try_lock_write = rw_lock_try_lock_write
};

u_semaphore_ops_t g_semaphore_ops = {
    .create = semaphore_create,
    .destroy = semaphore_destroy,
    .init = semaphore_init,
    .wait = semaphore_wait,
    .post = semaphore_post
};

int u_register_external_lock_ops(const u_external_lock_ops_t *ops)
{
    if (!ops || !ops->create || !ops->destroy || !ops->lock || !ops->unlock || !ops->try_lock) {
        return -1;
    }
    g_external_lock_ops = *ops;
    umq_external_mutex_lock_ops_t umq_mutex_ops = {
        .create = (umq_external_mutex_t *(*)(umq_external_mutex_attr_t))ops->create,
        .destroy = (int (*)(umq_external_mutex_t *))ops->destroy,
        .lock = (int (*)(umq_external_mutex_t *))ops->lock,
        .unlock = (int (*)(umq_external_mutex_t *))ops->unlock,
        .trylock = (int (*)(umq_external_mutex_t *))ops->try_lock
    };
    return umq_external_mutex_lock_ops_register(&umq_mutex_ops);
}

int u_register_rw_lock_ops(const u_rw_lock_ops_t *ops)
{
    if (!ops || !ops->create || !ops->destroy || !ops->lock_read || !ops->lock_write ||
        !ops->unlock_rw || !ops->try_lock_read || !ops->try_lock_write) {
        return -1;
    }
    g_rw_lock_ops = *ops;
    umq_external_rw_lock_ops umq_rwlock_ops = {
        .create = (umq_external_rwlock_t *(*)(void))ops->create,
        .destroy = (int (*)(umq_external_rwlock_t *))ops->destroy,
        .read_lock = (int (*)(umq_external_rwlock_t *))ops->lock_read,
        .write_lock = (int (*)(umq_external_rwlock_t *))ops->lock_write,
        .unlock = (int (*)(umq_external_rwlock_t *))ops->unlock_rw,
        .try_read_lock = (int (*)(umq_external_rwlock_t *))ops->try_lock_read,
        .try_write_lock = (int (*)(umq_external_rwlock_t *))ops->try_lock_write
    };
    return umq_external_rwlock_ops_register(&umq_rwlock_ops);
}

int u_register_semaphore_ops(const u_semaphore_ops_t *ops)
{
    if (!ops || !ops->create || !ops->destroy || !ops->init || !ops->wait || !ops->post) {
        return -1;
    }
    g_semaphore_ops = *ops;
    return 0;
}