add_executable(umq_errno_converter_test "")
add_test(NAME umq_errno_converter_test COMMAND umq_errno_converter_test)
set_target_properties(umq_errno_converter_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

add_executable(umq_ops_errno_test "")
add_test(NAME umq_ops_errno_test COMMAND umq_ops_errno_test)
set_target_properties(umq_ops_errno_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

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

target_compile_definitions(umq_errno_converter_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_definitions(umq_ops_errno_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

add_compile_options(-fno-access-control)

target_compile_options(umq_ops_errno_test
  PRIVATE
    -fno-access-control
)

target_include_directories(umq_errno_converter_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc/core/umq
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
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
)

target_link_libraries(umq_errno_converter_test
  PRIVATE
    GTest::gtest_main
)

target_link_libraries(umq_ops_errno_test
  PRIVATE
    ubsocket_static
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
add_subdirectory(profiling)

