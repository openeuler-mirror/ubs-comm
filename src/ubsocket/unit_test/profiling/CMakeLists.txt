 add_executable(profiling_test "")
 	 add_test(NAME profiling_test COMMAND profiling_test)
 	 set_target_properties(profiling_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
 	 
 	 set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
 	 set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")
 	 
 	 target_compile_features(profiling_test
 	   PRIVATE
 	     cxx_std_17
 	 )
 	 
 	 target_compile_definitions(profiling_test
 	   PRIVATE
 	     UBSOCKET_UNIT_TEST
 	 )
 	 
 	 target_compile_options(profiling_test
 	   PRIVATE
 	     -fno-access-control
 	 )
 	 
  target_include_directories(profiling_test
  	   PRIVATE
  	     ${UBSOCKET_BASE_DIR}/csrc/profiling
  	     ${UBSOCKET_BASE_DIR}/csrc/profiling/impl
  	     ${UBSOCKET_BASE_DIR}/csrc/common
  	     ${UBSOCKET_BASE_DIR}/csrc

  	 )
 	 
 	 target_link_libraries(profiling_test
 	   PRIVATE
 	     GTest::gtest_main
 	     mockcpp
 	 )
 	 
 target_sources(profiling_test
   PRIVATE
        ubsocket_profiling_test.cpp
         ${UBSOCKET_BASE_DIR}/csrc/profiling/ubsocket_prof.cpp
         ${UBSOCKET_BASE_DIR}/csrc/profiling/impl/ubsocket_prof_tracer.cpp
         ${UBSOCKET_BASE_DIR}/csrc/profiling/impl/ubsocket_prof_tracepoint_combiner.cpp
         ${UBSOCKET_BASE_DIR}/csrc/profiling/impl/ubsocket_prof_tracepoint_dumper.cpp
         ${UBSOCKET_BASE_DIR}/csrc/profiling/impl/ubsocket_prof_tracer_ext.cpp
         ${UBSOCKET_BASE_DIR}/csrc/profiling/impl/ubsocket_prof_tracepoint_combiner_ext.cpp
         ${UBSOCKET_BASE_DIR}/csrc/profiling/impl/ubsocket_prof_tracepoint_dumper_ext.cpp
         ${UBSOCKET_BASE_DIR}/csrc/common/ubsocket_global_setting.cpp
 )

add_executable(ubsocket_prof_tracer_ext_test "")
add_test(NAME ubsocket_prof_tracer_ext_test COMMAND ubsocket_prof_tracer_ext_test)
set_target_properties(ubsocket_prof_tracer_ext_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_prof_tracer_ext_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_prof_tracer_ext_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_prof_tracer_ext_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_prof_tracer_ext_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc/profiling
    ${UBSOCKET_BASE_DIR}/csrc/profiling/impl
)

target_link_libraries(ubsocket_prof_tracer_ext_test
  PRIVATE
    GTest::gtest_main
    mockcpp
)

target_sources(ubsocket_prof_tracer_ext_test
  PRIVATE
    ubsocket_prof_tracer_ext_test.cpp
    ${UBSOCKET_BASE_DIR}/csrc/profiling/impl/ubsocket_prof_tracer_ext.cpp
    ${UBSOCKET_BASE_DIR}/csrc/profiling/impl/ubsocket_prof_tracepoint_combiner_ext.cpp
    ${UBSOCKET_BASE_DIR}/csrc/profiling/impl/ubsocket_prof_tracepoint_dumper_ext.cpp
    ${UBSOCKET_BASE_DIR}/csrc/common/ubsocket_global_setting.cpp
)

# TraceCombiner(fast) 纯逻辑头/实现，轻量形态
add_executable(ubsocket_prof_tracepoint_combiner_test "")
add_test(NAME ubsocket_prof_tracepoint_combiner_test COMMAND ubsocket_prof_tracepoint_combiner_test)
set_target_properties(ubsocket_prof_tracepoint_combiner_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_prof_tracepoint_combiner_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_prof_tracepoint_combiner_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_prof_tracepoint_combiner_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_prof_tracepoint_combiner_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc/profiling
    ${UBSOCKET_BASE_DIR}/csrc/profiling/impl
)

target_link_libraries(ubsocket_prof_tracepoint_combiner_test
  PRIVATE
    GTest::gtest_main
    mockcpp
)

target_sources(ubsocket_prof_tracepoint_combiner_test
  PRIVATE
    ubsocket_prof_tracepoint_combiner_test.cpp
    ${UBSOCKET_BASE_DIR}/csrc/profiling/impl/ubsocket_prof_tracepoint_combiner.cpp
)

# TraceCombinerExt(ext) 纯逻辑头/实现，轻量形态
add_executable(ubsocket_prof_tracepoint_combiner_ext_test "")
add_test(NAME ubsocket_prof_tracepoint_combiner_ext_test COMMAND ubsocket_prof_tracepoint_combiner_ext_test)
set_target_properties(ubsocket_prof_tracepoint_combiner_ext_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_prof_tracepoint_combiner_ext_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_prof_tracepoint_combiner_ext_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_prof_tracepoint_combiner_ext_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_prof_tracepoint_combiner_ext_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc/profiling
    ${UBSOCKET_BASE_DIR}/csrc/profiling/impl
)

target_link_libraries(ubsocket_prof_tracepoint_combiner_ext_test
  PRIVATE
    GTest::gtest_main
    mockcpp
)

target_sources(ubsocket_prof_tracepoint_combiner_ext_test
  PRIVATE
    ubsocket_prof_tracepoint_combiner_ext_test.cpp
    ${UBSOCKET_BASE_DIR}/csrc/profiling/impl/ubsocket_prof_tracepoint_combiner_ext.cpp
)

# TraceGroup(fast) 纯逻辑头，轻量形态
add_executable(ubsocket_prof_tracepoint_group_test "")
add_test(NAME ubsocket_prof_tracepoint_group_test COMMAND ubsocket_prof_tracepoint_group_test)
set_target_properties(ubsocket_prof_tracepoint_group_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_prof_tracepoint_group_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_prof_tracepoint_group_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_prof_tracepoint_group_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_prof_tracepoint_group_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc/profiling
    ${UBSOCKET_BASE_DIR}/csrc/profiling/impl
)

target_link_libraries(ubsocket_prof_tracepoint_group_test
  PRIVATE
    GTest::gtest_main
    mockcpp
)

target_sources(ubsocket_prof_tracepoint_group_test
  PRIVATE
    ubsocket_prof_tracepoint_group_test.cpp
)

# Statistics::StatsMgr::UpdateReTxCount 依赖 umq_dfx_api + umq_trans_mode_t，走 ubsocket_static 重形态
add_executable(ubsocket_statistics_test "")
add_test(NAME ubsocket_statistics_test COMMAND ubsocket_statistics_test)
set_target_properties(ubsocket_statistics_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_statistics_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_statistics_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_statistics_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_statistics_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/profiling
    ${UBSOCKET_BASE_DIR}/csrc/profiling/impl
    ${UBSOCKET_BASE_DIR}/csrc/profiling/statistics
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/core/umq
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
    ${UBSOCKET_BASE_DIR}/include
)

target_link_libraries(ubsocket_statistics_test
  PRIVATE
    ubsocket_static
    boundscheck
    mockcpp
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_statistics_test
  PRIVATE
    statistics_test.cpp
)

# ProbeManager header-only logic and UMQ callback paths
add_executable(ubsocket_probe_manager_test "")
add_test(NAME ubsocket_probe_manager_test COMMAND ubsocket_probe_manager_test)
set_target_properties(ubsocket_probe_manager_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
target_compile_features(ubsocket_probe_manager_test PRIVATE cxx_std_17)
target_compile_definitions(ubsocket_probe_manager_test PRIVATE UBSOCKET_UNIT_TEST)
target_compile_options(ubsocket_probe_manager_test PRIVATE -fno-access-control)
target_include_directories(ubsocket_probe_manager_test PRIVATE
  ${UBSOCKET_BASE_DIR}/csrc
  ${UBSOCKET_BASE_DIR}/csrc/profiling
  ${UBSOCKET_BASE_DIR}/csrc/profiling/impl
  ${UBSOCKET_BASE_DIR}/csrc/profiling/statistics
  ${UBSOCKET_BASE_DIR}/csrc/common
  ${UBSOCKET_BASE_DIR}/csrc/core
  ${UBSOCKET_BASE_DIR}/csrc/core/umq
  ${UBSOCKET_BASE_DIR}/csrc/iobuf
  ${UBSOCKET_BASE_DIR}/csrc/under_api
  ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
  ${UBSOCKET_BASE_DIR}/include)
target_link_libraries(ubsocket_probe_manager_test PRIVATE ubsocket_static boundscheck mockcpp GTest::gtest_main pthread)
target_sources(ubsocket_probe_manager_test PRIVATE probe_manager_test.cpp)

# ubsocket_trace.cpp 依赖 ArraySet/Socket/GlobalSetting，走 ubsocket_static 重形态
add_executable(ubsocket_trace_test "")
add_test(NAME ubsocket_trace_test COMMAND ubsocket_trace_test)
set_target_properties(ubsocket_trace_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_trace_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_trace_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_compile_options(ubsocket_trace_test
  PRIVATE
    -fno-access-control
)

target_include_directories(ubsocket_trace_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc
    ${UBSOCKET_BASE_DIR}/csrc/profiling
    ${UBSOCKET_BASE_DIR}/csrc/profiling/impl
    ${UBSOCKET_BASE_DIR}/csrc/profiling/statistics
    ${UBSOCKET_BASE_DIR}/csrc/common
    ${UBSOCKET_BASE_DIR}/csrc/core
    ${UBSOCKET_BASE_DIR}/csrc/core/umq
    ${UBSOCKET_BASE_DIR}/csrc/iobuf
    ${UBSOCKET_BASE_DIR}/csrc/under_api
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
    ${UBSOCKET_BASE_DIR}/include
)

target_link_libraries(ubsocket_trace_test
  PRIVATE
    ubsocket_static
    boundscheck
    mockcpp
    GTest::gtest_main
    pthread
)

target_sources(ubsocket_trace_test
  PRIVATE
    ubsocket_trace_test.cpp
)

# cli_message.h 纯逻辑(CLIMessage/CLIControlHeader/Split)，零外部依赖，轻量形态
add_executable(ubsocket_cli_message_test "")
add_test(NAME ubsocket_cli_message_test COMMAND ubsocket_cli_message_test)
set_target_properties(ubsocket_cli_message_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

target_compile_features(ubsocket_cli_message_test
  PRIVATE
    cxx_std_17
)

target_compile_definitions(ubsocket_cli_message_test
  PRIVATE
    UBSOCKET_UNIT_TEST
)

target_include_directories(ubsocket_cli_message_test
  PRIVATE
    ${UBSOCKET_BASE_DIR}/csrc/profiling/statistics
    ${UBSOCKET_BASE_DIR}/../hcom/umq/include/umq
)

target_link_libraries(ubsocket_cli_message_test
  PRIVATE
    GTest::gtest_main
)

target_sources(ubsocket_cli_message_test
  PRIVATE
    cli_message_test.cpp
)
