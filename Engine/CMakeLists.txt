cmake_minimum_required(VERSION 3.21)
project(Engine)

set(ENGINE_INCLUDE_DIR "${ENGINE_SRC_DIR}/Include")

add_subdirectory(Source)
add_subdirectory(Runtime)