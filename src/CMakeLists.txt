cmake_minimum_required(VERSION 3.14)

option(BUILD_HCOM "Build hcom" OFF)
option(BUILD_UBSOCKET "Build ubsocket" ON)

message(STATUS "BUILD_HCOM = ${BUILD_HCOM}")
message(STATUS "BUILD_UBSOCKET = ${BUILD_HCOM}")

if (BUILD_HCOM)
    add_subdirectory(hcom)
endif ()

if (BUILD_UBSOCKET)
    add_subdirectory(ubsocket)
endif ()