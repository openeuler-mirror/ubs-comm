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

#include "fake_ibv.h"
#include <sys/eventfd.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <cstdio>
#include <cstdlib>

#ifdef __cplusplus
extern "C" {
#endif

#define FAKE_FILENAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define FAKE_LOG(fmt, ...)                                                        \
    do {                                                                          \
        char log[512UL];                                                          \
        int len = 0;                                                              \
        len += sprintf(log, "[%s %s:%d]", __FUNCTION__, FAKE_FILENAME, __LINE__); \
        len += sprintf(log + len, fmt, ##__VA_ARGS__);                            \
        sprintf(log + len, "\n");                                                 \
        printf(log);                                                              \
    } while (0)

#define IBV_ERROR (-1)
#define FAKE_NULL_FD (-1)
#define FAKE_NULL_DWORD 0XFFFFFFFF
#ifndef container_of
#define container_of(ptr, type, member) ((type *)(void *)((char *)(ptr) - offsetof(type, member)))
#endif

struct ibv_device g_ibvdevice[FAKE_IBV_DEVICE_NUM] = {
    {
        {NULL, NULL},
        IBV_NODE_RNIC,
        IBV_TRANSPORT_IB,
        "hrn0_0",
        "uverbs0",
        "/tmp/hrn0_0",
        "/tmp/uverbs0"
    },
    {
        {NULL, NULL},
        IBV_NODE_RNIC,
        IBV_TRANSPORT_IB,
        "hrn1_0",
        "uverbs1",
        "/tmp/hrn1_0",
        "/tmp/uverbs1"
    }
};

static uint64_t readBuff[100];
fake_lock_list_t g_f_qp_list;
uint32_t g_fake_qp_num_gen = 0;
uint32_t g_keyId = 0;
/* *********************************************************************
 功能描述  : 获取key号
********************************************************************** */
uint32_t fake_get_key(void)
{
    return (uint32_t)__sync_add_and_fetch(&g_keyId, 1);
}
/* *********************************************************************
 功能描述  : 获取qp链表
********************************************************************** */
fake_lock_list_t *fake_get_qp_list(void)
{
    return &g_f_qp_list;
}

/* *********************************************************************
 功能描述  : 获取qp号
********************************************************************** */
uint32_t fake_get_qp_num(void)
{
    return (uint32_t)__sync_add_and_fetch(&g_fake_qp_num_gen, 1);
}

/* *********************************************************************
 功能描述  : 销毁srq
********************************************************************** */
int ibv_destroy_srq(struct ibv_srq *srq)
{
    if (srq != NULL) {
        free(srq);
    }

    return 0;
}

/* *********************************************************************
 功能描述  : 初始化recv wr mgr

********************************************************************** */
void fake_recv_wr_mgr_init(fake_recv_wr_mgr_t *recv_wr_mgr)
{
    memset(recv_wr_mgr, 0x00, sizeof(fake_recv_wr_mgr_t));
    pthread_mutex_init(&recv_wr_mgr->wrLock, NULL);
}

/* *********************************************************************
 功能描述  : 创建srq
********************************************************************** */
struct ibv_srq *ibv_create_srq(struct ibv_pd *pd, struct ibv_srq_init_attr *attr)
{
    fake_srq_t *fsrq = (fake_srq_t *)malloc(sizeof(fake_srq_t));
    if (fsrq == NULL) {
        return NULL;
    }
    fsrq->srq.pd = pd;
    fsrq->srq.srq_context = attr->srq_context;
    fsrq->srq.context = pd->context;
    fake_recv_wr_mgr_init(&fsrq->recv_wr_mgr);
    return (struct ibv_srq *)fsrq;
}

/* *********************************************************************
 功能描述  : recv wr mgr 产生item
********************************************************************** */
fake_recv_wr_item_t *fake_recv_wr_mgr_produce_item(fake_recv_wr_mgr_t *recv_wr_mgr)
{
    pthread_mutex_lock(&recv_wr_mgr->wrLock);
    if (((recv_wr_mgr->producer + 1) % FAKE_RECV_WR_DEPTH) == recv_wr_mgr->comsuer) {
        FAKE_LOG("Fake: mgr(%p) recv wr queue is empty.", recv_wr_mgr);
        pthread_mutex_unlock(&recv_wr_mgr->wrLock);
        return NULL;
    }

    uint32_t idx = recv_wr_mgr->producer;
    recv_wr_mgr->producer = (recv_wr_mgr->producer + 1) % FAKE_RECV_WR_DEPTH;
    pthread_mutex_unlock(&recv_wr_mgr->wrLock);

    return &recv_wr_mgr->item[idx];
}

/* *********************************************************************
 功能描述  : 从qp中获取一个recv wr
********************************************************************** */
fake_recv_wr_item_t *fake_recv_wr_mgr_comsume_item(fake_recv_wr_mgr_t *recv_wr_mgr)
{
    pthread_mutex_lock(&recv_wr_mgr->wrLock);
    if (recv_wr_mgr->comsuer == recv_wr_mgr->producer) {
        FAKE_LOG("Fake: mgr(%p) recv wr queue is full.", recv_wr_mgr);
        pthread_mutex_unlock(&recv_wr_mgr->wrLock);
        return NULL;
    }

    uint32_t idx = recv_wr_mgr->comsuer++;
    recv_wr_mgr->comsuer %= FAKE_RECV_WR_DEPTH;
    pthread_mutex_unlock(&recv_wr_mgr->wrLock);

    return &recv_wr_mgr->item[idx];
}

/* *********************************************************************
 功能描述  : qp链表初始化
********************************************************************** */
void fake_ibv_init(void)
{
    fake_lock_list_init(&g_f_qp_list);
}

/* *********************************************************************
 功能描述  : fork init桩
********************************************************************** */
int ibv_fork_init(void)
{
    return 0;
}

/* *********************************************************************
 功能描述  : 申请pd
********************************************************************** */
struct ibv_pd *ibv_alloc_pd(struct ibv_context *context)
{
    struct ibv_pd *pd = (struct ibv_pd *)malloc(sizeof(struct ibv_pd));
    if (pd == NULL) {
        return NULL;
    }

    pd->context = context;

    return pd;
}

/* *********************************************************************
 功能描述  : 释放pd
********************************************************************** */
int ibv_dealloc_pd(struct ibv_pd *pd)
{
    free(pd);
    return 0;
}

/* *********************************************************************
 功能描述  : 创建CC
********************************************************************** */
struct ibv_comp_channel *ibv_create_comp_channel(struct ibv_context *context)
{
    fake_cc_t *fcc = (fake_cc_t *)malloc(sizeof(fake_cc_t));
    if (fcc == NULL) {
        return NULL;
    }

    fcc->cc.context = context;
    fcc->cc.fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    fcc->cc.refcnt = 0;

    fake_lock_list_init(&fcc->cq_list);

    return &fcc->cc;
}

/* *********************************************************************
 功能描述  : 释放CC
********************************************************************** */
int ibv_destroy_comp_channel(struct ibv_comp_channel *channel)
{
    fake_cc_t *fcc = (fake_cc_t *)channel;

    if (fcc->cc.fd != FAKE_NULL_FD) {
        close(fcc->cc.fd);
        fcc->cc.fd = FAKE_NULL_FD;
    }

    free(fcc);

    return 0;
}

/* *********************************************************************
 功能描述  : 发送一个完成事件
********************************************************************** */
void fake_send_event_on_cc(struct ibv_comp_channel *cmc)
{
    eventfd_t val = 1; // 任意数据

    (void)eventfd_write(cmc->fd, val);
}

/* *********************************************************************
 功能描述  : 创建一个CQ
********************************************************************** */
struct ibv_cq *ibv_create_cq(struct ibv_context *context, int cqe, void *cq_context, struct ibv_comp_channel *channel,
    int comp_vector)
{
    fake_cq_t *fcq = (fake_cq_t *)malloc(sizeof(fake_cq_t));
    if (fcq == NULL) {
        return NULL;
    }
    if (channel == nullptr) {
        auto cl = ibv_create_comp_channel(context);
        channel = cl;
    }
    struct ibv_cq *cq = &fcq->cq;
    cq->context = context;
    cq->cqe = cqe;
    cq->cq_context = cq_context;
    cq->channel = channel;

    pthread_mutex_init(&fcq->cqLock, NULL);
    fcq->wcq.producer = 0;
    fcq->wcq.comsumer = 0;
    fcq->wcq.buff = (struct ibv_wc *)malloc(cqe * sizeof(struct ibv_wc));
    if (fcq->wcq.buff == NULL) {
        free(fcq);
        return NULL;
    }

    UNREF_PARAM(comp_vector);

    FAKE_LOG("Create cq(%p) num(%d) success.", cq, cqe);

    fake_cc_t *fcc = (fake_cc_t *)channel;
    fake_lock_list_add_tail(&fcq->entry, &fcc->cq_list);

    return cq;
}

/* *********************************************************************
 功能描述  : 生成一个cqe
********************************************************************** */
struct ibv_wc *fake_fetch_cqe(fake_cq_t *fcq)
{
    if (((fcq->wcq.producer + 1) % fcq->cq.cqe) == fcq->wcq.comsumer) {
        return NULL;
    }

    uint32_t idx = fcq->wcq.producer;
    fcq->wcq.producer = (fcq->wcq.producer + 1) % fcq->cq.cqe;

    return &fcq->wcq.buff[idx];
}

/* *********************************************************************
 功能描述  : 产生CQE
********************************************************************** */
int fake_prodce_cqe(fake_cq_t *fcq, struct ibv_wc *wc)
{
    int ret = IBV_ERROR;
    pthread_mutex_lock(&fcq->cqLock);
    struct ibv_wc *next_wc = fake_fetch_cqe(fcq);
    if (next_wc != NULL) {
        *next_wc = *wc;
        ret = 0;
    } else {
        FAKE_LOG("Fake, fetch cqe failed, producer(%u) comsumer(%u).", fcq->wcq.producer, fcq->wcq.comsumer);
    }
    pthread_mutex_unlock(&fcq->cqLock);
    return ret;
}

/* *********************************************************************
 功能描述  : 消费一个cqe
********************************************************************** */
struct ibv_wc *fake_comsume_cqe(fake_cq_t *fcq)
{
    if (fcq->wcq.comsumer == fcq->wcq.producer) {
        return NULL;
    }

    uint32_t idx = fcq->wcq.comsumer++;
    fcq->wcq.comsumer %= fcq->cq.cqe;

    return &fcq->wcq.buff[idx];
}

/* *********************************************************************
 功能描述  : 创建一个CQ
********************************************************************** */
int ibv_destroy_cq(struct ibv_cq *cq)
{
    fake_cq_t *fcq = (fake_cq_t *)(void *)cq;

    fake_cc_t *fcc = (fake_cc_t *)cq->channel;
    fake_lock_list_del_node(&fcq->entry, &fcc->cq_list);

    if (fcq->wcq.buff != NULL) {
        free(fcq->wcq.buff);
        fcq->wcq.buff = NULL;
    }

    free(fcq);

    return 0;
}

/* *********************************************************************
 功能描述  : 创建一个QP
********************************************************************** */
struct ibv_qp *ibv_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr)
{
    fake_qp_t *fqp = (fake_qp_t *)malloc(sizeof(fake_qp_t));
    if (fqp == NULL) {
        return NULL;
    }

    struct ibv_qp *qp = &(fqp->qp);

    qp->context = pd->context;
    qp->qp_context = qp_init_attr->qp_context;
    qp->pd = pd;
    qp->send_cq = qp_init_attr->send_cq;
    qp->recv_cq = qp_init_attr->recv_cq;
    qp->srq = qp_init_attr->srq;
    qp->state = IBV_QPS_RESET;
    qp->qp_type = qp_init_attr->qp_type;
    qp->qp_num = fake_get_qp_num();
    fqp->dest_qp_num = FAKE_NULL_DWORD;
    fake_recv_wr_mgr_init(&fqp->recv_wr_mgr);

    fake_lock_list_add_tail(&fqp->entry, &g_f_qp_list);

    return qp;
}

/* *********************************************************************
 功能描述  : 释放一个QP
********************************************************************** */
int ibv_destroy_qp(struct ibv_qp *qp)
{
    fake_qp_t *fqp = (fake_qp_t *)(void *)qp;
    fake_lock_list_del_node(&fqp->entry, &g_f_qp_list);
    free(fqp);

    return 0;
}

/* *********************************************************************
 功能描述  : 获取事件名字
********************************************************************** */
const char *ibv_event_type_str(enum ibv_event_type event)
{
    static const char *event_type_str[] = {
        [IBV_EVENT_CQ_ERR]        = "CQ error",
        [IBV_EVENT_QP_FATAL]        = "local work queue catastrophic error",
        [IBV_EVENT_QP_REQ_ERR]        = "invalid request local work queue error",
        [IBV_EVENT_QP_ACCESS_ERR]    = "local access violation work queue error",
        [IBV_EVENT_COMM_EST]        = "communication established",
        [IBV_EVENT_SQ_DRAINED]        = "send queue drained",
        [IBV_EVENT_PATH_MIG]        = "path migrated",
        [IBV_EVENT_PATH_MIG_ERR]    = "path migration request error",
        [IBV_EVENT_DEVICE_FATAL]    = "local catastrophic error",
        [IBV_EVENT_PORT_ACTIVE]        = "port active",
        [IBV_EVENT_PORT_ERR]        = "port error",
        [IBV_EVENT_LID_CHANGE]        = "LID change",
        [IBV_EVENT_PKEY_CHANGE]        = "P_Key change",
        [IBV_EVENT_SM_CHANGE]        = "SM change",
        [IBV_EVENT_SRQ_ERR]        = "SRQ catastrophic error",
        [IBV_EVENT_SRQ_LIMIT_REACHED]    = "SRQ limit reached",
        [IBV_EVENT_QP_LAST_WQE_REACHED]    = "last WQE reached",
        [IBV_EVENT_CLIENT_REREGISTER]    = "client reregistration",
        [IBV_EVENT_GID_CHANGE]        = "GID table change"
    };

    if (event < IBV_EVENT_CQ_ERR || event > IBV_EVENT_GID_CHANGE) {
        return "unknown";
    }

    return event_type_str[event];
}

int ibv_get_async_event(struct ibv_context *context, struct ibv_async_event *event)
{
    (void)memset(event, 0, sizeof(struct ibv_async_event));
    FAKE_LOG("fake_ibv: call fake function ibv_get_async_event.");
    UNREF_PARAM(context);

    /* no block mode return -1 will be ingnored */
    return -1;
}

void ibv_ack_async_event(struct ibv_async_event *event)
{
    FAKE_LOG("fake_ibv: call ibv_ack_async_event.");
    UNREF_PARAM(event);
    return;
}

/* *********************************************************************
 功能描述  : 获取cc上有数据的cq
********************************************************************** */
int ibv_get_cq_event(struct ibv_comp_channel *channel, struct ibv_cq **cq, void **cq_context)
{
    list_head_t *node = NULL;
    list_head_t *next = NULL;
    fake_cc_t *fcc = (fake_cc_t *)channel;

    UNREF_PARAM(cq_context);

    *cq = NULL;
    {
        pthread_mutex_lock(&fcc->cq_list.listLock);
        list_for_each_safe(node, next, &fcc->cq_list.list.list_head)
        {
            fake_cq_t *fcq = list_entry(node, fake_cq_t, entry);
            if (fcq->wcq.comsumer != fcq->wcq.producer) {
                *cq = &fcq->cq;
                break;
            }
        }
        pthread_mutex_unlock(&fcc->cq_list.listLock);
    }

    if (*cq != NULL) {
        fake_send_event_on_cc(channel);
    }

    read(channel->fd, readBuff, sizeof(uint64_t) * 100);

    return 0;
}

/* *********************************************************************
 功能描述  : ack events
********************************************************************** */
void ibv_ack_cq_events(struct ibv_cq *cq, unsigned int nevents)
{
    UNREF_PARAM(cq);
    UNREF_PARAM(nevents);
    return;
}

struct ibv_device **ibv_get_device_list(int *device_num)
{
    struct ibv_device **l;
    int i;

    l = (struct ibv_device **)calloc(FAKE_IBV_DEVICE_NUM + 1, sizeof(struct ibv_device *));
    if (!l) {
        FAKE_LOG("fake_ibv: create device list fail.");
        return NULL;
    }

    for (i = 0; i < FAKE_IBV_DEVICE_NUM; ++i) {
        l[i] = &g_ibvdevice[i];
    }

    *device_num = FAKE_IBV_DEVICE_NUM;

    return l;
}

void ibv_free_device_list(struct ibv_device **list)
{
    free(list);

    return;
}

/* *********************************************************************
 功能描述  : 将所有的recv wr全部放入cq中
********************************************************************** */
void fake_flash_all_recv_wr(fake_qp_t *fqp)
{
    uint32_t total = 0;
    fake_cq_t *fcq = (fake_cq_t *)fqp->qp.recv_cq;
    fake_recv_wr_item_t *item = fake_recv_wr_mgr_comsume_item(&fqp->recv_wr_mgr);
    struct ibv_wc wc = {};

    while (item != NULL) {
        wc.wr_id = item->wr.wr_id;
        wc.status = IBV_WC_WR_FLUSH_ERR;
        wc.opcode = IBV_WC_RECV;
        wc.qp_num = fqp->qp.qp_num;
        if (fake_prodce_cqe(fcq, &wc) != 0) {
            break;
        }

        item = fake_recv_wr_mgr_comsume_item(&fqp->recv_wr_mgr);

        total++;
    }

    fake_send_event_on_cc(fcq->cq.channel);

    FAKE_LOG("Recv_wr_mgr(%p) Qp(%p) cq(%p) flash to wr(%d) to cq(%u-%u).", &fqp->recv_wr_mgr, fqp, fcq, total,
        fcq->wcq.comsumer, fcq->wcq.producer);
}

/* *********************************************************************
 功能描述  : 改变qp
********************************************************************** */
int ibv_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask)
{
    fake_qp_t *fqp = (fake_qp_t *)qp;

    qp->state = attr->qp_state;

    if (attr->qp_state == IBV_QPS_RTR) {
        fqp->dest_qp_num = attr->dest_qp_num;
    }

    if (attr->qp_state == IBV_QPS_ERR) {
        fake_flash_all_recv_wr((fake_qp_t *)qp);
    }

    FAKE_LOG("Modify qp(%p, %u) to %d.", qp, qp->qp_num, attr->qp_state);

    UNREF_PARAM(attr_mask);

    return 0;
}

