// =============================================================================
// CHROMODYNAMIC — engine/world/audio/src/NativeBackend.cpp
//
// Platform-dispatching factory. See NativeBackend.hpp for the
// resolution order. Falls back to the null backend so the result is
// always non-null.
// =============================================================================
#include <cd/audio/AlsaBackend.hpp>
#include <cd/audio/CoreAudioBackend.hpp>
#include <cd/audio/NativeBackend.hpp>
#include <cd/audio/WasapiBackend.hpp>

#include <memory>
#include <utility>

namespace cd::audio
{

NativeBackendResult make_native_audio_backend()
{
#if defined(_WIN32)
    if (auto wasapi = make_wasapi_audio_backend())
        return { std::move(wasapi), NativeBackendKind::kWasapi };
#elif defined(__APPLE__)
    if (auto core = make_coreaudio_backend())
        return { std::move(core), NativeBackendKind::kCoreAudio };
#elif defined(__linux__)
    if (auto alsa = make_alsa_backend())
        return { std::move(alsa), NativeBackendKind::kAlsa };
#endif
    return { make_null_audio_backend(), NativeBackendKind::kNullFallback };
}

}  // namespace cd::audio
