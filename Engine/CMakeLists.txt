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
add_subdirectory(asset_obj)
add_subdirectory(asset_cdmesh)
add_subdirectory(asset_cdtex)
add_subdirectory(asset_ktx2)
add_subdirectory(asset_wav)
add_subdirectory(asset_json)
add_subdirectory(asset_pak)
add_subdirectory(imgdiff)
add_subdirectory(script)
add_subdirectory(render)
if(CD_ENABLE_VULKAN)
  add_subdirectory(imgui_backend)
endif()
add_subdirectory(world)
add_subdirectory(ui)
add_subdirectory(runtime)
# Phase 2 closed at S2.10 (foundation + runtime).
# Phase 3 starts at S3.0 with engine/render/rhi header types.
# S3.2 adds engine/asset.
