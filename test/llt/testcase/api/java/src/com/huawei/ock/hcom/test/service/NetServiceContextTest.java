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

import com.huawei.ock.hcom.service.NetServiceContext;
import com.huawei.ock.hcom.service.NetChannelCallback;
import com.huawei.ock.hcom.service.NetChannel;
import com.huawei.ock.hcom.service.NetServiceOpInfo;
import com.huawei.ock.hcom.service.NetServiceMessage;

import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.powermock.api.mockito.PowerMockito;
import org.powermock.core.classloader.annotations.PrepareForTest;
import org.powermock.core.classloader.annotations.SuppressStaticInitializationFor;
import org.powermock.modules.junit4.PowerMockRunner;

import java.util.Arrays;

/**
 * class NetServiceContext test
 *
 * @since 2024-02-17
 */
@RunWith(PowerMockRunner.class)
@PrepareForTest({NetServiceContext.class, NetChannel.class})
@SuppressStaticInitializationFor("com.huawei.ock.hcom.service.NetServiceContext")
public class NetServiceContextTest {
    @Test
    public void GetOpInfoTest() {
        byte[] data = new byte[1024];
        NetServiceContext ctx = new NetServiceContext(0L, 1L, true, (short) 100, (short) 1, (short) 0,
                (short) 10, 3, data, 0L);
        NetChannel channel = ctx.getChannel();
        Assert.assertNotNull(channel);
        NetServiceOpInfo opt = ctx.getOpInfo();
        Assert.assertEquals(10, opt.flags);
        Assert.assertEquals(100, opt.opCode);
    }

    @Test
    public void replySendTest_Except() throws Exception {
        byte[] data = new byte[1024];
        NetServiceContext ctx = new NetServiceContext(0L, 1L, true, (short) 100, (short) 1, (short) 0,
                (short) 10, 3, data, 0L);
        byte[] localData = new byte[100];
        Arrays.fill(localData, (byte) 'h');
        NetServiceMessage message = new NetServiceMessage(localData);
        NetServiceOpInfo opInfo1 = new NetServiceOpInfo((short) 0);
        PowerMockito.suppress(PowerMockito.method(NetChannel.class, "send", NetServiceOpInfo.class,
                NetServiceMessage.class, long.class));
        ctx.replySend(opInfo1, message);
    }

    class NetChannelCallbackTest implements NetChannelCallback {
        @Override
        public void run(NetServiceContext netServiceContext) {}
    }

    @Test
    public void replySendTest_Except2() throws Exception {
        byte[] data = new byte[1024];
        NetServiceContext ctx = new NetServiceContext(0L, 1L, true, (short) 100, (short) 1, (short) 0,
                (short) 10, 3, data, 0L);
        byte[] localData = new byte[100];
        Arrays.fill(localData, (byte) 'h');
        NetServiceMessage message = new NetServiceMessage(localData, true);
        NetServiceOpInfo opInfo1 = new NetServiceOpInfo((short) 0);
        NetChannelCallbackTest cb = new NetChannelCallbackTest();
        PowerMockito.suppress(PowerMockito.method(NetChannel.class, "send", NetServiceOpInfo.class,
                NetServiceMessage.class, NetChannelCallback.class, long.class));
        ctx.replySend(opInfo1, message, cb);

        ctx.invalidate();
        ctx.replySendRaw(message, cb);
    }

    @Test
    public void getNum() {
        byte[] data = new byte[1024];
        NetServiceContext ctx1 = new NetServiceContext(0L, 1L, true, (short) 100, (short) 1, (short) 0,
                (short) 10, 3, data, 0L);
        // getOpCode
        Assert.assertEquals(100, ctx1.getOpCode());

        // getResult
        Assert.assertEquals(3, ctx1.getResult());

        // getData
        Assert.assertEquals(data, ctx1.getData());

        // getDataLength
        Assert.assertEquals(1024, ctx1.getDataLength());

        // isTimeout
        Assert.assertEquals(false, ctx1.isTimeout());

        // getRspCtx
        Assert.assertEquals(0L, ctx1.getRspCtx());
    }

    @Test
    public void getOpCode() throws Exception {
        byte[] data = new byte[1024];
        NetServiceContext ctx1 = new NetServiceContext(0L, 1L, true, (short) 100, (short) 1, (short) 0,
                (short) 10, 3, data, 0L);
        PowerMockito.suppress(PowerMockito.field(NetServiceContext.class, "opInfo"));
        Assert.assertEquals(1024, ctx1.getOpCode());
    }
}
