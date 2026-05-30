# engine/game/query — cd::game::query (Phase 475)
#
# G3.1 — spatial + ECS query helpers. Wraps cd::scene::SpatialHash
# (uniform-grid broad phase) and the cd::scene::Frustum n/p-vertex cull
# behind a single QueryWorld facade. Game code calls raycast() /
# sphere_query() / box_query() / frustum_query() without coupling to a
# specific accelerator implementation.
#
# References (cf. ADR-20260530-gameplay-library-family §G3):
#   * Akenine-Möller, Haines, Hoffman. Real-Time Rendering 4e §22.7 (ray
#     vs AABB slab method) and §16.10 (frustum n/p-vertex cull).
#   * Williams, Barrus, Morley, Shirley. "An Efficient and Robust
#     Ray-Box Intersection Algorithm", JGT 2005.
#
# Dependencies (CLAUDE.md §7): cd::core, cd::math, cd::ecs, cd::physics,
# cd::scene — gameplay tier sits strictly above world / script / ui.

cd_add_library(game_query
  TYPE STATIC
  SOURCES
    src/Query.cpp
  PUBLIC_DEPS
    cd::core
    cd::math
    cd::ecs
    cd::physics
    cd::scene
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
