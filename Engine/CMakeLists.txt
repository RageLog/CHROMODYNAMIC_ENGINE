# engine/ — top-level engine subtree
# Library DAG order (bottom-up):
#   foundation  → mem, concurrency, time, io, serialization, log, events, diag, profile,
#                 platform, plugin, config (Sprint S2.1+)
#   render      → rhi, shader, material, render (Sprint S2.8+)
#   world       → ecs, scene, physics, anim, audio, input, net (Sprint S2.14+)
#   ui          → ui, editor_ui, editor (Phase 4)
#   runtime     → compose layer (final)

add_subdirectory(foundation)
add_subdirectory(asset)
add_subdirectory(asset_image)
add_subdirectory(asset_gltf)
add_subdirectory(render)
add_subdirectory(world)
add_subdirectory(ui)
add_subdirectory(runtime)
# Phase 2 closed at S2.10 (foundation + runtime).
# Phase 3 starts at S3.0 with engine/render/rhi header types.
# S3.2 adds engine/asset.
