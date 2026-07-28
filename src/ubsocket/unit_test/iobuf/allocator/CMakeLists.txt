set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")
add_compile_options(-fno-access-control)

set(ALLOCATOR_INCLUDE_DIRS
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/iobuf/allocator
)

add_executable(allocator_test "")
add_test(NAME allocator_test COMMAND allocator_test)
set_target_properties(allocator_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(allocator_test PRIVATE cxx_std_17)

target_compile_definitions(allocator_test PRIVATE UBSOCKET_UNIT_TEST)

target_include_directories(allocator_test PRIVATE ${ALLOCATOR_INCLUDE_DIRS})

target_link_libraries(allocator_test PRIVATE
    ubsocket_static
    boundscheck
    GTest::gtest_main
    pthread
)

target_sources(allocator_test PRIVATE
        allocator_test.cpp
)
