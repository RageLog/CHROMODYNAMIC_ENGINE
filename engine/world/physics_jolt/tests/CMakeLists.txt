cd_add_test(jolt_world
  SOURCES test_jolt_world.cpp
  DEPS    cd::physics_jolt cd::physics cd::math cd::core
)

# Forward the active backend selector (set by the parent CMakeLists) so the
# BackendSelectionMatchesBuildDefinition test can cross-check is_stub_backend()
# against the build configuration. The library keeps the define PRIVATE; only
# this selector token is mirrored to the test target.
if(DEFINED _CD_JOLT_TEST_DEFINE)
  target_compile_definitions(cd_test_jolt_world PRIVATE ${_CD_JOLT_TEST_DEFINE})
endif()
