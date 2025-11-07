file(GLOB_RECURSE FAKE_IBV *.cpp *.h)

add_library(fake_ibv_static STATIC ${FAKE_IBV})
target_link_libraries(fake_ibv_static
        -Wl,--start-group
        pthread dl rt
        -Wl,--end-group)