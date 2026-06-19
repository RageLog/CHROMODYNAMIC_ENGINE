cd_add_test(ecs
  SOURCES test_ecs.cpp test_query_cache.cpp test_scheduler.cpp test_ecs_edge.cpp
  DEPS    cd::ecs cd::core
)

# Marathon Run 27 X7 slice: spatial sphere-query proof on cd::ecs::World.
cd_add_test(sphere_query
  SOURCES test_sphere_query.cpp
  DEPS    cd::ecs cd::core
)

# Marathon Run 30 X7B slice: archetype-table side-layer proof.
cd_add_test(archetype_world
  SOURCES test_archetype_world.cpp
  DEPS    cd::ecs cd::core
)
