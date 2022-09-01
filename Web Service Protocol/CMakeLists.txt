#cmake_minimum_required(VERSION 3.16)
project(Block3 C)

set(CMAKE_C_STANDARD 99)

add_executable(server gserver.c)
add_executable(client gclient.c)
