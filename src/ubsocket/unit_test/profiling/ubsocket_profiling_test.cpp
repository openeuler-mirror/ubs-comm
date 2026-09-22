#include <gtest/gtest.h>
#include <chrono>
#include <cstdlib>
#include <mockcpp/mockcpp.hpp>
#include <sched.h>
#include <thread>
#include <vector>
#include <dirent.h>
#include <map>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <cstring>
#include <sstream>
#include <string>
#include "ubsocket_prof.h"
#include "common/ubsocket_errno.h"
#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_logger.h"
#include "common/ubsocket_profiling.h"
#include "ubsocket_prof_tracepoint_dumper.h"
#include "ubsocket_prof_tracer.h"
#include "ubsocket_prof_tracer_ext.h"

using namespace ock::ubs;

class UProfilingTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

enum ProfilingTPIdTest : uint32_t
{
    TP0 = 0,
    TP1,
    TP2,
    TP3,
    TP4,
};

constexpr const char *DEFAULT_DUMP_PATH = "/tmp/ubsocket/unitest/profiling";
constexpr uint16_t INTERVAL_DEFAULT_MIN = 1;
constexpr uint32_t TP_MAX = 5;
constexpr uint32_t TP_COUNT_TOO_LARGE = 1001;
constexpr int CPU_ID_MOCK_0 = 0;

void UProfilingTest::SetUp()
{
    GlobalSetting::UBS_PROF_MODE = "fast";
    ubsocket_prof_option_t option;
    option.tracepoint_count = TP_MAX;
    option.enable_dump = 1;
    option.dump_file_path = DEFAULT_DUMP_PATH;
    option.dump_interval_min = INTERVAL_DEFAULT_MIN;
    ubsocket_prof_init(&option);
}

void UProfilingTest::TearDown()
{
    ubsocket_prof_uninit();
    GlobalSetting::UBS_PROF_MODE = "fast";
    GlobalMockObject::verify();
}

static int g_mockCpuId = 0;

static int MockSchedGetCpu()
{
    return g_mockCpuId;
}

void test_thread()
{
    // 所有线程都操作同一个全局 recorder
    PROF_START(TP0);
    PROF_END(TP0, true);
}

TEST_F(UProfilingTest, InitAddSum)
{
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++) {
        threads.emplace_back(test_thread);
    }

    for (auto &t : threads) {
        t.join();
    }
}

// ==================== Profiling class tests ====================

TEST_F(UProfilingTest, Profiling_Init_Uninit)
{
    int ret = Profiling::Init(TP_MAX, DEFAULT_DUMP_PATH, INTERVAL_DEFAULT_MIN);
    EXPECT_EQ(ret, 0);
    ret = Profiling::Uninit();
    EXPECT_EQ(ret, 0);
}

TEST_F(UProfilingTest, Profiling_Combine_AfterRecords)
{
    PROF_START(TP0);
    PROF_END(TP0, true);
    std::string out;
    int ret = Profiling::Combine(out);
    EXPECT_EQ(ret, 0);
    EXPECT_FALSE(out.empty());
}

TEST_F(UProfilingTest, Profiling_Reset)
{
    PROF_START(TP0);
    PROF_END(TP0, true);
    Profiling::Reset();
    // After reset, combine should return empty or minimal
    std::string out;
    Profiling::Combine(out);
}

TEST_F(UProfilingTest, Profiling_Combine_NotInitialized_ReturnsError)
{
    Profiling::Uninit();
    std::string out;
    int ret = Profiling::Combine(out);
    EXPECT_EQ(ret, UBS_ERROR);
    // Re-init for TearDown
    ubsocket_prof_option_t option{};
    option.tracepoint_count = TP_MAX;
    option.enable_dump = 1;
    option.dump_file_path = DEFAULT_DUMP_PATH;
    option.dump_interval_min = INTERVAL_DEFAULT_MIN;
    ubsocket_prof_init(&option);
}

// ==================== Init error-path tests ====================

TEST_F(UProfilingTest, ProfInit_NullOption_ReturnsMinusOne)
{
    int ret = ubsocket_prof_init(nullptr);
    EXPECT_EQ(ret, -1);
}

TEST_F(UProfilingTest, ProfInit_ZeroTracepointCount_ReturnsMinusOne)
{
    ubsocket_prof_option_t option{};
    option.tracepoint_count = 0;
    option.enable_dump = 1;
    option.dump_file_path = DEFAULT_DUMP_PATH;
    option.dump_interval_min = INTERVAL_DEFAULT_MIN;
    int ret = ubsocket_prof_init(&option);
    EXPECT_EQ(ret, -1);
}

