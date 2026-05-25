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
  add_subdirectory(rhi_vulkan)
endif()

add_subdirectory(material)
add_subdirectory(light)        # Phase 165 — SOTA light primitives + clustered + CSM
add_subdirectory(camera)
add_subdirectory(framegraph)
add_subdirectory(render)
add_subdirectory(async_submit)
add_subdirectory(ibl)
add_subdirectory(cluster)
add_subdirectory(cluster_gpu)
add_subdirectory(cluster_pbr)
add_subdirectory(volumetric)
add_subdirectory(post_tonemap)        # Day 13 — AGX / Hill / Hable / Narkowicz
add_subdirectory(post_bloom)          # Day  8 — Karis 2013 stable bloom
add_subdirectory(post_taa)            # Day 11 — Karis 2014 TAA
add_subdirectory(post_gtao)           # Day  4 — Jimenez 2016 GTAO
add_subdirectory(post_ssr)            # Day  5 — Stachowiak 2015 SSR
add_subdirectory(atmosphere)          # Day 15 — Hillaire 2020 sky LUTs
add_subdirectory(volumetric_fog)      # Day 16 — Wronski 2014 froxel fog
add_subdirectory(brdf_ltc)            # Day 19 — Heitz 2016 LTC area lights
add_subdirectory(brdf_sss)            # Day 20 — Burley 2015 SSS + Jimenez blur
add_subdirectory(decal)               # Day 22 — Persson 2009 deferred decals
add_subdirectory(velocity)            # Day 24 — motion-vector pass (TAA prereq)
add_subdirectory(rhi_metal)
add_subdirectory(rhi_d3d12)
add_subdirectory(rhi_opengl)  # Phase 18.B — 3rd API priority (boot-only)
add_subdirectory(rhi_native)
