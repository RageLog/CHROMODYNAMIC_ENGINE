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
