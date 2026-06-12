# samples/rhi — RHI backend smokes (Vulkan / D3D12 / OpenGL).
# Outer CMake already gated this directory on CD_ENABLE_VULKAN, so
# triangle/cube/path-trace/RT/rhi_features land here.
add_subdirectory(hello_triangle)
add_subdirectory(hello_cube)

if(WIN32)
  # phase909-sample-consolidation: hello_d3d12_boot retired. Its 4-step
  # smoke (create_d3d12_device + adapter_name + Backend::kD3D12 +
  # wait_idle) is fully covered by GTEST_SKIP-on-no-adapter prologues
  # in engine/render/rhi/tests/test_d3d12_mesh_shader.cpp (9) +
  # test_d3d12_parity_m4.cpp (14). Source directory deleted.
  add_subdirectory(hello_d3d12_clear)
  add_subdirectory(hello_d3d12_triangle)
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
