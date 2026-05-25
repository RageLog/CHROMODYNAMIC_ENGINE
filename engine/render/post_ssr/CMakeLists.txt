# engine/render/post_ssr — cd::post_ssr (Day 5)
# Stachowiak 2015 / McGuire 2014 screen-space reflections.
cd_add_library(post_ssr
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
