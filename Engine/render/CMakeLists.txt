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
add_subdirectory(camera)
add_subdirectory(framegraph)
add_subdirectory(render)
