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
add_subdirectory(hello_rt_check)
add_subdirectory(hello_rt)
add_subdirectory(hello_path_trace)
