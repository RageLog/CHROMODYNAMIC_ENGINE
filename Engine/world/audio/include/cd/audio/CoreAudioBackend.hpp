// =============================================================================
// CHROMODYNAMIC — cd/audio/CoreAudioBackend.hpp
// Phase 5 / S4.x — macOS CoreAudio backend stub.
//
// API parity with WasapiBackend so portable code can just call the
// `make_native_audio_backend()` factory in NativeBackend.hpp and get
// the right thing on every host. The macOS implementation hooks into
// AudioUnit (kAudioUnitSubType_DefaultOutput) for shared-mode mixed
// output; the buffer-fill render callback streams interleaved float
// samples in the same internal layout used by WASAPI.
//
// CURRENT STATUS: factory always returns nullptr. The real AudioUnit
// wire-up is deferred until the engine has a macOS CI runner — the
// header lands now so portable callers and tests can reference the
// symbol unconditionally.
// =============================================================================
#pragma once

#include <cd/audio/IAudioBackend.hpp>

#include <memory>

namespace cd::audio
{

/// Build a CoreAudio (AudioUnit DefaultOutput) backend. Returns nullptr if:
///   * Built on a non-Apple platform.
///   * AudioComponentInstanceNew fails for kAudioUnitSubType_DefaultOutput.
///   * The render callback cannot be installed.
///
/// Until the AudioUnit implementation lands, the factory unconditionally
/// returns nullptr on every platform. Callers fall back to the file-sink
/// or null backend (see NativeBackend.hpp for the canonical fallback chain).
[[nodiscard]] std::unique_ptr<IAudioBackend> make_coreaudio_backend();

}  // namespace cd::audio
