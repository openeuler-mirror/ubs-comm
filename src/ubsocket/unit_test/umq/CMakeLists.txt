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

add_umq_test(umq_socket_acceptor_test)
target_include_directories(umq_socket_acceptor_test PRIVATE
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
target_link_libraries(umq_socket_acceptor_test PRIVATE
    ubsocket_static boundscheck mockcpp GTest::gtest_main pthread
)
target_sources(umq_socket_acceptor_test PRIVATE
    umq_socket_acceptor_test.cpp
)

add_umq_test(umq_tp_event_epoll_runner_ops_test)
target_include_directories(umq_tp_event_epoll_runner_ops_test PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/include
)
target_link_libraries(umq_tp_event_epoll_runner_ops_test PRIVATE
    ubsocket_static boundscheck mockcpp GTest::gtest_main pthread
)
target_sources(umq_tp_event_epoll_runner_ops_test PRIVATE
    umq_tp_event_epoll_runner_ops_test.cpp
)

add_umq_test(umq_tp_tx_epoll_runner_ops_test)
target_include_directories(umq_tp_tx_epoll_runner_ops_test PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/include
)
target_link_libraries(umq_tp_tx_epoll_runner_ops_test PRIVATE
    ubsocket_static boundscheck mockcpp GTest::gtest_main pthread
)
target_sources(umq_tp_tx_epoll_runner_ops_test PRIVATE
    umq_tp_tx_epoll_runner_ops_test.cpp
)

add_umq_test(umq_setting_multi_level_test)
target_include_directories(umq_setting_multi_level_test PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
    ${UBSOCKET_BASE_DIR}/include
)
target_link_libraries(umq_setting_multi_level_test PRIVATE
    ubsocket_static mockcpp GTest::gtest_main pthread
)
target_sources(umq_setting_multi_level_test PRIVATE
    umq_setting_multi_level_test.cpp
)

add_umq_test(umq_setting_test)
target_include_directories(umq_setting_test PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/include
)
target_link_libraries(umq_setting_test PRIVATE
    ubsocket_static mockcpp GTest::gtest_main pthread
)
target_sources(umq_setting_test PRIVATE
    umq_setting_test.cpp
)

add_umq_test(umq_tx_helper_multi_level_test)
target_include_directories(umq_tx_helper_multi_level_test PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/include
)
target_link_libraries(umq_tx_helper_multi_level_test PRIVATE
    ubsocket_static mockcpp GTest::gtest_main pthread
)
target_sources(umq_tx_helper_multi_level_test PRIVATE
    umq_tx_helper_multi_level_test.cpp
)

add_umq_test(umq_share_jfr_epoll_runner_ops_test)
target_include_directories(umq_share_jfr_epoll_runner_ops_test PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/csrc/profiling
    ${UBSOCKET_BASE_DIR}/include
)
target_link_libraries(umq_share_jfr_epoll_runner_ops_test PRIVATE
    ubsocket_static mockcpp GTest::gtest_main pthread
)
target_sources(umq_share_jfr_epoll_runner_ops_test PRIVATE
    umq_share_jfr_epoll_runner_ops_test.cpp
)

add_umq_test(umq_zcopy_allocator_multi_level_test)
target_include_directories(umq_zcopy_allocator_multi_level_test PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/include
)
target_link_libraries(umq_zcopy_allocator_multi_level_test PRIVATE
    ubsocket_static mockcpp GTest::gtest_main pthread
)
target_sources(umq_zcopy_allocator_multi_level_test PRIVATE
    umq_zcopy_allocator_multi_level_test.cpp
)

add_umq_test(umq_buffer_receive_queue_test)
target_include_directories(umq_buffer_receive_queue_test PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/include
)
target_link_libraries(umq_buffer_receive_queue_test PRIVATE
    ubsocket_static mockcpp GTest::gtest_main pthread
)
target_sources(umq_buffer_receive_queue_test PRIVATE
    umq_buffer_receive_queue_test.cpp
)

add_umq_test(umq_conn_helper_test)
target_include_directories(umq_conn_helper_test PRIVATE
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
target_link_libraries(umq_conn_helper_test PRIVATE
    ubsocket_static mockcpp GTest::gtest_main pthread
)
target_sources(umq_conn_helper_test PRIVATE
    umq_conn_helper_test.cpp
)

add_umq_test(umq_tp_wait_queue_test)
target_include_directories(umq_tp_wait_queue_test PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/csrc/profiling
    ${UBSOCKET_BASE_DIR}/include
)
target_link_libraries(umq_tp_wait_queue_test PRIVATE
    ubsocket_static mockcpp GTest::gtest_main pthread
)
target_sources(umq_tp_wait_queue_test PRIVATE
    umq_tp_wait_queue_test.cpp
)

add_umq_test(umq_tx_helper_test)
target_include_directories(umq_tx_helper_test PRIVATE
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
target_link_libraries(umq_tx_helper_test PRIVATE
    ubsocket_static mockcpp GTest::gtest_main pthread
)
target_sources(umq_tx_helper_test PRIVATE
    umq_tx_helper_test.cpp
)

add_umq_test(umq_data_rx_ops_test)
target_include_directories(umq_data_rx_ops_test PRIVATE
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
target_link_libraries(umq_data_rx_ops_test PRIVATE
    ubsocket_static mockcpp GTest::gtest_main pthread
)
target_sources(umq_data_rx_ops_test PRIVATE
    umq_data_rx_ops_test.cpp
)

add_umq_test(umq_data_tx_ops_test)
target_include_directories(umq_data_tx_ops_test PRIVATE
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
target_link_libraries(umq_data_tx_ops_test PRIVATE
    ubsocket_static mockcpp GTest::gtest_main pthread
)
target_sources(umq_data_tx_ops_test PRIVATE
    umq_data_tx_ops_test.cpp
)

add_umq_test(umq_transport_pool_test)
target_include_directories(umq_transport_pool_test PRIVATE
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
target_link_libraries(umq_transport_pool_test PRIVATE
    ubsocket_static mockcpp GTest::gtest_main pthread
)
target_sources(umq_transport_pool_test PRIVATE
    umq_transport_pool_test.cpp
)

add_umq_test(umq_backend_test)
target_include_directories(umq_backend_test PRIVATE
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
target_link_libraries(umq_backend_test PRIVATE
    ubsocket_static mockcpp GTest::gtest_main pthread
)
target_sources(umq_backend_test PRIVATE
    umq_backend_test.cpp
)

add_umq_test(umq_socket_test)
target_include_directories(umq_socket_test PRIVATE
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
target_link_libraries(umq_socket_test PRIVATE
    ubsocket_static boundscheck mockcpp GTest::gtest_main pthread
)
target_sources(umq_socket_test PRIVATE
    umq_socket_test.cpp
)

add_umq_test(umq_mid_risk_regression_test)
target_include_directories(umq_mid_risk_regression_test PRIVATE
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
target_link_libraries(umq_mid_risk_regression_test PRIVATE
    ubsocket_static boundscheck mockcpp GTest::gtest_main pthread
)
target_sources(umq_mid_risk_regression_test PRIVATE
    umq_mid_risk_regression_test.cpp
)
