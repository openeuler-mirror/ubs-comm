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
 	 
 	 add_compile_options(-fno-access-control)
 	 
 	 target_include_directories(profiling_test
 	   PRIVATE
 	     ${UBSOCKET_BASE_DIR}/csrc/profiling
 	     ${UBSOCKET_BASE_DIR}/csrc/profiling/impl

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
 	 )