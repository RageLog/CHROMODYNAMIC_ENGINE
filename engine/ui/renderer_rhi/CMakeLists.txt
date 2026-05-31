# engine/ui/renderer_rhi — UI batcher → RHI bridge (ADR-20260530, Phase 1.2b)
#
# cd::ui_renderer_rhi — owns ring vertex/index buffers + the descriptor
# set bound to a single atlas texture slot. Takes a cd::ui::renderer::
# DrawBatcher output and submits via cd::rhi::IDevice. The actual UI
# pipeline (shader compile + draw recording) lands with Phase 1.5's
# hello_ui sample; this library shipping the API + lifecycle plumbing
# unblocks downstream work.

cd_add_library(ui_renderer_rhi
  TYPE STATIC
  EXCLUDE_FROM_INSTALL  # private dep on cd::shader (EXCLUDE_FROM_INSTALL via FetchContent glslang)
  SOURCES
    src/Submitter.cpp
  PUBLIC_DEPS
    cd::core
    cd::rhi
    cd::ui_renderer
    # Phase 608 / M7 W3 -- Route A factory `create_with_material_ui_variant`
    # takes a `cd::material::UiVariant&&` by value in the public header
    # (forward-declared) and stores it via std::unique_ptr inside the
    # PIMPL. Callers must `#include <cd/material/UiVariant.hpp>` to
    # construct the variant, so cd::material is PUBLIC so the consumer's
    # include surface resolves cleanly.
    cd::material
  PRIVATE_DEPS
    # Phase 554 / M3 W1A -- Route B inline-shader factory uses
    # cd::shader::make_glslang_compiler() to assemble the fallback
    # pipeline. cd::shader is a private dependency so downstream
    # consumers of cd::ui_renderer_rhi do NOT inherit glslang's
    # transitive include surface.
    cd::shader
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
