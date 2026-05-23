# engine/render/framegraph — cd::framegraph (Sprint S3.10)
#
# Declarative per-frame graph: passes declare reads/writes, the compiler
# materializes transient resources and inserts barriers, execute() drives
# the command buffer.
#
# Depends on cd::rhi only — the graph is renderer-agnostic; cd::render
# (and future high-level systems) compose with it but do not own it.

cd_add_library(framegraph
  TYPE STATIC
  SOURCES
    src/FrameGraph.cpp
  PUBLIC_DEPS
    cd::rhi
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
