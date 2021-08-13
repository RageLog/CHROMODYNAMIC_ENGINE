#CMake version to be used
cmake_minimum_required(VERSION 3.21)

#Defination
Set(MAIN_PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

#options
option(USE_VCPKG "Use VCPKG" OFF)
option(PASS_VERSION "Pass the version from cmake file to source code" ON)
option(ENABLE_TESTING "Enable testing" OFF)
option(ENABLE_clangformat "Activate ClangFormat.cmake module" OFF)
#option(USE_CUSTOM_DIR "Use custom dir" ON)
#option(BUILD_STATIC "Build static or dinamic library" ON)
#option(CREATE_RUNTIME_FILE "Create executable filese" ON)
#option(ADD_TO_PACKAGE_LIB_HEADER "Create lib and Header folders in package" ON)
#option(USE_NSIS "Use NSIS generator to produce installer" ON)
#option(USE_ICON "use icon" ON)


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


#Project Files
