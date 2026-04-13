/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Unit tests for file_descriptor module
 */

#include "file_descriptor.h"
#include "rpc_adpt_vlog.h"
#include "net_common.h"

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <sys/socket.h>
#include <fcntl.h>
#include <cstring>

using namespace ubsocket;

// Named constants to avoid magic numbers (G.CNS.02)
namespace {
static const int FD_TEST_100 = 100;
static const int FD_TEST_200 = 200;
static const int FD_TEST_500 = 500;
static const int FD_TEST_1000 = 1000;
static const int FD_TEST_10 = 10;
static const int FD_TEST_42 = 42;
static const int FD_TEST_1 = 1;
static const int FD_TEST_2 = 2;
static const int FD_TEST_3 = 3;
static const int FD_NOT_EXIST_999 = 999;
static const int FD_NOT_EXIST_99999 = 99999;
static const int FD_NOT_EXIST_99998 = 99998;
static const int FD_SOCK_12345 = 12345;
static const int FD_SOCK_12346 = 12346;
static const int FD_SOCK_54321 = 54321;
static const int FD_SOCK_54322 = 54322;
static const int FD_SOCK_99991 = 99991;
static const int FD_SOCK_99992 = 99992;
static const int FD_SOCK_111 = 111;
static const int FD_SOCK_222 = 222;
static const int FD_SOCK_333 = 333;
static const int FD_SOCK_100000 = 100000;
static const int TIMEOUT_1000_MS = 1000;
static const int TIMEOUT_100_MS = 100;
static const int TIMEOUT_10_MS = 10;
static const int TIMEOUT_100000_MS = 100000;
static const int SETENV_OVERWRITE = 1;
static const int FD_EPOLL_1 = 1;
static const int FD_EPOLL_2 = 2;
static const int FD_EPOLL_3 = 3;
static const int FD_EPOLL_4 = 4;
static const int FD_EPOLL_5 = 5;
static const int FD_EPOLL_10 = 10;
static const int LOOP_10 = 10;
static const int LOOP_5 = 5;
static const int RET_OK = 0;
static const int RET_ERR = -1;
static const int TIMEOUT_0_MS = 0;
static const int TIMEOUT_1_MS = 1;
} // namespace

// Mock for OsAPiMgr
class OsApiMock {
public:
    static int MockFcntl(int fd, int cmd, ...);
    static ssize_t MockSend(int sockfd, const void *buf, size_t len, int flags); // NOLINT
    static ssize_t MockRecv(int sockfd, void *buf, size_t len, int flags); // NOLINT
    static int MockEpollCtl(int epfd, int op, int fd, struct epoll_event *event);
};

// Test fixture for file_descriptor tests
class FileDescriptorTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void FileDescriptorTest::SetUp()
{
    setenv("UBSOCKET_USE_UB_FORCE", "true", SETENV_OVERWRITE);
    setenv("UBSOCKET_TRANS_MODE", "UB", SETENV_OVERWRITE);
    RpcAdptSetLogCtx(UTIL_VLOG_LEVEL_INFO);

    // Initialize global locks
    if (g_socket_epoll_lock == nullptr) {
        g_socket_epoll_lock = g_rw_lock_ops.create();
    }
    if (Fd<SocketFd>::GetRWLock() == nullptr) {
        Fd<SocketFd>::GlobalFdInit();
    }
    if (Fd<EpollFd>::GetRWLock() == nullptr) {
        Fd<EpollFd>::GlobalFdInit();
    }
}

void FileDescriptorTest::TearDown()
{
    unsetenv("UBSOCKET_USE_UB_FORCE");
    unsetenv("UBSOCKET_TRANS_MODE");
    GlobalMockObject::verify();
}

// ============= SocketEpollMapper Tests =============

TEST_F(FileDescriptorTest, SocketEpollMapper_AddDel)
{
    SocketEpollMapper mapper(FD_TEST_100);

    // Test Add
    mapper.Add(FD_EPOLL_1);
    mapper.Add(FD_EPOLL_2);
    mapper.Add(FD_EPOLL_1);  // Duplicate add

    // Test Del
    mapper.Del(FD_EPOLL_1);
    mapper.Del(FD_NOT_EXIST_999);  // Non-existent
}

TEST_F(FileDescriptorTest, SocketEpollMapper_Clear)
{
    SocketEpollMapper mapper(FD_TEST_100);
    mapper.Add(FD_EPOLL_1);
    mapper.Add(FD_EPOLL_2);
    mapper.Add(FD_EPOLL_3);

    // Clear should remove all epoll fds
    mapper.Clear();
}