TEST_F(UProfilingTest, ProfInit_EnableDumpNullPath_ReturnsMinusOne)
{
    ubsocket_prof_option_t option{};
    option.tracepoint_count = TP_MAX;
    option.enable_dump = 1;
    option.dump_file_path = nullptr;
    option.dump_interval_min = INTERVAL_DEFAULT_MIN;
    int ret = ubsocket_prof_init(&option);
    EXPECT_EQ(ret, -1);
}

TEST_F(UProfilingTest, ProfInit_DisableDumpNullPath_SkipPathCheck_InitSuccess)
{
    // enable_dump == 0 -> dump_file_path check skipped, init proceeds
    ubsocket_prof_option_t option{};
    option.tracepoint_count = TP_MAX;
    option.enable_dump = 0;
    option.dump_file_path = nullptr;
    option.dump_interval_min = INTERVAL_DEFAULT_MIN;
    int ret = ubsocket_prof_init(&option);
    EXPECT_EQ(ret, 0);
}

TEST_F(UProfilingTest, ProfInit_FastMode_TracepointCountTooLarge_ReturnsInvalidParam)
{
    ubsocket_prof_uninit();
    ubsocket_prof_option_t option{};
    option.tracepoint_count = TP_COUNT_TOO_LARGE;
    option.enable_dump = 0;
    option.dump_file_path = nullptr;
    option.dump_interval_min = INTERVAL_DEFAULT_MIN;
    int ret = ubsocket_prof_init(&option);
    EXPECT_EQ(ret, static_cast<int>(UBS_INVALID_PARAM));
    EXPECT_EQ(ubsocket_prof_enabled, 0);
}

// ==================== Ext-mode tests ====================

TEST_F(UProfilingTest, ProfExtMode_InitSuccess_EnabledFlagSet)
{
    GlobalSetting::UBS_PROF_MODE = "ext";
    ubsocket_prof_option_t option{};
    option.tracepoint_count = TP_MAX;
    option.enable_dump = 0;
    option.dump_file_path = nullptr;
    option.dump_interval_min = INTERVAL_DEFAULT_MIN;
    int ret = ubsocket_prof_init(&option);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(ubsocket_prof_enabled, 1);
}

TEST_F(UProfilingTest, ProfExtMode_TracepointCountTooLarge_ReturnsInvalidParam)
{
    GlobalSetting::UBS_PROF_MODE = "ext";
    ubsocket_prof_uninit();
    ubsocket_prof_option_t option{};
    option.tracepoint_count = TP_COUNT_TOO_LARGE;
    option.enable_dump = 0;
    option.dump_file_path = nullptr;
    option.dump_interval_min = INTERVAL_DEFAULT_MIN;
    int ret = ubsocket_prof_init(&option);
    EXPECT_EQ(ret, static_cast<int>(UBS_INVALID_PARAM));
    EXPECT_EQ(ubsocket_prof_enabled, 0);
}

TEST_F(UProfilingTest, ProfExtMode_Record_ReturnsOk)
{
    GlobalSetting::UBS_PROF_MODE = "ext";
    g_mockCpuId = CPU_ID_MOCK_0;
    MOCKER_CPP(::sched_getcpu).stubs().will(invoke(&MockSchedGetCpu));
    ubsocket_prof_option_t option{};
    option.tracepoint_count = TP_MAX;
    option.enable_dump = 0;
    option.dump_file_path = nullptr;
    option.dump_interval_min = INTERVAL_DEFAULT_MIN;
    ubsocket_prof_init(&option);
    int ret = ubsocket_prof_record(TP0, "TP0", 100, true);
    EXPECT_EQ(ret, 0);
}

TEST_F(UProfilingTest, ProfExtMode_Combind_ReturnsLen)
{
    GlobalSetting::UBS_PROF_MODE = "ext";
    g_mockCpuId = CPU_ID_MOCK_0;
    MOCKER_CPP(::sched_getcpu).stubs().will(invoke(&MockSchedGetCpu));
    ubsocket_prof_option_t option{};
    option.tracepoint_count = TP_MAX;
    option.enable_dump = 0;
    option.dump_file_path = nullptr;
    option.dump_interval_min = INTERVAL_DEFAULT_MIN;
    ubsocket_prof_init(&option);
    ubsocket_prof_record(TP0, "TP0", 100, true);
    char *outBuf = nullptr;
    int len = ubsocket_prof_combind(&outBuf);
    EXPECT_GT(len, 0);
    if (outBuf != nullptr) {
        free(outBuf);
    }
}

