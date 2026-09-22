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

#include <sched.h>
#include <cerrno>
#include <cstdlib>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/syscall.h>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "ubsocket_prof_tracer_ext.h"

using namespace ock::ubs;
using namespace ock::ubs::profiling;

namespace {

static const uint32_t TEST_TP_COUNT = 16;
static const uint32_t TEST_TP_COUNT_TOO_LARGE = 1001;
static const uint32_t TEST_TP_ID_0 = 0;
static const uint32_t TEST_TP_ID_1 = 1;
static const uint32_t TEST_TP_ID_2 = 2;
static const uint32_t TEST_TP_ID_3 = 3;
static const char *TEST_TP_NAME_0 = "tp0";
static const char *TEST_TP_NAME_1 = "tp1";
static const char *TEST_TP_NAME_3 = "tp3";
static const char *TEST_DUMP_PATH = "/tmp/ubsocket/unitest/profiling_ext";
static const char *TEST_DUMP_PATH_2 = "/tmp/ubsocket/unitest/profiling_ext2";
static const pid_t TEST_MOCK_PID = 1234;

static int g_mockCpuId = 0;

static int MockSchedGetCpu()
{
    return g_mockCpuId;
}

static int MockMkdirFail(const char *path, mode_t mode)
{
    (void)path;
    (void)mode;
    errno = EACCES;
    return -1;
}

static int MockMkdirReal(const char *path, mode_t mode)
{
    if (::syscall(SYS_mkdirat, AT_FDCWD, path, static_cast<mode_t>(mode)) != 0) {
        return -1;
    }
    return 0;
}

static int MockMkdirEexist(const char *path, mode_t mode)
{
    (void)path;
    (void)mode;
    errno = EEXIST;
    return -1;
}

static int MockMkdirPrefixOkFinalFail(const char *path, mode_t mode)
{
    (void)mode;
    std::string p(path);
    if (p == "/" || p == "/tmp/") {
        errno = EEXIST;
        return -1;
    }
    errno = EACCES;
    return -1;
}

static pid_t MockForkParent()
{
    return TEST_MOCK_PID;
}

static pid_t MockWaitpidFail(pid_t pid, int *status, int options)
{
    (void)pid;
    (void)status;
    (void)options;
    errno = ECHILD;
    return -1;
}

static pid_t MockWaitpidExitZero(pid_t pid, int *status, int options)
{
    (void)pid;
    (void)options;
    *status = 0;
    return 0;
}

static pid_t MockWaitpidExitNonZero(pid_t pid, int *status, int options)
{
    (void)pid;
    (void)options;
    *status = 1 << 8;
    return 0;
}

static int MockStatLarge(const char *path, struct stat *st)
{
    (void)path;
    st->st_size = DUMP_FILE_MAX_SIZE_EXT + 1024;
    st->st_mtime = 0;
    return 0;
}

static void InitTracer(uint32_t tpCount, bool enableDump = false)
{
    TracerOptionsExt options;
    options.tracepoint_count = tpCount;
    options.enable_dump = enableDump;
    options.dump_interval_min = 1;
    options.dump_path = TEST_DUMP_PATH;
    (void)TracerExt::Instance().InitExt(options);
}

static void ResetTracerExt()
{
    TracerExt &tracer = TracerExt::Instance();
    tracer.UnInitExt();
    for (size_t i = 0; i < MAX_CPU_AGENTS_EXT; i++) {
        TraceGroupExt *agent = tracer.agents_[i].load();
        if (agent != nullptr) {
            agent->DecreaseRef();
            tracer.agents_[i].store(nullptr);
        }
    }
    tracer.trace_combiner_ = nullptr;
    tracer.dump_thread_ = nullptr;
    tracer.inited_ = false;
    tracer.options_ = TracerOptionsExt{};
}

class TracerExtTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        ResetTracerExt();
        g_mockCpuId = 0;
        MOCKER_CPP(::sched_getcpu).stubs().will(invoke(&MockSchedGetCpu));
    }

    void TearDown() override
    {
        ResetTracerExt();
        GlobalMockObject::verify();
        errno = 0;
    }
};

TEST_F(TracerExtTest, InitExt_AlreadyInitialized_ReturnsOk)
{
    InitTracer(TEST_TP_COUNT);
    TracerOptionsExt options;
    options.tracepoint_count = TEST_TP_COUNT;
    ock::ubs::Result ret = TracerExt::Instance().InitExt(options);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_TRUE(TracerExt::Instance().inited_);
}

