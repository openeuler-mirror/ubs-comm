add_library(ubstat_objects OBJECT
        cli_client.cpp
        cli_args_parser.cpp
        cli_terminal_display.cpp
)

target_include_directories(ubstat_objects PUBLIC .)
target_link_libraries(ubstat_objects PUBLIC ubsocket_static)

add_executable(ubstat cli_main.cpp)

target_include_directories(ubstat PUBLIC .)
target_link_libraries(ubstat PRIVATE 
    ubstat_objects 
    ubsocket_static 
)