# engine/render — render tier (Phase 3)
# phase431: engine/render/render/ flattened into this umbrella.
#
# cd::render — Sprint S3.4: high-level renderer that orchestrates per-frame
#   command buffers, sync primitives, swapchain acquire/present, and implicit
#   image-layout transitions. Sits on top of cd::rhi only.
#
# Sub-libraries:
#   rhi       — RHI abstraction (header types in S3.0, Vulkan backend in S3.1)
#   shader    — (S3.2+) shader compilation / reflection
#   material  — (S3.3+) material system

add_subdirectory(rhi)
add_subdirectory(shader)
add_subdirectory(shader_lib)    # phase1133 SL-C: SOTA shader module library (ADR-20260612)

add_subdirectory(material)
add_subdirectory(light)        # Phase 165 — SOTA light primitives + clustered + CSM
add_subdirectory(camera)
add_subdirectory(framegraph)

cd_add_library(render
  TYPE STATIC
  SOURCES
    src/Renderer.cpp
  PUBLIC_DEPS
    cd::rhi
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
add_subdirectory(async_submit)
add_subdirectory(ibl)
add_subdirectory(ibl_gpu)
add_subdirectory(light_shafts)        # Day 18 — Mitchell 2007 god rays (must precede post/post_fx dep)
add_subdirectory(cluster)             # phase420: consolidated (cluster/core + gpu + pbr + fx)
add_subdirectory(volumetric)          # phase420: consolidated (volumetric/core + fog + clouds + fx)
add_subdirectory(post)                # phase420: consolidated (all post sub-libs + fx umbrella)
add_subdirectory(brdf)                # phase420: consolidated (brdf_ltc + sheen_clearcoat + sss + umbrella)
add_subdirectory(atmosphere)          # Day 15 — Hillaire 2020 sky LUTs
add_subdirectory(decal)               # Day 22 — Persson 2009 deferred decals
add_subdirectory(velocity)            # Day 24 — motion-vector pass (TAA prereq)
add_subdirectory(debug_line)          # phase1030 — CPU line-batch builder (debug viz)
add_subdirectory(debug_draw)          # phase1057 — GPU line renderer (pipeline + VB + flush)
add_subdirectory(mesh_shader)         # Day 25 — meshlet cluster builder + task/mesh shaders
add_subdirectory(restir_di)           # Day  6 — Bitterli 2020 ReSTIR DI reservoir
add_subdirectory(restir_gi)           # Day  7 — Ouyang 2021 ReSTIR GI bounce reservoir
add_subdirectory(denoise)             # Day 30 — A-trous + OIDN-slot path-trace denoiser
add_subdirectory(hdr_display)         # Day 14 — HDR10 PQ + scRGB
add_subdirectory(gpu_particles)       # Day 23 — compute particles
add_subdirectory(virtual_geometry)    # Day 26 — Karis 2021 Nanite-style DAG
add_subdirectory(virtual_textures)    # Day 27 — Mittring 2008 / Hollander 2013 VT
add_subdirectory(ddgi)                # Day 29 — Majercik 2019 DDGI irradiance probes
add_subdirectory(nrc)                 # Faz 3 M — Muller 2021 Neural Radiance Cache
# rhi backends now consolidated inside rhi/CMakeLists.txt (phase418)
add_subdirectory(spirv_cross_glue)  # B-infra1 — SPIR-V → HLSL/MSL/GLSL cross-compilation
add_subdirectory(scene_ingest)      # phase439 — generic glTF → GPU + ECS (ADR-20260530)
add_subdirectory(lighting_clusters) # phase660 — Forward+ froxel grid CPU assignment (Olsson 2012)
