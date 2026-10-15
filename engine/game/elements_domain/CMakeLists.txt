# engine/game/elements_domain — cd::elements_domain & modular domain aliases (FAZ 5 Wave 5.2)

cd_add_library(elements_domain
  TYPE STATIC
  SOURCES
    src/DomainElements.cpp
  PUBLIC_DEPS
    cd::core
    cd::element_core
)

# Expose each domain family as its own CMake alias target so consumers can link
# only the domain family they use (cd::elements_visual, cd::elements_radiance, etc.).
foreach(_dom_alias
    elements_visual
    elements_radiance
    elements_aether
    elements_optics
    elements_nature
    elements_dynamics
    elements_acoustics
    elements_wayfinding
    elements_planar
    elements_ui)
  if(NOT TARGET cd::${_dom_alias})
    add_library(cd::${_dom_alias} ALIAS cd_elements_domain)
  endif()
endforeach()

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
