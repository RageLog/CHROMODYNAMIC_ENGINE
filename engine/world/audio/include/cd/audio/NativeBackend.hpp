// =============================================================================
// CHROMODYNAMIC — cd/audio/NativeBackend.hpp
// Phase 5 / S4.x — portable audio backend factory.
//
// Returns the best available real-time audio backend for the current host,
// or a deterministic fallback when no platform backend can initialise.
//
// Resolution order:
//   1. Windows: WASAPI shared-mode (cd::audio::make_wasapi_audio_backend)
//   2. macOS:   CoreAudio / AudioUnit (cd::audio::make_coreaudio_backend) — stub
//   3. Linux:   ALSA / PipeWire shim (cd::audio::make_alsa_backend)     — stub
//   4. Fallback: null backend — accepts everything, plays nothing. Tests
//      and headless tools rely on this so `make_native_audio_backend()`
//      never returns nullptr; callers always get something with the same
//      surface.
//
// Why one factory? Sample code, tests, and downstream tools shouldn't
// each have to fan out `#if defined(_WIN32) / __APPLE__ / __linux__`
// chains. They call this and trust the result.
// =============================================================================
#pragma once

#include <cd/audio/IAudioBackend.hpp>

#include <memory>

namespace cd::audio
{

/// Description of which backend the factory ended up returning. Useful
/// for sample/UI code that wants to print "running on WASAPI" or fall
/// back to a file-sink backend if the host had no real-time output.
enum class NativeBackendKind : std::uint8_t
{
    kWasapi,
    kCoreAudio,
    kAlsa,
    kNullFallback,
};

struct NativeBackendResult
{
    std::unique_ptr<IAudioBackend> backend;
    NativeBackendKind kind { NativeBackendKind::kNullFallback };
};

/// Best-available native audio backend, with a guaranteed non-null
/// fallback to the null backend. `kind` records which path was taken.
[[nodiscard]] NativeBackendResult make_native_audio_backend();

}  // namespace cd::audio
