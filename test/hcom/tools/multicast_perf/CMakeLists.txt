cmake_minimum_required(VERSION 3.14.0)
project(TurboCommTest CXX C)

#add_compile_options(-fsanitize=address)
#add_link_options(-fsanitize=address)

set(HCOM_INCLUDE_DIR ${HCOM_INSTALL_DIR}/include/hcom)
set(HCOM_LIB_DIR ${HCOM_INSTALL_DIR}/lib)

message(STATUS "HCOM_INCLUDE_DIR: " ${HCOM_INCLUDE_DIR})
message(STATUS "HCOM_LIB_DIR: " ${HCOM_LIB_DIR})

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE ${HCOM_BUILD_TYPE})
endif()
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

function(add_cpp_executable NAME)
    set(EXECUTABLE ${NAME})
    add_executable(${EXECUTABLE} ${EXECUTABLE}.cpp)
    target_include_directories(${EXECUTABLE} PRIVATE ${HCOM_INCLUDE_DIR})
    target_include_directories(${EXECUTABLE} PRIVATE ${HCOM_INSTALL_DIR}/../hcom_3rdparty/libboundscheck/include)
    target_link_directories(${EXECUTABLE} PRIVATE ${HCOM_LIB_DIR})
    target_link_directories(${EXECUTABLE} PRIVATE ${HCOM_INSTALL_DIR}/../hcom_3rdparty/libboundscheck/lib)
    target_link_libraries(${EXECUTABLE} hcom pthread rt dl boundscheck)
endfunction()

add_cpp_executable(publisher)
add_cpp_executable(subscriber)