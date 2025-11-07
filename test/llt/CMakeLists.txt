#
# Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
#
cmake_minimum_required(VERSION 3.14)
project(HCOM_LLT C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

# define macro that will be used in source code
add_compile_options(-DMOCK_VERBS -DTEST_LLT)

# collect hcom source files
include_directories(${CMAKE_SOURCE_DIR}/src/)
aux_source_directory(service_v2/api SOURCE_FILES)
aux_source_directory(service_v2 SOURCE_FILES)
include_directories(${CMAKE_SOURCE_DIR}/src/api/java_sdk/jni)
include_directories(${CMAKE_SOURCE_DIR}/src/api/java_sdk/jni/include)
include_directories(${CMAKE_SOURCE_DIR}/src/api/java_sdk/jni/service)
include_directories(${CMAKE_SOURCE_DIR}/src/api/java_sdk/jni/common)
include_directories(${CMAKE_SOURCE_DIR}/src/under_api/verbs)
file(GLOB_RECURSE SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.h)

# collect hcom unittest files
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/testcase)
aux_source_directory(main/ SOURCE_FILES)
aux_source_directory(testcase/ SOURCE_FILES)
aux_source_directory(testcase/common SOURCE_FILES)
aux_source_directory(testcase/transport SOURCE_FILES)
aux_source_directory(testcase/transport/rdma SOURCE_FILES)
#aux_source_directory(testcase/transport/shm SOURCE_FILES)
aux_source_directory(testcase/transport/sock SOURCE_FILES)

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

# build hcom_ut
set(DEPEND_LIBS rt pthread gtest gcov mockcpp dl fake_ibv_static boundscheck)
add_executable(Hcomtest ${SOURCE_FILES})
target_compile_options(Hcomtest PUBLIC -D_GNU_SOURCE)
set_target_properties(Hcomtest PROPERTIES OUTPUT_NAME "hcom_test")
set_target_properties(Hcomtest PROPERTIES CLEAN_DIRECT_OUTPUT 1)
set_target_properties(Hcomtest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(Hcomtest ${DEPEND_LIBS} rt)
