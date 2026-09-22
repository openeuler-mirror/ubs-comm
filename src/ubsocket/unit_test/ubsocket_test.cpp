/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You may obtain a copy of the Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 */

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include <cerrno>

#include "common/ubsocket_errno.h"
#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_profiling.h"
#include "common/ubsocket_thread_pool.h"
#include "common/ubsocket_version.h"
#include "iobuf/ubsocket_zcopy_adapter.h"
#include "under_api/dl_api.h"
#include "profiling/trace/ubsocket_trace.h"
#include "core/umq/umq_backend.h"
#include "include/ubsocket.h"
#include "include/ubsocket_def.h"
#include "umq/umq_types.h"
#include "umq/umq_api.h"

void UmqLogger(int level, char *logMsg);
void UmqExtLogger(int level, const char *file, const char *function, int line, char *logMsg);

class TestAllocator final : public ock::ubs::UbsZeroCopyAllocator {
public:
    void *allocate(size_t, const ubs_iobuf_alloc_option_t *) override
    {
        ++allocateCount;
        return &storage;
    }
    void deallocate(void *) override
    {
        ++deallocateCount;
    }
    int allocateCount = 0;
    int deallocateCount = 0;
    int storage = 0;
};

using namespace ock::ubs;

TEST(UbsocketTest, InitOptionsRejectsNull)
{
    errno = 0;
    EXPECT_EQ(UBS_ERROR, ubsocket_init_options(nullptr));
    EXPECT_EQ(EINVAL, errno);
}

TEST(UbsocketTest, InitOptionsSetsDocumentedDefaults)
{
    u_external_lock_ops_t lockOps{};
    u_external_rw_lock_ops_t rwLockOps{};
    u_external_semaphore_ops_t semOps{};
    u_external_rpc_id_ops_t rpcIdOps{};
    u_external_poller_ops_t pollerOps{};
    u_init_options_t options{};
    options.allowed_protocol = 0xFFFFFFFFU;
    options.async_acceptor_thread_count = 7U;
    options.async_connector_thread_count = 8U;
    options.async_epoll_thread_count = 9U;
    options.lock_ops = &lockOps;
    options.rw_lock_ops = &rwLockOps;
    options.sem_ops = &semOps;
    options.rpc_id_ops = &rpcIdOps;
    options.poller_ops = &pollerOps;

    EXPECT_EQ(UBS_OK, ubsocket_init_options(&options));
    EXPECT_EQ(UBS_PROTOCOL_TCP, options.allowed_protocol);
    EXPECT_EQ(0U, options.async_acceptor_thread_count);
    EXPECT_EQ(0U, options.async_connector_thread_count);
    EXPECT_EQ(1U, options.async_epoll_thread_count);
    EXPECT_EQ(nullptr, options.lock_ops);
    EXPECT_EQ(nullptr, options.rw_lock_ops);
    EXPECT_EQ(nullptr, options.sem_ops);
    EXPECT_EQ(nullptr, options.rpc_id_ops);
    EXPECT_EQ(nullptr, options.poller_ops);
}

TEST(UbsocketTest, VersionReturnsConfiguredShortVersion)
{
    EXPECT_STREQ(UBS_LIB_VERSION, ubsocket_version());
}

TEST(UbsocketTest, SetLogLevelAcceptsConfiguredValue)
{
    EXPECT_EQ(UBS_OK, ubsocket_set_log_level(3));
}

TEST(UbsocketTest, SetLoggerPropagatesUmqConfigResult)
{
    MOCKER_CPP(::umq_log_config_set).stubs().will(returnValue(-1));
    EXPECT_EQ(-1, ubsocket_set_logger(nullptr));
    GlobalMockObject::verify();
}

TEST(UbsocketTest, UmqLoggersHandleErrorAndOtherLevels)
{
    char message[] = "unit-test";
    UmqLogger(static_cast<int>(umq_log_level::UMQ_LOG_LEVEL_DEBUG), message);
    UmqLogger(static_cast<int>(umq_log_level::UMQ_LOG_LEVEL_DEBUG) + 1, message);
    UmqExtLogger(static_cast<int>(umq_log_level::UMQ_LOG_LEVEL_DEBUG), "file", "func", 1, message);
    UmqExtLogger(static_cast<int>(umq_log_level::UMQ_LOG_LEVEL_DEBUG) + 1, "file", "func", 1, message);
}

TEST(UbsocketTest, IobufApisAreSafeBeforeAllocatorInitialization)
{
    g_zcopy_allocator = nullptr;
    EXPECT_EQ(nullptr, ubsocket_iobuf_allocate(64, nullptr));
    ubsocket_iobuf_deallocate(nullptr);
    int marker = 0;
    ubsocket_iobuf_deallocate(&marker);
}

TEST(UbsocketTest, IobufApisDelegateToConfiguredAllocator)
{
    TestAllocator allocator;
    g_zcopy_allocator = &allocator;
    EXPECT_EQ(&allocator.storage, ubsocket_iobuf_allocate(16, nullptr));
    ubsocket_iobuf_deallocate(&allocator.storage);
    EXPECT_EQ(1, allocator.allocateCount);
    EXPECT_EQ(1, allocator.deallocateCount);
    g_zcopy_allocator = nullptr;
}

TEST(UbsocketTest, InitRejectsNullOptions)
{
    errno = 0;
    EXPECT_EQ(UBS_ERROR, ubsocket_init(nullptr));
    EXPECT_EQ(EINVAL, errno);
}