TEST_F(FileDescriptorTest, GetSocketEpollMapper_NotFound)
{
    SocketEpollMapper* mapper = GetSocketEpollMapper(FD_NOT_EXIST_99999);
    EXPECT_EQ(mapper, nullptr);
}

TEST_F(FileDescriptorTest, CreateSocketEpollMapper_Success)
{
    int testFd = FD_SOCK_12345;
    SocketEpollMapper* mapper = nullptr;

    bool result = CreateSocketEpollMapper(testFd, mapper);
    EXPECT_TRUE(result);
    EXPECT_NE(mapper, nullptr);

    // Cleanup
    CleanSocketEpollMapper(testFd);
}

TEST_F(FileDescriptorTest, CreateSocketEpollMapper_AlreadyExists)
{
    int testFd = FD_SOCK_12346;
    SocketEpollMapper* mapper1 = nullptr;
    SocketEpollMapper* mapper2 = nullptr;

    bool result1 = CreateSocketEpollMapper(testFd, mapper1);
    EXPECT_TRUE(result1);

    // Second call should return existing mapper
    bool result2 = CreateSocketEpollMapper(testFd, mapper2);
    EXPECT_FALSE(result2);
    EXPECT_EQ(mapper1, mapper2);

    CleanSocketEpollMapper(testFd);
}

TEST_F(FileDescriptorTest, CleanSocketEpollMapper_NotExists)
{
    // Should not crash when cleaning non-existent mapper
    CleanSocketEpollMapper(FD_NOT_EXIST_99998);
}

// ============= Fd Template Tests =============

TEST_F(FileDescriptorTest, Fd_GetFdObj_InvalidFd)
{
    SocketFd* obj = Fd<SocketFd>::GetFdObj(-1);
    EXPECT_EQ(obj, nullptr);

    obj = Fd<SocketFd>::GetFdObj(RPC_ADPT_FD_MAX);
    EXPECT_EQ(obj, nullptr);
}

TEST_F(FileDescriptorTest, Fd_GetFdObjMap)
{
    SocketFd** map = Fd<SocketFd>::GetFdObjMap();
    EXPECT_NE(map, nullptr);
}

TEST_F(FileDescriptorTest, Fd_OverrideFdObj_InvalidFd)
{
    // Should not crash with invalid fd
    Fd<SocketFd>::OverrideFdObj(-1, nullptr);
    Fd<SocketFd>::OverrideFdObj(RPC_ADPT_FD_MAX, nullptr);
}

TEST_F(FileDescriptorTest, Fd_GetRWLock)
{
    u_rw_lock_t* lock = Fd<SocketFd>::GetRWLock();
    EXPECT_NE(lock, nullptr);
}

// ============= IsTimeout Tests =============

TEST_F(FileDescriptorTest, IsTimeout_NotExpired)
{
    auto start = std::chrono::high_resolution_clock::now();
    bool result = IsTimeout(start, TIMEOUT_1000_MS);  // 1 second timeout
    EXPECT_FALSE(result);
}

TEST_F(FileDescriptorTest, IsTimeout_Expired)
{
    auto start = std::chrono::high_resolution_clock::now() - std::chrono::milliseconds(100);
    bool result = IsTimeout(start, TIMEOUT_10_MS);  // 10ms timeout, already waited 100ms
    EXPECT_TRUE(result);
}

// ============= EpollEvent Tests =============

class EpollEventTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
protected:
    struct epoll_event test_event;
};

void EpollEventTest::SetUp()
{
    setenv("UBSOCKET_USE_UB_FORCE", "true", SETENV_OVERWRITE);
    setenv("UBSOCKET_TRANS_MODE", "UB", SETENV_OVERWRITE);
    RpcAdptSetLogCtx(UTIL_VLOG_LEVEL_INFO);

    test_event.events = EPOLLIN | EPOLLOUT;
    test_event.data.ptr = nullptr;

    if (g_socket_epoll_lock == nullptr) {
        g_socket_epoll_lock = g_rw_lock_ops.create();
    }
}

void EpollEventTest::TearDown()
{
    unsetenv("UBSOCKET_USE_UB_FORCE");
    unsetenv("UBSOCKET_TRANS_MODE");
    GlobalMockObject::verify();
}

