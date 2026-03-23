#
# Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
#
cmake_minimum_required(VERSION 3.14)
project(HCOM_LLT C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

# define macro that will be used in source code
add_compile_options(-DMOCK_VERBS -DTEST_LLT)

option(BUILD_WITH_HW_CRC "Build with hardware crc" OFF)
option(BUILD_WITH_SERVICE "Build with SERVICE" ON)
option(BUILD_WITH_RDMA "Build with RDMA" ON)
option(BUILD_WITH_UB "Build with UB" ON)
option(BUILD_WITH_SHM "Build with SHM" ON)
option(BUILD_WITH_SOCK "Build with SOCK" ON)
option(BUILD_WITH_ALLOCATOR_PROTECTION "Build with allocator protection" OFF)
option(BUILD_WITH_HTRACER "Build with htracer" ON)
option(ENABLE_ARM_KP "enable arm kunpeng" OFF)

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
file(GLOB_RECURSE SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/src/hcom/*.cpp
    ${CMAKE_SOURCE_DIR}/src/hcom/*.h)
set(HCOM_STUB_SRC_DIR ${CMAKE_SOURCE_DIR}/test/hcom/stub)
# collect hcom unittest files
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/testcase)
include_directories(${HCOM_STUB_SRC_DIR})
aux_source_directory(main/ SOURCE_FILES)
aux_source_directory(testcase/ SOURCE_FILES)
aux_source_directory(testcase/common SOURCE_FILES)
aux_source_directory(testcase/transport SOURCE_FILES)
aux_source_directory(testcase/transport/rdma SOURCE_FILES)
#aux_source_directory(testcase/transport/shm SOURCE_FILES)
aux_source_directory(testcase/transport/sock SOURCE_FILES)
link_directories(${HCOM_OUTPUT_PATH}/hcom_3rdparty/libboundscheck/lib/)

# include&link gtest
set(GTEST_INSTALL_DIR "${TEST_TOOL_INSTALL_PATH}/googletest")
if (NOT EXISTS ${GTEST_INSTALL_DIR})
    message(ERROR "GTEST_INSTALL_DIR(${GTEST_INSTALL_DIR}) is invalid")
endif()
include_directories(${GTEST_INSTALL_DIR}/include)
link_directories(${GTEST_INSTALL_DIR}/lib64)

# include&link mockcpp
set(MOCKCPP_INSTALL_DIR "${TEST_TOOL_INSTALL_PATH}/mockcpp")
if (NOT EXISTS ${MOCKCPP_INSTALL_DIR})
    message(ERROR "MOCKCPP_INSTALL_DIR(${MOCKCPP_INSTALL_DIR}) is invalid")
endif()
include_directories(${MOCKCPP_INSTALL_DIR}/include)
link_directories(${MOCKCPP_INSTALL_DIR}/lib)
link_directories(${HCOM_OUTPUT_PATH}/hcom_3rdparty/libboundscheck/lib/)

add_executable(Hcomtest ${SOURCE_FILES})

target_include_directories(Hcomtest PRIVATE ${GTEST_INSTALL_DIR}/include)
target_include_directories(Hcomtest PRIVATE ${MOCKCPP_INSTALL_DIR}/include)
# collect hcom source files
target_include_directories(Hcomtest PRIVATE Hcomtest PRIVATE ${CMAKE_SOURCE_DIR}/src/)
target_include_directories(Hcomtest PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/)
target_include_directories(Hcomtest PRIVATE ${CMAKE_SOURCE_DIR}/src/api/capi)
target_include_directories(Hcomtest PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/under_api/verbs)
target_include_directories(Hcomtest PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/under_api/openssl)
target_include_directories(Hcomtest PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/under_api/urma)
target_include_directories(Hcomtest PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/service_v2)
target_include_directories(Hcomtest PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/service_v2/api)
target_include_directories(Hcomtest PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/transport)
target_include_directories(Hcomtest PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/transport/rdma)
target_include_directories(Hcomtest PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/transport/rdma/verbs)
target_include_directories(Hcomtest PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/transport/sock)
target_include_directories(Hcomtest PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/transport/shm)
target_include_directories(Hcomtest PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/transport/ub)
target_include_directories(Hcomtest PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/common)
target_include_directories(Hcomtest PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/api/capi_v2)
target_include_directories(Hcomtest PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/common/trace)
target_include_directories(Hcomtest PRIVATE ${HCOM_OUTPUT_PATH}/hcom_3rdparty/umdk/urma/include)
target_include_directories(Hcomtest PRIVATE ${HCOM_OUTPUT_PATH}/hcom_3rdparty/libboundscheck/include)
target_include_directories(Hcomtest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(Hcomtest PRIVATE ${HCOM_STUB_SRC_DIR})
target_include_directories(Hcomtest PRIVATE ${CMAKE_SOURCE_DIR}/src/api/capi_v2/)
# enable gcov
target_compile_options(Hcomtest PUBLIC --coverage)
target_link_options(Hcomtest PUBLIC --coverage)

# build hcom_ut
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
string(REPLACE ";" " " CMAKE_CXX_FLAGS "${CXX_FLAGS}")
set(DEPEND_LIBS rt pthread gtest gcov mockcpp dl fake_ibv_static boundscheck)
target_compile_options(Hcomtest PUBLIC -D_GNU_SOURCE)
set_target_properties(Hcomtest PROPERTIES OUTPUT_NAME "hcom_test")
set_target_properties(Hcomtest PROPERTIES CLEAN_DIRECT_OUTPUT 1)
set_target_properties(Hcomtest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(Hcomtest ${DEPEND_LIBS} rt)
