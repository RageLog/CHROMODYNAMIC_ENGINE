# samples/ — runnable demos, grouped by domain into thematic
# sub-directories (Phase 236 organisation). Each subdir's CMakeLists
# enumerates its own samples; this top-level just dispatches.
#
# samples/
#   foundation/   core/handle/foundation/json/scheduler/bench/stress/flip/...
#   rhi/          triangle/cube/d3d12/opengl/rt/path_trace/rhi_features
#   asset/        gltf/obj/json/cooked/mesh/texture/asset_registry/...
#   render/       pbr/lit_pbr/skybox/framegraph/clustered/compute_cluster/...
#   audio/        audio_chain/audio_play/audio_synth/audio_wasapi/positional_audio
#   world/        anim/ecs/events/inspector/scene_graph/replication/particles
#   net/          net_sim/udp
#   editor/       editor/hot_reload/runtime
#   script/       script
#   lib_smokes/   CPU parity smokes for the queued render libs (Phase 232-233)
#   engine/       hello_engine — the mega-showcase

# samples/common/ is a header-only helper folder. Inject its include
# path via an INTERFACE library so every cd_sample_* target picks it
# up without each CMakeLists having to repeat the path.
add_library(cd_sample_common INTERFACE)
target_include_directories(cd_sample_common INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(cd_sample_common INTERFACE
  cd::asset_image
  cd::imgdiff
  cd::rhi
)
add_library(cd::sample_common ALIAS cd_sample_common)

# Headless samples — no platform or backend gating needed.
add_subdirectory(foundation)
add_subdirectory(asset)
add_subdirectory(audio)
add_subdirectory(world)
add_subdirectory(net)
add_subdirectory(script)
add_subdirectory(lib_smokes)
add_subdirectory(game)     # phase479 — G1 closeout: hello_game (time + input + save + settings)
add_subdirectory(physics)  # phase530 — T3.3: 5 physics demos (box/pile/ragdoll/character/raycast)

# Render-side samples — gated by Vulkan availability since they boot
# the cd::rhi_vulkan backend.
if(CD_ENABLE_VULKAN)
  add_subdirectory(rhi)
  add_subdirectory(render)
  add_subdirectory(editor)
  add_subdirectory(engine)
  add_subdirectory(ui)     # phase463 — ADR-20260530 Phase 1.5 hello_ui demo
endif()
