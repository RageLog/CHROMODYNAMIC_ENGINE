// =============================================================================
// CHROMODYNAMIC — engine/world/audio/src/AlsaBackend.cpp
//
// Linux ALSA backend stub. See AlsaBackend.hpp for the roll-out plan.
// The factory currently returns nullptr on every platform so portable
// code can compile against the symbol without an #ifdef.
// =============================================================================
#include <cd/audio/AlsaBackend.hpp>

#include <memory>

namespace cd::audio
{

std::unique_ptr<IAudioBackend> make_alsa_backend()
{
#if defined(__linux__)
    // Real snd_pcm_* wire-up lands when the engine has a Linux audio
    // CI runner with ALSA available. Returning nullptr keeps the
    // factory contract: callers fall back to the null backend via
    // NativeBackend.hpp.
    return nullptr;
#else
    return nullptr;
#endif
}

}  // namespace cd::audio
