# engine/ui/renderer — UI batcher (ADR-20260530, Phase 1.2)
#
# cd::ui_renderer — pure CPU-side draw batcher that converts UI commands
# (quad / textured quad / glyph) into a flat vertex + index buffer pair
# + scissor-keyed DrawCommand list. RHI submission (vertex upload + draw
# recording) lives in cd::ui_renderer_rhi (separate Phase 1.2b lib that
# depends on cd::rhi -- not yet written).

cd_add_library(ui_renderer
  TYPE STATIC
  SOURCES
    src/DrawBatcher.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
