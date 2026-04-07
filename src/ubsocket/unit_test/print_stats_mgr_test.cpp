/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Unit tests for print_stats_mgr module
 */

#include "print_stats_mgr.h"
#include "rpc_adpt_vlog.h"

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

using namespace Statistics;

// Named constants to avoid magic numbers (G.CNS.02)
namespace {
static const uint64_t PSM_TRACE_TIME_1 = 1U;
static const uint64_t PSM_TRACE_TIME_2 = 2U;
static const uint64_t PSM_TRACE_TIME_5 = 5U;
static const uint64_t PSM_TRACE_TIME_10 = 10U;
static const uint64_t PSM_TRACE_TIME_30 = 30U;
static const uint64_t PSM_TRACE_TIME_60 = 60U;
static const uint64_t PSM_FILE_SIZE_1 = 1U;
static const uint64_t PSM_FILE_SIZE_10 = 10U;
static const uint64_t PSM_FILE_SIZE_20 = 20U;
static const uint64_t PSM_FILE_SIZE_50 = 50U;
static const uint64_t PSM_FILE_SIZE_100 = 100U;
static const uint64_t PSM_FILE_SIZE_600 = 600U;
static const double PSM_RECORDER_VAL_10 = 10.0;
static const double PSM_RECORDER_VAL_20 = 20.0;
static const double PSM_RECORDER_VAL_30 = 30.0;
static const int PSM_LOOP_3 = 3;
static const int PSM_LOOP_5 = 5;
} // namespace

// Test fixture for PrintStatsMgr tests
class PrintStatsMgrTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void PrintStatsMgrTest::SetUp()
{
    RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
}

void PrintStatsMgrTest::TearDown()
{
    GlobalMockObject::verify();
}

// ============= PrintStatsMgr Tests =============

TEST_F(PrintStatsMgrTest, GetPrintStatsMgr_ReturnsNonNull)
{
    PrintStatsMgr* mgr = PrintStatsMgr::GetPrintStatsMgr();
    EXPECT_NE(mgr, nullptr);
}

TEST_F(PrintStatsMgrTest, GetPrintStatsMgr_SameInstance)
{
    PrintStatsMgr* mgr1 = PrintStatsMgr::GetPrintStatsMgr();
    PrintStatsMgr* mgr2 = PrintStatsMgr::GetPrintStatsMgr();
    EXPECT_EQ(mgr1, mgr2);
}

TEST_F(PrintStatsMgrTest, ProcessStats_DoesNotCrash)
{
    PrintStatsMgr* mgr = PrintStatsMgr::GetPrintStatsMgr();
    // ProcessStats should not crash
    mgr->ProcessStats();
}

TEST_F(PrintStatsMgrTest, StartStopStatsCollection)
{
    // Start with short trace time
    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, "/tmp/test_ubsocket", PSM_FILE_SIZE_10);

    // Stop collection
    PrintStatsMgr::StopStatsCollection();
}

TEST_F(PrintStatsMgrTest, StartStatsCollection_WithNullPath)
{
    // Start with null path
    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, nullptr, PSM_FILE_SIZE_10);

    // Stop collection
    PrintStatsMgr::StopStatsCollection();
}

TEST_F(PrintStatsMgrTest, StartStatsCollection_MultipleTimes)
{
    // Start multiple times - should only start once
    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, "/tmp/test1", PSM_FILE_SIZE_10);
    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_2, "/tmp/test2", PSM_FILE_SIZE_20);

    // Stop collection
    PrintStatsMgr::StopStatsCollection();
}

TEST_F(PrintStatsMgrTest, StopStatsCollection_WhenNotRunning)
{
    // Stop when not running should not crash
    PrintStatsMgr::StopStatsCollection();
}

TEST_F(PrintStatsMgrTest, ProcessStats_MultipleCalls)
{
    PrintStatsMgr* mgr = PrintStatsMgr::GetPrintStatsMgr();
    for (int i = 0; i < PSM_LOOP_5; ++i) {
        mgr->ProcessStats();
    }
    // Should not crash
}

TEST_F(PrintStatsMgrTest, StartStatsCollection_WithEmptyPath)
{
    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, "", PSM_FILE_SIZE_10);
    PrintStatsMgr::StopStatsCollection();
}

TEST_F(PrintStatsMgrTest, StartStatsCollection_WithLongPath)
{
    // Long path that might exceed buffer
    std::string longPath = "/tmp/test_ubsocket_long_path_directory/subdirectory/subdirectory2/subdirectory3";
    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, longPath.c_str(), PSM_FILE_SIZE_10);
    PrintStatsMgr::StopStatsCollection();
}

