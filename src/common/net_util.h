// Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
// Author: bao

#ifndef OCK_RDMA_UTIL_1233432457233_H
#define OCK_RDMA_UTIL_1233432457233_H

#include <arpa/inet.h>
#include <climits>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <cstring>
#include <ctime>
#include <semaphore.h>
#include <sys/time.h>
#include <vector>

#include "hcom_err.h"
#include "hcom_num_def.h"
#include "hcom_def.h"
#include "hcom_log.h"

namespace ock {
namespace hcom {
/* NetLocalAutoDecreasePtr */
template <typename T> class NetLocalAutoDecreasePtr {
public:
    explicit NetLocalAutoDecreasePtr(T *obj)
    {
        if (obj != nullptr) {
            mObj = obj;
            mObj->IncreaseRef();
        }
    }

    ~NetLocalAutoDecreasePtr()
    {
        if (mObj != nullptr) {
            mObj->DecreaseRef();
            mObj = nullptr;
        }
    }

    inline T *Get() const
    {
        return mObj;
    }

    /*
     * @brief Set inner obj to null, after the inner obj will be free during de-constructor
     */
    inline void SetNull()
    {
        mObj = nullptr;
    }

    NetLocalAutoDecreasePtr() = delete;
    NetLocalAutoDecreasePtr(const NetLocalAutoDecreasePtr<T> &) = delete;
    NetLocalAutoDecreasePtr(NetLocalAutoDecreasePtr<T> &&) = delete;
    // operator =
    NetLocalAutoDecreasePtr<T> &operator = (T *newObj) = delete;
    NetLocalAutoDecreasePtr<T> &operator = (const NetLocalAutoDecreasePtr<T> &other) = delete;
    NetLocalAutoDecreasePtr<T> &operator = (NetLocalAutoDecreasePtr<T> &&other) = delete;

private:
    T *mObj = nullptr;
};

/* NetLocalAutoFreePtr */
template <typename T> class NetLocalAutoFreePtr {
public:
    explicit NetLocalAutoFreePtr(T *obj, bool isArray = false) : mObj(obj), mIsArray(isArray) {}

    ~NetLocalAutoFreePtr()
    {
        if (mObj == nullptr) {
            return;
        }

        if (mIsArray) {
            delete[] mObj;
            mObj = nullptr;
        } else {
            delete mObj;
            mObj = nullptr;
        }
    }

    /*
     * @brief Set inner obj to null, after the inner obj will be free during de-constructor
     */
    inline void SetNull()
    {
        mObj = nullptr;
    }

    /*
     * @brief Get the inner obj ptr
     */
    inline T *Get() const
    {
        return mObj;
    }

    NetLocalAutoFreePtr() = delete;
    NetLocalAutoFreePtr(const NetLocalAutoFreePtr<T> &) = delete;
    NetLocalAutoFreePtr(NetLocalAutoFreePtr<T> &&) = delete;
    // operator =
    NetLocalAutoFreePtr<T> &operator = (T *newObj) = delete;
    NetLocalAutoFreePtr<T> &operator = (const NetLocalAutoFreePtr<T> &other) = delete;
    NetLocalAutoFreePtr<T> &operator = (NetLocalAutoFreePtr<T> &&other) = delete;

private:
    T *mObj = nullptr;
    bool mIsArray = false;
};

/// ScopeExit 主要功能为作用域退出时执行一些动作，常用于清理.
template<typename F> class ScopeExit {
public:
    ScopeExit(F f, bool active) : mHolder(std::move(f), active)
    {
    }

    ScopeExit(ScopeExit &&rhs) noexcept : mHolder(std::move(rhs.mHolder))
    {
        rhs.Deactivate();
    }

    ScopeExit(const ScopeExit &) = delete;
    ScopeExit &operator=(const ScopeExit &) = delete;
    ScopeExit &operator=(ScopeExit &&) = delete;

    void Deactivate()
    {
        mHolder.mActive = false;
    }

    bool Active() const
    {
        return mHolder.mActive;
    }

    ~ScopeExit()
    {
        if (Active()) {
            mHolder();
        }
    }

private:
    struct FuncHolder : public F {
        FuncHolder(F f, bool active) : F(std::move(f)), mActive(active)
        {
        }

        FuncHolder(FuncHolder &&rhs) noexcept : F(static_cast<F>(rhs)), mActive(rhs.mActive)
        {
            rhs.mActive = false;
        }

        FuncHolder& operator=(FuncHolder&&) = delete;

        bool mActive;
    };

    FuncHolder mHolder;
};

template<typename F> auto MakeScopeExit(F f, bool active = true) -> ScopeExit<F>
{
    return ScopeExit<F>(std::move(f), active);
}

bool BuffToHexString(void *buff, uint32_t buffSize, std::string &out);
bool HexStringToBuff(const std::string &str, uint32_t buffSize, void *buff);
uint32_t GenerateSecureRandomUint32();
}  // namespace hcom
}
#endif // OCK_RDMA_UTIL_1233432457233_H
