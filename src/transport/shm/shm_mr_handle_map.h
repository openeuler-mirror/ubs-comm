/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef OCK_HCOM_SHM_HANDLE_MAP_H
#define OCK_HCOM_SHM_HANDLE_MAP_H
#include "shm_lock_guard.h"

namespace ock {
namespace hcom {
class ShmHandle;
using ShmHandlePtr = NetRef<ShmHandle>;

class ShmMRHandleMap {
public:
    ShmMRHandleMap(const ShmMRHandleMap &) = delete;
    ShmMRHandleMap &operator = (const ShmMRHandleMap &) = delete;

    static ShmMRHandleMap &GetInstance()
    {
        static ShmMRHandleMap shmMrHandleMap;
        return shmMrHandleMap;
    }

    inline NResult AddToLocalMap(uint32_t key, const ShmHandlePtr &shmHandle)
    {
        RWLockGuard(mLRwLock).LockWrite();
        mMrLKeyFdMap.emplace(key, shmHandle);
        return NN_OK;
    }

    inline void ClearLocalMap()
    {
        RWLockGuard(mLRwLock).LockWrite();
        if (!mMrLKeyFdMap.empty()) {
            mMrLKeyFdMap.clear();
        }
    }

    inline ShmHandlePtr GetFromLocalMap(uint32_t key)
    {
        RWLockGuard(mLRwLock).LockRead();
        auto iter = mMrLKeyFdMap.find(key);
        if (iter == mMrLKeyFdMap.end()) {
            return nullptr;
        }
        return iter->second;
    }

    inline NResult AddToRemoteMap(uint32_t key, const ShmHandlePtr &shmHandle)
    {
        RWLockGuard(mRRwLock).LockWrite();
        mMrRKeyFdMap.emplace(key, shmHandle);
        return NN_OK;
    }

    inline void ClearRemoteMap()
    {
        RWLockGuard(mRRwLock).LockWrite();
        if (!mMrRKeyFdMap.empty()) {
            mMrRKeyFdMap.clear();
        }
    }

    inline ShmHandlePtr GetFromRemoteMap(uint32_t key)
    {
        RWLockGuard(mRRwLock).LockRead();
        auto iter = mMrRKeyFdMap.find(key);
        if (iter == mMrRKeyFdMap.end()) {
            return nullptr;
        }
        return iter->second;
    }

private:
    ShmMRHandleMap() = default;
    ~ShmMRHandleMap()
    {
        ClearLocalMap();
        ClearRemoteMap();
    }

private:
    NetReadWriteLock mLRwLock;
    NetReadWriteLock mRRwLock;
    std::map<uint32_t, ShmHandlePtr> mMrLKeyFdMap;
    std::map<uint32_t, ShmHandlePtr> mMrRKeyFdMap;
};
}
}
#endif