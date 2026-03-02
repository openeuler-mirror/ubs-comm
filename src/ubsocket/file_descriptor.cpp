/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: Provide the utility for umq buffer, iov, etc
 * Author:
 * Create: 2025-07-31
 * Note:
 * History: 2025-07-31
*/

#include "file_descriptor.h"

template <>
SocketFd *Fd<SocketFd>::m_fd_obj_map[RPC_ADPT_FD_MAX] = {0};
template <>
pthread_rwlock_t Fd<SocketFd>::m_rwlock = PTHREAD_RWLOCK_INITIALIZER;

template <>
EpollFd *Fd<EpollFd>::m_fd_obj_map[RPC_ADPT_FD_MAX] = {0};
template <>
pthread_rwlock_t Fd<EpollFd>::m_rwlock = PTHREAD_RWLOCK_INITIALIZER;