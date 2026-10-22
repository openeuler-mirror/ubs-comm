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
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "under_api/dl_api.h"
#include "urma_backend.h"
#include "urma_context_pool.h"
#include "urma_io_buf_pool.h"
#include "urma_jetty_pool.h"
#include "urma_poller_agg.h"
#include "urma_socket.h"

namespace ock {
namespace ubs {
namespace urma {
void UrmaLog(int level, char *message);
urma_vlog_level_t UrmaGetLogLevel();
} // namespace urma
} // namespace ubs
} // namespace ock

using namespace ock::ubs;
using namespace ock::ubs::urma;

namespace {
urma_status_t g_initReturn = URMA_SUCCESS;
urma_status_t g_registerReturn = URMA_SUCCESS;
uint32_t g_uninitCount = 0;
uint32_t g_logSetCount = 0;

urma_status_t MockInit(urma_init_attr_t *)
{
    return g_initReturn;
}

urma_status_t MockUninit()
{
    ++g_uninitCount;
    return URMA_SUCCESS;
}

urma_status_t MockRegisterLog(urma_log_cb_t)
{
    return g_registerReturn;
}

void MockSetLogLevel(urma_vlog_level_t)
{
    ++g_logSetCount;
}

} // namespace

class UrmaBackendTest : public testing::Test {
protected:
    void SetUp() override
    {
        backend_ = &UrmaBackend::Instance();
        backend_->inited_ = false;
        backend_->urma_inited_ = false;
        backend_->init_urma_only_ = false;
        backend_->io_buff_pool_ = nullptr;
        backend_->context_pool_ = nullptr;
        backend_->poller_set_ = nullptr;
        backend_->jetty_pool_ = nullptr;
        backend_->alive_manager_ = MakeRef<UrmaAliveManager>();
        backend_->ClearPendingClose();
        backend_->broken_conns_.clear();
        g_initReturn = URMA_SUCCESS;
        g_registerReturn = URMA_SUCCESS;
        g_uninitCount = 0;
        g_logSetCount = 0;
        ArraySet<Socket>::GetInstance().Init();
        UrmaApi::urma_init_ptr = MockInit;
        UrmaApi::urma_uninit_ptr = MockUninit;
        UrmaApi::urma_register_log_func_ptr = MockRegisterLog;
        UrmaApi::urma_log_set_level_ptr = MockSetLogLevel;
    }

    void TearDown() override
    {
        ArraySet<Socket>::GetInstance().ReleaseAll();
        backend_->inited_ = false;
        backend_->urma_inited_ = false;
        backend_->io_buff_pool_ = nullptr;
        backend_->context_pool_ = nullptr;
        backend_->poller_set_ = nullptr;
        backend_->jetty_pool_ = nullptr;
        backend_->alive_manager_ = nullptr;
        backend_->ClearPendingClose();
        backend_->broken_conns_.clear();
        GlobalMockObject::verify();
    }

    UrmaBackend *backend_{nullptr};
};

TEST_F(UrmaBackendTest, UrmaCore_InitFailureSuccessAndUninit)
{
    g_initReturn = URMA_FAIL;
    EXPECT_EQ(URMA_FAIL, backend_->InitUrma());
    EXPECT_FALSE(backend_->urma_inited_);

    g_initReturn = URMA_SUCCESS;
    g_registerReturn = URMA_FAIL;
    EXPECT_EQ(UBS_OK, backend_->InitUrma());
    EXPECT_TRUE(backend_->urma_inited_);
    EXPECT_EQ(1u, g_logSetCount);
    backend_->UnInitUrma();
    backend_->UnInitUrma();
    EXPECT_EQ(1u, g_uninitCount);
}

TEST_F(UrmaBackendTest, LoadAndUnloadUrmaApiForwardToDlApi)
{
    MOCKER_CPP(&DlApi::Load).expects(once()).will(returnValue(static_cast<int32_t>(UBS_ERROR)));
    EXPECT_EQ(UBS_ERROR, backend_->LoadUrmaApi());
    GlobalMockObject::verify();

    MOCKER_CPP(&DlApi::Load).expects(once()).will(returnValue(static_cast<int32_t>(UBS_OK)));
    EXPECT_EQ(UBS_OK, backend_->LoadUrmaApi());
    MOCKER_CPP(&DlApi::UnLoad).expects(once());
    backend_->UnLoadUrmaApi();
}

