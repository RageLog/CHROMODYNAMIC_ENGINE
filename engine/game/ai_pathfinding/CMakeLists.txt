# engine/game/ai_pathfinding — cd::ai::pathfinding (Phase 671)
#
# Sprint-1: in-memory NavMesh + A* pathfinding on a triangulated nav-mesh.
#
# Purpose: 2D/3D pathfinding over a pre-built navigation mesh using the A*
# algorithm on the triangle dual-graph (centroid graph). Supports arbitrary
# 3D meshes; flat 2D navmeshes are a degenerate case (y == 0 for all verts).
#
# Sprint-2 plan: spatial hash / BVH for nearest-triangle queries (currently
# O(N) linear scan), funnel algorithm (Snook 2000) for path smoothing, and
# a Recast-compatible triangulation pipeline for mesh import.
#
# References:
#   * Hart, Nilsson & Raphael, IEEE TSSC 1968 — A* admissible heuristic.
#   * Millington & Funge, "AI for Games", CRC Press 2009, Ch. 4.
#   * Snook, "Simplified 3D Movement and Pathfinding Using Navigation Meshes",
#     Game Programming Gems 1, 2000.
#
# Dependencies (CLAUDE.md §7): cd::core only — pure stdlib, no ECS/render.
#
# Moment: A game designer drops a navmesh into the scene, NPCs walk around
# obstacles to reach the player using actual A* — no scripted move-to-point
# hacks.

cd_add_library(game_ai_pathfinding
  TYPE STATIC
  SOURCES
    src/Pathfinding.cpp
    src/Triangulator.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
