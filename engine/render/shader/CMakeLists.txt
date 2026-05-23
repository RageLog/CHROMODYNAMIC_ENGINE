# engine/render/shader — cd::shader (Sprint S3.7)
# ADR-001 — shader compilation surface.
#
# Backend: glslang (Khronos reference) by default. Slang will plug into the
# same `ICompiler` interface in a later sprint.
#
# Toggles:
#   CD_ENABLE_GLSLANG=ON  — fetch + link glslang, full GLSL/HLSL → SPIR-V.
#   CD_ENABLE_GLSLANG=OFF — stub TU; only load_spirv_file() works (lets
#                           consumers ship pre-compiled .spv blobs without
#                           the heavier glslang build cost).

include(FetchContent)

option(CD_ENABLE_GLSLANG "Build cd::shader with the glslang backend" ON)

if(CD_ENABLE_GLSLANG)
  # glslang ships its own CMake; we suppress every optional sub-build we
  # don't need (tests, binaries, SPIRV-Tools optimizer) so the engine build
  # stays lean. ENABLE_HLSL=ON keeps the HLSL frontend (we may compile HLSL
  # later via the same compiler instance).
  set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
  set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
  # We deliberately skip SPIRV-Tools to keep the configure/build lean.
  # glslang will emit the warning:
  #   "spirv-tools not linked - illegal SPIRV may be generated for HLSL"
  # That warning is harmless for the engine's GLSL-only default path; the
  # HLSL frontend is exposed via cd::shader::ShaderLanguage::kHlsl but no
  # current consumer (samples, tests) uses it. Engines that ship HLSL
  # shaders should flip both options ON below and add a FetchContent_Declare
  # for SPIRV-Tools + SPIRV-Headers (versions must match the glslang tag).
  set(ENABLE_OPT OFF CACHE BOOL "" FORCE)            # no SPIRV-Tools optimizer
  set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)        # do not fetch SPIRV-Tools
  set(ENABLE_CTEST OFF CACHE BOOL "" FORCE)
  set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "" FORCE)

  FetchContent_Declare(
    glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG        15.0.0
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(glslang)

  # glslang's targets are vendor C++ — tank our strict warning set on them.
  foreach(_t glslang SPIRV glslang-default-resource-limits MachineIndependent
              GenericCodeGen OSDependent OGLCompiler HLSL SPVRemapper)
    if(TARGET ${_t})
      if(MSVC)
        target_compile_options(${_t} PRIVATE /W0)
      else()
        target_compile_options(${_t} PRIVATE -w)
      endif()
    endif()
  endforeach()
endif()

cd_add_library(shader
  TYPE STATIC
  EXCLUDE_FROM_INSTALL  # vendored glslang/SPIRV via FetchContent
  SOURCES
    src/SpirvFile.cpp
    src/CachedCompiler.cpp
    $<$<BOOL:${CD_ENABLE_GLSLANG}>:${CMAKE_CURRENT_SOURCE_DIR}/src/GlslangCompiler.cpp>
    $<$<NOT:$<BOOL:${CD_ENABLE_GLSLANG}>>:${CMAKE_CURRENT_SOURCE_DIR}/src/GlslangCompilerStub.cpp>
    # Slang backend: stub TU until a focused integration session lands the
    # FetchContent of shader-slang/slang. The factory + interface are
    # already in the public header so consumers can branch on the runtime
    # availability of Slang without recompiling.
    src/SlangCompilerStub.cpp
  PUBLIC_DEPS  cd::core
)

# Forward-compat option. When real Slang integration arrives the body of
# the `if(CD_ENABLE_SLANG)` block will add the FetchContent + replace
# SlangCompilerStub.cpp with the real SlangCompiler.cpp. Today setting it
# to ON has no effect beyond defining CD_SHADER_HAVE_SLANG.
option(CD_ENABLE_SLANG "Build cd::shader with the Slang backend (deferred)" OFF)
if(CD_ENABLE_SLANG)
  target_compile_definitions(cd_shader PRIVATE CD_SHADER_HAVE_SLANG=1)
  message(WARNING
    "CD_ENABLE_SLANG is set but the Slang backend ships only the stub TU "
    "in this build. The factory returns nullptr until the real Slang "
    "FetchContent integration lands.")
endif()

if(CD_ENABLE_GLSLANG)
  # glslang's modern CMake exposes the `glslang` target (front-end) and
  # `SPIRV` (SPV emission). `glslang-default-resource-limits` provides
  # GetDefaultResources() which we use to seed the parser.
  target_link_libraries(cd_shader PRIVATE
    glslang::glslang
    glslang::SPIRV
    glslang::glslang-default-resource-limits
  )
  # Mark vendor includes SYSTEM so warnings from inside glslang headers do
  # not poison cd_shader's strict diagnostic set.
  foreach(_t glslang::glslang glslang::SPIRV glslang::glslang-default-resource-limits)
    if(TARGET ${_t})
      cd_mark_target_system_includes(${_t})
    endif()
  endforeach()
  target_compile_definitions(cd_shader PRIVATE CD_SHADER_HAVE_GLSLANG=1)
endif()

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
