/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * This SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <fcntl.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <memory>
#include <vector>

#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_version.h"
#include "core/ubsocket_socket_helper.h"
#include "core/urma/urma_backend.h"
#include "core/urma/urma_conn_ops.h"
#include "core/urma/urma_setting.h"
#include "core/urma/urma_socket.h"
#include "core/urma/urma_socket_seg_ex.h"
#include "under_api/dl_libc_api.h"

using namespace ock::ubs;
using namespace ock::ubs::urma;

namespace {
static const int TEST_FD = 100;
static const int TEST_EVENT_FD = 700;

/* ---------- recv 行为序列 ---------- */
struct RecvChunk {
    int ret = 1;
    int err = 0;
    std::vector<uint8_t> data;
    size_t off = 0;
};

static std::vector<RecvChunk> g_recv_chunks;
static size_t g_recv_idx = 0;

static ssize_t MockRecvSeq(int, void *buf, size_t len, int)
{
    while (g_recv_idx < g_recv_chunks.size()) {
        RecvChunk &chunk = g_recv_chunks[g_recv_idx];
        if (chunk.ret < 0) {
            ++g_recv_idx;
            errno = chunk.err;
            return -1;
        }
        if (chunk.ret == 0) {
            ++g_recv_idx;
            return 0;
        }
        size_t avail = chunk.data.size() - chunk.off;
        if (avail > 0) {
            size_t n = (len < avail) ? len : avail;
            memcpy(buf, chunk.data.data() + chunk.off, n);
            chunk.off += n;
            return static_cast<ssize_t>(n);
        }
        ++g_recv_idx; /* 当前分片耗尽：继续服务下一个分片 */
    }
    errno = EAGAIN; /* socket 缓冲全空才是 EAGAIN */
    return -1;
}

static void PushRecvData(const void *data, size_t size)
{
    RecvChunk chunk;
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    chunk.data.assign(bytes, bytes + size);
    g_recv_chunks.push_back(chunk);
}

static void PushRecvError(int err)
{
    RecvChunk chunk;
    chunk.ret = -1;
    chunk.err = err;
    g_recv_chunks.push_back(chunk);
}

/* ---------- send 行为序列：>0 发送 min(ret,len) 字节(可截断)；0 返回0；-1 发送全部；-2 EAGAIN ---------- */
static std::vector<ssize_t> g_send_results;
static size_t g_send_idx = 0;
static std::vector<uint8_t> g_sent_bytes;

static ssize_t MockSendSeq(int, const void *buf, size_t len, int)
{
    if (g_send_idx >= g_send_results.size()) {
        errno = EAGAIN;
        return -1;
    }
    ssize_t ret = g_send_results[g_send_idx++];
    if (ret == -2) {
        errno = EAGAIN;
        return -1;
    }
    if (ret == 0) {
        return 0;
    }
    size_t n = (ret < 0 || static_cast<size_t>(ret) > len) ? len : static_cast<size_t>(ret);
    const uint8_t *bytes = static_cast<const uint8_t *>(buf);
    g_sent_bytes.insert(g_sent_bytes.end(), bytes, bytes + n);
    return static_cast<ssize_t>(n);
}

static void PushSendOk(ssize_t bytes = -1)
{
    g_send_results.push_back(bytes);
}

static void PushSendWouldBlock()
{
    g_send_results.push_back(-2);
}

/* ---------- fcntl 状态化 mock ---------- */
static int g_fcntl_flags = O_NONBLOCK;
static int g_fcntl_setfl_calls = 0;
static int g_rm_remote_calls = 0;
static int MockFcntlStateful(int fd, int cmd, ...)
{
    (void)fd;
    va_list va;
    va_start(va, cmd);
    if (cmd == F_GETFL) {
        return g_fcntl_flags;
    }
    if (cmd == F_SETFL) {
        g_fcntl_flags = va_arg(va, int);
        ++g_fcntl_setfl_calls;
        return 0;
    }
    return -1;
}

/* ---------- 其它 libc mock ---------- */
static int g_so_error = 0;
static int MockGetsockoptZero(int, int, int, void *optval, socklen_t *optlen)
{
    if (optval != nullptr && optlen != nullptr) {
        *static_cast<int *>(optval) = g_so_error;
    }
    return 0;
}

static int MockSetSockoptOk(int, int, int, const void *, socklen_t)
{
    return 0;
}

static int MockShutdownOk(int, int)
{
    return 0;
}

/* 生产签名: rm_remote_segments(UrmaContextPtr ctx, const IdentifyNode &remote), handler 参数须一致 */
static void MockRmRemoteSegments(UrmaContextPtr, const IdentifyNode &)
{
    ++g_rm_remote_calls;
}

/* ---------- 构造辅助 ---------- */
static struct sockaddr_in MakeTestAddr()
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(12345);
    return addr;
}

static const struct sockaddr &TestAddr()
{
    static struct sockaddr_in addr = MakeTestAddr();
    return *reinterpret_cast<const struct sockaddr *>(&addr);
}

static UrmaSocketPtr MakeTestUrmaSocket(int fd = TEST_FD)
{
    auto sock = MakeRef<UrmaSocket>(fd);
    sock->recv_buf_addr_ = 0;
    sock->send_buf_addr_ = 0;
    sock->recv_buf_size_ = 0;
    sock->send_buf_size_ = 0;
    return sock;
}

