add_library(cli_objects OBJECT
    cli_client.cpp
    cli_args_parser.cpp
    cli_terminal_display.cpp
)

target_include_directories(cli_objects PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cli_objects PUBLIC ubsocket ${UMQ_BUF_LIB})

add_executable(ubstat cli_main.cpp)

target_include_directories(ubstat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ubstat PRIVATE 
    cli_objects 
    ubsocket 
    ${UMQ_BUF_LIB}
)