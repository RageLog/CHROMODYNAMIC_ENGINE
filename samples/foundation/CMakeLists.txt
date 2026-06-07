# samples/foundation — headless foundation-tier samples (no Vulkan).
#
# phase908-sample-consolidation: per
# [[feedback-sample-consolidation-and-per-feature-demo]], the 11 headless
# "console proof" smokes have all been folded into the foundation
# libraries' gtests. Source directories deleted in this commit.
#
# Retired in phase 908:
#   * hello_core                → engine/foundation/core/tests/test_core.cpp (118 cases)
#   * hello_handle              → engine/foundation/core/tests/test_core.cpp (CoreHandle suite)
#   * hello_foundation          → multi-lib meta-smoke folded across
#       engine/foundation/{core,mem,concurrency,time,platform,diag}/tests/*
#       (each library's own gtest covers its public API).
#   * hello_random              → engine/foundation/math/tests/test_math.cpp (Random suite, PCG32)
#   * hello_watch               → engine/render/shader/tests/test_file_watcher.cpp (5 cases)
#   * hello_bezier              → engine/foundation/math/tests/test_math.cpp (CubicBezier suite)
#   * hello_command_palette     → engine/ui/editor/tests/test_editor.cpp (CommandPalette suite)
#   * hello_bench               → engine/foundation/bench/tests/test_bench.cpp (10 cases)
#   * hello_scheduler           → engine/world/ecs/tests/test_scheduler.cpp (11 cases)
#   * hello_flip                → engine/imgdiff/tests/test_imgdiff.cpp (32 cases)
#   * hello_frame_graph_timeline → engine/foundation/profile_frame_graph_timeline/tests/test_frame_graph_timeline.cpp (7 cases)
#
# Retained:
#   * hello_stress — 24-hour CI stress harness. Category (c) standalone
#     tool — different in kind from short-lived gtests, lives on its own
#     CI hardware budget (see docs/PRODUCTION_V20_PLAN.md v2.0.7).
add_subdirectory(hello_stress)
