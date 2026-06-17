/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: umq perf
 * Create: 2025-10-29
 */

#include <pthread.h>
#include <stdarg.h>

#include "umq_errno.h"
#include "umq_vlog.h"
#include "urpc_thread_closure.h"
#include "urpc_util.h"
#include "urpc_timer.h"
#include "perf.h"

#define UMQ_PERF_MAX_THRESH_NS         (100000u)
#define MEDIAN_FACTOR                  (0.5)
#define P90_FACTOR                     (0.9)
#define P99_FACTOR                     (0.99)

#define UMQ_PERF_IO_DIRECTION_ALL_OFFSET     (0)
#define UMQ_PERF_IO_DIRECTION_TX_OFFSET      (1)
#define UMQ_PERF_IO_DIRECTION_RX_OFFSET      (2)

#define UMQ_PERF_INTERRUPT_DIRECTION_TX_OFFSET      (0)
#define UMQ_PERF_INTERRUPT_DIRECTION_RX_OFFSET      (1)

static __thread uint32_t g_perf_record_index = -1;
static __thread pthread_once_t g_dp_thread_run_once = PTHREAD_ONCE_INIT;
static bool g_umq_perf_record_enable = false;

typedef struct umq_perf_record {
    struct {
        umq_perf_record_type_t type; // types of probe points supported by perf probe
        uint64_t accumulation; // total latency
        uint64_t min; // min latency
        uint64_t max; // max latency
        uint64_t cnt; // statistical count
        uint64_t bucket[UMQ_PERF_QUANTILE_MAX_NUM + 1]; // sample count in each quantile bin
    } type_record[UMQ_PERF_RECORD_TYPE_MAX]; // statistical results list for each type of probe poin
    bool is_used; // the statistic item valid
} umq_perf_record_t;

typedef struct umq_perf_record_ctx {
    umq_perf_record_t perf_record_table[UMQ_PERF_REC_MAX_NUM];
    pthread_once_t *dp_thread_run_once[UMQ_PERF_REC_MAX_NUM];
    uint64_t perf_quantile_thresh[UMQ_PERF_QUANTILE_MAX_NUM];
    uint64_t thresh_ns[UMQ_PERF_QUANTILE_MAX_NUM];
    uint32_t thresh_num;
} umq_perf_record_ctx_t;

static pthread_spinlock_t g_umq_perf_record_lock;
static umq_perf_record_ctx_t *g_umq_perf_record_ctx;

#define DATA_RECORD_NUM 1000000
#define DATA_RECORD_LIMIT 5000
static bool g_umq_print_flag = true;

typedef struct umq_perf_data {
    uint32_t post_cnt;
    uint32_t pre_post_cnt;
    uint32_t poll_cnt;
    uint32_t pre_poll_cnt;
    umq_data_record_t post_data_record[DATA_RECORD_NUM]; // 每个线程post数据
    umq_data_record_t poll_data_record[DATA_RECORD_NUM]; // 每个线程poll数据
    bool is_used; // the statistic item valid
} umq_perf_data_t;

typedef struct umq_perf_data_ctx {
    umq_perf_data_t data_table[UMQ_PERF_REC_MAX_NUM]; // 线程组数据
} umq_perf_data_ctx_t;

static umq_perf_data_ctx_t *g_umq_perf_data_ctx;

int umq_perf_init(void)
{
    if (g_umq_perf_record_ctx != NULL) {
        UMQ_VLOG_ERR(VLOG_UMQ, "umq perf has been inited\n");
        return -UMQ_ERR_EEXIST;
    }

    g_umq_perf_record_ctx = (umq_perf_record_ctx_t *)calloc(1, sizeof(umq_perf_record_ctx_t));
    if (g_umq_perf_record_ctx == NULL) {
        UMQ_VLOG_ERR(VLOG_UMQ, "calloc for umq_perf_record failed\n");
        return -UMQ_ERR_ENOMEM;
    }
    (void)pthread_spin_init(&g_umq_perf_record_lock, PTHREAD_PROCESS_PRIVATE);

    g_umq_perf_data_ctx = (umq_perf_data_ctx_t *)calloc(1, sizeof(umq_perf_data_ctx_t));
    if (g_umq_perf_data_ctx == NULL) {
        UMQ_VLOG_ERR(VLOG_UMQ, "calloc for umq_perf_data failed\n");
        return -UMQ_ERR_ENOMEM;
    }
    return UMQ_SUCCESS;
}

