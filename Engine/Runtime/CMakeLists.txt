cmake_minimum_required(VERSION 3.21)
project(RUNTIME)

add_subdirectory(Application)

set(APP_NAME ${APPLICATION_NAME})
add_executable(${APP_NAME} main.cpp)

#TODO: Include dır