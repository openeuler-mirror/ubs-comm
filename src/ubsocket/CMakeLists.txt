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
option(BUILD_UMQ_ADAPTER_BACKEND "build umq adapter backend" ON)
option(BUILD_UMQ_DLOPEN_BACKEND "build umq dlopen backend" OFF)
option(BUILD_URMA_DLOPEN_BACKEND "build urma dlopen backend" OFF)
option(ENABLE_OBJECT_COUNTING "enable object couting to detect memory leak" OFF)
option(UBSOCKET_ENABLE_COVERAGE "Enable coverage reporting for ubsocket UT" OFF)
option(UBSOCKET_BUILD_CLI "Enable CLI component build" OFF)

if (BUILD_UMQ_ADAPTER_BACKEND AND BUILD_UMQ_DLOPEN_BACKEND)
    message(FATAL_ERROR "BUILD_UMQ_ADAPTER_BACKEND and BUILD_UMQ_DLOPEN_BACKEND cannot both be ON")
endif ()

message(STATUS "ubsocket: BUILD_UMQ_ADAPTER_BACKEND = ${BUILD_UMQ_ADAPTER_BACKEND}")
message(STATUS "ubsocket: BUILD_UMQ_DLOPEN_BACKEND = ${BUILD_UMQ_DLOPEN_BACKEND}")
message(STATUS "ubsocket: BUILD_URMA_DLOPEN_BACKEND = ${BUILD_URMA_DLOPEN_BACKEND}")
message(STATUS "ubsocket: ENABLE_OBJECT_COUNTING = ${ENABLE_OBJECT_COUNTING}")
message(STATUS "ubsocket: UBSOCKET_BUILD_CLI = ${UBSOCKET_BUILD_CLI}")

