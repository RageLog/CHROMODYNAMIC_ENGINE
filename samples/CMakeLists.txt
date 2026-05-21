# samples/ — runnable demos per phase milestone
# Sprint S2.0: hello_core (cd::core link + Result roundtrip)
# Sprint S2.10: hello_triangle (RHI Vulkan MVP)
# Sprint S2.20: hello_asset (gltf import → cdmesh)
# Sprint S2.24: hello_scene (end-to-end Phase 2 demo)

add_subdirectory(hello_core)
add_subdirectory(hello_handle)
add_subdirectory(hello_foundation)
add_subdirectory(hello_runtime)

# hello_triangle requires the Vulkan backend; gate so the rest of the
# engine still builds when CD_ENABLE_VULKAN=OFF.
if(CD_ENABLE_VULKAN)
  add_subdirectory(hello_triangle)
  add_subdirectory(hello_mesh)
  add_subdirectory(hello_cube)
  add_subdirectory(hello_texture)
  add_subdirectory(hello_gltf)
endif()
