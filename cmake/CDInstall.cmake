# =============================================================================
# CDInstall — CHROMODYNAMIC install / export pipeline
# Phase 10 / Sprint 4 / Wave 121
#
# Each `cd_add_library` call already registers its target into the
# CHROMODYNAMICTargets export set (see cmake/CDProject.cmake). This file
# provides `cd_finalize_install()`, called once from the top-level
# CMakeLists.txt after every library has been declared, to:
#
#   * Write CHROMODYNAMICConfig.cmake from the template — that's the
#     file `find_package(CHROMODYNAMIC)` reads downstream.
#   * Write CHROMODYNAMICConfigVersion.cmake so SemVer compatibility
#     resolution works.
#   * Install the EXPORT set as CHROMODYNAMICTargets.cmake.
#   * Install the top-level docs (Readme, CHANGELOG, ARCHITECTURE.md,
#     LIBRARIES.md, Modules.dox) into the package's share/doc area.
#
# Downstream consumption (once a prefix is installed):
#
#   find_package(CHROMODYNAMIC 0.24 REQUIRED)
#   add_executable(myapp main.cpp)
#   target_link_libraries(myapp PRIVATE cd::core cd::diag cd::log)
#
# Activation: top-level CMakeLists includes this module and calls
# cd_finalize_install() after all add_subdirectory() walks finish.
# When CD_ENABLE_INSTALL is OFF (e.g. consumed-as-subproject build),
# the function is a no-op so vendored builds don't try to install.
# =============================================================================

include_guard(GLOBAL)

function(cd_finalize_install)
  if(NOT CD_ENABLE_INSTALL)
    return()
  endif()

  include(CMakePackageConfigHelpers)
  include(GNUInstallDirs)

  set(_config_dir "${CMAKE_INSTALL_LIBDIR}/cmake/CHROMODYNAMIC")
  set(_build_config "${CMAKE_BINARY_DIR}/CHROMODYNAMICConfig.cmake")
  set(_build_version "${CMAKE_BINARY_DIR}/CHROMODYNAMICConfigVersion.cmake")
  set(_template "${CMAKE_SOURCE_DIR}/cmake/CHROMODYNAMICConfig.cmake.in")

  if(NOT EXISTS "${_template}")
    message(WARNING "[cd-install] Config template missing: ${_template}")
    return()
  endif()

  # Generate the package config from the template. configure_package_config_file
  # rewrites @PACKAGE_VAR@ placeholders using install-relative paths so the
  # generated file is relocatable.
  configure_package_config_file(
    "${_template}"
    "${_build_config}"
    INSTALL_DESTINATION "${_config_dir}"
    NO_SET_AND_CHECK_MACRO
    NO_CHECK_REQUIRED_COMPONENTS_MACRO
  )

  # SemVer 2 says anything MAY change in 0.x, so SameMajorVersion (treats
  # 0.25 and 0.99 as interchangeable because both have major=0) lies to
  # downstream find_package() callers. SameMinorVersion is the honest
  # policy for the 0.x line — 0.30.x consumers will be required to
  # rebuild against any 0.31.0 release. When v1.0 ships, flip this back
  # to SameMajorVersion, which then becomes semantically correct.
  if(PROJECT_VERSION_MAJOR EQUAL 0)
    set(_cd_version_compat SameMinorVersion)
  else()
    set(_cd_version_compat SameMajorVersion)
  endif()

  write_basic_package_version_file(
    "${_build_version}"
    VERSION       "${PROJECT_VERSION}"
    COMPATIBILITY ${_cd_version_compat}
  )

  install(FILES
      "${_build_config}"
      "${_build_version}"
    DESTINATION "${_config_dir}"
  )

  install(EXPORT CHROMODYNAMICTargets
    FILE        CHROMODYNAMICTargets.cmake
    NAMESPACE   cd::
    DESTINATION "${_config_dir}"
  )

  # Top-level docs travel with the install tree so a consumer that
  # pulls the prefix into their build environment still sees the
  # canonical architecture / library / changelog references.
  set(_doc_dest "${CMAKE_INSTALL_DOCDIR}")
  set(_doc_files "")
  foreach(_doc IN ITEMS Readme.md CHANGELOG.md CLAUDE.md
                        docs/ARCHITECTURE.md docs/LIBRARIES.md
                        docs/DESIGN.md docs/PLAN.md docs/Modules.dox)
    if(EXISTS "${CMAKE_SOURCE_DIR}/${_doc}")
      list(APPEND _doc_files "${CMAKE_SOURCE_DIR}/${_doc}")
    endif()
  endforeach()
  if(_doc_files)
    install(FILES ${_doc_files} DESTINATION "${_doc_dest}")
  endif()
  if(EXISTS "${CMAKE_SOURCE_DIR}/docs/ADR")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/docs/ADR"
      DESTINATION "${_doc_dest}"
      FILES_MATCHING PATTERN "*.md"
    )
  endif()

  message(STATUS "[cd-install] Install prefix: ${CMAKE_INSTALL_PREFIX}")
  message(STATUS "[cd-install]   config dir: ${_config_dir}")
  message(STATUS "[cd-install]   include dir: ${CMAKE_INSTALL_INCLUDEDIR}")
  message(STATUS "[cd-install]   lib dir:     ${CMAKE_INSTALL_LIBDIR}")
  message(STATUS "[cd-install]   bin dir:     ${CMAKE_INSTALL_BINDIR}")
endfunction()
