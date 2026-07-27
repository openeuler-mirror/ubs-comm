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

target_include_directories(ubsocket_core_types_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
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

add_subdirectory(profiling)
