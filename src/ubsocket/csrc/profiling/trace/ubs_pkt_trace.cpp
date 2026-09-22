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

#include "ubs_pkt_trace.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace ock {
namespace ubs {
namespace {

constexpr uint32_t DEFAULT_RING_CAP = 1 << 16;  /* per-thread records, power of two */
constexpr uint32_t FLUSH_INTERVAL_MS = 10;
constexpr const char *DEFAULT_DIR = "/tmp/ubsocket";
constexpr const char *FILE_PREFIX = "/ubs_pkt_trace_";

/* Per-thread single-producer ring; the flush thread is the only consumer.
 * Overflow drops oldest by advancing tail (a lost record only removes one
 * sample, never corrupts others) -- acceptable for a diagnostic trace. */
struct Ring {
    std::vector<UbsPktTraceRecord> buf;
    uint32_t cap_mask = 0;
    std::atomic<uint64_t> head{0};
    std::atomic<uint64_t> tail{0};
};

std::mutex g_rings_mu;
std::vector<Ring *> g_rings;
std::atomic<bool> g_started{false};
std::atomic<bool> g_stop{false};
pthread_t g_flush_tid;
std::atomic<int> g_enabled{-1};  /* -1 unknown, 0 off, 1 on */

bool ReadEnableEnv()
{
    const char *v = getenv("UBS_PKT_TRACE_ENABLE");
    return (v != nullptr && (strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0));
}

Ring *ThreadRing()
{
    static thread_local Ring *tl_ring = nullptr;
    if (tl_ring != nullptr) {
        return tl_ring;
    }
    Ring *r = new Ring();
    r->buf.resize(DEFAULT_RING_CAP);
    r->cap_mask = DEFAULT_RING_CAP - 1;
    {
        std::lock_guard<std::mutex> lk(g_rings_mu);
        g_rings.push_back(r);
    }
    tl_ring = r;
    return r;
}

inline void Push(const UbsPktTraceRecord &rec)
{
    Ring *r = ThreadRing();
    const uint64_t h = r->head.load(std::memory_order_relaxed);
    r->buf[h & r->cap_mask] = rec;
    r->head.store(h + 1, std::memory_order_release);
    const uint64_t t = r->tail.load(std::memory_order_acquire);
    if (h + 1 - t > (uint64_t)(r->cap_mask + 1)) {
        r->tail.store(h + 1 - (uint64_t)(r->cap_mask + 1), std::memory_order_release);
    }
}

std::string LogPath()
{
    char buf[256];
    snprintf(buf, sizeof(buf), "%s%s%d.log", DEFAULT_DIR, FILE_PREFIX, (int)getpid());
    return std::string(buf);
}

void EnsureDir()
{
    /* mkdir -p DEFAULT_DIR (0750); tolerate EEXIST. */
    std::string cur;
    for (char c : std::string(DEFAULT_DIR)) {
        cur += c;
        if (c == '/') {
            if (mkdir(cur.c_str(), 0750) == -1 && errno != EEXIST) {
                return;
            }
        }
    }
    (void)mkdir(DEFAULT_DIR, 0750);
}

void FlushAll(FILE *fp)
{
    std::vector<Ring *> rings;
    {
        std::lock_guard<std::mutex> lk(g_rings_mu);
        rings = g_rings;
    }
    for (Ring *r : rings) {
        const uint64_t h = r->head.load(std::memory_order_acquire);
        const uint64_t t0 = r->tail.load(std::memory_order_relaxed);
        for (uint64_t t = t0; t < h; ++t) {
            const UbsPktTraceRecord &rec = r->buf[t & r->cap_mask];
            if (rec.stage_id == STAGE_PKT_DELIVERY) {
                /* P row: fd  sn  byte_cursor  ts_ns */
                fprintf(fp, "%d\t%u\t%llu\t%llu\n", rec.fd, rec.sn,
                        (unsigned long long)rec.byte_cursor,
                        (unsigned long long)rec.ts_ns);
            } else {
                /* S row: S  stage_id  fd  first_sn  ts_ns  sn_count
                 * (sn_count appended as an optional 6th column; 0 = single-SN
                 * event, >0 = range event covering [sn, sn+sn_count). 5-column
                 * readers stay compatible.) */
                fprintf(fp, "S\t%u\t%d\t%u\t%llu\t%u\n", (unsigned)rec.stage_id, rec.fd, rec.sn,
                        (unsigned long long)rec.ts_ns, (unsigned)rec.sn_count);
            }
        }
        r->tail.store(h, std::memory_order_release);
    }
}

void *FlushThreadMain(void *)
{
    EnsureDir();
    FILE *fp = fopen(LogPath().c_str(), "a");
    if (fp == nullptr) {
        g_started.store(false);
        return nullptr;
    }
    while (!g_stop.load(std::memory_order_acquire)) {
        FlushAll(fp);
        fflush(fp);
        usleep(FLUSH_INTERVAL_MS * 1000);
    }
    FlushAll(fp);
    fclose(fp);
    return nullptr;
}

} // namespace

bool UbsPktTraceEnabled()
{
    int e = g_enabled.load(std::memory_order_acquire);
    if (e < 0) {
        e = ReadEnableEnv() ? 1 : 0;
        g_enabled.store(e, std::memory_order_release);
    }
    return e == 1;
}

void UbsPktTrace(int32_t fd, uint32_t sn, uint64_t byte_cursor, uint64_t ts_ns)
{
    if (!UbsPktTraceEnabled()) {
        return;
    }
    UbsPktTraceRecord rec;
    rec.fd = fd;
    rec.sn = sn;
    rec.byte_cursor = byte_cursor;
    rec.ts_ns = ts_ns;
    rec.stage_id = STAGE_PKT_DELIVERY;
    rec.sn_count = 0;
    Push(rec);
}

void UbsStageTrace(int32_t fd, uint32_t first_sn, uint8_t stage_id, uint64_t ts_ns, uint32_t sn_count)
{
    if (!UbsPktTraceEnabled()) {
        return;
    }
    UbsPktTraceRecord rec;
    rec.fd = fd;
    rec.sn = first_sn;
    rec.byte_cursor = 0;
    rec.ts_ns = ts_ns;
    rec.stage_id = stage_id;
    rec.sn_count = sn_count;
    Push(rec);
}

void UbsPktTraceStart()
{
    if (!UbsPktTraceEnabled()) {
        return;
    }
    bool expected = false;
    if (!g_started.compare_exchange_strong(expected, true)) {
        return;
    }
    g_stop.store(false);
    if (pthread_create(&g_flush_tid, nullptr, FlushThreadMain, nullptr) != 0) {
        g_started.store(false);
        return;
    }
    pthread_detach(g_flush_tid);
}

void UbsPktTraceStop()
{
    if (!g_started.load()) {
        return;
    }
    g_stop.store(true);
    /* detached thread exits after the current sleep window; the final
     * FlushAll inside FlushThreadMain drains remaining records. */
}

} // namespace ubs
} // namespace ock
