add_subdirectory(umq)

add_executable(ubsocket_signal_handler_test "")
add_test(NAME ubsocket_signal_handler_test COMMAND ubsocket_signal_handler_test)
set_target_properties(ubsocket_signal_handler_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_signal_handler_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_signal_handler_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_include_directories(ubsocket_signal_handler_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc
)

target_link_libraries(ubsocket_signal_handler_test
  PRIVATE
    ubsocket_static
    boundscheck
    mockcpp
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_signal_handler_test
  PRIVATE
    ubsocket_signal_handler_test.cpp
)

add_subdirectory(profiling)