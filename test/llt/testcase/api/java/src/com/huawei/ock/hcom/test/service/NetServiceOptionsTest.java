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

import com.huawei.ock.hcom.service.NetServiceOptions;
import com.huawei.ock.hcom.service.NetOobUDSListenOptions;
import com.huawei.ock.hcom.service.NetOobListenOptions;

import org.junit.Assert;
import org.junit.Test;

import java.util.ArrayList;

/**
 * class ServiceOptions test
 *
 * @since 2024-02-17
 */
public class NetServiceOptionsTest {
    String ip = "12121.12312 ";
    int port = 9982;

    @Test(expected = Exception.class)
    public void validateTest_Fail1() throws Exception {
        NetServiceOptions options = new NetServiceOptions();
        NetOobUDSListenOptions optUds = new NetOobUDSListenOptions();
        optUds.name = "null";
        options.addUDSListenOptions(optUds);
        NetOobUDSListenOptions optUds1 = new NetOobUDSListenOptions();
        options.addUDSListenOptions(optUds1);
        options.validate();
    }

    @Test(expected = Exception.class)
    public void validateTest_Fail2() throws Exception {
        NetServiceOptions options = new NetServiceOptions();
        options.validate();
        NetOobListenOptions opt = new NetOobListenOptions(ip, 1);
        options.addListenOptions(opt);
        NetOobListenOptions opt1 = new NetOobListenOptions(ip, -1);
        options.addListenOptions(opt1);
        options.validate();
    }

    @Test
    public void AddListenOptionsTest() {
        NetServiceOptions options = new NetServiceOptions();
        NetOobListenOptions opt = new NetOobListenOptions(ip, port);
        options.addListenOptions(opt);
        ArrayList<NetOobListenOptions> list = options.getOobListenOptions();
        Assert.assertEquals(ip, list.get(0).getIp());
    }

    @Test
    public void AddUDSListenOptions() {
        NetServiceOptions options = new NetServiceOptions();
        NetOobUDSListenOptions opt = new NetOobUDSListenOptions();
        opt.perm = 10;
        options.addUDSListenOptions(opt);

        ArrayList<NetOobUDSListenOptions> list = options.getOobUDSListenOptions();
        Assert.assertEquals(10, list.get(0).perm);
    }
}