/* test-only ConnectorOps：避免 mockcpp 对虚函数 PrepareConnect 的 vtable 替换，行为经成员变量可控。
   mockcpp.hpp 引入 mockcpp::Result，与 ock::ubs::Result 歧义，返回类型必须显式限定。 */
class TestConnectorOps : public ConnectorOps {
public:
    TestConnectorOps()
    {
        conn_info.type_fd = 0;
    }

    ock::ubs::Result PrepareConnect(int, const struct sockaddr *, socklen_t, const SocketPtr &) override
    {
        return prepare_result;
    }

    ock::ubs::Result Negotiate(int, const SocketPtr &) override
    {
        return UBS_OK;
    }

    ock::ubs::Result CreateSocketResources(SocketPtr &) override
    {
        return UBS_OK;
    }

    void DestroySocketResources() override {}

    ock::ubs::Result BuildTfoPayload(uint8_t *, int &, const SocketPtr &) override
    {
        return UBS_OK;
    }

    ock::ubs::Result prepare_result = UBS_OK;
};

static std::shared_ptr<UrmaConnOps> MakeClientOps(SocketPtr &sock_ptr, ConnectorOpsPtr &conn_ops_ptr)
{
    UrmaSocketPtr sock = MakeTestUrmaSocket();
    sock_ptr = RefConvert<UrmaSocket, Socket>(sock);
    auto connector = MakeRef<TestConnectorOps>();
    conn_ops_ptr = RefConvert<TestConnectorOps, ConnectorOps>(connector);
    return std::make_shared<UrmaConnOps>(ConnRole::kClient, TEST_FD, sock_ptr, conn_ops_ptr, TestAddr(),
                                         sizeof(struct sockaddr_in));
}

static TestConnectorOps *RawConnectorOps(const ConnectorOpsPtr &conn_ops)
{
    return RefStaticCastToRaw<TestConnectorOps>(conn_ops);
}

static std::shared_ptr<UrmaConnOps> MakeClientOpsInState(SocketPtr &sock_ptr, ConnectorOpsPtr &conn_ops_ptr,
                                                         ConnState state)
{
    auto ops = MakeClientOps(sock_ptr, conn_ops_ptr);
    ops->state_.store(state, std::memory_order_release);
    return ops;
}

static std::shared_ptr<UrmaConnOps> MakeServerOps()
{
    return std::make_shared<UrmaConnOps>(ConnRole::kServer, TEST_FD, nullptr, nullptr, TestAddr(),
                                         sizeof(struct sockaddr_in));
}

static std::vector<uint8_t> MakeLpFrame(const void *body, size_t body_size)
{
    uint32_t body_len = static_cast<uint32_t>(body_size);
    std::vector<uint8_t> frame(sizeof(body_len) + body_size);
    memcpy(frame.data(), &body_len, sizeof(body_len));
    memcpy(frame.data() + sizeof(body_len), body, body_size);
    return frame;
}

static UrmaNegotiateReq MakeCompatReq()
{
    UrmaNegotiateReq req = {};
    req.peer_version = UBS_PROTOCOL_VERSION.GetWhole();
    req.trans_mode = RM_CTP;
    req.send_buf_kb = URMA_SEND_BUF_DEFAULT_KB;
    req.recv_buf_kb = URMA_RECV_BUF_DEFAULT_KB;
    req.conn_flag = URMA_SOCKET_MASK_DEGRADE;
    return req;
}

static UrmaNegotiateRsp MakeCompatRsp(int ret_code = 0)
{
    UrmaNegotiateRsp rsp = {};
    rsp.ret_code = ret_code;
    rsp.negotiated_version = UBS_PROTOCOL_VERSION.GetWhole();
    rsp.trans_mode = RM_CTP;
    rsp.send_buf_kb = URMA_SEND_BUF_DEFAULT_KB;
    rsp.recv_buf_kb = URMA_RECV_BUF_DEFAULT_KB;
    rsp.conn_flag = URMA_SOCKET_MASK_DEGRADE;
    return rsp;
}

static ock::ubs::Result MockCreateUrmaSocket(int fd, SocketType, SocketPtr &sock)
{
    UrmaSocketPtr created = MakeTestUrmaSocket(fd);
    sock = RefConvert<UrmaSocket, Socket>(created);
    return UBS_OK;
}
} // namespace

class UrmaConnOpsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        GlobalSetting::UBS_TRACE_ENABLED = false;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
        GlobalSetting::UBS_ENABLE_DEGRADE = true;
        GlobalSetting::UBS_BACKUP_LINK_ENABLED = false;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        UrmaSetting::Init();
        LibcApi::Load();

        g_recv_chunks.clear();
        g_recv_idx = 0;
        g_send_results.clear();
        g_send_idx = 0;
        g_sent_bytes.clear();
        g_fcntl_flags = O_NONBLOCK;
        g_fcntl_setfl_calls = 0;
        g_so_error = 0;
        g_rm_remote_calls = 0;

        LibcApi::recv_ptr = MockRecvSeq;
        LibcApi::send_ptr = MockSendSeq;
        LibcApi::fcntl_ptr = MockFcntlStateful;
        LibcApi::getsockopt_ptr = MockGetsockoptZero;
        LibcApi::setsockopt_ptr = MockSetSockoptOk;
    }

    void TearDown() override
    {
        errno = 0;
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        LibcApi::UnLoad();
        LibcApi::Load();
    }
};

