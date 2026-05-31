# engine/foundation/profile_gpu_marker — cd::profile_gpu_marker
#
# Phase 606 — GPU marker recorder (cpu/gpu classification companion to
#             cd::profile_cpu_marker_overlay, Phase 587).
#
# Provides Recorder (begin_marker/end_marker/resolve), RAII Scope, and
# GpuMarkerSample (name + raw ticks + computed duration_ms).
#
# Stub implementation: uses a monotonic counter in place of real GPU
# timestamps until ICommandBuffer::write_timestamp() lands. The library
# calls push_debug_group / pop_debug_group on the supplied ICommandBuffer
# for capture-tool (RenderDoc / PIX / NSight) visibility.
#
# cd::rhi is INTERFACE (header-only); it is a PUBLIC dep because
# GpuMarker.hpp forward-declares cd::rhi::ICommandBuffer and
# cd::rhi::IDevice, and callers must include the RHI headers themselves
# to pass concrete objects. The .cpp includes the RHI headers privately.

cd_add_library(profile_gpu_marker
  TYPE STATIC
  SOURCES
    src/GpuMarker.cpp
  PUBLIC_DEPS
    cd::core
    cd::rhi
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
