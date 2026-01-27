project(hcom_tests_integration)

add_compile_options(-fno-access-control)
add_compile_options(-fno-inline)
message(STATUS "CMAKE_SOURCE_DIR: ${CMAKE_SOURCE_DIR}")

set(HCOM_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/dist)
set(HCOM_3RDPARTY_INSTALL_DIR "${HCOM_OUTPUT_PATH}/hcom_3rdparty")
set(LIB_HTRACER_DIR "${HCOM_3RDPARTY_INSTALL_DIR}/hcom_tracer/lib")
set(LIB_HTRACER_STATIC "${HCOM_3RDPARTY_INSTALL_DIR}/hcom_tracer/lib/libhtracer_static.a")

# prepare libhtracer_static for ut compile
EXEC_PROGRAM(ar ${LIB_HTRACER_DIR} ARGS x ${LIB_HTRACER_STATIC})
EXEC_PROGRAM(ar ${LIB_HTRACER_DIR} ARGS x ${LIB_SECURE_C_3RDPARTH_STATIC})
EXEC_PROGRAM(ar ${LIB_HTRACER_DIR} ARGS cur ${LIB_HTRACER_STATIC} ${LIB_HTRACER_DIR}/*.o)
EXEC_PROGRAM(rm ${LIB_HTRACER_DIR} ARGS ${LIB_HTRACER_DIR}/*.o)

add_subdirectory(stub)
add_subdirectory(llt)
add_subdirectory(unit_test)
