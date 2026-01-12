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
#ifndef OCK_HCOM_SHM_DATA_CHANNEL_H
#define OCK_HCOM_SHM_DATA_CHANNEL_H

#include "shm_common.h"
#include "shm_handle.h"

namespace ock {
namespace hcom {
/*
 * @brief buck state
 */
enum ShmBuckState : uint8_t {
    HCOM_SHM_FREE = 0,
    HCOM_SHM_OCCUPIED = 1,
};

/*
 * @brief Shm buck meta
 */
struct ShmBuckMeta {
    ShmBuckState state = HCOM_SHM_FREE;

    /*
     * @brief Occupy this buck if its free using CAS
     */
    inline bool OccupyIfFree()
    {
        return __sync_bool_compare_and_swap(&state, HCOM_SHM_FREE, HCOM_SHM_OCCUPIED);
    }

    /*
     * @brief Mark to free if occupied using CAS
     */
    inline bool Free()
    {
        return __sync_bool_compare_and_swap(&state, HCOM_SHM_OCCUPIED, HCOM_SHM_FREE);
    }
} __attribute__((packed));

struct ShmDataChannelOptions {
    uint32_t buckSize = 0;
    int mFd = 0;
    uint16_t buckCount = 0;
    bool isOwner = true;
    uint64_t id = 0;
    char fileName[NN_NO64]{};

    inline bool SetFileName(const std::string &v)
    {
        NN_SET_CHAR_ARRAY_FROM_STRING(fileName, v);
    }

    inline std::string GetFileName() const
    {
        return NN_CHAR_ARRAY_TO_STRING(fileName);
    }

    ShmDataChannelOptions() = default;
    ShmDataChannelOptions(uint64_t i, uint32_t buckS, uint16_t bCnt, bool own)
        : buckSize(buckS), buckCount(bCnt), isOwner(own), id(i)
    {}
    ShmDataChannelOptions(uint64_t i, uint32_t buckS, uint16_t bCnt, int fd, bool own)
        : buckSize(buckS), mFd(fd), buckCount(bCnt), isOwner(own), id(i)
    {}

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "buck-size: " << buckSize << ", buck-count: " << buckCount << ", is-owner: " << isOwner;
        return oss.str();
    }
};

/*
 * @brief Data channel for shm communication
 *
 * 1 hold shm memory, owner hold it; with fixed size of buck
 * 2 get a free buck, for writer
 * 3 mark the buck to free, for read
 */
class ShmDataChannel {
public:
    static inline uint64_t MemSize(uint32_t buckMemSize, uint32_t buckCnt)
    {
        // buckMemSize max value is NET_SGE_MAX_SIZE(900MB), buckCnt max is 65535, will not over 2^64
        return static_cast<uint64_t>(buckMemSize) * buckCnt + buckCnt * sizeof(ShmBuckMeta);
    }

public:
    ShmDataChannel(const std::string &name, const ShmDataChannelOptions &opt,
        UBSHcomNetAtomicState<ShmChannelState> *state)
        : mOptions(opt), mName(name), mState(state)
    {
        OBJ_GC_INCREASE(ShmDataChannel);
    }

    ~ShmDataChannel()
    {
        UnInitialize();
        OBJ_GC_DECREASE(ShmDataChannel);
    }

    const std::string &Filepath() const
    {
        if (mHandle.Get() != nullptr) {
            return mHandle->FullPath();
        }

        return CONST_EMPTY_STRING;
    }

    HResult ValidateOptions()
    {
        if (mName.empty()) {
            NN_LOG_ERROR("Name of shm data channel is empty");
            return SH_PARAM_INVALID;
        }

        if (NN_UNLIKELY(mState == nullptr)) {
            NN_LOG_ERROR("State of shm data state is empty");
            return SH_PARAM_INVALID;
        }

        if (mOptions.buckSize == 0 || mOptions.buckCount == 0) {
            NN_LOG_ERROR("Buck mem size or buck count is 0 for shm data channel " << mName);
            return SH_PARAM_INVALID;
        }

        /* do later check buck size and buck count */

        return SH_OK;
    }

    /*
     * @brief Initialize data channel
     */
    HResult Initialize()
    {
        if (mInited) {
            return SH_OK;
        }

        HResult result = SH_OK;
        if ((result = ValidateOptions()) != SH_OK) {
            return result;
        }

        /* create shm file handle */
        uint64_t desired = MemSize(mOptions.buckSize, mOptions.buckCount);
        std::string fileName = mOptions.isOwner ? SHM_F_DC_PREFIX : mOptions.GetFileName();
        mHandle = new (std::nothrow) ShmHandle(mName, fileName, mOptions.id, desired, mOptions.mFd, mOptions.isOwner);
        if (NN_UNLIKELY(mHandle == nullptr)) {
            NN_LOG_ERROR("Failed to new shm handle for shm data channel " << mName << ", probably out of memory");
            return SH_NEW_OBJECT_FAILED;
        }

        if ((result = mHandle->Initialize()) != SH_OK) {
            return result;
        }

        mBuckBaseAddress = mHandle->ShmAddress() + sizeof(ShmBuckMeta) * mOptions.buckCount;
        mBuckEndAddress = mBuckBaseAddress + static_cast<uint64_t>(mOptions.buckSize) * mOptions.buckCount;
        mMeta = reinterpret_cast<ShmBuckMeta *>(mHandle->ShmAddress());
        if (mOptions.isOwner) {
            for (uint16_t i = 0; i < mOptions.buckCount; ++i) {
                mMeta[i].state = HCOM_SHM_FREE;
            }
        }

        NN_LOG_INFO("Data channel " << mName << " at " << mHandle->FullPath() << " initialized, size meta "
                                    << sizeof(ShmBuckMeta) << " with options " << mOptions.ToString());

        mInited = true;
        return SH_OK;
    }

