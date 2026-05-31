# samples/game/hello_world - Phase 523 (T3.1 - visual 3rd-person walk-around).
#
# Replaces the Phase 504 console-only G3 closeout with a visual Vulkan sample.
# All four G3 gameplay libraries remain exercised at the same simulation
# fidelity; a real platform window + Vulkan swapchain + cd::render::Renderer
# is added so the player's walk can be observed on screen.
#
# Gameplay libs (unchanged from Phase 504):
#   * cd::game_query            (Phase 475 / G3.1)
#   * cd::game_trigger          (Phase 476 / G3.2)
#   * cd::game_camera           (Phase 497 / G3.3)
#   * cd::game_particles_event  (Phase 498 / G3.4)
#
# Render additions (Phase 523):
#   * cd::platform    — platform window (Win32/Wayland/Xlib/Cocoa)
#   * cd::rhi         — RHI interface + descriptors
#   * cd::rhi_vulkan  — Vulkan backend (create_vulkan_device)
#   * cd::render      — Renderer (swapchain + begin/end_frame)
#   * cd::camera      — view_projection helper
#   * cd::material    — Material (GLSL shader compilation, pipeline state)
#   * cd::shader      — make_glslang_compiler
#
# Scope-down note: no Sponza, no ImGui, no IBL, no shadow maps.
# Colored cubes per entity + ground quad + present loop is the gate.
# "Sample compile = the gate." — CLAUDE.md §3.
#
# The sample is listed under samples/game/ which is compiled unconditionally,
# but cd::rhi_vulkan is gated by CD_ENABLE_VULKAN in the engine build.
# The CMakeLists below mirrors samples/rhi/hello_cube/CMakeLists.txt which
# already solved this: cd_add_sample links cd::rhi_vulkan; the project-level
# gate (CD_ENABLE_VULKAN) controls whether the rhi_vulkan target exists, and
# the linker will simply fail cleanly on headless CI that skips Vulkan.
#
# Per brief: samples/game/ is unconditionally included (no CD_ENABLE_VULKAN
# guard here). If the project is configured without Vulkan, the link step
# will fail on cd::rhi_vulkan — acceptable because the task requires Vulkan.

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
    cd::physics
    cd::platform
    cd::rhi
    cd::rhi_vulkan
    cd::render
    cd::camera
    cd::material
    cd::shader
)
