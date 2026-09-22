add_compile_definitions(UBSOCKET_UNIT_TEST)
include_directories(
  ${UBSOCKET_BASE_DIR}/csrc/
  ${UBSOCKET_BASE_DIR}/csrc/common
)

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

add_executable(ubsocket_socket_helper_test ubsocket_socket_helper_test.cpp)
add_test(NAME ubsocket_socket_helper_test COMMAND ubsocket_socket_helper_test)
set_target_properties(ubsocket_socket_helper_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
target_compile_options(ubsocket_socket_helper_test
  PRIVATE
    -fno-access-control
)
target_link_libraries(ubsocket_socket_helper_test
  PRIVATE
    ubsocket_static
    mockcpp
    GTest::gtest_main
    pthread
)

add_executable(ubsocket_socket_test "")
add_test(NAME ubsocket_socket_test COMMAND ubsocket_socket_test)
set_target_properties(ubsocket_socket_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_socket_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_socket_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_socket_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_socket_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/core/umq
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/csrc/profiling
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
    ${UBSOCKET_BASE_DIR}/include
)

target_link_libraries(ubsocket_socket_test
  PRIVATE
    ubsocket_static
    boundscheck
    mockcpp
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_socket_test
  PRIVATE
    ubsocket_socket_test.cpp
)

add_executable(ubsocket_ring_buffer_test "")
add_test(NAME ubsocket_ring_buffer_test COMMAND ubsocket_ring_buffer_test)
set_target_properties(ubsocket_ring_buffer_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_ring_buffer_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_ring_buffer_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_include_directories(ubsocket_ring_buffer_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/core
)

target_link_libraries(ubsocket_ring_buffer_test
  PRIVATE
    ubsocket_static
    boundscheck
    mockcpp
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_ring_buffer_test
  PRIVATE
    ubsocket_ring_buffer_test.cpp
)

add_executable(ubsocket_core_types_test "")
add_test(NAME ubsocket_core_types_test COMMAND ubsocket_core_types_test)
set_target_properties(ubsocket_core_types_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_core_types_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_core_types_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_core_types_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_core_types_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/profiling
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
)

target_link_libraries(ubsocket_core_types_test
  PRIVATE
    ubsocket_static
    boundscheck
    mockcpp
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_core_types_test
  PRIVATE
    ubsocket_core_types_test.cpp
)


add_executable(ubsocket_setting_validator_test "")
add_test(NAME ubsocket_setting_validator_test COMMAND ubsocket_setting_validator_test)
set_target_properties(ubsocket_setting_validator_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_setting_validator_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_setting_validator_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_include_directories(ubsocket_setting_validator_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
)

target_link_libraries(ubsocket_setting_validator_test
  PRIVATE
    ubsocket_static
    boundscheck
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_setting_validator_test
  PRIVATE
    ubsocket_setting_validator_test.cpp
)


add_executable(ubsocket_umq_setting_multi_level_test "")
add_test(NAME ubsocket_umq_setting_multi_level_test COMMAND ubsocket_umq_setting_multi_level_test)
set_target_properties(ubsocket_umq_setting_multi_level_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_umq_setting_multi_level_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_umq_setting_multi_level_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_include_directories(ubsocket_umq_setting_multi_level_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/common
)

target_link_libraries(ubsocket_umq_setting_multi_level_test
  PRIVATE
    ubsocket_static
    boundscheck
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_umq_setting_multi_level_test
  PRIVATE
    umq/umq_setting_multi_level_test.cpp
)


add_executable(ubsocket_ring_queue_test "")
add_test(NAME ubsocket_ring_queue_test COMMAND ubsocket_ring_queue_test)
set_target_properties(ubsocket_ring_queue_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_ring_queue_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_ring_queue_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_ring_queue_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_ring_queue_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
)

target_link_libraries(ubsocket_ring_queue_test
  PRIVATE
    ubsocket_static
    boundscheck
    mockcpp
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_ring_queue_test
  PRIVATE
    ubsocket_ring_queue_test.cpp
)


add_executable(ubsocket_queue_heap_test "")
add_test(NAME ubsocket_queue_heap_test COMMAND ubsocket_queue_heap_test)
set_target_properties(ubsocket_queue_heap_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_queue_heap_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_queue_heap_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_queue_heap_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_queue_heap_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/core
)

target_link_libraries(ubsocket_queue_heap_test
  PRIVATE
    ubsocket_static
    boundscheck
    mockcpp
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_queue_heap_test
  PRIVATE
    ubsocket_queue_heap_test.cpp
)


add_executable(ubsocket_scope_exit_ref_test "")
add_test(NAME ubsocket_scope_exit_ref_test COMMAND ubsocket_scope_exit_ref_test)
set_target_properties(ubsocket_scope_exit_ref_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_scope_exit_ref_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_scope_exit_ref_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_include_directories(ubsocket_scope_exit_ref_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
)

target_link_libraries(ubsocket_scope_exit_ref_test
  PRIVATE
    ubsocket_static
    boundscheck
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_scope_exit_ref_test
  PRIVATE
    ubsocket_scope_exit_ref_test.cpp
)


add_executable(ubsocket_bitset_lock_seq_test "")
add_test(NAME ubsocket_bitset_lock_seq_test COMMAND ubsocket_bitset_lock_seq_test)
set_target_properties(ubsocket_bitset_lock_seq_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_bitset_lock_seq_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_bitset_lock_seq_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_bitset_lock_seq_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_bitset_lock_seq_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/core
)

target_link_libraries(ubsocket_bitset_lock_seq_test
  PRIVATE
    ubsocket_static
    boundscheck
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_bitset_lock_seq_test
  PRIVATE
    ubsocket_bitset_lock_seq_test.cpp
)

add_executable(ubsocket_common_utils_test "")
add_test(NAME ubsocket_common_utils_test COMMAND ubsocket_common_utils_test)
set_target_properties(ubsocket_common_utils_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_common_utils_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_common_utils_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_include_directories(ubsocket_common_utils_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${CMAKE_BINARY_DIR}/csrc/common
)

target_link_libraries(ubsocket_common_utils_test
  PRIVATE
    ubsocket_static
    boundscheck
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_common_utils_test
  PRIVATE
    ubsocket_common_utils_test.cpp
)

add_executable(ubsocket_struct_helper_test "")
add_test(NAME ubsocket_struct_helper_test COMMAND ubsocket_struct_helper_test)
set_target_properties(ubsocket_struct_helper_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_struct_helper_test PRIVATE cxx_std_17)
target_compile_definitions(ubsocket_struct_helper_test PRIVATE UBSOCKET_UNIT_TEST)
target_include_directories(ubsocket_struct_helper_test PRIVATE
  ${UBSOCKET_BASE_DIR}/csrc
  ${UBSOCKET_BASE_DIR}/csrc/common
  ${UBSOCKET_BASE_DIR}/include
)
target_link_libraries(ubsocket_struct_helper_test PRIVATE
  GTest::gtest_main
  pthread
)
target_sources(ubsocket_struct_helper_test PRIVATE ubsocket_struct_helper_test.cpp)

add_executable(ubsocket_leaky_singleton_test "")
add_test(NAME ubsocket_leaky_singleton_test COMMAND ubsocket_leaky_singleton_test)
set_target_properties(ubsocket_leaky_singleton_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_leaky_singleton_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_leaky_singleton_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_include_directories(ubsocket_leaky_singleton_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
)

target_link_libraries(ubsocket_leaky_singleton_test
  PRIVATE
    ubsocket_static
    boundscheck
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_leaky_singleton_test
  PRIVATE
    ubsocket_leaky_singleton_test.cpp
)

add_executable(ubsocket_buf_converter_test "")
add_test(NAME ubsocket_buf_converter_test COMMAND ubsocket_buf_converter_test)
set_target_properties(ubsocket_buf_converter_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_buf_converter_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_buf_converter_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_include_directories(ubsocket_buf_converter_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/core
)

target_link_libraries(ubsocket_buf_converter_test
  PRIVATE
    ubsocket_static
    boundscheck
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_buf_converter_test
  PRIVATE
    ubsocket_buf_converter_test.cpp
)

add_executable(ubsocket_set_test "")
add_test(NAME ubsocket_set_test COMMAND ubsocket_set_test)
set_target_properties(ubsocket_set_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_set_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_set_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_set_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_set_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
)

target_link_libraries(ubsocket_set_test
  PRIVATE
    ubsocket_static
    boundscheck
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_set_test
  PRIVATE
    ubsocket_set_test.cpp
)

add_executable(ubsocket_obj_statistics_test "")
add_test(NAME ubsocket_obj_statistics_test COMMAND ubsocket_obj_statistics_test)
set_target_properties(ubsocket_obj_statistics_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_obj_statistics_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_obj_statistics_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_include_directories(ubsocket_obj_statistics_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
)

target_link_libraries(ubsocket_obj_statistics_test
  PRIVATE
    ubsocket_static
    boundscheck
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_obj_statistics_test
  PRIVATE
    ubsocket_obj_statistics_test.cpp
)

add_executable(ubsocket_thread_pool_test "")
add_test(NAME ubsocket_thread_pool_test COMMAND ubsocket_thread_pool_test)
set_target_properties(ubsocket_thread_pool_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_thread_pool_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_thread_pool_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_thread_pool_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_thread_pool_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
)

target_link_libraries(ubsocket_thread_pool_test
  PRIVATE
    ubsocket_static
    boundscheck
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_thread_pool_test
  PRIVATE
    ubsocket_thread_pool_test.cpp
)

add_executable(ubsocket_global_setting_test "")
add_test(NAME ubsocket_global_setting_test COMMAND ubsocket_global_setting_test)
set_target_properties(ubsocket_global_setting_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_global_setting_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_global_setting_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_global_setting_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_global_setting_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
)

target_link_libraries(ubsocket_global_setting_test
  PRIVATE
    ubsocket_static
    boundscheck
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_global_setting_test
  PRIVATE
    ubsocket_global_setting_test.cpp
)

add_executable(ubsocket_degrade_api_test "")
add_test(NAME ubsocket_degrade_api_test COMMAND ubsocket_degrade_api_test)
set_target_properties(ubsocket_degrade_api_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_degrade_api_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_degrade_api_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_include_directories(ubsocket_degrade_api_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/include
)

target_link_libraries(ubsocket_degrade_api_test
  PRIVATE
    ubsocket_static
    boundscheck
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_degrade_api_test
  PRIVATE
    ubsocket_degrade_api_test.cpp
)

add_executable(ubsocket_test "")
add_test(NAME ubsocket_test COMMAND ubsocket_test)
set_target_properties(ubsocket_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
target_compile_features(ubsocket_test PRIVATE cxx_std_17)
target_compile_definitions(ubsocket_test PRIVATE UBSOCKET_UNIT_TEST)
target_include_directories(ubsocket_test PRIVATE
  ${UBSOCKET_BASE_DIR}/csrc
  ${UBSOCKET_BASE_DIR}/csrc/common
  ${UBSOCKET_BASE_DIR}/include
)
target_link_libraries(ubsocket_test PRIVATE
  ubsocket_static
  boundscheck
  mockcpp
  GTest::gtest_main
  pthread
)
target_sources(ubsocket_test PRIVATE ubsocket_test.cpp)

add_executable(ubsocket_version_test "")
add_test(NAME ubsocket_version_test COMMAND ubsocket_version_test)
set_target_properties(ubsocket_version_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_version_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_version_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_version_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_version_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
)

target_link_libraries(ubsocket_version_test
  PRIVATE
    ubsocket_static
    boundscheck
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_version_test
  PRIVATE
    ubsocket_version_test.cpp
)

add_executable(ubsocket_port_cooldown_test "")
add_test(NAME ubsocket_port_cooldown_test COMMAND ubsocket_port_cooldown_test)
set_target_properties(ubsocket_port_cooldown_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_port_cooldown_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_port_cooldown_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_include_directories(ubsocket_port_cooldown_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
)

target_link_libraries(ubsocket_port_cooldown_test
  PRIVATE
    ubsocket_static
    boundscheck
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_port_cooldown_test
  PRIVATE
    ubsocket_port_cooldown_test.cpp
)

add_executable(ubsocket_bigdata_proto_test "")
add_test(NAME ubsocket_bigdata_proto_test COMMAND ubsocket_bigdata_proto_test)
set_target_properties(ubsocket_bigdata_proto_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_bigdata_proto_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_bigdata_proto_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_include_directories(ubsocket_bigdata_proto_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
)

target_link_libraries(ubsocket_bigdata_proto_test
  PRIVATE
    ubsocket_static
    boundscheck
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_bigdata_proto_test
  PRIVATE
    ubsocket_bigdata_proto_test.cpp
)

add_executable(ubsocket_gen_check_test "")
add_test(NAME ubsocket_gen_check_test COMMAND ubsocket_gen_check_test)
set_target_properties(ubsocket_gen_check_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_gen_check_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_gen_check_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_include_directories(ubsocket_gen_check_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/core/umq
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/include
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
)

target_link_directories(ubsocket_gen_check_test
  PRIVATE
    ${UMQ_LIB_DIR}
    ${UMQ_QBUF_LIB_DIR}
)

target_link_libraries(ubsocket_gen_check_test
  PRIVATE
    ubsocket_static
    umq
    umq_buf
    boundscheck
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_gen_check_test
  PRIVATE
    ubsocket_gen_check_test.cpp
)

add_executable(ubsocket_tx_unified_poller_test "")
add_test(NAME ubsocket_tx_unified_poller_test COMMAND ubsocket_tx_unified_poller_test)
set_target_properties(ubsocket_tx_unified_poller_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_tx_unified_poller_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_tx_unified_poller_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_tx_unified_poller_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_tx_unified_poller_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/core/umq
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/csrc/profiling
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
    ${UBSOCKET_BASE_DIR}/include
)

target_link_libraries(ubsocket_tx_unified_poller_test
  PRIVATE
    ubsocket_static
    boundscheck
    mockcpp
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_tx_unified_poller_test
  PRIVATE
    ubsocket_tx_unified_poller_test.cpp
)

add_executable(ubsocket_bigdata_test "")
add_test(NAME ubsocket_bigdata_test COMMAND ubsocket_bigdata_test)
set_target_properties(ubsocket_bigdata_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_bigdata_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_bigdata_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_bigdata_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_bigdata_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/core/umq
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/csrc/profiling
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
    ${UBSOCKET_BASE_DIR}/include
)

target_link_libraries(ubsocket_bigdata_test
  PRIVATE
    # 不链 boundscheck(securec 安全库): csrc 生产代码与测试代码均无 securec
    # 函数调用,链了会给动态库版本引入多余 DT_NEEDED(见 SKILL.md §6 模板)
    ubsocket_static
    mockcpp
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_bigdata_test
  PRIVATE
    ubsocket_bigdata_test.cpp
)

# ubsocket_bigdata_order.h 纯头 inline 辅助直测(拆分自 ubsocket_bigdata_test
# §1, 依据 research §9.1): 零 mock 零 fixture 零 UMQ 调用, 不链
# ubsocket_static/mockcpp, 仅 GTest::gtest_main(极轻形态, 仿 SKILL.md §6
# converter-only 模板; 依赖 umq_pro_types.h/umq_types.h 纯类型头, 故保留
# ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq include 路径)
add_executable(ubsocket_bigdata_order_test "")
add_test(NAME ubsocket_bigdata_order_test COMMAND ubsocket_bigdata_order_test)
set_target_properties(ubsocket_bigdata_order_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_bigdata_order_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_bigdata_order_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_include_directories(ubsocket_bigdata_order_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
)

target_link_libraries(ubsocket_bigdata_order_test
  PRIVATE
    GTest::gtest_main
)

target_sources(ubsocket_bigdata_order_test
  PRIVATE
    ubsocket_bigdata_order_test.cpp
)

add_executable(ubsocket_link_trace_test "")
add_test(NAME ubsocket_link_trace_test COMMAND ubsocket_link_trace_test)
set_target_properties(ubsocket_link_trace_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_link_trace_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_link_trace_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_link_trace_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_link_trace_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
)

target_link_libraries(ubsocket_link_trace_test
  PRIVATE
    ubsocket_static
    boundscheck
    mockcpp
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_link_trace_test
  PRIVATE
    ubsocket_link_trace_test.cpp
)

add_executable(ubsocket_logger_test "")
add_test(NAME ubsocket_logger_test COMMAND ubsocket_logger_test)
set_target_properties(ubsocket_logger_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_logger_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_logger_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_logger_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_logger_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
)

target_link_libraries(ubsocket_logger_test
  PRIVATE
    ubsocket_static
    boundscheck
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_logger_test
  PRIVATE
    ubsocket_logger_test.cpp
)

add_executable(ubsocket_data_rx_test "")
add_test(NAME ubsocket_data_rx_test COMMAND ubsocket_data_rx_test)
set_target_properties(ubsocket_data_rx_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_data_rx_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_data_rx_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_data_rx_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_data_rx_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/core/umq
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/profiling
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
)

target_link_libraries(ubsocket_data_rx_test
  PRIVATE
    ubsocket_static
    boundscheck
    mockcpp
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_data_rx_test
  PRIVATE
    ubsocket_data_rx_test.cpp
)

add_executable(ubsocket_data_tx_test "")
add_test(NAME ubsocket_data_tx_test COMMAND ubsocket_data_tx_test)
set_target_properties(ubsocket_data_tx_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_data_tx_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_data_tx_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_data_tx_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_data_tx_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/core/umq
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/profiling
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
)

target_link_libraries(ubsocket_data_tx_test
  PRIVATE
    ubsocket_static
    boundscheck
    mockcpp
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_data_tx_test
  PRIVATE
    ubsocket_data_tx_test.cpp
)

add_executable(ubsocket_data_test "")
add_test(NAME ubsocket_data_test COMMAND ubsocket_data_test)
set_target_properties(ubsocket_data_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
target_compile_features(ubsocket_data_test PRIVATE cxx_std_17)
target_compile_definitions(ubsocket_data_test PRIVATE UBSOCKET_UNIT_TEST)
target_compile_options(ubsocket_data_test PRIVATE -fno-access-control)
target_include_directories(ubsocket_data_test PRIVATE
  ${UBSOCKET_BASE_DIR}/csrc
  ${UBSOCKET_BASE_DIR}/csrc/common
  ${UBSOCKET_BASE_DIR}/csrc/core
  ${UBSOCKET_BASE_DIR}/csrc/core/umq
  ${UBSOCKET_BASE_DIR}/csrc/iobuf
  ${UBSOCKET_BASE_DIR}/csrc/profiling
  ${UBSOCKET_BASE_DIR}/csrc/under_api
  ${UBSOCKET_BASE_DIR}/../hcom/umq/include
  ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
  ${UBSOCKET_BASE_DIR}/include)
target_link_libraries(ubsocket_data_test PRIVATE ubsocket_static boundscheck mockcpp GTest::gtest_main pthread)
target_sources(ubsocket_data_test PRIVATE ubsocket_data_test.cpp ${UBSOCKET_BASE_DIR}/csrc/ubsocket_data.cpp)

add_executable(ubsocket_epoll_api_test "")
add_test(NAME ubsocket_epoll_api_test COMMAND ubsocket_epoll_api_test)
set_target_properties(ubsocket_epoll_api_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
target_compile_features(ubsocket_epoll_api_test PRIVATE cxx_std_17)
target_compile_definitions(ubsocket_epoll_api_test PRIVATE UBSOCKET_UNIT_TEST)
target_compile_options(ubsocket_epoll_api_test PRIVATE -fno-access-control)
target_include_directories(ubsocket_epoll_api_test PRIVATE
  ${UBSOCKET_BASE_DIR}/csrc ${UBSOCKET_BASE_DIR}/csrc/common ${UBSOCKET_BASE_DIR}/csrc/core
  ${UBSOCKET_BASE_DIR}/csrc/core/umq ${UBSOCKET_BASE_DIR}/csrc/iobuf ${UBSOCKET_BASE_DIR}/csrc/profiling
  ${UBSOCKET_BASE_DIR}/csrc/under_api ${UBSOCKET_BASE_DIR}/../hcom/umq/include
  ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq ${UBSOCKET_BASE_DIR}/include)
target_link_libraries(ubsocket_epoll_api_test PRIVATE ubsocket_static boundscheck mockcpp GTest::gtest_main pthread)
target_sources(ubsocket_epoll_api_test PRIVATE ubsocket_epoll_test.cpp ${UBSOCKET_BASE_DIR}/csrc/ubsocket_epoll.cpp)

add_executable(ubsocket_sock_api_test "")
add_test(NAME ubsocket_sock_api_test COMMAND ubsocket_sock_api_test)
set_target_properties(ubsocket_sock_api_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
target_compile_features(ubsocket_sock_api_test PRIVATE cxx_std_17)
target_compile_definitions(ubsocket_sock_api_test PRIVATE UBSOCKET_UNIT_TEST)
target_compile_options(ubsocket_sock_api_test PRIVATE -fno-access-control)
target_include_directories(ubsocket_sock_api_test PRIVATE
  ${UBSOCKET_BASE_DIR}/csrc ${UBSOCKET_BASE_DIR}/csrc/common ${UBSOCKET_BASE_DIR}/csrc/core
  ${UBSOCKET_BASE_DIR}/csrc/core/umq ${UBSOCKET_BASE_DIR}/csrc/iobuf ${UBSOCKET_BASE_DIR}/csrc/profiling
  ${UBSOCKET_BASE_DIR}/csrc/under_api ${UBSOCKET_BASE_DIR}/../hcom/umq/include
  ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq ${UBSOCKET_BASE_DIR}/include)
target_link_libraries(ubsocket_sock_api_test PRIVATE ubsocket_static boundscheck mockcpp GTest::gtest_main pthread)
target_sources(ubsocket_sock_api_test PRIVATE ubsocket_sock_test.cpp ${UBSOCKET_BASE_DIR}/csrc/ubsocket_sock.cpp)

add_executable(ubsocket_event_epoll_test "")
add_test(NAME ubsocket_event_epoll_test COMMAND ubsocket_event_epoll_test)
set_target_properties(ubsocket_event_epoll_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_event_epoll_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_event_epoll_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_event_epoll_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_event_epoll_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/core/umq
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/profiling
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/include
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
)

target_link_libraries(ubsocket_event_epoll_test
  PRIVATE
    ubsocket_static
    boundscheck
    mockcpp
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_event_epoll_test
  PRIVATE
    ubsocket_event_epoll_test.cpp
)

add_executable(ubsocket_wakeup_event_test "")
add_test(NAME ubsocket_wakeup_event_test COMMAND ubsocket_wakeup_event_test)
set_target_properties(ubsocket_wakeup_event_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_wakeup_event_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_wakeup_event_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_wakeup_event_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_wakeup_event_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/profiling
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
)

target_link_libraries(ubsocket_wakeup_event_test
  PRIVATE
    ubsocket_static
    boundscheck
    mockcpp
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_wakeup_event_test
  PRIVATE
    ubsocket_wakeup_event_test.cpp
)

add_executable(ubsocket_iobuf_test "")
add_test(NAME ubsocket_iobuf_test COMMAND ubsocket_iobuf_test)
set_target_properties(ubsocket_iobuf_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_iobuf_test PRIVATE cxx_std_17)
target_compile_definitions(ubsocket_iobuf_test PRIVATE UBSOCKET_UNIT_TEST)
target_compile_options(ubsocket_iobuf_test PRIVATE -fno-access-control)
target_include_directories(ubsocket_iobuf_test PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/include
)
target_link_libraries(ubsocket_iobuf_test PRIVATE
    ubsocket_static GTest::gtest_main pthread
)
target_sources(ubsocket_iobuf_test PRIVATE ubsocket_iobuf_test.cpp)

add_executable(ubsocket_socket_acceptor_test "")
add_test(NAME ubsocket_socket_acceptor_test COMMAND ubsocket_socket_acceptor_test)
set_target_properties(ubsocket_socket_acceptor_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_socket_acceptor_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_socket_acceptor_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_socket_acceptor_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_socket_acceptor_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/core/umq
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/csrc/profiling
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
    ${UBSOCKET_BASE_DIR}/include
)

target_link_libraries(ubsocket_socket_acceptor_test
  PRIVATE
    ubsocket_static
    boundscheck
    mockcpp
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_socket_acceptor_test
  PRIVATE
    ubsocket_socket_acceptor_test.cpp
)

add_executable(ubsocket_socket_connector_test "")
add_test(NAME ubsocket_socket_connector_test COMMAND ubsocket_socket_connector_test)
set_target_properties(ubsocket_socket_connector_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_socket_connector_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_socket_connector_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_socket_connector_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_socket_connector_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/core/umq
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/csrc/profiling
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
    ${UBSOCKET_BASE_DIR}/include
)

target_link_libraries(ubsocket_socket_connector_test
  PRIVATE
    ubsocket_static
    boundscheck
    mockcpp
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_socket_connector_test
  PRIVATE
    ubsocket_socket_connector_test.cpp
)

add_subdirectory(profiling)
