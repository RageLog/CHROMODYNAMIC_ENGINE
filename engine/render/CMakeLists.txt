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
add_subdirectory(rhi_metal)
add_subdirectory(rhi_d3d12)
add_subdirectory(rhi_opengl)  # Phase 18.B — 3rd API priority (boot-only)
add_subdirectory(rhi_native)
