// =============================================================================
// CHROMODYNAMIC — cd/audio/WasapiBackend.hpp
// Phase 5 / S4.5.b — Windows WASAPI shared-mode audio backend.
//
// Real-time audio output on Windows via WASAPI (Windows Audio Session
// API). Runs a dedicated render thread that:
//   1. Calls IAudioClient::Initialize in shared mode (cooperates with
//      the OS mixer, never exclusive — no app can grab the device).
//   2. Acquires the buffer from IAudioRenderClient and mixes every
//      active voice into 32-bit float interleaved samples.
//   3. ReleaseBuffer + wait on the event handle the OS signals when
//      it's time for the next packet (low-latency, no busy poll).
//
// Sample-rate conversion is delegated to WASAPI via
// AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
// so clips at 44100 / 22050 / 11025 Hz all play correctly on a 48 kHz
// device.
//
// Header DOES NOT include <windows.h> or COM headers — the backend's
// concrete type is opaque via std::unique_ptr<IAudioBackend>. Only the
// factory function `make_wasapi_audio_backend()` is exposed.
//
// Build gate: on non-Windows platforms the factory returns nullptr.
// Callers that need cross-platform support should fall back to
// `make_file_sink_audio_backend(...)` (Wave 16) when nullptr is returned.
// =============================================================================
#pragma once

#include <cd/audio/IAudioBackend.hpp>

#include <cstdint>
#include <memory>

namespace cd::audio
{

/// Build a WASAPI shared-mode backend. Returns nullptr if:
///   * Built on a non-Windows platform.
///   * No default render device is present.
///   * IAudioClient::Initialize fails (e.g. driver missing).
///
/// The returned backend owns a background render thread that starts
/// immediately. Destructor signals stop, waits for the thread to
/// finish the current buffer, then unwinds COM.
[[nodiscard]] std::unique_ptr<IAudioBackend> make_wasapi_audio_backend();

}  // namespace cd::audio