TEST_F(UProfilingTest, ProfExtMode_Reset_NoCrash)
{
    GlobalSetting::UBS_PROF_MODE = "ext";
    g_mockCpuId = CPU_ID_MOCK_0;
    MOCKER_CPP(::sched_getcpu).stubs().will(invoke(&MockSchedGetCpu));
    ubsocket_prof_option_t option{};
    option.tracepoint_count = TP_MAX;
    option.enable_dump = 0;
    option.dump_file_path = nullptr;
    option.dump_interval_min = INTERVAL_DEFAULT_MIN;
    ubsocket_prof_init(&option);
    ubsocket_prof_record(TP0, "TP0", 100, true);
    ubsocket_prof_reset();
    char *outBuf = nullptr;
    int len = ubsocket_prof_combind(&outBuf);
    if (outBuf != nullptr) {
        free(outBuf);
    }
    (void)len;
}

TEST_F(UProfilingTest, ProfExtMode_Uninit_ExtBranch)
{
    GlobalSetting::UBS_PROF_MODE = "ext";
    ubsocket_prof_option_t option{};
    option.tracepoint_count = TP_MAX;
    option.enable_dump = 0;
    option.dump_file_path = nullptr;
    option.dump_interval_min = INTERVAL_DEFAULT_MIN;
    ubsocket_prof_init(&option);
    int ret = ubsocket_prof_uninit();
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(ubsocket_prof_enabled, 0);
}

TEST_F(UProfilingTest, Tracer_UninitThenInit_ReleasesOldCombiner)
{
    Profiling::Uninit();
    ubsocket_prof_option_t option{};
    option.tracepoint_count = TP_MAX;
    option.enable_dump = 0;
    option.dump_file_path = nullptr;
    option.dump_interval_min = INTERVAL_DEFAULT_MIN;
    EXPECT_EQ(ubsocket_prof_init(&option), 0);
}

// ==================== Tracer (fast) inline Record error paths ====================

TEST_F(UProfilingTest, Tracer_Record_NotInitialized_ReturnsError)
{
    ock::ubs::profiling::Tracer &tracer = ock::ubs::profiling::Tracer::Instance();
    tracer.UnInit();
    ock::ubs::profiling::Tracer::tls_group = nullptr;
    EXPECT_EQ(tracer.Record(TP0, "TP0", 100, true), UBS_ERROR);
}

// ==================== TracerExt inline RecordExt error paths ====================

TEST_F(UProfilingTest, ProfExtMode_Record_NegativeCpuId_ReturnsError)
{
    GlobalSetting::UBS_PROF_MODE = "ext";
    g_mockCpuId = -1;
    MOCKER_CPP(::sched_getcpu).stubs().will(invoke(&MockSchedGetCpu));
    ubsocket_prof_option_t option{};
    option.tracepoint_count = TP_MAX;
    option.enable_dump = 0;
    option.dump_file_path = nullptr;
    option.dump_interval_min = INTERVAL_DEFAULT_MIN;
    EXPECT_EQ(ubsocket_prof_init(&option), 0);
    EXPECT_EQ(ubsocket_prof_record(TP0, "TP0", 100, true), UBS_ERROR);
}

TEST_F(UProfilingTest, ProfExtMode_Record_CpuIdOutOfRange_ReturnsError)
{
    GlobalSetting::UBS_PROF_MODE = "ext";
    g_mockCpuId = static_cast<int>(ock::ubs::profiling::MAX_CPU_AGENTS_EXT);
    MOCKER_CPP(::sched_getcpu).stubs().will(invoke(&MockSchedGetCpu));
    ubsocket_prof_option_t option{};
    option.tracepoint_count = TP_MAX;
    option.enable_dump = 0;
    option.dump_file_path = nullptr;
    option.dump_interval_min = INTERVAL_DEFAULT_MIN;
    EXPECT_EQ(ubsocket_prof_init(&option), 0);
    EXPECT_EQ(ubsocket_prof_record(TP0, "TP0", 100, true), UBS_ERROR);
}

