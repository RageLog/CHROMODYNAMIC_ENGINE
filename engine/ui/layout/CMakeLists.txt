# engine/ui/layout — Flex / Grid / Absolute solver (ADR-20260530, Phase 1.0)
#
# cd::ui::layout — pure-C++ flexbox layout solver. No RHI / no font /
# no input dependencies. Header API + .cpp impl. Yoga-shape semantics
# for callers ported from web / React Native.

cd_add_library(ui_layout
  TYPE STATIC
  SOURCES
    src/Flex.cpp
    src/Constraint.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