TEST_F(UrmaBackendTest, KeepAliveManagerInitializesDisabledAndUninitializes)
{
    uint32_t savedTime = UrmaSetting::UB_KEEPALIVE_TIME;
    UrmaSetting::UB_KEEPALIVE_TIME = 0;
    EXPECT_EQ(UBS_OK, backend_->InitKeepAliveManager());
    EXPECT_NE(nullptr, backend_->alive_manager_.Get());
    backend_->UnInitKeepAliveManager();
    EXPECT_EQ(nullptr, backend_->alive_manager_.Get());
    UrmaSetting::UB_KEEPALIVE_TIME = savedTime;
}

TEST_F(UrmaBackendTest, SetLogLevelOnlyUpdatesInitializedBackend)
{
    backend_->SetLogLevel(2);
    EXPECT_EQ(0u, g_logSetCount);
    backend_->inited_ = true;
    backend_->SetLogLevel(2);
    EXPECT_EQ(1u, g_logSetCount);
}

TEST_F(UrmaBackendTest, UrmaLogAcceptsMappedAndDriverSpecificLevels)
{
    char message[] = "urma unit test";
    UrmaLog(URMA_VLOG_LEVEL_DEBUG, message);
    UrmaLog(URMA_VLOG_LEVEL_EMERG, message);
    EXPECT_EQ(static_cast<urma_vlog_level_t>(URMA_VLOG_LEVEL_DEBUG - Logger::Instance().GetLogLevel()),
              UrmaGetLogLevel());
}

TEST_F(UrmaBackendTest, Pools_ValidateDependenciesAndExposeMembers)
{
    EXPECT_EQ(UBS_ERROR, backend_->InitJettyPool());
    EXPECT_EQ(UBS_ERROR, backend_->InitPollerSet());
    EXPECT_EQ(UBS_ERROR, backend_->InitIoBufPool());
    EXPECT_EQ(nullptr, backend_->GetContextPoll().Get());
    EXPECT_EQ(nullptr, backend_->GetPollerSet().Get());
    EXPECT_NE(nullptr, backend_->GetAllocator());
    EXPECT_EQ(backend_->GetAllocator(), backend_->GetAllocator());
    backend_->UnInitContextPool();
    backend_->UnInitJettyPool();
    backend_->UnInitPollerSet();
    backend_->UnInitIoBufPool();
}

TEST_F(UrmaBackendTest, BrokenConnections_RecordTakeEraseAndIgnoreInvalid)
{
    backend_->RecordBrokenConn(0, 1);
    backend_->RecordBrokenConn(1, 0);
    EXPECT_EQ(0u, backend_->TakeBrokenConnId(1));
    backend_->RecordBrokenConn(11, 22);
    EXPECT_EQ(22u, backend_->TakeBrokenConnId(11));
    backend_->RecordBrokenConn(11, 23);
    EXPECT_EQ(23u, backend_->TakeBrokenConnId(11));
    backend_->EraseBrokenConn(11);
    EXPECT_EQ(0u, backend_->TakeBrokenConnId(11));
}

TEST_F(UrmaBackendTest, PendingCloseRejectsInvalidAndEmptySlots)
{
    auto sock = MakeRef<UrmaSocket>(100);
    backend_->EnsurePendingCloseRegistry();
    backend_->HoldPendingClose(-1, sock.Get());
    backend_->ReleasePendingClose(-1);
    EXPECT_FALSE(backend_->HasPendingClose(-1));
    backend_->ReleasePendingClose(100);

    backend_->HoldPendingClose(100, sock.Get());
    backend_->HoldPendingClose(100, sock.Get());
    EXPECT_TRUE(backend_->HasPendingClose(100));
    backend_->ReleasePendingClose(100);
    EXPECT_FALSE(backend_->HasPendingClose(100));
}

TEST_F(UrmaBackendTest, AllocConnIdIsMonotonicAndNeverZero)
{
    auto first = backend_->AllocConnId();
    auto second = backend_->AllocConnId();
    EXPECT_GT(first, 0u);
    EXPECT_EQ(first + 1, second);
}

