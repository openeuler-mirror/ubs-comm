
include_directories(${CMAKE_SOURCE_DIR}/src/ubsocket ${CMAKE_SOURCE_DIR}/src/ubsocket/csrc)

add_executable(test_ubsocket_atomic_fd_set core/test_ubsocket_atomic_fd_set.cpp)
target_compile_features(test_ubsocket_atomic_fd_set PRIVATE cxx_std_17)
target_link_libraries(test_ubsocket_atomic_fd_set GTest::GTest GTest::Main)
target_include_directories(test_ubsocket_atomic_fd_set PRIVATE ${CMAKE_SOURCE_DIR}/src/ubsocket/csrc/core)
gtest_discover_tests(test_ubsocket_atomic_fd_set)

add_executable(test_urma_socket_buf
    core/test_urma_socket_buf.cpp
    ${CMAKE_SOURCE_DIR}/src/ubsocket/csrc/core/urma/urma_socket_buf.cpp)
target_compile_features(test_urma_socket_buf PRIVATE cxx_std_17)
target_link_libraries(test_urma_socket_buf GTest::GTest GTest::Main pthread)
target_include_directories(test_urma_socket_buf PRIVATE ${CMAKE_SOURCE_DIR}/src/ubsocket/csrc/core/urma)
gtest_discover_tests(test_urma_socket_buf)

add_executable(test_urma_backend
    core/test_urma_backend.cpp)
target_compile_features(test_urma_backend PRIVATE cxx_std_17)
target_link_libraries(test_urma_backend GTest::GTest GTest::Main ubsocket_static pthread)
target_include_directories(test_urma_backend
    PRIVATE ${CMAKE_SOURCE_DIR}/src/ubsocket/csrc/core/urma
    PRIVATE ${CMAKE_SOURCE_DIR}/src/ubsocket/csrc
    PRIVATE ${CMAKE_SOURCE_DIR}/src/ubsocket/csrc/core
    PRIVATE ${CMAKE_SOURCE_DIR}/src/ubsocket/csrc/common
    PRIVATE ${CMAKE_SOURCE_DIR}/src/ubsocket/csrc/iobuf
    PRIVATE ${CMAKE_SOURCE_DIR}/src/ubsocket/csrc/under_api
    PRIVATE ${CMAKE_SOURCE_DIR}/src/ubsocket/csrc/under_api/urma
    PRIVATE ${CMAKE_SOURCE_DIR}/src/ubsocket/include
    PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom)
target_compile_options(test_urma_backend PRIVATE -fno-access-control)
gtest_discover_tests(test_urma_backend)