TEST_F(UProfilingTest, TracerExt_RecordExt_NotInitialized_ReturnsError)
{
    g_mockCpuId = CPU_ID_MOCK_0;
    MOCKER_CPP(::sched_getcpu).stubs().will(invoke(&MockSchedGetCpu));
    ock::ubs::profiling::TracerExt::Instance().UnInitExt();
    EXPECT_EQ(ock::ubs::profiling::TracerExt::Instance().RecordExt(TP0, "TP0", 100, true), UBS_ERROR);
}

// ==================== DumpThread (ubsocket_prof_tracepoint_dumper) tests ====================

namespace {

using ock::ubs::profiling::DumpThread;
using ock::ubs::profiling::DEFAULT_DUMP_PATH;
using ock::ubs::profiling::DUMP_FILE_PREFIX;
using ock::ubs::profiling::DUMP_FILE_SUFFIX;
using ock::ubs::profiling::DUMP_ARCHIVE_SUFFIX;
using ock::ubs::profiling::DUMP_FILE_MAX_SIZE;

static const char *TEST_DUMP_DIR = "/tmp/ubsocket/ut_dump_dir";
static const char *TEST_DUMP_DIR_B = "/tmp/ubsocket/ut_dump_dir_b";
static const char *TEST_ROTATE_DIR = "/tmp/ubsocket/ut_rotate_dir";
static const char *TEST_ROTATE_FILE = "/tmp/ubsocket/ut_rotate_dir/ubsocket_profiling.log";

enum MockMkdirMode { MKDIR_DEFAULT = 0, MKDIR_FAIL_ALL, MKDIR_FAIL_PATH };

static int g_mockMkdirMode = MKDIR_DEFAULT;
static std::string g_mockMkdirFailPath;

static int MockMkdir(const char *path, mode_t mode)
{
    if (g_mockMkdirMode == MKDIR_FAIL_ALL) {
        errno = EACCES;
        return -1;
    }
    if (g_mockMkdirMode == MKDIR_FAIL_PATH && g_mockMkdirFailPath == path) {
        errno = EACCES;
        return -1;
    }
    if (::syscall(SYS_mkdirat, AT_FDCWD, path, static_cast<mode_t>(mode)) != 0) {
        return -1; /* EEXIST for existing prefixes, other errno from kernel */
    }
    return 0;
}

static bool g_mockStatFailAll = false;
static std::map<std::string, time_t> g_mockStatMtimes;

static int MockStat(const char *path, struct stat *buf)
{
    memset(buf, 0, sizeof(struct stat));
    if (g_mockStatFailAll) {
        errno = ENOENT;
        return -1;
    }
    std::string pathStr(path != nullptr ? path : "");
    if (g_mockStatMtimes.count(pathStr) > 0) {
        buf->st_size = static_cast<off_t>(DUMP_FILE_MAX_SIZE);
        buf->st_mtime = g_mockStatMtimes[pathStr];
        return 0;
    }
    buf->st_size = 0;
    return 0;
}

static pid_t g_mockForkRet = -1;
static pid_t MockFork()
{
    return g_mockForkRet;
}

static pid_t g_mockWaitPidRet = 0;
static int g_mockWaitStatus = 0;
static pid_t MockWaitPid(pid_t pid, int *status, int options)
{
    (void)pid;
    (void)options;
    if (status != nullptr) {
        *status = g_mockWaitStatus;
    }
    return g_mockWaitPidRet;
}

static std::vector<std::string> g_mockReaddirNames;
static int g_mockReaddirNamesIdx = 0;
static struct dirent g_mockDirent;
static DIR *g_mockDirHandle = reinterpret_cast<DIR *>(0x1234);

static DIR *MockOpendir(const char *path)
{
    (void)path;
    g_mockReaddirNamesIdx = 0;
    return g_mockDirHandle;
}

static struct dirent *MockReaddir(DIR *dirp)
{
    (void)dirp;
    if (g_mockReaddirNamesIdx >= static_cast<int>(g_mockReaddirNames.size())) {
        return nullptr;
    }
    const std::string &name = g_mockReaddirNames[g_mockReaddirNamesIdx++];
    strncpy(g_mockDirent.d_name, name.c_str(), sizeof(g_mockDirent.d_name) - 1);
    g_mockDirent.d_name[sizeof(g_mockDirent.d_name) - 1] = '\0';
    return &g_mockDirent;
}

static int MockClosedir(DIR *dirp)
{
    (void)dirp;
    return 0;
}

static struct tm *MockLocaltimeR(const time_t *timep, struct tm *result)
{
    (void)timep;
    (void)result;
    return nullptr;
}

class DumpThreadTest : public testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        g_origLogLevel = ock::ubs::Logger::Instance().GetLogLevel();
        ResetMockGlobals();
        GlobalSetting::UBS_PROF_DUMP_PATH = TEST_DUMP_DIR;
    }

    void TearDown() override
    {
        GlobalSetting::UBS_PROF_DUMP_PATH = "";
        ock::ubs::Logger::Instance().SetLogLevel(g_origLogLevel);
        GlobalMockObject::verify();
        errno = 0;
    }

    static void ResetMockGlobals()
    {
        g_mockMkdirMode = MKDIR_DEFAULT;
        g_mockMkdirFailPath.clear();
        g_mockStatFailAll = false;
        g_mockStatMtimes.clear();
        g_mockForkRet = -1;
        g_mockWaitPidRet = 0;
        g_mockWaitStatus = 0;
        g_mockReaddirNames.clear();
        g_mockReaddirNamesIdx = 0;
        g_mockDirHandle = reinterpret_cast<DIR *>(0x1234);
        errno = 0;
    }

    static int g_origLogLevel;
};

