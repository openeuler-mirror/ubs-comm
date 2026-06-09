set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")
add_compile_options(-fno-access-control)

set(UMQ_INCLUDE_DIRS
    ${UBSOCKET_BASE_DIR}/csrc/core/umq
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
)

function(add_umq_test TARGET_NAME)
    add_executable(${TARGET_NAME} "")
    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
    set_target_properties(${TARGET_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
    target_compile_features(${TARGET_NAME} PRIVATE cxx_std_17)
    target_compile_definitions(${TARGET_NAME} PRIVATE UBSOCKET_UNIT_TEST)
    target_include_directories(${TARGET_NAME} PRIVATE ${UMQ_INCLUDE_DIRS})
endfunction()

add_umq_test(umq_errno_converter_test)
target_link_libraries(umq_errno_converter_test PRIVATE GTest::gtest_main)
target_sources(umq_errno_converter_test PRIVATE
    umq_errno_converter_test.cpp
    ${UBSOCKET_BASE_DIR}/csrc/core/umq/umq_errno_converter.cpp
)

add_umq_test(umq_socket_connector_test)
target_include_directories(umq_socket_connector_test PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/csrc/profiling
    ${UBSOCKET_BASE_DIR}/csrc/profiling/impl
    ${UBSOCKET_BASE_DIR}/csrc/profiling/statistics
    ${UBSOCKET_BASE_DIR}/include
)
target_link_libraries(umq_socket_connector_test PRIVATE
    ubsocket_static boundscheck mockcpp GTest::gtest_main pthread
)
target_sources(umq_socket_connector_test PRIVATE
    umq_socket_connector_test.cpp
)