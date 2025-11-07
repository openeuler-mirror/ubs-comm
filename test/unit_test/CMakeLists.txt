#
# Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
#
cmake_minimum_required(VERSION 3.14)
project(HCOM_UNIT_TEST C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

# define macro that will be used in source code
add_compile_options(-DMOCK_VERBS -DTEST_LLT)

# collect hcom source files
include_directories(${CMAKE_SOURCE_DIR}/src/)
include_directories(${CMAKE_SOURCE_DIR}/src/api/capi)
include_directories(${CMAKE_SOURCE_DIR}/src/api/java_sdk/jni)
include_directories(${CMAKE_SOURCE_DIR}/src/api/java_sdk/jni/include)
include_directories(${CMAKE_SOURCE_DIR}/src/api/java_sdk/jni/service)
include_directories(${CMAKE_SOURCE_DIR}/src/api/java_sdk/jni/common)
include_directories(${CMAKE_SOURCE_DIR}/src/under_api/verbs)
include_directories(${CMAKE_SOURCE_DIR}/src/service_v2/api/)
include_directories(${CMAKE_SOURCE_DIR}/src/service_v2/)
include_directories(${CMAKE_SOURCE_DIR}/src/api/capi_v2/)
include_directories(${CMAKE_SOURCE_DIR}/src/common/trace)


file(GLOB_RECURSE SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.h)

# remove jni file from hcom source files
file(GLOB_RECURSE HCOM_JNI_SRCS "${HCOM_SRC_DIR}/src/api/java_sdk/*.cpp"
		                        "${HCOM_SRC_DIR}/src/api/java_sdk/*.h")
list(REMOVE_ITEM  SOURCE_FILES  ${HCOM_JNI_SRCS})

# collect hcom unittest files
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SOURCE_FILES)
aux_source_directory(service_v2/api SOURCE_FILES)
aux_source_directory(service_v2 SOURCE_FILES)
aux_source_directory(capi_v2 SOURCE_FILES)
include_directories(${CMAKE_SOURCE_DIR}/src/api/capi_v2/)
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
        "${HCOM_SRC_DIR}/src/service/*"
        "${HCOM_SRC_DIR}/src/api/capi/*")

file(GLOB_RECURSE HCOM_SERVICE_V2_SRCS
        "${HCOM_SRC_DIR}/src/service_v2/*"
        "${HCOM_SRC_DIR}/src/api/capi_v2/*")

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

# enable gcov
add_compile_options(-ftest-coverage -fprofile-arcs)

# Ignore access control in class(like private or protected)
add_compile_options(-fno-access-control)

# enable asan
# add_compile_options(-fsanitize=address)
# add_link_options(-fsanitize=address)

# build hcom_ut
set(DEPEND_LIBS rt pthread gtest gcov mockcpp dl boundscheck fake_ibv_static)
set(HCOM_UT hcom_ut)
add_executable(${HCOM_UT} ${SOURCE_FILES})
target_compile_options(${HCOM_UT} PUBLIC -D_GNU_SOURCE)
set_target_properties(${HCOM_UT} PROPERTIES CLEAN_DIRECT_OUTPUT 1)
set_target_properties(${HCOM_UT} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(${HCOM_UT} ${DEPEND_LIBS})
