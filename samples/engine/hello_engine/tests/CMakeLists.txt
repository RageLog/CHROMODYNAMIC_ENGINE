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

# phase827 — PBR demo-grid layout. Locks the 4x4 cardinality, the
# metallic/roughness gradient monotonicity, the chrome albedo tint,
# the phase799 "Z=0 mid-nave" relocation, and the in-atrium bounds
# guarantee (X∈[-15,+15], Y∈[0,+5], Z∈[-3,+3]) so a future refactor
# can't silently push the grid back outside Sponza (the original
# "spheres in a different universe" failure mode).
cd_add_test(hello_engine_pbr_grid
  SOURCES test_hello_engine_pbr_grid.cpp
  DEPS    cd::core cd::math
)

# phase882 — first-boot HelloEngineFx default lock. Phase 875 + 876
# converged on a clean-sky / no-haze first-boot look (fog 0, aerial 0,
# clouds 0.20). The struct is header-only POD; this test simply
# value-checks the in-class default initialisers so a future "subtle
# atmosphere by default" PR has to pass a peer-reviewed test diff.
cd_add_test(hello_engine_fx_defaults
  SOURCES test_hello_engine_fx_defaults.cpp
  DEPS    cd::core
)

# phase845 — W8-BE layout regression nets. Locks InstanceMatGpu at 48 B
# with the post-phase840 albedo_tex_slot + index_offset extension,
# the kBindlessAlbedoSlotNone = 0xFFFFFFFFu sentinel contract, the
# kInstMatBytes total size, and W8BEGeomMeta default-sentinel +
# trivially-copyable contract. Wrong layout here = misaligned per-prim
# SSBO reads = chrome reflections paint garbage. Caught at compile
# time + ctest, not at render-time PNG inspection.
cd_add_test(hello_engine_w8be_layout
  SOURCES test_hello_engine_w8be_layout.cpp
  DEPS    cd::core cd::math cd::rhi cd::concurrency
)

# prim hot-reload bindless-layout parity -- regression net for the
# phase864-class silent bindless drop: the main.cpp shader-watch closure
# (add_entry "prim") once called prim_recreate WITHOUT the 4th
# bindless_set_layout arg (defaulted {}) while boot (spawn_materials)
# passes out.prim_bindless_layout, so the first prim hot-reload rebuilt
# the pipeline without its dedicated set=1 bindless sampler-array layout.
# The test pins the prim_recreate contract on the Null backend via a
# forwarding device that captures PipelineLayoutDesc::set_layouts:
# with-layout => 2 sets (own + bindless), defaulted => 1 set (the drop).
# HelloMaterials.hpp pulls the sample's shader/material include graph,
# hence the wider DEPS list than the sibling tests.
cd_add_test(hello_materials_prim_bindless
  SOURCES test_hello_materials_prim_bindless.cpp
  DEPS    cd::asset cd::core cd::debug_line cd::gluon cd::light cd::material
          cd::math cd::post_bloom cd::post_composite cd::rhi cd::shader
          cd::velocity
)

# X5 hardening step 2 (ADR-20260608 addendum A.3(a)) -- unit net for
# cd_sample::resolve_shader_path (HelloMaterials.hpp): dev builds map the
# cwd-relative "shaders/<file>.glsl" onto the SOURCE-tree file so compile
# path and FileWatcher watch path are the SAME file the developer edits;
# shipped binaries get the relative path back byte-identical. source_dir is
# an injectable parameter, so the tests are compile-definition-independent.
# HelloMaterials.hpp pulls the sample's shader/material include graph,
# hence the same DEPS list as hello_materials_prim_bindless above.
cd_add_test(hello_resolve_shader_path
  SOURCES test_hello_resolve_shader_path.cpp
  DEPS    cd::asset cd::core cd::debug_line cd::gluon cd::light cd::material
          cd::math cd::post_bloom cd::post_composite cd::rhi cd::shader
          cd::velocity
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

# X5-3(b) (ADR-20260608 addendum A.3) — pre-golden EXE-side staleness guard.
# Asserts sha256(SOURCE shader) == sha256(EXE-side copied shader) for every
# hello_engine *.glsl, run as a FIXTURES_SETUP step so it executes BEFORE any
# golden-image compare (FIXTURES_REQUIRED on the golden test). A stale exe-side
# copy (the relink-only-build trap) fails LOUDLY here instead of producing a
# false byte-identical golden. No gtest binary — a pure CMake -P script keyed
# off the configure-time source dir + the build-time exe dir.
if(TARGET cd_sample_hello_engine)
  add_test(NAME cd_test_hello_engine_shader_staleness
    COMMAND ${CMAKE_COMMAND}
      -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../shaders
      -DEXE_DIR=$<TARGET_FILE_DIR:cd_sample_hello_engine>/shaders
      -P ${CMAKE_SOURCE_DIR}/scripts/check_shader_staleness.cmake)
  set_tests_properties(cd_test_hello_engine_shader_staleness PROPERTIES
    LABELS "golden;sample;shader-staleness"
    FIXTURES_SETUP HELLO_ENGINE_SHADERS_FRESH
    TIMEOUT 60)
  # The fixture asserts what the POST_BUILD copy staged — it reads the
  # exe-side shaders/ that the cd_sample_hello_engine POST_BUILD step copies.
  # CTest cannot build a target before a `cmake -P` script test, so freshness
  # is guaranteed by the normal `cmake --build` ordering (the exe + its copy
  # step are built before `ctest` runs). The guard catches the case where a
  # shader edit slipped through a relink-only build.
  # Any golden-image compare that reads the exe-side shaders must run AFTER
  # this guard. The Sponza golden's GPU-subprocess tier launches
  # hello_engine.exe which reads shaders/ from its cwd, so it depends on the
  # exe-side copy being fresh.
  set_tests_properties(cd_test_sample_sponza_golden PROPERTIES
    FIXTURES_REQUIRED HELLO_ENGINE_SHADERS_FRESH)
endif()
