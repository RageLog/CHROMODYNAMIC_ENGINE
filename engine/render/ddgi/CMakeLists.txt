# engine/render/ddgi — Dynamic Diffuse Global Illumination
#
# phase526 shipped CPU-only probe math + GLSL string constants as an
# INTERFACE library (header-only). phase549 promotes the library to STATIC
# and adds DispatchPass.cpp — the GPU-side trace-pass orchestrator that
# binds resources + dispatches kDdgiTraceCS through cd::rhi::IDevice.
#
# Public deps stay header-only (cd::core, cd::math, cd::rhi) so consumers
# pulling in <cd/ddgi/DispatchPass.hpp> don't transitively link glslang.
# The shader compiler is a PRIVATE dep — only the .cpp uses it.
cd_add_library(ddgi
  TYPE STATIC
  # cd::shader (glslang) is EXCLUDE_FROM_INSTALL because of vendored FetchContent
  # deps; transitively any library that depends on it must also opt out of the
  # install export set, otherwise CMake refuses to generate.
  EXCLUDE_FROM_INSTALL
  SOURCES
    src/DispatchPass.cpp
  PUBLIC_DEPS  cd::core cd::math cd::rhi
  PRIVATE_DEPS cd::shader
)
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
