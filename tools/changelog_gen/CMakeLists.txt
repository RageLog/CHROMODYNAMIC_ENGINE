# tools/changelog_gen — Conventional Commits → CHANGELOG.md
#
# Reads `git log --format='%h %s'` lines from stdin (one per commit),
# groups by Conventional Commit type, and emits a Markdown changelog
# section. CI pipes git log into this and saves the result.

add_executable(cd_changelog_gen main.cpp)
target_link_libraries(cd_changelog_gen PRIVATE cd::core)
target_compile_features(cd_changelog_gen PUBLIC cxx_std_23)
cd_apply_warnings(cd_changelog_gen)
cd_apply_sanitizers(cd_changelog_gen)
cd_apply_coverage(cd_changelog_gen)
set_target_properties(cd_changelog_gen PROPERTIES
  FOLDER "tools"
  OUTPUT_NAME "cd_changelog_gen"
)
