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
#ifndef OCK_HCOM_SHM_HANDLE_FDS_H
#define OCK_HCOM_SHM_HANDLE_FDS_H

#include "shm_common.h"

namespace ock {
namespace hcom {

class ShmHandleFds {
public:
    static HResult SendMsgFds(int udsFd, int fds[], uint32_t len)
    {
        if (NN_UNLIKELY(len != NN_NO4)) {
            NN_LOG_ERROR("Failed to send fds as len of fds should be 4");
            return SH_ERROR;
        }

        // create iov for msg_iov param
        struct iovec iov = {
            .iov_base = &len,
            .iov_len = sizeof(uint32_t)
        };

        uint32_t fdsSize = sizeof(int) * NN_NO4;
        char buf[CMSG_SPACE(fdsSize)];
        bzero(buf, fdsSize);

        struct msghdr msg {};
        msg.msg_iov = &iov;
        msg.msg_control = buf;
        msg.msg_iovlen = 1;
        msg.msg_controllen = sizeof(buf);

        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        if (NN_UNLIKELY(cmsg == nullptr)) {
            NN_LOG_ERROR("CMSG_FIRSTHDR get empty msg");
            return SH_ERROR;
        }
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(fdsSize);

        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<char *>(CMSG_DATA(cmsg)), fdsSize, fds, fdsSize) != SH_OK)) {
            NN_LOG_ERROR("Failed to copy fds to cmsg");
            return SH_PARAM_INVALID;
        }

        auto result = ::sendmsg(udsFd, &msg, 0);
        if (NN_UNLIKELY(result <= 0)) {
            char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to send fds msg to peer result:" << result << ", as errno:" << errno << " error:" <<
                NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
            return SH_ERROR;
        }

        return NN_OK;
    }

    static HResult ReceiveMsgFds(int udsFd, int fds[], uint32_t len)
    {
        if (NN_UNLIKELY(len != NN_NO4)) {
            NN_LOG_ERROR("Failed to receive fds as len of fds should be 4");
            return SH_ERROR;
        }

        // create iov for msg_iov param
        uint32_t recvLen = 0;
        struct iovec iov = {
            .iov_base = &recvLen,
            .iov_len = sizeof(uint32_t)
        };

        uint32_t fdsSize = sizeof(int) * NN_NO4;

        char buf[CMSG_SPACE(fdsSize)];
        bzero(buf, fdsSize);

        struct msghdr msg {};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = buf;
        msg.msg_controllen = sizeof(buf);

        auto result = ::recvmsg(udsFd, &msg, 0);
        if (NN_UNLIKELY((result == 0) && (errno == EXIT_SUCCESS))) {
            NN_LOG_ERROR("Failed to receive fds msg from peer, as channel fd has been destroyed ");
            return SH_PEER_FD_ERROR;
        }

        if (NN_UNLIKELY(result <= 0)) {
            char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to receive fds msg from peer, as errno:" << errno << " error:" <<
                NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
            return SH_ERROR;
        }

        if (NN_UNLIKELY(recvLen != len)) {
            NN_LOG_ERROR("Failed to receive fds as receive Len:" << recvLen << " is not equal to len:" << len);
            return SH_ERROR;
        }

        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        if (NN_UNLIKELY(cmsg == nullptr)) {
            NN_LOG_ERROR("CMSG_FIRSTHDR get empty msg");
            return SH_ERROR;
        }
        if (NN_UNLIKELY(memcpy_s(fds, fdsSize, reinterpret_cast<char *>(CMSG_DATA(cmsg)), fdsSize) != SH_OK)) {
            NN_LOG_ERROR("Failed to copy cmsg to fds");
            return SH_PARAM_INVALID;
        }

        return SH_OK;
    }
};
}
}
#endif