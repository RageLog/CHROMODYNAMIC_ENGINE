# engine/game/gameplay_framework — cd::gameplay_framework & modular aliases (FAZ 5 Wave 5.3)

cd_add_library(gameplay_framework
  TYPE STATIC
  SOURCES
    src/VesselPilotDirector.cpp
  PUBLIC_DEPS
    cd::core
    cd::element_core
    cd::elements_domain
)

foreach(_gp_alias gameplay_vessel gameplay_pilot gameplay_director)
  if(NOT TARGET cd::${_gp_alias})
    add_library(cd::${_gp_alias} ALIAS cd_gameplay_framework)
  endif()
endforeach()

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
