# tools/bench_compare — diff two cd::bench JSON artifacts
#
# Reads two `[Report, Report, ...]` JSON files emitted by hello_bench
# (or any consumer of cd::bench::reports_to_json_array) and prints a
# Markdown comparison table with per-bench mean-ns deltas and a simple
# regression / improvement classification.

add_executable(cd_bench_compare main.cpp)
target_link_libraries(cd_bench_compare PRIVATE
  cd::asset_json
  cd::bench
  cd::core
)
target_compile_features(cd_bench_compare PUBLIC cxx_std_23)
cd_apply_warnings(cd_bench_compare)
cd_apply_sanitizers(cd_bench_compare)
cd_apply_coverage(cd_bench_compare)
set_target_properties(cd_bench_compare PROPERTIES
  FOLDER "tools"
  OUTPUT_NAME "cd_bench_compare"
)