/* *********************************************************************
 功能描述  : post recv
********************************************************************** */
int fake_post_recv(struct ibv_qp *qp, struct ibv_recv_wr *wr, struct ibv_recv_wr **bad_wr)
{
    fake_qp_t *fqp = (fake_qp_t *)(void *)qp;

    UNREF_PARAM(bad_wr);
    if (qp->state == IBV_QPS_ERR) {
        return IBV_ERROR;
    }

    fake_recv_wr_item_t *item = fake_recv_wr_mgr_produce_item(&fqp->recv_wr_mgr);
    if (item == NULL) {
        return IBV_ERROR;
    }

    item->wr = *wr;
    memcpy(item->sg_list, wr->sg_list, wr->num_sge * sizeof(struct ibv_sge));

    return 0;
}

int fake_post_srq_recv(struct ibv_srq *srq, struct ibv_recv_wr *recv_wr, struct ibv_recv_wr **bad_recv_wr)
{
    UNREF_PARAM(bad_recv_wr);

    fake_srq_t *fsrq = (fake_srq_t *)(void *)srq;
    fake_recv_wr_item_t *item = fake_recv_wr_mgr_produce_item(&fsrq->recv_wr_mgr);
    if (item == NULL) {
        return IBV_ERROR;
    }

    item->wr = *recv_wr;
    memcpy(item->sg_list, recv_wr->sg_list, recv_wr->num_sge * sizeof(struct ibv_sge));

    return 0;
}


