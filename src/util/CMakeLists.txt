cmake_minimum_required(VERSION 3.3)
project(libutil-showermat)
set(VERSION 0.1)
set(CMAKE_CXX_FLAGS "-std=gnu++14 -Wall -Og -g")
include_directories(".")
add_library(util util.cpp lua.cpp template.cpp)
target_link_libraries(util magic lua)

