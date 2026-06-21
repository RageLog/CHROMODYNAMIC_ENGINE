cd_add_test(runtime
  SOURCES test_engine_context.cpp
  DEPS    cd::runtime cd::core cd::asset cd::concurrency cd::diag cd::log cd::vfs
)
