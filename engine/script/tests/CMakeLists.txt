cd_add_test(script
  SOURCES test_script.cpp
  DEPS    cd::script cd::core
)

# 80->100 marathon: Engine edge + negative coverage (moved-from, empty/large/
# re-entrant scripts, run_file runtime-error split, cap on call paths).
cd_add_test(script_edge
  SOURCES test_script_edge.cpp
  DEPS    cd::script cd::core
)

# phase467: math + ecs + scene Lua bindings.
cd_add_test(lua_bindings
  SOURCES test_lua_bindings.cpp
  DEPS    cd::script cd::core cd::math cd::ecs cd::scene
)

# 80->100 marathon: bindings edge + negative coverage (nil/message contract,
# dead/invalid entity, Vec4f/Mat4f metamethods, handler-table edges, null-guards).
cd_add_test(lua_bindings_edge
  SOURCES test_lua_bindings_edge.cpp
  DEPS    cd::script cd::core cd::math cd::ecs cd::scene
)
