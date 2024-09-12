cmake_minimum_required(VERSION 3.30.0)
project(server_cache)
include_directories(include)
set(SOURCES
    src/proxy_parse.c
    src/server_cache.cpp)
add_executable(server_cache ${SOURCES})   
