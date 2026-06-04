# engine/game/ai_squad — cd::ai::squad (Phase 682 / M13 W5A + Phase 712 / M16 W4)
#
# Multi-agent squad coordination layer: Left 4 Dead Special Infected /
# Halo Flood-style squad logic. Sprint-1 (Phase 682) ships:
#   * SquadRole enum (kPointman / kFlanker / kSuppressor / kCoverer)
#   * Formation snapshot (centroid + radius + member_count)
#   * Blackboard shared knowledge store (target + threat_level + facts map)
#   * Squad class: add/remove/update/tick/query API
#
# Tick computes formation centroid, radius, and exponential threat decay.
#
# Sprint-2 (Phase 712, opt-in via CD_AI_SQUAD_ENABLE_GPU=ON):
#   * GpuBatchSolver — compute-shader-driven formation solver for large
#     squads (>=64 members). One workgroup per squad; one invocation per
#     member. The GLSL kSquadFormationCS source lives inline in the header
#     so this code-path stays self-contained.
#
#   The GPU code-path is DORMANT by default (option OFF). When OFF:
#     - GpuBatchSolver.cpp is NOT compiled (no cd::rhi compile fan-out).
#     - The class symbol in GpuBatchSolver.hpp is gated by the same macro;
#       consumers that reference it without flipping the option get a clear
#       compile error.
#     - The associated test (cd_test_squad_gpu_batch) GTEST_SKIPs cleanly.
#
#   This split is deliberate (M16 W4 retry policy): the M15 attempt OOMed
#   when clang-cl pulled the rhi compile fan-out into every default ninja
#   build. Keeping the .cpp opt-in means the heavy include graph only
#   appears when the user explicitly asks for it.
#
# Design references (Sprint-1):
#   * Booth, M. "The AI Systems of Left 4 Dead." AIIDE 2009.
#   * Isla, D. "Handling Complexity in the Halo 2 AI." GDC 2005.
#   * van der Sterren, W. "Squad Tactics for Video Games." AI Game
#     Programming Wisdom 3, 2006.
#
# Design references (Sprint-2 GPU path):
#   * Reynolds, C. W. "Steering Behaviors For Autonomous Characters." GDC 1999.
#   * Bleiweiss, A. "Multi Agent Navigation on the GPU." NVIDIA GTC 2009.
#
# Dependencies (CLAUDE.md §7):
#   * Default OFF build : cd::core only — pure stdlib.
#   * CD_AI_SQUAD_ENABLE_GPU=ON : cd::core + cd::rhi (PRIVATE — header
#     surface stays the same; the cd::rhi include only fires inside the
#     macro-gated section of GpuBatchSolver.hpp).
#
# Moment: A combat designer creates a 4-agent enemy squad, sees them advance
# in formation with role-specific behaviors. With GPU batch ON, the same
# designer can drop a 128-member squad and keep 60 FPS — the formation
# cohesion runs in a single compute dispatch instead of 128 CPU iterations.

option(CD_AI_SQUAD_ENABLE_GPU
       "Compile the cd::ai::squad GpuBatchSolver compute-dispatch path (default OFF; opt-in to avoid pulling cd::rhi compile fan-out into default builds and to side-step the M15 OOM on memory-tight Windows hosts)."
       OFF)

set(_CD_AI_SQUAD_SOURCES src/AiSquad.cpp)
if(CD_AI_SQUAD_ENABLE_GPU)
  list(APPEND _CD_AI_SQUAD_SOURCES src/GpuBatchSolver.cpp)
endif()

cd_add_library(game_ai_squad
  TYPE STATIC
  SOURCES
    ${_CD_AI_SQUAD_SOURCES}
  PUBLIC_DEPS
    cd::core
)

if(CD_AI_SQUAD_ENABLE_GPU)
  # PUBLIC so the macro propagates to test/sample TUs that include
  # GpuBatchSolver.hpp. PRIVATE on the cd::rhi link because the rhi
  # surface is only used inside the .cpp + macro-gated header region —
  # downstream targets that opt in pick up the dep via transitive
  # PUBLIC_DEPS of cd::game_ai_squad (none of them need to link rhi
  # themselves).
  target_compile_definitions(cd_game_ai_squad PUBLIC CD_AI_SQUAD_ENABLE_GPU=1)
  target_link_libraries(cd_game_ai_squad PRIVATE cd::rhi)
endif()

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