TEST_F(TracerExtTest, InitExt_TracepointCountTooLarge_ReturnsInvalidParam)
{
    TracerOptionsExt options;
    options.tracepoint_count = TEST_TP_COUNT_TOO_LARGE;
    ock::ubs::Result ret = TracerExt::Instance().InitExt(options);
    EXPECT_EQ(ret, UBS_INVALID_PARAM);
    EXPECT_FALSE(TracerExt::Instance().inited_);
}

TEST_F(TracerExtTest, InitExt_ValidOptions_ReturnsOk)
{
    TracerOptionsExt options;
    options.tracepoint_count = TEST_TP_COUNT;
    ock::ubs::Result ret = TracerExt::Instance().InitExt(options);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_TRUE(TracerExt::Instance().inited_);
    EXPECT_NE(TracerExt::Instance().trace_combiner_.Get(), nullptr);
}

TEST_F(TracerExtTest, InitExt_EnableDump_CreatesAndStopsDumpThread)
{
    TracerOptionsExt options;
    options.tracepoint_count = TEST_TP_COUNT;
    options.enable_dump = true;
    options.dump_interval_min = 1;
    options.dump_path = TEST_DUMP_PATH;

    TracerExt &tracer = TracerExt::Instance();
    ock::ubs::Result ret = tracer.InitExt(options);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_NE(tracer.dump_thread_.Get(), nullptr);

    tracer.UnInitExt();
    EXPECT_EQ(tracer.dump_thread_.Get(), nullptr);
    EXPECT_FALSE(tracer.inited_);
}

TEST_F(TracerExtTest, UnInitExt_NotInitialized_ReturnsEarly)
{
    TracerExt &tracer = TracerExt::Instance();
    tracer.UnInitExt();
    EXPECT_FALSE(tracer.inited_);
}

TEST_F(TracerExtTest, UnInitExt_Initialized_ClearsAgentsAndDumpThread)
{
    InitTracer(TEST_TP_COUNT, true);
    TracerExt &tracer = TracerExt::Instance();
    tracer.CreateAgentExt(0);
    ASSERT_NE(tracer.agents_[0].load(), nullptr);
    tracer.agents_[0].load()->RecordExt(TEST_TP_ID_0, TEST_TP_NAME_0, 100, true);

    tracer.UnInitExt();
    EXPECT_FALSE(tracer.inited_);
    EXPECT_EQ(tracer.dump_thread_.Get(), nullptr);
    EXPECT_EQ(tracer.agents_[0].load(), nullptr);
}

TEST_F(TracerExtTest, CreateAgentExt_AgentAlreadyExists_ReturnsOk)
{
    InitTracer(TEST_TP_COUNT);
    TracerExt &tracer = TracerExt::Instance();
    ock::ubs::Result ret = tracer.CreateAgentExt(0);
    EXPECT_EQ(ret, UBS_OK);
    ASSERT_NE(tracer.agents_[0].load(), nullptr);

    ret = tracer.CreateAgentExt(0);
    EXPECT_EQ(ret, UBS_OK);
}

TEST_F(TracerExtTest, CreateAgentExt_NotInitialized_ReturnsError)
{
    TracerExt &tracer = TracerExt::Instance();
    ock::ubs::Result ret = tracer.CreateAgentExt(0);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(tracer.agents_[0].load(), nullptr);
}

TEST_F(TracerExtTest, CreateAgentExt_ValidCpuId_CreatesAndStoresAgent)
{
    InitTracer(TEST_TP_COUNT);
    TracerExt &tracer = TracerExt::Instance();
    ock::ubs::Result ret = tracer.CreateAgentExt(0);
    EXPECT_EQ(ret, UBS_OK);
    ASSERT_NE(tracer.agents_[0].load(), nullptr);
}

TEST_F(TracerExtTest, RecordExt_CpuIdNegative_ReturnsError)
{
    InitTracer(TEST_TP_COUNT);
    g_mockCpuId = -1;
    int ret = TracerExt::Instance().RecordExt(TEST_TP_ID_0, TEST_TP_NAME_0, 100, true);
    EXPECT_EQ(ret, UBS_ERROR);
}

