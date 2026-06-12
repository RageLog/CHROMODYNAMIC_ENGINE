# phase1098 — hello_editor <-> hello_engine bridge contract pin.
# Headless: library APIs only (Scene/Serializer/Json/Light).
cd_add_test(hello_editor_bridge_contract
  SOURCES test_bridge_contract.cpp
  DEPS    cd::scene cd::ecs cd::asset_json cd::light cd::math cd::core
)
