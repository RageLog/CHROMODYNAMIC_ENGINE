// =============================================================================
// CHROMODYNAMIC — engine/world/audio/src/AlsaBackend.cpp
//
// Linux ALSA snd_pcm backend implementation.
//
// Unlike the WASAPI / CoreAudio paths which rely on the OS to drive
// the render callback, ALSA's snd_pcm requires the application to push
// frames into the buffer itself. This backend owns a dedicated
// std::thread that:
//   1. Mixes every active voice into a local interleaved float32
//      buffer (kBlockFrames frames, 2 channels).
//   2. snd_pcm_writei()'s the block to "default" output.
//   3. Repeats until stop_ is set.
//
// snd_pcm is opened non-blocking so the writei loop never deadlocks
// stop(); the producer thread polls a short stop flag between writes.
//
// On non-Linux platforms the file compiles to a stub factory that
// returns nullptr (Wave 34 contract).
// =============================================================================
#include <cd/audio/AlsaBackend.hpp>

#if !defined(__linux__)

namespace cd::audio
{
std::unique_ptr<IAudioBackend> make_alsa_backend()
{
    return {};
}
}  // namespace cd::audio

#else  // __linux__

#    include <alsa/asoundlib.h>

#    include <algorithm>
#    include <atomic>
#    include <cstdint>
#    include <cstring>
#    include <mutex>
#    include <thread>
#    include <unordered_map>
#    include <vector>

namespace cd::audio
{

namespace
{

struct ClipRec
{
    std::uint32_t channels { 1 };
    std::uint32_t sample_rate { 48000 };
    std::vector<float> samples;
};

struct VoiceRec
{
    std::uint32_t clip_slot { 0 };
    std::size_t cursor { 0 };
    float volume { 1.0F };
    bool looping { false };
    bool playing { false };
};

constexpr std::uint32_t kBlockFrames = 1024;
constexpr std::uint32_t kSampleRate = 48000;
constexpr std::uint32_t kChannels = 2;

class AlsaBackend final : public IAudioBackend
{
public:
    AlsaBackend() noexcept = default;
    ~AlsaBackend() override
    {
        stop_.store(true, std::memory_order_release);
        if (worker_.joinable())
            worker_.join();
        if (pcm_ != nullptr)
        {
            ::snd_pcm_drain(pcm_);
            ::snd_pcm_close(pcm_);
            pcm_ = nullptr;
        }
    }

    [[nodiscard]] bool init()
    {
        if (::snd_pcm_open(&pcm_, "default", SND_PCM_STREAM_PLAYBACK, 0) != 0)
            return false;
        const auto rc = ::snd_pcm_set_params(
            pcm_,
            SND_PCM_FORMAT_FLOAT_LE,
            SND_PCM_ACCESS_RW_INTERLEAVED,
            kChannels,
            kSampleRate,
            /*soft_resample=*/1,
            /*latency_us=*/100000);
        if (rc != 0)
        {
            ::snd_pcm_close(pcm_);
            pcm_ = nullptr;
            return false;
        }
        worker_ = std::thread { [this] { worker_loop(); } };
        return true;
    }

    // ---- Clip lifecycle ------------------------------------------------

    [[nodiscard]] cd::core::Result<ClipHandle> create_clip(const ClipDesc& desc) override
    {
        if (desc.channels == 0 || desc.sample_rate == 0)
            return std::unexpected(audio_errors::make(audio_errors::Code::kInvalidArgument));
        std::lock_guard guard { mu_ };
        const auto slot = next_clip_slot_++;
        ClipRec rec;
        rec.channels = desc.channels;
        rec.sample_rate = desc.sample_rate;
        rec.samples.assign(desc.samples.begin(), desc.samples.end());
        clips_[slot] = std::move(rec);
        return ClipHandle { slot };
    }

    void destroy_clip(ClipHandle h) override
    {
        std::lock_guard guard { mu_ };
        for (auto& [vid, v] : voices_)
            if (v.clip_slot == h.value())
                v.playing = false;
        clips_.erase(h.value());
    }

    [[nodiscard]] std::size_t clip_count() const noexcept override
    {
        std::lock_guard guard { mu_ };
        return clips_.size();
    }

    // ---- Playback ------------------------------------------------------

