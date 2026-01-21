
if(NOT DEFINED CPU_TYPE)
    set(CPU_TYPE "arm_64")
endif()
message(STATUS "CPU_TYPE: ${CPU_TYPE}")

option(BUILD_JAVA_SDK "Build java sdk" OFF)
option(BUILD_WITH_HW_CRC "Build with hardware crc" OFF)
option(BUILD_WITH_SERVICE "Build with SERVICE" ON)
option(BUILD_WITH_RDMA "Build with RDMA" ON)
option(BUILD_WITH_UB "Build with UB" OFF)
option(BUILD_WITH_SHM "Build with SHM" ON)
option(BUILD_WITH_SOCK "Build with SOCK" ON)
option(BUILD_WITH_ALLOCATOR_PROTECTION "Build with allocator protection" OFF)
option(BUILD_WITH_HTRACER "Build with htracer" ON)
option(ENABLE_ARM_KP "enable arm kunpeng" OFF)
option(BUILD_WITH_MULTICAST "Build with multicast" OFF)

if (ENABLE_ARM_KP)
    add_definitions(-DENABLE_ARM_KP)
endif ()

# Default to ld.gold if available.
option(USE_GOLD_LINKER "Use ld.gold to link" ON)
if(USE_GOLD_LINKER)
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -fuse-ld=gold -Wl,--version
        ERROR_QUIET
        OUTPUT_VARIABLE LD_GOLD_VERSION
    )
    if("${LD_GOLD_VERSION}" MATCHES "GNU gold")
        message(STATUS "ld.gold is available, using it to link")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=gold")
    else()
        message(WARNING "USE_GOLD_LINKER is set but ld.gold is not available, fallback to ld.bfd")
        set(USE_GOLD_LINKER OFF)
    endif()
endif()