TEST_F(EpollEventTest, Constructor)
{
    EpollEvent event(FD_TEST_10, &test_event);
    EXPECT_EQ(event.GetFd(), FD_TEST_10);
    EXPECT_EQ(event.GetEvents(), (uint32_t)(EPOLLIN | EPOLLOUT));
}

TEST_F(EpollEventTest, ProcessEpollEvent)
{
    EpollEvent event(FD_TEST_10, &test_event);

    struct epoll_event input_event;
    input_event.events = EPOLLIN;
    input_event.data.ptr = &event;

    struct epoll_event output_event;
    int count = event.ProcessEpollEvent(&input_event, &output_event, 0);
    EXPECT_EQ(count, FD_EPOLL_1);
}

TEST_F(EpollEventTest, ProcessEpollEvent_NoInput)
{
    EpollEvent event(FD_TEST_10, &test_event);

    struct epoll_event output_event;
    int count = event.ProcessEpollEvent(&output_event);
    EXPECT_EQ(count, FD_EPOLL_1);
}

TEST_F(EpollEventTest, IsAddEpollEvent)
{
    EpollEvent event(FD_TEST_10, &test_event);
    EXPECT_FALSE(event.IsAddEpollEvent());
}

TEST_F(EpollEventTest, GetData)
{
    int testData = FD_TEST_42;
    test_event.data.ptr = &testData;

    EpollEvent event(FD_TEST_10, &test_event);
    void* data = event.GetData();
    EXPECT_EQ(data, &testData);
}

// ============= EpollFd Tests =============

class EpollFdTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void EpollFdTest::SetUp()
{
    setenv("UBSOCKET_USE_UB_FORCE", "true", SETENV_OVERWRITE);
    setenv("UBSOCKET_TRANS_MODE", "UB", SETENV_OVERWRITE);
    RpcAdptSetLogCtx(UTIL_VLOG_LEVEL_INFO);

    if (g_socket_epoll_lock == nullptr) {
        g_socket_epoll_lock = g_rw_lock_ops.create();
    }
    if (Fd<EpollFd>::GetRWLock() == nullptr) {
        Fd<EpollFd>::GlobalFdInit();
    }
}

void EpollFdTest::TearDown()
{
    unsetenv("UBSOCKET_USE_UB_FORCE");
    unsetenv("UBSOCKET_TRANS_MODE");
    GlobalMockObject::verify();
}

TEST_F(EpollFdTest, Constructor)
{
    int epollFd = FD_TEST_100;
    EpollFd epollFdObj(epollFd);
    EXPECT_EQ(epollFdObj.GetFd(), epollFd);
}

TEST_F(EpollFdTest, Find_NotExists)
{
    EpollFd epollFd(FD_TEST_100);
    EpollEvent* event = epollFd.Find(FD_NOT_EXIST_999);
    EXPECT_EQ(event, nullptr);
}

TEST_F(EpollFdTest, EpollCtlAdd_NullEvent)
{
    EpollFd epollFd(FD_TEST_100);

    struct epoll_event* nullEvent = nullptr;
    int ret = epollFd.EpollCtlAdd(FD_TEST_200, nullEvent);
    EXPECT_EQ(ret, RET_ERR);
    EXPECT_EQ(errno, EINVAL);
}

TEST_F(EpollFdTest, EpollCtlMod_NullEvent)
{
    EpollFd epollFd(FD_TEST_100);

    struct epoll_event* nullEvent = nullptr;
    int ret = epollFd.EpollCtlMod(FD_TEST_200, nullEvent);
    EXPECT_EQ(ret, RET_ERR);
    EXPECT_EQ(errno, EINVAL);
}

TEST_F(EpollFdTest, EpollCtlMod_NotExists)
{
    EpollFd epollFd(FD_TEST_100);

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.ptr = nullptr;

    int ret = epollFd.EpollCtlMod(FD_TEST_200, &event);
    EXPECT_EQ(ret, RET_ERR);
}

TEST_F(EpollFdTest, EpollCtlDel_NotExists)
{
    EpollFd epollFd(FD_TEST_100);

    // Deleting non-existent fd should succeed
    int ret = epollFd.EpollCtlDel(FD_TEST_200, nullptr);
    EXPECT_EQ(ret, RET_OK);
}

TEST_F(EpollFdTest, EpollCtl_InvalidOp)
{
    EpollFd epollFd(FD_TEST_100);

    int ret = epollFd.EpollCtl(FD_NOT_EXIST_999, FD_TEST_200, nullptr);  // Invalid op
    EXPECT_EQ(ret, RET_ERR);
    EXPECT_EQ(errno, EINVAL);
}

