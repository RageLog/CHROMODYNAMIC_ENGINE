# engine/render — render tier (Phase 3)
# Sub-libraries:
#   rhi       — RHI abstraction (header types in S3.0, Vulkan backend in S3.1)
#   shader    — (S3.2+) shader compilation / reflection
#   material  — (S3.3+) material system
#   render    — (S3.4+) high-level renderer

add_subdirectory(rhi)
add_subdirectory(shader)

option(CD_ENABLE_VULKAN "Build cd::rhi Vulkan backend" ON)
if(CD_ENABLE_VULKAN)
  add_subdirectory(rhi/vulkan)
endif()

add_subdirectory(material)
add_subdirectory(light)        # Phase 165 — SOTA light primitives + clustered + CSM
add_subdirectory(camera)
add_subdirectory(framegraph)
add_subdirectory(render)
add_subdirectory(async_submit)
add_subdirectory(ibl)
add_subdirectory(ibl_gpu)
add_subdirectory(post/camera)
add_subdirectory(cluster/core)
add_subdirectory(cluster/gpu)
add_subdirectory(cluster/pbr)
add_subdirectory(volumetric/core)
add_subdirectory(post/tonemap)        # Day 13 — AGX / Hill / Hable / Narkowicz
add_subdirectory(post/bloom)          # Day  8 — Karis 2013 stable bloom
add_subdirectory(post/composite)      # Day 28+— Forward composite (AO/SSR/TAA/DOF/...)
add_subdirectory(post/fx)             # Phase 234 — umbrella aggregating the 11 post-fx libs
add_subdirectory(brdf)                # Phase 234 — umbrella over the 3 BRDF lobe libs
add_subdirectory(volumetric/fx)       # Phase 234 — umbrella over the 3 volumetric libs
add_subdirectory(cluster/fx)          # Phase 234 — umbrella over the 3 cluster libs
add_subdirectory(post/taa)            # Day 11 — Karis 2014 TAA
add_subdirectory(post/gtao)           # Day  4 — Jimenez 2016 GTAO
add_subdirectory(post/ssr)            # Day  5 — Stachowiak 2015 SSR
add_subdirectory(atmosphere)          # Day 15 — Hillaire 2020 sky LUTs
add_subdirectory(volumetric/fog)      # Day 16 — Wronski 2014 froxel fog
add_subdirectory(brdf/ltc)            # Day 19 — Heitz 2016 LTC area lights
add_subdirectory(brdf/sss)            # Day 20 — Burley 2015 SSS + Jimenez blur
add_subdirectory(decal)               # Day 22 — Persson 2009 deferred decals
add_subdirectory(velocity)            # Day 24 — motion-vector pass (TAA prereq)
add_subdirectory(mesh_shader)         # Day 25 — meshlet cluster builder + task/mesh shaders
add_subdirectory(restir_di)           # Day  6 — Bitterli 2020 ReSTIR DI reservoir
add_subdirectory(restir_gi)           # Day  7 — Ouyang 2021 ReSTIR GI bounce reservoir
add_subdirectory(denoise)             # Day 30 — A-trous + OIDN-slot path-trace denoiser
add_subdirectory(post/motion_blur)    # Day  9 — McGuire 2012 tiled-max motion blur
add_subdirectory(post/dof)            # Day 10 — Sousa 2013 hexagonal bokeh DOF
add_subdirectory(brdf/sheen_clearcoat)  # Day 21 — Charlie sheen + Filament clearcoat
add_subdirectory(post/smaa)           # Day 12 — Jimenez 2012 SMAA T2x
add_subdirectory(hdr_display)         # Day 14 — HDR10 PQ + scRGB
add_subdirectory(volumetric/clouds)   # Day 17 — Schneider 2017 cloud ray-march
add_subdirectory(light_shafts)        # Day 18 — Mitchell 2007 god rays
add_subdirectory(gpu_particles)       # Day 23 — compute particles
add_subdirectory(virtual_geometry)    # Day 26 — Karis 2021 Nanite-style DAG
add_subdirectory(virtual_textures)    # Day 27 — Mittring 2008 / Hollander 2013 VT
add_subdirectory(ddgi)                # Day 29 — Majercik 2019 DDGI irradiance probes
add_subdirectory(nrc)                 # Faz 3 M — Muller 2021 Neural Radiance Cache
add_subdirectory(rhi/metal)
add_subdirectory(rhi/d3d12)
add_subdirectory(rhi/opengl)  # Phase 18.B — 3rd API priority (boot-only)
add_subdirectory(rhi/native)
add_subdirectory(spirv_cross_glue)  # B-infra1 — SPIR-V → HLSL/MSL/GLSL cross-compilation