void umq_perf_uninit(void)
{
    if (g_umq_perf_record_ctx == NULL) {
        return;
    }

    (void)pthread_spin_lock(&g_umq_perf_record_lock);
    for (uint32_t i = 0; i < UMQ_PERF_REC_MAX_NUM; i++) {
        if (g_umq_perf_record_ctx->dp_thread_run_once[i] != NULL) {
            *g_umq_perf_record_ctx->dp_thread_run_once[i] = PTHREAD_ONCE_INIT;
        }
    }

    g_umq_perf_record_enable = false;
    free(g_umq_perf_record_ctx);
    g_umq_perf_record_ctx = NULL;
    (void)pthread_spin_unlock(&g_umq_perf_record_lock);
    (void)pthread_spin_destroy(&g_umq_perf_record_lock);
}

static void umq_clear_perf_record_item(uint32_t record_idx)
{
    umq_perf_record_t *cur_record = &g_umq_perf_record_ctx->perf_record_table[record_idx];
    for (int type = 0; type < UMQ_PERF_RECORD_TYPE_MAX; ++type) {
        cur_record->type_record[type].accumulation = 0;
        cur_record->type_record[type].min = UINT64_MAX;
        cur_record->type_record[type].max = 0;
        cur_record->type_record[type].cnt = 0;
        (void)memset(cur_record->type_record[type].bucket, 0, sizeof(cur_record->type_record[type].bucket));
    }
}

static void umq_perf_record_closure(uint64_t idx)
{
    (void)pthread_spin_lock(&g_umq_perf_record_lock);
    if (g_umq_perf_record_ctx == NULL) {
        (void)pthread_spin_unlock(&g_umq_perf_record_lock);
        return;
    }
    g_umq_perf_record_ctx->perf_record_table[idx].is_used = false;
    g_umq_perf_record_ctx->dp_thread_run_once[idx] = NULL;
    (void)pthread_spin_unlock(&g_umq_perf_record_lock);
}

static void umq_clear_data_record(uint32_t idx)
{
    umq_perf_data_t *cur_record = &g_umq_perf_data_ctx->data_table[idx];
    cur_record->post_cnt = 0;
    cur_record->poll_cnt = 0;
    cur_record->pre_post_cnt = 0;
    cur_record->pre_poll_cnt = 0;
}

void umq_perf_record_alloc(void)
{
    uint32_t idx;
    (void)pthread_spin_lock(&g_umq_perf_record_lock);
    if (g_umq_perf_record_ctx == NULL) {
        (void)pthread_spin_unlock(&g_umq_perf_record_lock);
        UMQ_VLOG_ERR(VLOG_UMQ, "perf record ctx invalid\n");
        return;
    }

    for (idx = 0; idx < UMQ_PERF_REC_MAX_NUM; ++idx) {
        if (!g_umq_perf_record_ctx->perf_record_table[idx].is_used) {
            break;
        }
    }
    if (idx == UMQ_PERF_REC_MAX_NUM) {
        (void)pthread_spin_unlock(&g_umq_perf_record_lock);
        UMQ_VLOG_WARN(VLOG_UMQ, "perf_rec table capacity %u were exhausted, alloc perf_rec failed\n",
            UMQ_PERF_REC_MAX_NUM);
        return;
    }

    umq_clear_perf_record_item(idx);
    umq_clear_data_record(idx);
    g_umq_perf_record_ctx->perf_record_table[idx].is_used = true;
    g_umq_perf_data_ctx->data_table[idx].is_used = true;
    (void)pthread_spin_unlock(&g_umq_perf_record_lock);

    g_perf_record_index = idx;
    g_umq_perf_record_ctx->dp_thread_run_once[idx] = &g_dp_thread_run_once;
    urpc_thread_closure_register(THREAD_CLOSURE_UMQ_PERF, idx, umq_perf_record_closure);
}

