# 设置库版本（用于 soname）
set(LIB_VERSION_MAJOR 0)
set(LIB_VERSION_MINOR 0)
set(LIB_VERSION_PATCH 1)
set(LIB_VERSION "${LIB_VERSION_MAJOR}.${LIB_VERSION_MINOR}.${LIB_VERSION_PATCH}")
set(SOVERSION "${LIB_VERSION_MAJOR}")

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

set_target_properties(rpc_adapter_brpc PROPERTIES
    VERSION ${LIB_VERSION}
    SOVERSION ${SOVERSION}
)

install(TARGETS rpc_adapter_brpc DESTINATION /usr/lib64/)
