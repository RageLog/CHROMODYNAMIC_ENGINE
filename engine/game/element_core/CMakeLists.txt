# engine/game/element_core — cd::element_core (FAZ 5 Wave 5.1)
# Original CHROMODYNAMIC Scene Element Hierarchy (Element, Pulse, Planar2D, Spatial3D, Stage)

cd_add_library(element_core
  TYPE STATIC
  SOURCES
    src/Element.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