    void UnInitialize()
    {
        if (!mInited) {
            return;
        }

        mHandle->UnInitialize();

        mInited = false;
    }

    /*
     * @brief Try to occupy one, if one is available then wait some time and try again
     *
     * @param address          [out] the address occupied
     * @param offset           [out] offset to base address
     * @param waitPeriodMs     [in] sleep period in us
     * @param timeoutSecond    [in] timeout in seconds, -1 means wait infinity, 0 don't wait, >0 wait n second
     *
     * @return 0 if occupied
     *
     */
    inline HResult TryOccupyWithWait(
        uintptr_t &address, uint64_t &offset, uint16_t waitPeriodUs = NN_NO100, int32_t timeoutSecond = -1)
    {
        if (NN_UNLIKELY(!mInited)) {
            NN_LOG_ERROR("Failed to occupy one buck from shm data channel " << mName << ", as not initialized");
            return SH_NOT_INITIALIZED;
        }

        const int64_t timeCountUs = static_cast<int64_t>(timeoutSecond) * NN_NO1000 * NN_NO1000;
        int64_t timePassedUs = 0;

        const uint16_t buckCnt = mOptions.buckCount;

        while (true) {
            for (uint16_t i = 0; i < buckCnt; ++i) {
                /* to find */
                if (mMeta[i].OccupyIfFree()) {
                    /* found one and return */
                    offset = mOptions.buckSize * i;
                    address = offset + mBuckBaseAddress;
                    return SH_OK;
                }
            }

            /* check if needing to re-find */
            if (timeCountUs < 0) {
                /* <0 means wait infinity */
                usleep(waitPeriodUs);
                continue;
            } else if (timeCountUs == 0) {
                /* 0 means don't wait */
                return SH_TIME_OUT;
            }

            /* > 0 means wait sometime */
            if (timePassedUs >= timeCountUs) {
                return SH_TIME_OUT;
            }

            if (NN_UNLIKELY(mState->Compare(CH_BROKEN))) {
                NN_LOG_ERROR("Failed to occupy one buck from shm data channel " << mName << ", as ch state is broken");
                return SH_CH_BROKEN;
            }

            usleep(waitPeriodUs);
            timePassedUs += waitPeriodUs;
        }
    }

    /*
     * @brief Try to mark to free
     *
     * @param address          [in] the address to be marked
     *
     * @return 0 if marked
     */
    inline void MarkFree(uintptr_t address)
    {
        if (NN_UNLIKELY(!mInited)) {
            NN_LOG_WARN("Unable to mark one buck free from shm data channel " << mName << " as not initialized");
            return;
        }

        if (NN_UNLIKELY(address >= mBuckEndAddress || address < mBuckBaseAddress)) {
            NN_LOG_WARN("Unable to mark one buck free from shm data channel " << mName << " as address is invalid");
            return;
        }

        uint64_t tmpIndex = (address - mBuckBaseAddress) / mOptions.buckSize;
        if ((tmpIndex * mOptions.buckSize + mBuckBaseAddress) != address) {
            NN_LOG_WARN("Unable to mark one buck free from shm data channel " << mName << " as address is invalid");
            return;
        }

        auto &stateData = mMeta[tmpIndex];
        if (NN_UNLIKELY(!stateData.Free())) {
            NN_LOG_WARN("Unable to mark free as is not occupied in shm data channel " << mName);
            return;
        }
    }

    inline uint32_t BuckSize() const
    {
        return mOptions.buckSize;
    }

    inline uint32_t BuckCount() const
    {
        return mOptions.buckCount;
    }

    inline HResult GetAddressByOffset(uint64_t offset, uintptr_t &address) const
    {
        if (NN_UNLIKELY(!mInited)) {
            NN_LOG_ERROR("Failed to translate address by shm data channel " << mName << " as not initialized");
            return SH_NOT_INITIALIZED;
        }

        uint64_t tmpIndex = offset / mOptions.buckSize;
        if (tmpIndex >= mOptions.buckCount) {
            NN_LOG_ERROR("Failed to translate address by shm data channel " << mName << " as address is invalid");
            return SH_PARAM_INVALID;
        }

        address = mBuckBaseAddress + offset;

        if ((tmpIndex * mOptions.buckSize + mBuckBaseAddress) != address) {
            NN_LOG_ERROR("Failed to translate address by shm data channel " << mName << " as address is invalid");
            return SH_PARAM_INVALID;
        }

        return SH_OK;
    }

    inline ShmHandlePtr &GetShmHandle()
    {
        return mHandle;
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    /* put the hot variables at head part of this class */
    ShmBuckMeta *mMeta = nullptr;
    uintptr_t mBuckBaseAddress = 0;
    uintptr_t mBuckEndAddress = 0;
    ShmHandlePtr mHandle = nullptr;
    bool mInited = false;

    DEFINE_RDMA_REF_COUNT_VARIABLE;

    ShmDataChannelOptions mOptions{};

    std::string mName;
    UBSHcomNetAtomicState<ShmChannelState> *mState = nullptr;
};
}  // namespace hcom
}  // namespace ock
#endif  // OCK_HCOM_SHM_DATA_CHANNEL_H
