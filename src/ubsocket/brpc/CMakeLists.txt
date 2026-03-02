# 设置库版本（用于 soname）
set(LIB_VERSION_MAJOR 0)
set(LIB_VERSION_MINOR 0)
set(LIB_VERSION_PATCH 1)
set(LIB_VERSION "${LIB_VERSION_MAJOR}.${LIB_VERSION_MINOR}.${LIB_VERSION_PATCH}")
set(SOVERSION "${LIB_VERSION_MAJOR}")
include_directories(
    ${CMAKE_CURRENT_LIST_DIR}/../
    ${CMAKE_CURRENT_LIST_DIR}/ubs_mem
)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR} BRPC_ADPT_SRC)

aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/ubs_mem UBS_MEM_SRC)


add_library(rpc_adapter_brpc
  SHARED
    brpc_context.cpp
    brpc_iobuf_adapter.cpp
    brpc_socket_adapter.cpp
    brpc_file_descriptor.cpp
    ${COMMON_RPC_ADPT_SRC} ${BRPC_ADPT_SRC} ${UBS_MEM_SRC})
set_property(TARGET rpc_adapter_brpc PROPERTY CMAKE_CXX_STANDARD 11)

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
