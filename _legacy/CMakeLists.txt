#CMake version to be used
cmake_minimum_required(VERSION 3.21)

#Defination
##Directories
Set(MAIN_PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
Set(ENGINE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Engine")
Set(vcpkgRoot "D:/repositories/VS/vcpkg")
Set(ENGINE_NAME "CHROMODYNAMIC")
Set(COMPANY_NAME "CRFLT")
Set(APPLICATION_NAME "YOUR_PROJECT_NAME")
Set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/output/")

#options
option(USE_VCPKG "Use VCPKG" OFF)#TODO: VCPKG Implemantation -> If posible, auto dependency download
option(ENABLE_TESTING "Enable testing" OFF)
option(ENABLE_clangformat "Activate ClangFormat.cmake module" ON)
option(ENGINE_GENERATE_DLL "If this is set to ON, DLL files will generated" ON)
option(BUILD_SHARED_LIBS "Rebuild using shared libraries" ON)
option(EXPORT_DLL_BUILD "Export:ON , Import OFF" ON)
option(ENABLE_clangformat "Activate ClangFormat.cmake module" ON)


#Modules (include .cmake)
list(APPEND CMAKE_MODULE_PATH "${MAIN_PROJECT_DIR}/CMakeModules")
include(util)
include(VersionFromGit)
if(ENABLE_clangformat)
    include(ClangFormat)
endif()
if(ENGINE_GENERATE_DLL)
    add_compile_definitions(EXPORT_DLL)
endif()
if(EXPORT_DLL_BUILD)
    add_compile_definitions(EXPORT_DLL_BUILD)
endif()


# Project NAME
version_from_git()
project(${ENGINE_NAME} VERSION ${VERSION} LANGUAGES CXX) 
add_compile_definitions(ENGINE_VERSION="${CMAKE_PROJECT_VERSION}")
add_compile_definitions(APPLICATION_NAME="${APPLICATION_NAME}")

# C++ Standart Definition
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

 #Thank you for this Jason Turner :)
add_library(project_options INTERFACE)
add_library(project_warnings INTERFACE)


if (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang" AND "x${CMAKE_CXX_SIMULATE_ID}" STREQUAL "xMSVC" OR ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang" )
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Xclang -std=c++2b")
    target_compile_features(project_options INTERFACE cxx_std_23)
endif()

if(MSVC)
    if(NOT USE_VCPKG)
        target_compile_options(project_warnings INTERFACE /W4 /WX "/permissive-")
    endif()
else()
  target_compile_options(project_warnings
                         INTERFACE -Wall
                                   -Werror # treat all warnings as errors
                                   -Wextra # reasonable and standard
                                   -Wshadow # warn the user if a variable
                                            # declaration shadows one from a
                                            # parent context
                                   -Wnon-virtual-dtor # warn the user if a class
                                                      # with virtual functions
                                                      # has a non-virtual
                                                      # destructor. This helps
                                                      # catch hard to track down
                                                      # memory errors
                                   -Wold-style-cast # warn for c-style casts
                                   -Wcast-align # warn for potential performance
                                                # problem casts
                                   -Wunused # warn on anything being unused
                                   -Woverloaded-virtual # warn if you overload
                                                        # (not override) a
                                                        # virtual function
                                   -Wpedantic # warn if non-standard C++ is used
                                   -Wconversion # warn on type conversions that
                                                # may lose data
                                   -Wsign-conversion # warn on sign conversions
                                   -Wnull-dereference # warn if a null
                                                      # dereference is detected
                                   -Wdouble-promotion # warn if float is
                                                      # implicit promoted to
                                                      # double
                                   -Wformat=2 # warn on security issues around
                                              # functions that format output (ie
                                              # printf)
                         )

  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    target_compile_options(project_warnings
                           INTERFACE -Wmisleading-indentation # warn if
                                                              # identation
                                                              # implies blocks
                                                              # where blocks do
                                                              # not exist
                                     -Wduplicated-cond # warn if if / else chain
                                                       # has duplicated
                                                       # conditions
                                     -Wduplicated-branches # warn if if / else
                                                           # branches have
                                                           # duplicated code
                                     -Wlogical-op # warn about logical
                                                  # operations being used where
                                                  # bitwise were probably wanted
                                     -Wuseless-cast # warn if you perform a cast
                                                    # to the same type
                                                    )
    target_link_libraries(project_options INTERFACE stdc++fs)
  endif()
endif()

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
add_subdirectory(Engine)
add_subdirectory(Project)

#Testing
if(ENABLE_TESTING)
    if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
        include(CTest)
    endif()
    add_subdirectory(Tests)
endif()