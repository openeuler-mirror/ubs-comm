add_executable(brpc_adapter_test "")
add_test(NAME brpc_adapter_test COMMAND brpc_adapter_test)
set_target_properties(brpc_adapter_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

# gtest requires c++17
target_compile_features(brpc_adapter_test
  PRIVATE
    cxx_std_17
)

target_link_libraries(brpc_adapter_test
  PRIVATE
    GTest::gtest_main
    mockcpp

    rpc_adapter_brpc
)

target_sources(brpc_adapter_test
  PRIVATE
    brpc_iobuf_adapter_test.cpp
    brpc_file_descriptor_test.cpp
)
