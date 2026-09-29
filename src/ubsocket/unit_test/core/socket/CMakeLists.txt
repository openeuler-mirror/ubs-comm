set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")
add_compile_options(-fno-access-control)

add_executable(ubsocket_send_recv_test "")
add_test(NAME ubsocket_send_recv_test COMMAND ubsocket_send_recv_test)
set_target_properties(ubsocket_send_recv_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
target_compile_features(ubsocket_send_recv_test PRIVATE cxx_std_17)
target_compile_definitions(ubsocket_send_recv_test PRIVATE UBSOCKET_UNIT_TEST)
target_include_directories(ubsocket_send_recv_test PRIVATE
        ${UBSOCKET_BASE_DIR}/csrc
        ${UBSOCKET_BASE_DIR}/csrc/core
        ${UBSOCKET_BASE_DIR}/csrc/core/urma
        ${UBSOCKET_BASE_DIR}/csrc/common
        ${UBSOCKET_BASE_DIR}/csrc/iobuf
        ${UBSOCKET_BASE_DIR}/csrc/under_api
        ${UBSOCKET_BASE_DIR}/csrc/under_api/urma
        ${UBSOCKET_BASE_DIR}/include
        ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
)
target_link_libraries(ubsocket_send_recv_test PRIVATE
        ubsocket_static GTest::gtest_main pthread
)
target_sources(ubsocket_send_recv_test PRIVATE
        ubsocket_send_recv_test.cpp
)

function(add_conn_test TARGET_NAME)
    add_executable(${TARGET_NAME} "")
    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
    set_target_properties(${TARGET_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
    target_compile_features(${TARGET_NAME} PRIVATE cxx_std_17)
    target_compile_definitions(${TARGET_NAME} PRIVATE UBSOCKET_UNIT_TEST)
    target_include_directories(${TARGET_NAME} PRIVATE
            ${UBSOCKET_BASE_DIR}/csrc
            ${UBSOCKET_BASE_DIR}/csrc/core
            ${UBSOCKET_BASE_DIR}/csrc/core/urma
            ${UBSOCKET_BASE_DIR}/csrc/common
            ${UBSOCKET_BASE_DIR}/csrc/iobuf
            ${UBSOCKET_BASE_DIR}/csrc/under_api
            ${UBSOCKET_BASE_DIR}/csrc/under_api/urma
            ${UBSOCKET_BASE_DIR}/include
            ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
    )
endfunction()

add_conn_test(ubsocket_conn_buffer_test)
target_link_libraries(ubsocket_conn_buffer_test PRIVATE
        ubsocket_static GTest::gtest_main pthread
)
target_sources(ubsocket_conn_buffer_test PRIVATE
        ubsocket_conn_buffer_test.cpp
)

add_conn_test(ubsocket_conn_ops_base_test)
target_link_libraries(ubsocket_conn_ops_base_test PRIVATE
        ubsocket_static boundscheck mockcpp GTest::gtest_main pthread
)
target_sources(ubsocket_conn_ops_base_test PRIVATE
        ubsocket_conn_ops_base_test.cpp
)

add_conn_test(ubsocket_conn_reactor_test)
target_link_libraries(ubsocket_conn_reactor_test PRIVATE
        ubsocket_static boundscheck mockcpp GTest::gtest_main pthread
)
target_sources(ubsocket_conn_reactor_test PRIVATE
        ubsocket_conn_reactor_test.cpp
)
