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

package com.huawei.ock.hcom.test.service.datatype;

import com.huawei.ock.hcom.service.NetDriverOptions;
import com.huawei.ock.hcom.service.datatype.CAInfo;

import org.junit.Assert;
import org.junit.Test;

/**
 * class CAInfo test
 *
 * @since 2024-02-17
 */
public class CAInfoTest {
    private String caPath = "yyyyyy";
    private String crlPath = "yyyyyy/ccccc";
    private NetDriverOptions.PeerCertVerifyType verifyType = NetDriverOptions.PeerCertVerifyType.VERIFY_BY_DEFAULT;

    @Test
    public void getUT() {
        CAInfo caInfo = new CAInfo(caPath, crlPath, verifyType);
        Assert.assertEquals(caPath, caInfo.getCaPath());
        Assert.assertEquals(crlPath, caInfo.getCrlPath());
        Assert.assertEquals(1, caInfo.getVerifyType());
    }

    @Test
    public void getCaPathUT() {
        CAInfo caInfo = new CAInfo(caPath);
        Assert.assertEquals(caPath, caInfo.getCaPath());
    }
}