TEST_F(UrmaBackendTest, HandleCqeIgnoresUnsupportedOrMissingSockets)
{
    urma_cr_t crs[4]{};
    crs[0].status = URMA_CR_SUCCESS;
    crs[0].flag.bs.s_r = 0;
    crs[1].status = URMA_CR_SUCCESS;
    crs[1].flag.bs.s_r = 0;
    crs[1].user_ctx = SenderCRContext{READ_DATA_REQUEST, 1, 777}.value;
    crs[2].status = URMA_CR_SUCCESS;
    crs[2].opcode = URMA_CR_OPC_WRITE_WITH_IMM;
    crs[3].status = URMA_CR_SUCCESS;
    crs[3].opcode = URMA_CR_OPC_WRITE_WITH_IMM;
    crs[3].user_ctx = SenderCRContext{CTRL_DATA_RECEiVE, 0, 778}.value;
    EXPECT_EQ(UBS_OK, backend_->HandleCqe(0, 4, crs));

    urma_cr_t error{};
    error.status = URMA_CR_LOC_OPERATION_ERR;
    backend_->HandleErrorCqe(&error);
    error.user_ctx = SenderCRContext{READ_DATA_REQUEST, 1, 779}.value;
    backend_->HandleErrorCqe(&error);
}

TEST_F(UrmaBackendTest, HandleCqeDispatchesValidSocketAndErrorPaths)
{
    constexpr int fd = 777;
    auto urmaSocket = MakeRef<UrmaSocket>(fd);
    urmaSocket->event_fd_ = -1;
    SocketPtr socket = RefConvert<UrmaSocket, Socket>(urmaSocket);
    ArraySet<Socket>::GetInstance().OverrideItem(fd, socket.Get());

    MOCKER_CPP(&UrmaSocket::DataReadFinished).expects(once());
    MOCKER_CPP(&UrmaSocket::PostEmptyRecvWorkerRecords).expects(once());

    urma_cr_t crs[3]{};
    crs[0].status = URMA_CR_SUCCESS;
    crs[0].flag.bs.s_r = 0;
    crs[0].user_ctx = SenderCRContext{SHUTDOWN_NOTIFY, 2, fd}.value;
    crs[1].status = URMA_CR_SUCCESS;
    crs[1].flag.bs.s_r = 0;
    crs[1].user_ctx = SenderCRContext{READ_DATA_REQUEST, 3, fd}.value;
    crs[2].status = URMA_CR_SUCCESS;
    crs[2].flag.bs.s_r = 1;
    crs[2].opcode = URMA_CR_OPC_WRITE_WITH_IMM;
    crs[2].user_ctx = SenderCRContext{CTRL_DATA_RECEiVE, 4, fd}.value;
    WriteImmData imm{CLOSE, 0};
    crs[2].imm_data = imm.data;
    EXPECT_EQ(UBS_OK, backend_->HandleCqe(0, 3, crs));

    urma_cr_t error{};
    error.user_ctx = SenderCRContext{CTRL_DATA_REQUEST, 5, fd}.value;
    error.status = URMA_CR_LOC_LEN_ERR;
    backend_->HandleErrorCqe(&error);
    EXPECT_FALSE(urmaSocket->IsPeerReset());

    error.status = URMA_CR_LOC_OPERATION_ERR;
    backend_->HandleErrorCqe(&error);
    EXPECT_TRUE(urmaSocket->IsClosed());
}

TEST_F(UrmaBackendTest, FactoryCreatesAllOperationObjects)
{
    auto urmaSocket = MakeRef<UrmaSocket>(55);
    SocketPtr socket = RefConvert<UrmaSocket, Socket>(urmaSocket);
    DataTxOps *tx = nullptr;
    DataRxOps *rx = nullptr;
    AcceptorOps *acceptor = nullptr;
    ConnectorOps *connector = nullptr;
    EXPECT_EQ(UBS_OK, UrmaBackend::CreateTxOps(SocketType::SOCK_TYPE_URMA, socket, tx));
    EXPECT_EQ(UBS_OK, UrmaBackend::CreateRxOps(SocketType::SOCK_TYPE_URMA, socket, rx));
    EXPECT_EQ(UBS_OK, UrmaBackend::CreateAcceptorOps(SocketType::SOCK_TYPE_URMA, socket, acceptor));
    EXPECT_EQ(UBS_OK, UrmaBackend::CreateConnectorOps(SocketType::SOCK_TYPE_URMA, socket, connector));
    delete tx;
    delete rx;
    delete acceptor;
    delete connector;
    SocketPtr out;
    EXPECT_EQ(UBS_OK, backend_->CreateSocket(1, SocketType::SOCK_TYPE_URMA, out));
}

