// =============================================================================
// CHROMODYNAMIC — cd/audio/NullAudioBackend.cpp
//
// Headless backend: accepts every call, plays no audio. The voice state
// machine is real (so callers can write the same code that talks to a real
// driver and have it stay coherent), but no samples reach hardware.
// =============================================================================
#include <cd/audio/IAudioBackend.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::audio
{

namespace
{

struct ClipRec
{
    std::uint32_t channels { 1 };
    std::uint32_t sample_rate { 48000 };
    std::vector<float> samples {};
};

struct VoiceRec
{
    ClipHandle clip {};
    float volume { 1.0F };
    bool looping { false };
    bool playing { true };
};

class NullAudioBackend final : public IAudioBackend
{
public:
    // ---- Clip lifecycle ------------------------------------------------

    [[nodiscard]] cd::core::Result<ClipHandle> create_clip(const ClipDesc& desc) override
    {
        if (desc.channels == 0 || desc.sample_rate == 0)
        {
            return std::unexpected(
                audio_errors::make(
                    audio_errors::Code::kInvalidArgument,
                    "create_clip: channels and sample_rate must be > 0"
                )
            );
        }
        ClipRec rec {};
        rec.channels = desc.channels;
        rec.sample_rate = desc.sample_rate;
        rec.samples.assign(desc.samples.begin(), desc.samples.end());
        const auto id = next_id_++;
        clips_.emplace(id, std::move(rec));
        return ClipHandle { id, 1u };
    }

    void destroy_clip(ClipHandle h) override
    {
        clips_.erase(h.index());
        // Reap any voices that referenced the freed clip.
        for (auto it = voices_.begin(); it != voices_.end();)
        {
            if (it->second.clip == h)
                it = voices_.erase(it);
            else
                ++it;
        }
    }

    [[nodiscard]] std::size_t clip_count() const noexcept override
    {
        return clips_.size();
    }

    // ---- Playback ------------------------------------------------------

    [[nodiscard]] cd::core::Result<VoiceHandle> play(ClipHandle clip, float volume, bool looping) override
    {
        if (!clips_.contains(clip.index()))
        {
            return std::unexpected(audio_errors::make(audio_errors::Code::kUnknownClip, "play: unknown clip"));
        }
        VoiceRec rec {};
        rec.clip = clip;
        rec.volume = std::clamp(volume, 0.0F, 1.0F);
        rec.looping = looping;
        rec.playing = true;
        const auto id = next_id_++;
        voices_.emplace(id, rec);
        return VoiceHandle { id, 1u };
    }

    void stop(VoiceHandle voice) override
    {
        voices_.erase(voice.index());
    }

    void set_volume(VoiceHandle voice, float volume) override
    {
        if (auto it = voices_.find(voice.index()); it != voices_.end())
        {
            it->second.volume = std::clamp(volume, 0.0F, 1.0F);
        }
    }

    [[nodiscard]] bool is_playing(VoiceHandle voice) const noexcept override
    {
        auto it = voices_.find(voice.index());
        return it != voices_.end() && it->second.playing;
    }

    [[nodiscard]] std::size_t voice_count() const noexcept override
    {
        return voices_.size();
    }

    // ---- Master --------------------------------------------------------

    void set_master_volume(float v) noexcept override
    {
        master_ = std::clamp(v, 0.0F, 1.0F);
    }

    [[nodiscard]] float master_volume() const noexcept override
    {
        return master_;
    }

private:
    std::uint32_t next_id_ { 1 };
    std::unordered_map<std::uint32_t, ClipRec> clips_ {};
    std::unordered_map<std::uint32_t, VoiceRec> voices_ {};
    float master_ { 1.0F };
};

}  // namespace

std::unique_ptr<IAudioBackend> make_null_audio_backend()
{
    return std::make_unique<NullAudioBackend>();
}

}  // namespace cd::audio