static void umq_dp_thread_run_once(void)
{
    umq_perf_record_alloc();
}

uint64_t umq_perf_get_start_timestamp(void)
{
    if (!g_umq_perf_record_enable) {
        return 0;
    }
    pthread_once(&g_dp_thread_run_once, umq_dp_thread_run_once);
    return urpc_get_cpu_cycles();
}

static inline void umq_perf_fill_data_record(umq_perf_data_type_t type, umq_data_record_t data)
{
    umq_perf_data_t *cur_rec = &g_umq_perf_data_ctx->data_table[g_perf_record_index];
    if (type == UMQ_PERF_DATA_TYPE_POST) {
        uint32_t post_cnt = cur_rec->post_cnt;
        cur_rec->post_data_record[post_cnt].user_data = data.user_data;
        cur_rec->post_data_record[post_cnt].total_size = data.total_size;
        cur_rec->post_data_record[post_cnt].jetty_id = data.jetty_id;
        cur_rec->post_data_record[post_cnt].local_umq_id = data.local_umq_id;
        cur_rec->post_data_record[post_cnt].remote_umq_id = data.remote_umq_id;
        cur_rec->post_data_record[post_cnt].timestamp = data.timestamp;
        cur_rec->post_data_record[post_cnt].urma_post_time = data.urma_post_time;
        cur_rec->post_data_record[post_cnt].urma_post_start = data.urma_post_start;
        cur_rec->post_data_record[post_cnt].urma_post_end = data.urma_post_end;
        cur_rec->post_data_record[post_cnt].fc_send_req = data.fc_send_req;
        cur_rec->post_data_record[post_cnt].umq_post_tx_time = data.umq_post_tx_time;
        cur_rec->post_data_record[post_cnt].umq_post_tx_do_while_time = data.umq_post_tx_do_while_time;
        cur_rec->post_data_record[post_cnt].umq_post_tx_do_after_while_time = data.umq_post_tx_do_after_while_time;
        cur_rec->post_data_record[post_cnt].umq_post_start = data.umq_post_start;
        cur_rec->post_cnt++;
        if (cur_rec->post_cnt >= DATA_RECORD_NUM) {
            cur_rec->post_cnt = 0;
        }
    } else {
        uint32_t poll_cnt = cur_rec->poll_cnt;
        cur_rec->poll_data_record[poll_cnt].user_data = data.user_data;
        cur_rec->poll_data_record[poll_cnt].total_size = data.total_size;
        cur_rec->poll_data_record[poll_cnt].jetty_id = data.jetty_id;
        cur_rec->poll_data_record[poll_cnt].local_umq_id = data.local_umq_id;
        cur_rec->poll_data_record[poll_cnt].remote_umq_id = data.remote_umq_id;
        cur_rec->poll_data_record[poll_cnt].timestamp = data.timestamp;
        cur_rec->poll_data_record[poll_cnt].urma_poll_time = data.urma_poll_time;
        cur_rec->poll_data_record[poll_cnt].umq_poll_end = data.umq_poll_end;
        cur_rec->poll_cnt++;
        if (cur_rec->poll_cnt >= DATA_RECORD_NUM) {
            cur_rec->poll_cnt = 0;
        }
    }
}

void umq_perf_data_write(umq_perf_data_type_t type, umq_data_record_t data)
{
    if (!g_umq_perf_record_enable || g_perf_record_index >= UMQ_PERF_REC_MAX_NUM) {
        return;
    }
    umq_perf_fill_data_record(type, data);
}


static inline uint32_t find_perf_record_bucket(uint64_t delta)
{
    if (g_umq_perf_record_ctx->perf_quantile_thresh[0] == 0) {
        // quantile thresh is not set, don't fill the bucket
        return UINT32_MAX;
    }
    uint32_t idx;
    for (idx = 0; idx < UMQ_PERF_QUANTILE_MAX_NUM; ++idx) {
        if (delta <= g_umq_perf_record_ctx->perf_quantile_thresh[idx]) {
            break;
        }
    }
    return idx;
}