TEST_F(UrmaBackendTest, ObjectPoolsInitializeAndUninitialize)
{
    EXPECT_EQ(UBS_OK, backend_->InitObjectPool());
    backend_->UnInitObjectPool();
}

TEST_F(UrmaBackendTest, InitReturnsImmediatelyWhenAlreadyInitialized)
{
    backend_->inited_ = true;
    EXPECT_EQ(UBS_OK, backend_->Init(false));
}

TEST_F(UrmaBackendTest, InitUrmaOnlyRunsCoreStages)
{
    MOCKER_CPP(&UrmaBackend::LoadUrmaApi).expects(once()).will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaBackend::InitUrma).expects(once()).will(returnValue(static_cast<int32_t>(UBS_OK)));

    EXPECT_EQ(UBS_OK, backend_->Init(true));
    EXPECT_TRUE(backend_->init_urma_only_);
    EXPECT_FALSE(backend_->inited_);
}

TEST_F(UrmaBackendTest, InitCleansUpWhenCoreStagesFail)
{
    MOCKER_CPP(&UrmaBackend::LoadUrmaApi).expects(once()).will(returnValue(static_cast<int32_t>(UBS_ERROR)));
    MOCKER_CPP(&UrmaBackend::UnInitInner).expects(once());
    EXPECT_EQ(UBS_ERROR, backend_->Init(true));
}

TEST_F(UrmaBackendTest, InitCleansUpWhenUrmaInitFails)
{
    MOCKER_CPP(&UrmaBackend::LoadUrmaApi).expects(once()).will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaBackend::InitUrma).expects(once()).will(returnValue(static_cast<int32_t>(UBS_ERROR)));
    MOCKER_CPP(&UrmaBackend::UnInitInner).expects(once());
    EXPECT_EQ(UBS_ERROR, backend_->Init(true));
}

TEST_F(UrmaBackendTest, FullInitRunsAllUpperLayerStages)
{
    backend_->urma_inited_ = true;
    MOCKER_CPP(&UrmaBackend::InitContextPool).expects(once()).will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaBackend::InitPollerSet).expects(once()).will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaBackend::InitJettyPool).expects(once()).will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaBackend::InitIoBufPool).expects(once()).will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP(&UrmaBackend::InitObjectPool).expects(once()).will(returnValue(static_cast<int32_t>(UBS_OK)));

    EXPECT_EQ(UBS_OK, backend_->Init(false));
    EXPECT_TRUE(backend_->inited_);
}

TEST_F(UrmaBackendTest, FullInitCleansUpForEveryFailedUpperLayerStage)
{
    using InitStage = ock::ubs::Result (UrmaBackend::*)() noexcept;
    const InitStage stages[] = {&UrmaBackend::InitContextPool, &UrmaBackend::InitPollerSet, &UrmaBackend::InitJettyPool,
                                &UrmaBackend::InitIoBufPool, &UrmaBackend::InitObjectPool};
    for (auto failedStage : stages) {
        backend_->urma_inited_ = true;
        for (auto stage : stages) {
            auto builder = MOCKER_CPP(stage).expects(once());
            builder.will(returnValue(static_cast<int32_t>(stage == failedStage ? UBS_ERROR : UBS_OK)));
            if (stage == failedStage) {
                break;
            }
        }
        MOCKER_CPP(&UrmaBackend::UnInitInner).expects(once());
        EXPECT_EQ(UBS_ERROR, backend_->Init(false));
        GlobalMockObject::verify();
    }
}

