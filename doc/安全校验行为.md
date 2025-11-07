# HCOM
HCOM is a high performance communication library for C/S applications.
* Easy to use, HCOM provides high level APIs instead of difficult low RDMA etc
* High performance, expose hardware capability as much as possible, and also optimization of domain oriented applications
* Various protocol supports, HCOM hidden the complex of low level API, RDMA/TCP/UDS/Shm/URMA etc

#### 1 how to clone
```
1 git clone repo
2 git submodule update --init --recursive
```

#### 2 how to build
```
cd ${TOP_DIR}/test
sh build.sh

cd ${TOP_DIR}
mkdir build
cd build

#if gcc version 4.8.5 
sh adapter_script.sh 

cmake -DCMAKE_DEPENDS_USE_COMPILER=false -DCMAKE_BUILD_TYPE=release ..
make -j8

# more flags 
# -DBUILD_TESTS=off for disable building test
# -DBUILD_EXAMPLE=off for disable building example
# -DBUILD_PERF=off for disable building perf
# -DBUILD_JAVA_SDK=off for disable building java code

ll

# you will see server and client binary under current dir, include libhcom.so and libhcom_static.a.
# note: if you want to link libhcom_static.a, make sure libsecurec.a exist in your project.

more cmake options:
1 CMAKE_BUILD_TYPE: release|debug, release binary or debug binary
  default: release

2 NN_LOG_TRACE_INFO_ENABLED: enable trace log
  default: disabled

3 USE_PROCESS_MONOTONIC: use CPU instruction for fast timestamp, need to change it in CMakelists.txt
  default: enabled

4 ENABLE_OBJ_GLOBAL_STATISTICS: enable object statistic, need to change it in CMakelists.txt
  default: enabled
  
5 CMAKE_INSTALL_PREFIX, CMAKE built-in options, to set make install target folder
  default: system

```


#### 3 how to execute UT cases
```
export HCOM_BUILD_TYPE=debug
export HCOM_BUILD_TESTS=on
./build.sh
./build/generate_gtest_report.sh
```

#### 4 how to run examples
```

```

#### 5 how to run perf
```

```

#### 6 secure info behavior table
单向校验行为表：

