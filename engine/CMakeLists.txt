# engine/ — top-level engine subtree
# Library DAG order (bottom-up):
#   foundation  → core, mem, math, concurrency, time, io, log, events, diag,
#                 profile, platform, plugin, config, vfs, bench
#   asset       → asset, asset_image, asset_obj, asset_gltf, asset_cdmesh,
#                 asset_cdtex, asset_ktx2, asset_wav, asset_json, asset_pak
#   render      → rhi, rhi_vulkan, rhi_d3d12, rhi_metal, rhi_native,
#                 shader, material, render, framegraph, camera,
#                 imgui_backend, async_submit, cluster, cluster_gpu,
#                 cluster_pbr, ibl, volumetric
#   world       → ecs, scene, physics, anim, audio, input, net
#   ui          → ui, editor_ui, editor
#   runtime     → compose layer (final)
#   cross-cutting → imgdiff (test infra), script (Lua tier)

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
add_subdirectory(asset_streaming)   # Day 28 — priority-queue async I/O
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
