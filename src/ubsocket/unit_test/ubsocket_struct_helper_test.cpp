/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You may obtain a copy of the Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 */

#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "ubsocket_struct_helper.h"

using namespace ock::ubs;

TEST(UbsocketStructHelperTest, FormatsAllInitializationOptions)
{
    u_external_lock_ops_t lockOps{};
    u_external_rw_lock_ops_t rwLockOps{};
    u_external_semaphore_ops_t semOps{};
    u_external_rpc_id_ops_t rpcIdOps{};
    u_external_poller_ops_t pollerOps{};
    u_init_options_t options{};
    options.allowed_protocol = 0x12U;
    options.async_acceptor_thread_count = 2U;
    options.async_connector_thread_count = 3U;
    options.async_epoll_thread_count = 4U;
    options.lock_ops = &lockOps;
    options.rw_lock_ops = &rwLockOps;
    options.sem_ops = &semOps;
    options.rpc_id_ops = &rpcIdOps;
    options.poller_ops = &pollerOps;

    std::ostringstream stream;
    std::ostream &result = stream << options;
    const std::string text = stream.str();

    EXPECT_EQ(&result, &stream);
    EXPECT_NE(text.find("u_init_options_t [allowed_protocol: 18"), std::string::npos);
    EXPECT_NE(text.find("async_acceptor_thread_count: 2"), std::string::npos);
    EXPECT_NE(text.find("async_connector_thread_count: 3"), std::string::npos);
    EXPECT_NE(text.find("async_epoll_thread_count: 4"), std::string::npos);
    EXPECT_NE(text.find("lock_ops: "), std::string::npos);
    EXPECT_NE(text.find("rw_lock_ops: "), std::string::npos);
    EXPECT_NE(text.find("sem_ops: "), std::string::npos);
    EXPECT_NE(text.find("rpc_id_ops: "), std::string::npos);
    EXPECT_NE(text.find("poller_ops: "), std::string::npos);
}

TEST(UbsocketStructHelperTest, FormatsNullOptionalPointers)
{
    u_init_options_t options{};
    std::ostringstream stream;

    stream << options;

    const std::string text = stream.str();
    EXPECT_NE(text.find("allowed_protocol: 0"), std::string::npos);
    EXPECT_NE(text.find("async_acceptor_thread_count: 0"), std::string::npos);
    EXPECT_NE(text.find("async_connector_thread_count: 0"), std::string::npos);
    EXPECT_NE(text.find("async_epoll_thread_count: 0"), std::string::npos);
    EXPECT_NE(text.find("lock_ops: 0"), std::string::npos);
    EXPECT_NE(text.find("rw_lock_ops: 0"), std::string::npos);
    EXPECT_NE(text.find("sem_ops: 0"), std::string::npos);
    EXPECT_NE(text.find("rpc_id_ops: 0"), std::string::npos);
    EXPECT_NE(text.find("poller_ops: 0"), std::string::npos);
}
