option(CD_SPONZA_GOLDEN_ENABLE
  "Enable the Sponza golden-image CI gate (cd_test_sample_sponza_golden). Off by default; flip on for self-hosted CI cells with a Vulkan device + committed reference set."
  OFF)

cd_add_test(tlas_compaction
  SOURCES test_tlas_compaction.cpp
  DEPS    cd::math cd::rhi cd::concurrency
)

# T1.7 -- Sponza golden-image CI gate (phase 514).
#
# Pure-CPU infrastructure tier: 5 fixed camera fixtures + pixel diff /
# SSIM-lite / FLIP-lite via cd::imgdiff against committed PNG goldens
# under tests/golden/sponza/.  The actual Vulkan offscreen render-
# capture step is queued for the next session (see test source for the
# scope-down rationale); the CI gate infrastructure is in place so the
# next session only has to wire the swapchain readback through the
# existing capture/compare path.
#
# Labelling + DISABLED policy:
#   * Labelled "golden" + "sample" so `ctest -L golden` runs it
#     alongside the other 5-sample harness (when re-enabled per cell).
#   * Initially DISABLED so the minimal CI matrix (which lacks a Vulkan
#     device on most cells) does not even attempt to run it. Self-hosted
#     NVIDIA / Intel CI flips DISABLED=FALSE via:
#       ctest --test-dir <build> -L golden \
#             --resource-spec-file ... -j1 --output-on-failure
#     after setting CD_SPONZA_GOLDEN_ENABLE=ON in the cache.
#   * To enable locally:  ctest -R sponza_golden  (CMake's add_test does
#     not honour DISABLED for direct name matches when --no-tests=error
#     is set, so `-R` is the easy override).
cd_add_test(sample_sponza_golden
  SOURCES test_sponza_golden.cpp
  DEPS    cd::asset_image cd::imgdiff cd::core
)
if(TARGET cd_test_sample_sponza_golden)
  set_tests_properties(cd_test_sample_sponza_golden PROPERTIES
    LABELS "golden;sample;sponza"
  )
  if(NOT CD_SPONZA_GOLDEN_ENABLE)
    # CTest only honours DISABLED when set to a truthy value; we set TRUE
    # explicitly so `ctest -L golden` still skips it on cells that have
    # not opted in. Override with `-DCD_SPONZA_GOLDEN_ENABLE=ON` at
    # configure time, OR run it directly via name (`ctest -R sponza_golden`)
    # which bypasses DISABLED.
    set_tests_properties(cd_test_sample_sponza_golden PROPERTIES
      DISABLED TRUE)
  endif()
endif()
