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
#ifndef HCOM_SERVICE_V2_SERVICE_TIMER_TRACE_H_
#define HCOM_SERVICE_V2_SERVICE_TIMER_TRACE_H_

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>

#include "net_common.h"
#include "securec.h"

namespace ock {
namespace hcom {

/*
 * [TIMER-TRACE] HcomServiceTimer 生命周期打点。
 *
 * 背景：`ServiceContextTimer-<service>` 内存池出现 [MEMPOOL-LEAK]（outstanding 块单调增长），
 * 说明 timer 对象从池中取出后再也没有归还。timer 的引用计数有三个持有者：
 *   ① 调用方        —— PrepareTimerContext 加，响应命中 / 发送失败 / 超时触发时减
 *   ② 通道链表      —— AddTimerCtx 加，RemoveTimerCtx 减（只由周期线程调用）
 *   ③ 超时队列      —— PrepareTimerContext 加，ProcessTimeOut 收集到该 timer 时减
 * 只要 ① 没被释放，timer 既不会 IsFinished()，也就永远不会被周期线程收集，②③ 随之泄漏，
 * 对象永远不会 Return 回内存池。因此定位泄漏的关键，是看清"分配了多少 / 通过哪条路径释放"。
 *
 * 这些计数按事件分类累计，既有每通道（ctx store）视角，也有进程级汇总视角：
 *   alloc  == return                → 没有泄漏
 *   alloc  >  return，且 resp-miss 持续增长  → 响应未命中 seqNo，泄漏在响应派发路径
 *   alloc  >  return，且 resp-miss 为 0      → 响应根本没进入 isResp 分支（派发路径不对）
 *   timeout-fired 一直为 0                   → 超时兜底没生效（never-timeout 计数会同时增长）
 *
 * 开关：环境变量 HCOM_TIMER_TRACE=0 可完全关闭（默认开启，代价是热路径上几个 relaxed 原子加）。
 */
/*
 * 诊断构建版本号：每次重编自动用编译时间戳（格式 YYYYMMDD-HHMMSS）刷新，无需手动维护。
 * 由编译器内置宏 __DATE__ / __TIME__ 在编译期生成，clean build 即生效；
 * 增量编译时若本文件未重新编译，时间戳沿用旧值（故务必 clean build 后再部署）。
 * 通过 [VERSION] 横幅与 [TIMER-TRACE] census 的 build= 字段打印，用于确认线上运行的
 * libhcom.so 与本次源码一致 —— 排查泄漏时第一件事就是核对该时间戳。
 */
inline const char *HcomTimerDiagVersion()
{
    /* __DATE__ == "Mmm dd yyyy"（日不足两位时前导空格），__TIME__ == "HH:MM:SS"；
       二者均为编译期常量，每次重编自动刷新，无需手动改版本号。 */
    static const std::string ver = []() {
        const char *d = __DATE__;
        const char *t = __TIME__;
        int month = 0;
        switch (d[0]) {
            case 'J':
                month = (d[1] == 'a') ? 1 : (d[2] == 'n' ? 6 : 7);
                break; /* Jan / Jun / Jul */
            case 'F':
                month = 2;
                break; /* Feb */
            case 'M':
                month = (d[2] == 'r') ? 3 : 5;
                break; /* Mar / May */
            case 'A':
                month = (d[1] == 'p') ? 4 : 8;
                break; /* Apr / Aug */
            case 'S':
                month = 9;
                break; /* Sep */
            case 'O':
                month = 10;
                break; /* Oct */
            case 'N':
                month = 11;
                break; /* Nov */
            case 'D':
                month = 12;
                break; /* Dec */
            default:
                break;
        }
        const int day = ((d[4] >= '0' && d[4] <= '9') ? (d[4] - '0') * 10 : 0) + (d[5] - '0');
        const int year = (d[7] - '0') * 1000 + (d[8] - '0') * 100 + (d[9] - '0') * 10 + (d[10] - '0');
        char buf[32] = {0};
        /* G.FUU.21：snprintf 替换为安全函数 snprintf_s；count 取 destMax-1，保证末尾留 '\0' */
        (void)snprintf_s(buf, sizeof(buf), sizeof(buf) - 1, "%04d%02d%02d-%c%c%c%c%c%c", year, month, day, t[0], t[1],
                         t[3], t[4], t[6], t[7]);
        return std::string(buf);
    }();
    return ver.c_str();
}

enum class HcomTimerEvent : uint32_t
{
    ALLOC = 0,       /* 从 ctx 内存池取出一个 timer 对象 */
    RETURN,          /* 引用计数归零，timer 对象归还内存池 */
    SEQ_PUT,         /* seqNo 注册成功，timer 挂入 ctx store */
    SEQ_FAIL,        /* seqNo 注册失败（flat 满 + hash 冲突） */
    NEVER_TIMEOUT,   /* 创建时 timeout < 0，该 timer 永不超时，超时兜底对它无效 */
    ADD_TIMER_FAIL,  /* 加入周期超时队列失败 */
    SEND_FAIL,       /* 发送失败，DestroyTimerContext 主动清理 */
    RESP_HIT,        /* 收到响应且命中 seqNo，正常释放调用方引用 ① */
    RESP_MISS,       /* 收到响应但 seqNo 未命中 —— 首要泄漏嫌疑点 */
    POSTED_HIT,      /* 发送完成回调命中 seqNo（RunRequestCallback 路径） */
    POSTED_MISS,     /* 发送完成回调未命中 seqNo */
    FRAG_HIT,        /* 分片消息拼接完成后命中 seqNo */
    TIMEOUT_COLLECT, /* 周期线程收集到一个 timer（IsFinished || IsTimeOut） */
    TIMEOUT_FIRED,   /* 周期线程真正触发了超时回调（EraseSeqNoWithRet 成功） */
    TIMEOUT_NULL_CB, /* 周期线程发现 callback 为空，已跳过（否则解引用空指针崩溃） */
    STOP_COLLECT,    /* 服务停止时 ProcessCleanUp 回收 */
    EVENT_COUNT
};

constexpr uint32_t HCOM_TIMER_EVENT_NUM = static_cast<uint32_t>(HcomTimerEvent::EVENT_COUNT);

inline const char *HcomTimerEventName(uint32_t index)
{
    static const char *const names[HCOM_TIMER_EVENT_NUM] = {
        "alloc",       "return",    "seq-put",     "seq-fail",    "never-timeout", "add-timer-fail",
        "send-fail",   "resp-hit",  "resp-miss",   "posted-hit",  "posted-miss",   "frag-hit",
        "tmo-collect", "tmo-fired", "tmo-null-cb", "stop-collect"};
    return (index < HCOM_TIMER_EVENT_NUM) ? names[index] : "unknown";
}

/*
 * @brief 一组按事件分类的累计计数器，可被多线程并发累加（relaxed，只求最终一致的量级）
 */
class HcomTimerTraceCounters {
public:
    HcomTimerTraceCounters()
    {
        for (uint32_t i = 0; i < HCOM_TIMER_EVENT_NUM; i++) {
            mCounter[i].store(0, std::memory_order_relaxed);
        }
    }

