# samples/rhi — RHI backend smokes (Vulkan / D3D12 / OpenGL).
# Outer CMake already gated this directory on CD_ENABLE_VULKAN, so
# triangle/path-trace land here.
# phase1142: hello_cube folded (batch-2 sample consolidation).
add_subdirectory(hello_triangle)

if(WIN32)
  # phase909-sample-consolidation: hello_d3d12_boot retired. Its 4-step
  # smoke (create_d3d12_device + adapter_name + Backend::kD3D12 +
  # wait_idle) is fully covered by GTEST_SKIP-on-no-adapter prologues
  # in engine/render/rhi/tests/test_d3d12_mesh_shader.cpp (9) +
  # test_d3d12_parity_m4.cpp (14). Source directory deleted.
  # phase1152: hello_d3d12_clear + hello_d3d12_triangle folded into
  # hello_d3d12_pbr — it carries the same Win32 window + device + swapchain +
  # clear path AND the PSO/draw/HLSL-compile path, with richer descriptors
  # (docs/D3D12_PARITY_AUDIT.md). Both retired as redundant.
  # phase1149: hello_opengl_resources + hello_opengl_triangle folded into
  # hello_opengl (sample consolidation plan #6) — resource smoke (exit-code
  # gated) runs first, then the Phase 167 visible triangle.
  add_subdirectory(hello_opengl)
endif()

# RT samples need Vulkan ray-query — same gate as the parent. Listed
# here for visibility; if you build with CD_ENABLE_VULKAN=OFF this
# whole subdirectory is skipped.
# hello_rt_check (Phase 14.G v0.40.0 API-surface probe) was deleted in
# phase411-D-F5. phase1142: hello_rt folded (batch-2 sample
# consolidation) — BLAS/TLAS create, RT pipeline, SBT and dispatch_rays
# coverage lives in hello_path_trace + engine/render/rhi/tests.
add_subdirectory(hello_path_trace)

# hello_metal — M12 (B2 — ADR-20260615): a WINDOWED Metal host on Apple
# (NSWindow + CAMetalLayer + clear/draw/present loop), with a Windows-compilable
# #else stub. The TARGET builds on EVERY platform now (the sample's main.cpp
# gates the Apple-only code behind `#if defined(__APPLE__) && CD_RHI_METAL_ENABLED`
# and links cd::rhi_metal, which is the kBackendInitFailed stub off-Apple). On
# Apple the sample's CMakeLists flips main.cpp to Objective-C++ + links the
# Cocoa/Metal/QuartzCore frameworks. Win11 stays clean: the #else stub compiles.
add_subdirectory(hello_metal)
