# engine/world — Phase 4 world tier
# Sub-libraries (in build / dependency order):
#   ecs              — Sprint S4.1: entity-component-system (sparse-set storage).
#   scene            — S4.2 (planned): scene graph / transforms on top of ECS.
#   physics          — S4.3 (planned).
#   physics_jolt     — Phase 525 / T0.3: Jolt backend (cd::physics_jolt::JoltWorld)
#                      implementing cd::physics::IPhysicsWorld. Stub scope until
#                      ADR-2026xxxx-physics-jolt-bringup lands the real vendor.
#   anim             — S4.4 (planned).
#   audio            — S4.5 (planned).
#   audio_dsp_fx     — Phase 562: cd::audio::dsp_fx Sprint-1 (BiquadCoeffs, LowPass, HighPass, DelayLine, Reverb stub).
#   input            — S4.6 (planned).
#   net              — S4.7 (planned).
#   net_matchmaker   — Phase 563 / Sprint W5B: cd::net::matchmaker Sprint-1 (in-memory).
#   world_container  — gap #18 — World / Project / Level / Layer foundation.
#   sample_framework — Mega-Marathon M2: cd::sample::App lifecycle library.
#   gameplay_time    — Phase 459: cd::gameplay::time::TimeKeeper multi-channel clock.
#   gameplay_input_binding — Phase 462: cd::gameplay::input_binding::ActionMap (action-mapping layer).
#   particle_system        — Phase 564/M3 W5C + Phase 583 rename: cd::particle::system (was particle_system_v2; no v1 ever existed).
#
# Cross-library dependency rule (CLAUDE.md §7): explicit context/registry
# objects only — no global state shared between world libraries.

add_subdirectory(ecs)
add_subdirectory(scene)
add_subdirectory(physics)
add_subdirectory(physics_jolt)
add_subdirectory(anim)
add_subdirectory(audio)
add_subdirectory(audio_dsp_fx)
add_subdirectory(input)
add_subdirectory(net)
add_subdirectory(net_matchmaker)
add_subdirectory(world_container)
add_subdirectory(sample_framework)
add_subdirectory(gameplay_time)
add_subdirectory(gameplay_input_binding)
add_subdirectory(particle_system)
