cmake_minimum_required(VERSION 3.14.1)
project(hcom CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_SKIP_BUILD_RPATH ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE release)
endif()
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

if(NOT DEFINED CPU_TYPE)
    set(CPU_TYPE "arm_64")
endif()
message(STATUS "CPU_TYPE: ${CPU_TYPE}")

if (NOT BUILD_SERVICE_VERSION)
    set(BUILD_SERVICE_VERSION 1)
endif()
add_definitions(-DBUILD_SERVICE_VERSION=${BUILD_SERVICE_VERSION})
message(STATUS "BUILD_SERVICE_VERSION: ${BUILD_SERVICE_VERSION}")

option(BUILD_JAVA_SDK "Build java sdk" OFF)
option(BUILD_TESTS "Build unit tests" OFF)
option(BUILD_WITH_HW_CRC "Build with hardware crc" OFF)
option(BUILD_WITH_SERVICE "Build with SERVICE" ON)
option(BUILD_WITH_RDMA "Build with RDMA" ON)
option(BUILD_WITH_UB "Build with UB" OFF)
option(BUILD_WITH_SHM "Build with SHM" ON)
option(BUILD_WITH_SOCK "Build with SOCK" ON)
option(BUILD_WITH_ALLOCATOR_PROTECTION "Build with allocator protection" OFF)
option(BUILD_WITH_HTRACER "Build with htracer" ON)
option(ENABLE_ARM_KP "enable arm kunpeng" OFF)

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

if (BUILD_TESTS)
    enable_testing()
    add_definitions(-DBUILD_TESTS)

    # GTest can be found via find_package.
    #   find_package(GTest CONFIG REQUIRED)
    #   target_link_libraries(foo GTest::gtest_main)  # or GTest::gtest w/o the main function
    if(NOT EXISTS ${TEST_TOOL_INSTALL_PATH})
        message(FATAL_ERROR "Specify -DTEST_TOOL_INSTALL_PATH=/path/to/dir in cmake command args.")
    endif()
    list(APPEND CMAKE_PREFIX_PATH ${TEST_TOOL_INSTALL_PATH}/googletest/lib64/cmake)
endif ()

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

set(HCOM_SRC_DIR ${PROJECT_SOURCE_DIR})
set(HCOM_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/dist)
set(HCOM_BUILD_PATH ${PROJECT_SOURCE_DIR}/build)

set(HCOM_STUB_SRC_DIR ${PROJECT_SOURCE_DIR}/test/stub)

if (NOT DEFINED ENV{JAVA_HOME})
    set(JAVA_HOME /usr/local/jdk)
else ()
    set(JAVA_HOME $ENV{JAVA_HOME})
endif ()

include_directories(${HCOM_SRC_DIR}/src/under_api/verbs
                    ${HCOM_SRC_DIR}/src/under_api/urma
                    ${HCOM_SRC_DIR}/src/under_api/openssl
                    ${HCOM_SRC_DIR}/src/under_api/obmm
                    ${HCOM_SRC_DIR}/src
                    ${HCOM_SRC_DIR}/src/api
                    ${HCOM_SRC_DIR}/src/api/capi
                    ${HCOM_SRC_DIR}/src/api/java_sdk
                    ${HCOM_SRC_DIR}/src/common
                    ${HCOM_SRC_DIR}/src/service
                    ${HCOM_SRC_DIR}/src/service_v2
                    ${HCOM_SRC_DIR}/src/service_v2/api
                    ${HCOM_SRC_DIR}/src/transport
                    ${HCOM_SRC_DIR}/src/transport/rdma
                    ${HCOM_SRC_DIR}/src/transport/rdma/verbs
                    ${HCOM_SRC_DIR}/src/transport/ub
                    ${HCOM_SRC_DIR}/src/transport/shm
                    ${HCOM_SRC_DIR}/src/transport/sock
                    ${HCOM_STUB_SRC_DIR}/
                    ${JAVA_HOME}/include
                    ${JAVA_HOME}/include/linux)

if (BUILD_WITH_UB)
    include_directories(${HCOM_SRC_DIR}/3rdparty/)
endif ()
# Reset the prefix path if user doesn't provide one.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set_property(CACHE CMAKE_INSTALL_PREFIX PROPERTY VALUE ${PROJECT_BINARY_DIR})
endif()

add_subdirectory(src)

if(BUILD_TESTS)
    add_subdirectory(test)
endif()
message(STATUS "BUILD_TESTS: ${BUILD_TESTS}")
