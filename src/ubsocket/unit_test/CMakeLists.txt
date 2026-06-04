add_executable(umq_errno_converter_test "")
add_test(NAME umq_errno_converter_test COMMAND umq_errno_converter_test)
set_target_properties(umq_errno_converter_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")

target_compile_features(umq_errno_converter_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(umq_errno_converter_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

add_compile_options(-fno-access-control)

target_include_directories(umq_errno_converter_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc/core/umq
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
)

target_link_libraries(umq_errno_converter_test
  PRIVATE
    GTest::gtest_main
)

target_sources(umq_errno_converter_test
  PRIVATE
        umq_errno_converter_test.cpp
        ${UBSOCKET_BASE_DIR}/csrc/core/umq/umq_errno_converter.cpp
)

add_subdirectory(profiling)