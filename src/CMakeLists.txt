cmake_minimum_required(VERSION 3.14)
project(ubs_comm CXX C)

option(BUILD_HCOM "Build hcom" ON)
option(BUILD_UBSOCKET "Build ubsocket" OFF)

message(STATUS "BUILD_HCOM = ${BUILD_HCOM}")
message(STATUS "BUILD_UBSOCKET = ${BUILD_UBSOCKET}")

if (BUILD_HCOM)
    add_subdirectory(hcom)
endif ()

if (BUILD_UBSOCKET)
    add_subdirectory(ubsocket)
endif ()