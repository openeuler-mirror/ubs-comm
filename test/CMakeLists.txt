option(BUILD_HCOM "Build hcom" ON)
option(BUILD_UBSOCKET "Build ubsocket" ON)

if(BUILD_HCOM)
    set(HCOM_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/dist)
    add_subdirectory(hcom)
endif()
message(STATUS "BUILD_HCOM: ${BUILD_HCOM}")

if (BUILD_UBSOCKET)
    add_subdirectory(ubsocket)
endif ()