TEST_F(PrintStatsMgrTest, StartStop_QuickCycle)
{
    for (int i = 0; i < PSM_LOOP_3; ++i) {
        PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, "/tmp/test_quick", PSM_FILE_SIZE_10);
        PrintStatsMgr::StopStatsCollection();
    }
}

TEST_F(PrintStatsMgrTest, StartStatsCollection_ZeroTraceTime)
{
    PrintStatsMgr::StartStatsCollection(0U, "/tmp/test_zero", PSM_FILE_SIZE_10);
    PrintStatsMgr::StopStatsCollection();
}

TEST_F(PrintStatsMgrTest, StartStatsCollection_ZeroFileSize)
{
    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, "/tmp/test_zero_size", 0U);
    PrintStatsMgr::StopStatsCollection();
}

TEST_F(PrintStatsMgrTest, ProcessStats_WithStatsMgr)
{
    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, "/tmp/test_process", PSM_FILE_SIZE_10);
    PrintStatsMgr* mgr = PrintStatsMgr::GetPrintStatsMgr();
    mgr->ProcessStats();
    PrintStatsMgr::StopStatsCollection();
}

// ============= Additional ProcessStats Tests =============

TEST_F(PrintStatsMgrTest, ProcessStats_WithMultipleUpdates)
{
    PrintStatsMgr* mgr = PrintStatsMgr::GetPrintStatsMgr();
    mgr->ProcessStats();
    mgr->ProcessStats();
    mgr->ProcessStats();
    // Should not crash
}

TEST_F(PrintStatsMgrTest, GetPrintStatsMgr_MultipleCalls)
{
    PrintStatsMgr* mgr1 = PrintStatsMgr::GetPrintStatsMgr();
    PrintStatsMgr* mgr2 = PrintStatsMgr::GetPrintStatsMgr();
    PrintStatsMgr* mgr3 = PrintStatsMgr::GetPrintStatsMgr();

    EXPECT_EQ(mgr1, mgr2);
    EXPECT_EQ(mgr2, mgr3);
}

TEST_F(PrintStatsMgrTest, StartStatsCollection_MultipleDifferentPaths)
{
    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, "/tmp/test_path1", PSM_FILE_SIZE_10);
    PrintStatsMgr::StopStatsCollection();

    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, "/tmp/test_path2", PSM_FILE_SIZE_20);
    PrintStatsMgr::StopStatsCollection();

    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, "/tmp/test_path3", PSM_FILE_SIZE_50 - PSM_FILE_SIZE_20);
    PrintStatsMgr::StopStatsCollection();
}

TEST_F(PrintStatsMgrTest, StartStatsCollection_LongTraceTime)
{
    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_10, "/tmp/test_long_trace", PSM_FILE_SIZE_100);
    PrintStatsMgr::StopStatsCollection();
}

TEST_F(PrintStatsMgrTest, StartStatsCollection_OneTraceTime)
{
    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, "/tmp/test_one_trace", PSM_FILE_SIZE_10);
    PrintStatsMgr* mgr = PrintStatsMgr::GetPrintStatsMgr();
    mgr->ProcessStats();
    PrintStatsMgr::StopStatsCollection();
}

TEST_F(PrintStatsMgrTest, ProcessStats_AfterStopAndRestart)
{
    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, "/tmp/test_restart", PSM_FILE_SIZE_10);
    PrintStatsMgr* mgr = PrintStatsMgr::GetPrintStatsMgr();
    mgr->ProcessStats();
    PrintStatsMgr::StopStatsCollection();

    // Restart
    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, "/tmp/test_restart2", PSM_FILE_SIZE_20);
    mgr->ProcessStats();
    PrintStatsMgr::StopStatsCollection();
}

TEST_F(PrintStatsMgrTest, StopStatsCollection_MultipleTimes)
{
    PrintStatsMgr::StopStatsCollection();
    PrintStatsMgr::StopStatsCollection();
    PrintStatsMgr::StopStatsCollection();
    // Should not crash
}

TEST_F(PrintStatsMgrTest, StartStatsCollection_PathWithSubdirs)
{
    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, "/tmp/test_subdir/subdir2/subdir3", PSM_FILE_SIZE_10);
    PrintStatsMgr::StopStatsCollection();
}

// ============= Additional Tests for Better Coverage =============