TEST_F(TracerExtTest, RecordExt_CpuIdOutOfRange_ReturnsError)
{
    InitTracer(TEST_TP_COUNT);
    g_mockCpuId = static_cast<int>(MAX_CPU_AGENTS_EXT);
    int ret = TracerExt::Instance().RecordExt(TEST_TP_ID_0, TEST_TP_NAME_0, 100, true);
    EXPECT_EQ(ret, UBS_ERROR);
}

TEST_F(TracerExtTest, RecordExt_NotInitialized_ReturnsError)
{
    g_mockCpuId = 0;
    int ret = TracerExt::Instance().RecordExt(TEST_TP_ID_0, TEST_TP_NAME_0, 100, true);
    EXPECT_EQ(ret, UBS_ERROR);
}

TEST_F(TracerExtTest, RecordExt_ValidRecord_CreatesAgentAndStoresData)
{
    InitTracer(TEST_TP_COUNT);
    g_mockCpuId = 0;
    TracerExt &tracer = TracerExt::Instance();

    int ret = tracer.RecordExt(TEST_TP_ID_0, TEST_TP_NAME_0, 100, true);
    EXPECT_EQ(ret, UBS_OK);
    ASSERT_NE(tracer.agents_[0].load(), nullptr);
    EXPECT_EQ(tracer.agents_[0].load()->GetTracepointExt(TEST_TP_ID_0).data.success_count, 1);

    ret = tracer.RecordExt(TEST_TP_ID_0, TEST_TP_NAME_0, 50, true);
    EXPECT_EQ(ret, UBS_OK);
    TracepointExt tp0 = tracer.agents_[0].load()->GetTracepointExt(TEST_TP_ID_0);
    EXPECT_EQ(tp0.data.success_count, 2);
    EXPECT_EQ(tp0.data.total_time, 150);
    EXPECT_EQ(tp0.data.min_time, 50);
    EXPECT_EQ(tp0.data.max_time, 100);
}

TEST_F(TracerExtTest, RecordExt_InvalidTpId_ReturnsError)
{
    InitTracer(TEST_TP_COUNT);
    g_mockCpuId = 0;
    int ret = TracerExt::Instance().RecordExt(TEST_TP_COUNT, TEST_TP_NAME_0, 100, true);
    EXPECT_EQ(ret, UBS_ERROR);
}

TEST_F(TracerExtTest, RecordExt_NullTpName_ReturnsError)
{
    InitTracer(TEST_TP_COUNT);
    g_mockCpuId = 0;
    int ret = TracerExt::Instance().RecordExt(TEST_TP_ID_0, nullptr, 100, true);
    EXPECT_EQ(ret, UBS_ERROR);
}

TEST_F(TracerExtTest, CombineExt_NoAgents_ReturnsOkWithFreshOut)
{
    InitTracer(TEST_TP_COUNT);
    TracerExt &tracer = TracerExt::Instance();
    TraceGroupExtPtr out;
    int ret = tracer.CombineExt(out);
    EXPECT_EQ(ret, 0);
    ASSERT_NE(out.Get(), nullptr);
    EXPECT_EQ(out->points_.size(), TEST_TP_COUNT);
}

TEST_F(TracerExtTest, CombineExt_WithAgents_AggregatesStatsAndName)
{
    InitTracer(TEST_TP_COUNT);
    TracerExt &tracer = TracerExt::Instance();

    ock::ubs::Result ret = tracer.CreateAgentExt(0);
    EXPECT_EQ(ret, UBS_OK);
    ret = tracer.CreateAgentExt(1);
    EXPECT_EQ(ret, UBS_OK);
    TraceGroupExt *agent0 = tracer.agents_[0].load();
    TraceGroupExt *agent1 = tracer.agents_[1].load();
    ASSERT_NE(agent0, nullptr);
    ASSERT_NE(agent1, nullptr);

    agent0->RecordExt(TEST_TP_ID_0, TEST_TP_NAME_0, 100, true);
    agent0->RecordExt(TEST_TP_ID_1, TEST_TP_NAME_1, 200, false);
    agent1->RecordExt(TEST_TP_ID_0, TEST_TP_NAME_0, 300, true);
    agent1->RecordExt(TEST_TP_ID_3, TEST_TP_NAME_3, 50, true);
    agent0->points_[TEST_TP_ID_2].data.failure_count = 7;

    TraceGroupExtPtr out;
    int combineRet = tracer.CombineExt(out);
    EXPECT_EQ(combineRet, 0);
    ASSERT_NE(out.Get(), nullptr);

    TracepointExt tp0 = out->GetTracepointExt(TEST_TP_ID_0);
    EXPECT_EQ(tp0.has_name, 1);
    EXPECT_STREQ(tp0.name, TEST_TP_NAME_0);
    EXPECT_EQ(tp0.data.success_count, 2);
    EXPECT_EQ(tp0.data.failure_count, 0);
    EXPECT_EQ(tp0.data.total_time, 400);
    EXPECT_EQ(tp0.data.max_time, 300);
    EXPECT_EQ(tp0.data.min_time, 100);
    EXPECT_EQ(tp0.data.reservoir_count, 2);

    TracepointExt tp1 = out->GetTracepointExt(TEST_TP_ID_1);
    EXPECT_EQ(tp1.data.success_count, 0);
    EXPECT_EQ(tp1.data.failure_count, 1);

    TracepointExt tp2 = out->GetTracepointExt(TEST_TP_ID_2);
    EXPECT_EQ(tp2.has_name, 0);
    EXPECT_EQ(tp2.data.failure_count, 7);

    TracepointExt tp3 = out->GetTracepointExt(TEST_TP_ID_3);
    EXPECT_EQ(tp3.has_name, 1);
    EXPECT_STREQ(tp3.name, TEST_TP_NAME_3);
    EXPECT_EQ(tp3.data.success_count, 1);
}

