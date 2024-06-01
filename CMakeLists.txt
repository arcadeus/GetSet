cmake_minimum_required(VERSION 3.5)

project(GetSet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(GetSet main.cpp)

add_custom_target(
    static ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        ${CMAKE_CURRENT_LIST_DIR}/static
        ${CMAKE_BINARY_DIR}
    SOURCES
        static/config.txt
        static/www/auto.html
        static/www/manual.html
)
