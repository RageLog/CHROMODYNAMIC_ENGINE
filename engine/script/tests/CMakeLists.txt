cd_add_test(script
  SOURCES test_script.cpp
  DEPS    cd::script cd::core
)

# phase467: math + ecs + scene Lua bindings.
cd_add_test(lua_bindings
  SOURCES test_lua_bindings.cpp
  DEPS    cd::script cd::core cd::math cd::ecs cd::scene
)