TEST_F(TracerExtTest, CombineExt_ReservoirDirectCopy_WhenSamplesWithinCapacity)
{
    InitTracer(TEST_TP_COUNT);
    TracerExt &tracer = TracerExt::Instance();
    tracer.CreateAgentExt(0);
    TraceGroupExt *agent = tracer.agents_[0].load();
    ASSERT_NE(agent, nullptr);

    const size_t sampleCount = RESERVOIR_SIZE_EXT - 1;
    for (size_t i = 0; i < sampleCount; i++) {
        agent->RecordExt(TEST_TP_ID_0, TEST_TP_NAME_0, static_cast<uint64_t>(i), true);
    }

    TraceGroupExtPtr out;
    int ret = tracer.CombineExt(out);
    EXPECT_EQ(ret, 0);
    TracepointExt tp0 = out->GetTracepointExt(TEST_TP_ID_0);
    EXPECT_EQ(tp0.data.reservoir_count, sampleCount);
    EXPECT_EQ(tp0.data.success_count, sampleCount);
    for (size_t i = 0; i < sampleCount; i++) {
        EXPECT_EQ(tp0.data.reservoir[i], i);
    }
}

TEST_F(TracerExtTest, CombineExt_ReservoirRandomSample_WhenSamplesExceedCapacity)
{
    InitTracer(TEST_TP_COUNT);
    TracerExt &tracer = TracerExt::Instance();
    tracer.CreateAgentExt(0);
    tracer.CreateAgentExt(1);
    TraceGroupExt *agent0 = tracer.agents_[0].load();
    TraceGroupExt *agent1 = tracer.agents_[1].load();
    ASSERT_NE(agent0, nullptr);
    ASSERT_NE(agent1, nullptr);

    for (size_t i = 0; i < RESERVOIR_SIZE_EXT; i++) {
        agent0->RecordExt(TEST_TP_ID_0, TEST_TP_NAME_0, static_cast<uint64_t>(i), true);
        agent1->RecordExt(TEST_TP_ID_0, TEST_TP_NAME_0, static_cast<uint64_t>(RESERVOIR_SIZE_EXT + i), true);
    }

    TraceGroupExtPtr out;
    int ret = tracer.CombineExt(out);
    EXPECT_EQ(ret, 0);
    TracepointExt tp0 = out->GetTracepointExt(TEST_TP_ID_0);
    EXPECT_EQ(tp0.data.success_count, RESERVOIR_SIZE_EXT * 2);
    EXPECT_EQ(tp0.data.total_samples, RESERVOIR_SIZE_EXT * 2);
    EXPECT_EQ(tp0.data.reservoir_count, RESERVOIR_SIZE_EXT);
    for (size_t i = 0; i < RESERVOIR_SIZE_EXT; i++) {
        EXPECT_LT(tp0.data.reservoir[i], RESERVOIR_SIZE_EXT * 2);
    }
}

