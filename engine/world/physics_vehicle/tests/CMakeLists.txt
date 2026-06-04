cd_add_test(physics_vehicle
  SOURCES test_vehicle.cpp
  DEPS    cd::physics_vehicle cd::core
)

# Sprint-2: JoltAdapter gated test.
# Links cd::physics_jolt so is_stub_backend() is available; the test
# exercises the adapter against make_builtin_physics_world() so it compiles
# and passes on all platforms regardless of whether the real Jolt port is
# wired.  A GTEST_SKIP guard inside the test documents which assertions
# require real JPH collision geometry (Sprint-3 scope).
cd_add_test(vehicle_jolt
  SOURCES test_vehicle_jolt.cpp
  DEPS    cd::physics_vehicle cd::physics_jolt cd::physics cd::math cd::core
)
