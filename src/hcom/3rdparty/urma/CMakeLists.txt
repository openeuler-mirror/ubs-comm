# -----------------------------
# 尝试查找系统已安装的 umdk-urma (RPM 模式)
# -----------------------------
find_path(URMA_INCLUDE_DIR
        NAMES urma_ubagg.h
        PATHS /usr/include/ub/umdk/urma/
        NO_DEFAULT_PATH
)

set(URMA_INSTALL_DIR "${HCOM_OUTPUT_PATH}/hcom_3rdparty/umdk/urma")
EXEC_PROGRAM(mkdir ${HCOM_BUILD_PATH} ARGS -p ${URMA_INSTALL_DIR}/include)
if(URMA_INCLUDE_DIR)
    message(STATUS "Found system-installed umdk-urma: headers=${URMA_INCLUDE_DIR}")
    EXEC_PROGRAM(cp ${URMA_INSTALL_DIR} ARGS -rf /usr/include/ub/umdk/urma/*.h ${URMA_INSTALL_DIR}/include)
    set(USE_SYSTEM_URMA ON)
else()
    message(STATUS "umdk-urma not found in system. Fetching from source...")
    set(USE_SYSTEM_URMA OFF)
endif()
message(STATUS "URMA_INSTALL_DIR: ${URMA_INSTALL_DIR}")
# -----------------------------
# 如果系统有，从系统路径拉取 umdk-urma 头文件; 如果系统没有，从源码拉取 umdk-urma 头文件
# -----------------------------
if(NOT USE_SYSTEM_URMA)
    find_path(URMA_INCLUDE_DIR
            NAMES urma_ubagg.h
            PATHS ${URMA_INSTALL_DIR}/include/
            NO_DEFAULT_PATH
    )
    if(NOT URMA_INCLUDE_DIR)
        include(FetchContent)

        # 假设 umdk-urma 仓库包含 urma 的头文件
        FetchContent_Declare(
                umdk
                GIT_REPOSITORY https://gitee.com/openeuler/umdk.git
                GIT_TAG        master
        )
        FetchContent_MakeAvailable(umdk)
        execute_process(COMMAND ls -al ${umdk_SOURCE_DIR})
        foreach(dir ${URMA_INCLUDE_DIR})
            message(STATUS "URMA include directory: ${dir}")
        endforeach()
        EXEC_PROGRAM(cp ${URMA_INSTALL_DIR} ARGS -rf ${umdk_SOURCE_DIR}/src/urma/lib/urma/bond/include/*.h ${URMA_INSTALL_DIR}/include)
        EXEC_PROGRAM(cp ${URMA_INSTALL_DIR} ARGS -rf ${umdk_SOURCE_DIR}/src/urma/lib/urma/core/include/*.h ${URMA_INSTALL_DIR}/include)
    else()
        message("-- URMA_INSTALL_DIR: ${URMA_INSTALL_DIR}/include/")
        message("-- URMA_INSTALL_DIR: has been built, ignored")
    endif()
    set(URMA_INCLUDE_DIR ${URMA_INSTALL_DIR}/include/)
endif()
target_include_directories(hcom PRIVATE ${URMA_INCLUDE_DIR})
target_include_directories(hcom_static_obj PRIVATE ${URMA_INCLUDE_DIR})