static inline void umq_perf_fill_perf_record(umq_perf_record_type_t type, uint64_t start)
{
    uint64_t delta = urpc_get_cpu_cycles() - start;
    umq_perf_record_t *cur_rec = &g_umq_perf_record_ctx->perf_record_table[g_perf_record_index];
    cur_rec->type_record[type].accumulation += delta;
    (delta < cur_rec->type_record[type].min) ? cur_rec->type_record[type].min = delta : 0;
    (delta > cur_rec->type_record[type].max) ? cur_rec->type_record[type].max = delta : 0;
    uint32_t bucket_idx = find_perf_record_bucket(delta);
    if (bucket_idx != UINT32_MAX) {
        ++cur_rec->type_record[type].bucket[bucket_idx];
    }
    ++cur_rec->type_record[type].cnt;
}

void umq_perf_record_write(umq_perf_record_type_t type, uint64_t start)
{
    if (!g_umq_perf_record_enable || start == 0 || g_perf_record_index >= UMQ_PERF_REC_MAX_NUM) {
        return;
    }
    umq_perf_fill_perf_record(type, start);
}

void umq_perf_record_write_with_direction(umq_perf_record_type_t type, uint64_t start, umq_io_direction_t direction)
{
    if (!g_umq_perf_record_enable || start == 0 ||
        g_perf_record_index >= UMQ_PERF_REC_MAX_NUM || direction >= UMQ_IO_MAX) {
        return;
    }

    static const umq_perf_record_type_t perf_record_type_map[UMQ_IO_MAX] = {
        [UMQ_IO_ALL] = UMQ_PERF_IO_DIRECTION_ALL_OFFSET,
        [UMQ_IO_TX]  = UMQ_PERF_IO_DIRECTION_TX_OFFSET,
        [UMQ_IO_RX]  = UMQ_PERF_IO_DIRECTION_RX_OFFSET,
    };
    umq_perf_fill_perf_record(type + perf_record_type_map[direction], start);
}

void umq_perf_record_write_interrupt_with_direction(
    umq_perf_record_type_t type, uint64_t start, umq_io_direction_t direction)
{
    if (!g_umq_perf_record_enable || start == 0 ||
        g_perf_record_index >= UMQ_PERF_REC_MAX_NUM || direction >= UMQ_IO_MAX || direction == UMQ_IO_ALL) {
        return;
    }

    static const umq_perf_record_type_t perf_record_type_map[UMQ_IO_MAX] = {
        [UMQ_IO_TX]  = UMQ_PERF_INTERRUPT_DIRECTION_TX_OFFSET,
        [UMQ_IO_RX]  = UMQ_PERF_INTERRUPT_DIRECTION_RX_OFFSET,
    };
    umq_perf_fill_perf_record(type + perf_record_type_map[direction], start);
}

void umq_ub_close_perf_log(void)
{
    g_umq_print_flag = false;
}