/* ==================== 构造 ==================== */

TEST_F(UrmaConnOpsTest, ClientConstruct_InitialStateWaitTcpConnect)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOps(sock_ptr, conn_ops);

    EXPECT_EQ(ConnState::kWaitTcpConnect, ops->State());
    EXPECT_FALSE(ops->CloseOnFailure());
    EXPECT_NE(nullptr, ops->urma_sock_);
    EXPECT_EQ(TEST_FD, ops->Fd());
}

TEST_F(UrmaConnOpsTest, ServerConstruct_NonBlockingFd_DetectsWasNotBlocking)
{
    g_fcntl_flags = O_NONBLOCK;
    auto ops = MakeServerOps();
    EXPECT_EQ(ConnState::kWaitNegotiate, ops->State());
    EXPECT_TRUE(ops->CloseOnFailure());
    EXPECT_FALSE(ops->was_blocking_);
}

TEST_F(UrmaConnOpsTest, ServerConstruct_BlockingFd_WasBlockingTrue)
{
    g_fcntl_flags = 0; /* 无 O_NONBLOCK：阻塞态 */
    auto ops = MakeServerOps();
    EXPECT_TRUE(ops->was_blocking_);
}

/* ==================== DoWaitTcpConnect (Client 阶段 A) ==================== */

TEST_F(UrmaConnOpsTest, DoWaitTcpConnect_PrepareConnectFails_ReturnsFailed)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOps(sock_ptr, conn_ops);
    RawConnectorOps(conn_ops)->prepare_result = UBS_ERROR;

    EXPECT_EQ(ConnStageResult::kFailed, ops->Execute(1000));
    EXPECT_EQ(ECONNREFUSED, ops->FailureErrno());
    EXPECT_FALSE(ops->degradable_);
}

TEST_F(UrmaConnOpsTest, DoWaitTcpConnect_FirstInvoke_StartsConnectAndWaitsPollout)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOps(sock_ptr, conn_ops);

    EXPECT_EQ(ConnStageResult::kPending, ops->Execute(1000));
    EXPECT_TRUE(ops->tcp_connect_started_);
    EXPECT_TRUE(ops->WantsWritable());
}

TEST_F(UrmaConnOpsTest, DoWaitTcpConnect_SoErrorNonZero_ReturnsFailedWithErrno)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOps(sock_ptr, conn_ops);
    ops->tcp_connect_started_ = true;

    g_so_error = EADDRNOTAVAIL;
    EXPECT_EQ(ConnStageResult::kFailed, ops->Execute(1000));
    EXPECT_EQ(EADDRNOTAVAIL, ops->FailureErrno());
}

TEST_F(UrmaConnOpsTest, DoWaitTcpConnect_NotUbsConnection_DegradesToTcpAndDone)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOps(sock_ptr, conn_ops);
    ops->tcp_connect_started_ = true;

    MOCKER_CPP(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(false));
    MOCKER_CPP(&UrmaConnOps::DegradeToTcp).stubs().will(returnValue(true));

    EXPECT_EQ(ConnStageResult::kDone, ops->Execute(1000));
    EXPECT_EQ(ConnState::kConnEstablished, ops->State());
    EXPECT_TRUE(ops->degraded_);
    GlobalMockObject::verify();
}

TEST_F(UrmaConnOpsTest, DoWaitTcpConnect_NotUbsConnection_DegradeFails_ReturnsFailed)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOps(sock_ptr, conn_ops);
    ops->tcp_connect_started_ = true;

    MOCKER_CPP(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(false));
    MOCKER_CPP(&UrmaConnOps::DegradeToTcp).stubs().will(returnValue(false));

    EXPECT_EQ(ConnStageResult::kFailed, ops->Execute(1000));
    EXPECT_EQ(ECONNREFUSED, ops->FailureErrno());
    GlobalMockObject::verify();
}

TEST_F(UrmaConnOpsTest, DoWaitTcpConnect_UbsConnection_StagesNegotiateReq)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOps(sock_ptr, conn_ops);
    ops->tcp_connect_started_ = true;

    MOCKER_CPP(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    auto tmp_context_pool = MakeRef<UrmaContextPool>();
    MOCKER_CPP(&UrmaBackend::GetContextPoll).stubs().will(returnValue(tmp_context_pool));

    EXPECT_EQ(ConnStageResult::kPending, ops->Execute(1000));
    EXPECT_EQ(ConnState::kWaitNegotiate, ops->State());
    EXPECT_TRUE(ops->WantsWritable());

    /* wire 帧：[4B body_len][UrmaNegotiateReq] */
    uint32_t body_len = 0;
    memcpy(&body_len, ops->pending_send_buf_.data(), sizeof(body_len));
    EXPECT_EQ(static_cast<uint32_t>(sizeof(UrmaNegotiateReq)), body_len);

    UrmaNegotiateReq req = {};
    memcpy(&req, ops->pending_send_buf_.data() + sizeof(body_len), sizeof(req));
    EXPECT_EQ(UBS_PROTOCOL_VERSION.GetWhole(), req.peer_version);
    EXPECT_EQ(UrmaSetting::UB_TANS_MODE, req.trans_mode);
    EXPECT_NE(0u, req.conn_flag & URMA_SOCKET_MASK_DEGRADE);
    GlobalMockObject::verify();
}

/* ==================== DoWaitNegotiate (Client) ==================== */

