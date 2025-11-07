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

import com.huawei.ock.hcom.service.NetProvideSecInfo;
import com.huawei.ock.hcom.service.NetServiceOptions;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.powermock.modules.junit4.PowerMockRunner;

/**
 * class NetProvideSecInfo test
 *
 * @since 2024-08-22
 */
@RunWith(PowerMockRunner.class)
public class NetProvideSecInfoTest {
    @Test
    public void sendTest() {
        NetProvideSecInfo info = new NetProvideSecInfo();
        info.type = NetServiceOptions.SecInfoValidateType.SEC_VALIDATE_DISABLED;
        info.validate();
    }
}
