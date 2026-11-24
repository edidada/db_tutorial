cmake_minimum_required(VERSION 3.10)
project(db_tutorial C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 所有实现位于 src/，头文件也在该目录
file(GLOB DB_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

add_executable(db ${DB_SOURCES})
target_include_directories(db PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
