# engine/render/post/exposure — Reinhard log-avg auto-exposure (phase 454)
#
# cd::post_exposure — CPU kernel: log-avg luminance reduction + EMA
# smoothing + EV-to-multiplier conversion. The GPU reduction pass that
# produces the log-avg float belongs in the composite-pass companion;
# this library carries the algorithm and is unit-testable headless.

cd_add_library(post_exposure
  TYPE STATIC
  SOURCES
    src/Exposure.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