TEST_F(UrmaConnOpsTest, ClientDoWaitNegotiate_FlushEagain_ReturnsPending)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOpsInState(sock_ptr, conn_ops, ConnState::kWaitNegotiate);
    const uint8_t frame[8] = {0};
    ops->StageControl(frame, sizeof(frame));
    PushSendOk(1);
    PushSendWouldBlock();

    EXPECT_EQ(ConnStageResult::kPending, ops->Execute(1000));
    EXPECT_FALSE(ops->pending_send_buf_.empty());
    EXPECT_TRUE(ops->WantsWritable());
    EXPECT_EQ(ConnState::kWaitNegotiate, ops->State());
}

TEST_F(UrmaConnOpsTest, ClientDoWaitNegotiate_RspHalfFrame_ReturnsPending)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOpsInState(sock_ptr, conn_ops, ConnState::kWaitNegotiate);
    UrmaNegotiateRsp rsp = MakeCompatRsp();
    std::vector<uint8_t> wire = MakeLpFrame(&rsp, sizeof(rsp));
    PushSendOk();
    PushRecvData(wire.data(), 6); /* 前缀 + 2B body，body 半包 */

    EXPECT_EQ(ConnStageResult::kPending, ops->Execute(1000));
    EXPECT_EQ(ConnState::kWaitNegotiate, ops->State());
}

TEST_F(UrmaConnOpsTest, ClientDoWaitNegotiate_RspRetCodeNonZero_ReturnsFailed)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOpsInState(sock_ptr, conn_ops, ConnState::kWaitNegotiate);
    UrmaNegotiateRsp rsp = MakeCompatRsp(-1);
    std::vector<uint8_t> wire = MakeLpFrame(&rsp, sizeof(rsp));
    PushSendOk();
    PushRecvData(wire.data(), wire.size());

    EXPECT_EQ(ConnStageResult::kFailed, ops->Execute(1000));
    EXPECT_EQ(ECONNREFUSED, ops->FailureErrno());
}

TEST_F(UrmaConnOpsTest, ClientDoWaitNegotiate_VersionMajorMismatch_ReturnsFailed)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOpsInState(sock_ptr, conn_ops, ConnState::kWaitNegotiate);
    UrmaNegotiateRsp rsp = MakeCompatRsp();
    /* UBSVersion 布局: Patch(bit0-13) | Minor(bit14-25) | Major(bit26-31)。
       Major=1 (bit26) 与 local Major=0 不符；勿用 0x00020000 —— 其 bit17 落在 Minor 段、Major 仍为 0 会误判兼容 */
    rsp.negotiated_version = UBSVersion(1, 0, 0).GetWhole();
    std::vector<uint8_t> wire = MakeLpFrame(&rsp, sizeof(rsp));
    PushSendOk();
    PushRecvData(wire.data(), wire.size());

    EXPECT_EQ(ConnStageResult::kFailed, ops->Execute(1000));
    /* mismatch 置 degradable_ 后由 ClientOnConnFailed 走 DegradeToTcp 带内降级 */
    EXPECT_TRUE(ops->degradable_);
    EXPECT_EQ(ECONNREFUSED, ops->FailureErrno());
}

TEST_F(UrmaConnOpsTest, ClientDoWaitNegotiate_CreateResourcesFail_DegradableByConnFlag)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOpsInState(sock_ptr, conn_ops, ConnState::kWaitNegotiate);
    auto dev = MakeRef<UrmaDevice>("ut_dev", "", urma_device_attr_t{});
    ops->sorted_contexts_.push_back(MakeRef<UrmaContext>(nullptr, dev, urma_eid_info_t{}));
    UrmaNegotiateRsp rsp = MakeCompatRsp();
    rsp.conn_flag = URMA_SOCKET_MASK_DEGRADE;
    std::vector<uint8_t> wire = MakeLpFrame(&rsp, sizeof(rsp));
    PushSendOk();
    PushRecvData(wire.data(), wire.size());

    MOCKER_CPP(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(&UrmaSocket::CreateUrmaResources).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));

    EXPECT_EQ(ConnStageResult::kFailed, ops->Execute(1000));
    EXPECT_TRUE(ops->degradable_); /* rsp conn_flag 带 DEGRADABLE → 失败可降级 */
    GlobalMockObject::verify();
}

TEST_F(UrmaConnOpsTest, ClientDoWaitNegotiate_Happy_StagesLocalSegAndSwitchesState)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOpsInState(sock_ptr, conn_ops, ConnState::kWaitNegotiate);
    auto dev = MakeRef<UrmaDevice>("ut_dev", "", urma_device_attr_t{});
    ops->sorted_contexts_.push_back(MakeRef<UrmaContext>(nullptr, dev, urma_eid_info_t{}));
    UrmaNegotiateRsp rsp = MakeCompatRsp();
    std::vector<uint8_t> wire = MakeLpFrame(&rsp, sizeof(rsp));
    PushSendOk();
    PushRecvData(wire.data(), wire.size());

    MOCKER_CPP(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(&UrmaSocket::CreateUrmaResources).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(&UrmaSocket::BuildLocalSegExInfo).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));

    EXPECT_EQ(ConnStageResult::kPending, ops->Execute(1000));
    EXPECT_EQ(ConnState::kWaitPeerRes, ops->State());
    EXPECT_FALSE(ops->pending_send_buf_.empty()); /* local seg 已暂存待续发 */
    EXPECT_TRUE(ops->WantsWritable());

    /* 协商配置已应用到 socket */
    UrmaSocket *urma_sock = RefStaticCastToRaw<UrmaSocket>(sock_ptr);
    EXPECT_EQ(rsp.trans_mode, urma_sock->trans_mode);
    GlobalMockObject::verify();
}

