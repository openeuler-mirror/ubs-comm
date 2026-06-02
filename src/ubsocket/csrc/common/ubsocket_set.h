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
#ifndef UBS_COMM_UBSOCKET_SET_H
#define UBS_COMM_UBSOCKET_SET_H

#include "ubsocket_common_includes.h"

namespace ock {
namespace ubs {

template <typename T>
class ArraySet {
public:
    static ArraySet &GetInstance()
    {
        static ArraySet instance;
        return instance;
    }

    int Init()
    {
        rwlock_ = LockRegistry::RW_LOCK_OPS.create();
        if (rwlock_ != nullptr) {
            for (int i = 0; i < RPC_ADPT_FD_MAX; i++) {
                set_obj_[i] = nullptr;
            }
            return 0;
        }
        return -1;
    }

    ALWAYS_INLINE Ref<T> GetItem(int idx)
    {
        if (idx < 0 || idx >= RPC_ADPT_FD_MAX) {
            return Ref<T>();
        }
        ReadLocker lock(rwlock_);
        return Ref<T>(set_obj_[idx]);
    }

    Ref<T> OverrideItem(int idx, T *new_item)
    {
        if (idx < 0 || idx >= RPC_ADPT_FD_MAX) {
            return Ref<T>();
        }
        if (new_item != nullptr) {
            new_item->IncreaseRef();
        }

        T *old_item = nullptr;
        {
            WriteLocker lock(rwlock_);
            old_item = set_obj_[idx];
            set_obj_[idx] = new_item;
        }

        if (old_item != nullptr) {
            old_item->DecreaseRef();
        }
        return Ref<T>(old_item);
    }

    Ref<T> RemoveItem(int idx)
    {
        if (idx < 0 || idx >= RPC_ADPT_FD_MAX) {
            return Ref<T>();
        }
        T *item = nullptr;
        {
            WriteLocker lock(rwlock_);
            item = set_obj_[idx];
            set_obj_[idx] = nullptr;
        }
        return Ref<T>(item);
    }

    void ReleaseAll()
    {
        WriteLocker lock(rwlock_);
        for (int i = 0; i < RPC_ADPT_FD_MAX; i++) {
            if (set_obj_[i] != nullptr) {
                set_obj_[i]->DecreaseRef();
                set_obj_[i] = nullptr;
            }
        }
    }

    void ForEach(const std::function<void(int fd, T *)> &callback)
    {
        ReadLocker lock(rwlock_);
        for (int i = 0; i < RPC_ADPT_FD_MAX; i++) {
            if (set_obj_[i] != nullptr) {
                callback(i, set_obj_[i]);
            }
        }
    }

    size_t Size()
    {
        ReadLocker lock(rwlock_);
        size_t count = 0;
        for (int i = 0; i < RPC_ADPT_FD_MAX; i++) {
            if (set_obj_[i] != nullptr) {
                count++;
            }
        }
        return count;
    }

private:
    ArraySet() = default;

    ~ArraySet()
    {
        ReleaseAll();
        if (rwlock_ != nullptr) {
            LockRegistry::RW_LOCK_OPS.destroy(rwlock_);
            rwlock_ = nullptr;
        }
    }

    ArraySet(const ArraySet &) = delete;
    ArraySet &operator=(const ArraySet &) = delete;

    u_rw_lock_t *rwlock_ = nullptr;
    T *set_obj_[RPC_ADPT_FD_MAX];
};

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_SET_H