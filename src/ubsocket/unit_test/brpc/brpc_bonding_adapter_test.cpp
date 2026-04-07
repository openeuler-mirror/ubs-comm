#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "brpc_context.h"

// Named constants to avoid magic numbers (G.CNS.02)
namespace {
static const int SETENV_OVERWRITE = 1;
} // namespace

class BrpcBondingAdapterTest : public testing::Test {
public:
    void SetUp() override
    {
        setenv("UBSOCKET_USE_BRPC_ZCOPY", "false", 1);
        setenv("UBSOCKET_TRANS_MODE", "ubmm", 1);
        RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
        MOCKER_CPP(umq_init)
            .stubs()
            .will(returnValue(int(0)));
    }

    void TearDown() override
    {
        unsetenv("UBSOCKET_USE_BRPC_ZCOPY");
        unsetenv("UBSOCKET_TRANS_MODE");
    }
};

TEST_F(BrpcBondingAdapterTest, TestCreateContext)
{
    Brpc::Context *context = new Brpc::Context();

    EXPECT_EQ(context->GetDevNameStr(), nullptr);
    EXPECT_EQ(context->GetDevIpStr(), nullptr);
    EXPECT_EQ(context->IsBonding(), false);
    EXPECT_EQ(context->m_socket_fd_trans_mode, ConfigSettings::socket_fd_trans_mode::SOCKET_FD_TRANS_MODE_TCP);

    delete context;
    context = nullptr;
    GlobalMockObject::verify();
}