# samples/script/hello_hot_reload — Phase 534 (T3.5)
#
# Lua entity behavior with cd::game::asset_hot_reload live re-apply.
#
# Demonstrates:
#   * cd::script::Engine + cd::script::Bindings (Phase 467) — Lua on_tick()
#     callback that reads/writes a cd::scene::LocalTransform each frame.
#   * cd::game::asset_hot_reload::HotReloadBus (Phase 502) — file-system watch
#     on scripts/entity_behavior.lua; on mtime advance the bus fires a reload
#     callback that re-executes the Lua chunk, replacing the old on_tick.
#   * cd::ecs::World + cd::scene::LocalTransform — entity lifecycle and
#     position tracking without any RHI / Vulkan dependency.
#
# Console-only (headless). No platform window, no GPU, no CD_ENABLE_VULKAN
# gate — the sample is portable to every host that can link cd::core.
#
# Scope gate (CLAUDE.md §3): "sample compiles + exit 0" is the CI gate.
#
# CHROMA_SAMPLE_SOURCE_DIR is injected as a compile definition so the binary
# can locate scripts/entity_behavior.lua when run from the build-output dir.

cd_add_sample(hello_script_hot_reload
  SOURCES main.cpp
  DEPS
    cd::core
    cd::math
    cd::ecs
    cd::scene
    cd::script
    cd::game_asset_hot_reload
)

# Inject the source directory so the binary can find the Lua script when run
# from the build output folder (CI / developer build both work).
target_compile_definitions(cd_sample_hello_script_hot_reload PRIVATE
  CHROMA_SAMPLE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# Copy the Lua scripts directory next to the executable so the deployed binary
# can also find the script without compile-definition fallback.
file(COPY scripts DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
