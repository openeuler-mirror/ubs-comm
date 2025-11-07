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

import com.huawei.ock.hcom.service.NetOobListenOptions;

import org.junit.Assert;
import org.junit.Test;

/**
 * class NetOobListenOptions test
 *
 * @since 2024-02-17
 */
public class NetOobListenOptionsTest {
    @Test
    public void getIpAndPortTest() throws Exception {
        String ip = "999999.0000";
        int port = 198;
        NetOobListenOptions opt = new NetOobListenOptions(ip, port);
        opt.validate();

        Assert.assertEquals(ip, opt.getIp());
        Assert.assertEquals(port, opt.getPort());
    }

    @Test
    public void getTargetWorkerCountTest() {
        String ip1 = "999999.0000";
        int port1 = 198;
        int targetWorkerCount = 3;
        NetOobListenOptions opt1 = new NetOobListenOptions(ip1, port1, targetWorkerCount);
        Assert.assertEquals(targetWorkerCount, opt1.getTargetWorkerCount());
    }

    @Test(expected = Exception.class)
    public void validateTest_Exception() throws Exception {
        String ip = "999999.0000";
        int port = -1;
        NetOobListenOptions opt = new NetOobListenOptions(ip, port);
        opt.validate();
    }

    @Test(expected = Exception.class)
    public void validateTest_Exception2() throws Exception {
        int port = 59;
        NetOobListenOptions opt = new NetOobListenOptions("", port);
        opt.validate();
    }

    @Test
    public void setIpAndPortTest() {
        String ip = "999999.0000";
        int port = 198;
        NetOobListenOptions opt = new NetOobListenOptions(ip, port);
        opt.setPort(100);
        Assert.assertEquals(100, opt.getPort());

        opt.setIp("123.123");
        Assert.assertEquals("123.123", opt.getIp());
    }

    @Test
    public void setTargetWorkerCountTest() {
        String ip = "999999.0000";
        int port = 198;
        int targetWorkerCount = 3;
        NetOobListenOptions opt = new NetOobListenOptions(ip, port, targetWorkerCount);
        opt.setTargetWorkerCount(55);
        Assert.assertEquals(55, opt.getTargetWorkerCount());
    }
}
