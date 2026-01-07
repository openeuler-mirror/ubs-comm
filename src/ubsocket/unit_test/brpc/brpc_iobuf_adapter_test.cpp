#include "brpc_iobuf_adapter.h"

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

class BrpcIOBufAdapterTest : public testing::Test {
public:
    void SetUp() override
    {
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

TEST_F(BrpcIOBufAdapterTest, init)
{
}
