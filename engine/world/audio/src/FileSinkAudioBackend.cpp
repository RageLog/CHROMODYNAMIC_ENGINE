// =============================================================================
// CHROMODYNAMIC — cd/audio/FileSinkAudioBackend.cpp
// =============================================================================
#include <cd/audio/FileSinkBackend.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <ios>
#include <memory>
#include <string>
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
    /// Per-channel sample cursor into the clip.
    std::uint32_t cursor { 0 };
};

class FileSinkBackend final : public IFileSinkBackend
{
public:
    FileSinkBackend(std::uint32_t sample_rate, std::uint32_t channels) noexcept
        : sample_rate_ { sample_rate }
        , channels_ { channels == 0 ? 1u : channels }
    {
    }

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
        return ClipHandle { static_cast<std::uint32_t>(id), 1u };
    }

    void destroy_clip(ClipHandle h) override
    {
        clips_.erase(h.index());
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
        if (clips_.find(clip.index()) == clips_.end())
        {
            return std::unexpected(audio_errors::make(audio_errors::Code::kUnknownClip, "play: clip not found"));
        }
        VoiceRec v {};
        v.clip = clip;
        v.volume = volume;
        v.looping = looping;
        const auto id = next_voice_id_++;
        voices_.emplace(id, v);
        return VoiceHandle { static_cast<std::uint32_t>(id), 1u };
    }

    void stop(VoiceHandle voice) override
    {
        voices_.erase(voice.index());
    }

    void set_volume(VoiceHandle voice, float volume) override
    {
        auto it = voices_.find(voice.index());
        if (it != voices_.end())
            it->second.volume = volume;
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

    void set_master_volume(float v) noexcept override
    {
        master_ = v;
    }

    [[nodiscard]] float master_volume() const noexcept override
    {
        return master_;
    }

    // ---- IFileSinkBackend ---------------------------------------------

    void render(std::uint32_t frames) override
    {
        rendered_.reserve(static_cast<std::size_t>(rendered_.size() + frames * channels_));
        for (std::uint32_t i = 0; i < frames; ++i)
        {
            // For each output channel, sum every active voice (mono clips
            // broadcast across all output channels, multi-channel clips
            // contribute their own channel up to the device channel count).
            for (std::uint32_t ch = 0; ch < channels_; ++ch)
            {
                float acc = 0.0F;
                for (auto& [_, v] : voices_)
                {
                    if (!v.playing)
                        continue;
                    auto cit = clips_.find(v.clip.index());
                    if (cit == clips_.end())
                        continue;
                    const auto& c = cit->second;
                    if (c.samples.empty())
                        continue;
                    // Cursor is advanced once per OUTPUT FRAME (after the
                    // channel loop closes). Within a frame it is the same
                    // sample index for every output channel. No SRC: clip
                    // sample_rate is assumed to match the device — quality
                    // resampling is a follow-up sprint.
                    const std::uint64_t total_frames = static_cast<std::uint64_t>(c.samples.size()) / c.channels;
                    if (total_frames == 0 || v.cursor >= total_frames)
                        continue;
                    const std::uint32_t use_ch = ch < c.channels ? ch : 0u;
                    const std::size_t idx = static_cast<std::size_t>(v.cursor) * c.channels + use_ch;
                    acc += c.samples[idx] * v.volume;
                }
                rendered_.push_back(std::clamp(acc * master_, -1.0F, 1.0F));
            }
            // Advance per-voice cursors by one frame (per output frame).
            for (auto& [_, v] : voices_)
            {
                if (!v.playing)
                    continue;
                auto cit = clips_.find(v.clip.index());
                if (cit == clips_.end())
                    continue;
                const auto& c = cit->second;
                const std::uint64_t total_frames = static_cast<std::uint64_t>(c.samples.size()) / c.channels;
                ++v.cursor;
                if (v.cursor >= total_frames)
                {
                    if (v.looping && total_frames > 0)
                        v.cursor = 0;
                    else
                        v.playing = false;
                }
            }
        }
        rendered_frames_ += frames;
    }

    [[nodiscard]] cd::core::Result<void> write_wav(std::string_view path) const override
    {
        const std::string path_s { path };
        std::ofstream f { path_s, std::ios::binary | std::ios::trunc };
        if (!f)
        {
            return std::unexpected(
                audio_errors::make(
                    audio_errors::Code::kBackendError,
                    std::string { "write_wav: cannot open " } + path_s
                )
            );
        }

        // 16-bit interleaved PCM WAV. Convert float [-1,1] → int16 [-32768,32767].
        const auto data_size = static_cast<std::uint32_t>(rendered_.size() * sizeof(std::int16_t));
        const std::uint32_t fmt_size = 16;
        const std::uint32_t riff_size = 4 + 8 + fmt_size + 8 + data_size;

        auto put32 = [&](std::uint32_t x)
        {
            char b[4];
            b[0] = static_cast<char>(x & 0xFFu);
            b[1] = static_cast<char>((x >> 8u) & 0xFFu);
            b[2] = static_cast<char>((x >> 16u) & 0xFFu);
            b[3] = static_cast<char>((x >> 24u) & 0xFFu);
            f.write(b, 4);
        };
        auto put16 = [&](std::uint16_t x)
        {
            char b[2];
            b[0] = static_cast<char>(x & 0xFFu);
            b[1] = static_cast<char>((x >> 8u) & 0xFFu);
            f.write(b, 2);
        };

        f.write("RIFF", 4);
        put32(riff_size);
        f.write("WAVE", 4);
        f.write("fmt ", 4);
        put32(fmt_size);
        put16(1);  // PCM
        put16(static_cast<std::uint16_t>(channels_));
        put32(sample_rate_);
        put32(sample_rate_ * channels_ * 2u);               // byte rate
        put16(static_cast<std::uint16_t>(channels_ * 2u));  // block align
        put16(16);                                          // bits per sample
        f.write("data", 4);
        put32(data_size);
        for (float sample : rendered_)
        {
            const float clamped = std::clamp(sample, -1.0F, 1.0F);
            const auto s16 = static_cast<std::int16_t>(std::lround(clamped * 32767.0F));
            put16(static_cast<std::uint16_t>(s16));
        }
        if (!f)
        {
            return std::unexpected(audio_errors::make(audio_errors::Code::kBackendError, "write_wav: write failed"));
        }
        return {};
    }

    [[nodiscard]] std::uint32_t rendered_frames() const noexcept override
    {
        return rendered_frames_;
    }

private:
    std::uint32_t sample_rate_ { 48000 };
    std::uint32_t channels_ { 2 };
    float master_ { 1.0F };
    std::uint64_t next_id_ { 1 };
    std::uint64_t next_voice_id_ { 1 };
    std::unordered_map<std::uint64_t, ClipRec> clips_;
    std::unordered_map<std::uint64_t, VoiceRec> voices_;
    std::vector<float> rendered_;
    std::uint32_t rendered_frames_ { 0 };
};

}  // namespace

std::unique_ptr<IFileSinkBackend> make_file_sink_audio_backend(std::uint32_t sample_rate, std::uint32_t channels)
{
    return std::make_unique<FileSinkBackend>(sample_rate, channels);
}

}  // namespace cd::audio
