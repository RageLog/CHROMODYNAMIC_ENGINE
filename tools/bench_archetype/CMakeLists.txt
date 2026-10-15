add_executable(cd_bench_archetype main.cpp)
cd_apply_warnings(cd_bench_archetype)
cd_apply_sanitizers(cd_bench_archetype)
target_link_libraries(cd_bench_archetype PRIVATE
  cd::ecs
  cd::core
)
set_target_properties(cd_bench_archetype PROPERTIES FOLDER "tools")
