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

import com.huawei.ock.hcom.service.NetChannel;
import com.huawei.ock.hcom.service.NetServiceMessage;
import com.huawei.ock.hcom.service.NetServiceOpInfo;
import com.huawei.ock.hcom.service.NetChannelCallback;
import com.huawei.ock.hcom.service.NetServiceContext;

import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.powermock.api.mockito.PowerMockito;
import org.powermock.core.classloader.annotations.PrepareForTest;
import org.powermock.core.classloader.annotations.SuppressStaticInitializationFor;
import org.powermock.modules.junit4.PowerMockRunner;

import java.util.Arrays;

/**
 * class NetChannel test
 *
 * @since 2024-02-17
 */
@RunWith(PowerMockRunner.class)
@PrepareForTest(NetChannel.class)
@SuppressStaticInitializationFor("com.huawei.ock.hcom.service.NetChannel")
public class NetChannelTest {
    @Test
    public void sendTest() throws Exception {
        byte[] data = new byte[1024];
        Arrays.fill(data, (byte) 'h');
        NetServiceMessage message = new NetServiceMessage(data);
        byte[] data1 = new byte[10];
        Arrays.fill(data, (byte) 'c');
        message.data = data1;
        message.transferOwner = false;
        NetServiceOpInfo opInfo = new NetServiceOpInfo((short) 0);
        PowerMockito.stub(PowerMockito.method(NetChannel.class, "nativeSend")).toReturn(0);
        NetChannel channel = new NetChannel(123, 456, false);
        channel.send(opInfo, message, 1);
    }

    @Test(expected = Exception.class)
    public void sendTestException() throws Exception {
        byte[] data = new byte[1024];
        Arrays.fill(data, (byte) 'h');
        NetServiceMessage message = new NetServiceMessage(data);
        byte[] data1 = new byte[10];
        Arrays.fill(data, (byte) 'c');
        message.data = data1;
        message.transferOwner = false;
        NetServiceOpInfo opInfo = new NetServiceOpInfo((short) 0);
        PowerMockito.stub(PowerMockito.method(NetChannel.class, "nativeSend")).toReturn(1);
        NetChannel channel = new NetChannel(123, 456, false);
        channel.send(opInfo, message, 1);
    }

    class NetChannelCallbackTest implements NetChannelCallback {
        @Override
        public void run(NetServiceContext ctx) {

        }
    }

    @Test
    public void sendTestCb() throws Exception {
        byte[] data = new byte[1024];
        Arrays.fill(data, (byte) 'h');
        NetServiceMessage message = new NetServiceMessage(data);
        byte[] data1 = new byte[10];
        Arrays.fill(data, (byte) 'c');
        message.data = data1;
        message.transferOwner = false;
        NetServiceOpInfo opInfo = new NetServiceOpInfo((short) 0);
        PowerMockito.stub(PowerMockito.method(NetChannel.class, "nativeSendWithCb")).toReturn(0);
        NetChannel channel = new NetChannel(123, 456, false);
        NetChannelCallbackTest cbTest = new NetChannelCallbackTest();
        channel.send(opInfo, message, cbTest, 1);
    }

    @Test(expected = Exception.class)
    public void sendTestCbException() throws Exception {
        byte[] data = new byte[1024];
        Arrays.fill(data, (byte) 'h');
        NetServiceMessage message = new NetServiceMessage(data);
        byte[] data1 = new byte[10];
        Arrays.fill(data, (byte) 'c');
        message.data = data1;
        message.transferOwner = false;
        NetServiceOpInfo opInfo = new NetServiceOpInfo((short) 0);
        PowerMockito.stub(PowerMockito.method(NetChannel.class, "nativeSendWithCb")).toReturn(1);
        NetChannel channel = new NetChannel(123, 456, false);
        NetChannelCallbackTest cbTest = new NetChannelCallbackTest();
        channel.send(opInfo, message, cbTest, 1);
    }

    @Test(expected = Exception.class)
    public void sendExceptionTest() throws Exception {
        byte[] data = new byte[1024];
        Arrays.fill(data, (byte) 'h');
        NetServiceMessage message = new NetServiceMessage(data);
        byte[] data1 = new byte[10];
        Arrays.fill(data, (byte) 'c');
        message.data = data1;
        message.transferOwner = false;
        NetServiceOpInfo opInfo = new NetServiceOpInfo((short) 0);
        NetChannel channel = new NetChannel(123, 456, false);
        NetChannelCallbackTest cbTest = new NetChannelCallbackTest();
        channel.send(opInfo, message, cbTest);
    }

