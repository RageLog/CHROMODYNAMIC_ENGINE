# samples/game/hello_world - Phase 504 (G3.5 - closes Phase G3).
#
# Console-only demo that ties together the four Phase-G3 gameplay libraries:
#   * cd::game_query             (Phase 475 / G3.1)
#   * cd::game_trigger           (Phase 476 / G3.2)
#   * cd::game_camera            (Phase 497 / G3.3)
#   * cd::game_particles_event   (Phase 498 / G3.4)
#
# A "player" entity walks the (+X, +Z) diagonal at 1 m/s for 10 simulated
# seconds, crosses a sphere trigger volume @ (5, 0, 5) that prints the
# canonical checkpoint message, follows a critically-damped Cinemachine-style
# vcam, and fires a "jump_dust" particle burst every 2 s to simulate a
# jump-land event. The TimeKeeper drives the loop at a fixed 60 Hz tick.
#
# No platform window, no RHI, no Vulkan - the sample is deliberately portable
# so the cd::game::* family compiles cleanly on every host that links cd::core.
# Per the brief: "Sample compile = the gate; no new ctest required."

cd_add_sample(hello_world
  SOURCES main.cpp
  DEPS
    cd::core
    cd::math
    cd::ecs
    cd::gameplay_time
    cd::game_query
    cd::game_trigger
    cd::game_camera
    cd::game_particles_event
)