void umq_ub_remain_data_print(void *args)
{
    if (!g_umq_print_flag) {
 	    return;
 	}
    UMQ_VLOG_INFO(VLOG_UMQ, "============umq_ub_remain_data_print===========\n");
    for (uint32_t k = 0; k < UMQ_PERF_REC_MAX_NUM; ++k) { //遍历所有线程
        if (!g_umq_perf_data_ctx->data_table[k].is_used) {
            break;
        }
        umq_perf_data_t *cur_rec = &g_umq_perf_data_ctx->data_table[k];
        uint32_t post_cnt = cur_rec->post_cnt;
        uint32_t poll_cnt = cur_rec->poll_cnt;
        uint32_t pre_post_cnt = cur_rec->pre_post_cnt;
        uint32_t pre_poll_cnt = cur_rec->pre_poll_cnt;
        uint32_t post_end = post_cnt < pre_post_cnt? (post_cnt + DATA_RECORD_NUM): post_cnt;
        uint32_t poll_end = poll_cnt < pre_poll_cnt? (poll_cnt + DATA_RECORD_NUM): poll_cnt;
        if (post_end - pre_post_cnt > 0) {
            uint32_t idx = 0;
            UMQ_VLOG_INFO(VLOG_UMQ, "============thread %d post: %d--%d ===========\n", k, pre_post_cnt, post_end);
            for (uint32_t i = pre_post_cnt; i < post_end; i++) {
                idx = i % DATA_RECORD_NUM;
                UMQ_VLOG_INFO(VLOG_UMQ, "user_data: %u, total_size: %u, jetty_id: %u, local_umq_id: %u, remote_umq_id: %u, "
                "timestamp: %lu, fc_post_send: %lu, urma_post_start: %lu, urma_post_end: %lu, urma_post_time: %lu, umq_post_start: %lu, umq_post_tx_time: %lu, post_tx_while_time: %lu, post_tx_after_while_time: %lu\n",
                cur_rec->post_data_record[idx].user_data,
                cur_rec->post_data_record[idx].total_size,
                cur_rec->post_data_record[idx].jetty_id, 
                cur_rec->post_data_record[idx].local_umq_id, 
                cur_rec->post_data_record[idx].remote_umq_id,
                cur_rec->post_data_record[idx].timestamp, 
                cur_rec->post_data_record[idx].fc_send_req,
                cur_rec->post_data_record[idx].urma_post_start,
                cur_rec->post_data_record[idx].urma_post_end,
                cur_rec->post_data_record[idx].urma_post_time,
                cur_rec->post_data_record[idx].umq_post_start,
                cur_rec->post_data_record[idx].umq_post_tx_time,
                cur_rec->post_data_record[idx].umq_post_tx_do_while_time,
                cur_rec->post_data_record[idx].umq_post_tx_do_after_while_time
                );
            }
            cur_rec->pre_post_cnt = post_cnt;
        }
        if (poll_end - pre_poll_cnt > 0) {
            uint32_t idx = 0;
            UMQ_VLOG_INFO(VLOG_UMQ, "============thread %d poll: %d--%d ===========\n", k, pre_poll_cnt, poll_end);
            for (uint32_t i = pre_poll_cnt; i < poll_end; i++) {
                idx = i % DATA_RECORD_NUM;
                UMQ_VLOG_INFO(VLOG_UMQ, "user_data: %u, total_size: %u, jetty_id: %u, local_umq_id: %u, remote_umq_id: %u, timestamp: %lu, urma_poll_time: %lu, umq_poll_end: %lu\n",
                cur_rec->poll_data_record[idx].user_data,
                cur_rec->poll_data_record[idx].total_size,
                cur_rec->poll_data_record[idx].jetty_id, 
                cur_rec->poll_data_record[idx].local_umq_id, 
                cur_rec->poll_data_record[idx].remote_umq_id,
                cur_rec->poll_data_record[idx].timestamp, 
                cur_rec->poll_data_record[idx].urma_poll_time,
                cur_rec->poll_data_record[idx].umq_poll_end);
            }
            cur_rec->pre_poll_cnt = poll_cnt;
        }
    }
}