int DumpThreadTest::g_origLogLevel = ock::ubs::LEVEL_INFO;

TEST_F(DumpThreadTest, DumpData_WriteDumpDataFails_WarnsAndReturns)
{
    g_mockMkdirMode = MKDIR_FAIL_ALL;
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdir));
    DumpThread dumper;
    dumper.DumpData();
    EXPECT_TRUE(dumper.file_name_.empty());
    EXPECT_FALSE(dumper.dir_created_);
}

TEST_F(DumpThreadTest, DumpData_WriteSucceeds_DumpsData)
{
    g_mockMkdirMode = MKDIR_DEFAULT;
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdir));
    DumpThread dumper;
    dumper.DumpData();
    EXPECT_FALSE(dumper.file_name_.empty());
    EXPECT_TRUE(dumper.dir_created_);
    EXPECT_TRUE(dumper.dump_file_.is_open());
}

TEST_F(DumpThreadTest, DumpData_LogLevelDebug_DebugEnabled)
{
    ock::ubs::Logger::Instance().SetLogLevel(ock::ubs::LEVEL_DEBUG);
    g_mockMkdirMode = MKDIR_DEFAULT;
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdir));
    DumpThread dumper;
    dumper.DumpData();
    EXPECT_FALSE(dumper.file_name_.empty());
}

TEST_F(DumpThreadTest, DumpData_LogLevelErr_WarnDisabled)
{
    ock::ubs::Logger::Instance().SetLogLevel(ock::ubs::LEVEL_ERR);
    g_mockMkdirMode = MKDIR_FAIL_ALL;
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdir));
    DumpThread dumper;
    dumper.DumpData();
    EXPECT_TRUE(dumper.file_name_.empty());
}

TEST_F(DumpThreadTest, ErrorPaths_LogLevelErr_LogLinesDisabled)
{
    ock::ubs::Logger::Instance().SetLogLevel(ock::ubs::LEVEL_ERR);

    /* WriteDumpTitle localtime-fail (146) + CreateDirectory loop mkdir-fail (176) */
    MOCKER_CPP(::localtime_r).stubs().will(invoke(&MockLocaltimeR));
    g_mockMkdirMode = MKDIR_FAIL_ALL;
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdir));
    DumpThread dumper;
    dumper.DumpData();
    EXPECT_TRUE(dumper.file_name_.empty());

    /* CreateDirectory final mkdir-fail warn (184) */
    g_mockMkdirMode = MKDIR_FAIL_PATH;
    g_mockMkdirFailPath = TEST_DUMP_DIR;
    DumpThread dumper2;
    std::string path = TEST_DUMP_DIR;
    EXPECT_EQ(dumper2.CreateDirectory(path), -1);

    /* WriteDumpData open-fail warn (226) */
    g_mockMkdirMode = MKDIR_DEFAULT;
    DumpThread dumper3;
    dumper3.last_file_path_ = TEST_DUMP_DIR;
    dumper3.file_name_ = TEST_DUMP_DIR;
    std::ostringstream oss;
    EXPECT_EQ(dumper3.WriteDumpData(oss), -1);

    /* CompressFile warns (250, 262, 268) */
    g_mockForkRet = -1;
    MOCKER_CPP(::fork).stubs().will(invoke(&MockFork));
    EXPECT_FALSE(dumper3.CompressFile(TEST_ROTATE_FILE));
    g_mockForkRet = 100;
    g_mockWaitPidRet = -1;
    MOCKER_CPP(::waitpid).stubs().will(invoke(&MockWaitPid));
    EXPECT_FALSE(dumper3.CompressFile(TEST_ROTATE_FILE));
    g_mockWaitPidRet = 0;
    g_mockWaitStatus = 256; /* WIFEXITED true but WEXITSTATUS != 0 */
    EXPECT_FALSE(dumper3.CompressFile(TEST_ROTATE_FILE));
}

