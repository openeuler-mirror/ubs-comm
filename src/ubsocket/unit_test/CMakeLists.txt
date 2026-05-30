add_executable(umq_errno_converter_test "")
add_test(NAME umq_errno_converter_test COMMAND umq_errno_converter_test)
set_target_properties(umq_errno_converter_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

add_executable(umq_ops_errno_test "")
add_test(NAME umq_ops_errno_test COMMAND umq_ops_errno_test)
set_target_properties(umq_ops_errno_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

add_executable(mock_infrastructure_test "")
add_test(NAME mock_infrastructure_test COMMAND mock_infrastructure_test)
set_target_properties(mock_infrastructure_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

add_executable(iobuf_zcopy_adapter_test "")
add_test(NAME iobuf_zcopy_adapter_test COMMAND iobuf_zcopy_adapter_test)
set_target_properties(iobuf_zcopy_adapter_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")

target_compile_features(umq_errno_converter_test
  PRIVATE
    cxx_std_17
)

target_compile_features(umq_ops_errno_test
  PRIVATE
    cxx_std_17
)

target_compile_features(mock_infrastructure_test
  PRIVATE
    cxx_std_17
)

target_compile_features(iobuf_zcopy_adapter_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(umq_errno_converter_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_definitions(umq_ops_errno_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_definitions(mock_infrastructure_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_definitions(iobuf_zcopy_adapter_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

add_compile_options(-fno-access-control)

target_compile_options(umq_ops_errno_test
  PRIVATE
    -fno-access-control
)

target_compile_options(mock_infrastructure_test
  PRIVATE
    -fno-access-control
)

target_compile_options(iobuf_zcopy_adapter_test
  PRIVATE
    -fno-access-control
)

add_subdirectory(stub)

target_include_directories(umq_errno_converter_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc/core/umq
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
    ${CMAKE_CURRENT_SOURCE_DIR}/stub
)

target_include_directories(umq_ops_errno_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc/core/umq
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
    ${UBSOCKET_BASE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/stub
)

target_include_directories(mock_infrastructure_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/core/umq
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
    ${UBSOCKET_BASE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/stub
)

target_include_directories(iobuf_zcopy_adapter_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/core/umq
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
    ${UBSOCKET_BASE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/stub
)

target_link_libraries(umq_errno_converter_test
  PRIVATE
    GTest::gtest_main
)

target_link_libraries(umq_ops_errno_test
  PRIVATE
    ubsocket_static
    fake_epoll_static
    boundscheck
    mockcpp
    GTest::gtest_main
    pthread
)

target_link_libraries(mock_infrastructure_test
  PRIVATE
    ubsocket_static
    fake_epoll_static
    boundscheck
    mockcpp
    GTest::gtest_main
    pthread
)

target_link_libraries(iobuf_zcopy_adapter_test
  PRIVATE
    ubsocket_static
    fake_epoll_static
    boundscheck
    mockcpp
    GTest::gtest_main
    pthread
)

target_sources(umq_errno_converter_test
  PRIVATE
        umq_errno_converter_test.cpp
        ${UBSOCKET_BASE_DIR}/csrc/core/umq/umq_errno_converter.cpp
)

target_sources(umq_ops_errno_test
  PRIVATE
        umq_ops_errno_test.cpp
)

target_sources(mock_infrastructure_test
  PRIVATE
        mock_infrastructure_test.cpp
)

target_sources(iobuf_zcopy_adapter_test
  PRIVATE
        iobuf_zcopy_adapter_test.cpp
)

add_subdirectory(profiling)

