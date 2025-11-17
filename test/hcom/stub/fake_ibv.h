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
#ifndef __FAKE_IBV_H__
#define __FAKE_IBV_H__

#include <cxxabi.h>
#include <execinfo.h>
#include <infiniband/verbs.h>
#include <pthread.h>
#include <cstring>

#include "hcom_log.h"

#ifdef __cplusplus
extern "C" {
#endif

struct list_head {
    struct list_head *next, *prev;
};
typedef struct list_head list_head_t;

#define FAKE_IBV_DEVICE_NUM 2
#define FAKE_RECV_WR_SGE_NUM 8
#define FAKE_RECV_WR_DEPTH 1024

typedef struct dsw_list_s {
    list_head_t list_head;
    int node_num;
} fake_list_t;

typedef struct {
    fake_list_t list;
    pthread_mutex_t listLock;
} fake_lock_list_t;

#define INIT_LIST_HEAD(ptr)  \
    {                        \
        (ptr)->next = (ptr); \
        (ptr)->prev = (ptr); \
    }

#define list_for_each_safe(pos, n, head) \
    for ((pos) = (head)->next, (n) = (pos)->next; (pos) != (head); (pos) = (n), (n) = (pos)->next)

#define list_entry(ptr, type, member) ((type *)(void *)((char *)(ptr) - offsetof(type, member)))

static inline void __list_add(struct list_head *newnode, struct list_head *prevnode, struct list_head *nextnode)
{
    nextnode->prev = newnode;
    newnode->next = nextnode;
    newnode->prev = prevnode;
    prevnode->next = newnode;
}

static inline void __list_del(struct list_head *prev, struct list_head *next)
{
    next->prev = prev;
    prev->next = next;
}

static inline void list_add_tail(struct list_head *new_head, struct list_head *head)
{
    __list_add(new_head, head->prev, head);
}

static inline void list_del_node(list_head_t *node, fake_list_t *list)
{
    __list_del(node->prev, node->next);
    INIT_LIST_HEAD(node);
}

static inline void fake_lock_list_init(fake_lock_list_t *list)
{
    pthread_mutex_init(&list->listLock, NULL);
    INIT_LIST_HEAD(&(list->list.list_head));
    list->list.node_num = 0;
}

static inline void fake_lock_list_add_tail(list_head_t *node, fake_lock_list_t *lock_list)
{
    pthread_mutex_lock(&lock_list->listLock);
    lock_list->list.node_num += 1;
    list_add_tail(node, &(lock_list->list.list_head));
    pthread_mutex_unlock(&lock_list->listLock);
}

static inline void fake_lock_list_del_node(list_head_t *node, fake_lock_list_t *lock_list)
{
    pthread_mutex_lock(&lock_list->listLock);
    list_del_node(node, &(lock_list->list));
    lock_list->list.node_num -= 1;
    pthread_mutex_unlock(&lock_list->listLock);
}

typedef struct {
    struct ibv_recv_wr wr;
    struct ibv_sge sg_list[FAKE_RECV_WR_SGE_NUM];
} fake_recv_wr_item_t;

typedef struct {
    uint32_t producer;
    uint32_t comsuer;
    fake_recv_wr_item_t item[FAKE_RECV_WR_DEPTH];
    pthread_mutex_t wrLock;
} fake_recv_wr_mgr_t;

/* 结构体定义 */
typedef struct fake_qp_s {
    struct ibv_qp qp;        /* 对外呈现的qp结构 */
    struct ibv_recv_wr head; /* 用来挂载外部注册的recv，外部post的时候申请结构挂到链表上 */
    list_head_t entry;
    uint32_t dest_qp_num;
    fake_recv_wr_mgr_t recv_wr_mgr;
} fake_qp_t;

typedef struct fake_srq_s {
    struct ibv_srq srq;
    fake_recv_wr_mgr_t recv_wr_mgr;
} fake_srq_t;

typedef struct cqe_queue_s {
    uint32_t producer;
    uint32_t comsumer;
    struct ibv_wc *buff;
} cqe_queue_t;

typedef struct fake_cq_s {
    struct ibv_cq cq;
    pthread_mutex_t cqLock;
    cqe_queue_t wcq;
    list_head_t entry;
} fake_cq_t;

#define MAX_CQ_ON_CC 128

typedef struct {
    struct ibv_comp_channel cc;
    fake_lock_list_t cq_list;
} fake_cc_t;


struct ibv_mr *ibv_reg_umm_page_mr(struct ibv_pd *pd, void *addr, void *knl_addr, size_t length, int access_flag);
int ibv_dereg_umm_page_mr(struct ibv_mr *mr);
int ibv_check_qp(struct ibv_context *context, pid_t pid);
void ibv_dfx_qp_wr_dump(struct ibv_qp *qp);
void ibv_dfx_port_traffic_dump(struct ibv_context *context);
void ibv_dfx_counter(struct ibv_qp *qp);

int fake_prodce_cqe(fake_cq_t *fcq, struct ibv_wc *wc);
void fake_create_recv_wc(fake_cq_t *fcq, uint32_t qp_num, fake_recv_wr_item_t *item, uint64_t size, uint32_t immData);
void fake_recv_wr_mgr_init(fake_recv_wr_mgr_t *recv_wr_mgr);
__attribute__((constructor)) void fake_ibv_init(void);
void fake_send_event_on_cc(struct ibv_comp_channel *cmc);
fake_recv_wr_item_t *fake_recv_wr_mgr_comsume_item(fake_recv_wr_mgr_t *recv_wr_mgr);
void fake_create_send_wc(fake_qp_t *fqp, struct ibv_send_wr *wr, uint64_t size);
fake_lock_list_t *fake_get_qp_list(void);
uint32_t fake_get_qp_num(void);
void fake_flash_all_recv_wr(fake_qp_t *fqp);

#define UNREF_PARAM(x) ((void)(x))

#ifdef ibv_query_port
#undef ibv_query_port
#endif
static int fake_ibv_query_port(struct ibv_context *context, uint8_t port_num,
    struct _compat_ibv_port_attr *port_attr_in)
{
    struct ibv_port_attr *port_attr = (struct ibv_port_attr *)port_attr_in;
    port_attr->state = IBV_PORT_ACTIVE;
    port_attr->active_mtu = IBV_MTU_4096;
    port_attr->gid_tbl_len = 1;  /* gid table长度不知道为多少，先设置为1，不行再改 */
    port_attr->lid = 0;          /* lid值调试的时候再定 */
    port_attr->active_speed = 0; /* 速率也乱填一个值，应该不会影响LLT功能 */
    port_attr->link_layer = IBV_LINK_LAYER_ETHERNET;

    UNREF_PARAM(context);
    UNREF_PARAM(port_num);

    return 0;
}

#define fake_ibv_query_port(context, port_num, port_attr) fake_ibv_query_port(context, port_num, port_attr)
#ifdef __cplusplus
}
#endif