# Obtain commit information from Git.
if(NOT HCOM_COMMIT_ID)
  execute_process(COMMAND git rev-parse HEAD
    OUTPUT_VARIABLE HCOM_COMMIT_ID
    OUTPUT_STRIP_TRAILING_WHITESPACE
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

  if(NOT HCOM_COMMIT_ID)
    set(HCOM_COMMIT_ID "<unknown>")
  endif()
endif()

add_definitions(-DHCOM_COMMIT_ID="${HCOM_COMMIT_ID}")
message(STATUS "Build hcom on commit ${HCOM_COMMIT_ID}")

if (BUILD_WITH_RDMA)
    add_definitions(-DRDMA_BUILD_ENABLED)
endif ()

if (BUILD_WITH_SHM)
    add_definitions(-DSHM_BUILD_ENABLED)
endif ()

if (BUILD_WITH_SOCK)
    add_definitions(-DSOCK_BUILD_ENABLED)
endif ()

if (BUILD_WITH_UB)
    add_definitions(-DUB_BUILD_ENABLED)
endif ()

if (BUILD_WITH_ALLOCATOR_PROTECTION)
    add_definitions(-DALLOCATOR_PROTECTION_ENABLED)
endif ()

if (NOT HCOM_COMPONENT_VERSION)
    add_definitions(-DHCOM_COMPONENT_VERSION="1.0.0")
else ()
    add_definitions(-DHCOM_COMPONENT_VERSION="${HCOM_COMPONENT_VERSION}")
endif ()

if (${CMAKE_BUILD_TYPE} MATCHES "release")
    set(CXX_FLAGS
        -g
        -pipe
        -O3
        -Wall
        -fms-extensions
        -Wno-unused-parameter
        -Wno-unused-function
        -w -Wno-address-of-packed-member
        -Wunused-variable
        -Wunused-value
        -Wcast-align
        -Wcast-qual
        -Winvalid-pch
        -Wwrite-strings
        -Wsign-compare
        -Wfloat-equal
        -Wextra
        -std=c++11
        -fPIC
        -fstack-protector-strong
        -Wl,-z,relro,-z,now,-z,noexecstack
    )
else ()
    set(CXX_FLAGS
        -pipe
        -g
        -rdynamic
        -Wall
        -fms-extensions
        -Wno-unused-parameter
        -Wno-unused-function
        -w -Wno-address-of-packed-member
        -Wunused-variable
        -Wunused-value
        -Winvalid-pch
        -Wcast-align
        -Wcast-qual
        -Wwrite-strings
        -Wsign-compare
        -Wfloat-equal
        -Wextra
        -std=c++11
        #-fsanitize=address
        #-fno-omit-frame-pointer
        -fstack-protector-strong
        #-fPIC will cause failure while mocking system functions
    )
    add_definitions(-DDEBUG)
    if (NOT BUILD_TESTS)
        set(CXX_FLAGS "${CXX_FLAGS} -fPIC")
    endif ()
endif()

string(REPLACE ";" " " CMAKE_CXX_FLAGS "${CXX_FLAGS}")

#set(CMAKE_VERBOSE_MAKEFILE on)

# enable asan
# add_compile_options(-fsanitize=address)
# add_link_options(-fsanitize=address)

add_definitions(-DUSE_PROCESS_MONOTONIC)

if(BUILD_WITH_HW_CRC)
    add_definitions(-DUSE_HARDWARE_CRC)
    if (${CPU_TYPE} MATCHES "arm_64")
        add_definitions(-march=armv8-a+crc)
    endif()
endif()
message(STATUS "BUILD_WITH_HW_CRC: ${BUILD_WITH_HW_CRC}")

#enable object statistics
add_definitions(-DENABLE_OBJ_GLOBAL_STATISTICS)

set(CMAKE_C_STANDARD 11)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2")

# add header path and add source files
set(HCOM_SRC_DIR ${PROJECT_SOURCE_DIR})
set(HCOM_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/dist)
set(HCOM_BUILD_PATH ${PROJECT_SOURCE_DIR}/build)
set(HCOM_SRC_INCLUDE_PATH
    ${HCOM_SRC_DIR}/src/hcom/under_api/verbs
    ${HCOM_SRC_DIR}/src/hcom/under_api/urma
    ${HCOM_SRC_DIR}/src/hcom/under_api/openssl
    ${HCOM_SRC_DIR}/src/hcom
    ${HCOM_SRC_DIR}/src/hcom/api
    ${HCOM_SRC_DIR}/src/hcom/api/capi_v2
    ${HCOM_SRC_DIR}/src/hcom/common
    ${HCOM_SRC_DIR}/src/hcom/service_v2
    ${HCOM_SRC_DIR}/src/hcom/service_v2/api
    ${HCOM_SRC_DIR}/src/hcom/transport
    ${HCOM_SRC_DIR}/src/hcom/transport/rdma
    ${HCOM_SRC_DIR}/src/hcom/transport/rdma/verbs
    ${HCOM_SRC_DIR}/src/hcom/transport/ub
    ${HCOM_SRC_DIR}/src/hcom/transport/shm
    ${HCOM_SRC_DIR}/src/hcom/transport/sock
    ${HCOM_SRC_DIR}/src/hcom/multicast
    ${HCOM_SRC_DIR}/src/hcom/multicast/include
)

file(GLOB_RECURSE HCOM *.cpp *.h)
file(GLOB_RECURSE HCOM_JNI_SRCS "${HCOM_SRC_DIR}/src/hcom/api/java_sdk/*.cpp"
                                "${HCOM_SRC_DIR}/src/hcom/api/java_sdk/*.h")
list(REMOVE_ITEM  HCOM  ${HCOM_JNI_SRCS})

file(GLOB HCOM_RDMA_COMMON "${HCOM_SRC_DIR}/src/hcom/transport/rdma/*")
file(GLOB_RECURSE HCOM_RDMA_SRCS
    "${HCOM_SRC_DIR}/src/hcom/transport/rdma/verbs/*"
    "${HCOM_SRC_DIR}/src/hcom/under_api/verbs/verbs*"
)
file(GLOB_RECURSE HCOM_SHM_SRCS "${HCOM_SRC_DIR}/src/hcom/transport/shm/*")
file(GLOB_RECURSE HCOM_SOCK_SRCS "${HCOM_SRC_DIR}/src/hcom/transport/sock/*")
file(GLOB_RECURSE HCOM_UB_SRCS 
    "${HCOM_SRC_DIR}/src/hcom/transport/ub/*"
    "${HCOM_SRC_DIR}/src/hcom/under_api/urma/*")
file(GLOB_RECURSE HCOM_MULTICAST_SRCS "${HCOM_SRC_DIR}/src/hcom/multicast/*")

if(NOT BUILD_WITH_RDMA)
    list(REMOVE_ITEM HCOM ${HCOM_RDMA_COMMON})
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

if (NOT BUILD_WITH_MULTICAST)
    list(REMOVE_ITEM HCOM ${HCOM_MULTICAST_SRCS})
endif ()

# add debuginfo file for dynamic library
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
add_subdirectory(3rdparty)

target_include_directories(hcom PUBLIC ${HCOM_SRC_INCLUDE_PATH})
target_include_directories(hcom_static_obj PUBLIC ${HCOM_SRC_INCLUDE_PATH})

#set which header files wil be installed
file(GLOB INCLUDE_HEADERS hcom*.h
    ${HCOM_SRC_DIR}/src/hcom/service_v2/api/hcom_service_channel.h
    ${HCOM_SRC_DIR}/src/hcom/service_v2/api/hcom_service_def.h
    ${HCOM_SRC_DIR}/src/hcom/service_v2/api/hcom_service_context.h
    ${HCOM_SRC_DIR}/src/hcom/service_v2/api/hcom_service.h
)

file(GLOB C_INCLUDE_HEADERS api/capi_v2/hcom_c.h
    api/capi_v2/hcom_service_c.h
    api/capi/hcom_cgo_c.h)

set(TARGET_INSTALL_INCLUDE ${CMAKE_INSTALL_PREFIX}/include/hcom)
set(TARGET_INSTALL_LIB ${CMAKE_INSTALL_PREFIX}/lib)

if (${BUILD_WITH_MULTICAST})
    file(GLOB MULTICAST_INCLUDE_HEADERS multicast/include/multicast*.h)
endif ()

install(FILES ${INCLUDE_HEADERS} DESTINATION ${TARGET_INSTALL_INCLUDE} PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ)
install(FILES ${MULTICAST_INCLUDE_HEADERS} DESTINATION ${TARGET_INSTALL_INCLUDE}/multicast PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ)
install(FILES ${C_INCLUDE_HEADERS} DESTINATION ${TARGET_INSTALL_INCLUDE}/capi PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ)
install(FILES ${CMAKE_BINARY_DIR}/src/hcom/libhcom_static.a DESTINATION ${TARGET_INSTALL_LIB}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
install(TARGETS hcom DESTINATION ${TARGET_INSTALL_LIB}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
install(FILES ${CMAKE_BINARY_DIR}/src/hcom/libhcom.so.debug DESTINATION ${TARGET_INSTALL_LIB}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

#set wether java sdk will be built.(not implemented yet)
if(${BUILD_JAVA_SDK} MATCHES "ON")
    add_subdirectory(api/java_sdk)
endif()
message(STATUS "BUILD_JAVA_SDK: ${BUILD_JAVA_SDK}")
