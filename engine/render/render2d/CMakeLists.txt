# engine/render/render2d — cd::render2d
# FAZ 34 (ADR-20261111) — Modular SOTA 2D Renderer (64-bit Z/Y-Sort, Camera2D,
# SpriteBatch2D, FlipbookAnimator2D, Sparse Wang Tilemap2D + Greedy Mesher,
# Dynamic Soft-Shadow Lighting2D, and .aseprite / .ldtk Direct Loaders)

cd_add_library(render2d
  TYPE STATIC
  SOURCES
    src/TextureAtlas2D.cpp
    src/SpriteBatch2D.cpp
    src/Tilemap2D.cpp
    src/FormatLoaders2D.cpp
    src/Renderer2D.cpp
  PUBLIC_DEPS
    cd::core
    cd::math
    cd::schema
    cd::asset_image
    cd::asset_json
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