/* *********************************************************************
 功能描述  : 查找对端的qp
********************************************************************** */
fake_qp_t *fake_find_peer_qp(fake_qp_t *my_qp)
{
    list_head_t *node = NULL;
    list_head_t *next = NULL;
    fake_qp_t *fqp = NULL;
    fake_qp_t *find = NULL;

    pthread_mutex_lock(&g_f_qp_list.listLock);
    list_for_each_safe(node, next, &g_f_qp_list.list.list_head)
    {
        fqp = list_entry(node, fake_qp_t, entry);
        if (fqp->dest_qp_num == my_qp->qp.qp_num) {
            find = fqp;
            break;
        }
    }
    pthread_mutex_unlock(&g_f_qp_list.listLock);

    return find;
}

/* *********************************************************************
 功能描述  : wr转换成wc的opcode
********************************************************************** */
enum ibv_wc_opcode fake_wr_to_wc_opcode(enum ibv_wr_opcode opcode)
{
    switch (opcode) {
        case IBV_WR_RDMA_WRITE:
            return IBV_WC_RDMA_WRITE;

        case IBV_WR_SEND:
            return IBV_WC_SEND;

        case IBV_WR_RDMA_READ:
            return IBV_WC_RDMA_READ;

        default:
            return IBV_WC_RECV;
    }
}

