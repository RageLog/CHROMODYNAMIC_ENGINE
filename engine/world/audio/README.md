# cd::audio

**Purpose**: cross-platform audio synthesis + playback. WASAPI (Windows), CoreAudio (macOS), ALSA (Linux), OpenSL ES (Android) backends behind a uniform DeviceContext + render-callback API. Header-only synthesis primitives (square wave, noise burst, sine, ADSR envelope, FFT) for the demo + the Audio panel scope visualizer.

**Namespace**: `cd::audio`.

**Headers**: `cd/audio/{DeviceContext,Synthesis,Envelope,Fft,WavWriter}.hpp`.

**Primary types**:
- `cd::audio::DeviceContext` -- opens an output device + schedules the render callback on the audio thread.
- `cd::audio::SquareFn` / `cd::audio::NoiseFn` -- callable synthesis primitives wired into the per-frame tick.
- `cd::audio::Envelope` -- ADSR shaper applied to the synth output.
- `cd::audio::Fft` -- power-of-2 FFT for the scope visualizer.
- `cd::audio::WavWriter` -- offline WAV serialisation for the captured-audio sample.

**Test command**: `ctest --preset ninja-debug -R cd_test_audio --output-on-failure`.

**Notes**:
- hello_engine wraps the audio-chain boot in `cd_sample::init_audio` (HelloAudio.hpp / Marathon Run 11 phase N11).
- Win32 backend is shipped today; macOS / Linux / Android queued behind ADR-pending.
- Synthesis primitives stay header-only so headless tests can simulate device output without opening a real audio interface.
