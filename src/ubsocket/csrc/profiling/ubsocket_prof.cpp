/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "ubsocket_prof.h"
#include <string>
#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_global_setting.h"
#include "profiling/impl/ubsocket_prof_tracer.h"
#include "profiling/impl/ubsocket_prof_tracer_ext.h"

using namespace ock::ubs;
using namespace ock::ubs::profiling;

int ubsocket_prof_enabled = 0;
static int ubsocket_prof_mode_ext = 0; // 标记是否为扩展模式（从 GlobalSetting::UBS_PROF_MODE 读取）
uint64_t ubsocket_arm_cpu_freq = 1;

int ubsocket_prof_init(ubsocket_prof_option_t *option)
{
    if (option == nullptr) {
        UBS_VLOG_ERR("Profiling init failed, as param 'option' is null");
        return -1;
    }

    if (option->tracepoint_count == 0) {
        UBS_VLOG_ERR("Profiling init failed, as param 'option.tracepoint_count' is 0");
        return -1;
    }

    if (option->enable_dump != 0) {
        if (option->dump_file_path == nullptr) {
            UBS_VLOG_ERR("Profiling init failed, as param 'option.dump_file_path' is null");
            return -1;
        }
    }

#if defined(ENABLE_CPU_MONOTONIC) && defined(__aarch64__)
    /* get frequ */
    uint64_t tmpFreq = 0;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(tmpFreq));

    /* calculate */
    tmpFreq = tmpFreq / 1000L / 1000L;
    if (tmpFreq == 0) {
        UBS_VLOG_ERR("Ubsocket prof get cpu freq failed for arm cpu");
        ubsocket_arm_cpu_freq = 1;
    } else {
        ubsocket_arm_cpu_freq = tmpFreq;
    }
#endif

    // 根据环境变量 UBSOCKET_PROF_MODE 决定初始化哪种模式
    ubsocket_prof_mode_ext = (GlobalSetting::UBS_PROF_MODE == "ext") ? 1 : 0;

    if (ubsocket_prof_mode_ext == 1) {
        // 扩展模式：只初始化 TracerExt
        TracerOptionsExt optionsExt{};
        optionsExt.tracepoint_count = option->tracepoint_count;
        optionsExt.enable_dump = (option->enable_dump != 0);
        optionsExt.dump_path = option->dump_file_path != nullptr ? std::string(option->dump_file_path) : "";
        optionsExt.dump_interval_min = option->dump_interval_min;
        auto res = TracerExt::Instance().InitExt(optionsExt);
        if (res == UBS_OK) {
            ubsocket_prof_enabled = 1;
            UBS_VLOG_INFO("Ubsocket profiling init success (ext mode).");
        }
        return res;
    } else {
        // 高性能模式：只初始化 Tracer
        TracerOptions options{};
        options.tracepoint_count = option->tracepoint_count;
        options.enable_dump = (option->enable_dump != 0);
        options.dump_path = option->dump_file_path != nullptr ? std::string(option->dump_file_path) : "";
        options.dump_interval_min = option->dump_interval_min;
        auto res = Tracer::Instance().Init(options);
        if (res == UBS_OK) {
            ubsocket_prof_enabled = 1;
            UBS_VLOG_INFO("Ubsocket profiling init success (fast mode).");
        }
        return res;
    }
}

int ubsocket_prof_uninit()
{
    ubsocket_prof_enabled = 0;
    if (ubsocket_prof_mode_ext == 1) {
        TracerExt::Instance().UnInitExt();
    } else {
        Tracer::Instance().UnInit();
    }
    ubsocket_prof_mode_ext = 0;
    return 0;
}

/* 打点接口 - 根据模式自动选择实现 */
int ubsocket_prof_record(uint32_t tracepoint_id, const char *tracepoint_name, uint64_t timestamp, bool good)
{
    if (ubsocket_prof_mode_ext == 1) {
        // 扩展模式：使用 TracerExt（支持百分位统计）
        return TracerExt::Instance().RecordExt(tracepoint_id, tracepoint_name, timestamp, good);
    } else {
        // 高性能模式：使用 Tracer（thread_local，无锁写入）
        return Tracer::Instance().Record(tracepoint_id, tracepoint_name, timestamp, good);
    }
}

int ubsocket_prof_combind(char **out_buf)
{
    if (ubsocket_prof_mode_ext == 1) {
        // 扩展模式，输出百分位统计
        return TracerExt::Instance().CombineExt(out_buf);
    } else {
        // 高性能模式，输出基础统计
        return Tracer::Instance().Combine(out_buf);
    }
}

void ubsocket_prof_reset()
{
    if (ubsocket_prof_mode_ext == 1) {
        TracerExt::Instance().ResetExt();
    } else {
        Tracer::Instance().Reset();
    }
}
