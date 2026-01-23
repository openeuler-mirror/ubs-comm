cmake_minimum_required(VERSION 3.14)
option(BUILD_HCOM "Build hcom" ON)

if(BUILD_HCOM)
    add_subdirectory(hcom)
endif()
message(STATUS "BUILD_HCOM: ${BUILD_HCOM}")