const int BT_SIZE = 40u;
const int THREAD_MAX_NAME_LEN = 16u;
const int STR_SIZE = 512u;

inline std::string DemangleFuncName(const char *str)
{
    size_t size = 0;
    int status = 0;
    std::string tmpStr;
    tmpStr.resize(STR_SIZE);

    if (str == nullptr) {
        std::string emptyStr = "empty";
        return emptyStr;
    }

    if (1 == sscanf(str, "%*[^(]%*[^_]%255[^)+]", &tmpStr[0])) {
        char *tmp = abi::__cxa_demangle(&tmpStr[0], nullptr, &size, &status);
        if (tmp) {
            std::string result(tmp);
            free(tmp);
            return result;
        }
    }

    if (1 == sscanf(str, "%255s", &tmpStr[0])) {
        return tmpStr;
    }

    return str;
}

inline void NetBacktrace(uint64_t id)
{
    void **list = nullptr;
    char **stacks = nullptr;
    int size;
    list = new (std::nothrow) void *[BT_SIZE];
    if (list == nullptr) {
        printf("Failed to alloc memory for list");
        return;
    }
    size = backtrace(list, BT_SIZE);
    stacks = backtrace_symbols(list, size);
    if (stacks != nullptr) {
        char *thName = new (std::nothrow) char[THREAD_MAX_NAME_LEN];
        if (thName == nullptr) {
            printf("Failed to alloc memory for thName");
            free(stacks);
            delete[] list;
            return;
        }
        if (pthread_getname_np(pthread_self(), thName, THREAD_MAX_NAME_LEN) != 0) {
            printf("Failed to get the thread name for %lu", pthread_self());
        } else {
            printf("%s id: %lu backtrace:", thName, id);
        }
        delete[] thName;
        for (int i = 0; i < size; i++) {
            printf("Id(%d) :[%s]\n", i, DemangleFuncName(stacks[i]).c_str());
        }
        free(stacks);
    }
    delete[] list;
}

#endif /* __FAKE_IBV_H__ */