    [[nodiscard]] cd::core::Result<VoiceHandle>
    do_play(ClipHandle clip, float volume, bool looping) override
    {
        std::lock_guard guard { mu_ };
        if (clips_.find(clip.value()) == clips_.end())
            return std::unexpected(audio_errors::make(audio_errors::Code::kUnknownClip));
        const auto slot = next_voice_slot_++;
        VoiceRec v;
        v.clip_slot = clip.value();
        v.volume = std::clamp(volume, 0.0F, 1.0F);
        v.looping = looping;
        v.playing = true;
        voices_[slot] = v;
        return VoiceHandle { slot };
    }

    void stop(VoiceHandle voice) override
    {
        std::lock_guard guard { mu_ };
        auto it = voices_.find(voice.value());
        if (it != voices_.end())
            it->second.playing = false;
    }

    void set_volume(VoiceHandle voice, float volume) override
    {
        std::lock_guard guard { mu_ };
        auto it = voices_.find(voice.value());
        if (it != voices_.end())
            it->second.volume = std::clamp(volume, 0.0F, 1.0F);
    }

    [[nodiscard]] bool is_playing(VoiceHandle voice) const noexcept override
    {
        std::lock_guard guard { mu_ };
        auto it = voices_.find(voice.value());
        return it != voices_.end() && it->second.playing;
    }

    [[nodiscard]] std::size_t voice_count() const noexcept override
    {
        std::lock_guard guard { mu_ };
        return voices_.size();
    }

    void set_master_volume(float v) noexcept override
    {
        master_volume_.store(std::clamp(v, 0.0F, 1.0F), std::memory_order_relaxed);
    }
    [[nodiscard]] float master_volume() const noexcept override
    {
        return master_volume_.load(std::memory_order_relaxed);
    }

private:
    void worker_loop()
    {
        std::vector<float> block(static_cast<std::size_t>(kBlockFrames) * kChannels, 0.0F);
        while (!stop_.load(std::memory_order_acquire))
        {
            std::fill(block.begin(), block.end(), 0.0F);
            {
                std::lock_guard guard { mu_ };
                const float master = master_volume_.load(std::memory_order_relaxed);
                for (auto& [vid, v] : voices_)
                {
                    if (!v.playing)
                        continue;
                    auto clip_it = clips_.find(v.clip_slot);
                    if (clip_it == clips_.end())
                    {
                        v.playing = false;
                        continue;
                    }
                    const auto& clip = clip_it->second;
                    const float gain = v.volume * master;
                    const auto src_channels = clip.channels;
                    for (std::uint32_t f = 0; f < kBlockFrames; ++f)
                    {
                        if (v.cursor >= clip.samples.size())
                        {
                            if (v.looping && !clip.samples.empty())
                                v.cursor = 0;
                            else
                            {
                                v.playing = false;
                                break;
                            }
                        }
                        const float left = clip.samples[v.cursor];
                        const float right = (src_channels > 1
                                             && v.cursor + 1 < clip.samples.size())
                                              ? clip.samples[v.cursor + 1]
                                              : left;
                        block[f * 2 + 0] += left * gain;
                        block[f * 2 + 1] += right * gain;
                        v.cursor += src_channels;
                    }
                }
            }
            snd_pcm_sframes_t written = ::snd_pcm_writei(pcm_, block.data(), kBlockFrames);
            if (written < 0)
                written = ::snd_pcm_recover(pcm_, static_cast<int>(written), 1);
            if (written < 0)
                break;  // unrecoverable PCM state — give up
        }
    }

    snd_pcm_t* pcm_ { nullptr };
    std::thread worker_;
    std::atomic<bool> stop_ { false };
    mutable std::mutex mu_;
    std::atomic<float> master_volume_ { 1.0F };
    std::uint64_t next_clip_slot_ { 1 };
    std::uint64_t next_voice_slot_ { 1 };
    std::unordered_map<std::uint64_t, ClipRec> clips_;
    std::unordered_map<std::uint64_t, VoiceRec> voices_;
};

}  // namespace

std::unique_ptr<IAudioBackend> make_alsa_backend()
{
    auto be = std::make_unique<AlsaBackend>();
    if (!be->init())
        return {};
    return be;
}

}  // namespace cd::audio

#endif  // __linux__