/* ==================== DoWaitNegotiate (Server) ==================== */

TEST_F(UrmaConnOpsTest, ServerDoWaitNegotiate_ReqHalfFrame_ReturnsPending)
{
    auto ops = MakeServerOps();
    UrmaNegotiateReq req = MakeCompatReq();
    std::vector<uint8_t> wire = MakeLpFrame(&req, sizeof(req));
    PushRecvData(wire.data(), 4); /* 仅前缀 */

    EXPECT_EQ(ConnStageResult::kPending, ops->Execute(1000));
    EXPECT_EQ(ConnState::kWaitNegotiate, ops->State());
}

TEST_F(UrmaConnOpsTest, ServerDoWaitNegotiate_BodyLenZero_ReturnsFailed)
{
    auto ops = MakeServerOps();
    uint32_t zero_len = 0;
    PushRecvData(&zero_len, sizeof(zero_len));

    EXPECT_EQ(ConnStageResult::kFailed, ops->Execute(1000));
}

TEST_F(UrmaConnOpsTest, ServerDoWaitNegotiate_RecvError_ReturnsFailed)
{
    auto ops = MakeServerOps();
    PushRecvError(ECONNREFUSED);

    EXPECT_EQ(ConnStageResult::kFailed, ops->Execute(1000));
}

TEST_F(UrmaConnOpsTest, ServerDoWaitNegotiate_MajorMismatch_SendsRspAndDegradesToTcp)
{
    auto ops = MakeServerOps();
    UrmaNegotiateReq req = MakeCompatReq();
    req.peer_version = 0xFF010101; /* Major mismatch */
    std::vector<uint8_t> wire = MakeLpFrame(&req, sizeof(req));
    PushRecvData(wire.data(), wire.size());
    PushSendOk(); /* 一次发完 [len][rsp] */

    MOCKER_CPP(&UrmaConnOps::DegradeToTcp).stubs().will(returnValue(true));

    /* mismatch rsp 发完后带内降级：kDone 触发 reactor done_cb，brpc 以普通 TCP 接受该 fd */
    EXPECT_EQ(ConnStageResult::kDone, ops->Execute(1000));
    EXPECT_EQ(ConnState::kConnEstablished, ops->State());
    EXPECT_TRUE(ops->degraded_);

    /* mismatch rsp：ret_code=0，negotiated_version=server 自身版本（绝不发 0），selected_context_index=-1 */
    UrmaNegotiateRsp expect_rsp = {};
    expect_rsp.ret_code = 0;
    expect_rsp.negotiated_version = UBS_PROTOCOL_VERSION.GetWhole();
    expect_rsp.selected_context_index = -1;
    std::vector<uint8_t> expect_frame = MakeLpFrame(&expect_rsp, sizeof(expect_rsp));
    ASSERT_EQ(expect_frame.size(), g_sent_bytes.size());
    EXPECT_EQ(0, memcmp(expect_frame.data(), g_sent_bytes.data(), expect_frame.size()));
}

TEST_F(UrmaConnOpsTest, ServerDoWaitNegotiate_FlushRspEagain_ReturnsPending)
{
    auto ops = MakeServerOps();
    UrmaNegotiateReq req = MakeCompatReq();
    std::vector<uint8_t> wire = MakeLpFrame(&req, sizeof(req));
    PushRecvData(wire.data(), wire.size());
    PushSendOk(1);
    PushSendWouldBlock();
    auto tmp_context_pool = MakeRef<UrmaContextPool>();
    MOCKER_CPP(&UrmaBackend::GetContextPoll).stubs().will(returnValue(tmp_context_pool));

    EXPECT_EQ(ConnStageResult::kPending, ops->Execute(1000));
    EXPECT_EQ(ConnState::kWaitNegotiate, ops->State());
    EXPECT_FALSE(ops->pending_send_buf_.empty());
    EXPECT_TRUE(ops->WantsWritable());
}

TEST_F(UrmaConnOpsTest, ServerDoWaitNegotiate_Happy_CreatesSocketAndStagesLocalSeg)
{
    auto ops = MakeServerOps();
    UrmaNegotiateReq req = MakeCompatReq();
    std::vector<uint8_t> wire = MakeLpFrame(&req, sizeof(req));
    PushRecvData(wire.data(), wire.size());
    PushSendOk();

    MOCKER(eventfd).stubs().will(returnValue(TEST_EVENT_FD));
    MOCKER_CPP(&SocketBase::Create).stubs().will(invoke(MockCreateUrmaSocket));
    MOCKER_CPP(&SocketBase::GenerateSocketCommOps).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(&UrmaSocket::CreateUrmaResources).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(&UrmaSocket::BuildLocalSegExInfo).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    auto tmp_context_pool = MakeRef<UrmaContextPool>();
    auto dev = MakeRef<UrmaDevice>("ut_dev", "", urma_device_attr_t{});
    auto tmp_ctx = MakeRef<UrmaContext>(nullptr, dev, urma_eid_info_t{});
    tmp_ctx->SetAffinity(false);
    tmp_context_pool->sorted_contexts_.push_back(tmp_ctx);
    MOCKER_CPP(&UrmaBackend::GetContextPoll).stubs().will(returnValue(tmp_context_pool));

    EXPECT_EQ(ConnStageResult::kPending, ops->Execute(1000));
    EXPECT_EQ(ConnState::kWaitPeerRes, ops->State());
    ASSERT_NE(nullptr, ops->urma_sock_);
    EXPECT_NE(nullptr, ops->sock_.Get());
    EXPECT_EQ(TEST_EVENT_FD, ops->urma_sock_->event_fd_);
    EXPECT_EQ(SOCK_CREATE_TYPE_ACCEPT, ops->sock_->create_type_);
    EXPECT_FALSE(ops->pending_send_buf_.empty());
    /* server 侧协商结果来自 req/本端 */
    EXPECT_EQ(req.peer_version, ops->urma_sock_->GetPeerVersion());
    GlobalMockObject::verify();
}

