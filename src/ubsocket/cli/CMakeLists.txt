add_executable(ubstat
    cli_client.cpp
    cli_args_parser.cpp
    cli_terminal_display.cpp)

target_include_directories(ubstat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ubstat PUBLIC ubsocket ${UMQ_BUF_LIB})