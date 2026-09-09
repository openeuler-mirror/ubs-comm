set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")
add_compile_options(-fno-access-control)

set(URMA_INCLUDE_DIRS
        ${UBSOCKET_BASE_DIR}/csrc/core/urma
        ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
)

function(add_urma_test TARGET_NAME)
    add_executable(${TARGET_NAME} "")
    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
    set_target_properties(${TARGET_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
    target_compile_features(${TARGET_NAME} PRIVATE cxx_std_17)
    target_compile_definitions(${TARGET_NAME} PRIVATE UBSOCKET_UNIT_TEST)
    target_include_directories(${TARGET_NAME} PRIVATE ${URMA_INCLUDE_DIRS})
endfunction()

add_urma_test(urma_errno_converter_test)
target_link_libraries(urma_errno_converter_test PRIVATE GTest::gtest_main)
target_sources(urma_errno_converter_test PRIVATE
        urma_errno_converter_test.cpp
)

add_urma_test(urma_readable_event_queue_test)
target_include_directories(urma_readable_event_queue_test PRIVATE
        ${UBSOCKET_BASE_DIR}/csrc
        ${UBSOCKET_BASE_DIR}/csrc/core
        ${UBSOCKET_BASE_DIR}/csrc/common
        ${UBSOCKET_BASE_DIR}/csrc/under_api
        ${UBSOCKET_BASE_DIR}/csrc/under_api/urma
        ${UBSOCKET_BASE_DIR}/include
)
target_link_libraries(urma_readable_event_queue_test PRIVATE
        ubsocket_static boundscheck mockcpp GTest::gtest_main pthread
)
target_sources(urma_readable_event_queue_test PRIVATE
        urma_readable_event_queue_test.cpp
)

add_urma_test(urma_socket_test)
target_include_directories(urma_socket_test PRIVATE
        ${UBSOCKET_BASE_DIR}/csrc
        ${UBSOCKET_BASE_DIR}/csrc/core
        ${UBSOCKET_BASE_DIR}/csrc/common
        ${UBSOCKET_BASE_DIR}/csrc/under_api
        ${UBSOCKET_BASE_DIR}/csrc/under_api/urma
        ${UBSOCKET_BASE_DIR}/include
)
target_link_libraries(urma_socket_test PRIVATE
        ubsocket_static boundscheck mockcpp GTest::gtest_main pthread
)
target_sources(urma_socket_test PRIVATE
        urma_socket_test.cpp
)

add_urma_test(urma_acceptor_test)
target_include_directories(urma_acceptor_test PRIVATE
        ${UBSOCKET_BASE_DIR}/csrc
        ${UBSOCKET_BASE_DIR}/csrc/core
        ${UBSOCKET_BASE_DIR}/csrc/common
        ${UBSOCKET_BASE_DIR}/csrc/iobuf
        ${UBSOCKET_BASE_DIR}/csrc/under_api
        ${UBSOCKET_BASE_DIR}/csrc/under_api/urma
        ${UBSOCKET_BASE_DIR}/include
)
target_link_libraries(urma_acceptor_test PRIVATE
        ubsocket_static boundscheck mockcpp GTest::gtest_main pthread
)
target_sources(urma_acceptor_test PRIVATE
        urma_acceptor_test.cpp
)

add_urma_test(urma_connector_test)
target_include_directories(urma_connector_test PRIVATE
        ${UBSOCKET_BASE_DIR}/csrc
        ${UBSOCKET_BASE_DIR}/csrc/core
        ${UBSOCKET_BASE_DIR}/csrc/common
        ${UBSOCKET_BASE_DIR}/csrc/iobuf
        ${UBSOCKET_BASE_DIR}/csrc/under_api
        ${UBSOCKET_BASE_DIR}/csrc/under_api/urma
        ${UBSOCKET_BASE_DIR}/include
)
target_link_libraries(urma_connector_test PRIVATE
        ubsocket_static boundscheck mockcpp GTest::gtest_main pthread
)
target_sources(urma_connector_test PRIVATE
        urma_connector_test.cpp
)

add_urma_test(urma_exchange_test)
target_include_directories(urma_exchange_test PRIVATE
        ${UBSOCKET_BASE_DIR}/csrc
        ${UBSOCKET_BASE_DIR}/csrc/core
        ${UBSOCKET_BASE_DIR}/csrc/common
        ${UBSOCKET_BASE_DIR}/csrc/iobuf
        ${UBSOCKET_BASE_DIR}/csrc/under_api
        ${UBSOCKET_BASE_DIR}/csrc/under_api/urma
        ${UBSOCKET_BASE_DIR}/include
)
target_link_libraries(urma_exchange_test PRIVATE
        ubsocket_static boundscheck mockcpp GTest::gtest_main pthread
)
target_sources(urma_exchange_test PRIVATE
        urma_exchange_test.cpp
)

add_urma_test(urma_data_rx_test)
target_include_directories(urma_data_rx_test PRIVATE
        ${UBSOCKET_BASE_DIR}/csrc
        ${UBSOCKET_BASE_DIR}/csrc/core
        ${UBSOCKET_BASE_DIR}/csrc/common
        ${UBSOCKET_BASE_DIR}/csrc/iobuf
        ${UBSOCKET_BASE_DIR}/csrc/under_api
        ${UBSOCKET_BASE_DIR}/csrc/under_api/urma
        ${UBSOCKET_BASE_DIR}/include
)
target_link_libraries(urma_data_rx_test PRIVATE
        ubsocket_static boundscheck mockcpp GTest::gtest_main pthread
)
target_sources(urma_data_rx_test PRIVATE
        urma_data_rx_test.cpp
)

add_urma_test(urma_data_tx_test)
target_include_directories(urma_data_tx_test PRIVATE
        ${UBSOCKET_BASE_DIR}/csrc
        ${UBSOCKET_BASE_DIR}/csrc/core
        ${UBSOCKET_BASE_DIR}/csrc/common
        ${UBSOCKET_BASE_DIR}/csrc/iobuf
        ${UBSOCKET_BASE_DIR}/csrc/under_api
        ${UBSOCKET_BASE_DIR}/csrc/under_api/urma
        ${UBSOCKET_BASE_DIR}/include
)
target_link_libraries(urma_data_tx_test PRIVATE
        ubsocket_static boundscheck mockcpp GTest::gtest_main pthread
)
target_sources(urma_data_tx_test PRIVATE
        urma_data_tx_test.cpp
)

add_urma_test(urma_event_epoll_test)
target_include_directories(urma_event_epoll_test PRIVATE
        ${UBSOCKET_BASE_DIR}/csrc
        ${UBSOCKET_BASE_DIR}/csrc/core
        ${UBSOCKET_BASE_DIR}/csrc/common
        ${UBSOCKET_BASE_DIR}/csrc/iobuf
        ${UBSOCKET_BASE_DIR}/csrc/under_api
        ${UBSOCKET_BASE_DIR}/csrc/under_api/urma
        ${UBSOCKET_BASE_DIR}/include
)
target_link_libraries(urma_event_epoll_test PRIVATE
        ubsocket_static boundscheck mockcpp GTest::gtest_main pthread
)
target_sources(urma_event_epoll_test PRIVATE
        urma_event_epoll_test.cpp
)

# NOTE: urma_context_pool_test / urma_poller_agg_test 为硬件冒烟测试 —— 无 driver mock, 依赖真实 URMA
# 设备枚举/context 创建(poller_agg 用例还以 getchar() 阻塞等待), 在无驱动 CI/UT 环境必然空指针
# SegFault, 不满足 UT 单用例<=1s/禁止阻塞约束。故默认不纳入自动化 UT; 有驱动环境可临时放开。
# add_urma_test(urma_context_pool_test)
# target_include_directories(urma_context_pool_test PRIVATE
#         ${UBSOCKET_BASE_DIR}/csrc
#         ${UBSOCKET_BASE_DIR}/csrc/core
#         ${UBSOCKET_BASE_DIR}/csrc/common
#         ${UBSOCKET_BASE_DIR}/csrc/iobuf
#         ${UBSOCKET_BASE_DIR}/csrc/under_api
#         ${UBSOCKET_BASE_DIR}/csrc/under_api/urma
#         ${UBSOCKET_BASE_DIR}/include
# )
# target_link_libraries(urma_context_pool_test PRIVATE
#         ubsocket_static GTest::gtest_main pthread
# )
# target_sources(urma_context_pool_test PRIVATE
#         urma_context_pool_test.cpp
# )
#
# add_urma_test(urma_poller_agg_test)
# target_include_directories(urma_poller_agg_test PRIVATE
#         ${UBSOCKET_BASE_DIR}/csrc
#         ${UBSOCKET_BASE_DIR}/csrc/core
#         ${UBSOCKET_BASE_DIR}/csrc/common
#         ${UBSOCKET_BASE_DIR}/csrc/iobuf
#         ${UBSOCKET_BASE_DIR}/csrc/under_api
#         ${UBSOCKET_BASE_DIR}/csrc/under_api/urma
#         ${UBSOCKET_BASE_DIR}/include
# )
# target_link_libraries(urma_poller_agg_test PRIVATE
#         ubsocket_static GTest::gtest_main pthread
# )
# target_sources(urma_poller_agg_test PRIVATE
#         urma_poller_agg.cpp
# )

add_urma_test(urma_socket_buf_test)
target_include_directories(urma_socket_buf_test PRIVATE
        ${UBSOCKET_BASE_DIR}/csrc
        ${UBSOCKET_BASE_DIR}/csrc/common
)
target_link_libraries(urma_socket_buf_test PRIVATE
        ubsocket_static GTest::gtest_main pthread
)
target_sources(urma_socket_buf_test PRIVATE
        urma_socket_buf_test.cpp
)