file(GLOB SOURCES "*.cpp")

foreach(SOURCE ${SOURCES})
    get_filename_component(EXECUTABLE ${SOURCE} NAME_WE)
    add_executable(${EXECUTABLE} ${SOURCE})
    target_compile_options(${EXECUTABLE} PUBLIC -g -O0 -fconcepts-ts)
endforeach()