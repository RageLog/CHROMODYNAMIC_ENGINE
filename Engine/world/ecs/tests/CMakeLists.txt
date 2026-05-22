cd_add_test(ecs
  SOURCES test_ecs.cpp test_query_cache.cpp test_scheduler.cpp
  DEPS    cd::ecs cd::core
)
