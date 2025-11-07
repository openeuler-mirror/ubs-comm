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
#ifndef HCOM_VERBS_API_WRAPPER_H
#define HCOM_VERBS_API_WRAPPER_H
#ifdef RDMA_BUILD_ENABLED

#include "verbs_api_dl.h"

namespace ock {
namespace hcom {
class HcomIbv {
public:
    static inline struct ibv_device **GetDevList(int *num_devices)
    {
        return VerbsAPI::hcomInnerIbvGetDevList(num_devices);
    }

    static inline int ForkInit()
    {
        return VerbsAPI::hcomInnerIbvForkInit();
    }

    static inline struct ibv_context *OpenDevice(struct ibv_device *device)
    {
        return VerbsAPI::hcomInnerIbvOpenDevice(device);
    }

    static inline struct ibv_pd *AllocPd(struct ibv_context *context)
    {
        return VerbsAPI::hcomInnerIbvAllocPD(context);
    }

    static inline int QueryPort(struct ibv_context *context, uint8_t port_num, struct ibv_port_attr *port_attr)
    {
        return HCOM_IBV_INNER_QUERY_PORT(context, port_num, port_attr);
    }

    static inline void FreeDevList(struct ibv_device **list)
    {
        VerbsAPI::hcomInnerIbvFreeDevList(list);
    }

    static inline struct ibv_mr *RegMr(struct ibv_pd *pd, void *addr, size_t length, unsigned int access)
    {
        return HCOM_IBV_INNER_REG_MR(pd, addr, length, access);
    }

    static inline struct ibv_comp_channel *CreateCompChannel(struct ibv_context *context)
    {
        return VerbsAPI::hcomInnerIbvCreateCompChannel(context);
    }

    static inline int GetCqEvent(struct ibv_comp_channel *channel, struct ibv_cq **cq, void **cq_context)
    {
        return VerbsAPI::hcomInnerIbvGetCQEvent(channel, cq, cq_context);
    }

    static inline int GetAsyncEvent(struct ibv_context *context, struct ibv_async_event *event)
    {
        return VerbsAPI::hcomInnerIbvGetAsyncEvent(context, event);
    }

    static inline void AckAsyncEvent(struct ibv_async_event *event)
    {
        VerbsAPI::hcomInnerIbvAckAsyncEvent(event);
    }

    static inline struct ibv_qp *CreateQp(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr)
    {
        return VerbsAPI::hcomInnerIbvCreateQP(pd, qp_init_attr);
    }

    static inline int CloseDev(struct ibv_context *context)
    {
        return VerbsAPI::hcomInnerIbvCloseDev(context);
    }

    static inline int DeallocPd(struct ibv_pd *pd)
    {
        return VerbsAPI::hcomInnerIbvDeallocPD(pd);
    }

    static inline struct ibv_cq *CreateCq(struct ibv_context *context, int cqe, void *cq_context,
        struct ibv_comp_channel *channel, int comp_vector)
    {
        return VerbsAPI::hcomInnerCreateCQ(context, cqe, cq_context, channel, comp_vector);
    }

    static inline int DestroyCompChannel(struct ibv_comp_channel *channel)
    {
        return VerbsAPI::hcomInnerDestroyCompChannel(channel);
    }
    static inline int DestroyCq(struct ibv_cq *cq)
    {
        return VerbsAPI::hcomInnerDestroyCQ(cq);
    }

    static inline void AckCqEvents(struct ibv_cq *cq, unsigned int nevents)
    {
        VerbsAPI::hcomInnerAckCQ(cq, nevents);
    }
    static inline int DestroyQp(struct ibv_qp *qp)
    {
        return VerbsAPI::hcomInnerDestroyQP(qp);
    }

    static inline int ModifyQp(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask)
    {
        return VerbsAPI::hcomInnerModityQP(qp, attr, attr_mask);
    }

    static inline int DeregMr(struct ibv_mr *mr)
    {
        return VerbsAPI::hcomInnerDeregMr(mr);
    }

    static inline int QueryGid(struct ibv_context *context, uint8_t port_num, int index, union ibv_gid *gid)
    {
        return VerbsAPI::hcomInnerQueryGid(context, port_num, index, gid);
    }

    static inline int QueryDevice(struct ibv_context *context, struct ibv_device_attr *device_attr)
    {
        return VerbsAPI::hcomInnerQueryDevice(context, device_attr);
    }

    static inline const char *PortStateStr(enum ibv_port_state port_state)
    {
        return VerbsAPI::hcomInnerPortStateStr(port_state);
    }

    static inline int Load()
    {
#if !defined(TEST_LLT) || !defined(MOCK_VERBS)
        return VerbsAPI::LoadVerbsAPI();
#else
        return VerbsAPI::LoadFakeVerbsAPI();
#endif
    }
};
}
}

#endif
#endif // HCOM_VERBS_API_WRAPPER_H