void umq_ub_data_print(void *args)
{
    if (!g_umq_print_flag) {
 	    return;
 	}
    for (uint32_t k = 0; k < UMQ_PERF_REC_MAX_NUM; ++k) { //遍历所有线程
        if (!g_umq_perf_data_ctx->data_table[k].is_used) {
            break;
        }
        umq_perf_data_t *cur_rec = &g_umq_perf_data_ctx->data_table[k];
        uint32_t post_cnt = cur_rec->post_cnt;
        uint32_t poll_cnt = cur_rec->poll_cnt;
        uint32_t pre_post_cnt = cur_rec->pre_post_cnt;
        uint32_t pre_poll_cnt = cur_rec->pre_poll_cnt;
        uint32_t post_end = post_cnt < pre_post_cnt? (post_cnt + DATA_RECORD_NUM): post_cnt;
        uint32_t poll_end = poll_cnt < pre_poll_cnt? (poll_cnt + DATA_RECORD_NUM): poll_cnt;
        if (post_end - pre_post_cnt > DATA_RECORD_LIMIT) {
            uint32_t idx = 0;
            UMQ_VLOG_INFO(VLOG_UMQ, "============thread %d post: %d--%d ===========\n", k, pre_post_cnt, post_end);
            for (uint32_t i = pre_post_cnt; i < post_end; i++) {
                idx = i % DATA_RECORD_NUM;
                UMQ_VLOG_INFO(VLOG_UMQ, "user_data: %u, total_size: %u, jetty_id: %u, local_umq_id: %u, remote_umq_id: %u, "
                "timestamp: %lu, fc_send_req: %lu, urma_post_start: %lu, urma_post_end: %lu, urma_post_time: %lu, umq_post_start: %lu, umq_post_tx_time: %lu, post_tx_while_time: %lu, post_tx_after_while_time: %lu\n",
                cur_rec->post_data_record[idx].user_data,
                cur_rec->post_data_record[idx].total_size,
                cur_rec->post_data_record[idx].jetty_id, 
                cur_rec->post_data_record[idx].local_umq_id, 
                cur_rec->post_data_record[idx].remote_umq_id,
                cur_rec->post_data_record[idx].timestamp, 
                cur_rec->post_data_record[idx].fc_send_req,
                cur_rec->post_data_record[idx].urma_post_start,
                cur_rec->post_data_record[idx].urma_post_end,
                cur_rec->post_data_record[idx].urma_post_time,
                cur_rec->post_data_record[idx].umq_post_start,
                cur_rec->post_data_record[idx].umq_post_tx_time,
                cur_rec->post_data_record[idx].umq_post_tx_do_while_time,
                cur_rec->post_data_record[idx].umq_post_tx_do_after_while_time
                );
            }
            cur_rec->pre_post_cnt = post_cnt;
        }
        if (poll_end - pre_poll_cnt > DATA_RECORD_LIMIT) {
            uint32_t idx = 0;
            UMQ_VLOG_INFO(VLOG_UMQ, "============thread %d poll: %d--%d ===========\n", k, pre_poll_cnt, poll_end);
            for (uint32_t i = pre_poll_cnt; i < poll_end; i++) {
                idx = i % DATA_RECORD_NUM;
                UMQ_VLOG_INFO(VLOG_UMQ, "user_data: %u, total_size: %u, jetty_id: %u, local_umq_id: %u, remote_umq_id: %u, timestamp: %lu, umq_poll_end: %lu, urma_poll_time: %lu\n",
                cur_rec->poll_data_record[idx].user_data,
                cur_rec->poll_data_record[idx].total_size,
                cur_rec->poll_data_record[idx].jetty_id, 
                cur_rec->poll_data_record[idx].local_umq_id, 
                cur_rec->poll_data_record[idx].remote_umq_id,
                cur_rec->poll_data_record[idx].timestamp, 
                cur_rec->poll_data_record[idx].umq_poll_end,
                cur_rec->poll_data_record[idx].urma_poll_time);
            }
            cur_rec->pre_poll_cnt = poll_cnt;
        }
    }
}

static int umq_ub_data_print_timer_create(void)
{
    urpc_timer_t *timer = urpc_timer_create(0xFFFFFFFF, false);
    if (URPC_UNLIKELY(timer == NULL)) {
        UMQ_VLOG_ERR(VLOG_UMQ, "umq timer create failed\n");
        return UMQ_FAIL;
    }
    void *args = NULL;
    int ret = urpc_timer_start(timer, 1, umq_ub_data_print, args, true);
    if (URPC_UNLIKELY(ret != UMQ_SUCCESS)) {
        goto TIMER_DESTROY;
    }
    return UMQ_SUCCESS;

TIMER_DESTROY:
    urpc_timer_destroy(timer);
    return UMQ_FAIL;
}

int umq_perf_start(void)
{
    // initialize perf at first start
    if (g_umq_perf_record_ctx == NULL) {
        int ret = umq_perf_init();
        if (ret != UMQ_SUCCESS) {
            UMQ_VLOG_ERR(VLOG_UMQ, "umq perf init failed\n");
            return ret;
        }
    }
    if (umq_ub_data_print_timer_create() != UMQ_SUCCESS) {
        UMQ_VLOG_ERR(VLOG_UMQ, "=========umq print data timer create failed\n");
        return -UMQ_ERR_EINVAL;
    }

    // IO perf record has been started, user must stop it first before restart
    if (g_umq_perf_record_enable) {
        UMQ_VLOG_ERR(VLOG_UMQ, "invalid parameter\n");
        return -UMQ_ERR_EINVAL;
    }

    g_umq_perf_record_enable = true;
    return UMQ_SUCCESS;
}