TEST_F(TracerExtTest, CombineExt_Oss_WithAgents_OutputsFormattedStats)
{
    InitTracer(TEST_TP_COUNT);
    TracerExt &tracer = TracerExt::Instance();
    tracer.CreateAgentExt(0);
    ASSERT_NE(tracer.agents_[0].load(), nullptr);
    tracer.agents_[0].load()->RecordExt(TEST_TP_ID_0, TEST_TP_NAME_0, 100, true);

    std::ostringstream oss;
    int ret = tracer.CombineExt(oss);
    EXPECT_EQ(ret, 0);
    EXPECT_NE(oss.str().find(TEST_TP_NAME_0), std::string::npos);
}

TEST_F(TracerExtTest, CombineExt_Oss_NoAgents_OutputsEmpty)
{
    InitTracer(TEST_TP_COUNT);
    TracerExt &tracer = TracerExt::Instance();
    std::ostringstream oss;
    int ret = tracer.CombineExt(oss);
    EXPECT_EQ(ret, 0);
    EXPECT_TRUE(oss.str().empty());
}

TEST_F(TracerExtTest, CombineExt_CliBuf_WithAgents_AllocatesBuffer)
{
    InitTracer(TEST_TP_COUNT);
    TracerExt &tracer = TracerExt::Instance();
    tracer.CreateAgentExt(0);
    ASSERT_NE(tracer.agents_[0].load(), nullptr);
    tracer.agents_[0].load()->RecordExt(TEST_TP_ID_0, TEST_TP_NAME_0, 100, true);

    char *buf = nullptr;
    int ret = tracer.CombineExt(&buf);
    EXPECT_GT(ret, 0);
    ASSERT_NE(buf, nullptr);
    EXPECT_NE(std::string(buf).find(TEST_TP_NAME_0), std::string::npos);
    free(buf);
}

TEST_F(TracerExtTest, CombineExt_CliBuf_NoAgents_ReturnsMinusOne)
{
    InitTracer(TEST_TP_COUNT);
    TracerExt &tracer = TracerExt::Instance();
    char *buf = nullptr;
    int ret = tracer.CombineExt(&buf);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(buf, nullptr);
}

TEST_F(TracerExtTest, ResetExt_WithAgents_ResetsAllTracepoints)
{
    InitTracer(TEST_TP_COUNT);
    TracerExt &tracer = TracerExt::Instance();
    tracer.CreateAgentExt(0);
    tracer.CreateAgentExt(1);
    ASSERT_NE(tracer.agents_[0].load(), nullptr);
    ASSERT_NE(tracer.agents_[1].load(), nullptr);
    tracer.agents_[0].load()->RecordExt(TEST_TP_ID_0, TEST_TP_NAME_0, 100, true);
    tracer.agents_[1].load()->RecordExt(TEST_TP_ID_0, TEST_TP_NAME_0, 200, true);

    tracer.ResetExt();

    TracepointExt tp0 = tracer.agents_[0].load()->GetTracepointExt(TEST_TP_ID_0);
    EXPECT_EQ(tp0.data.success_count, 0);
    EXPECT_EQ(tp0.data.total_time, 0);
    EXPECT_EQ(tp0.data.reservoir_count, 0);
}

TEST_F(TracerExtTest, DumpDataExt_WriteDumpDataFails_WarnsAndReturns)
{
    InitTracer(TEST_TP_COUNT);
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdirFail));

    DumpThreadExt dumper;
    dumper.DumpDataExt();

    EXPECT_TRUE(dumper.file_name_.empty());
    EXPECT_FALSE(dumper.dir_created_);
    EXPECT_FALSE(dumper.dump_file_.is_open());
}

TEST_F(TracerExtTest, DumpDataExt_WriteSucceeds_DumpsData)
{
    InitTracer(TEST_TP_COUNT);
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdirReal));

    DumpThreadExt dumper;
    dumper.DumpDataExt();

    EXPECT_FALSE(dumper.file_name_.empty());
    EXPECT_TRUE(dumper.dir_created_);
    EXPECT_TRUE(dumper.dump_file_.is_open());
}

TEST_F(TracerExtTest, DumpDataExt_LogLevelDebug_DebugBranchEnabled)
{
    InitTracer(TEST_TP_COUNT);
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdirReal));
    int origLogLevel = ock::ubs::Logger::Instance().GetLogLevel();
    ock::ubs::Logger::Instance().SetLogLevel(ock::ubs::LEVEL_DEBUG);

    DumpThreadExt dumper;
    dumper.DumpDataExt();

    ock::ubs::Logger::Instance().SetLogLevel(origLogLevel);
    EXPECT_FALSE(dumper.file_name_.empty());
}

