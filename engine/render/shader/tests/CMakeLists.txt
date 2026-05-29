cd_add_test(shader
  SOURCES test_shader.cpp
  DEPS    cd::shader cd::core
)

# CachedCompiler is a thin disk-IO decorator and benefits from being its
# own gtest binary so a failure there doesn't drag the shader backend's
# test runtime up by ~100 ms of glslang init time.
cd_add_test(cached_compiler
  SOURCES test_cached_compiler.cpp
  DEPS    cd::shader cd::core
)

cd_add_test(file_watcher
  SOURCES test_file_watcher.cpp
  DEPS    cd::shader cd::core
)

# Hot-reload smoke test: mirrors the M1 hello_engine call-site
# (FileWatcher + per-file change callback within a deadline) so a
# regression in the polling path shows up at unit-test scope, not
# only when hello_engine.exe is launched.
cd_add_test(file_watcher_hotreload
  SOURCES test_file_watcher_hotreload.cpp
  DEPS    cd::shader cd::core
)

# Live edit-and-revert smoke (D-F8 / ADR-20260529-X5 deferred path):
# write original → watcher fires; modify → fires; revert → fires.
# Three sequential write events, each fires the watcher within 3 s.
# Headless — no engine launch or window required.
cd_add_test(live_edit_smoke
  SOURCES test_live_edit_smoke.cpp
  DEPS    cd::shader cd::core
)
