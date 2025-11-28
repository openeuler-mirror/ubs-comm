include_directories(${CMAKE_CURRENT_LIST_DIR}/../)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR} BRPC_ADPT_SRC)

add_library(rpc_adapter_brpc SHARED ${COMMON_RPC_ADPT_SRC} ${BRPC_ADPT_SRC})
set_property(TARGET rpc_adapter_brpc PROPERTY CMAKE_CXX_STANDARD 11)

target_link_libraries(rpc_adapter_brpc PUBLIC dl boundscheck PRIVATE ${UMQ_LIB})

install(TARGETS rpc_adapter_brpc DESTINATION /usr/lib64/)