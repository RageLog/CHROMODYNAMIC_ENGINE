# CDProject.cmake — CHROMODYNAMIC project-level CMake helpers
# ADR-005 (Foundation Policy Bundle) + ADR-014 (CI/CD)
#
# Provides:
#   cd_add_library(<short_name> ...)       — Library + alias + export header
#   cd_add_plugin(<short_name> ...)        — Plugin (always SHARED)
#   cd_add_test(<short_name> ...)          — gtest binary + ctest registration
#   cd_add_sample(<short_name> ...)        — Sample executable
#   cd_apply_warnings(<target>)            — `-Wall -Werror -Wextra ...`
#   cd_apply_sanitizers(<target>)          — ASan/UBSan/TSan/MSan via CD_SANITIZER
#
# Naming convention (ADR-016 D1 Replace-Ready):
#   - Target name:   cd_<lib>          (e.g., cd_core)
#   - Alias:         cd::<lib>         (e.g., cd::core)
#   - Export macro:  CD_<LIB>_API      (e.g., CD_CORE_API)
#   - Public include: include/cd/<lib>/
#   - Namespace:     cd::<lib>::

include_guard(GLOBAL)

#-------------------------------------------------------------------------------
# cd_add_library(<short_name>
#   [TYPE STATIC|SHARED|INTERFACE]
#   SOURCES <file>...
#   PUBLIC_DEPS <target>...
#   PRIVATE_DEPS <target>...
#   [PCH <header>]
# )
#-------------------------------------------------------------------------------
function(cd_add_library short_name)
  set(options "")
  set(oneValueArgs TYPE PCH)
  set(multiValueArgs SOURCES PUBLIC_DEPS PRIVATE_DEPS)
  cmake_parse_arguments(CD "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(target_name "cd_${short_name}")
  set(alias_name  "cd::${short_name}")
  string(TOUPPER "${short_name}" SHORT_UPPER)

  # Default static, switch via global option
  if(NOT CD_TYPE)
    if(CD_BUILD_SHARED_LIBS)
      set(CD_TYPE SHARED)
    else()
      set(CD_TYPE STATIC)
    endif()
  endif()

  if(CD_TYPE STREQUAL "INTERFACE")
    add_library(${target_name} INTERFACE)
    target_include_directories(${target_name}
      INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    if(CD_PUBLIC_DEPS)
      target_link_libraries(${target_name} INTERFACE ${CD_PUBLIC_DEPS})
    endif()
  else()
    add_library(${target_name} ${CD_TYPE} ${CD_SOURCES})

    target_include_directories(${target_name}
      PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    # Export macro generation
    if(CD_TYPE STREQUAL "SHARED")
      target_compile_definitions(${target_name}
        PRIVATE  CD_${SHORT_UPPER}_BUILD_DLL=1
        INTERFACE CD_${SHORT_UPPER}_DLL=1
      )
    endif()

    set_target_properties(${target_name} PROPERTIES
      C_VISIBILITY_PRESET hidden
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN ON
      POSITION_INDEPENDENT_CODE ON
    )

    if(CD_PUBLIC_DEPS)
      target_link_libraries(${target_name} PUBLIC ${CD_PUBLIC_DEPS})
    endif()
    if(CD_PRIVATE_DEPS)
      target_link_libraries(${target_name} PRIVATE ${CD_PRIVATE_DEPS})
    endif()

    if(CD_PCH AND CD_ENABLE_PCH)
      target_precompile_headers(${target_name} PRIVATE ${CD_PCH})
    endif()

    cd_apply_warnings(${target_name})
    cd_apply_sanitizers(${target_name})

    target_compile_features(${target_name} PUBLIC cxx_std_23)
  endif()

  add_library(${alias_name} ALIAS ${target_name})

  set_property(GLOBAL APPEND PROPERTY CD_ALL_LIBRARIES ${target_name})

  if(NOT CD_QUIET)
    message(STATUS "[cd] library ${alias_name} (${CD_TYPE})")
  endif()
endfunction()

#-------------------------------------------------------------------------------
# cd_add_plugin(<short_name> ...) — always SHARED, hot-reloadable
#-------------------------------------------------------------------------------
function(cd_add_plugin short_name)
  cd_add_library(${short_name} TYPE SHARED ${ARGN})
endfunction()

#-------------------------------------------------------------------------------
# cd_add_test(<short_name>
#   SOURCES <file>...
#   DEPS <target>...
# )
#-------------------------------------------------------------------------------
function(cd_add_test short_name)
  set(options "")
  set(oneValueArgs "")
  set(multiValueArgs SOURCES DEPS)
  cmake_parse_arguments(CDT "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CD_ENABLE_TESTING)
    return()
  endif()

  set(target_name "cd_test_${short_name}")
  add_executable(${target_name} ${CDT_SOURCES})
  target_link_libraries(${target_name} PRIVATE ${CDT_DEPS} GTest::gtest GTest::gtest_main)
  target_compile_features(${target_name} PUBLIC cxx_std_23)

  cd_apply_warnings(${target_name})
  cd_apply_sanitizers(${target_name})

  add_test(NAME ${target_name} COMMAND ${target_name})

  set_target_properties(${target_name} PROPERTIES
    FOLDER "tests/${short_name}"
  )

  if(NOT CD_QUIET)
    message(STATUS "[cd] test ${target_name}")
  endif()
endfunction()

#-------------------------------------------------------------------------------
# cd_add_sample(<short_name>
#   SOURCES <file>...
#   DEPS <target>...
# )
#-------------------------------------------------------------------------------
function(cd_add_sample short_name)
  set(options "")
  set(oneValueArgs "")
  set(multiValueArgs SOURCES DEPS)
  cmake_parse_arguments(CDS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(target_name "cd_sample_${short_name}")
  add_executable(${target_name} ${CDS_SOURCES})
  target_link_libraries(${target_name} PRIVATE ${CDS_DEPS})
  target_compile_features(${target_name} PUBLIC cxx_std_23)

  cd_apply_warnings(${target_name})
  cd_apply_sanitizers(${target_name})

  set_target_properties(${target_name} PROPERTIES
    FOLDER "samples"
    OUTPUT_NAME "${short_name}"
  )

  if(NOT CD_QUIET)
    message(STATUS "[cd] sample ${target_name}")
  endif()
endfunction()
