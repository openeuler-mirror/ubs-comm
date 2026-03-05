#
# Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
#
cmake_minimum_required(VERSION 3.14)
project(HCOM_UNIT_TEST C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

# define macro that will be used in source code
add_compile_options(-DMOCK_VERBS -DTEST_LLT)
message(STATUS "LIBBOUNDCHECK_SHARED_LIBRARY: ${LIBBOUNDCHECK_SHARED_LIBRARY}")
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
    message(STATUS "UB_BUILD_ENABLED true")
endif ()

if (BUILD_WITH_ALLOCATOR_PROTECTION)
    add_definitions(-DALLOCATOR_PROTECTION_ENABLED)
endif ()

set(HCOM_STUB_SRC_DIR ${CMAKE_SOURCE_DIR}/test/hcom/stub)
file(GLOB_RECURSE SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/src/hcom/*.cpp
    ${CMAKE_SOURCE_DIR}/src/hcom/*.h)

# remove jni file from hcom source files
file(GLOB_RECURSE HCOM_JNI_SRCS "${HCOM_SRC_DIR}/src/api/java_sdk/*.cpp"
		                        "${HCOM_SRC_DIR}/src/api/java_sdk/*.h")
list(REMOVE_ITEM  SOURCE_FILES  ${HCOM_JNI_SRCS})

# collect hcom unittest files
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SOURCE_FILES)
aux_source_directory(service_v2/api SOURCE_FILES)
aux_source_directory(service_v2 SOURCE_FILES)
aux_source_directory(capi_v2 SOURCE_FILES)
aux_source_directory(common SOURCE_FILES)
aux_source_directory(under_api SOURCE_FILES)
aux_source_directory(transport SOURCE_FILES)
aux_source_directory(transport/rdma SOURCE_FILES)
aux_source_directory(transport/rdma/verbs SOURCE_FILES)
aux_source_directory(transport/shm SOURCE_FILES)
aux_source_directory(transport/sock SOURCE_FILES)
aux_source_directory(transport/ub SOURCE_FILES)
aux_source_directory(transport/common SOURCE_FILES)
aux_source_directory(under_api/openssl SOURCE_FILES)
aux_source_directory(under_api/urma SOURCE_FILES)

if (BUILD_WITH_HTRACER)
aux_source_directory(common/trace SOURCE_FILES)
endif()

file(GLOB_RECURSE HCOM_SERVICE_SRCS
        "${HCOM_SRC_DIR}/src/hcom/service/*"
        "${HCOM_SRC_DIR}/src/hcom/api/capi/*")

file(GLOB_RECURSE HCOM_SERVICE_V2_SRCS
        "${HCOM_SRC_DIR}/src/hcom/service_v2/*"
        "${HCOM_SRC_DIR}/src/hcom/api/capi_v2/*")

# include&link gtest
set(GTEST_INSTALL_DIR "${TEST_TOOL_INSTALL_PATH}/googletest")
if (NOT EXISTS ${GTEST_INSTALL_DIR})
    message(ERROR "GTEST_INSTALL_DIR(${GTEST_INSTALL_DIR}) is invalid")
endif()

set(HCOM_UT hcom_ut)

link_directories(${GTEST_INSTALL_DIR}/lib64)

message("TEST_TOOL_INSTALL_PATH: ${TEST_TOOL_INSTALL_PATH}")
# include&link mockcpp
set(MOCKCPP_INSTALL_DIR "${TEST_TOOL_INSTALL_PATH}/mockcpp")
if (NOT EXISTS ${MOCKCPP_INSTALL_DIR})
    message(ERROR "MOCKCPP_INSTALL_DIR(${MOCKCPP_INSTALL_DIR}) is invalid")
endif()
link_directories(${MOCKCPP_INSTALL_DIR}/lib)
link_directories(${HCOM_OUTPUT_PATH}/hcom_3rdparty/libboundscheck/lib/)
add_executable(${HCOM_UT} ${SOURCE_FILES})

target_include_directories(${HCOM_UT} PRIVATE ${GTEST_INSTALL_DIR}/include)
target_include_directories(${HCOM_UT} PRIVATE ${MOCKCPP_INSTALL_DIR}/include)
# collect hcom source files
target_include_directories(${HCOM_UT} PRIVATE ${HCOM_UT} PRIVATE ${CMAKE_SOURCE_DIR}/src/)
target_include_directories(${HCOM_UT} PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/)
target_include_directories(${HCOM_UT} PRIVATE ${CMAKE_SOURCE_DIR}/src/api/capi)
target_include_directories(${HCOM_UT} PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/under_api/verbs)
target_include_directories(${HCOM_UT} PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/under_api/openssl)
target_include_directories(${HCOM_UT} PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/under_api/urma)
target_include_directories(${HCOM_UT} PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/service_v2)
target_include_directories(${HCOM_UT} PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/service_v2/api)
target_include_directories(${HCOM_UT} PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/transport)
target_include_directories(${HCOM_UT} PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/transport/rdma)
target_include_directories(${HCOM_UT} PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/transport/rdma/verbs)
target_include_directories(${HCOM_UT} PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/transport/sock)
target_include_directories(${HCOM_UT} PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/transport/shm)
target_include_directories(${HCOM_UT} PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/transport/ub)
target_include_directories(${HCOM_UT} PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/common)
target_include_directories(${HCOM_UT} PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/api/capi_v2)
target_include_directories(${HCOM_UT} PRIVATE ${CMAKE_SOURCE_DIR}/src/hcom/common/trace)
target_include_directories(${HCOM_UT} PRIVATE ${HCOM_OUTPUT_PATH}/hcom_3rdparty/umdk/urma/include)
target_include_directories(${HCOM_UT} PRIVATE ${HCOM_OUTPUT_PATH}/hcom_3rdparty/libboundscheck/include)
target_include_directories(${HCOM_UT} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(${HCOM_UT} PRIVATE ${HCOM_STUB_SRC_DIR})
target_include_directories(${HCOM_UT} PRIVATE ${CMAKE_SOURCE_DIR}/src/api/capi_v2/)
# enable gcov
target_compile_options(${HCOM_UT} PUBLIC --coverage)
target_link_options(${HCOM_UT} PUBLIC --coverage)

# Ignore access control in class(like private or protected)
add_compile_options(-fno-access-control)

# enable asan
# add_compile_options(-fsanitize=address)
# add_link_options(-fsanitize=address)

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
target_compile_options(${HCOM_UT} PUBLIC -D_GNU_SOURCE)
set_target_properties(${HCOM_UT} PROPERTIES CLEAN_DIRECT_OUTPUT 1)
set_target_properties(${HCOM_UT} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(${HCOM_UT} ${DEPEND_LIBS})
