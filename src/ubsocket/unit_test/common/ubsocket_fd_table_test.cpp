/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>

#include "common/ubsocket_fd_table.h"
#include "common/ubsocket_global_setting.h"

using namespace ock::ubs;

namespace {
/* /proc/self/status 的 FDSize = 内核当前 fd 表的槽位数（fdt->max_fds）；读不到返回 -1 */
long FdTableSize()
{
    FILE *f = fopen("/proc/self/status", "r");
    if (f == nullptr) {
        return -1;
    }
    char line[256];
    long v = -1;
    while (fgets(line, sizeof(line), f) != nullptr) {
        if (strncmp(line, "FDSize:", 7) == 0) {
            v = atol(line + 7);
            break;
        }
    }
    fclose(f);
    return v;
}

uint64_t SoftNofile()
{
    struct rlimit rl {};
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0 || rl.rlim_cur == RLIM_INFINITY) {
        return 0;
    }
    return static_cast<uint64_t>(rl.rlim_cur);
}
} // namespace

// ==================== ClampFdTableTarget（纯函数） ====================

TEST(FdTableClampTest, ZeroMeansOff)
{
    EXPECT_EQ(ClampFdTableTarget(0, 1024), 0u);
    EXPECT_EQ(ClampFdTableTarget(0, 0), 0u);
}

TEST(FdTableClampTest, BelowSoftLimitIsKept)
{
    EXPECT_EQ(ClampFdTableTarget(45000, 1048576), 45000u);
    EXPECT_EQ(ClampFdTableTarget(1023, 1024), 1023u); /* 最大可用 fd 号 = 软上限 - 1 */
}

TEST(FdTableClampTest, AtOrAboveSoftLimitIsClampedToLastUsableFd)
{
    EXPECT_EQ(ClampFdTableTarget(1024, 1024), 1023u);
    EXPECT_EQ(ClampFdTableTarget(45000, 1024), 1023u);
    EXPECT_EQ(ClampFdTableTarget(45000, 1), 0u); /* 软上限 1：没有可用的高位 fd，放弃 */
}

TEST(FdTableClampTest, UnknownOrUnlimitedSoftLimitKeepsTarget)
{
    EXPECT_EQ(ClampFdTableTarget(45000, 0), 45000u);
}

// ==================== PreExpandFdTable（真实系统调用） ====================

TEST(FdTablePreExpandTest, ZeroIsOffAndTouchesNothing)
{
    const long before = FdTableSize();
    EXPECT_EQ(PreExpandFdTable(0), 0u);
    EXPECT_EQ(FdTableSize(), before);
}

/* 表被撑到能容纳目标 fd 号，且 close 之后不回缩 */
TEST(FdTablePreExpandTest, TableCoversTargetAndStaysAfterClose)
{
    const uint64_t soft = SoftNofile();
    if (soft != 0 && soft < 4096) {
        GTEST_SKIP() << "RLIMIT_NOFILE soft " << soft << " too small for this case";
    }
    const uint32_t target = 3000;
    EXPECT_EQ(PreExpandFdTable(target), target);
    const long size = FdTableSize();
    if (size < 0) {
        GTEST_SKIP() << "no /proc/self/status";
    }
    EXPECT_GT(size, static_cast<long>(target)); /* 3000 → 4096 档 */
    /* 辅助 fd 都已关闭：目标号此刻必须是空闲的 */
    errno = 0;
    EXPECT_EQ(fcntl(static_cast<int>(target), F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);
}

/* 回归：目标 fd 号已被宿主占用时不得把它关掉（dup2 会；F_DUPFD_CLOEXEC 不会） */
TEST(FdTablePreExpandTest, DoesNotCloseAnFdTheHostAlreadyHoldsAtTheTarget)
{
    const uint64_t soft = SoftNofile();
    if (soft != 0 && soft < 4096) {
        GTEST_SKIP() << "RLIMIT_NOFILE soft " << soft << " too small for this case";
    }
    const int target = 2000;
    int dummy = open("/dev/null", O_RDONLY | O_CLOEXEC);
    ASSERT_GE(dummy, 0);
    int held = fcntl(dummy, F_DUPFD_CLOEXEC, target);
    close(dummy);
    ASSERT_EQ(held, target) << "fd " << target << " was expected to be free in the test process";

    EXPECT_EQ(PreExpandFdTable(static_cast<uint32_t>(target)), static_cast<uint32_t>(target));

    EXPECT_NE(fcntl(held, F_GETFD), -1) << "the host's fd at the target number was closed";
    close(held);
}

/* 目标不小于软上限：钳到 rlim_cur - 1 仍然成功，而不是静默失败 */
TEST(FdTablePreExpandTest, TargetAboveSoftLimitIsClampedNotFailed)
{
    struct rlimit saved {};
    ASSERT_EQ(getrlimit(RLIMIT_NOFILE, &saved), 0);
    if (saved.rlim_cur != RLIM_INFINITY && saved.rlim_cur < 600) {
        GTEST_SKIP() << "RLIMIT_NOFILE soft already below 600";
    }
    struct rlimit low = saved;
    low.rlim_cur = 512;
    ASSERT_EQ(setrlimit(RLIMIT_NOFILE, &low), 0);
    const uint32_t done = PreExpandFdTable(45000);
    ASSERT_EQ(setrlimit(RLIMIT_NOFILE, &saved), 0);
    EXPECT_EQ(done, 511u);
}

// ==================== 配置项 ====================

TEST(FdTableSettingTest, DefaultAndEnv)
{
    EXPECT_EQ(GlobalSetting::UBS_PRE_ALLOC_SYSTEM_FD_NUM, 45000u);
    GlobalSetting::AddRules();
    /* LoadEnv 要求这几个 required env 在场（与 ubsocket_global_setting_test 同法） */
    setenv("UBSOCKET_MONITOR_ENABLE", "true", 1);
    setenv("UBSOCKET_BACKUP_LINK_ENABLE", "true", 1);
    setenv("UBSOCKET_READV_UNLIMITED", "true", 1);
    struct Case {
        const char *env;
        uint32_t expect;
    } cases[] = {{"0", 0}, {"70000", 70000}, {"1048576", 1048576}, {"-1", 45000}, {"1048577", 45000}};
    for (const auto &c : cases) {
        GlobalSetting::UBS_PRE_ALLOC_SYSTEM_FD_NUM = 45000;
        setenv("UBSOCKET_PRE_ALLOC_SYSTEM_FD_NUM", c.env, 1);
        EXPECT_EQ(GlobalSetting::LoadEnv(), UBS_OK) << "env=" << c.env;
        EXPECT_EQ(GlobalSetting::UBS_PRE_ALLOC_SYSTEM_FD_NUM, c.expect) << "env=" << c.env;
    }
    unsetenv("UBSOCKET_PRE_ALLOC_SYSTEM_FD_NUM");
    unsetenv("UBSOCKET_MONITOR_ENABLE");
    unsetenv("UBSOCKET_BACKUP_LINK_ENABLE");
    unsetenv("UBSOCKET_READV_UNLIMITED");
    GlobalSetting::UBS_PRE_ALLOC_SYSTEM_FD_NUM = 45000;
}