TEST_F(UrmaBackendTest, UnInitAndInnerDispatchCleanupInReverseOrder)
{
    backend_->UnInit();

    backend_->inited_ = true;
    MOCKER_CPP(&UrmaBackend::ClearPendingClose).expects(once());
    MOCKER_CPP(&UrmaBackend::UnInitInner).expects(once());
    backend_->UnInit();
    EXPECT_FALSE(backend_->inited_);

    GlobalMockObject::verify();
    backend_->init_urma_only_ = false;
    MOCKER_CPP(&UrmaBackend::UnInitPollerSet).expects(once());
    MOCKER_CPP(&UrmaBackend::UnInitObjectPool).expects(once());
    MOCKER_CPP(&UrmaBackend::UnInitIoBufPool).expects(once());
    MOCKER_CPP(&UrmaBackend::UnInitJettyPool).expects(once());
    MOCKER_CPP(&UrmaBackend::UnInitContextPool).expects(once());
    MOCKER_CPP(&UrmaBackend::UnInitUrma).expects(once());
    MOCKER_CPP(&UrmaBackend::UnLoadUrmaApi).expects(once());
    backend_->UnInitInner();
}

TEST_F(UrmaBackendTest, JettyPoolInitializationHandlesFailureAndSuccess)
{
    backend_->context_pool_ = MakeRef<UrmaContextPool>();
    MOCKER_CPP(&UrmaJettyPool::Init).stubs().will(returnValue(static_cast<int32_t>(UBS_ERROR)));
    EXPECT_EQ(UBS_ERROR, backend_->InitJettyPool());
    EXPECT_EQ(nullptr, backend_->jetty_pool_.Get());

    GlobalMockObject::verify();
    MOCKER_CPP(&UrmaJettyPool::Init).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    EXPECT_EQ(UBS_OK, backend_->InitJettyPool());
    ASSERT_NE(nullptr, backend_->jetty_pool_.Get());
    MOCKER_CPP(&UrmaJettyPool::UnInit).stubs();
    backend_->UnInitJettyPool();
    EXPECT_EQ(nullptr, backend_->jetty_pool_.Get());
}

TEST_F(UrmaBackendTest, PollerSetInitializationHandlesFailureAndSuccess)
{
    backend_->context_pool_ = MakeRef<UrmaContextPool>();
    MOCKER_CPP(&UrmaAggPollerSet::Init).stubs().will(returnValue(static_cast<int32_t>(UBS_ERROR)));
    EXPECT_EQ(UBS_ERROR, backend_->InitPollerSet());
    EXPECT_EQ(nullptr, backend_->poller_set_.Get());

    GlobalMockObject::verify();
    MOCKER_CPP(&UrmaAggPollerSet::Init).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    EXPECT_EQ(UBS_OK, backend_->InitPollerSet());
    ASSERT_NE(nullptr, backend_->poller_set_.Get());
    MOCKER_CPP(&UrmaAggPollerSet::UnInit).stubs();
    backend_->UnInitPollerSet();
}

TEST_F(UrmaBackendTest, IoBufferPoolInitializationHandlesFailureAndSuccess)
{
    using InitWithOptions = ock::ubs::Result (UrmaIoBufferPool::*)(const allocator::FixedAllocatorOptions &,
                                                                   const UrmaContextPoolPtr &) noexcept;
    backend_->context_pool_ = MakeRef<UrmaContextPool>();
    MOCKER_CPP((static_cast<InitWithOptions>(&UrmaIoBufferPool::init)))
        .stubs()
        .will(returnValue(static_cast<int32_t>(UBS_ERROR)));
    EXPECT_EQ(UBS_ERROR, backend_->InitIoBufPool());

    GlobalMockObject::verify();
    MOCKER_CPP((static_cast<InitWithOptions>(&UrmaIoBufferPool::init)))
        .stubs()
        .will(returnValue(static_cast<int32_t>(UBS_OK)));
    EXPECT_EQ(UBS_OK, backend_->InitIoBufPool());
    ASSERT_NE(nullptr, backend_->io_buff_pool_.Get());
    EXPECT_EQ(backend_->io_buff_pool_.Get(), g_zcopy_allocator);
    MOCKER_CPP(&UrmaIoBufferPool::un_init).stubs();
    backend_->UnInitIoBufPool();
    EXPECT_EQ(nullptr, backend_->io_buff_pool_.Get());
}

TEST_F(UrmaBackendTest, ContextPoolUninitReleasesStoredPool)
{
    backend_->context_pool_ = MakeRef<UrmaContextPool>();
    MOCKER_CPP(&UrmaContextPool::UnInit).stubs();
    backend_->UnInitContextPool();
    EXPECT_EQ(nullptr, backend_->context_pool_.Get());
}