TEST_F(DumpThreadTest, DumpData_LocaltimeRNull_UsesEmptyTimeStamp)
{
    MOCKER_CPP(::localtime_r).stubs().will(invoke(&MockLocaltimeR));
    g_mockMkdirMode = MKDIR_DEFAULT;
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdir));
    DumpThread dumper;
    dumper.DumpData();
    EXPECT_FALSE(dumper.file_name_.empty());
}

TEST_F(DumpThreadTest, DumpStart_AlreadyRunning_ReturnsEarly)
{
    DumpThread dumper;
    dumper.running_ = true;
    dumper.DumpStart("", 1);
    EXPECT_TRUE(dumper.running_.load());
}

TEST_F(DumpThreadTest, DumpStop_NotRunning_ReturnsEarly)
{
    DumpThread dumper;
    dumper.DumpStop();
    EXPECT_FALSE(dumper.running_.load());
}

TEST_F(DumpThreadTest, DumpStop_RunningFileOpen_ClosesFileAndResetsDirFlag)
{
    DumpThread dumper;
    dumper.running_ = true;
    dumper.dir_created_ = true;
    dumper.dump_file_.open("/tmp/ubsocket_ut_dump_stop.log", std::ios::out | std::ios::app);
    ASSERT_TRUE(dumper.dump_file_.is_open());
    dumper.DumpStop();
    EXPECT_FALSE(dumper.running_.load());
    EXPECT_FALSE(dumper.dump_file_.is_open());
    EXPECT_FALSE(dumper.dir_created_);
}

TEST_F(DumpThreadTest, CreateDirectory_AlreadyCreated_ReturnsZero)
{
    DumpThread dumper;
    dumper.dir_created_ = true;
    std::string path = TEST_DUMP_DIR;
    EXPECT_EQ(dumper.CreateDirectory(path), 0);
}

TEST_F(DumpThreadTest, CreateDirectory_EmptyPath_UsesDefaultPath)
{
    g_mockMkdirMode = MKDIR_DEFAULT;
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdir));
    DumpThread dumper;
    std::string path;
    EXPECT_EQ(dumper.CreateDirectory(path), 0);
    EXPECT_EQ(path, DEFAULT_DUMP_PATH);
    EXPECT_TRUE(dumper.dir_created_);
}

TEST_F(DumpThreadTest, CreateDirectory_ExistingPrefixes_ContinuesOnEexist)
{
    g_mockMkdirMode = MKDIR_DEFAULT;
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdir));
    DumpThread dumper;
    std::string path = TEST_DUMP_DIR;
    EXPECT_EQ(dumper.CreateDirectory(path), 0);
    EXPECT_TRUE(dumper.dir_created_);
    dumper.dir_created_ = false;
    EXPECT_EQ(dumper.CreateDirectory(path), 0);
    EXPECT_TRUE(dumper.dir_created_);
}

TEST_F(DumpThreadTest, CreateDirectory_FreshDeepPath_MkdirSucceeds)
{
    g_mockMkdirMode = MKDIR_DEFAULT;
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdir));
    std::string freshPath = "/tmp/ubsocket_ut_fresh_" + std::to_string(getpid()) + "/sub";
    DumpThread dumper;
    EXPECT_EQ(dumper.CreateDirectory(freshPath), 0);
    EXPECT_TRUE(dumper.dir_created_);
}

TEST_F(DumpThreadTest, CreateDirectory_FinalMkdirFails_ReturnsMinusOne)
{
    g_mockMkdirMode = MKDIR_FAIL_PATH;
    g_mockMkdirFailPath = TEST_DUMP_DIR;
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdir));
    DumpThread dumper;
    std::string path = TEST_DUMP_DIR;
    EXPECT_EQ(dumper.CreateDirectory(path), -1);
    EXPECT_FALSE(dumper.dir_created_);
}