set(UBSOCKET_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(UBSOCKET_CSRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/csrc)
set(UBSOCKET_INSTALL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/output/ubsocket)

message(STATUS "ubsocket: CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")
message(STATUS "ubsocket: UBSOCKET_BASE_DIR = ${UBSOCKET_BASE_DIR}")

include(cmake/config_version.cmake)
include(cmake/config_last_commit.cmake)

add_compile_options(-Wunused-variable -Wunused-value -Wcast-align)
add_compile_options(-fpermissive -Wno-error)
add_compile_options(-Wcast-qual -Winvalid-pch -Wwrite-strings -Wsign-compare -Wfloat-equal -Wextra -Wno-deprecated-declarations)

if (${CMAKE_BUILD_TYPE} MATCHES "RELEASE")
    add_compile_options(-fvisibility=hidden -fvisibility-inlines-hidden)
    add_compile_options(-fstack-protector-strong -Wstack-usage=16384 -Werror)
    add_compile_options(-Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now -fPIE -fPIC -ftrapv -s)
    add_compile_options(-D_FORTIFY_SOURCE=2 -O2)
    add_link_options(-Wl,-z,noexecstack,-z,relro,-z,now)
    set(CMAKE_SKIP_BUILD_RPATH ON)
    message(STATUS "ubsocket: added compile options for RELEASE")
elseif (${CMAKE_BUILD_TYPE} MATCHES "ASAN")
    set(SANITIZERS "address,undefined,pointer-compare,pointer-subtract,alignment")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=${SANITIZERS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=${SANITIZERS}")
    add_link_options(-fsanitize=${SANITIZERS})
else ()
    add_compile_options(-g -ggdb3 -O0 -fno-omit-frame-pointer -rdynamic -fPIC)
endif ()

if (UBSOCKET_ENABLE_COVERAGE)
    add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
    add_link_options(--coverage)
    message(STATUS "ubsocket: coverage enabled (--coverage flags added)")
endif ()

message(STATUS "ubsocket: CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")

if (BUILD_URMA_DLOPEN_BACKEND)
    add_definitions(-DURMA_DLOPEN_BACKEND_ENABLED)
    add_definitions(-DURMA_BACKEND_ENABLED)
endif ()

if (BUILD_UMQ_DLOPEN_BACKEND)
    add_definitions(-DUMQ_DLOPEN_BACKEND_ENABLED)
endif ()

if (BUILD_UMQ_ADAPTER_BACKEND)
    add_definitions(-DUMQ_ADAPTER_BACKEND_ENABLED)

    set(UMQ_BUILD_DIR "${UBSOCKET_BASE_DIR}/../hcom/umq/build")
    set(UMQ_LIB_DIR "${UMQ_BUILD_DIR}/src")
    set(UMQ_QBUF_LIB_DIR "${UMQ_LIB_DIR}/qbuf")
    set(UMQ_IPC_LIB_DIR "${UMQ_LIB_DIR}/umq_ipc")

    if (NOT EXISTS "${UMQ_LIB_DIR}/libumq.so")
        message(STATUS "ubsocket: libumq.so not found, building umq automatically...")
        file(MAKE_DIRECTORY "${UMQ_BUILD_DIR}")
        execute_process(
                COMMAND ${CMAKE_COMMAND} -DUSE_URMA_STUB=ON -S "${UBSOCKET_BASE_DIR}/../hcom/umq" -B "${UMQ_BUILD_DIR}"
                RESULT_VARIABLE UMQ_CMAKE_RESULT
        )
        if (NOT UMQ_CMAKE_RESULT EQUAL 0)
            message(FATAL_ERROR "ubsocket: umq cmake failed: ${UMQ_CMAKE_RESULT}")
        endif ()
        execute_process(
                COMMAND ${CMAKE_COMMAND} --build "${UMQ_BUILD_DIR}" -j ${CMAKE_BUILD_PARALLEL_LEVEL}
                RESULT_VARIABLE UMQ_BUILD_RESULT
        )
        if (NOT UMQ_BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "ubsocket: umq build failed: ${UMQ_BUILD_RESULT}")
        endif ()
        message(STATUS "ubsocket: umq built successfully")
    else ()
        message(STATUS "ubsocket: libumq.so found at ${UMQ_LIB_DIR}")
    endif ()
endif ()

if (ENABLE_OBJECT_COUNTING)
    add_definitions(-DOBJ_COUNTING_ENABLED)
endif ()

include_directories(
        ${UBSOCKET_BASE_DIR}/
        ${UBSOCKET_BASE_DIR}/csrc
        ${UBSOCKET_BASE_DIR}/../hcom
        ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
)

add_subdirectory(csrc)
find_package(ZLIB)
if (ZLIB_FOUND)
    add_subdirectory(tools/golden)
endif()

option(UBSOCKET_BUILD_TESTS "Build tests" OFF)
if (UBSOCKET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(3rdparty/gtest)
    add_subdirectory(3rdparty/mockcpp)
    add_subdirectory(unit_test)

    if (UBSOCKET_ENABLE_COVERAGE)
        find_program(LCOV_PATH lcov REQUIRED)
        find_program(GENHTML_PATH genhtml REQUIRED)

        set(UBSOCKET_COVERAGE_DIR ${CMAKE_BINARY_DIR}/coverage_report)
        set(UBSOCKET_COVERAGE_INFO ${CMAKE_BINARY_DIR}/coverage.info)
        set(UBSOCKET_COVERAGE_INITIAL ${CMAKE_BINARY_DIR}/coverage_initial.info)
        set(UBSOCKET_COVERAGE_COMBINED ${CMAKE_BINARY_DIR}/coverage_combined.info)
        set(UBSOCKET_COVERAGE_FILTERED ${CMAKE_BINARY_DIR}/coverage_filtered.info)

        add_custom_target(coverage
            COMMAND ${CMAKE_COMMAND} -E echo "Zeroing coverage counters..."
            COMMAND ${LCOV_PATH} --directory ${CMAKE_BINARY_DIR} --zerocounters --rc lcov_branch_coverage=1
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${CMAKE_COMMAND} -E echo "Capturing initial zero-coverage baseline..."
            COMMAND ${LCOV_PATH} --rc lcov_branch_coverage=1
                --directory ${CMAKE_BINARY_DIR}
                --base-directory ${UBSOCKET_BASE_DIR}/csrc
                --initial --capture --output-file ${UBSOCKET_COVERAGE_INITIAL}
                --ignore-errors gcov
            COMMAND ${CMAKE_COMMAND} -E echo "Capturing runtime coverage data..."
            COMMAND ${LCOV_PATH} --rc lcov_branch_coverage=1
                --directory ${CMAKE_BINARY_DIR}
                --base-directory ${UBSOCKET_BASE_DIR}/csrc
                --capture --output-file ${UBSOCKET_COVERAGE_INFO}
                --ignore-errors gcov
            COMMAND ${CMAKE_COMMAND} -E echo "Combining initial and runtime coverage..."
            COMMAND ${LCOV_PATH} --rc lcov_branch_coverage=1
                -a ${UBSOCKET_COVERAGE_INITIAL}
                -a ${UBSOCKET_COVERAGE_INFO}
                --output-file ${UBSOCKET_COVERAGE_COMBINED}
            COMMAND ${CMAKE_COMMAND} -E echo "Filtering coverage data..."
            COMMAND ${LCOV_PATH} --rc lcov_branch_coverage=1
                --remove ${UBSOCKET_COVERAGE_COMBINED}
                '*/_deps/*' '/usr/include/*' '*/3rdparty/*' '*/unit_test/*' '*/tools/*'
                --output-file ${UBSOCKET_COVERAGE_FILTERED}
                --ignore-errors gcov
            COMMAND ${CMAKE_COMMAND} -E echo "Generating TXT summary report..."
            COMMAND ${LCOV_PATH} --rc lcov_branch_coverage=1
                --summary ${UBSOCKET_COVERAGE_FILTERED}
                > ${CMAKE_BINARY_DIR}/coverage_summary.txt
                2>&1 || true
            COMMAND ${LCOV_PATH} --rc lcov_branch_coverage=1
                --list ${UBSOCKET_COVERAGE_FILTERED}
                > ${CMAKE_BINARY_DIR}/coverage_detailed.txt
                2>&1 || true
            COMMAND ${CMAKE_COMMAND} -E echo "Generating HTML report..."
            COMMAND ${GENHTML_PATH} --branch-coverage
                --output-directory ${UBSOCKET_COVERAGE_DIR}
                ${UBSOCKET_COVERAGE_FILTERED}
            COMMAND ${CMAKE_COMMAND} -E echo "Coverage summary: ${CMAKE_BINARY_DIR}/coverage_summary.txt"
            COMMAND ${CMAKE_COMMAND} -E echo "Coverage detailed: ${CMAKE_BINARY_DIR}/coverage_detailed.txt"
            COMMAND ${CMAKE_COMMAND} -E echo "Coverage HTML:    ${UBSOCKET_COVERAGE_DIR}"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generate ubsocket UT coverage report"
        )
    endif ()
endif ()