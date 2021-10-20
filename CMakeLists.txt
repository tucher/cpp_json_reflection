cmake_minimum_required(VERSION 3.5)

project(cpp_struct_serialisation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include_directories(${PROJECT_SOURCE_DIR}/3party)
add_executable(cpp_struct_serialisation main.cpp cpp_json_reflection.hpp jsmn.c)
