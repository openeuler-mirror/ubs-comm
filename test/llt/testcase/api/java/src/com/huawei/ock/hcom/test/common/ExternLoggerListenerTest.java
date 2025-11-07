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

package com.huawei.ock.hcom.test.common;

import com.huawei.ock.hcom.common.ExternLogger;
import com.huawei.ock.hcom.common.ExternLoggerListener;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.powermock.api.mockito.PowerMockito;
import org.powermock.core.classloader.annotations.PrepareForTest;
import org.powermock.core.classloader.annotations.SuppressStaticInitializationFor;
import org.powermock.modules.junit4.PowerMockRunner;

/**
 * class ExternLoggerListener test
 *
 * @since 2024-08-22
 */
@RunWith(PowerMockRunner.class)
@PrepareForTest({ExternLoggerListener.class, Long.class})
@SuppressStaticInitializationFor("com.huawei.ock.hcom.common.ExternLoggerListener")
public class ExternLoggerListenerTest {
    class ExternLoggerTest implements ExternLogger {
        @Override
        public void log(int level, String message) {

        }
    }

    @Test
    public void exitRunTest() {
        ExternLoggerTest loggerTest = new ExternLoggerTest();
        PowerMockito.suppress(PowerMockito.method(ExternLoggerListener.class, "nativeAddExternLogListener"));
        ExternLoggerListener listener = new ExternLoggerListener(loggerTest, 1, 100);
        PowerMockito.suppress(PowerMockito.method(ExternLoggerListener.class, "nativeInterrupt"));
        listener.exit();
        PowerMockito.stub(PowerMockito.method(ExternLoggerListener.class, "nativeStop")).toReturn(2);
        PowerMockito.stub(PowerMockito.method(ExternLoggerListener.class, "nativePollLogMsg")).toReturn("test log");
        listener.run();
    }

    @Test
    public void logTest() {
        ExternLoggerTest loggerTest = new ExternLoggerTest();
        PowerMockito.suppress(PowerMockito.method(ExternLoggerListener.class, "nativeAddExternLogListener"));
        ExternLoggerListener listener = new ExternLoggerListener(loggerTest, 1, 100);
        PowerMockito.suppress(PowerMockito.method(ExternLoggerListener.class, "nativeInterrupt"));
        listener.exit();
        PowerMockito.stub(PowerMockito.method(ExternLoggerListener.class, "nativeStop")).toReturn(2);
        PowerMockito.stub(PowerMockito.method(ExternLoggerListener.class, "nativePollLogMsg")).toReturn("test log");
        PowerMockito.stub(PowerMockito.method(Long.class, "intValue")).toReturn(1);
        listener.run();
    }
}
