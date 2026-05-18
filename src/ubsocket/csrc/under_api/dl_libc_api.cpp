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
#include "dl_libc_api.h"

namespace ock {
namespace ubs {
DL_API_DEFINE(LibcApi, creat)
DL_API_DEFINE(LibcApi, open)
DL_API_DEFINE(LibcApi, dup)
DL_API_DEFINE(LibcApi, dup2)
DL_API_DEFINE(LibcApi, pipe)
DL_API_DEFINE(LibcApi, socket)
DL_API_DEFINE(LibcApi, socketpair)
DL_API_DEFINE(LibcApi, close)
DL_API_DEFINE(LibcApi, shutdown)
DL_API_DEFINE(LibcApi, accept)
DL_API_DEFINE(LibcApi, accept4)
DL_API_DEFINE(LibcApi, bind)
DL_API_DEFINE(LibcApi, connect)
DL_API_DEFINE(LibcApi, listen)
DL_API_DEFINE(LibcApi, setsockopt)
DL_API_DEFINE(LibcApi, getsockopt)
DL_API_DEFINE(LibcApi, fcntl)
DL_API_DEFINE(LibcApi, fcntl64)
DL_API_DEFINE(LibcApi, ioctl)
DL_API_DEFINE(LibcApi, getsockname)
DL_API_DEFINE(LibcApi, getpeername)
DL_API_DEFINE(LibcApi, read)
DL_API_DEFINE(LibcApi, readv)
DL_API_DEFINE(LibcApi, recv)
DL_API_DEFINE(LibcApi, recvmsg)
DL_API_DEFINE(LibcApi, recvmmsg)
DL_API_DEFINE(LibcApi, recvfrom)
DL_API_DEFINE(LibcApi, write)
DL_API_DEFINE(LibcApi, writev)
DL_API_DEFINE(LibcApi, send)
DL_API_DEFINE(LibcApi, sendmsg)
DL_API_DEFINE(LibcApi, sendmmsg)
DL_API_DEFINE(LibcApi, sendto)
DL_API_DEFINE(LibcApi, sendfile)
DL_API_DEFINE(LibcApi, sendfile64)
DL_API_DEFINE(LibcApi, select)
DL_API_DEFINE(LibcApi, pselect)
DL_API_DEFINE(LibcApi, poll)
DL_API_DEFINE(LibcApi, ppoll)
DL_API_DEFINE(LibcApi, epoll_create)
DL_API_DEFINE(LibcApi, epoll_create1)
DL_API_DEFINE(LibcApi, epoll_ctl)
DL_API_DEFINE(LibcApi, epoll_wait)
DL_API_DEFINE(LibcApi, epoll_pwait)
DL_API_DEFINE(LibcApi, fork)
DL_API_DEFINE(LibcApi, vfork)
DL_API_DEFINE(LibcApi, daemon)
DL_API_DEFINE(LibcApi, sigaction)
DL_API_DEFINE(LibcApi, signal)

std::mutex LibcApi::LOAD_MUTEX;
bool LibcApi::LOADED = false;

Result LibcApi::Load() noexcept
{
    UBS_VLOG_DEBUG("enter");

    std::lock_guard<std::mutex> guard(LOAD_MUTEX);
    if (LOADED) {
        return UBS_OK;
    }

    UBS_VLOG_DEBUG("start to load symbal");

    /* step1: open library file */
    void *handle = dlopen("libc.so.6", RTLD_NOW);
    if (handle == nullptr) {
        UBS_VLOG_ERR("Open libc.so.6 failed, error: %s\n", Func::Error2Str(errno));
        return UBS_DL_OPEN_LIB_FAILED;
    }

    /* step2: load functions */
    DL_API_LOAD(creat);
    DL_API_LOAD(open);
    DL_API_LOAD(dup);
    DL_API_LOAD(dup2);
    DL_API_LOAD(pipe);
    DL_API_LOAD(socket);
    DL_API_LOAD(socketpair);
    DL_API_LOAD(close);
    DL_API_LOAD(shutdown);
    DL_API_LOAD(accept);
    DL_API_LOAD(accept4);
    DL_API_LOAD(bind);
    DL_API_LOAD(connect);
    DL_API_LOAD(listen);
    DL_API_LOAD(setsockopt);
    DL_API_LOAD(getsockopt);
    DL_API_LOAD(fcntl);
    DL_API_LOAD(fcntl64);
    DL_API_LOAD(ioctl);
    DL_API_LOAD(getsockname);
    DL_API_LOAD(getpeername);
    DL_API_LOAD(read);
    DL_API_LOAD(readv);
    DL_API_LOAD(recv);
    DL_API_LOAD(recvmsg);
    DL_API_LOAD(recvmmsg);
    DL_API_LOAD(recvfrom);
    DL_API_LOAD(write);
    DL_API_LOAD(writev);
    DL_API_LOAD(send);
    DL_API_LOAD(sendmsg);
    DL_API_LOAD(sendmmsg);
    DL_API_LOAD(sendto);
    DL_API_LOAD(sendfile);
    DL_API_LOAD(sendfile64);
    DL_API_LOAD(select);
    DL_API_LOAD(pselect);
    DL_API_LOAD(poll);
    DL_API_LOAD(ppoll);
    DL_API_LOAD(epoll_create);
    DL_API_LOAD(epoll_create1);
    DL_API_LOAD(epoll_ctl);
    DL_API_LOAD(epoll_wait);
    DL_API_LOAD(epoll_pwait);
    DL_API_LOAD(fork);
    DL_API_LOAD(vfork);
    DL_API_LOAD(daemon);
    DL_API_LOAD(sigaction);
    DL_API_LOAD(signal);

    /* step3: close handle */
    dlclose(handle);

    /* step4: set loaded */
    LOADED = true;

    UBS_VLOG_DEBUG("load symbal loaded");
    return UBS_OK;
}

void LibcApi::UnLoad() noexcept
{
    std::lock_guard<std::mutex> guard(LOAD_MUTEX);
    if (!LOADED) {
        return;
    }

    UnLoadInner();
    LOADED = false;
}

void LibcApi::UnLoadInner() noexcept
{
    UBS_VLOG_DEBUG("unload symbal");
    DL_API_SET_NULL(creat)
    DL_API_SET_NULL(open)
    DL_API_SET_NULL(dup)
    DL_API_SET_NULL(dup2)
    DL_API_SET_NULL(pipe)
    DL_API_SET_NULL(socket)
    DL_API_SET_NULL(socketpair)
    DL_API_SET_NULL(close)
    DL_API_SET_NULL(shutdown)
    DL_API_SET_NULL(accept)
    DL_API_SET_NULL(accept4)
    DL_API_SET_NULL(bind)
    DL_API_SET_NULL(connect)
    DL_API_SET_NULL(listen)
    DL_API_SET_NULL(setsockopt)
    DL_API_SET_NULL(getsockopt)
    DL_API_SET_NULL(fcntl)
    DL_API_SET_NULL(fcntl64)
    DL_API_SET_NULL(ioctl)
    DL_API_SET_NULL(getsockname)
    DL_API_SET_NULL(getpeername)
    DL_API_SET_NULL(read)
    DL_API_SET_NULL(readv)
    DL_API_SET_NULL(recv)
    DL_API_SET_NULL(recvmsg)
    DL_API_SET_NULL(recvmmsg)
    DL_API_SET_NULL(recvfrom)
    DL_API_SET_NULL(write)
    DL_API_SET_NULL(writev)
    DL_API_SET_NULL(send)
    DL_API_SET_NULL(sendmsg)
    DL_API_SET_NULL(sendmmsg)
    DL_API_SET_NULL(sendto)
    DL_API_SET_NULL(sendfile)
    DL_API_SET_NULL(sendfile64)
    DL_API_SET_NULL(select)
    DL_API_SET_NULL(pselect)
    DL_API_SET_NULL(poll)
    DL_API_SET_NULL(ppoll)
    DL_API_SET_NULL(epoll_create)
    DL_API_SET_NULL(epoll_create1)
    DL_API_SET_NULL(epoll_ctl)
    DL_API_SET_NULL(epoll_wait)
    DL_API_SET_NULL(epoll_pwait)
    DL_API_SET_NULL(fork)
    DL_API_SET_NULL(vfork)
    DL_API_SET_NULL(daemon)
    DL_API_SET_NULL(sigaction)
    DL_API_SET_NULL(signal)

    UBS_VLOG_DEBUG("unload symbal finished");
}
} // namespace ubs
} // namespace ock