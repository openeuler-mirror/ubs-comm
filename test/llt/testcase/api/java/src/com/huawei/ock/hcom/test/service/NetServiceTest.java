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

import com.huawei.ock.hcom.common.ExternLogger;
import com.huawei.ock.hcom.common.ExternLoggerListener;
import com.huawei.ock.hcom.service.NetService;
import com.huawei.ock.hcom.service.NetChannel;
import com.huawei.ock.hcom.service.NetDriverOptions;
import com.huawei.ock.hcom.service.NetProvideSecInfo;
import com.huawei.ock.hcom.service.NetServiceOptions;
import com.huawei.ock.hcom.service.NetServiceContext;
import com.huawei.ock.hcom.service.NetServiceConnectOptions;
import com.huawei.ock.hcom.service.NetServiceListener;
import com.huawei.ock.hcom.service.NetSecValidateListener;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.powermock.api.mockito.PowerMockito;
import org.powermock.core.classloader.annotations.PrepareForTest;
import org.powermock.core.classloader.annotations.SuppressStaticInitializationFor;
import org.powermock.modules.junit4.PowerMockRunner;

/**
 * class NetService test
 *
 * @since 2024-08-22
 */
@RunWith(PowerMockRunner.class)
@PrepareForTest({NetService.class, System.class, ExternLoggerListener.class, Thread.class, NetServiceTest.class})
@SuppressStaticInitializationFor("com.huawei.ock.hcom.service.NetService")
public class NetServiceTest {
    @Before
    public void setUp() {
        PowerMockito.suppress(PowerMockito.method(ExternLoggerListener.class, "run"));
    }

    @Test
    public void InstanceTest() throws Exception {
        PowerMockito.suppress(PowerMockito.method(System.class, "loadLibrary"));
        PowerMockito.stub(PowerMockito.method(NetService.class, "nativeInstance")).toReturn(1L);
        NetService serviceTest = NetService.Instance(NetDriverOptions.Protocol.TCP, "name", true);
        Assert.assertNotNull(serviceTest);
    }

    @Test(expected = Exception.class)
    public void InstanceExceptionTest() throws Exception {
        PowerMockito.suppress(PowerMockito.method(System.class, "loadLibrary"));
        PowerMockito.stub(PowerMockito.method(NetService.class, "nativeInstance")).toReturn(-1L);
        NetService serviceTest = NetService.Instance(NetDriverOptions.Protocol.TCP, "name", true);
        Assert.assertNull(serviceTest);
    }

    /**
     * 创建service实例
     *
     * @return NetService 返回创建的Service实例
     * @throws Exception if an error occurs when createService
     */
    public NetService createService() throws Exception {
        PowerMockito.suppress(PowerMockito.method(System.class, "loadLibrary"));
        PowerMockito.stub(PowerMockito.method(NetService.class, "nativeInstance")).toReturn(1L);
        return NetService.Instance(NetDriverOptions.Protocol.TCP, "testService", true);
    }

    class ExternLoggerTest implements ExternLogger {
        @Override
        public void log(int level, String message) {
        }
    }

    @Test
    public void addExternLoggerTest() throws Exception {
        ExternLoggerTest exLoggerTest = new ExternLoggerTest();
        PowerMockito.suppress(PowerMockito.method(ExternLoggerListener.class, "nativeAddExternLogListener"));
        PowerMockito.suppress(PowerMockito.method(Thread.class, "start"));
        NetService.addExternLogger(exLoggerTest, 1, 100);
    }

    class NetServiceListenerTest implements NetServiceListener {
        @Override
        public int onNewChannel(String ipPort, NetChannel channel, String payload) {
            return 0;
        }

        @Override
        public int onChannelBroken(NetChannel ch) {
            return 0;
        }

        @Override
        public int onNewRequest(NetServiceContext ctx) {
            return 0;
        }

        @Override
        public int onRequestSent(NetServiceContext ctx) {
            return 0;
        }

        @Override
        public int onOneSideDone(NetServiceContext ctx) {
            return 0;
        }

        @Override
        public int onIdle() {
            return 0;
        }
    }

    @Test(expected = Exception.class)
    public void addListenerCppAddressTest() throws Exception {
        NetService serviceTest = createService();
        NetServiceListenerTest listenerTest = new NetServiceListenerTest();
        PowerMockito.suppress(PowerMockito.field(NetService.class, "cppAddress"));
        serviceTest.addListener(listenerTest);
    }

    @Test(expected = Exception.class)
    public void addListenerNullTest() throws Exception {
        NetService serviceTest = createService();
        serviceTest.addListener(null);
    }

    @Test
    public void addListenerTest() throws Exception {
        NetService serviceTest = createService();
        NetServiceListenerTest listenerTest = new NetServiceListenerTest();
        PowerMockito.stub(PowerMockito.method(NetService.class, "nativeAddEventListener")).toReturn(0);
        serviceTest.addListener(listenerTest);
    }

    @Test(expected = Exception.class)
    public void addListenerExceptionTest() throws Exception {
        NetService serviceTest = createService();
        NetServiceListenerTest listenerTest = new NetServiceListenerTest();
        PowerMockito.stub(PowerMockito.method(NetService.class, "nativeAddEventListener")).toReturn(1);
        serviceTest.addListener(listenerTest);
    }

    class NetSecValidateListenerTest implements NetSecValidateListener {
        @Override
        public NetProvideSecInfo onProvideSecInfo(long ctx) {
            return null;
        }