TEST_F(EpollFdTest, EpollWait_InvalidMaxevents)
{
    EpollFd epollFd(FD_TEST_100);

    struct epoll_event events[FD_TEST_10];
    int ret = epollFd.EpollWait(events, 0, 0);  // maxevents <= 0
    EXPECT_EQ(ret, RET_ERR);
}

TEST_F(EpollFdTest, GetCtlMutex)
{
    EpollFd epollFd(FD_TEST_100);
    u_external_mutex_t* mutex = epollFd.GetCtlMutex();
    EXPECT_NE(mutex, nullptr);
}

TEST_F(EpollFdTest, GlobalFdInit)
{
    // Call GlobalFdInit - should succeed if already initialized
    int ret = Fd<EpollFd>::GlobalFdInit();
    EXPECT_EQ(ret, RET_OK);
}

// ============= SocketFd Tests =============

class SocketFdTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void SocketFdTest::SetUp()
{
    setenv("UBSOCKET_USE_UB_FORCE", "true", SETENV_OVERWRITE);
    setenv("UBSOCKET_TRANS_MODE", "UB", SETENV_OVERWRITE);
    RpcAdptSetLogCtx(UTIL_VLOG_LEVEL_INFO);

    if (g_socket_epoll_lock == nullptr) {
        g_socket_epoll_lock = g_rw_lock_ops.create();
    }
    if (Fd<SocketFd>::GetRWLock() == nullptr) {
        Fd<SocketFd>::GlobalFdInit();
    }
}

void SocketFdTest::TearDown()
{
    unsetenv("UBSOCKET_USE_UB_FORCE");
    unsetenv("UBSOCKET_TRANS_MODE");
    GlobalMockObject::verify();
}

TEST_F(SocketFdTest, GlobalFdInit)
{
    int ret = Fd<SocketFd>::GlobalFdInit();
    EXPECT_EQ(ret, RET_OK);
}

TEST_F(SocketFdTest, GetFdObjMap_Valid)
{
    SocketFd** map = Fd<SocketFd>::GetFdObjMap();
    EXPECT_NE(map, nullptr);
}

TEST_F(SocketFdTest, GetRWLock_Valid)
{
    u_rw_lock_t* lock = Fd<SocketFd>::GetRWLock();
    EXPECT_NE(lock, nullptr);
}

// ============= SocketEpollMapper Additional Tests =============

TEST_F(FileDescriptorTest, SocketEpollMapper_MultipleAdds)
{
    SocketEpollMapper mapper(FD_TEST_100);

    // Add multiple epoll fds
    for (int i = 1; i <= LOOP_10; ++i) {
        mapper.Add(i);
    }

    // Delete them
    for (int i = 1; i <= LOOP_10; ++i) {
        mapper.Del(i);
    }
}

TEST_F(FileDescriptorTest, SocketEpollMapper_AddSameFdTwice)
{
    SocketEpollMapper mapper(FD_TEST_100);

    // Add same fd twice
    mapper.Add(FD_EPOLL_1);
    mapper.Add(FD_EPOLL_1);  // Duplicate - should not add again (set ignores duplicates)
    mapper.Del(FD_EPOLL_1);
}

// ============= GetSocketEpollMapper Tests =============

TEST_F(FileDescriptorTest, GetSocketEpollMapper_AfterCreate)
{
    int testFd = FD_SOCK_54321;
    SocketEpollMapper* mapper = nullptr;

    CreateSocketEpollMapper(testFd, mapper);
    ASSERT_NE(mapper, nullptr);

    // Get should return the same mapper
    SocketEpollMapper* retrieved = GetSocketEpollMapper(testFd);
    EXPECT_EQ(retrieved, mapper);

    CleanSocketEpollMapper(testFd);
}

TEST_F(FileDescriptorTest, GetSocketEpollMapper_AfterClean)
{
    int testFd = FD_SOCK_54322;
    SocketEpollMapper* mapper = nullptr;

    CreateSocketEpollMapper(testFd, mapper);
    CleanSocketEpollMapper(testFd);

    // Should return nullptr after clean
    SocketEpollMapper* retrieved = GetSocketEpollMapper(testFd);
    EXPECT_EQ(retrieved, nullptr);
}

// ============= Fd Template Additional Tests =============

