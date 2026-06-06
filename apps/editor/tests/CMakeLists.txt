# =============================================================================
# apps/editor/tests — editor binary unit tests
#
# phase775 / FINALE-8 W1B H2 — splash audio cue synthetic test.
# Verifies that SplashAudioCue::synthesize() produces non-zero PCM at the
# expected timing, and that trigger_if_needed() fires exactly once.
#
# This target is independent of the Vulkan gate (it only depends on
# cd::audio which is always available); gated on CD_ENABLE_TESTING via
# cd_add_test's internal guard.
# =============================================================================

cd_add_test(splash_audio_cue
  SOURCES test_splash_audio_cue.cpp
  DEPS    cd::audio cd::core
)

# The test's source includes ../SplashAudioCue.hpp from the parent directory.
# cd_add_test creates target cd_test_splash_audio_cue; add the parent dir to
# its include path so the relative #include resolves correctly.
if(TARGET cd_test_splash_audio_cue)
  target_include_directories(cd_test_splash_audio_cue
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..
  )
  set_target_properties(cd_test_splash_audio_cue PROPERTIES
    FOLDER "tests/splash_audio_cue"
  )
endif()

# phase785 / E5 — input recorder editor binding smoke test.
# Verifies: Recorder start/stop, save_to_file + Replayer load_from_file round-
# trip, panel draw with live bindings, OSEvent→InputEvent translation logic,
# and the was_recording stop-detection state machine.
# Only depends on cd::game_input_recorder + cd::editor_panel_input_recorder +
# cd::ui_renderer + cd::ui_widgets + cd::core; independent of Vulkan gate.
cd_add_test(input_recorder_editor_binding
  SOURCES test_input_recorder_editor_binding.cpp
  DEPS
    cd::game_input_recorder
    cd::editor_panel_input_recorder
    cd::ui_renderer
    cd::ui_widgets
    cd::core
)

if(TARGET cd_test_input_recorder_editor_binding)
  set_target_properties(cd_test_input_recorder_editor_binding PROPERTIES
    FOLDER "tests/input_recorder_editor_binding"
  )
endif()