TEST(UbsocketTest, InitReturnsImmediatelyWhenAlreadyInitialized)
{
    u_init_options_t options{};
    GlobalSetting::UBS_INITED = true;
    EXPECT_EQ(UBS_OK, ubsocket_init(&options));
    GlobalSetting::UBS_INITED = false;
}

TEST(UbsocketTest, InitRejectsInvalidGlobalSetting)
{
    u_init_options_t options{};
    GlobalSetting::UBS_INITED = false;
    MOCKER(&GlobalSetting::VerifySetting).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));
    errno = 0;
    EXPECT_EQ(UBS_ERROR, ubsocket_init(&options));
    EXPECT_EQ(EINVAL, errno);
    GlobalMockObject::verify();
}

TEST(UbsocketTest, InitReportsUnderApiLoadFailure)
{
    u_init_options_t options{};
    GlobalSetting::UBS_INITED = false;
    MOCKER(&GlobalSetting::VerifySetting).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&DlApi::Load).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));
    errno = 0;
    EXPECT_EQ(UBS_ERROR, ubsocket_init(&options));
    EXPECT_EQ(EBADF, errno);
    GlobalMockObject::verify();
}

TEST(UbsocketTest, InitRejectsExternalLockRegistrationFailure)
{
    u_init_options_t options{};
    u_external_lock_ops_t lockOps{};
    options.lock_ops = &lockOps;
    MOCKER(&GlobalSetting::VerifySetting).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&DlApi::Load).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&LockRegistry::RegisterLockOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));
    errno = 0;
    EXPECT_EQ(UBS_ERROR, ubsocket_init(&options));
    EXPECT_EQ(EBADF, errno);
    GlobalMockObject::verify();
}

TEST(UbsocketTest, InitRejectsExternalRwLockRegistrationFailure)
{
    u_init_options_t options{};
    u_external_rw_lock_ops_t rwLockOps{};
    options.rw_lock_ops = &rwLockOps;
    MOCKER(&GlobalSetting::VerifySetting).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&DlApi::Load).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&LockRegistry::RegisterRwLockOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));
    errno = 0;
    EXPECT_EQ(UBS_ERROR, ubsocket_init(&options));
    EXPECT_EQ(EBADF, errno);
    GlobalMockObject::verify();
}

TEST(UbsocketTest, InitRejectsExternalSemaphoreRegistrationFailure)
{
    u_init_options_t options{};
    u_external_semaphore_ops_t semOps{};
    options.sem_ops = &semOps;
    MOCKER(&GlobalSetting::VerifySetting).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&DlApi::Load).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&LockRegistry::RegisterSemOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));
    errno = 0;
    EXPECT_EQ(UBS_ERROR, ubsocket_init(&options));
    EXPECT_EQ(EBADF, errno);
    GlobalMockObject::verify();
}

TEST(UbsocketTest, InitRejectsExternalRpcIdRegistrationFailure)
{
    u_init_options_t options{};
    u_external_rpc_id_ops_t rpcIdOps{};
    options.rpc_id_ops = &rpcIdOps;
    MOCKER(&GlobalSetting::VerifySetting).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&DlApi::Load).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&TraceRegistry::RegisterRpcIdOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));
    errno = 0;
    EXPECT_EQ(UBS_ERROR, ubsocket_init(&options));
    EXPECT_EQ(EBADF, errno);
    GlobalMockObject::verify();
}

TEST(UbsocketTest, InitCompletesWithBackendIsolated)
{
    u_init_options_t options{};
    u_external_poller_ops_t pollerOps{};
    options.allowed_protocol = UBS_PROTOCOL_UB_RM_RTP;
    options.async_acceptor_thread_count = 1U;
    options.async_connector_thread_count = 1U;
    options.async_epoll_thread_count = 1U;
    options.poller_ops = &pollerOps;
    GlobalSetting::UBS_INITED = false;
    GlobalSetting::UBS_PROF_ENABLE = true;
    GlobalSetting::UBS_PROBE_ENABLED = false;
    GlobalSetting::UBS_CLI_ENABLED = false;
    GlobalSetting::UBS_ALLOWED_PROTOCOL = UBS_PROTOCOL_TCP;
    MOCKER(&Profiling::Init).stubs().will(returnValue(0));
    MOCKER(&Profiling::Uninit).stubs().will(returnValue(0));
    MOCKER(&umq::UmqBackend::Init).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    EXPECT_EQ(UBS_OK, ubsocket_init(&options));
    EXPECT_TRUE(GlobalSetting::UBS_INITED);
    ubsocket_uninit();
    GlobalMockObject::verify();
}

TEST(UbsocketTest, InitContinuesWhenAsyncAcceptorStartFails)
{
    u_init_options_t options{};
    options.async_acceptor_thread_count = 1U;
    /* 上一用例 InitCompletesWithBackendIsolated 的 ubsocket_uninit() 会不可逆地置位
     * UBS_EXITING。本用例测的是"正常 init 流程"，先复位退出标志与 UBS_INITED，
     * 模拟未 uninit 的干净初始状态，避免被 init 的重新初始化守卫拦截。 */
    GlobalSetting::UBS_INITED = false;
    GlobalSetting::UBS_EXITING.store(false, std::memory_order_release);
    GlobalSetting::UBS_PROF_ENABLE = false;
    GlobalSetting::UBS_PROBE_ENABLED = false;
    GlobalSetting::UBS_CLI_ENABLED = false;
    MOCKER(&umq::UmqBackend::Init).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(&ExecutorService::Start).stubs().will(returnValue(false));
    EXPECT_EQ(UBS_OK, ubsocket_init(&options));
    EXPECT_TRUE(GlobalSetting::UBS_INITED);
    ubsocket_uninit();
    GlobalMockObject::verify();
}