int umq_perf_reset(umq_perf_stats_cfg_t *perf_stats_cfg)
{
    // IO perf record has been started, user must stop it first before restart
    if (g_umq_perf_record_ctx == NULL || perf_stats_cfg == NULL) {
        UMQ_VLOG_ERR(VLOG_UMQ, "invalid parameter\n");
        return -UMQ_ERR_EINVAL;
    }

    for (uint32_t i = 0; i < UMQ_PERF_REC_MAX_NUM; ++i) {
        umq_clear_perf_record_item(i);
    }

    uint32_t thresh_num = perf_stats_cfg->thresh_num;
    if (thresh_num > UMQ_PERF_QUANTILE_MAX_NUM) {
        UMQ_VLOG_ERR(VLOG_UMQ, "configured thresh num %u exceeds the max thresh_num %u, only the minimum %d of them"
            "are used\n", thresh_num, UMQ_PERF_QUANTILE_MAX_NUM, UMQ_PERF_QUANTILE_MAX_NUM);
        return -UMQ_ERR_EAGAIN;
    }

    uint64_t *thresh_array = perf_stats_cfg->thresh_array;
    // set quantile bucket
    uint32_t idx = 0;
    for (uint32_t i = 0; i < thresh_num; ++i) {
        if (thresh_array[i] > UMQ_PERF_MAX_THRESH_NS || thresh_array[i] == 0) {
            continue;
        }
        if (idx == 0 || thresh_array[i] > g_umq_perf_record_ctx->perf_quantile_thresh[idx - 1]) {
            g_umq_perf_record_ctx->thresh_ns[idx] = thresh_array[i];
            g_umq_perf_record_ctx->perf_quantile_thresh[idx++] = thresh_array[i] * urpc_get_cpu_hz() / NS_PER_SEC;
        }
    }
    g_umq_perf_record_ctx->thresh_num = idx;

    return UMQ_SUCCESS;
}

int umq_perf_stop(void)
{
    if (g_umq_perf_record_ctx == NULL || !g_umq_perf_record_enable) {
        UMQ_VLOG_ERR(VLOG_UMQ, "invalid parameter\n");
        return -UMQ_ERR_EINVAL;
    }

    g_umq_perf_record_enable = false;
    return UMQ_SUCCESS;
}

static inline uint64_t cpu_cycles_to_ns(uint64_t cycles)
{
    // The CPU frequency is around X GHz, so dividing by CPU hz will solve the overflow issue.
    if (cycles != 0 && UINT64_MAX / cycles <= NS_PER_SEC) {
        return cycles / urpc_get_cpu_hz() * NS_PER_SEC;
    } else {
        return cycles * NS_PER_SEC / urpc_get_cpu_hz();
    }
}

static inline void umq_perf_convert_cycles_to_ns(umq_perf_record_t *perf_rec)
{
    for (int type = 0; type < UMQ_PERF_RECORD_TYPE_MAX; ++type) {
        perf_rec->type_record[type].accumulation = cpu_cycles_to_ns(perf_rec->type_record[type].accumulation);
        // min default value is inited as UINT64_MAX, we output it as 0 for readability
        perf_rec->type_record[type].min =
            perf_rec->type_record[type].min == UINT64_MAX ? 0 : cpu_cycles_to_ns(perf_rec->type_record[type].min);
        perf_rec->type_record[type].max = cpu_cycles_to_ns(perf_rec->type_record[type].max);
    }
}

