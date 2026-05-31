# engine/ui/editor/cdproj — cd::editor_cdproj
#
# phase547 — .cdproj project-file read/write library.
#
# Standalone STATIC library with no engine-level dependencies beyond
# cd::core. This keeps it usable by the apps/editor binary without pulling
# in any UI or rendering headers.
#
# Targets:
#   cd_editor_cdproj   (real target)
#   cd::editor_cdproj  (alias)

cd_add_library(editor_cdproj
  TYPE STATIC
  SOURCES
    src/CdprojFile.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