TEST_F(TracerExtTest, DumpDataExt_LogLevelErr_WarnBranchDisabled)
{
    InitTracer(TEST_TP_COUNT);
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdirFail));
    int origLogLevel = ock::ubs::Logger::Instance().GetLogLevel();
    ock::ubs::Logger::Instance().SetLogLevel(ock::ubs::LEVEL_ERR);

    DumpThreadExt dumper;
    dumper.DumpDataExt();

    ock::ubs::Logger::Instance().SetLogLevel(origLogLevel);
    EXPECT_TRUE(dumper.file_name_.empty());
}

class DumpThreadExtTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        ResetTracerExt();
        g_mockCpuId = 0;
        MOCKER_CPP(::sched_getcpu).stubs().will(invoke(&MockSchedGetCpu));
        savedDumpPath_ = GlobalSetting::UBS_PROF_DUMP_PATH;
    }

    void TearDown() override
    {
        GlobalSetting::UBS_PROF_DUMP_PATH = savedDumpPath_;
        ResetTracerExt();
        GlobalMockObject::verify();
        errno = 0;
    }

private:
    std::string savedDumpPath_;
};

TEST_F(DumpThreadExtTest, WriteDumpTitleExt_LocaltimeRFails_WritesEmptyTimestamp)
{
    MOCKER_CPP(::localtime_r).stubs().will(returnValue(static_cast<struct tm *>(nullptr)));

    DumpThreadExt dumper;
    std::ostringstream oss;
    dumper.WriteDumpTitleExt(oss);

    EXPECT_NE(oss.str().find("timeStamp: "), std::string::npos);
    EXPECT_NE(oss.str().find("[TRACE_NAME]"), std::string::npos);
}

TEST_F(DumpThreadExtTest, GetSleepDurationExt_ReturnsUtDuration)
{
    DumpThreadExt dumper;
    EXPECT_EQ(dumper.GetSleepDurationExt().count(), UT_SLEEP_DURATION_MS_EXT);
}

TEST_F(DumpThreadExtTest, CreateDirectoryExt_DirAlreadyCreated_ReturnsOk)
{
    DumpThreadExt dumper;
    dumper.dir_created_ = true;
    std::string path = TEST_DUMP_PATH;
    EXPECT_EQ(dumper.CreateDirectoryExt(path), 0);
}

TEST_F(DumpThreadExtTest, CreateDirectoryExt_EmptyPath_UsesDefault)
{
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdirReal));
    DumpThreadExt dumper;
    std::string path;
    EXPECT_EQ(dumper.CreateDirectoryExt(path), 0);
    EXPECT_EQ(path, DEFAULT_DUMP_PATH_EXT);
    EXPECT_TRUE(dumper.dir_created_);
}

TEST_F(DumpThreadExtTest, CreateDirectoryExt_AllEexist_ReturnsOk)
{
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdirEexist));
    DumpThreadExt dumper;
    std::string path = "/tmp/ubsocket_prof_eexist";
    EXPECT_EQ(dumper.CreateDirectoryExt(path), 0);
    EXPECT_TRUE(dumper.dir_created_);
}

TEST_F(DumpThreadExtTest, CreateDirectoryExt_FinalMkdirFails_ReturnsMinusOne)
{
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdirPrefixOkFinalFail));
    DumpThreadExt dumper;
    std::string path = "/tmp/ubsocket_prof_final_fail";
    EXPECT_EQ(dumper.CreateDirectoryExt(path), -1);
    EXPECT_FALSE(dumper.dir_created_);
}

TEST_F(DumpThreadExtTest, WriteDumpDataExt_EmptyPath_UsesDefault)
{
    GlobalSetting::UBS_PROF_DUMP_PATH = "";
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdirReal));
    DumpThreadExt dumper;
    std::ostringstream oss;
    EXPECT_EQ(dumper.WriteDumpDataExt(oss), 0);
    EXPECT_EQ(dumper.last_file_path_, DEFAULT_DUMP_PATH_EXT);
    EXPECT_FALSE(dumper.file_name_.empty());
}