TEST_F(DumpThreadTest, WriteDumpData_EmptyPath_UsesDefaultPath)
{
    g_mockMkdirMode = MKDIR_DEFAULT;
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdir));
    GlobalSetting::UBS_PROF_DUMP_PATH = "";
    DumpThread dumper;
    std::ostringstream oss;
    EXPECT_EQ(dumper.WriteDumpData(oss), 0);
    EXPECT_NE(dumper.file_name_.find(DEFAULT_DUMP_PATH), std::string::npos);
}

TEST_F(DumpThreadTest, WriteDumpData_FileOpenFails_ReturnsMinusOne)
{
    g_mockMkdirMode = MKDIR_DEFAULT;
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdir));
    DumpThread dumper;
    dumper.last_file_path_ = TEST_DUMP_DIR; /* avoid path-change reset which clears file_name_ */
    dumper.file_name_ = TEST_DUMP_DIR;      /* a directory path -> ofstream open fails */
    std::ostringstream oss;
    EXPECT_EQ(dumper.WriteDumpData(oss), -1);
    EXPECT_FALSE(dumper.dump_file_.is_open());
}

TEST_F(DumpThreadTest, WriteDumpData_PathChanged_ResetsAndReopens)
{
    g_mockMkdirMode = MKDIR_DEFAULT;
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdir));
    DumpThread dumper;
    std::ostringstream oss;
    EXPECT_EQ(dumper.WriteDumpData(oss), 0);
    std::string firstName = dumper.file_name_;
    GlobalSetting::UBS_PROF_DUMP_PATH = TEST_DUMP_DIR_B;
    EXPECT_EQ(dumper.WriteDumpData(oss), 0);
    EXPECT_NE(dumper.file_name_, firstName);
    EXPECT_NE(dumper.file_name_.find(TEST_DUMP_DIR_B), std::string::npos);
}

TEST_F(DumpThreadTest, CompressFile_ForkFails_ReturnsFalse)
{
    g_mockForkRet = -1;
    errno = EAGAIN;
    MOCKER_CPP(::fork).stubs().will(invoke(&MockFork));
    DumpThread dumper;
    EXPECT_FALSE(dumper.CompressFile(TEST_ROTATE_FILE));
}

TEST_F(DumpThreadTest, CompressFile_WaitPidFails_ReturnsFalse)
{
    g_mockForkRet = 100;
    g_mockWaitPidRet = -1;
    errno = ECHILD;
    MOCKER_CPP(::fork).stubs().will(invoke(&MockFork));
    MOCKER_CPP(::waitpid).stubs().will(invoke(&MockWaitPid));
    DumpThread dumper;
    EXPECT_FALSE(dumper.CompressFile(TEST_ROTATE_FILE));
}

TEST_F(DumpThreadTest, CompressFile_ChildExitZero_ReturnsTrue)
{
    g_mockForkRet = 100;
    g_mockWaitPidRet = 0;
    g_mockWaitStatus = 0;
    MOCKER_CPP(::fork).stubs().will(invoke(&MockFork));
    MOCKER_CPP(::waitpid).stubs().will(invoke(&MockWaitPid));
    DumpThread dumper;
    EXPECT_TRUE(dumper.CompressFile(TEST_ROTATE_FILE));
}

TEST_F(DumpThreadTest, CompressFile_ChildExitNonZero_ReturnsFalse)
{
    g_mockForkRet = 100;
    g_mockWaitPidRet = 0;
    g_mockWaitStatus = 256; /* WIFEXITED true but WEXITSTATUS != 0 */
    MOCKER_CPP(::fork).stubs().will(invoke(&MockFork));
    MOCKER_CPP(::waitpid).stubs().will(invoke(&MockWaitPid));
    DumpThread dumper;
    EXPECT_FALSE(dumper.CompressFile(TEST_ROTATE_FILE));
}

TEST_F(DumpThreadTest, RotateDumpFile_StatFails_ReturnsEarly)
{
    g_mockStatFailAll = true;
    MOCKER_CPP(::stat).stubs().will(invoke(&MockStat));
    DumpThread dumper;
    dumper.file_name_ = TEST_ROTATE_FILE;
    dumper.RotateDumpFile();
    EXPECT_EQ(dumper.file_name_, TEST_ROTATE_FILE);
}

