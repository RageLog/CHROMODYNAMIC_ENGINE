# samples/rhi — RHI backend smokes (Vulkan / D3D12 / OpenGL).
# Outer CMake already gated this directory on CD_ENABLE_VULKAN, so
# triangle/cube/path-trace/RT/rhi_features land here.
add_subdirectory(hello_triangle)
add_subdirectory(hello_cube)
add_subdirectory(hello_rhi_features)

if(WIN32)
  add_subdirectory(hello_d3d12_boot)
  add_subdirectory(hello_d3d12_clear)
  add_subdirectory(hello_d3d12_triangle)
  add_subdirectory(hello_opengl_boot)
  add_subdirectory(hello_opengl_resources)
  add_subdirectory(hello_opengl_triangle)
endif()

# RT samples need Vulkan ray-query — same gate as the parent. Listed
# here for visibility; if you build with CD_ENABLE_VULKAN=OFF this
# whole subdirectory is skipped.
# hello_rt_check (Phase 14.G v0.40.0 API-surface probe) was deleted in
# phase411-D-F5: hello_rt (Phase 141+406) covers BLAS/TLAS create, RT
# pipeline, SBT, dispatch_rays, and feature detection with a real scene.
add_subdirectory(hello_rt)
add_subdirectory(hello_path_trace)

# Metal boot smoke — macOS / iOS only; requires CD_RHI_METAL_ENABLED=ON.
# On Win11 / Linux CD_RHI_METAL_ENABLED defaults to OFF so this block
# is skipped and the Win11 build stays clean.
if(CD_RHI_METAL_ENABLED)
  add_subdirectory(hello_metal)
endif()
