option(BUILD_HCOM "Build hcom" ON)

if(BUILD_HCOM)
    set(HCOM_OUTPUT_PATH ${CMAKE_INSTALL_PREFIX}/dist)
    add_subdirectory(hcom)
endif()
message(STATUS "BUILD_HCOM: ${BUILD_HCOM}")