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
#ifndef HCOM_SHM_HANDLE_H
#define HCOM_SHM_HANDLE_H

#include <sys/file.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/version.h>
#include "net_common.h"
#include "shm_common.h"

namespace ock {
namespace hcom {
class ShmHandle {
public:
    ShmHandle(const std::string &name, const std::string &filePrefix, uint64_t id, uint64_t dataSize, bool isOwner)
        : mName(name), mFilePrefix(filePrefix), mId(id), mDataSize(dataSize), mIsOwner(isOwner)
    {
        OBJ_GC_INCREASE(ShmHandle);
    }

    ShmHandle(const std::string &name, const std::string &filePrefix, uint64_t id, uint64_t dataSize, int fd,
        bool isOwner)
        : mName(name), mFilePrefix(filePrefix), mId(id), mDataSize(dataSize), mFd(fd), mIsOwner(isOwner)
    {
        OBJ_GC_INCREASE(ShmHandle);
    }

    ~ShmHandle()
    {
        UnInitialize();
        OBJ_GC_DECREASE(ShmHandle);
    }

    HResult Initialize()
    {
        if (mInited) {
            return SH_OK;
        }

        if (mFilePrefix.empty()) {
            NN_LOG_ERROR("File prefix is empty in shm handle " << mName);
            return SH_PARAM_INVALID;
        }

        int32_t pid = getpid();
        if (pid < 0) {
            NN_LOG_ERROR("Get PID is incorrect in shm handle " << mName);
            return SH_ERROR;
        }
        mPId = mIsOwner ? static_cast<uint32_t>(pid) : 0;

        /* get file name */
        mFullPath = mIsOwner ? GetFileName() : mFilePrefix;

        if (mIsOwner) {
            /* create file */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
            auto tmpFd = shm_open(mFullPath.c_str(), O_CREAT | O_RDWR | O_EXCL | O_CLOEXEC, mPermission);
#else
            int tmpFd = syscall(SYS_memfd_create, mFullPath.c_str(), 0);
#endif
            if (tmpFd < 0) {
                char buf[NET_STR_ERROR_BUF_SIZE] = {0};
                NN_LOG_ERROR("Failed to create shm file for " << mName << ", error "
                        << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE) <<
                    ", please check if fd is out of limit");
                return SH_FILE_OP_FAILED;
            }
            /* truncate */
            if (ftruncate(tmpFd, mDataSize) != 0) {
                NetFunc::NN_SafeCloseFd(tmpFd);
                char buf[NET_STR_ERROR_BUF_SIZE] = {0};
                NN_LOG_ERROR("Failed to truncate file for " << mName << ", error "
                        << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
                return SH_FILE_OP_FAILED;
            }
            mFd = tmpFd;
        }

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
        /* lock file Make other processes aware that the file is in use */
        if (NN_UNLIKELY(flock(mFd, LOCK_EX | LOCK_NB) != 0)) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to lock file for " << mName << ", error "
                    << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
            NetFunc::NN_SafeCloseFd(mFd);
            return SH_FILE_OP_FAILED;
        }
#endif

        /* mmap */
        auto mappedAddress = mmap(nullptr, mDataSize, PROT_READ | PROT_WRITE, MAP_SHARED, mFd, 0);
        if (mappedAddress == MAP_FAILED) {
            NetFunc::NN_SafeCloseFd(mFd);
            
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to mmap file for " << mName << ", error "
                    << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
            return SH_FILE_OP_FAILED;
        }

        /* owner set 1B per 4K, make sure physical page */
        if (mIsOwner) {
            auto pos = reinterpret_cast<uint8_t *>(mappedAddress);
            uint64_t setLength = 0;
            // if directly *pos=0 may be call bus error
            uint8_t zero = 0;
            while (setLength < mDataSize) {
                *pos = zero;
                setLength += NN_NO4096;
                pos += NN_NO4096;
            }

            pos = reinterpret_cast<uint8_t *>(mappedAddress) + (mDataSize - NN_NO1);
            *pos = zero;
        }

        mAddress = reinterpret_cast<uintptr_t>(mappedAddress);
        mInited = true;
        return SH_OK;
    }

    void UnInitialize()
    {
        if (!mInited) {
            return;
        }

        if (munmap(reinterpret_cast<void *>(mAddress), mDataSize) != 0) {
            NN_LOG_ERROR("Failed to munmap address in shm handle " << mName);
        }
        
        NetFunc::NN_SafeCloseFd(mFd);

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
        if (mIsOwner && shm_unlink(mFullPath.c_str()) != 0) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to remove file for " << mName << " error "
                    << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        }
#endif
        mInited = false;
    }

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "name: " << mName << ", id: " << mId << ", data-size: " << mDataSize << ", address: " << mAddress <<
            ", fd: " << mFd << ", is-owner: " << mIsOwner << ", inited: " << mInited << ", full-path: " << mFullPath;
        return oss.str();
    }

    inline uint64_t Id() const
    {
        return mId;
    }

    inline uint64_t DataSize() const
    {
        return mDataSize;
    }

    inline uintptr_t ShmAddress() const
    {
        return mAddress;
    }

    inline bool IsOwner() const
    {
        return mIsOwner;
    }

    inline int Fd() const
    {
        return mFd;
    }

    inline const std::string &FullPath() const
    {
        return mFullPath;
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    std::string GetFileName() const
    {
        std::string filePath = mFilePrefix + "-" + std::to_string(mPId) + "-" + std::to_string(mId);
        return filePath;
    }

private:
    std::string mFullPath;
    std::string mName;
    std::string mFilePrefix;
    uint64_t mId = 0;
    uint64_t mDataSize = 0;
    uintptr_t mAddress = 0;
    int mFd = -1;
    int mPermission = NN_NO400;
    uint32_t mPId = 0;

    bool mIsOwner = true;
    bool mInited = false;

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};
}
}

#endif // HCOM_SHM_HANDLE_H