TEST_F(DumpThreadExtTest, WriteDumpDataExt_PathChanged_ClosesAndReopens)
{
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdirReal));
    DumpThreadExt dumper;
    std::ostringstream oss;

    GlobalSetting::UBS_PROF_DUMP_PATH = TEST_DUMP_PATH;
    EXPECT_EQ(dumper.WriteDumpDataExt(oss), 0);
    EXPECT_TRUE(dumper.dump_file_.is_open());

    GlobalSetting::UBS_PROF_DUMP_PATH = TEST_DUMP_PATH_2;
    EXPECT_EQ(dumper.WriteDumpDataExt(oss), 0);
    EXPECT_TRUE(dumper.dump_file_.is_open());
    EXPECT_EQ(dumper.last_file_path_, TEST_DUMP_PATH_2);
}

TEST_F(DumpThreadExtTest, WriteDumpDataExt_OpenFails_ReturnsMinusOne)
{
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdirReal));
    DumpThreadExt dumper;
    dumper.dir_created_ = true;
    dumper.last_file_path_ = "/nonexistent_dir_ubsocket_prof";
    dumper.file_name_ = "/nonexistent_dir_ubsocket_prof/prof.log";
    GlobalSetting::UBS_PROF_DUMP_PATH = "/nonexistent_dir_ubsocket_prof";

    std::ostringstream oss;
    EXPECT_EQ(dumper.WriteDumpDataExt(oss), -1);
    EXPECT_FALSE(dumper.dump_file_.is_open());
}

TEST_F(DumpThreadExtTest, CompressFileExt_ForkFails_ReturnsFalse)
{
    MOCKER_CPP(::fork).stubs().will(returnValue(-1));
    DumpThreadExt dumper;
    EXPECT_FALSE(dumper.CompressFileExt("/tmp/ubsocket_dummy.log"));
}

TEST_F(DumpThreadExtTest, CompressFileExt_WaitpidFails_ReturnsFalse)
{
    MOCKER_CPP(::fork).stubs().will(invoke(&MockForkParent));
    MOCKER_CPP(::waitpid).stubs().will(invoke(&MockWaitpidFail));
    DumpThreadExt dumper;
    EXPECT_FALSE(dumper.CompressFileExt("/tmp/ubsocket_dummy.log"));
}

TEST_F(DumpThreadExtTest, CompressFileExt_GzipExitZero_ReturnsTrue)
{
    MOCKER_CPP(::fork).stubs().will(invoke(&MockForkParent));
    MOCKER_CPP(::waitpid).stubs().will(invoke(&MockWaitpidExitZero));
    DumpThreadExt dumper;
    EXPECT_TRUE(dumper.CompressFileExt("/tmp/ubsocket_dummy.log"));
}

TEST_F(DumpThreadExtTest, CompressFileExt_GzipExitNonZero_ReturnsFalse)
{
    MOCKER_CPP(::fork).stubs().will(invoke(&MockForkParent));
    MOCKER_CPP(::waitpid).stubs().will(invoke(&MockWaitpidExitNonZero));
    DumpThreadExt dumper;
    EXPECT_FALSE(dumper.CompressFileExt("/tmp/ubsocket_dummy.log"));
}

TEST_F(DumpThreadExtTest, RotateDumpFileExt_StatFails_ReturnsEarly)
{
    DumpThreadExt dumper;
    dumper.file_name_ = "/nonexistent_ubsocket_prof/file.log";
    dumper.RotateDumpFileExt();
}

TEST_F(DumpThreadExtTest, RotateDumpFileExt_SmallFile_ReturnsEarly)
{
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdirReal));
    DumpThreadExt dumper;
    std::ostringstream oss;
    GlobalSetting::UBS_PROF_DUMP_PATH = TEST_DUMP_PATH;
    EXPECT_EQ(dumper.WriteDumpDataExt(oss), 0);
    EXPECT_FALSE(dumper.file_name_.empty());

    dumper.RotateDumpFileExt();
    EXPECT_FALSE(dumper.file_name_.empty());
}

TEST_F(DumpThreadExtTest, RotateDumpFileExt_TooLarge_CompressFail_UnlinksAndClears)
{
    MOCKER_CPP(::stat).stubs().will(invoke(&MockStatLarge));
    MOCKER_CPP(::fork).stubs().will(returnValue(-1));
    DumpThreadExt dumper;
    dumper.last_file_path_ = "/tmp";
    dumper.file_name_ = "/tmp/ubsocket_profiling_dummy.log";
    dumper.RotateDumpFileExt();
    EXPECT_TRUE(dumper.file_name_.empty());
}