/* *********************************************************************
 功能描述  : 通知发送完成
********************************************************************** */
void fake_create_send_wc(fake_qp_t *fqp, struct ibv_send_wr *wr, uint64_t size)
{
    if (wr == NULL) {
        /* dcc combo write 分2次post，前一次post没有singled标记，不产生完成事件 */
        return;
    }

    fake_cq_t *fcq = (fake_cq_t *)fqp->qp.send_cq;

    struct ibv_wc wc = {};
    wc.wr_id = wr->wr_id;
    wc.status = IBV_WC_SUCCESS;
    wc.opcode = fake_wr_to_wc_opcode(wr->opcode);
    wc.qp_num = fqp->qp.qp_num;
    wc.byte_len = size;
    wc.imm_data = wr->imm_data;

    if (fake_prodce_cqe(fcq, &wc) == 0) {
        fake_send_event_on_cc(fcq->cq.channel);
    }
}

/* *********************************************************************
 功能描述  : 通知接收完成
********************************************************************** */
void fake_create_recv_wc(fake_cq_t *fcq, uint32_t qp_num, fake_recv_wr_item_t *item, uint64_t size, uint32_t immData)
{
    struct ibv_wc wc = {};
    wc.wr_id = item->wr.wr_id;
    wc.status = IBV_WC_SUCCESS;
    wc.opcode = IBV_WC_RECV;
    wc.byte_len = size;
    wc.qp_num = qp_num;
    wc.imm_data = immData;

    if (fake_prodce_cqe(fcq, &wc) == 0) {
        fake_send_event_on_cc(fcq->cq.channel);
    }
}