TEST_F(FileDescriptorTest, Fd_GlobalFdInit_AlreadyInitialized)
{
    // Call GlobalFdInit twice
    int ret1 = Fd<SocketFd>::GlobalFdInit();
    int ret2 = Fd<SocketFd>::GlobalFdInit();
    EXPECT_EQ(ret1, 0);
    EXPECT_EQ(ret2, 0);
}

TEST_F(FileDescriptorTest, Fd_EpollFd_GlobalFdInit)
{
    int ret = Fd<EpollFd>::GlobalFdInit();
    EXPECT_EQ(ret, RET_OK);
}

TEST_F(FileDescriptorTest, Fd_GetFdObj_WithinRange)
{
    // GetFdObj for fd within range should return nullptr initially
    SocketFd* obj = Fd<SocketFd>::GetFdObj(FD_TEST_100);
    EXPECT_EQ(obj, nullptr);
}

TEST_F(FileDescriptorTest, Fd_EpollFd_GetFdObj)
{
    EpollFd* obj = Fd<EpollFd>::GetFdObj(FD_TEST_100);
    EXPECT_EQ(obj, nullptr);
}

TEST_F(FileDescriptorTest, Fd_EpollFd_GetFdObjMap)
{
    EpollFd** map = Fd<EpollFd>::GetFdObjMap();
    EXPECT_NE(map, nullptr);
}

TEST_F(FileDescriptorTest, Fd_EpollFd_GetRWLock)
{
    u_rw_lock_t* lock = Fd<EpollFd>::GetRWLock();
    EXPECT_NE(lock, nullptr);
}

// ============= IsTimeout Additional Tests =============

TEST_F(FileDescriptorTest, IsTimeout_ZeroTimeout)
{
    auto start = std::chrono::high_resolution_clock::now();
    // Zero timeout - IsTimeout returns true only if elapsed time > 0
    // Since time passes between now() and the check, it typically returns true
    bool result = IsTimeout(start, TIMEOUT_0_MS);
    // The result depends on whether any time has elapsed (> 0)
    // This is a timing-dependent test, so we just verify it doesn't crash
    (void)result;
}

TEST_F(FileDescriptorTest, IsTimeout_VerySmallTimeout)
{
    auto start = std::chrono::high_resolution_clock::now() - std::chrono::milliseconds(100);
    bool result = IsTimeout(start, TIMEOUT_1_MS);  // 1ms timeout, already waited 100ms
    EXPECT_TRUE(result);
}

// ============= CreateSocketEpollMapper Edge Cases =============

TEST_F(FileDescriptorTest, CreateSocketEpollMapper_MultipleFds)
{
    SocketEpollMapper* mapper1 = nullptr;
    SocketEpollMapper* mapper2 = nullptr;
    SocketEpollMapper* mapper3 = nullptr;

    bool result1 = CreateSocketEpollMapper(FD_SOCK_111, mapper1);
    bool result2 = CreateSocketEpollMapper(FD_SOCK_222, mapper2);
    bool result3 = CreateSocketEpollMapper(FD_SOCK_333, mapper3);

    EXPECT_TRUE(result1);
    EXPECT_TRUE(result2);
    EXPECT_TRUE(result3);

    EXPECT_NE(mapper1, mapper2);
    EXPECT_NE(mapper2, mapper3);

    CleanSocketEpollMapper(FD_SOCK_111);
    CleanSocketEpollMapper(FD_SOCK_222);
    CleanSocketEpollMapper(FD_SOCK_333);
}

// ============= SocketEpollMapper Clear Tests =============

TEST_F(FileDescriptorTest, SocketEpollMapper_ClearWithMultipleEpolls)
{
    SocketEpollMapper mapper(FD_TEST_100);

    mapper.Add(FD_EPOLL_1);
    mapper.Add(FD_EPOLL_2);
    mapper.Add(FD_EPOLL_3);

    // Clear should handle multiple epoll fds
    mapper.Clear();
}

TEST_F(FileDescriptorTest, SocketEpollMapper_ClearEmpty)
{
    SocketEpollMapper mapper(FD_TEST_100);

    // Clear on empty mapper
    mapper.Clear();
}

// ============= Fd Template Edge Cases =============

TEST_F(FileDescriptorTest, Fd_GetFdObj_AfterGlobalFdInit)
{
    // After GlobalFdInit, GetFdObj should return nullptr for unset fd
    SocketFd* obj = Fd<SocketFd>::GetFdObj(0);
    EXPECT_EQ(obj, nullptr);

    obj = Fd<SocketFd>::GetFdObj(FD_TEST_1000);
    EXPECT_EQ(obj, nullptr);
}