TEST_F(DumpThreadTest, RotateDumpFile_SmallFile_ReturnsEarly)
{
    MOCKER_CPP(::stat).stubs().will(invoke(&MockStat));
    DumpThread dumper;
    dumper.file_name_ = TEST_ROTATE_FILE;
    dumper.RotateDumpFile();
    EXPECT_EQ(dumper.file_name_, TEST_ROTATE_FILE);
}

TEST_F(DumpThreadTest, RotateDumpFile_SizeExceeds_CompressFailUnlinkAndPrune)
{
    g_mockForkRet = -1;
    std::string archivePattern = std::string(DUMP_FILE_PREFIX) + std::to_string(getpid()) + DUMP_FILE_SUFFIX;
    std::string archiveName = archivePattern + DUMP_ARCHIVE_SUFFIX;
    std::string archivePath = std::string(TEST_ROTATE_DIR) + "/" + archiveName;
    g_mockStatMtimes[TEST_ROTATE_FILE] = 100;
    g_mockStatMtimes[archivePath] = 1;
    g_mockReaddirNames = {"unrelated.log", archivePattern, archivePattern + ".tmp", archiveName, archiveName,
                          archiveName, archiveName};

    MOCKER_CPP(::stat).stubs().will(invoke(&MockStat));
    MOCKER_CPP(::fork).stubs().will(invoke(&MockFork));
    MOCKER_CPP(::opendir).stubs().will(invoke(&MockOpendir));
    MOCKER_CPP(::readdir).stubs().will(invoke(&MockReaddir));
    MOCKER_CPP(::closedir).stubs().will(invoke(&MockClosedir));

    DumpThread dumper;
    dumper.file_name_ = TEST_ROTATE_FILE;
    dumper.last_file_path_ = TEST_ROTATE_DIR;
    dumper.RotateDumpFile();
    EXPECT_TRUE(dumper.file_name_.empty());
}

TEST_F(DumpThreadTest, RotateDumpFile_OpendirNull_SkipsPrune)
{
    g_mockForkRet = -1;
    g_mockDirHandle = nullptr;
    g_mockStatMtimes[TEST_ROTATE_FILE] = 100;
    MOCKER_CPP(::stat).stubs().will(invoke(&MockStat));
    MOCKER_CPP(::fork).stubs().will(invoke(&MockFork));
    MOCKER_CPP(::opendir).stubs().will(invoke(&MockOpendir));

    DumpThread dumper;
    dumper.file_name_ = TEST_ROTATE_FILE;
    dumper.last_file_path_ = TEST_ROTATE_DIR;
    dumper.RotateDumpFile();
    EXPECT_TRUE(dumper.file_name_.empty());
}

TEST_F(DumpThreadTest, RotateDumpFile_FileOpen_ClosesBeforePrune)
{
    g_mockForkRet = -1;
    g_mockDirHandle = nullptr;
    g_mockStatMtimes[TEST_ROTATE_FILE] = 100;
    MOCKER_CPP(::stat).stubs().will(invoke(&MockStat));
    MOCKER_CPP(::fork).stubs().will(invoke(&MockFork));
    MOCKER_CPP(::opendir).stubs().will(invoke(&MockOpendir));

    DumpThread dumper;
    dumper.file_name_ = TEST_ROTATE_FILE;
    dumper.last_file_path_ = TEST_ROTATE_DIR;
    ::syscall(SYS_mkdirat, AT_FDCWD, "/tmp/ubsocket", 0750);
    ::syscall(SYS_mkdirat, AT_FDCWD, TEST_ROTATE_DIR, 0750);
    dumper.dump_file_.open(TEST_ROTATE_FILE, std::ios::out | std::ios::app);
    ASSERT_TRUE(dumper.dump_file_.is_open());
    dumper.RotateDumpFile();
    EXPECT_FALSE(dumper.dump_file_.is_open());
    EXPECT_TRUE(dumper.file_name_.empty());
}

TEST_F(DumpThreadTest, DumpLoop_RunsCycles_InvokesDumpDataAndFinalDrain)
{
    g_mockMkdirMode = MKDIR_DEFAULT;
    MOCKER_CPP(::mkdir).stubs().will(invoke(&MockMkdir));
    DumpThread dumper;
    dumper.DumpStart("", 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    dumper.DumpStop();
    EXPECT_FALSE(dumper.running_.load());
}

} // namespace