/* *********************************************************************
 功能描述  : post send处理
********************************************************************** */
int fake_ibv_post_send(fake_qp_t *my_qp, struct ibv_send_wr *wr)
{
    fake_qp_t *peer_qp = fake_find_peer_qp(my_qp);
    if (peer_qp == NULL) {
        return IBV_ERROR;
    }
    fake_recv_wr_mgr_t *recv_wr_mgr;
    if (peer_qp->qp.srq != NULL) {
        fake_srq_t *fsrq = (fake_srq_t *)peer_qp->qp.srq;
        recv_wr_mgr = &(fsrq->recv_wr_mgr);
    } else {
        recv_wr_mgr = &peer_qp->recv_wr_mgr;
    }

    fake_recv_wr_item_t *item = fake_recv_wr_mgr_comsume_item(recv_wr_mgr);
    if (item == NULL) {
        FAKE_LOG("Item is null.");
        return IBV_ERROR;
    }

    uint64_t size = 0;
    for (int i = 0; i < wr->num_sge; i++) {
        if (item->sg_list[i].length == 0) {
            // 兼容一个wqe接受sgl的场景，直接拷贝在上一个
            memcpy(reinterpret_cast<void *>(
                static_cast<uintptr_t>(item->sg_list[i - 1].addr + wr->sg_list[i - 1].length)),
                reinterpret_cast<void *>(static_cast<uintptr_t>(wr->sg_list[i].addr)),
                wr->sg_list[i].length);
            size += wr->sg_list[i].length;
            continue;
        }
        memcpy(reinterpret_cast<void *>(
            static_cast<uintptr_t>(item->sg_list[i].addr)),
            reinterpret_cast<void *>(static_cast<uintptr_t>(wr->sg_list[i].addr)),
            wr->sg_list[i].length);
        size += wr->sg_list[i].length;
    }

    if (wr->send_flags == IBV_SEND_SIGNALED) {
        fake_create_send_wc(my_qp, wr, size);
    }

    fake_cq_t *fcq = (fake_cq_t *)peer_qp->qp.recv_cq;
    fake_create_recv_wc(fcq, peer_qp->qp.qp_num, item, size, wr->imm_data);

    return 0;
}

