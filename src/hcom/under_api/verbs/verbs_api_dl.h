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
#ifndef HCOM_DYLOADER_IVERBS_H
#define HCOM_DYLOADER_IVERBS_H
#ifdef RDMA_BUILD_ENABLED

#include <errno.h>
#include <infiniband/verbs.h>
#include <linux/types.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <cstring>

#include "hcom_def.h"

#define IVERBS_SO_PATH "libibverbs.so"

using IBV_GET_DEVICE_LIST = struct ibv_device **(*)(int *num_devices);
using IBV_FORK_INIT = int (*)();
using IBV_OPEN_DEVICE = struct ibv_context *(*)(struct ibv_device *device);
using IBV_ALLOC_PD = struct ibv_pd *(*)(struct ibv_context *context);
using IBV_QUERY_PORT = int (*)(struct ibv_context *context, uint8_t port_num, struct _compat_ibv_port_attr *port_attr);
using IBV_FREE_DEVICE_LIST = void (*)(struct ibv_device **list);
using IBV_REG_MR = struct ibv_mr *(*)(struct ibv_pd *pd, void *addr, size_t length, int access);
using IBV_CREATE_COMP_CHANNEL = struct ibv_comp_channel *(*)(struct ibv_context *context);
using IBV_GET_CQ_EVENT = int (*)(struct ibv_comp_channel *channel, struct ibv_cq **cq, void **cq_context);
using IBV_GET_ASYNC_EVENT = int (*)(struct ibv_context *context, struct ibv_async_event *event);
using IBV_ACK_ASYNC_EVENT = void (*)(struct ibv_async_event *event);
using IBV_CREATE_QP = struct ibv_qp *(*)(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr);
using IBV_CLOSE_DEVICE = int (*)(struct ibv_context *context);
using IBV_DEALLOC_PD = int (*)(struct ibv_pd *pd);
using IBV_CREATE_CQ = struct ibv_cq *(*)(struct ibv_context *context, int cqe, void *cq_context,
    struct ibv_comp_channel *channel, int comp_vector);
using IBV_DESTROY_COMP_CHANNEL = int (*)(struct ibv_comp_channel *channel);
using IBV_DESTROY_CQ = int (*)(struct ibv_cq *cq);
using IBV_ACK_CQ_EVENTS = void (*)(struct ibv_cq *cq, unsigned int nevents);
using IBV_DESTROY_QP = int (*)(struct ibv_qp *qp);
using IBV_MODIFY_QP = int (*)(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask);
using IBV_DEREG_MR = int (*)(struct ibv_mr *mr);
using IBV_QUERY_GID = int (*)(struct ibv_context *context, uint8_t port_num, int index, union ibv_gid *gid);
using IBV_QUERY_DEVICE = int (*)(struct ibv_context *context, struct ibv_device_attr *device_attr);
using IBV_REG_MR_IOVA2 = struct ibv_mr *(*)(struct ibv_pd *pd, void *addr, size_t length, uint64_t iova,
    unsigned int access);
using IBV_PORT_STATE_STR = const char *(*)(enum ibv_port_state port_state);

