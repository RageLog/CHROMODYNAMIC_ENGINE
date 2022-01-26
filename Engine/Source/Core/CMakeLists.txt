cmake_minimum_required(VERSION 3.21)

# project name
get_filename_component("${CMAKE_CURRENT_SOURCE_DIR}_Name" ${CMAKE_CURRENT_SOURCE_DIR} NAME)
set(LIBRARY_Name "${${CMAKE_CURRENT_SOURCE_DIR}_Name}")
string(REPLACE " " "_" LIBRARY_Name ${LIBRARY_Name})
project(${LIBRARY_Name} LANGUAGES CXX) 

set(THIS_SOURCE "${ENGINE_INCLUDE_DIR}/${LIBRARY_Name}")
file(GLOB "src_${CMAKE_CURRENT_SOURCE_DIR}" "${THIS_SOURCE}/*.h*"  "*.cpp")

#TODO:(Cemal) static or shared implemantation
add_library(${LIBRARY_Name} STATIC "${src_${CMAKE_CURRENT_SOURCE_DIR}}")


SET_TARGET_PROPERTIES(${LIBRARY_Name} PROPERTIES PREFIX " ")

#where is your include file
target_include_directories(${LIBRARY_Name} PUBLIC "${ENGINE_INCLUDE_DIR}/")
target_include_directories(${LIBRARY_Name} PRIVATE ${ENGINE_SRC_DIR}/Source/)

#GIVE to ID for each
set(LIB_IDENTIFIER "YOUR_IDENTIFIER")
target_compile_definitions(${LIBRARY_Name} PUBLIC ${LIBRARY_Name}_ID="${LIB_IDENTIFIER}")
#TODO:Options and warnings
#target_link_libraries(${LIBRARY_Name} PRIVATE project_options project_warnings)