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
#include <cstdlib>
#include <cstring>
#include <ostream>
#include <sstream>
#include <string>
#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_global_setting.h"
#include "profiling/impl/ubsocket_prof_tracer.h"
#include "profiling/impl/ubsocket_prof_tracer_ext.h"
#include "under_api/urma/dl_urma_api.h"

using namespace ock::ubs;
using namespace ock::ubs::profiling;

int ubsocket_prof_enabled = 0;
static int ubsocket_prof_mode_ext = 0; // 标记是否为扩展模式（从 GlobalSetting::UBS_PROF_MODE 读取）
uint64_t ubsocket_arm_cpu_freq = 1;

class ExtraProf {
public:
    virtual ~ExtraProf() = default;
    /* 初始化时启动统计 */
    virtual void Start() = 0;
    /* 停止统计、获取结果、重启统计；直接写入 stream，避免中间 buffer 拷贝 */
    virtual void GetInfo(std::ostream &os) = 0;
};

#ifdef URMA_DLOPEN_BACKEND_ENABLED
class UrmaExtraProf : public ExtraProf {
public:
    void Start() override
    {
        if (UrmaApi::IsLoaded()) {
            auto ret = UrmaApi::urma_start_perf();
            if (ret != URMA_SUCCESS) {
                UBS_VLOG_WARN("urma_start_perf failed, ret=%d", ret);
            }
        }
    }

    void GetInfo(std::ostream &os) override
    {
        if (!UrmaApi::IsLoaded()) {
            return;
        }
        auto ret = UrmaApi::urma_stop_perf();
        if (ret != URMA_SUCCESS) {
            UBS_VLOG_WARN("urma_stop_perf failed, ret=%d", ret);
            return;
        }
        constexpr uint32_t PERF_BUF_SIZE = 8192;
        char perf_buf[PERF_BUF_SIZE];
        uint32_t length = PERF_BUF_SIZE;
        ret = UrmaApi::urma_get_perf_info(perf_buf, &length);
        if (ret == URMA_SUCCESS && length > 0) {
            os.write(perf_buf, static_cast<std::streamsize>(length));
        } else {
            UBS_VLOG_WARN("urma_get_perf_info failed, ret=%d, len=%u", ret, length);
        }
        auto start_ret = UrmaApi::urma_start_perf();
        if (start_ret != URMA_SUCCESS) {
            UBS_VLOG_WARN("urma_start_perf restart failed, ret=%d", start_ret);
        }
    }
};
#endif

static ExtraProf *g_extra_prof = nullptr;

#if defined(ENABLE_CPU_MONOTONIC) && defined(__aarch64__)
namespace {
struct ArmCpuFreqInitializer {
    ArmCpuFreqInitializer() noexcept
    {
        uint64_t tmpFreq = 0;
        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(tmpFreq));
        tmpFreq = tmpFreq / 1000000UL; /* Hz -> MHz, keep ns = ticks * 1000 / freq_MHz */
        ubsocket_arm_cpu_freq = (tmpFreq == 0) ? 1 : tmpFreq;
    }
};
static ArmCpuFreqInitializer g_arm_cpu_freq_init;
} // namespace
#endif

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

    // 根据环境变量 UBS_PROF_MODE 决定初始化哪种模式
    ubsocket_prof_mode_ext = (GlobalSetting::UBS_PROF_MODE == "ext") ? 1 : 0;

    Result prof_res;
    if (ubsocket_prof_mode_ext == 1) {
        // 扩展模式：只初始化 TracerExt
        TracerOptionsExt optionsExt{};
        optionsExt.tracepoint_count = option->tracepoint_count;
        optionsExt.enable_dump = (option->enable_dump != 0);
        optionsExt.dump_path = option->dump_file_path != nullptr ? std::string(option->dump_file_path) : "";
        optionsExt.dump_interval_min = option->dump_interval_min;
        prof_res = TracerExt::Instance().InitExt(optionsExt);
        if (prof_res == UBS_OK) {
            ubsocket_prof_enabled = 1;
            UBS_VLOG_INFO("Ubsocket profiling init success (ext mode).");
        }
    } else {
        // 高性能模式：只初始化 Tracer
        TracerOptions options{};
        options.tracepoint_count = option->tracepoint_count;
        options.enable_dump = (option->enable_dump != 0);
        options.dump_path = option->dump_file_path != nullptr ? std::string(option->dump_file_path) : "";
        options.dump_interval_min = option->dump_interval_min;
        prof_res = Tracer::Instance().Init(options);
        if (prof_res == UBS_OK) {
            ubsocket_prof_enabled = 1;
            UBS_VLOG_INFO("Ubsocket profiling init success (fast mode).");
        }
    }

    delete g_extra_prof;
    g_extra_prof = nullptr;
#ifdef URMA_DLOPEN_BACKEND_ENABLED
    if (option->extra_prof_type == EXTRA_PROF_URMA) {
        g_extra_prof = new (std::nothrow) UrmaExtraProf();
        if (g_extra_prof != nullptr) {
            g_extra_prof->Start();
            UBS_VLOG_INFO("Extra prof (urma) started.");
        }
    }
#endif

    return prof_res;
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

    delete g_extra_prof;
    g_extra_prof = nullptr;
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
    int len;
    if (ubsocket_prof_mode_ext == 1) {
        // 扩展模式，输出百分位统计
        len = TracerExt::Instance().CombineExt(out_buf);
    } else {
        // 高性能模式，输出基础统计
        len = Tracer::Instance().Combine(out_buf);
    }
    if (len < 0 || out_buf == nullptr || *out_buf == nullptr) {
        return len;
    }

    /* 追加额外性能统计信息（如 urma 库统计），使用 realloc 避免 2 次额外 malloc+memcpy */
    if (g_extra_prof != nullptr) {
        std::ostringstream oss;
        g_extra_prof->GetInfo(oss);
        auto extra = oss.str();
        if (!extra.empty()) {
            size_t total_len = static_cast<size_t>(len) + 1 + extra.size();
            char *combined = static_cast<char *>(realloc(*out_buf, total_len));
            if (combined != nullptr) {
                combined[len] = '\n';
                memcpy(combined + len + 1, extra.data(), extra.size());
                *out_buf = combined;
                return static_cast<int>(total_len);
            }
        }
    }
    return len;
}

void ubsocket_prof_get_extra_info_stream(std::ostream &os)
{
    if (g_extra_prof != nullptr) {
        g_extra_prof->GetInfo(os);
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