/* *********************************************************************
 功能描述  : post read处理
********************************************************************** */
int fake_post_read(fake_qp_t *my_qp, struct ibv_send_wr *wr)
{
    uint64_t size = 0;
    struct ibv_send_wr *wr_temp = wr;
    struct ibv_send_wr *finish_wr = NULL;

    while (wr_temp != NULL) {
        int i;
        int remote_addr_offset = 0;
        for (i = 0; i < wr_temp->num_sge; i++) {
            remote_addr_offset += ((i == 0) ? 0 : wr_temp->sg_list[i - 1].length);
            char *src = (char *)(uintptr_t)wr_temp->wr.rdma.remote_addr + remote_addr_offset;
            if (src == 0x0 || wr_temp->wr.rdma.rkey == 0) {
                FAKE_LOG("Illegal rkey %d or rAddress %p", wr_temp->wr.rdma.rkey, src);
                return IBV_ERROR;
            }
            memcpy((void *)(uintptr_t)wr_temp->sg_list[i].addr, (char *)(uintptr_t)src, wr_temp->sg_list[i].length);
            size += wr_temp->sg_list[i].length;
        }

        if (wr_temp->send_flags == IBV_SEND_SIGNALED) {
            finish_wr = wr_temp;
            break;
        }
        wr_temp = wr_temp->next;
    }

    if (finish_wr == NULL) {
        FAKE_LOG("Wr isn't set any send signal flag.");
        return IBV_ERROR;
    }

    if (finish_wr->next != NULL) {
        FAKE_LOG("Wr must be the last one, if it is been set send signal flag.");
        return IBV_ERROR;
    }

    fake_create_send_wc(my_qp, finish_wr, size);

    return 0;
}

