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

package com.huawei.ock.hcom.test.service;

import com.huawei.ock.hcom.service.NetServiceOpInfo;

import org.junit.Assert;
import org.junit.Test;

/**
 * class NetServiceOpInfo test
 *
 * @since 2024-08-22
 */
public class NetServiceOpInfoTest {
    @Test
    public void createNetServiceOpInfoTest() {
        short opCode = 100;
        short errorCode = 20;
        NetServiceOpInfo opInfo = new NetServiceOpInfo(opCode, errorCode);
        Assert.assertEquals(opInfo.errorCode, errorCode);
        Assert.assertEquals(opInfo.opCode, opCode);

        short opCode1 = 111;
        short errorCode1 = 5;
        short timeOut = 10;
        NetServiceOpInfo opInfo1 = new NetServiceOpInfo(opCode1, errorCode1, timeOut);
        Assert.assertEquals(opInfo1.errorCode, errorCode1);
        Assert.assertEquals(opInfo1.opCode, opCode1);
        Assert.assertEquals(opInfo1.timeout, timeOut);
    }
}
