# engine/render/restir_di — cd::restir_di (Day 6/K + phase 550 Sprint-1)
# Bitterli 2020 ReSTIR direct illumination — per-pixel WRS reservoir
# + temporal/spatial reuse primitives. CPU reference + GLSL helpers
# (header-only, in include/cd/restir_di/Reservoir.hpp + TemporalBuffer.hpp)
# plus the GPU initial-candidate dispatch pass (src/DispatchPass.cpp).
#
# Phase 550 promotes the library from INTERFACE to STATIC so the new
# DispatchPass.cpp can compile against cd::rhi + cd::shader without
# leaking those deps to consumers that only need the CPU/GLSL helpers
# (they still link cd::restir_di, just transitively pull cd::rhi /
# cd::shader, both already engine baseline libraries).
cd_add_library(restir_di
  TYPE STATIC
  # cd::shader (glslang) is EXCLUDE_FROM_INSTALL because of vendored FetchContent
  # deps; transitively any library that depends on it must also opt out of the
  # install export set, otherwise CMake refuses to generate. Mirrors cd::ddgi
  # (phase549 DDGI Sprint-1) which made the same conversion.
  EXCLUDE_FROM_INSTALL
  SOURCES
    src/DispatchPass.cpp
    src/Denoiser.cpp
    src/SvgfDenoiser.cpp
    src/FullSvgfPipeline.cpp
    src/FullPipelineDenoised.cpp
  PUBLIC_DEPS
    cd::core
    cd::math
    cd::rhi
  PRIVATE_DEPS
    cd::shader
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
