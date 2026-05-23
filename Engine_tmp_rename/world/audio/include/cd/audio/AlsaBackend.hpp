// =============================================================================
// CHROMODYNAMIC — cd/audio/AlsaBackend.hpp
// Phase 5 / S4.x — Linux ALSA backend stub.
//
// API parity with WasapiBackend / CoreAudioBackend. The real
// implementation opens snd_pcm_open(..., "default", SND_PCM_STREAM_PLAYBACK)
// in non-blocking mode, snd_pcm_set_params() to interleaved float32 at the
// device's native rate, and a render thread that snd_pcm_writei()s mixed
// voices. PulseAudio / PipeWire are reached through the same ALSA shim on
// modern distros, so a single backend covers the common Linux desktop.
//
// CURRENT STATUS: factory always returns nullptr. The real snd_pcm_*
// wire-up is deferred until the engine has a Linux audio CI runner — the
// header lands now so portable callers and tests can reference the symbol
// unconditionally.
// =============================================================================
#pragma once

#include <cd/audio/IAudioBackend.hpp>

#include <memory>

namespace cd::audio
{

/// Build an ALSA (snd_pcm) backend. Returns nullptr if:
///   * Built on a non-Linux platform.
///   * snd_pcm_open fails for "default".
///   * The render thread cannot be installed.
///
/// Until the ALSA implementation lands, the factory unconditionally
/// returns nullptr on every platform. Callers fall back to the file-sink
/// or null backend (see NativeBackend.hpp for the canonical fallback chain).
[[nodiscard]] std::unique_ptr<IAudioBackend> make_alsa_backend();

}  // namespace cd::audio
