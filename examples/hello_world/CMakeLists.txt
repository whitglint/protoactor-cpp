cmake_minimum_required(VERSION 3.1)

project(hello_world)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME} "main.cpp")

find_package(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

include_directories("../../include")
