cmake_minimum_required(VERSION 3.22)

project(SykeroLabs CXX)

set(CMAKE_CXX_STANDARD 20)

add_compile_options(-Wall -Wextra -pedantic -Werror)

add_subdirectory(src)
