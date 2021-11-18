#CMake version to be used
cmake_minimum_required(VERSION 3.21)

#Defination
##Directories
Set(MAIN_PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
Set(ENGINE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Engine")
Set(vcpkgRoot "D:/repositories/VS/vcpkg")
Set(ENGINE_NAME "CHROMODYNAMIC")
Set(COMPANY_NAME "CRFLT")
Set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/output/")


#options
option(USE_VCPKG "Use VCPKG" OFF)
option(PASS_VERSION "Pass the version from cmake file to source code" ON)
option(ENABLE_TESTING "Enable testing" On)
option(ENABLE_clangformat "Activate ClangFormat.cmake module" ON)




#Modules (include .cmake)
list(APPEND CMAKE_MODULE_PATH "${MAIN_PROJECT_DIR}/CMakeModules")
include(util)
include(VersionFromGit)
if(ENABLE_clangformat)
    include(ClangFormat)
endif()
#if(NOT BUILD_STATIC)
#        include(GenerateExportHeader)
#endif()

# Project NAME
version_from_git()
project(${ENGINE_NAME} VERSION ${VERSION} LANGUAGES CXX) 

# C++ Standart Definition
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

#TODO:(Cemal) Implament warning interfaces
 

#Set custom outcame file
if(EXISTS OUTPUT_DIR)
    file(GLOB_RECURSE OUTPUT_FILES  "${OUTPUT_DIR}/*")
    file (REMOVE_RECURSE ${OUTPUT_FILES})
endif()
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}/${projectName}/bin)
file(GLOB MY_PUBLIC_HEADERS  "${CMAKE_SOURCE_DIR}/include/*")
file(COPY ${MY_PUBLIC_HEADERS} DESTINATION ${OUTPUT_DIR}/include)

#Add Subfolders
add_subdirectory(Dependencies)


#Testing
if(ENABLE_TESTING)
    if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
        include(CTest)
    endif()
    add_subdirectory(Tests)
endif()