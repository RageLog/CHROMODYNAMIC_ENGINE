cmake_minimum_required(VERSION 3.21)
project(RUNTIME)

add_subdirectory(Application)

set(APP_NAME ${APPLICATION_NAME})
add_executable(${APP_NAME} main.cpp)

#Include dır
target_link_libraries(${APP_NAME} PRIVATE Application)
target_include_directories(${APP_NAME} PRIVATE ${ENGINE_SRC_DIR}/Runtime/Application)