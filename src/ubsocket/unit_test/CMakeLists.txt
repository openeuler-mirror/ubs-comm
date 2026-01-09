add_executable(ubsocket_test "")
add_test(NAME ubsocket_test COMMAND ubsocket_test)
set_target_properties(ubsocket_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

# shutup, mockcpp.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")

# gtest requires c++17
target_compile_features(ubsocket_test
  PRIVATE
    cxx_std_17
)

target_link_libraries(ubsocket_test
  PRIVATE
    GTest::gtest_main
    mockcpp

    ubsocket
)

target_sources(ubsocket_test
  PRIVATE
    urpc_util_test.cpp
    urpc_adpt_vlog_test.cpp
)

add_subdirectory(brpc)
