add_library(rpc_adapter_brpc
  SHARED
    brpc_context.cpp
    brpc_iobuf_adapter.cpp
    brpc_socket_adapter.cpp
)

target_compile_options(rpc_adapter_brpc
  PRIVATE
    -Werror
)

target_include_directories(rpc_adapter_brpc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rpc_adapter_brpc PUBLIC ubsocket)

install(TARGETS rpc_adapter_brpc DESTINATION /usr/lib64/)
