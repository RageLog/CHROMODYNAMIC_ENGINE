# samples/audio — synth / play / DSP-chain / spatial audio samples.
#
# phase906-sample-consolidation: per
# [[feedback-sample-consolidation-and-per-feature-demo]], the headless
# audio "console proof" smokes are folded into the source libraries'
# gtests. WASAPI sample is the only one that exercises a real platform
# audio device (category c — keep as standalone tool).
#
# Retired in phase 906:
#   * hello_audio_synth        → engine/asset/tests/wav/test_asset_wav.cpp
#     (9 cases incl. RIFF/WAVE round-trip parity).
#   * hello_audio_play         → engine/world/audio/tests/test_audio.cpp
#     (89 cases cover Mixer + Voice + FileSinkBackend render loop).
#   * hello_audio_chain        → engine/world/audio/tests/test_audio.cpp
#     + engine/world/audio_dsp_fx/tests/test_dsp_fx.cpp
#     (mixer + compressor + reverb + lowpass + limiter all covered).
#   * hello_audio_dsp_fx       → engine/world/audio_dsp_fx/tests/test_dsp_fx.cpp
#     (23 cases incl. LowPass / HighPass / DelayLine RMS).
#   * hello_audio_reverb       → engine/world/audio_dsp_fx/tests/test_dsp_fx.cpp
#     (Schroeder reverb RT60 + comb + allpass invariants).
#   * hello_positional_audio   → engine/world/audio_spatial/tests/test_audio_spatial.cpp
#     (20 cases) + test_hrtf_mixer.cpp (31 cases — ITD/ILD/HRTF).
# Source directories deleted in the same commit.
#
# Retained:
#   * hello_audio_wasapi — only sample that exercises the WASAPI
#     shared-mode backend on a real audio device. Falls back to
#     FileSink off-Windows. Category (c) standalone tool.
#
# The "demo each library feature" half of the feedback is handled
# by adding a Sound panel to hello_engine in phase 907.
add_subdirectory(hello_audio_wasapi)
