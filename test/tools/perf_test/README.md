## 1 简介

`hcom_perf`是一个性能测试工具，类似 [Infiniband Verbs Performance Tests](https://github.com/linux-rdma/perftest/tree/master)。

## 2 编译

```shell
# 下载hcom源码（包含hcom_perf工具）
$ git clone <hcom-repo-url> --recurse-submodules

# 编译hcom（hcom_perf编译依赖libhcomstatic.a）
$ cd hcom
$ bash build.sh

# 编译hcom_perf，注意测试性能要求hcom_perf和hcom都用release版本
$ cd test/tools/perf_test
$ mkdir build && cd build

# 头文件默认使用是/output/hcom/include, 如果需要更改, 可通过环境变量HCOM_INCLUDE_DIR设置。
# 库文件默认使用是/output/hcom/lib, 如果需要更改, 可通过环境变量HCOM_LIB_DIR设置。
# 例如, cmake -DHCOM_INCLUDE_DIR=/usr/include/hcom -DHCOM_LIB_DIR=/usr/local/lib/hcom ..
$ cmake -DCMAKE_BUILD_TYPE=release ..
$ make -j8
```

## 3 使用

```shell
#rdma
# server端（假设server端IP为10.10.1.63）
./hcom_perf --case transport_send_lat --role server --protocol rdma -i 10.10.1.63 -n 1000 --all

# client端
./hcom_perf --case transport_send_lat --role client --protocol rdma -i 10.10.1.63 -n 1000 --all

#service UBC
# server端（假设server端IP为10.10.1.63）
./hcom_perf --case service_send_lat --role server --protocol UBC -i 10.10.1.63 -n 1000 --all

# client端
./hcom_perf --case service_send_lat --role client --protocol UBC -i 10.10.1.63 -n 1000 --all

#server 端输入 “q” 退出

# 如果有需要，可以执行如下命令，调整HCOM打印级别
export HCOM_SET_LOG_LEVEL=3
```



> 注意事项：
>
> 1. 性能测试时，`hcom_perf`工具和`hcom`都要使用release版本。
> 2. 默认不绑核，绑定网卡所在的`numa`中的某个核效率最高。
> 3. 参考`ibv`或者`urma`的`perf`场景，本工具也仅支持单线程。
> 4. 调整`hcom`日志打印级别（`export HCOM_SET_LOG_LEVEL=3`），可以精简打印。
> 5. 通过环境变量`HCOM_PERF_TEST_LOG_LEVEL`可以调整`hcom_perf`的打印级别，`0/1/2/3`依次对应`debug/info/warning/error`日志级别。



##  4 开发指南

> 注意：
>
> ​    本章节仅针对`hcom_perf`开发人员。

开发过程中，要注意如下事项：

1. 关键路径（发送、接收）逻辑要尽可能简单和高效。
2. 传输层或者服务层创建、初始化及启动`hcom`实例有较多公共逻辑，建议将公共代码放到*_helper辅助类中，避免重复代码。



遗留如下问题待处理：

1. 带宽的统计方式及打印，需要参考`ibv`或`urma`做实现和验证。
2. 仅实现和验证了“传输层-RDMA协议-双边发送时延”，其它场景需要进一步开发和验证。
3. 后续可考虑移除role参数，内部通过其它方式判断client或者server。如完成初始建链后，client可以向sever发送一条消息（包大小、测试次数等），以实现简化server的参数输入。
4. 使用`ibv`测试工具时，client和server均会打印结果。`hcom—perf`当前仅client端打印的耗时情况，如果需要server端也统计和打印结果，每次测试的开始和结束，需要client和server额外交互控制信息。



## 参考资料

1.  [Infiniband Verbs Performance Tests](https://github.com/linux-rdma/perftest/tree/master)
2. [Infiniband Verbs Performance Test 时延统计](https://github.com/linux-rdma/perftest/blob/master/src/perftest_parameters.c#L4118)
3.  [URMA Performance Tests](https://codehub-y.huawei.com/nStack/nStack/Cloud/UBus/urma/files?ref=master&filePath=code%2Ftools%2Furma_perftest)
4. [URMA perftest运行与开发](https://wiki.huawei.com/domains/13898/wiki/30209/WIKI202412045316527)

