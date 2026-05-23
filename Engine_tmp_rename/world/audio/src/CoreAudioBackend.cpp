// =============================================================================
// CHROMODYNAMIC — engine/world/audio/src/CoreAudioBackend.cpp
//
// macOS CoreAudio (AudioUnit DefaultOutput) backend implementation.
//
// The audio render thread is managed by the AudioUnit itself — there
// is no explicit std::thread loop here; the render callback fires
// on a high-priority IO thread the OS owns. We mix every active voice
// into the callback's buffer under a short critical section so the
// public API (create_clip / play / stop / set_volume / set_master_volume)
// stays callable from any thread.
//
// On non-Apple platforms the file compiles to a stub factory that
// returns nullptr (matches the Wave 34 contract).
// =============================================================================
#include <cd/audio/CoreAudioBackend.hpp>

#if !defined(__APPLE__)

namespace cd::audio
{
std::unique_ptr<IAudioBackend> make_coreaudio_backend()
{
    return {};
}
}  // namespace cd::audio

#else  // __APPLE__

#    include <AudioToolbox/AudioToolbox.h>
#    include <AudioUnit/AudioUnit.h>
#    include <CoreAudio/CoreAudio.h>

#    include <algorithm>
#    include <atomic>
#    include <cstdint>
#    include <cstring>
#    include <mutex>
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

class CoreAudioBackend final : public IAudioBackend
{
public:
    CoreAudioBackend() noexcept = default;

    ~CoreAudioBackend() override
    {
        teardown();
    }

    [[nodiscard]] bool init()
    {
        AudioComponentDescription desc {};
        desc.componentType = kAudioUnitType_Output;
        desc.componentSubType = kAudioUnitSubType_DefaultOutput;
        desc.componentManufacturer = kAudioUnitManufacturer_Apple;

        AudioComponent comp = ::AudioComponentFindNext(nullptr, &desc);
        if (comp == nullptr)
            return false;
        if (::AudioComponentInstanceNew(comp, &unit_) != noErr || unit_ == nullptr)
            return false;

        AudioStreamBasicDescription fmt {};
        fmt.mSampleRate = 48000.0;
        fmt.mFormatID = kAudioFormatLinearPCM;
        fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        fmt.mChannelsPerFrame = 2;
        fmt.mBitsPerChannel = 32;
        fmt.mBytesPerFrame = 4 * fmt.mChannelsPerFrame;
        fmt.mFramesPerPacket = 1;
        fmt.mBytesPerPacket = fmt.mBytesPerFrame;
        if (::AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat,
                                   kAudioUnitScope_Input, 0, &fmt, sizeof(fmt))
            != noErr)
            return false;
        sample_rate_ = static_cast<std::uint32_t>(fmt.mSampleRate);
        channels_ = fmt.mChannelsPerFrame;

        AURenderCallbackStruct cb {};
        cb.inputProc = &CoreAudioBackend::render_trampoline;
        cb.inputProcRefCon = this;
        if (::AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback,
                                   kAudioUnitScope_Input, 0, &cb, sizeof(cb))
            != noErr)
            return false;

        if (::AudioUnitInitialize(unit_) != noErr)
            return false;
        if (::AudioOutputUnitStart(unit_) != noErr)
            return false;
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
        // Stop voices referencing this clip first.
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
    play(ClipHandle clip, float volume, bool looping) override
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
    void teardown() noexcept
    {
        if (unit_ != nullptr)
        {
            ::AudioOutputUnitStop(unit_);
            ::AudioUnitUninitialize(unit_);
            ::AudioComponentInstanceDispose(unit_);
            unit_ = nullptr;
        }
    }

    static OSStatus render_trampoline(void* refcon,
                                      AudioUnitRenderActionFlags* /*flags*/,
                                      const AudioTimeStamp* /*ts*/,
                                      UInt32 /*bus*/,
                                      UInt32 frames,
                                      AudioBufferList* io_data)
    {
        auto* self = static_cast<CoreAudioBackend*>(refcon);
        return self->render(frames, io_data);
    }

    OSStatus render(UInt32 frames, AudioBufferList* io)
    {
        if (io == nullptr || io->mNumberBuffers == 0)
            return noErr;
        auto* out = static_cast<float*>(io->mBuffers[0].mData);
        const std::size_t out_samples = static_cast<std::size_t>(frames) * channels_;
        std::fill_n(out, out_samples, 0.0F);

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
            for (UInt32 f = 0; f < frames; ++f)
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
                // Mix into both output channels — mono → stereo
                // duplicated; stereo → channel-mapped.
                const float left = clip.samples[v.cursor];
                const float right = (src_channels > 1
                                     && v.cursor + 1 < clip.samples.size())
                                      ? clip.samples[v.cursor + 1]
                                      : left;
                out[f * 2 + 0] += left * gain;
                out[f * 2 + 1] += right * gain;
                v.cursor += src_channels;
            }
        }
        return noErr;
    }

    AudioUnit unit_ { nullptr };
    std::uint32_t sample_rate_ { 48000 };
    std::uint32_t channels_ { 2 };
    mutable std::mutex mu_;
    std::atomic<float> master_volume_ { 1.0F };
    std::uint64_t next_clip_slot_ { 1 };
    std::uint64_t next_voice_slot_ { 1 };
    std::unordered_map<std::uint64_t, ClipRec> clips_;
    std::unordered_map<std::uint64_t, VoiceRec> voices_;
};

}  // namespace

std::unique_ptr<IAudioBackend> make_coreaudio_backend()
{
    auto be = std::make_unique<CoreAudioBackend>();
    if (!be->init())
        return {};
    return be;
}

}  // namespace cd::audio

#endif  // __APPLE__
