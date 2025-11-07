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

#ifndef HCOM_TEST_CAPI_SERVICE_H
#define HCOM_TEST_CAPI_SERVICE_H
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

class TestCapiService : public testing::Test {
public:
    TestCapiService();
    virtual void SetUp(void);
    virtual void TearDown(void);
};

#endif // HCOM_TEST_CAPI_SERVICE_H