TEST_F(DumpThreadExtTest, RotateDumpFileExt_OpenFileTooLarge_ClosesFileAndClearsName)
{
    MOCKER_CPP(::stat).stubs().will(invoke(&MockStatLarge));
    MOCKER_CPP(::fork).stubs().will(returnValue(-1));

    DumpThreadExt dumper;
    dumper.last_file_path_ = "/tmp";
    dumper.file_name_ = "/tmp/ubsocket_profiling_rotate_test.log";
    dumper.dump_file_.open(dumper.file_name_, std::ios::out);
    ASSERT_TRUE(dumper.dump_file_.is_open());

    dumper.RotateDumpFileExt();

    EXPECT_FALSE(dumper.dump_file_.is_open());
    EXPECT_TRUE(dumper.file_name_.empty());
}

TEST_F(DumpThreadExtTest, RotateDumpFileExt_CompressSuccess_UnlinkNotCalled)
{
    MOCKER_CPP(::stat).stubs().will(invoke(&MockStatLarge));
    MOCKER_CPP(::fork).stubs().will(invoke(&MockForkParent));
    MOCKER_CPP(::waitpid).stubs().will(invoke(&MockWaitpidExitZero));

    DumpThreadExt dumper;
    dumper.last_file_path_ = "/tmp";
    dumper.file_name_ = "/tmp/ubsocket_profiling_rotate_ok.log";
    dumper.dump_file_.open(dumper.file_name_, std::ios::out);
    ASSERT_TRUE(dumper.dump_file_.is_open());

    dumper.RotateDumpFileExt();

    EXPECT_FALSE(dumper.dump_file_.is_open());
    EXPECT_TRUE(dumper.file_name_.empty());
}

TEST_F(DumpThreadExtTest, WriteDumpTitleExt_LocaltimeRFails_LogDisabled_EmptyTimestamp)
{
    int origLogLevel = ock::ubs::Logger::Instance().GetLogLevel();
    ock::ubs::Logger::Instance().SetLogLevel(ock::ubs::LEVEL_ERR);
    MOCKER_CPP(::localtime_r).stubs().will(returnValue(static_cast<struct tm *>(nullptr)));

    DumpThreadExt dumper;
    std::ostringstream oss;
    dumper.WriteDumpTitleExt(oss);

    ock::ubs::Logger::Instance().SetLogLevel(origLogLevel);
    EXPECT_NE(oss.str().find("timeStamp: "), std::string::npos);
}

TEST_F(DumpThreadExtTest, DumpStartExt_AlreadyRunning_ReturnsEarly)
{
    DumpThreadExt dumper;
    dumper.running_ = true;
    dumper.DumpStartExt("", 1);
    EXPECT_TRUE(dumper.running_.load());
    EXPECT_FALSE(dumper.dump_thread_.joinable());
    dumper.running_ = false;
}

TEST_F(DumpThreadExtTest, DumpStopExt_NotRunning_ReturnsEarly)
{
    DumpThreadExt dumper;
    dumper.DumpStopExt();
    EXPECT_FALSE(dumper.running_.load());
}

TEST_F(DumpThreadExtTest, DumpStopExt_WithOpenFile_ClosesFile)
{
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdirReal));
    DumpThreadExt dumper;
    std::ostringstream oss;
    GlobalSetting::UBS_PROF_DUMP_PATH = TEST_DUMP_PATH;
    EXPECT_EQ(dumper.WriteDumpDataExt(oss), 0);
    EXPECT_TRUE(dumper.dump_file_.is_open());

    dumper.running_ = true;
    dumper.DumpStopExt();
    EXPECT_FALSE(dumper.dump_file_.is_open());
    EXPECT_FALSE(dumper.dir_created_);
    EXPECT_FALSE(dumper.running_.load());
}

TEST_F(DumpThreadExtTest, DumpStartStop_Lifecycle_RunsAndJoins)
{
    InitTracer(TEST_TP_COUNT);
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdirFail));

    DumpThreadExt dumper;
    dumper.DumpStartExt("", 1);
    EXPECT_TRUE(dumper.running_.load());
    EXPECT_TRUE(dumper.dump_thread_.joinable());

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    dumper.DumpStopExt();
    EXPECT_FALSE(dumper.running_.load());
    EXPECT_FALSE(dumper.dump_thread_.joinable());
}

} // namespace
