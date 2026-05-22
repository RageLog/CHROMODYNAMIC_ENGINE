// =============================================================================
// CHROMODYNAMIC — engine/world/audio/src/CoreAudioBackend.cpp
//
// macOS CoreAudio backend stub. See CoreAudioBackend.hpp for the
// roll-out plan. The factory currently returns nullptr on every platform
// so portable code can compile against the symbol without an #ifdef.
// =============================================================================
#include <cd/audio/CoreAudioBackend.hpp>

#include <memory>

namespace cd::audio
{

std::unique_ptr<IAudioBackend> make_coreaudio_backend()
{
#if defined(__APPLE__)
    // Real AudioUnit DefaultOutput wire-up lands when the engine has a
    // macOS CI runner. Returning nullptr keeps the factory contract:
    // callers fall back to the null backend via NativeBackend.hpp.
    return nullptr;
#else
    return nullptr;
#endif
}

}  // namespace cd::audio