    @Test
    public void sendRawTest() throws Exception {
        byte[] data = new byte[1024];
        Arrays.fill(data, (byte) 'h');
        NetServiceMessage message = new NetServiceMessage(data);
        byte[] data1 = new byte[10];
        Arrays.fill(data, (byte) 'c');
        message.data = data1;
        message.transferOwner = false;
        PowerMockito.stub(PowerMockito.method(NetChannel.class, "nativeSendRaw")).toReturn(0);
        NetChannel channel = new NetChannel(123, 456, false);
        channel.sendRaw(message, 1);
    }

    @Test(expected = Exception.class)
    public void sendRawExceptionTest() throws Exception {
        byte[] data = new byte[1024];
        Arrays.fill(data, (byte) 'h');
        NetServiceMessage message = new NetServiceMessage(data);
        byte[] data1 = new byte[10];
        Arrays.fill(data, (byte) 'c');
        message.data = data1;
        message.transferOwner = false;
        PowerMockito.stub(PowerMockito.method(NetChannel.class, "nativeSendRaw")).toReturn(1);
        NetChannel channel = new NetChannel(123, 456, false);
        channel.sendRaw(message, 1);
    }

    @Test
    public void sendRawCbTest() throws Exception {
        byte[] data = new byte[1024];
        Arrays.fill(data, (byte) 'h');
        NetServiceMessage message = new NetServiceMessage(data);
        byte[] data1 = new byte[10];
        Arrays.fill(data, (byte) 'c');
        message.data = data1;
        message.transferOwner = false;
        NetChannelCallbackTest cbTest = new NetChannelCallbackTest();
        PowerMockito.stub(PowerMockito.method(NetChannel.class, "nativeSendRawWithCb")).toReturn(0);
        NetChannel channel = new NetChannel(123, 456, false);
        channel.sendRaw(message, cbTest, 1);
    }

    @Test(expected = Exception.class)
    public void sendRawCbExceptionTest() throws Exception {
        byte[] data = new byte[1024];
        Arrays.fill(data, (byte) 'h');
        NetServiceMessage message = new NetServiceMessage(data);
        byte[] data1 = new byte[10];
        Arrays.fill(data, (byte) 'c');
        message.data = data1;
        message.transferOwner = false;
        NetChannelCallbackTest cbTest = new NetChannelCallbackTest();
        PowerMockito.stub(PowerMockito.method(NetChannel.class, "nativeSendRawWithCb")).toReturn(1);
        NetChannel channel = new NetChannel(123, 456, false);
        channel.sendRaw(message, cbTest, 1);
    }

    @Test
    public void syncCallTest() throws Exception {
        byte[] data = new byte[1024];
        Arrays.fill(data, (byte) 'h');
        NetServiceMessage message = new NetServiceMessage(data, false);
        byte[] data1 = new byte[1024];
        Arrays.fill(data1, (byte) 'h');
        NetServiceMessage response = new NetServiceMessage(data1, false);
        NetServiceOpInfo opInfo = new NetServiceOpInfo((short) 0);
        NetServiceOpInfo respInfo = new NetServiceOpInfo((short) 0);
        PowerMockito.stub(PowerMockito.method(NetChannel.class, "nativeSyncCall")).toReturn(0);
        NetChannel channel = new NetChannel(123, 456, false);
        channel.syncCall(opInfo, message, respInfo, response);
    }

    @Test(expected = Exception.class)
    public void syncCallExceptionTest() throws Exception {
        byte[] data = new byte[1024];
        Arrays.fill(data, (byte) 'h');
        NetServiceMessage message = new NetServiceMessage(data, false);
        byte[] data1 = new byte[1024];
        Arrays.fill(data1, (byte) 'h');
        NetServiceMessage response = new NetServiceMessage(data1, false);
        NetServiceOpInfo opInfo = new NetServiceOpInfo((short) 0);
        NetServiceOpInfo respInfo = new NetServiceOpInfo((short) 0);
        PowerMockito.stub(PowerMockito.method(NetChannel.class, "nativeSyncCall")).toReturn(1);
        NetChannel channel = new NetChannel(123, 456, false);
        channel.syncCall(opInfo, message, respInfo, response);
    }