    inline void Mark(HcomTimerEvent event)
    {
        mCounter[static_cast<uint32_t>(event)].fetch_add(1, std::memory_order_relaxed);
    }

    inline uint64_t Get(HcomTimerEvent event) const
    {
        return mCounter[static_cast<uint32_t>(event)].load(std::memory_order_relaxed);
    }

    /* 存活 timer 数 = 取出 - 归还，正常应在 0 附近抖动；持续单调增长即为泄漏 */
    inline int64_t LiveTimer() const
    {
        return static_cast<int64_t>(Get(HcomTimerEvent::ALLOC)) - static_cast<int64_t>(Get(HcomTimerEvent::RETURN));
    }

    /* 拼成一行文本，值为 0 的事件省略，避免日志过长 */
    std::string ToString() const
    {
        std::ostringstream oss;
        for (uint32_t i = 0; i < HCOM_TIMER_EVENT_NUM; i++) {
            const uint64_t value = mCounter[i].load(std::memory_order_relaxed);
            if (value != 0) {
                oss << HcomTimerEventName(i) << " " << value << ", ";
            }
        }
        oss << "live-timer " << LiveTimer();
        return oss.str();
    }

private:
    std::atomic<uint64_t> mCounter[HCOM_TIMER_EVENT_NUM];
};

/*
 * @brief 打点的全局配置与进程级汇总计数
 *
 * 所有配置都用函数内 static 缓存，环境变量只在第一次调用时读一次，热路径上只有一次分支判断。
 */
class HcomTimerTrace {
public:
    /* 打点总开关，HCOM_TIMER_TRACE=0 关闭（默认开启） */
    static inline bool Enabled()
    {
        static const bool enabled = (NetFunc::NN_GetLongEnv("HCOM_TIMER_TRACE", 0, 1, 1) == 1);
        return enabled;
    }

    /* census 打印周期（秒），HCOM_TIMER_TRACE_INTERVAL_SEC，默认 60 */
    static inline uint64_t IntervalSec()
    {
        static const uint64_t interval =
            static_cast<uint64_t>(NetFunc::NN_GetLongEnv("HCOM_TIMER_TRACE_INTERVAL_SEC", 1, 86400, 60));
        return interval;
    }

    /* 单通道在途 seqNo 超过该阈值时按 WARN 打印，HCOM_TIMER_TRACE_INFLIGHT_WARN，默认 1024 */
    static inline int64_t InflightWarn()
    {
        static const int64_t threshold =
            static_cast<int64_t>(NetFunc::NN_GetLongEnv("HCOM_TIMER_TRACE_INFLIGHT_WARN", 1, INT32_MAX, 1024));
        return threshold;
    }

    /* 未命中响应的明细日志采样间隔，HCOM_TIMER_TRACE_MISS_SAMPLE，默认每 1024 次打一条 */
    static inline uint64_t MissSample()
    {
        static const uint64_t sample =
            static_cast<uint64_t>(NetFunc::NN_GetLongEnv("HCOM_TIMER_TRACE_MISS_SAMPLE", 1, INT32_MAX, 1024));
        return sample;
    }

    /*
     * 进程级汇总计数。析构函数是平凡的（只是一组 atomic），函数内 static 在进程退出后
     * 被访问也不会有未定义行为，无需 leaky 单例。
     */
    static inline HcomTimerTraceCounters &Global()
    {
        static HcomTimerTraceCounters counters;
        return counters;
    }
};
} // namespace hcom
} // namespace ock

#endif // HCOM_SERVICE_V2_SERVICE_TIMER_TRACE_H_
