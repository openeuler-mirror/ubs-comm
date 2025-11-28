cmake_minimum_required(VERSION 3.12.1)
project(ubsocket)

set(CMAKE_FLAGS_PUBILC " -Wall -Werror -Wfloat-equal -Wtrampolines -g -rdynamic -Wl,-z,noexecstack,-z,relro,-z,now \
-fno-strict-aliasing -fstack-protector-strong -fPIE -pie -fPIC -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
-Wno-type-limits -fno-common")

set(CMAKE_BUILD_TYPE "Release")
set(CMAKE_C_FLAGS_RELEASE "-O2 -D_FORTIFY_SOURCE=2")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -D_FORTIFY_SOURCE=2")

set(CMAKE_FLAGS_ARM64 " -march=armv8-a+crc -DUB_ARCH_ARM64")
set(CMAKE_FLAGS_x86_64 " -msse4.2 -DUB_ARCH_X86_64")
if("${CMAKE_HOST_SYSTEM_PROCESSOR}" STREQUAL "aarch64")
    set(CMAKE_C_FLAGS  "${CMAKE_FLAGS_PUBILC} ${CMAKE_FLAGS_ARM64} -std=gnu11")
    set(CMAKE_CXX_FLAGS  "${CMAKE_FLAGS_PUBILC} ${CMAKE_FLAGS_ARM64} -std=c++11")
else()
    set(CMAKE_C_FLAGS  "${CMAKE_FLAGS_PUBILC} ${CMAKE_FLAGS_x86_64} -std=gnu11")
    set(CMAKE_CXX_FLAGS  "${CMAKE_FLAGS_PUBILC} ${CMAKE_FLAGS_x86_64} -std=c++11")
endif()

if (NOT DEFINE UMQ_INCLUDE OR UMQ_INCLUDE STREQUAL "")
    set(UMQ_INCLUDE "/usr/include/ub/umdk/urpc/umq/")
endif()
if (NOT DEFINE UMQ_LIB OR UMQ_LIB STREQUAL "")
    set(UMQ_LIB "/usr/lib64/libumq.so")
endif()

message(status "UMQ_INCLUDE: ${UMQ_INCLUDE}")
message(status "UMQ_LIB: ${UMQ_LIB}")

include_directories(${UMQ_INCLUDE})
aux_source_directory(${CMAKE_CURRENT_LIST_DIR} COMMON_RPC_ADPT_SRC)
add_subdirectory(brpc)

