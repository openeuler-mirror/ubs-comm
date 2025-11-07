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
#ifndef OCK_HCOM_CPP_REF_H
#define OCK_HCOM_CPP_REF_H

#include <iostream>

namespace ock {
namespace hcom {

/**
 * @brief Smart pointer object
 */
template <typename T> class NetRef {
public:
    // constructor
    NetRef() noexcept = default;

    // fix: can't be explicit
    NetRef(T *newObj) noexcept
    {
        // if new obj is not null, increase reference count and assign to mObj
        // else nothing need to do as mObj is nullptr by default
        if (newObj != nullptr) {
            newObj->IncreaseRef();
            mObj = newObj;
        }
    }

    NetRef(const NetRef<T> &other) noexcept
    {
        // if other's obj is not null, increase reference count and assign to mObj
        // else nothing need to do as mObj is nullptr by default
        if (other.mObj != nullptr) {
            other.mObj->IncreaseRef();
            mObj = other.mObj;
        }
    }

#if __GNUC__ == 4 && __GNUC_MINOR__ == 8 && __GNUC_PATCHLEVEL__ == 5
    NetRef(NetRef<T> &&other) noexcept : mObj(exchangeHcom(other.mObj, nullptr))
#else
    NetRef(NetRef<T> &&other) noexcept : mObj(std::__exchange(other.mObj, nullptr))
#endif
    {
        // move constructor
        // since this mObj is null, just exchange
    }

    // de-constructor
    ~NetRef()
    {
        if (mObj != nullptr) {
            mObj->DecreaseRef();
        }
    }

    // operator =
    inline NetRef<T> &operator = (T *newObj)
    {
        this->Set(newObj);
        return *this;
    }

    inline NetRef<T> &operator = (const NetRef<T> &other)
    {
        if (this != &other) {
            this->Set(other.mObj);
        }
        return *this;
    }

    NetRef<T> &operator = (NetRef<T> &&other) noexcept
    {
        if (this != &other) {
            auto tmp = mObj;
#if __GNUC__ == 4 && __GNUC_MINOR__ == 8 && __GNUC_PATCHLEVEL__ == 5
            mObj = exchangeHcom(other.mObj, nullptr);
#else
            mObj = std::__exchange(other.mObj, nullptr);
#endif
            if (tmp != nullptr) {
                tmp->DecreaseRef();
            }
        }
        return *this;
    }

    // equal operator
    inline bool operator == (const NetRef<T> &other) const
    {
        return mObj == other.mObj;
    }

    inline bool operator == (T *other) const
    {
        return mObj == other;
    }

    inline bool operator != (const NetRef<T> &other) const
    {
        return mObj != other.mObj;
    }

    inline bool operator != (T *other) const
    {
        return mObj != other;
    }

    // get operator and set
    inline T *operator->() const
    {
        return mObj;
    }

    inline T *Get() const
    {
        return mObj;
    }

    inline void Set(T *newObj)
    {
        if (newObj == mObj) {
            return;
        }

        if (newObj != nullptr) {
            newObj->IncreaseRef();
        }

        if (mObj != nullptr) {
            mObj->DecreaseRef();
        }

        mObj = newObj;
    }

    template <typename C> C *ToChild()
    {
        if (mObj != nullptr) {
            return dynamic_cast<C *>(mObj);
        }
        return nullptr;
    }

private:
    T *mObj = nullptr;
};

}
}

#endif // OCK_HCOM_CPP_REF_H
