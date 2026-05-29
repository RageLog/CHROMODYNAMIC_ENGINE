# engine/world/sample_framework — Mega-Marathon M2A skeleton
#
# ADR-20260529-M2-sample-framework prescribes a 5-sub-phase build-out
# (M2A skeleton -> M2B device boot -> M2C frame loop -> M2D hello_engine
# port -> M2E hello_engine_lite). This CMakeLists wires the M2A slice:
# the App virtual base class + the run<App>() entry template + a
# trivial gtest that proves on_boot/on_frame/on_shutdown sequencing.
#
# Subsequent M2B/M2C runs extend the source list with the window /
# device / swapchain / shader-watcher / IBL / ImGui Impl.
cd_add_library(sample_framework
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