/* ==================== DoWaitPeerRes ==================== */

TEST_F(UrmaConnOpsTest, DoWaitPeerRes_FlushLocalSegEagain_ReturnsPending)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOps(sock_ptr, conn_ops);
    ops->state_.store(ConnState::kWaitPeerRes, std::memory_order_release);
    ops->StageControl("abcd", 4);
    PushSendOk(1);
    PushSendWouldBlock();
    auto tmp_context_pool = MakeRef<UrmaContextPool>();
    MOCKER_CPP(&UrmaBackend::GetContextPoll).stubs().will(returnValue(tmp_context_pool));

    EXPECT_EQ(ConnStageResult::kPending, ops->Execute(1000));
    EXPECT_TRUE(ops->WantsWritable());
    EXPECT_EQ(ConnState::kWaitPeerRes, ops->State());
}

TEST_F(UrmaConnOpsTest, DoWaitPeerRes_PeerSegHalfFrame_ReturnsPending)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOps(sock_ptr, conn_ops);
    ops->state_.store(ConnState::kWaitPeerRes, std::memory_order_release);
    UrmaSockSegExInfo peer = {};
    PushSendOk();
    PushRecvData(&peer, sizeof(peer) / 2); /* 半包 */
    auto tmp_context_pool = MakeRef<UrmaContextPool>();
    MOCKER_CPP(&UrmaBackend::GetContextPoll).stubs().will(returnValue(tmp_context_pool));

    EXPECT_EQ(ConnStageResult::kPending, ops->Execute(1000));
    EXPECT_EQ(ConnState::kWaitPeerRes, ops->State());
}

TEST_F(UrmaConnOpsTest, DoWaitPeerRes_RecvError_ReturnsFailed)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOps(sock_ptr, conn_ops);
    ops->state_.store(ConnState::kWaitPeerRes, std::memory_order_release);
    PushSendOk();
    PushRecvError(ECONNRESET);
    auto tmp_context_pool = MakeRef<UrmaContextPool>();
    MOCKER_CPP(&UrmaBackend::GetContextPoll).stubs().will(returnValue(tmp_context_pool));

    EXPECT_EQ(ConnStageResult::kFailed, ops->Execute(1000));
    EXPECT_EQ(ECONNREFUSED, ops->FailureErrno());
}

TEST_F(UrmaConnOpsTest, DoWaitPeerRes_ImportPeerFails_ReturnsFailed)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOps(sock_ptr, conn_ops);
    ops->state_.store(ConnState::kWaitPeerRes, std::memory_order_release);
    ops->StageControl("seg", 3);
    UrmaSockSegExInfo peer = {};
    PushSendOk(3);
    PushRecvData(&peer, sizeof(peer));

    MOCKER_CPP(&UrmaSocket::ImportPeerSegments).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));
    auto tmp_context_pool = MakeRef<UrmaContextPool>();
    MOCKER_CPP(&UrmaBackend::GetContextPoll).stubs().will(returnValue(tmp_context_pool));

    EXPECT_EQ(ConnStageResult::kFailed, ops->Execute(1000));
    EXPECT_FALSE(ops->peer_imported_.load());
    GlobalMockObject::verify();
}

TEST_F(UrmaConnOpsTest, ServerDoWaitPeerRes_Happy_RegistersAndStagesAck)
{
    auto ops = MakeServerOps();
    ops->state_.store(ConnState::kWaitPeerRes, std::memory_order_release);
    ops->sock_ = RefConvert<UrmaSocket, Socket>(MakeTestUrmaSocket());
    ops->urma_sock_ = RefStaticCastToRaw<UrmaSocket>(ops->sock_);
    ops->StageControl("seg", 3);
    UrmaSockSegExInfo peer = {};
    PushSendOk(3);
    PushRecvData(&peer, sizeof(peer));

    MOCKER_CPP(&UrmaSocket::ImportPeerSegments).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    auto tmp_context_pool = MakeRef<UrmaContextPool>();
    MOCKER_CPP(&UrmaBackend::GetContextPoll).stubs().will(returnValue(tmp_context_pool));

    EXPECT_EQ(ConnStageResult::kPending, ops->Execute(1000));
    EXPECT_EQ(ConnState::kWaitPeerAck, ops->State());
    EXPECT_TRUE(ops->registered_.load()); /* poll 线程内联注册 ArraySet */
    EXPECT_TRUE(ops->peer_imported_.load());
    EXPECT_FALSE(ops->pending_send_buf_.empty()); /* ack 已暂存 */
    EXPECT_NE(nullptr, ArraySet<Socket>::GetInstance().GetItem(TEST_FD).Get());
    GlobalMockObject::verify();
}

