cd_add_test(tlas_compaction
  SOURCES test_tlas_compaction.cpp
  DEPS    cd::math cd::rhi cd::concurrency
)

# phase822 — texture-average colour helper that drives the chrome-Sponza
# reflection per-prim albedo flow. Helper lives in
# samples/engine/hello_engine/HelloTextureAverage.hpp (stdlib-only) so
# this test can exercise it without the rhi / glTF heavy include graph.
cd_add_test(hello_engine_tex_avg
  SOURCES test_hello_engine_tex_avg.cpp
  DEPS    cd::core
)

# phase826 — golden-fixture CLI parser. The pure parse_args() lives in
# HelloGoldenCli.hpp (depends only on SponzaFixtures.hpp); the test
# exercises every edge case the agent-iteration loop relies on
# (out-of-range fixtures, non-numeric input, frame-index 0-based shift,
# null argv tolerance, full command-line round-trip).
cd_add_test(hello_engine_golden_cli
  SOURCES test_hello_engine_golden_cli.cpp
  DEPS    cd::core
)

# T1.7 phase543 -- Sponza golden-image CI gate.
#
# Two tiers (see test source header for details):
#   * CPU-synth tier  : always runs; exercises cd::imgdiff against the
#                       committed 256x144 PNG references under
#                       tests/golden/sponza/.
#   * GPU-subprocess  : gated by CD_SPONZA_GOLDEN_REQUIRE=1 env var at
#                       run time. SKIPs when unset. Spawns the
#                       hello_engine binary with --golden-fixture N
#                       --golden-out <path>.png and diffs the dumped PNG.
#
# Labels: "golden", "sample", "sponza" so `ctest -L golden` picks it up
# alongside the rest of the golden-image CI lane.
#
# No DISABLED gate -- the test is opt-in via the env var, not via
# CMake configuration. Existing CI cells without GPU stay green
# because the GPU tier SKIPs cleanly.
cd_add_test(sample_sponza_golden
  SOURCES test_sponza_golden.cpp
  DEPS    cd::asset_image cd::imgdiff cd::core
)
if(TARGET cd_test_sample_sponza_golden)
  set_tests_properties(cd_test_sample_sponza_golden PROPERTIES
    LABELS "golden;sample;sponza"
    # T1.7 GPU tier may take ~90s when active (Vulkan device init x 5).
    # CPU-synth tier is fast (<1s) -- the timeout only bites when the
    # subprocess path is engaged.
    TIMEOUT 600
  )
  # Test depends on the hello_engine binary being built first so the
  # subprocess path can locate it. CMake's "FIXTURES_REQUIRED" would
  # require a separate fixture test -- a build-order dependency via
  # add_dependencies is enough here (the test exe links nothing from
  # hello_engine; we just need the .exe to exist on disk before the
  # gpu-subprocess tier runs).
  if(TARGET cd_sample_hello_engine)
    add_dependencies(cd_test_sample_sponza_golden cd_sample_hello_engine)
  endif()
endif()