/* *********************************************************************
 功能描述  : post write处理
********************************************************************** */
int fake_post_write(fake_qp_t *my_qp, struct ibv_send_wr *wr)
{
    struct ibv_send_wr *wr_temp = wr;
    struct ibv_send_wr *finish_wr = NULL;
    uint64_t size = 0;

    while (wr_temp != NULL) {
        int i;
        int remote_addr_offset = 0;
        for (i = 0; i < wr_temp->num_sge; i++) {
            remote_addr_offset += ((i == 0) ? 0 : wr_temp->sg_list[i - 1].length);
            char *src = (char *)(uintptr_t)wr_temp->wr.rdma.remote_addr + remote_addr_offset;
            if (src == 0x0 || wr_temp->wr.rdma.rkey == 0) {
                FAKE_LOG("Illegal rkey %d or rAddress %p", wr_temp->wr.rdma.rkey, src);
                return IBV_ERROR;
            }
            memcpy(src, (void *)(uintptr_t)wr_temp->sg_list[i].addr, wr_temp->sg_list[i].length);
            size += wr_temp->sg_list[i].length;
        }

        if (wr_temp->send_flags == IBV_SEND_SIGNALED) {
            finish_wr = wr_temp;
            break;
        }
        wr_temp = wr_temp->next;
    }

    /* dcc combo write 分2次post，前一次post没有singled标记，不要求每次post一定有singled标记 */
    if (finish_wr != NULL && finish_wr->next != NULL) {
        FAKE_LOG("Wr must be the last one, if it is been set send signal flag.");
        return IBV_ERROR;
    }

    fake_create_send_wc(my_qp, finish_wr, size);

    return 0;
}

/* *********************************************************************
 功能描述  : post send
********************************************************************** */
int fake_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr)
{
    int ret;
    fake_qp_t *my_qp = (fake_qp_t *)qp;

    UNREF_PARAM(bad_wr);

    switch (wr->opcode) {
        case IBV_WR_SEND:
        case IBV_WR_SEND_WITH_IMM:
            ret = fake_ibv_post_send(my_qp, wr);
            break;

        case IBV_WR_RDMA_READ:
            ret = fake_post_read(my_qp, wr);
            break;

        case IBV_WR_RDMA_WRITE:
            ret = fake_post_write(my_qp, wr);
            break;

        default:
            ret = IBV_ERROR;
            FAKE_LOG("Fake don't support opcode(%d).", wr->opcode);
            break;
    }

    return ret;
}

/* *********************************************************************
 功能描述  : 模拟进行post cq,将已经完成的事件全部放到用户的wc中
********************************************************************** */
int fake_poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc)
{
    int i;
    fake_cq_t *fcq = (fake_cq_t *)(void *)cq;

    pthread_mutex_lock(&fcq->cqLock);
    for (i = 0; i < num_entries; i++) {
        struct ibv_wc *my_wc = fake_comsume_cqe(fcq);
        if (my_wc == NULL) {
            break;
        }

        (void)memcpy(&wc[i], my_wc, sizeof(struct ibv_wc));
    }
    pthread_mutex_unlock(&fcq->cqLock);

    return i;
}

int fake_req_notify_cq(struct ibv_cq *cq, int solicited_only)
{
    if (cq == NULL) {
        return 0;
    }

    fake_cq_t *fcq = (fake_cq_t *)cq;

    if (fcq->wcq.comsumer != fcq->wcq.producer) {
        fake_send_event_on_cc(fcq->cq.channel);
    }

    UNREF_PARAM(solicited_only);

    return 0;
}

typedef struct {
    struct verbs_context v_ctx;
} ibv_verb_ctx_all_t;

struct ibv_context *ibv_open_device(struct ibv_device *device)
{
    ibv_verb_ctx_all_t *ctx = (ibv_verb_ctx_all_t *)calloc(1, sizeof(ibv_verb_ctx_all_t));
    if (ctx == NULL) {
        FAKE_LOG("fake_ibv: create verbs_context fail.");
        return NULL;
    }

    struct verbs_context *v_ctx = &ctx->v_ctx;
    struct ibv_context *context = &v_ctx->context;

    context->abi_compat = ((uint8_t *)nullptr) - 1; /* verbs 扩展接口判断 */
    v_ctx->sz = sizeof(struct verbs_context);
    v_ctx->create_qp_ex = nullptr;

    context->device = device;
    context->cmd_fd = FAKE_NULL_FD;
    context->async_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    context->num_comp_vectors = 0;

    context->ops.poll_cq = fake_poll_cq;
    context->ops.post_send = fake_post_send;
    context->ops.post_recv = fake_post_recv;
    context->ops.req_notify_cq = fake_req_notify_cq;
    context->ops.post_srq_recv = fake_post_srq_recv;

    return context;
}

int ibv_close_device(struct ibv_context *context)
{
    if (context->cmd_fd != FAKE_NULL_FD) {
        close(context->cmd_fd);
        context->cmd_fd = FAKE_NULL_FD;
    }

    if (context->async_fd != FAKE_NULL_FD) {
        close(context->async_fd);
        context->async_fd = FAKE_NULL_FD;
    }

    struct verbs_context *v_ctx = container_of(context, struct verbs_context, context);
    ibv_verb_ctx_all_t *ctx = container_of(v_ctx, ibv_verb_ctx_all_t, v_ctx);
    free(ctx);

    return 0;
}

