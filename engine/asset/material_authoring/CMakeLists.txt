# engine/asset/material_authoring — cd::asset_material_authoring (Phase 644)
#
# Asset-tier material authoring helper: hand-edit .material.json, validate
# before the asset enters the render pipeline, load in the engine.
#
# Moment: a designer edits a .material.json, validate_authored() catches a
# typo (roughness: 1.5, empty id) before the asset ever hits the GPU — the
# hot-reload path surfaces the diagnostic in real time.
#
# Depends on cd::material (AlphaMode enum) and cd::asset_json (JSON I/O).
# Does NOT depend on cd::rhi — stays in the asset tier.
cd_add_library(asset_material_authoring
  TYPE STATIC
  SOURCES
    src/MaterialAuthoring.cpp
  PUBLIC_DEPS
    cd::core
    cd::material
    cd::asset_json
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
