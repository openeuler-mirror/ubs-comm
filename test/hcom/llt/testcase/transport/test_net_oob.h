/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef HCOM_TEST_NET_OOB_H
#define HCOM_TEST_NET_OOB_H

#include <fstream>
#include <cstdio>

#include "gtest/gtest.h"
#include "mockcpp/mokc.h"
#include "transport/net_oob.h"

class TestNetOob : public testing ::Test {
public:
    TestNetOob() {};

    ~TestNetOob() {};

    void SetUp()
    {
        // Use a short absolute path to avoid AF_UNIX sun_path overflow in deep CI workspaces.
        testFile = "/tmp/hcom_llt_oob_" + std::to_string(::getpid()) + ".socket";

        setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
    };

    void TearDown()
    {
        GlobalMockObject::verify();
    };

protected:
    std::string testFile;
};

#endif // HCOM_TEST_NET_OOB_H
