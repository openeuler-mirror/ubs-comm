/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UBSOCKET_SOCKET_SET_H
#define UBS_COMM_UBSOCKET_SOCKET_SET_H

#include "ubsocket_common_includes.h"
#include "ubsocket_socket.h"

namespace ock {
namespace ubs {

class SocketSet {
public:
    static SocketSet &Instance()
    {
        static SocketSet instance;
        return instance;
    }

    int Init()
    {
        rwlock_ = LockRegistry::RW_LOCK_OPS.create();
        if (rwlock_ != nullptr) {
            // 初始化数组为 nullptr
            for (int i = 0; i < RPC_ADPT_FD_MAX; i++) {
                socket_obj_[i] = nullptr;
            }
            return 0;
        }
        return -1;
    }

    ALWAYS_INLINE Socket *GetSocket(int fd)
    {
        if (fd < 0 || fd >= RPC_ADPT_FD_MAX) {
            return nullptr;
        }
        return socket_obj_[fd];
    }

    Socket *GetSocketLocked(int fd)
    {
        ReadLocker lock(rwlock_);
        return GetSocket(fd);
    }

    Socket *OverrideSocket(int fd, Socket *new_socket)
    {
        if (fd < 0 || fd >= RPC_ADPT_FD_MAX) {
            return nullptr;
        }
        if (new_socket != nullptr) {
            new_socket->IncreaseRef();
        }

        Socket *old_socket = nullptr;
        {
            WriteLocker lock(rwlock_);
            old_socket = socket_obj_[fd];
            socket_obj_[fd] = new_socket;
        }

        if (old_socket != nullptr) {
            old_socket->DecreaseRef();
        }
        return old_socket;
    }

    Socket *RemoveSocket(int fd)
    {
        if (fd < 0 || fd >= RPC_ADPT_FD_MAX) {
            return nullptr;
        }
        Socket *socket = nullptr;
        {
            WriteLocker lock(rwlock_);
            socket = socket_obj_[fd];
            socket_obj_[fd] = nullptr;
        }
        return socket;
    }

    void ReleaseAll()
    {
        WriteLocker lock(rwlock_);
        for (int i = 0; i < RPC_ADPT_FD_MAX; i++) {
            if (socket_obj_[i] != nullptr) {
                socket_obj_[i]->DecreaseRef();
                socket_obj_[i] = nullptr;
            }
        }
    }

    void ForEach(const std::function<void(int fd, Socket *)> &callback)
    {
        ReadLocker lock(rwlock_);
        for (int i = 0; i < RPC_ADPT_FD_MAX; i++) {
            if (socket_obj_[i] != nullptr) {
                callback(i, socket_obj_[i]);
            }
        }
    }

    size_t Size()
    {
        ReadLocker lock(rwlock_);
        size_t count = 0;
        for (int i = 0; i < RPC_ADPT_FD_MAX; i++) {
            if (socket_obj_[i] != nullptr) {
                count++;
            }
        }
        return count;
    }

    u_rw_lock_t *GetRWLock()
    {
        return rwlock_;
    }

private:
    SocketSet() = default;

    ~SocketSet()
    {
        ReleaseAll();
        if (rwlock_ != nullptr) {
            LockRegistry::RW_LOCK_OPS.destroy(rwlock_);
            rwlock_ = nullptr;
        }
    }

    SocketSet(const SocketSet &) = delete;
    SocketSet &operator=(const SocketSet &) = delete;

    u_rw_lock_t *rwlock_ = nullptr;
    Socket *socket_obj_[RPC_ADPT_FD_MAX];
};

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_SOCKET_SET_H