| 场景case | OOB   client 注册provider否 | OOB   client provider返回有效否 | OOB   Server 注册validator否 | OOB   Server validator返回无效否 | 内部行为发送header内容                                                                                                                                                                                                      | 用户可见行为                                                                                                                                      |
|:------:|--------------------------|----------------------------|---------------------------|-----------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------|
|   1    | Y                        | N                          | 任意组合（4种）                  | 　                           | 　                                                                                                                                                                                                                   | 1.OOB client终止connect, 返回错误NN_OOB_SEC_PROCESS_ERROR(141)      2.OOB server若有validator，则报错reset by peer, 返回错误码NN_OOB_CONN_RECEIVE_ERROR(126) |
|   2    | Y                        | Y                          | N                         | -（2种）                       | 1.OOB client发送header(flag, secInfoLen +SEC_VALID_ONE_WAY) + secInfo 2.OOB server接受header + secInfo，不进行校验，直接验证通过并返回respons(OK) 3.OOB client接受response(OK),认证通过                                                       | 1.OOB Server通过, 打印WARNING message                                                                                                           |
|   3    | Y                        | Y                          | Y                         | N                           | 1.OOB client发送header(flag, secInfoLen +SEC_VALID_ONE_WAY) + secInfo 2.OOB server接受header +   secInfo，进行校验，校验失败，直接验证通过并返回respons(SEC_VALID_ERROR)  (对面被reset by peer了) 3.OOB client接受response(SEC_VALID_ERROR), 认证失败 | 1.OOB Server失败，打印Error日志写明具体validator的错误值  2.OOB Client失败，打印Error日志,Response=SEC_VALID_FAILED（-9）                                            |
|   4    | Y                        | Y                          | Y                         | Y                           | 1.OOB client发送header(flag, secInfoLen +SEC_VALID_ONE_WAY) + secInfo 2.OOB server接受header + secInfo，校验通过，直接验证通过并返回respons(OK) 3.OOB client接受response(OK),认证通过                                                        | 1.OOB Server通过 2.OOB Client通过                                                                                                               |
|   5    | N                        | -（2种）                      | Y                         | Y                           | 1.OOB client发送header(0,0,NO_VALID) 2.OOB server收header，校验type失败，返回resp(SEC_VALID_FAILED) 3.OOB Client收的到resp(SEC_VALID_FAILED）                                                                                      | 1.OOB Server失败，打印Error 2.OOB Client失败，打印Error日志,Response=SEC_VALID_ERROR（-9）                                                                |
|   6    | N                        | -（2种）                      | Y                         | N                           | 同上                                                                                                                                                                                                                  | 同上                                                                                                                                          |
|   7    | N                        | -（2种）                      | N                         | -（2种）                       | 1.OOB client发送header(0,0,NO_VALID)      2.OOB server收header，校验type为NO_VAILD并且没有注册validator，校验通过                                                                                                                     | 1.OOB Server通过                                                                                                                              |

双向校验行为表：

| 场景case | OOB   Server 注册 provider否 | OOB   Server  provider返回有效否 | OOB   Client 注册validator否 | OOB   Client validator返回无效否 | 内部行为发送header内容                                                                                                                                                              | 用户可见行为                                                                                       |
|--------|---------------------------|-----------------------------|---------------------------|-----------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------|
| 8      | Y                         | N                           | N                         | - (2种）                      | 0.   前提条件，client已经注册了provider，否则参考表1（单项场景）      1.OOB client调用provider，获取secType = SEC_VALID_TWO_WAY,   校验没有validator直接打印并返回错误      2.OOB server, reset by peer             | OOB   client终止connect, 返回错误NN_OOB_SEC_PROCESS_ERROR(141)                                     |
| 9      | Y                         | N                           | Y                         | -（2种）                       | 1.OOB   server provider 返回无效，打印NN_OOB_SEC_PROCESS_ERROR，并返回错误      2.OOB client打印reset by peer，返回错误                                                                         | OOB   client打印返回错误NN_OOB_SEC_PROCESS_ERROR(141)                                              |
| 10     | Y                         | Y                           | N                         | -（2种）                       | 0.   前提条件，client已经注册了provider，否则参考表1（单项场景）      1. OOB client调用provider，获取secType = SEC_VALID_TWO_WAY,   校验没有validator直接打印并返回错误                                             | OOB   client终止connect, 返回错误NN_OOB_SEC_PROCESS_ERROR(141)                                     |
| 11     | Y                         | Y                           | Y                         | N                           | 1.OOB   server发送header(flag, secInfoLen +SEC_VALID_TWO_WAY+errCode(0)) +   secInfo      2.OOB client接受header，进行validate失败，打印错误并返回      3.OOB server收到reset by peer, 打印错误并返回 | 1.OOB   Server失败，打印reset by peer      2.OOB client打印返回错误NN_OOB_SEC_PROCESS_ERROR(141)        |
| 12     | Y                         | Y                           | Y                         | Y                           | 1.OOB   server发送header(flag, secInfoLen +SEC_VALID_TWO_WAY+errCode(0)) +   secInfo      2.OOB client接受header，进行validate成功，发送Response(OK)      3.OOB server收到response(OK), 校验成功 | 1.OOB   Server通过      2.OOB Client通过                                                         |
| 13     | N                         | -（8种）                       |                           |                             | 1.OOB   server 直接返回错误      2.OOB client 端收到reset by peer，打印错误并返回                                                                                                            | 1.OOB   Server失败，提示未注册provider, 打印Error      2.OOB client打印返回错误NN_OOB_SEC_PROCESS_ERROR(141) |