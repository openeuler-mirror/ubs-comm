set(LIBBOUNDCHECK_INSTALL_DIR "${HCOM_OUTPUT_PATH}/hcom_3rdparty/libboundscheck")
message(STATUS "LIBBOUNDCHECK_INSTALL_DIR: ${LIBBOUNDCHECK_INSTALL_DIR}")
# -----------------------------
# 查找 libboundscheck 头文件和库
# -----------------------------
find_path(LIBBOUNDCHECK_INCLUDE_DIR
        NAMES securec.h
        PATHS /usr/include
        NO_DEFAULT_PATH
)

find_library(LIBBOUNDCHECK_SHARED_LIBRARY
        NAMES libboundscheck.so boundscheck
        PATHS /usr/lib64 /usr/lib
        NO_DEFAULT_PATH
)

# -----------------------------
# 验证是否找到
# -----------------------------
if(NOT LIBBOUNDCHECK_INCLUDE_DIR OR NOT LIBBOUNDCHECK_SHARED_LIBRARY)
        message(STATUS
                "libboundscheck not found! Suggest install the RPM package:\n"
                "  sudo rpm -ivh libboundscheck-v1.1.11-6.oe2403.aarch64.rpm\n"
                "Or via yum/dnf:\n"
                "  sudo yum install libboundscheck\n"
                "Expected:\n"
                "  Header: /usr/include/securec.h\n"
                "  Shared: /usr/lib64/libboundscheck.so"
        )
        find_path(LIBBOUNDCHECK_INCLUDE_DIR
                NAMES securec.h
                PATHS ${LIBBOUNDCHECK_INSTALL_DIR}/include/
                NO_DEFAULT_PATH
        )

        find_library(LIBBOUNDCHECK_SHARED_LIBRARY
                NAMES libboundscheck.so boundscheck
                PATHS ${LIBBOUNDCHECK_INSTALL_DIR}/lib/
                NO_DEFAULT_PATH
        )
        if(NOT LIBBOUNDCHECK_INCLUDE_DIR OR NOT LIBBOUNDCHECK_SHARED_LIBRARY)
                message(STATUS "=========install from soucecode=========")
                
                include(FetchContent)

                # 假设 umdk-urma 仓库包含 urma 的头文件
                FetchContent_Declare(
                        libboundscheck
                        GIT_REPOSITORY https://gitee.com/openeuler/libboundscheck.git
                        GIT_TAG        master
                )
                FetchContent_MakeAvailable(libboundscheck)
                # 获取 hcom 源码路径
                FetchContent_GetProperties(libboundscheck)
                set(SECUREC_SRC_DIR ${libboundscheck_SOURCE_DIR}/src/hcom)
                execute_process(
                        COMMAND make CC=gcc
                        WORKING_DIRECTORY ${libboundscheck_SOURCE_DIR}
                        RESULT_VARIABLE MAKE_RESULT
                )
                EXEC_PROGRAM(mkdir ${HCOM_BUILD_PATH} ARGS -p ${LIBBOUNDCHECK_INSTALL_DIR})
                EXEC_PROGRAM(cp ${LIBBOUNDCHECK_INSTALL_DIR} ARGS -rf ${libboundscheck_SOURCE_DIR}/lib/ ${LIBBOUNDCHECK_INSTALL_DIR})
                EXEC_PROGRAM(cp ${LIBBOUNDCHECK_INSTALL_DIR} ARGS -rf ${libboundscheck_SOURCE_DIR}/include/ ${LIBBOUNDCHECK_INSTALL_DIR})

                set(LIBBOUNDCHECK_INCLUDE_DIR ${LIBBOUNDCHECK_INSTALL_DIR}/include)
                set(LIBBOUNDCHECK_SHARED_LIBRARY ${LIBBOUNDCHECK_INSTALL_DIR}/lib/libboundscheck.so)
        else()
                message("-- libboundscheck: ${LIBBOUNDCHECK_SHARED_LIBRARY}")
                message("-- libboundscheck: has been built, ignored")
        endif()
endif()

message(STATUS "libboundscheck headers: ${LIBBOUNDCHECK_INCLUDE_DIR}")
message(STATUS "libboundscheck shared: ${LIBBOUNDCHECK_SHARED_LIBRARY}")

# -----------------------------
# 链接 INTERFACE 库
# -----------------------------
target_include_directories(hcom PRIVATE ${LIBBOUNDCHECK_INCLUDE_DIR})
target_include_directories(hcom_static_obj PRIVATE ${LIBBOUNDCHECK_INCLUDE_DIR})

# 链接共享库
target_link_libraries(hcom ${LIBBOUNDCHECK_SHARED_LIBRARY})
target_link_libraries(hcom_static_obj ${LIBBOUNDCHECK_SHARED_LIBRARY})