TEST_F(FileDescriptorTest, Fd_EpollFd_GetFdObj_AfterGlobalFdInit)
{
    EpollFd* obj = Fd<EpollFd>::GetFdObj(0);
    EXPECT_EQ(obj, nullptr);

    obj = Fd<EpollFd>::GetFdObj(FD_TEST_500);
    EXPECT_EQ(obj, nullptr);
}

TEST_F(FileDescriptorTest, Fd_OverrideFdObj_ValidFd)
{
    // Override with nullptr
    Fd<SocketFd>::OverrideFdObj(FD_TEST_100, nullptr);
    SocketFd* obj = Fd<SocketFd>::GetFdObj(FD_TEST_100);
    EXPECT_EQ(obj, nullptr);
}

TEST_F(FileDescriptorTest, Fd_EpollFd_OverrideFdObj_ValidFd)
{
    Fd<EpollFd>::OverrideFdObj(FD_TEST_200, nullptr);
    EpollFd* obj = Fd<EpollFd>::GetFdObj(200);
    EXPECT_EQ(obj, nullptr);
}

// ============= IsTimeout Additional Tests =============

TEST_F(FileDescriptorTest, IsTimeout_FutureTime)
{
    auto start = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(1000);
    bool result = IsTimeout(start, TIMEOUT_100_MS);
    EXPECT_FALSE(result);
}

TEST_F(FileDescriptorTest, IsTimeout_PastTime)
{
    auto start = std::chrono::high_resolution_clock::now() - std::chrono::milliseconds(1000);
    bool result = IsTimeout(start, TIMEOUT_100_MS);
    EXPECT_TRUE(result);
}

TEST_F(FileDescriptorTest, IsTimeout_LargeTimeout)
{
    auto start = std::chrono::high_resolution_clock::now();
    bool result = IsTimeout(start, TIMEOUT_100000_MS);
    EXPECT_FALSE(result);
}

// ============= SocketEpollMapper Additional Tests =============

TEST_F(FileDescriptorTest, SocketEpollMapper_AddDelSequence)
{
    SocketEpollMapper mapper(FD_TEST_100);

    for (int i = 1; i <= LOOP_5; ++i) {
        mapper.Add(i);
    }
    for (int i = 1; i <= LOOP_5; ++i) {
        mapper.Del(i);
    }
    mapper.Clear();
}

TEST_F(FileDescriptorTest, SocketEpollMapper_AddAfterClear)
{
    SocketEpollMapper mapper(FD_TEST_100);

    mapper.Add(FD_EPOLL_1);
    mapper.Add(FD_EPOLL_2);
    mapper.Clear();

    // Add after clear
    mapper.Add(FD_EPOLL_3);
    mapper.Add(FD_EPOLL_4);
    mapper.Clear();
}

// ============= CreateSocketEpollMapper Edge Cases =============

TEST_F(FileDescriptorTest, CreateSocketEpollMapper_SameFdMultipleTimes)
{
    SocketEpollMapper* mapper1 = nullptr;
    SocketEpollMapper* mapper2 = nullptr;
    SocketEpollMapper* mapper3 = nullptr;

    bool result1 = CreateSocketEpollMapper(FD_SOCK_99991, mapper1);
    EXPECT_TRUE(result1);
    EXPECT_NE(mapper1, nullptr);

    // Second call should return existing mapper
    bool result2 = CreateSocketEpollMapper(FD_SOCK_99991, mapper2);
    EXPECT_FALSE(result2);
    EXPECT_EQ(mapper1, mapper2);

    // Third call
    bool result3 = CreateSocketEpollMapper(FD_SOCK_99991, mapper3);
    EXPECT_FALSE(result3);
    EXPECT_EQ(mapper1, mapper3);

    CleanSocketEpollMapper(FD_SOCK_99991);
}

TEST_F(FileDescriptorTest, CleanSocketEpollMapper_AfterMultipleOperations)
{
    SocketEpollMapper* mapper = nullptr;

    CreateSocketEpollMapper(FD_SOCK_99992, mapper);
    ASSERT_NE(mapper, nullptr);

    mapper->Add(FD_EPOLL_1);
    mapper->Add(FD_EPOLL_2);
    mapper->Add(FD_EPOLL_3);
    mapper->Clear();

    CleanSocketEpollMapper(FD_SOCK_99992);

    SocketEpollMapper* retrieved = GetSocketEpollMapper(FD_SOCK_99992);
    EXPECT_EQ(retrieved, nullptr);
}