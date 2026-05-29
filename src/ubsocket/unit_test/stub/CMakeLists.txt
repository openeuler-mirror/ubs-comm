# Helper headers (included via target_include_directories in parent)
# Static libraries for link-time system call replacement

add_library(fake_epoll_static STATIC
    fake_epoll/fake_epoll.cpp
)

target_include_directories(fake_epoll_static
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/fake_epoll
)

target_compile_features(fake_epoll_static PRIVATE cxx_std_17)
target_compile_options(fake_epoll_static PRIVATE -fno-access-control)
target_compile_definitions(fake_epoll_static PRIVATE UBSOCKET_UNIT_TEST)
target_link_libraries(fake_epoll_static PRIVATE dl)