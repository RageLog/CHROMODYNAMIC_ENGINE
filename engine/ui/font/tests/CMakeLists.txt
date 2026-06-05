cd_add_test(font
  SOURCES test_font.cpp
  DEPS    cd::ui_font cd::core
)

# When FreeType was resolved from vcpkg (shared DLL on Windows), the test
# binary needs freetyped.dll / freetype.dll on its DLL search path.
# VCPKG_APPLOCAL_DEPS copies DLLs to executable output dirs (samples/exes),
# but ctest runs the exe in-place which may not have them. We add the vcpkg
# debug and release bin dirs to PATH so both Debug and Release test runs work.
if(DEFINED VCPKG_INSTALLED_DIR AND WIN32)
  set_tests_properties(cd_test_font PROPERTIES
    ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin;PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
  )
endif()
