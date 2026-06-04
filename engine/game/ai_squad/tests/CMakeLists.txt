cd_add_test(game_ai_squad
  SOURCES test_ai_squad.cpp
  DEPS    cd::game_ai_squad cd::core
)

# Phase 712 — cd::ai::squad::GpuBatchSolver test.
#
# Builds in BOTH default (CD_AI_SQUAD_ENABLE_GPU=OFF) and opt-in builds.
# In the OFF build the tests compile against the (empty) symbol surface and
# GTEST_SKIP at runtime — so this binary is always present, and ctest shows
# the SKIP rationale clearly. In the ON build the test TU pulls in
# cd::rhi/ICommandBuffer.hpp through the macro-gated section of
# GpuBatchSolver.hpp, so the test target needs cd::rhi as a direct dep.
# (The lib itself keeps cd::rhi PRIVATE — see the parent CMakeLists.txt
# rationale: only the .cpp + macro-gated header region pull rhi headers, and
# downstream code that opts in must declare its own rhi link directly.)
set(_CD_SQUAD_GPU_TEST_DEPS cd::game_ai_squad cd::core)
if(CD_AI_SQUAD_ENABLE_GPU)
  list(APPEND _CD_SQUAD_GPU_TEST_DEPS cd::rhi)
endif()

cd_add_test(squad_gpu_batch
  SOURCES test_squad_gpu_batch.cpp
  DEPS    ${_CD_SQUAD_GPU_TEST_DEPS}
)