/* ==================== DoWaitPeerAck ==================== */

TEST_F(UrmaConnOpsTest, DoWaitPeerAck_FlushAckEagain_ReturnsPending)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOps(sock_ptr, conn_ops);
    ops->state_.store(ConnState::kWaitPeerAck, std::memory_order_release);
    ops->StageControl("ack", 3);
    PushSendOk(1);
    PushSendWouldBlock();

    EXPECT_EQ(ConnStageResult::kPending, ops->Execute(1000));
    EXPECT_EQ(ConnState::kWaitPeerAck, ops->State());
}

TEST_F(UrmaConnOpsTest, DoWaitPeerAck_RecvError_ReturnsFailed)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOps(sock_ptr, conn_ops);
    ops->state_.store(ConnState::kWaitPeerAck, std::memory_order_release);
    PushSendOk();
    PushRecvError(ECONNRESET);

    EXPECT_EQ(ConnStageResult::kFailed, ops->Execute(1000));
}

TEST_F(UrmaConnOpsTest, ClientDoWaitPeerAck_Happy_CommitsAndReturnsDone)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOps(sock_ptr, conn_ops);
    ops->state_.store(ConnState::kWaitPeerAck, std::memory_order_release);
    ops->peer_seg_ex_.node.eid.in4.addr = 42;
    uint32_t peer_ack = 0;
    PushSendOk();
    PushRecvData(&peer_ack, sizeof(peer_ack));

    EXPECT_EQ(ConnStageResult::kDone, ops->Execute(1000));
    EXPECT_EQ(ConnState::kConnEstablished, ops->State());
    UrmaSocket *urma_sock = RefStaticCastToRaw<UrmaSocket>(sock_ptr);
    EXPECT_TRUE(urma_sock->IsBindRemote()); /* CommitRemotePeer 内 SetBindRemote(true) */
    EXPECT_EQ(42u, urma_sock->GetPeerNode().eid.in4.addr);
}

TEST_F(UrmaConnOpsTest, ServerDoWaitPeerAck_Happy_SetsEstablishedAndRestoresBlocking)
{
    g_fcntl_flags = 0;          /* 建链前 fd 为阻塞态 */
    auto ops = MakeServerOps(); /* was_blocking_ = true */
    /* EnsureSocketCreated 阶段会切非阻塞：模拟该效果，使 OnConnEstablished 的 SetBlocking 进入 SETFL 分支 */
    g_fcntl_flags = O_NONBLOCK;
    ops->state_.store(ConnState::kWaitPeerAck, std::memory_order_release);
    ops->sock_ = RefConvert<UrmaSocket, Socket>(MakeTestUrmaSocket());
    ops->urma_sock_ = RefStaticCastToRaw<UrmaSocket>(ops->sock_);
    uint32_t peer_ack = 0;
    PushSendOk();
    PushRecvData(&peer_ack, sizeof(peer_ack));

    EXPECT_EQ(ConnStageResult::kDone, ops->Execute(1000));
    EXPECT_EQ(ConnState::kConnEstablished, ops->State());
    EXPECT_EQ(SOCK_STAT_ESTABLISHED, ops->sock_->State());
    EXPECT_EQ(1, g_fcntl_setfl_calls); /* OnConnEstablished 恢复 blocking 触发 SETFL */
}

/* ==================== OnConnDone ==================== */

TEST_F(UrmaConnOpsTest, ClientOnConnDone_Degraded_OnlyMarksWritable)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOps(sock_ptr, conn_ops);
    ops->degraded_ = true;

    ops->OnConnDone();
    auto *sock_base = RefStaticCastToRaw<SocketBase>(sock_ptr);
    EXPECT_EQ(0, sock_base->connect_error_.load());
    EXPECT_TRUE(sock_base->writable_ready_.load());
    EXPECT_NE(SOCK_STAT_ESTABLISHED, sock_ptr->State()); /* 降级路径 State 已是 RAW_ESTABLISHED，不重复置位 */
}

TEST_F(UrmaConnOpsTest, ClientOnConnDone_Normal_SetsEstablishedAndConnectorInfo)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOps(sock_ptr, conn_ops);

    ops->OnConnDone();
    EXPECT_EQ(1u, conn_ops->conn_info.type_fd);
    EXPECT_EQ(SOCK_CREATE_TYPE_CONNECT, sock_ptr->create_type_);
    EXPECT_EQ(SOCK_STAT_ESTABLISHED, sock_ptr->State());
    auto *sock_base = RefStaticCastToRaw<SocketBase>(sock_ptr);
    EXPECT_TRUE(sock_base->writable_ready_.load());
    EXPECT_EQ(0, sock_base->connect_error_.load());
}

/* ==================== OnConnFailed ==================== */

TEST_F(UrmaConnOpsTest, ClientOnConnFailed_Degradable_DegradesToTcp)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOps(sock_ptr, conn_ops);
    ops->degradable_ = true;

    MOCKER_CPP(&UrmaConnOps::DegradeToTcp).stubs().will(returnValue(true));

    ops->OnConnFailed();
    auto *sock_base = RefStaticCastToRaw<SocketBase>(sock_ptr);
    EXPECT_EQ(0, sock_base->connect_error_.load());
    EXPECT_TRUE(sock_base->writable_ready_.load());
    GlobalMockObject::verify();
}

