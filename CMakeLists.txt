cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

add_definitions(-std=c++11)

project(VideoWriter LANGUAGES CXX)

include_directories(${PROJECT_SOURCE_DIR}/inc)

set(demo_dir ${PROJECT_SOURCE_DIR}/demo)
file(GLOB demo_codes ${demo_dir}/*.cpp)

set(src_dir ${PROJECT_SOURCE_DIR}/src)
file(GLOB src_codes ${src_dir}/*.cpp)

foreach(demo ${demo_codes})
    string(REGEX MATCH "[^/]+$" demo_file ${demo})
    string(REPLACE ".cpp" "" demo_basename ${demo_file})
    add_executable(${demo_basename} ${demo} ${src_codes})
endforeach()