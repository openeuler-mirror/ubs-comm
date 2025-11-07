file(GLOB_RECURSE HCOM *.cpp *.h)
file(GLOB_RECURSE HCOM_JNI_SRCS "${HCOM_SRC_DIR}/src/api/java_sdk/*.cpp"
                                "${HCOM_SRC_DIR}/src/api/java_sdk/*.h")
list(REMOVE_ITEM  HCOM  ${HCOM_JNI_SRCS})

file(GLOB_RECURSE HCOM_SERVICE_SRCS
        "${HCOM_SRC_DIR}/src/service/*"
        "${HCOM_SRC_DIR}/src/api/capi/*")
file(GLOB_RECURSE HCOM_SERVICE_V2_SRCS
        "${HCOM_SRC_DIR}/src/service_v2/*"
        "${HCOM_SRC_DIR}/src/api/capi_v2/*")

file(GLOB HCOM_RDMA_COMMON "${HCOM_SRC_DIR}/src/transport/rdma/*")
file(GLOB_RECURSE HCOM_RDMA_SRCS
        "${HCOM_SRC_DIR}/src/transport/rdma/verbs/*"
        "${HCOM_SRC_DIR}/src/under_api/verbs/verbs*")
file(GLOB_RECURSE HCOM_SHM_SRCS "${HCOM_SRC_DIR}/src/transport/shm/*")
file(GLOB_RECURSE HCOM_SOCK_SRCS "${HCOM_SRC_DIR}/src/transport/sock/*")
file(GLOB_RECURSE HCOM_UB_SRCS "${HCOM_SRC_DIR}/src/transport/ub/*")

if(NOT BUILD_WITH_RDMA)
    list(REMOVE_ITEM HCOM ${HCOM_RDMA_COMMON})
endif()

if(NOT BUILD_WITH_RDMA)
    list(REMOVE_ITEM HCOM ${HCOM_RDMA_SRCS})
endif()

if(NOT BUILD_WITH_SHM)
    list(REMOVE_ITEM HCOM ${HCOM_SHM_SRCS})
endif()

if(NOT BUILD_WITH_SOCK)
    list(REMOVE_ITEM HCOM ${HCOM_SOCK_SRCS})
endif()

if(NOT BUILD_WITH_UB)
    list(REMOVE_ITEM HCOM ${HCOM_UB_SRCS})
endif ()


function(split_debug_symbols target)
    # 定义符号文件路径（与二进制同目录，添加.debug后缀）
    set(debug_file "$<TARGET_FILE:${target}>.debug")

    add_custom_command(TARGET ${target} POST_BUILD
            # 1. 提取完整调试符号到单独文件
            COMMAND ${CMAKE_OBJCOPY} --only-keep-debug "$<TARGET_FILE:${target}>" "${debug_file}"
            # 2. 从主二进制中剥离调试符号但保留最小关联信息
            COMMAND ${CMAKE_OBJCOPY} --strip-debug --strip-unneeded "$<TARGET_FILE:${target}>"
            # 为可执行文件添加符号表文件的链接
            COMMAND ${CMAKE_OBJCOPY} --add-gnu-debuglink=${debug_file} "$<TARGET_FILE:${target}>"
            COMMENT "Splitting debug symbols for ${target} into ${debug_file}"
    )
endfunction()

add_library(hcom_static_obj STATIC ${HCOM})
target_link_libraries(hcom_static_obj
        -Wl,--start-group
        pthread dl rt boundscheck
        -Wl,--end-group)

add_custom_command(
        OUTPUT libhcom_static.a
        DEPENDS hcom_static_obj
        COMMAND mkdir -p tmp_obj && cd tmp_obj
        && ar x $<TARGET_FILE:hcom_static_obj>
        && cd ..
        && ar cur libhcom_static.a tmp_obj/*.o
        && rm -rf tmp_obj
)
add_custom_target(hcom_static ALL DEPENDS libhcom_static.a)

add_library(hcom SHARED ${HCOM})
target_compile_options(hcom PUBLIC -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now)
target_link_libraries(hcom
        -Wl,--start-group
        pthread dl rt boundscheck
        -Wl,--end-group)

split_debug_symbols(hcom)
file(GLOB INCLUDE_HEADERS hcom*.h)

file(GLOB C_INCLUDE_HEADERS api/capi_v2/hcom_c.h
    api/capi_v2/hcom_service_c.h
    api/capi/hcom_cgo_c.h)

list(APPEND INCLUDE_HEADERS ${HCOM_SRC_DIR}/src/service_v2/api/hcom_service_channel.h)
list(APPEND INCLUDE_HEADERS ${HCOM_SRC_DIR}/src/service_v2/api/hcom_service_def.h)
list(APPEND INCLUDE_HEADERS ${HCOM_SRC_DIR}/src/service_v2/api/hcom_service_context.h)
list(APPEND INCLUDE_HEADERS ${HCOM_SRC_DIR}/src/service_v2/api/hcom_service.h)

set(TARGET_INSTALL_INCLUDE ${CMAKE_INSTALL_PREFIX}/include/hcom)
set(TARGET_INSTALL_LIB ${CMAKE_INSTALL_PREFIX}/lib)

install(FILES ${INCLUDE_HEADERS} DESTINATION ${TARGET_INSTALL_INCLUDE} PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ)
install(FILES ${C_INCLUDE_HEADERS} DESTINATION ${TARGET_INSTALL_INCLUDE}/capi PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ)
install(FILES ${CMAKE_BINARY_DIR}/src/libhcom_static.a DESTINATION ${TARGET_INSTALL_LIB}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
install(TARGETS hcom DESTINATION ${TARGET_INSTALL_LIB}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
install(FILES ${CMAKE_BINARY_DIR}/src/libhcom.so.debug DESTINATION ${TARGET_INSTALL_LIB}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

if(${BUILD_JAVA_SDK} MATCHES "ON")
    add_subdirectory(api/java_sdk)
endif()
message(STATUS "BUILD_JAVA_SDK: ${BUILD_JAVA_SDK}")
