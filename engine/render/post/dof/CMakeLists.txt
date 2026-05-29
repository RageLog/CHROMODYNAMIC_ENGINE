# engine/render/post_dof — cd::post_dof (Day 10)
# Sousa 2013 hexagonal-bokeh depth of field.
cd_add_library(post_dof
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
