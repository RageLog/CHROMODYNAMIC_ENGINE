# engine/render/post_fx — cd::post_fx (Phase 234 umbrella)
#
# Aggregation lib that pulls in every individual post_* library plus
# light_shafts (which conceptually belongs to the post-fx family).
# Existing samples can keep listing the granular deps; new ones can
# just say `cd::post_fx` and get the full screen-space + composite
# stack in one line.
#
# Member libs (each preserved with its own namespace + header):
#   cd::post_bloom          (Karis 2013 multi-mip)
#   cd::post_camera         (vignette / chromatic aberration / film grain)
#   cd::post_composite      (forward composite shader + Push struct)
#   cd::post_dof            (Sousa 2013 hexagonal bokeh)
#   cd::post_gtao           (Jimenez 2016 horizon scan)
#   cd::post_motion_blur    (McGuire 2012 tiled-max)
#   cd::post_smaa           (Jimenez 2012 SMAA T2x)
#   cd::post_ssr            (Stachowiak 2015 SSR)
#   cd::post_taa            (Karis 2014 TAA + Halton jitter)
#   cd::post_tonemap        (Narkowicz / Hill / Hable / AGX)
#   cd::light_shafts        (Mitchell 2007 god rays)
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

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
