# engine/render/post — post-processing sub-libraries (phase420)
#
# Layout:
#   include/cd/post/<sub>/   — public headers
#   tests/<sub>/             — per-sub tests
#
# Targets: cd::post_bloom cd::post_camera cd::post_composite cd::post_dof
#          cd::post_fx cd::post_gtao cd::post_motion_blur cd::post_smaa
#          cd::post_ssr cd::post_taa cd::post_tonemap

cd_add_library(post_bloom
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::math cd::framegraph cd::rhi
)

cd_add_library(post_camera
  TYPE INTERFACE
  PUBLIC_DEPS cd::core
)

cd_add_library(post_composite
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::math
)

# phase691 — M14 W4 — composite consumes DDGI + ReSTIR FullPipelines.
#
# Two opt-in compile-defs that propagate to every consumer of
# `cd::post_composite`:
#   * CD_COMPOSITE_USE_DDGI   — when ON the composite FS samples the DDGI
#                               indirect-irradiance output (binding 6) and
#                               adds it to the lit colour.
#   * CD_COMPOSITE_USE_RESTIR — when ON the composite FS samples the
#                               ReSTIR-denoised direct-light output
#                               (binding 7) and adds it to the lit colour.
#
# DEFAULT OFF — the default-OFF flags ship the full GI/RT chain ready to
# wire BUT do not change rendered output until a graphics dev opts in.
# Defining the macro on the INTERFACE target propagates to BOTH the C++
# consumers (so they can `#ifdef` their CPU-side bind code) AND the GLSL
# preprocessor when the composite FS source is preprocessed via
# `cd::post::composite::make_composite_fs_source_with_gi_hooks(...)`.
option(CD_COMPOSITE_USE_DDGI   "phase691: composite samples DDGI indirect irradiance"   OFF)
option(CD_COMPOSITE_USE_RESTIR "phase691: composite samples ReSTIR denoised direct light" OFF)

if(CD_COMPOSITE_USE_DDGI)
  target_compile_definitions(cd_post_composite INTERFACE CD_COMPOSITE_USE_DDGI=1)
endif()
if(CD_COMPOSITE_USE_RESTIR)
  target_compile_definitions(cd_post_composite INTERFACE CD_COMPOSITE_USE_RESTIR=1)
endif()

cd_add_library(post_dof
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::math
)

cd_add_library(post_gtao
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::math
)

cd_add_library(post_motion_blur
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::math
)

cd_add_library(post_smaa
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::math
)

cd_add_library(post_ssr
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::math
)

cd_add_library(post_taa
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::math
)

cd_add_library(post_tonemap
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::math
)

cd_add_library(post_fx
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::post_bloom
    cd::post_camera
    cd::post_composite
    cd::post_dof
    cd::post_gtao
    cd::post_motion_blur
    cd::post_smaa
    cd::post_ssr
    cd::post_taa
    cd::post_tonemap
    cd::light_shafts
)

# phase454 — auto-exposure: pure-CPU kernel (log-avg + EMA smoothing),
# GPU reduction queued for the composite-pass companion.
add_subdirectory(exposure)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests/bloom)
  add_subdirectory(tests/composite)
  add_subdirectory(tests/dof)
  add_subdirectory(tests/fx)
  add_subdirectory(tests/gtao)
  add_subdirectory(tests/motion_blur)
  add_subdirectory(tests/smaa)
  add_subdirectory(tests/ssr)
  add_subdirectory(tests/taa)
  add_subdirectory(tests/tonemap)
endif()
