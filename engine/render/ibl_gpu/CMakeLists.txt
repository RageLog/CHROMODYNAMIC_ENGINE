# engine/render/ibl_gpu — cd::ibl_gpu
#
# GPU upload of cd::ibl CPU-baked products (cubemaps, prefiltered
# specular, BRDF LUT). Header-only; depends on cd::ibl (the CPU bake)
# + cd::rhi (the device + texture creation API).

cd_add_library(ibl_gpu
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::ibl
    cd::rhi
)

if(CD_ENABLE_TESTING)
  # No CTest target yet — GPU upload requires a live device; lands
  # with the renderer-integration suite.
endif()
