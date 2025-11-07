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
#ifndef OCK_HCOM_SOCK_VALIDATION_H
#define OCK_HCOM_SOCK_VALIDATION_H
#ifdef SOCK_BUILD_ENABLED

#include "net_monotonic.h"
#include "net_sock_common.h"
#include "sock_common.h"

namespace ock {
namespace hcom {
#define OPCODE_VALIDATION()                                                                               \
    do {                                                                                                  \
        if (NN_UNLIKELY(opCode >= MAX_OPCODE)) {                                                          \
            NN_LOG_ERROR("Failed to post message as opcode is invalid, which should with the range 0~" << \
                (MAX_OPCODE - 1));                                                                        \
            return NN_INVALID_OPCODE;                                                                     \
        }                                                                                                 \
    } while (0)

#define REQ_SIZE_VALIDATION()                                                                           \
    do {                                                                                                \
        if (NN_UNLIKELY(request.size > mAllowedSize)) {                                                 \
            NN_LOG_ERROR("Failed to post message in sock as size " << request.size << " is too large"); \
            return NN_INVALID_PARAM;                                                                    \
        }                                                                                               \
    } while (0)

#define REQ_SIZE_VALIDATION_ZERO_COPY()                                                                 \
    do {                                                                                                \
        if (NN_UNLIKELY(request.size > NET_SGE_MAX_SIZE)) {                                             \
            NN_LOG_ERROR("Failed to post message in sock as size " << request.size << " is too large"); \
            return NN_INVALID_PARAM;                                                                    \
        }                                                                                               \
    } while (0)

static __always_inline NResult StateValidation(UBSHcomNetAtomicState<UBSHcomNetEndPointState> &state, uint64_t id,
    NetDriverSockWithOOB *driver, Sock *sock)
{
    if (NN_UNLIKELY(!state.Compare(NEP_ESTABLISHED))) {
        NN_LOG_ERROR("Endpoint " << id << " is not established, state is " << UBSHcomNEPStateToString(state.Get()));
        return NN_EP_NOT_ESTABLISHED;
    }

    if (NN_UNLIKELY(sock == nullptr || driver == nullptr)) {
        NN_LOG_ERROR("Invalid endpoint");
        return NN_ERROR;
    }
    return NN_OK;
}

static __always_inline NResult BuffValidation(const UBSHcomNetTransRequest &request)
{
    if (NN_UNLIKELY(request.upCtxSize > sizeof(SockOpContextInfo::upCtx))) {
        NN_LOG_ERROR("Failed to post message as upCtxSize > " << sizeof(SockOpContextInfo::upCtx));
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(request.lAddress == 0 || request.size == 0)) {
        NN_LOG_ERROR("Failed to post message as source data is null or size is zero");
        return NN_INVALID_PARAM;
    }
    return NN_OK;
}

static __always_inline NResult TwoSideSglValidation(const UBSHcomNetTransSglRequest &request,
    NetDriverSockWithOOB *driver, uint32_t segSize, size_t &totalSize)
{
    if (NN_UNLIKELY(request.upCtxSize > sizeof(SockOpContextInfo::upCtx))) {
        NN_LOG_ERROR("Sock failed to post message as upCtxSize > " << sizeof(SockOpContextInfo::upCtx));
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(request.iov == nullptr || request.iovCount == 0 || request.iovCount > NET_SGE_MAX_IOV)) {
        NN_LOG_ERROR("Sock failed to post message as iov is null or cnt is invalid " << request.iovCount);
        return NN_INVALID_PARAM;
    }

    for (uint16_t i = 0; i < request.iovCount; i++) {
        if (NN_OK != driver->ValidateMemoryRegion(request.iov[i].lKey, request.iov[i].lAddress, request.iov[i].size)) {
            NN_LOG_ERROR("Sock invalid MemoryRegion or lKey in iov of sgl request");
            return NN_INVALID_LKEY;
        }
        totalSize += request.iov[i].size;
    }

    if (NN_UNLIKELY(totalSize < NN_NO1 || totalSize > segSize)) {
        NN_LOG_ERROR("Sock Failed to post raw sgl message as size " << totalSize <<
            " is too large, use one side instead");
        return NN_INVALID_PARAM;
    }
    return NN_OK;
}

static __always_inline NResult OneSideValidation(const UBSHcomNetTransRequest &request, NetDriverSockWithOOB *driver)
{
    if (NN_UNLIKELY(request.upCtxSize > sizeof(SockOpContextInfo::upCtx))) {
        NN_LOG_ERROR("Failed to post message as upCtxSize > " << sizeof(SockOpContextInfo::upCtx));
        return NN_INVALID_PARAM;
    }

    if (NN_OK != driver->ValidateMemoryRegion(request.lKey, request.lAddress, request.size)) {
        NN_LOG_ERROR("Invalid MemoryRegion or lKey in request");
        return NN_INVALID_LKEY;
    }
    return NN_OK;
}

static __always_inline NResult OneSideSglValidation(const UBSHcomNetTransSglRequest &request,
    NetDriverSockWithOOB *driver, size_t &totalSize)
{
    if (NN_UNLIKELY(request.upCtxSize > sizeof(SockOpContextInfo::upCtx))) {
        NN_LOG_ERROR("Failed to post message as upCtxSize > " << sizeof(SockOpContextInfo::upCtx));
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(request.iov == nullptr || request.iovCount == 0 || request.iovCount > NET_SGE_MAX_IOV)) {
        NN_LOG_ERROR("Failed to post message as iov is null or cnt is invalid " << request.iovCount);
        return NN_INVALID_PARAM;
    }

    for (uint16_t i = 0; i < request.iovCount; i++) {
        if (NN_UNLIKELY(NN_OK != driver->ValidateMemoryRegion(request.iov[i].lKey, request.iov[i].lAddress,
            request.iov[i].size))) {
            NN_LOG_ERROR("Invalid MemoryRegion or lKey in iov of sgl request");
            return NN_INVALID_LKEY;
        }
        totalSize += request.iov[i].size;
    }
    return NN_OK;
}
}
}
#endif
#endif // OCK_HCOM_SOCK_VALIDATION_H