        @Override
        public int onValidateSecInfo(long ctx, long flag, String input) {
            return 0;
        }
    }

    @Test(expected = Exception.class)
    public void addSecValidateListenerCppAddressTest() throws Exception {
        NetService serviceTest = createService();
        NetSecValidateListenerTest listenerTest = new NetSecValidateListenerTest();
        PowerMockito.suppress(PowerMockito.field(NetService.class, "cppAddress"));
        serviceTest.addSecValidateListener(listenerTest);
    }

    @Test(expected = Exception.class)
    public void addSecValidateListenerNullTest() throws Exception {
        NetService serviceTest = createService();
        serviceTest.addSecValidateListener(null);
    }

    @Test
    public void addSecValidateListenerTest() throws Exception {
        NetService serviceTest = createService();
        NetSecValidateListenerTest listenerTest = new NetSecValidateListenerTest();
        PowerMockito.stub(PowerMockito.method(NetService.class, "nativeAddSecValidateListener")).toReturn(0);
        serviceTest.addSecValidateListener(listenerTest);
    }

    @Test(expected = Exception.class)
    public void addSecValidateListenerExceptionTest() throws Exception {
        NetService serviceTest = createService();
        NetSecValidateListenerTest listenerTest = new NetSecValidateListenerTest();
        PowerMockito.stub(PowerMockito.method(NetService.class, "nativeAddSecValidateListener")).toReturn(1);
        serviceTest.addSecValidateListener(listenerTest);
    }

    @Test(expected = Exception.class)
    public void startCppAddressTest() throws Exception {
        NetService serviceTest = createService();
        NetServiceOptions options = new NetServiceOptions();
        PowerMockito.suppress(PowerMockito.field(NetService.class, "cppAddress"));
        serviceTest.start(options);
    }

    @Test(expected = Exception.class)
    public void startNullTest() throws Exception {
        NetService serviceTest = createService();
        serviceTest.start(null);
    }

    @Test
    public void startTest() throws Exception {
        NetService serviceTest = createService();
        NetServiceOptions options = new NetServiceOptions();
        PowerMockito.stub(PowerMockito.method(NetService.class, "nativeStart")).toReturn(0);
        serviceTest.start(options);
    }

    @Test(expected = Exception.class)
    public void startExceptionTest() throws Exception {
        NetService serviceTest = createService();
        NetServiceOptions options = new NetServiceOptions();
        PowerMockito.stub(PowerMockito.method(NetService.class, "nativeStart")).toReturn(1);
        serviceTest.start(options);
    }

    @Test
    public void stopNotStartTest() throws Exception {
        NetService serviceTest = createService();
        serviceTest.stop();
    }

    @Test
    public void stopTest() throws Exception {
        NetService serviceTest = createService();
        NetServiceOptions options = new NetServiceOptions();
        PowerMockito.stub(PowerMockito.method(NetService.class, "nativeStart")).toReturn(0);
        serviceTest.start(options);
        PowerMockito.stub(PowerMockito.method(NetService.class, "nativeStop")).toReturn(0);
        serviceTest.stop();
    }

    @Test(expected = Exception.class)
    public void connectNameNullTest() throws Exception {
        NetService serviceTest = createService();
        String oobIpOrName = null;
        int oobPort = 100;
        String payload = "testService";
        NetServiceConnectOptions options = new NetServiceConnectOptions();
        serviceTest.connect(oobIpOrName, oobPort, payload, options);
    }

    @Test
    public void connectTest() throws Exception {
        NetService serviceTest = createService();
        String oobIpOrName = "null";
        int oobPort = 100;
        String payload = "testService";
        NetServiceConnectOptions options = new NetServiceConnectOptions();
        PowerMockito.stub(PowerMockito.method(NetService.class, "nativeConnect")).toReturn(0L);
        PowerMockito.stub(PowerMockito.method(NetService.class, "nativeGetChannelProperty")).toReturn("1234456##1");
        NetChannel channel = serviceTest.connect(oobIpOrName, oobPort, payload, options);
        Assert.assertNotNull(channel);
    }

    @Test(expected = Exception.class)
    public void connectNativeConnectTest() throws Exception {
        NetService serviceTest = createService();
        String oobIpOrName = "oobIpOrName";
        int oobPort = 100;
        String payload = "testService";
        NetServiceConnectOptions options = new NetServiceConnectOptions();
        PowerMockito.stub(PowerMockito.method(NetService.class, "nativeConnect")).toReturn(-1L);
        serviceTest.connect(oobIpOrName, oobPort, payload, options);
    }

    @Test(expected = Exception.class)
    public void connectNativeGetChannelPropertyTest() throws Exception {
        NetDriverOptions opt = new NetDriverOptions();
        NetService serviceTest = createService();
        String oobIpOrName = "oobIpOrName";
        int oobPort = 100;
        String payload = "testService";
        NetServiceConnectOptions options = new NetServiceConnectOptions();
        PowerMockito.stub(PowerMockito.method(NetService.class, "nativeConnect")).toReturn(0L);
        PowerMockito.stub(PowerMockito.method(NetService.class, "nativeGetChannelProperty")).toReturn(null);
        PowerMockito.suppress(PowerMockito.method(NetService.class, "nativeDestroyChannel"));
        serviceTest.connect(oobIpOrName, oobPort, payload, options);
    }
}
