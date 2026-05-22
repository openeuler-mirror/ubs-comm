#
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
# ubs-comm is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#      http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
#
###########################################################################
## target objects
# common objects
file(GLOB_RECURSE COMMON_SRCS ${UBSOCKET_CSRC_DIR}/common/*.cpp)
add_library(common_objects OBJECT ${COMMON_SRCS})
target_compile_options(common_objects PRIVATE -fPIC)

# entry objects
file(GLOB ENTRY_SRCS ${UBSOCKET_CSRC_DIR}/*.cpp)
add_library(entry_objects OBJECT ${ENTRY_SRCS})
target_compile_options(entry_objects PRIVATE -fPIC)

# core objects
file(GLOB CORE_SRCS ${UBSOCKET_CSRC_DIR}/core/*.cpp)

### umq source
file(GLOB UMQ_CORE_SRCS ${UBSOCKET_CSRC_DIR}/core/umq/*.cpp)
list(APPEND CORE_SRCS ${UMQ_CORE_SRCS})

### urma source
#file(GLOB URMA_CORE_SRCS ${UBSOCKET_CSRC_DIR}/core/urma/*.cpp)
#list(APPEND CORE_SRCS ${URMA_CORE_SRCS})

add_library(core_objects OBJECT ${CORE_SRCS})
target_compile_options(core_objects PRIVATE -fPIC)

# under_api objects
file(GLOB_RECURSE UNDER_API_SRCS ${UBSOCKET_CSRC_DIR}/under_api/*.cpp)
add_library(under_api_objects OBJECT ${UNDER_API_SRCS})
target_compile_options(under_api_objects PRIVATE -fPIC)

# iobuf objects
file(GLOB IOBUF_SRCS ${UBSOCKET_CSRC_DIR}/iobuf/*.cpp)
add_library(iobuf_objects OBJECT ${IOBUF_SRCS})
target_compile_options(iobuf_objects PRIVATE -fPIC)

file(GLOB_RECURSE PROFILING_SRCS ${UBSOCKET_CSRC_DIR}/profiling/*.cpp)
add_library(profiling_objects OBJECT ${PROFILING_SRCS})
target_compile_options(profiling_objects PRIVATE -fPIC)

###########################################################################
## libraries
# static library
add_library(ubsocket_static STATIC
        $<TARGET_OBJECTS:common_objects>
        $<TARGET_OBJECTS:entry_objects>
        $<TARGET_OBJECTS:core_objects>
        $<TARGET_OBJECTS:under_api_objects>
        $<TARGET_OBJECTS:iobuf_objects>
        $<TARGET_OBJECTS:profiling_objects>
)
set_target_properties(ubsocket_static PROPERTIES OUTPUT_NAME "ubsocket")

# shared library
add_library(ubsocket_shared SHARED
        $<TARGET_OBJECTS:common_objects>
        $<TARGET_OBJECTS:entry_objects>
        $<TARGET_OBJECTS:core_objects>
        $<TARGET_OBJECTS:under_api_objects>
        $<TARGET_OBJECTS:iobuf_objects>
        $<TARGET_OBJECTS:profiling_objects>
)
target_link_directories(ubsocket_shared PUBLIC)
set_target_properties(ubsocket_shared PROPERTIES OUTPUT_NAME "ubsocket")
target_link_libraries(ubsocket_shared PUBLIC pthread)

###########################################################################
## installation
set(TARGET_INSTALL_DIR ${UBSOCKET_INSTALL_DIR})

file(GLOB HEADER_FILES ${UBSOCKET_BASE_DIR}/include/*.h)

message(STATUS "ubsocket: install header files = ${HEADER_FILES}")

install(FILES ${HEADER_FILES} DESTINATION ${TARGET_INSTALL_DIR}/include/ubsocket)

install(TARGETS ubsocket_static
        LIBRARY DESTINATION ${TARGET_INSTALL_DIR}/lib64
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
)

install(TARGETS ubsocket_shared
        LIBRARY DESTINATION ${TARGET_INSTALL_DIR}/lib64
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
)