class VerbsAPI {
public:
    static IBV_GET_DEVICE_LIST hcomInnerIbvGetDevList;
    static IBV_FORK_INIT hcomInnerIbvForkInit;
    static IBV_QUERY_PORT hcomInnerIbvQueryPort;
    static IBV_OPEN_DEVICE hcomInnerIbvOpenDevice;
    static IBV_ALLOC_PD hcomInnerIbvAllocPD;
    static IBV_FREE_DEVICE_LIST hcomInnerIbvFreeDevList;
    static IBV_CREATE_COMP_CHANNEL hcomInnerIbvCreateCompChannel;
    static IBV_GET_CQ_EVENT hcomInnerIbvGetCQEvent;
    static IBV_GET_ASYNC_EVENT hcomInnerIbvGetAsyncEvent;
    static IBV_ACK_ASYNC_EVENT hcomInnerIbvAckAsyncEvent;
    static IBV_CREATE_QP hcomInnerIbvCreateQP;
    static IBV_CLOSE_DEVICE hcomInnerIbvCloseDev;
    static IBV_DEALLOC_PD hcomInnerIbvDeallocPD;
    static IBV_CREATE_CQ hcomInnerCreateCQ;
    static IBV_DESTROY_COMP_CHANNEL hcomInnerDestroyCompChannel;
    static IBV_DESTROY_CQ hcomInnerDestroyCQ;
    static IBV_ACK_CQ_EVENTS hcomInnerAckCQ;
    static IBV_DESTROY_QP hcomInnerDestroyQP;
    static IBV_MODIFY_QP hcomInnerModityQP;
    static IBV_DEREG_MR hcomInnerDeregMr;
    static IBV_QUERY_GID hcomInnerQueryGid;
    static IBV_QUERY_DEVICE hcomInnerQueryDevice;
    static IBV_REG_MR_IOVA2 hcomInnerRegMrIOVA2;
    static IBV_REG_MR hcomInnerRegMr;
    static IBV_PORT_STATE_STR hcomInnerPortStateStr;

#if defined(TEST_LLT) && defined(MOCK_VERBS)
    static int LoadFakeVerbsAPI();
#else
    static int LoadVerbsAPI();
#endif

private:
    static bool gLoaded;
};

#ifndef IBV_ACCESS_OPTIONAL_RANGE
#define IBV_ACCESS_OPTIONAL_RANGE 0
#endif

#define HCOM_IBV_REG_MR(pd, addr, length, access, is_access_const)                                    \
    ({                                                                                                \
        struct ibv_mr *ret;                                                                           \
        auto noIova2 = VerbsAPI::hcomInnerRegMrIOVA2 == nullptr;                                      \
        if (((is_access_const) && ((access)&IBV_ACCESS_OPTIONAL_RANGE) == 0) || noIova2) {            \
            ret = VerbsAPI::hcomInnerRegMr((pd), (addr), (length), (access));                         \
        } else {                                                                                      \
            ret = VerbsAPI::hcomInnerRegMrIOVA2((pd), (addr), (length), (uintptr_t)(addr), (access)); \
        }                                                                                             \
        ret;                                                                                          \
    })

#define HCOM_IBV_INNER_REG_MR(pd, addr, length, access) \
    HCOM_IBV_REG_MR(pd, addr, length, access, __builtin_constant_p(((access)&IBV_ACCESS_OPTIONAL_RANGE) == 0))

#ifndef verbs_get_ctx_op
#define HCOM_IBV_INNER_QUERY_PORT(context, port_num, port_attr)           \
    ({                                                                    \
        int rc;                                                           \
        bzero((port_attr), sizeof(*(port_attr)));                         \
        rc = VerbsAPI::hcomInnerIbvQueryPort(context, port_num,           \
            reinterpret_cast<struct _compat_ibv_port_attr *>(port_attr)); \
        rc;                                                               \
    })
#else
#define HCOM_IBV_INNER_QUERY_PORT(context, port_num, port_attr)                        \
    ({                                                                                 \
        struct verbs_context *vctx = verbs_get_ctx_op(context, query_port);            \
        int rc;                                                                        \
        if (!vctx) {                                                                   \
            bzero((port_attr), sizeof(*(port_attr)));                                  \
            rc = VerbsAPI::hcomInnerIbvQueryPort(context, port_num,                    \
                reinterpret_cast<struct _compat_ibv_port_attr *>(port_attr));          \
        } else {                                                                       \
            rc = vctx->query_port(context, port_num, port_attr, sizeof(*(port_attr))); \
        }                                                                              \
        rc;                                                                            \
    })
#endif
#endif
#endif // HCOM_DYLOADER_IVERBS_H