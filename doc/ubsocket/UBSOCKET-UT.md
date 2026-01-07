# UT 目录结构

当前单元测试位于 `UBSocket` 下的 unit_test 的目录中，遵循着

```text
.
├── CMakeLists.txt
├── brpc
│   ├── CMakeLists.txt
│   └── brpc_iobuf_adapter_test.cpp
└── urpc_util_test.cpp
```

这样的目录结构，其中一级目录下的 urpc_util_test.cpp 说明它是 ubsocket 中 urpc_util.c 的单元测试。brpc
目录下的则是针对 ubsocket-brpc_adapter 单独的单元测试目录。

# 添加单元测试

如果想为 ubsocket 通用功能写测试用例，那么需要在 unit_test/ 目录下创建一个 foo_test.cpp 文件，一般它的开头处类似这样:

```c++
#include "foo.h" # 对应功能的头文件

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

class FooTest : public testing::Test {
public:
    void SetUp() override
    {
    }

    void TearDown() override
    {
        GlobalMockObject::verify(); // 在每次 testcase 结束后，将 mockcpp 修改的代码段复原
    }
}

TEST_F(FooTest, CaseName) // 测试功能的名字
{
    // 参考 https://google.github.io/googletest/primer.html
    EXPECT_TRUE(true);
}
```

> 说明: 单元测试主要使用 gtest 框架，mock 功能源自 mockcpp. 因为 mockcpp 可以直接 mock glibc 提供的
> syscall > wrapper，如 read、write 等，而 gtest 附带的 gmock 则需要用户提供一个接口。

在编写完单元测试之后，在 unit_test/CMakeLists.txt 中的 target_source 项将 foo_test.cpp 添加。最终效果如

```diff
 target_source(ubsocket_test
   PRIVATE
     urpc_util_test.cpp
+    foo_test.cpp
 )
```


# 编译、运行

```bash
cd ubs-comm/src/ubsocket

# 编译 ubsocket 与单元测试，注意 -DUBSOCKET_BUILD_TEST=ON 和 -DUMQ_BUF_LIB=...
# 如果想观察下载进度 -DFETCHCONTENT_QUIET=OFF
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Debug -DUBSOCKET_BUILD_TESTS=ON \
  -DUMQ_INCLUDE=/path/to/umq -DUMQ_LIB=/path/to/libumq -DUMQ_BUF_LIB=/path/to/libumq_buf \
  -DOPENSSL_ROOT_DIR=/path/to/openssl
cmake --build build -j32

# 运行单元测试
ctest --test-dir build --output-on-failure

# 如果只想运行 brpc adapter 相关的单元测试
ctest --test-dir build --output-on-failure -R brpc
```

默认情况下它会下载、编译 gtest/mockcpp.

> 如果想使用系统自带的 gtest，可以在编译时指定 -DUBSOCKET_USE_SYSTEM_GTEST=ON
> 不过系统自带的版本可能与 mockcpp 不兼容，会在 gtest 中出现 delete self 时的 segfault.

另外，

**特别需要注意**: 一定要确保 `-DUMQ_BUF_LIB=...`
文件存在、有效，否则最终链接时可能会出现符号找不到的问题。如果不显示指定，它的默认值为 `/usr/lib64/libumq_buf.so`.

```text
[ 99%] Linking CXX executable ../../brpc_adapter_test
[100%] Linking CXX executable ../ubsocket_test
/usr/bin/ld: /home/chenzhiwei/ubsocket/src/hcom/umq/build/src/libumq.so.0.0.1: undefined reference to `umq_huge_qbuf_headroom_reset'
/usr/bin/ld: /home/chenzhiwei/ubsocket/src/hcom/umq/build/src/libumq.so.0.0.1: undefined reference to `umq_huge_qbuf_get_type_by_size'
collect2: error: ld returned 1 exit status
make[2]: *** [unit_test/brpc/CMakeFiles/brpc_adapter_test.dir/build.make:105: brpc_adapter_test] Error 1
make[1]: *** [CMakeFiles/Makefile2:1289: unit_test/brpc/CMakeFiles/brpc_adapter_test.dir/all] Error 2
make[1]: *** Waiting for unfinished jobs....
/usr/bin/ld: /home/chenzhiwei/ubsocket/src/hcom/umq/build/src/libumq.so.0.0.1: undefined reference to `umq_huge_qbuf_headroom_reset'
/usr/bin/ld: /home/chenzhiwei/ubsocket/src/hcom/umq/build/src/libumq.so.0.0.1: undefined reference to `umq_huge_qbuf_get_type_by_size'
collect2: error: ld returned 1 exit status
```

因为最终是要链接成一个可执行二进制，而 ubsocket 中有使用 `umq` 相关 API，必须要找到所有符号的定义。在编译
librpc_brpc_adapter.so 时只链接了 libumq.so, 而上面这两个符号均出自 libumq_buf.so，所以需要额外提供
`-DUMQ_BUF_LIB=/path/to/libumq_buf` 项。

之所以在编译 librpc_brpc_adapter.so 时仅有 libumq.so 依赖却未出现链接错误，是因为动态链接库的符号决议是在运行时做的.
librpc_brpc_adapter.so 依赖 libumq.so，libumq.so 又依赖 libumq_buf.so, 而在运行时可以通过
`export LD_LIBRARY_PATH=` 能够让 libumq.so 找到 libumq_buf.so，又或者系统路径下存在 libumq_buf.so，如
/usr/lib64/libumq_buf.so 文件存在。