TEST_F(PrintStatsMgrTest, StartStatsCollection_VeryLongPath)
{
    // Test with a path that exceeds buffer size to test snprintf failure path
    std::string veryLongPath(PSM_FILE_SIZE_600, 'a');
    veryLongPath = "/tmp/" + veryLongPath;
    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, veryLongPath.c_str(), PSM_FILE_SIZE_10);
    PrintStatsMgr::StopStatsCollection();
}

TEST_F(PrintStatsMgrTest, ProcessStats_WithExistingStats)
{
    // Create some stats
    Recorder recorder("test_recorder");
    recorder.Update(PSM_RECORDER_VAL_10);
    recorder.Update(PSM_RECORDER_VAL_20);
    recorder.Update(PSM_RECORDER_VAL_30);

    PrintStatsMgr* mgr = PrintStatsMgr::GetPrintStatsMgr();
    mgr->ProcessStats();
    // Should output stats to file
}

TEST_F(PrintStatsMgrTest, StartStatsCollection_DifferentTraceTimes)
{
    // Test various trace times
    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_5, "/tmp/test_trace_5", PSM_FILE_SIZE_10);
    PrintStatsMgr::StopStatsCollection();

    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_30, "/tmp/test_trace_30", PSM_FILE_SIZE_10);
    PrintStatsMgr::StopStatsCollection();

    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_60, "/tmp/test_trace_60", PSM_FILE_SIZE_10);
    PrintStatsMgr::StopStatsCollection();
}

TEST_F(PrintStatsMgrTest, StartStatsCollection_DifferentFileSizes)
{
    // Test various file sizes
    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, "/tmp/test_size_1", PSM_FILE_SIZE_1);
    PrintStatsMgr::StopStatsCollection();

    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, "/tmp/test_size_50", PSM_FILE_SIZE_50);
    PrintStatsMgr::StopStatsCollection();

    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, "/tmp/test_size_100", PSM_FILE_SIZE_100);
    PrintStatsMgr::StopStatsCollection();
}

TEST_F(PrintStatsMgrTest, ProcessStats_EmptyStats)
{
    // Process stats when no stats have been recorded
    PrintStatsMgr* mgr = PrintStatsMgr::GetPrintStatsMgr();
    mgr->ProcessStats();
    // Should not crash
}

TEST_F(PrintStatsMgrTest, StartStatsCollection_SpecialCharsInPath)
{
    // Test path with various characters
    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, "/tmp/test_special_123", PSM_FILE_SIZE_10);
    PrintStatsMgr::StopStatsCollection();
}

TEST_F(PrintStatsMgrTest, GetPrintStatsMgr_Consistency)
{
    // Verify singleton consistency
    PrintStatsMgr* mgr1 = PrintStatsMgr::GetPrintStatsMgr();
    PrintStatsMgr* mgr2 = PrintStatsMgr::GetPrintStatsMgr();
    PrintStatsMgr* mgr3 = PrintStatsMgr::GetPrintStatsMgr();

    EXPECT_EQ(mgr1, mgr2);
    EXPECT_EQ(mgr2, mgr3);
}

TEST_F(PrintStatsMgrTest, StartStop_RapidToggle)
{
    for (int i = 0; i < PSM_LOOP_5; ++i) {
        PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, "/tmp/test_rapid", PSM_FILE_SIZE_10);
        PrintStatsMgr::StopStatsCollection();
    }
}

TEST_F(PrintStatsMgrTest, ProcessStats_MultipleRecorders)
{
    // Create multiple recorders
    Recorder rec1("recorder1");
    Recorder rec2("recorder2");
    Recorder rec3("recorder3");

    rec1.Update(PSM_RECORDER_VAL_10);
    rec2.Update(PSM_RECORDER_VAL_20);
    rec3.Update(PSM_RECORDER_VAL_30);

    PrintStatsMgr* mgr = PrintStatsMgr::GetPrintStatsMgr();
    mgr->ProcessStats();
}

TEST_F(PrintStatsMgrTest, StartStatsCollection_PathWithTrailingSlash)
{
    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, "/tmp/test_trailing/", PSM_FILE_SIZE_10);
    PrintStatsMgr::StopStatsCollection();
}

TEST_F(PrintStatsMgrTest, StartStatsCollection_RootPath)
{
    PrintStatsMgr::StartStatsCollection(PSM_TRACE_TIME_1, "/tmp", PSM_FILE_SIZE_10);
    PrintStatsMgr::StopStatsCollection();
}