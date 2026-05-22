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
