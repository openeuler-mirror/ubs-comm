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

import com.huawei.ock.hcom.service.datatype.PKey;

import org.junit.Assert;
import org.junit.Test;

import java.util.Arrays;

/**
 * class PKey test
 *
 * @since 2024-02-17
 */
public class PKeyTest {
    private String path = "yyyyyy";
    byte[] data = new byte[10];

    @Test
    public void getUT() {
        PKey pKey = new PKey(path, data);
        Assert.assertEquals(path, pKey.getPath());
        Assert.assertEquals(data, pKey.getKeypass());
        Arrays.fill(data, (byte) 0);
        pKey.Clean();
        Assert.assertEquals(data, pKey.getKeypass());
    }
}
