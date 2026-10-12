cmake_minimum_required(VERSION 3.19)

project(distributor)
set(CMAKE_CXX_STANDARD 20)



include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/include)
# set(PROJ ${PROJECT_SOURCE_DIR}/src/*.cc)
# add_compile_options("-Wall")
add_compile_options("-O2")
add_compile_options("-Wno-format")
add_compile_options("-Wno-unused-result")

file(GLOB_RECURSE everything ./src/*.cc)
add_executable(main ${PROJECT_SOURCE_DIR}/exec/main.cc ${everything})