    @Test
    public void syncCallRawTest() throws Exception {
        byte[] data = new byte[1024];
        Arrays.fill(data, (byte) 'h');
        NetServiceMessage message = new NetServiceMessage(data, false);
        byte[] data1 = new byte[1024];
        Arrays.fill(data1, (byte) 'h');
        NetServiceMessage response = new NetServiceMessage(data1, false);
        PowerMockito.stub(PowerMockito.method(NetChannel.class, "nativeSyncCallRaw")).toReturn(0);
        NetChannel channel = new NetChannel(123, 456, false);
        channel.syncCallRaw(message, response);
    }

    @Test(expected = Exception.class)
    public void syncCallRawExceptionTest() throws Exception {
        byte[] data = new byte[1024];
        Arrays.fill(data, (byte) 'h');
        NetServiceMessage message = new NetServiceMessage(data, false);
        byte[] data1 = new byte[1024];
        Arrays.fill(data1, (byte) 'h');
        NetServiceMessage response = new NetServiceMessage(data1, false);
        PowerMockito.stub(PowerMockito.method(NetChannel.class, "nativeSyncCallRaw")).toReturn(1);
        NetChannel channel = new NetChannel(123, 456, false);
        channel.syncCallRaw(message, response);
    }

    @Test
    public void asyncCallTest() throws Exception {
        byte[] data = new byte[1024];
        Arrays.fill(data, (byte) 'h');
        NetServiceMessage message = new NetServiceMessage(data, false);
        NetServiceOpInfo opInfo = new NetServiceOpInfo((short) 0);
        NetChannelCallbackTest cbTest = new NetChannelCallbackTest();
        PowerMockito.stub(PowerMockito.method(NetChannel.class, "nativeAsyncCall")).toReturn(0);
        NetChannel channel = new NetChannel(123, 456, false);
        channel.asyncCall(opInfo, message, cbTest);
    }

    @Test(expected = Exception.class)
    public void asyncCallExceptionTest() throws Exception {
        byte[] data = new byte[1024];
        Arrays.fill(data, (byte) 'h');
        NetServiceMessage message = new NetServiceMessage(data, false);
        NetServiceOpInfo opInfo = new NetServiceOpInfo((short) 0);
        NetChannelCallbackTest cbTest = new NetChannelCallbackTest();
        PowerMockito.stub(PowerMockito.method(NetChannel.class, "nativeAsyncCall")).toReturn(1);
        NetChannel channel = new NetChannel(123, 456, false);
        channel.asyncCall(opInfo, message, cbTest);
    }

    @Test
    public void asyncCallRawTest() throws Exception {
        byte[] data = new byte[1024];
        Arrays.fill(data, (byte) 'h');
        NetServiceMessage message = new NetServiceMessage(data, false);
        NetChannelCallbackTest cbTest = new NetChannelCallbackTest();
        PowerMockito.stub(PowerMockito.method(NetChannel.class, "nativeAsyncCallRaw")).toReturn(0);
        NetChannel channel = new NetChannel(123, 456, false);
        channel.asyncCallRaw(message, cbTest);
    }

    @Test(expected = Exception.class)
    public void asyncCallRawExceptionTest() throws Exception {
        byte[] data = new byte[1024];
        Arrays.fill(data, (byte) 'h');
        NetServiceMessage message = new NetServiceMessage(data, false);
        NetChannelCallbackTest cbTest = new NetChannelCallbackTest();
        PowerMockito.stub(PowerMockito.method(NetChannel.class, "nativeAsyncCallRaw")).toReturn(1);
        NetChannel channel = new NetChannel(123, 456, false);
        channel.asyncCallRaw(message, cbTest);
    }

    @Test
    public void upCtxTest() {
        NetChannel channel = new NetChannel(123, 456, false);
        channel.setUpCtx("12345");
        Assert.assertEquals("12345", channel.getUpCtx());
    }

    @Test
    public void closeTest() {
        NetChannel channel = new NetChannel(123, 456, false);
        PowerMockito.suppress(PowerMockito.method(NetChannel.class, "decreaseObject"));
        channel.Close();
        Assert.assertNull(channel.getUpCtx());
    }
}
