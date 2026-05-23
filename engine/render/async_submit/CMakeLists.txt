# engine/render/async_submit — cd::render::AsyncSubmit
#
# Header-only "one frame deep" render-thread primitive. Depends only on
# cd::core for engine-wide types — no Vulkan, no RHI handles. Stand-alone
# library so non-render code can use the same worker-thread pattern.

cd_add_library(async_submit
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
