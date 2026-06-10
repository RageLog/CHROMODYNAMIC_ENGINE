# engine/render/debug_line — cd::debug_line (phase1030)
# CPU line-batch builder for 3D viewport debug shapes (AABB / OBB /
# frustum / circle / polyline / cross). RHI-independent by design:
# the consumer owns the kLineList pipeline + vertex buffer.
cd_add_library(debug_line
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