int ibv_query_device(struct ibv_context *context, struct ibv_device_attr *device_attr)
{
    (void)memset(device_attr, 0, sizeof(struct ibv_device_attr));

    /* 当前xnet只用了,gid和portcnt，故只初始化这两个字段，可根据代码需要自行增加 */
    if (snprintf(device_attr->fw_ver, sizeof(device_attr->fw_ver) - 1, "xnet_fake_0.1") == -1) {
        return IBV_ERROR;
    }

    device_attr->node_guid = 1;
    device_attr->phys_port_cnt = 1;

    UNREF_PARAM(context);
    return 0;
}

#ifdef ibv_reg_mr
#undef ibv_reg_mr
#endif
struct ibv_mr *ibv_reg_mr(struct ibv_pd *pd, void *address, size_t length, int access)
{
    struct ibv_mr *mr = (struct ibv_mr *)calloc(1, sizeof(struct ibv_mr));
    if (!mr) {
        FAKE_LOG("fake_ibv: ibv_reg_mr fail.");
        return NULL;
    }

    mr->context = pd->context;
    mr->pd = pd;
    mr->addr = address;
    mr->length = length;
    mr->lkey = fake_get_key();
    mr->rkey = fake_get_key();
    mr->handle = 0;

    UNREF_PARAM(access);

    return mr;
}

struct ibv_mr *ibv_reg_mr_iova2(struct ibv_pd *pd, void *address, size_t length, uint64_t iova, unsigned int access)
{
    struct ibv_mr *mr = (struct ibv_mr *)calloc(1, sizeof(struct ibv_mr));
    if (!mr) {
        FAKE_LOG("fake_ibv: ibv_reg_mr fail.");
        return NULL;
    }

    mr->context = pd->context;
    mr->pd = pd;
    mr->addr = address;
    mr->length = length;
    mr->lkey = fake_get_key();
    mr->rkey = fake_get_key();
    mr->handle = 0;

    UNREF_PARAM(access);

    return mr;
}

int ibv_dereg_mr(struct ibv_mr *mr)
{
    free(mr);
    return 0;
}

struct ibv_mr *ibv_reg_umm_page_mr(struct ibv_pd *pd, void *addr, void *knl_addr, size_t length, int access_flag)
{
    UNREF_PARAM(knl_addr);

    return ibv_reg_mr(pd, addr, length, access_flag);
}

int ibv_dereg_umm_page_mr(struct ibv_mr *mr)
{
    free(mr);
    return 0;
}


int ibv_query_gid(struct ibv_context *context, uint8_t port_num, int index, union ibv_gid *gid)
{
    (void)memset(gid, 0, sizeof(union ibv_gid));
    auto devI6Address = reinterpret_cast<struct in6_addr *>(gid->raw);

    devI6Address->s6_addr32[2UL] = htonl(0x0000ffff);
    devI6Address->s6_addr32[3UL] = inet_addr("127.0.0.1");

    UNREF_PARAM(context);
    UNREF_PARAM(port_num);
    UNREF_PARAM(index);
    return 0;
}

/* ********************************************************************
 * 下面几个函数无实际流程调用，打空桩
 * ******************************************************************** */
int ibv_query_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask, struct ibv_qp_init_attr *init_attr)
{
    FAKE_LOG("fake_ibv: call fake function ibv_query_qp.");
    UNREF_PARAM(qp);
    UNREF_PARAM(attr);
    UNREF_PARAM(attr_mask);
    UNREF_PARAM(init_attr);
    return 0;
}

int ibv_check_qp(struct ibv_context *context, pid_t pid)
{
    FAKE_LOG("fake_ibv: call fake function ibv_check_qp.");

    UNREF_PARAM(context);
    UNREF_PARAM(pid);
    return 0;
}

void ibv_dfx_qp_wr_dump(struct ibv_qp *qp)
{
    FAKE_LOG("fake_ibv: call fake function dfx_qp_wr_dump.");
    UNREF_PARAM(qp);
}

void ibv_dfx_port_traffic_dump(struct ibv_context *context)
{
    FAKE_LOG("fake_ibv: call fake function port_traffic_dump.");
    UNREF_PARAM(context);
}

void ibv_dfx_counter(struct ibv_qp *qp)
{
    FAKE_LOG("fake_ibv: call fake function dfx_counter.");
    UNREF_PARAM(qp);
}

const char *ibv_port_state_str(enum ibv_port_state port_state)
{
    return "OK";
}

#ifdef __cplusplus
}
#endif