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
#ifndef OCK_COMM_OBJ_POOL_H_23444
#define OCK_COMM_OBJ_POOL_H_23444

#include <sstream>

#include "hcom_def.h"
#include "hcom_log.h"
#include "hcom_utils.h"

namespace ock {
namespace hcom {
template <typename T> class NetObjPool {
public:
    explicit NetObjPool(const std::string &name, uint32_t capacity) : mName(name), mObjRB(capacity) {}
    ~NetObjPool()
    {
        UnInitialize();
    }

    NResult Initialize()
    {
        std::lock_guard<std::mutex> locker(mInitMutex);
        if (mObjs != nullptr) {
            NN_LOG_INFO("Obj pool already initialized");
            return NN_OK;
        }

        mObjs = static_cast<T *>(malloc(sizeof(T) * mObjRB.Capacity()));
        if (mObjs == nullptr) {
            NN_LOG_ERROR("Failed to new objects for pool, probably out of memory");
            return NN_NEW_OBJECT_FAILED;
        }

        auto result = mObjRB.Initialize();
        if (result != NN_OK) {
            NN_LOG_ERROR("Failed to initialize ring buffer, result " << result);
            free(mObjs);
            mObjs = nullptr;
            return result;
        }

        for (uint32_t i = 0; i < mObjRB.Capacity(); i++) {
            mObjRB.PushBack(&(mObjs[i]));
        }

        mObjsEnd = &(mObjs[mObjRB.Capacity() - 1]);
        return NN_OK;
    }

    void UnInitialize()
    {
        std::lock_guard<std::mutex> locker(mInitMutex);
        if (mObjs != nullptr) {
            free(mObjs);
            mObjs = nullptr;
        }

        mObjRB.UnInitialize();
    }

    inline bool Dequeue(T *&item)
    {
        if (NN_LIKELY(mObjRB.PopFront(item))) {
            return true;
        }

        // new one
        NN_LOG_INFO("Create new object from malloc lib for pool " << mName << " as pool is fully");
        item = static_cast<T *>(malloc(sizeof(T)));
        if (NN_UNLIKELY(item == nullptr)) {
            NN_LOG_INFO("Create new object from malloc lib for pool " << mName << ", probably out of memory");
            return false;
        }
        return true;
    }

    inline void Enqueue(T *item)
    {
        if (NN_LIKELY(item >= mObjs && item <= mObjsEnd)) {
            mObjRB.PushFront(item);
        } else {
            if (NN_UNLIKELY(item != nullptr)) {
                free(item);
            }
        }
    }

    std::string ToString()
    {
        std::ostringstream oss;
        oss << "obj pool " << mName << ", capacity " << mObjRB.Capacity() << ", size " << mObjRB.Size() <<
            ", addresses " << mObjs;
        return oss.str();
    }

private:
    std::mutex mInitMutex;
    T *mObjs = nullptr;
    T *mObjsEnd = nullptr;
    std::string mName;
    NetRingBuffer<T *> mObjRB;
};
}
}

#endif // OCK_COMM_OBJ_POOL_H_23444