TEST_F(UrmaConnOpsTest, ClientOnConnFailed_NotDegradable_CleansAndNotifiesFailed)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOps(sock_ptr, conn_ops);
    ops->SetFailureErrno(ECONNREFUSED);

    MOCKER_CPP(&UrmaSocket::DestroyUrmaResources).stubs();

    ops->OnConnFailed();
    auto *sock_base = RefStaticCastToRaw<SocketBase>(sock_ptr);
    EXPECT_EQ(ECONNREFUSED, sock_base->connect_error_.load());
    EXPECT_EQ(SOCK_STAT_CLOSE, sock_ptr->State());
    GlobalMockObject::verify();
}

TEST_F(UrmaConnOpsTest, ClientOnConnFailed_NoFailureErrno_DefaultsEconnrefused)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOps(sock_ptr, conn_ops);

    MOCKER_CPP(&UrmaSocket::DestroyUrmaResources).stubs();

    ops->OnConnFailed();
    auto *sock_base = RefStaticCastToRaw<SocketBase>(sock_ptr);
    EXPECT_EQ(ECONNREFUSED, sock_base->connect_error_.load());
    EXPECT_EQ(SOCK_STAT_CLOSE, sock_ptr->State());
    GlobalMockObject::verify();
}

TEST_F(UrmaConnOpsTest, ServerOnConnFailed_UnregistersWithoutEvictingSharedSegments)
{
    auto ops = MakeServerOps();
    ops->registered_.store(true);
    ops->peer_imported_.store(true);
    ops->peer_seg_ex_.node.eid.in4.addr = 7;

    UrmaSocketPtr reg_sock = MakeTestUrmaSocket();
    reg_sock->context_ =
        MakeRef<UrmaContext>(nullptr, MakeRef<UrmaDevice>("ut_dev", "", urma_device_attr_t{}), urma_eid_info_t{});
    ops->sock_ = RefConvert<UrmaSocket, Socket>(reg_sock);
    ops->urma_sock_ = reg_sock.Get();
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_FD, reg_sock.Get());
    MOCKER_CPP(&UrmaIoBufferPool::rm_remote_segments).stubs().will(invoke(MockRmRemoteSegments));

    ops->OnConnFailed();
    EXPECT_EQ(nullptr, ArraySet<Socket>::GetInstance().GetItem(TEST_FD).Get());
    EXPECT_FALSE(ops->registered_.load());
    /* This callback unregisters the socket without evicting the shared segment cache. */
    EXPECT_TRUE(ops->peer_imported_.load());
    EXPECT_EQ(0, g_rm_remote_calls);

    ops->OnConnFailed();
    EXPECT_EQ(nullptr, ArraySet<Socket>::GetInstance().GetItem(TEST_FD).Get());
    EXPECT_FALSE(ops->registered_.load());
    EXPECT_TRUE(ops->peer_imported_.load());
    EXPECT_EQ(0, g_rm_remote_calls);
    GlobalMockObject::verify();
}

TEST_F(UrmaConnOpsTest, CleanStaleSockMarksMatchingAcceptedSocketBroken)
{
    auto ops = MakeServerOps();
    constexpr int staleFd = TEST_FD + 1;
    constexpr uint64_t staleConnId = 99;
    auto stale = MakeTestUrmaSocket(staleFd);
    stale->create_type_ = SOCK_CREATE_TYPE_ACCEPT;
    stale->SetConnId(staleConnId);
    stale->State(SOCK_STAT_ESTABLISHED);
    ArraySet<Socket>::GetInstance().OverrideItem(staleFd, stale.Get());
    LibcApi::shutdown_ptr = MockShutdownOk;
    MOCKER_CPP(&UrmaIoBufferPool::rm_remote_segments).expects(never());

    ops->CleanStaleSock(staleConnId);
    EXPECT_EQ(SOCK_STAT_BROKEN, stale->State());
    EXPECT_TRUE(stale->socket_readable_ready_.load());
    EXPECT_EQ(0, g_rm_remote_calls);
    GlobalMockObject::verify();
}

TEST_F(UrmaConnOpsTest, DegradeToTcpReplacesRegisteredSocket)
{
    SocketPtr sockPtr;
    ConnectorOpsPtr connectorOps;
    auto ops = MakeClientOps(sockPtr, connectorOps);
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_FD, sockPtr.Get());

    EXPECT_TRUE(ops->DegradeToTcp());
    auto replacement = ArraySet<Socket>::GetInstance().GetItem(TEST_FD);
    ASSERT_NE(nullptr, replacement.Get());
    EXPECT_EQ(SocketType::SOCK_TYPE_TCP, replacement->Type());
    EXPECT_EQ(SOCK_CREATE_TYPE_CONNECT, replacement->create_type_);
}

/* ==================== Abort ==================== */

TEST_F(UrmaConnOpsTest, Execute_Aborted_ReturnsFailedWithoutDispatch)
{
    SocketPtr sock_ptr;
    ConnectorOpsPtr conn_ops;
    auto ops = MakeClientOps(sock_ptr, conn_ops);
    ops->Abort();

    EXPECT_EQ(ConnStageResult::kFailed, ops->Execute(1000));
    EXPECT_EQ(ConnState::kWaitTcpConnect, ops->State());
}