static uint64_t umq_perf_cal_quantile(
    umq_perf_record_t *record, umq_perf_record_type_t type, uint64_t count, uint64_t *thresh, uint32_t thresh_num)
{
    if (thresh_num == 0) {
        return 0;
    }

    uint32_t idx;
    uint64_t quantile_cnt = count;
    for (idx = 0; idx < thresh_num; ++idx) {
        if (record->type_record[type].bucket[idx] >= quantile_cnt) {
            break;
        }
        quantile_cnt -= record->type_record[type].bucket[idx];
    }

    // the queried quantile cnt exceeds the maximum thresh records, return the max thresh
    if (idx >= thresh_num) {
        return thresh[thresh_num - 1];
    }

    if (record->type_record[type].bucket[idx] == 0) {
        return 0;
    }

    uint64_t base = (idx == 0) ? 0 : thresh[idx - 1];
    return ((double)quantile_cnt / record->type_record[type].bucket[idx]) * (thresh[idx] - base) + base;
}

static ALWAYS_INLINE void umq_perf_record_add(umq_perf_record_t *total_perf_record, umq_perf_record_t *perf_record)
{
    for (uint32_t i = 0; i < UMQ_PERF_RECORD_TYPE_MAX; i++) {
        if (perf_record->type_record[i].cnt == 0) {
            continue;
        }
        total_perf_record->type_record[i].accumulation += perf_record->type_record[i].accumulation;
        total_perf_record->type_record[i].min =
            (total_perf_record->type_record[i].min != 0 &&
            total_perf_record->type_record[i].min < perf_record->type_record[i].min) ?
            total_perf_record->type_record[i].min : perf_record->type_record[i].min;
        total_perf_record->type_record[i].max =
            total_perf_record->type_record[i].max > perf_record->type_record[i].max ?
            total_perf_record->type_record[i].max : perf_record->type_record[i].max;
        total_perf_record->type_record[i].cnt += perf_record->type_record[i].cnt;
        for (uint32_t j = 0; j <= UMQ_PERF_QUANTILE_MAX_NUM ; j++) {
            total_perf_record->type_record[i].bucket[j] += perf_record->type_record[i].bucket[j];
        }
    }
}

int umq_perf_info_get(umq_perf_stats_t *perf_info)
{
    if (g_umq_perf_record_ctx == NULL || perf_info == NULL) {
        UMQ_VLOG_ERR(VLOG_UMQ, "invalid parameter\n");
        return -UMQ_ERR_EINVAL;
    }

    (void)pthread_spin_lock(&g_umq_perf_record_lock);

    umq_perf_record_t total_perf_record = {0};
    for (uint32_t i = 0; i < UMQ_PERF_REC_MAX_NUM; ++i) {
        if (!g_umq_perf_record_ctx->perf_record_table[i].is_used) {
            continue;
        }

        umq_perf_record_add(&total_perf_record, &g_umq_perf_record_ctx->perf_record_table[i]);
    }
    umq_perf_convert_cycles_to_ns(&total_perf_record);

    uint64_t *thresh = g_umq_perf_record_ctx->thresh_ns;
    uint32_t thresh_num = g_umq_perf_record_ctx->thresh_num;
    for (uint32_t i = 0; i < UMQ_PERF_RECORD_TYPE_MAX; i++) {
        perf_info->type_record[i].type = i;
        perf_info->type_record[i].sample_num = total_perf_record.type_record[i].cnt;
        perf_info->type_record[i].average = total_perf_record.type_record[i].cnt != 0 ?
            (total_perf_record.type_record[i].accumulation / total_perf_record.type_record[i].cnt) : 0;
        perf_info->type_record[i].maxinum = total_perf_record.type_record[i].max;
        perf_info->type_record[i].mininum = total_perf_record.type_record[i].min;
        perf_info->type_record[i].median = umq_perf_cal_quantile(&total_perf_record, i,
            (uint64_t)(MEDIAN_FACTOR * total_perf_record.type_record[i].cnt), thresh, thresh_num);
        perf_info->type_record[i].p90 = umq_perf_cal_quantile(&total_perf_record, i,
            (uint64_t)(P90_FACTOR * total_perf_record.type_record[i].cnt), thresh, thresh_num);
        perf_info->type_record[i].p99 = umq_perf_cal_quantile(&total_perf_record, i,
            (uint64_t)(P99_FACTOR * total_perf_record.type_record[i].cnt), thresh, thresh_num);
    }

    (void)pthread_spin_unlock(&g_umq_perf_record_lock);
    return 0;
}
