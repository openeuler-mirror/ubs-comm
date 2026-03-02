add_executable(ubsocket_cli_test "")
add_test(NAME ubsocket_cli_test COMMAND ubsocket_cli_test)
set_target_properties(ubsocket_cli_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

# gtest requires c++17
target_compile_features(ubsocket_cli_test
  PRIVATE
    cxx_std_17
)

target_include_directories(ubsocket_cli_test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/cli
)

target_link_libraries(ubsocket_cli_test
  PRIVATE
    GTest::gtest_main
    mockcpp
    cli_objects
    ubsocket
)

target_sources(ubsocket_cli_test
  PRIVATE
    cli_client_test.cpp
    cli_args_parser_test.cpp
    cli_terminal_display_test.cpp
)