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
#include <pthread.h>
#include <semaphore.h>
#include <umq_api.h>
#include <umq_types.h>

#include "ubsocket_lock.h"
#include "ubsocket_logger.h"

namespace ock {
namespace ubs {
namespace default_locks {
static u_mutex_t *default_lock_create(u_mutex_type_t type)
{
    auto *mutex = new (std::nothrow) pthread_mutex_t();
    if (mutex == nullptr) {
        UBS_VLOG_ERR("Error when create mutex\n");
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
    return reinterpret_cast<u_mutex_t *>(mutex);
}

static int default_lock_destroy(u_mutex_t *m)
{
    if (UNLIKELY(m == nullptr)) {
        UBS_VLOG_ERR("Error to execute external_lock_destroy for the pointer is nullptr \n");
        return -1;
    }
    if (int ret = pthread_mutex_destroy(reinterpret_cast<pthread_mutex_t *>(m)) != 0) {
        UBS_VLOG_ERR("Error to execute pthread_mutex_destroy, ret: %d \n", ret);
        return ret;
    }
    delete reinterpret_cast<pthread_mutex_t *>(m);
    return 0;
}

static int default_lock_lock(u_mutex_t *m)
{
    if (UNLIKELY(m == nullptr)) {
        UBS_VLOG_ERR("Error to execute external_lock_lock for the pointer is nullptr \n");
        return -1;
    }
    return pthread_mutex_lock(reinterpret_cast<pthread_mutex_t *>(m));
}

static int default_lock_unlock(u_mutex_t *m)
{
    if (UNLIKELY(m == nullptr)) {
        UBS_VLOG_ERR("Error to execute external_lock_unlock for the pointer is nullptr \n");
        return -1;
    }
    return pthread_mutex_unlock(reinterpret_cast<pthread_mutex_t *>(m));
}

static int default_lock_try_lock(u_mutex_t *m)
{
    if (UNLIKELY(m == nullptr)) {
        UBS_VLOG_ERR("Error to execute external_lock_try_lock for the pointer is nullptr \n");
        return -1;
    }
    return pthread_mutex_trylock(reinterpret_cast<pthread_mutex_t *>(m));
}

static u_rw_lock_t *default_rw_lock_create()
{
    auto *rwlock = new (std::nothrow) pthread_rwlock_t();
    if (rwlock == nullptr) {
        UBS_VLOG_ERR("Error when create rwlock \n");
        return nullptr;
    }
    pthread_rwlock_init(rwlock, nullptr);
    return reinterpret_cast<u_rw_lock_t *>(rwlock);
}

static int default_rw_lock_destroy(u_rw_lock_t *m)
{
    if (UNLIKELY(m == nullptr)) {
        UBS_VLOG_ERR("Error to execute rw_lock_destroy for the pointer is nullptr \n");
        return -1;
    }
    if (int ret = pthread_rwlock_destroy(reinterpret_cast<pthread_rwlock_t *>(m)) != 0) {
        UBS_VLOG_ERR("Error to execute pthread_rwlock_destroy, ret: %d \n", ret);
        return ret;
    }
    delete reinterpret_cast<pthread_rwlock_t *>(m);
    return 0;
}

static int default_rw_lock_lock_read(u_rw_lock_t *m)
{
    if (UNLIKELY(m == nullptr)) {
        UBS_VLOG_ERR("Error to execute rw_lock_lock_read for the pointer is nullptr \n");
        return -1;
    }
    return pthread_rwlock_rdlock(reinterpret_cast<pthread_rwlock_t *>(m));
}

static int default_rw_lock_lock_write(u_rw_lock_t *m)
{
    if (UNLIKELY(m == nullptr)) {
        UBS_VLOG_ERR("Error to execute rw_lock_lock_write for the pointer is nullptr \n");
        return -1;
    }
    return pthread_rwlock_wrlock(reinterpret_cast<pthread_rwlock_t *>(m));
}

static int default_rw_lock_unlock_rw(u_rw_lock_t *m)
{
    if (UNLIKELY(m == nullptr)) {
        UBS_VLOG_ERR("Error to execute rw_lock_unlock_rw for the pointer is nullptr \n");
        return -1;
    }
    return pthread_rwlock_unlock(reinterpret_cast<pthread_rwlock_t *>(m));
}

static int default_rw_lock_try_lock_read(u_rw_lock_t *m)
{
    if (UNLIKELY(m == nullptr)) {
        UBS_VLOG_ERR("Error to execute rw_lock_try_lock_read for the pointer is nullptr \n");
        return -1;
    }
    return pthread_rwlock_tryrdlock(reinterpret_cast<pthread_rwlock_t *>(m));
}

static int default_rw_lock_try_lock_write(u_rw_lock_t *m)
{
    if (UNLIKELY(m == nullptr)) {
        UBS_VLOG_ERR("Error to execute rw_lock_try_lock_write for the pointer is nullptr \n");
        return -1;
    }
    return pthread_rwlock_trywrlock(reinterpret_cast<pthread_rwlock_t *>(m));
}

static u_semaphore_t *default_semaphore_create()
{
    auto *sem = new (std::nothrow) sem_t();
    if (sem == nullptr) {
        UBS_VLOG_ERR("Error when create sem \n");
        return nullptr;
    }
    return reinterpret_cast<u_semaphore_t *>(sem);
}

static int default_semaphore_destroy(u_semaphore_t *s)
{
    if (UNLIKELY(s == nullptr)) {
        UBS_VLOG_ERR("Error to execute semaphore_destroy for the pointer is nullptr \n");
        return -1;
    }
    if (int ret = sem_destroy(reinterpret_cast<sem_t *>(s)) != 0) {
        UBS_VLOG_ERR("Error to execute sem_destroy, ret: %d \n", ret);
        return ret;
    }
    delete reinterpret_cast<sem_t *>(s);
    return 0;
}

static int default_semaphore_init(u_semaphore_t *s, int shared, unsigned int value)
{
    if (UNLIKELY(s == nullptr)) {
        UBS_VLOG_ERR("Error to execute semaphore_init for the pointer is nullptr \n");
        return -1;
    }
    return sem_init(reinterpret_cast<sem_t *>(s), shared, value);
}

static int default_semaphore_wait(u_semaphore_t *s)
{
    if (UNLIKELY(s == nullptr)) {
        UBS_VLOG_ERR("Error to execute semaphore_wait for the pointer is nullptr \n");
        return -1;
    }
    return sem_wait(reinterpret_cast<sem_t *>(s));
}

static int default_semaphore_post(u_semaphore_t *s)
{
    if (UNLIKELY(s == nullptr)) {
        UBS_VLOG_ERR("Error to execute semaphore_post for the pointer is nullptr \n");
        return -1;
    }
    return sem_post(reinterpret_cast<sem_t *>(s));
}
} // namespace default_locks

u_external_lock_ops_t LockRegistry::LOCK_OPS;
u_external_rw_lock_ops_t LockRegistry::RW_LOCK_OPS;
u_external_semaphore_ops_t LockRegistry::SEM_OPS;

Result LockRegistry::RegisterLockOps(u_external_lock_ops_t *ops)
{
    if (!ops || !ops->create || !ops->destroy || !ops->lock || !ops->unlock || !ops->try_lock) {
        return UBS_INVALID_PARAM;
    }

    LOCK_OPS = *ops;

    umq_external_mutex_lock_ops_t umq_mutex_ops = {
        .create = (umq_external_mutex_t * (*)(umq_external_mutex_attr_t)) ops->create,
        .destroy = (int (*)(umq_external_mutex_t *))ops->destroy,
        .lock = (int (*)(umq_external_mutex_t *))ops->lock,
        .unlock = (int (*)(umq_external_mutex_t *))ops->unlock,
        .trylock = (int (*)(umq_external_mutex_t *))ops->try_lock};
    return umq_external_mutex_lock_ops_register(&umq_mutex_ops);
}

Result LockRegistry::RegisterRwLockOps(u_external_rw_lock_ops_t *ops)
{
    if (!ops || !ops->create || !ops->destroy || !ops->lock_read || !ops->lock_write || !ops->unlock_rw ||
        !ops->try_lock_read || !ops->try_lock_write) {
        return UBS_INVALID_PARAM;
    }

    RW_LOCK_OPS = *ops;

    umq_external_rw_lock_ops umq_rwlock_ops = {.create = (umq_external_rwlock_t * (*)(void)) ops->create,
                                               .destroy = (int (*)(umq_external_rwlock_t *))ops->destroy,
                                               .read_lock = (int (*)(umq_external_rwlock_t *))ops->lock_read,
                                               .write_lock = (int (*)(umq_external_rwlock_t *))ops->lock_write,
                                               .unlock = (int (*)(umq_external_rwlock_t *))ops->unlock_rw,
                                               .try_read_lock = (int (*)(umq_external_rwlock_t *))ops->try_lock_read,
                                               .try_write_lock = (int (*)(umq_external_rwlock_t *))ops->try_lock_write};
    return umq_external_rwlock_ops_register(&umq_rwlock_ops);
}

Result LockRegistry::RegisterSemOps(u_external_semaphore_ops_t *ops)
{
    if (!ops || !ops->create || !ops->destroy || !ops->init || !ops->wait || !ops->post) {
        return UBS_INVALID_PARAM;
    }

    SEM_OPS = *ops;
    return 0;
}

Result LockRegistry::RegisterDefaultOps()
{
    using namespace default_locks;
    u_external_lock_ops_t default_lock_ops = {.create = default_lock_create,
                                              .destroy = default_lock_destroy,
                                              .lock = default_lock_lock,
                                              .unlock = default_lock_unlock,
                                              .try_lock = default_lock_try_lock};
    LOCK_OPS = default_lock_ops;

    u_external_rw_lock_ops_t default_rw_lock_ops = {.create = default_rw_lock_create,
                                                    .destroy = default_rw_lock_destroy,
                                                    .lock_read = default_rw_lock_lock_read,
                                                    .lock_write = default_rw_lock_lock_write,
                                                    .unlock_rw = default_rw_lock_unlock_rw,
                                                    .try_lock_read = default_rw_lock_try_lock_read,
                                                    .try_lock_write = default_rw_lock_try_lock_write};
    RW_LOCK_OPS = default_rw_lock_ops;

    u_external_semaphore_ops_t default_sem_ops = {.create = default_semaphore_create,
                                                  .destroy = default_semaphore_destroy,
                                                  .init = default_semaphore_init,
                                                  .wait = default_semaphore_wait,
                                                  .post = default_semaphore_post};
    SEM_OPS = default_sem_ops;

    return UBS_OK;
}

} // namespace ubs
} // namespace ock