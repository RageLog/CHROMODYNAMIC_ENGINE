# engine/world/app_framework — Application lifecycle library (cd::app::App)
cd_add_library(app_framework
  TYPE STATIC
  SOURCES
    src/App.cpp
    src/run.cpp
  PUBLIC_DEPS
    cd::core
)
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
