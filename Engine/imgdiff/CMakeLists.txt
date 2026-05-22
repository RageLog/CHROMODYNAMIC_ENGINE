# engine/imgdiff — cd::imgdiff
#
# Golden-image pixel diff utility. Header-only; cd::core only so any
# library's tests can adopt it without dragging extra deps in.

cd_add_library(imgdiff
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
