# tools/render_thread_soak — long-running AsyncSubmitN stress harness
#
# Standalone executable (NOT a sample) — it isn't an interactive demo;
# it's the validation rig that proves the N-deep render thread is
# leak-free, deadlock-free, and steady-state under millions of jobs.
#
# Default duration is short (2 s) so CI can run it as part of the smoke
# set. Production validation cranks `--duration-secs=N` up to hours or
# days; the per-second job rate stays the same so multi-day soaks
# produce the same statistical signature as the short runs.

add_executable(cd_render_thread_soak main.cpp)
target_link_libraries(cd_render_thread_soak PRIVATE
  cd::async_submit
  cd::bench
  cd::core
)
target_compile_features(cd_render_thread_soak PUBLIC cxx_std_23)
cd_apply_warnings(cd_render_thread_soak)
cd_apply_sanitizers(cd_render_thread_soak)
cd_apply_coverage(cd_render_thread_soak)
set_target_properties(cd_render_thread_soak PROPERTIES
  FOLDER "tools"
  OUTPUT_NAME "cd_render_thread_soak"
)
