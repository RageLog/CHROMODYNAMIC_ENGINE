// =============================================================================
// CHROMODYNAMIC — cd/audio/WasapiAudioBackend.cpp
// Phase 5 / S4.5.b — Windows shared-mode audio output backend.
// =============================================================================
#include <cd/audio/WasapiBackend.hpp>

#if !defined(_WIN32)

namespace cd::audio
{
std::unique_ptr<IAudioBackend> make_wasapi_audio_backend()
{
    return {};
}
}  // namespace cd::audio

#else  // _WIN32

    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>

    #include <Audioclient.h>
    #include <mmdeviceapi.h>

// `__uuidof(...)` is the canonical MS Windows COM-interface pattern,
// but clang-cl tags it as a language extension under -Wlanguage-
// extension-token + -Werror. Suppress that one diagnostic only for
// this TU — we'd otherwise have to maintain explicit IID GUID
// constants for every COM type we touch.
    #if defined(__clang__)
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wlanguage-extension-token"
    #endif

    #include <algorithm>
    #include <atomic>
    #include <cstring>
    #include <mutex>
    #include <thread>
    #include <unordered_map>
    #include <vector>

namespace cd::audio
{

namespace
{

// RAII handle so we never miss a Release() on early return.
template <class T>
struct ComPtr
{
    T* p { nullptr };
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& o) noexcept : p { o.p } { o.p = nullptr; }
    ~ComPtr()
    {
        if (p != nullptr)
            p->Release();
    }
    // NOLINTNEXTLINE(google-runtime-operator) — COM out-param idiom; &ptr must yield T** for WASAPI factory calls
    T** operator&() noexcept { return &p; }
    T* operator->() const noexcept { return p; }
    explicit operator bool() const noexcept { return p != nullptr; }
};

struct ClipRec
{
    std::uint32_t channels { 1 };
    std::uint32_t sample_rate { 48000 };
    std::vector<float> samples;
};

struct VoiceRec
{
    ClipHandle clip {};
    float volume { 1.0F };
    bool looping { false };
    bool playing { true };
    std::uint32_t cursor { 0 };
};

// Phase 157 — push-stream record. The queue holds interleaved float
// frames at the stream's native channel count. The mix loop converts
// channel layouts on the fly (mono → device-channels broadcast,
// stereo → first two device channels).
struct StreamRec
{
    std::uint32_t channels { 1 };
    std::uint32_t sample_rate { 48000 };
    float         volume { 1.0F };
    std::vector<float> queue;   // FIFO of interleaved samples
    std::size_t   read_offset { 0 };
};

class WasapiBackend final : public IAudioBackend
{
public:
    WasapiBackend() = default;

    ~WasapiBackend() override
    {
        stop_.store(true, std::memory_order_release);
        if (event_ != nullptr)
            SetEvent(event_);
        if (thread_.joinable())
            thread_.join();
        if (event_ != nullptr)
        {
            CloseHandle(event_);
            event_ = nullptr;
        }
        // Order matters: stop client before releasing other refs.
        if (audio_client_ && started_)
            audio_client_->Stop();
    }

    [[nodiscard]] bool init()
    {
        // Per-thread COM init: main thread + worker thread each call
        // CoInitializeEx. The worker calls it inside render_loop_.
        const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
            return false;
        com_initialized_main_ = (hr == S_OK || hr == S_FALSE);

        ComPtr<IMMDeviceEnumerator> enumerator;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                    nullptr,
                                    CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void**>(&enumerator))))
            return false;

        ComPtr<IMMDevice> device;
        if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)))
            return false;

        if (FAILED(device->Activate(__uuidof(IAudioClient),
                                    CLSCTX_ALL,
                                    nullptr,
                                    reinterpret_cast<void**>(&audio_client_))))
            return false;

        WAVEFORMATEX* fmt = nullptr;
        if (FAILED(audio_client_->GetMixFormat(&fmt)) || fmt == nullptr)
            return false;
        // Capture device format. We trust GetMixFormat to return a
        // float32 interleaved layout on modern Windows; if it's
        // PCM int16, AUTOCONVERTPCM will resample our float mix.
        device_channels_ = fmt->nChannels;
        device_sample_rate_ = fmt->nSamplesPerSec;
        device_is_float_ = (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
                          || (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE
                              && fmt->wBitsPerSample == 32);
        device_bits_per_sample_ = fmt->wBitsPerSample;

        constexpr REFERENCE_TIME k10msInHundredsOfNanos = 100000;  // 10 ms buffer
        const DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK
                                 | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
                                 | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        const auto hr_init = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                                       stream_flags,
                                                       k10msInHundredsOfNanos,
                                                       0,
                                                       fmt,
                                                       nullptr);
        CoTaskMemFree(fmt);
        if (FAILED(hr_init))
            return false;

        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (event_ == nullptr)
            return false;
        if (FAILED(audio_client_->SetEventHandle(event_)))
            return false;

        if (FAILED(audio_client_->GetService(__uuidof(IAudioRenderClient),
                                              reinterpret_cast<void**>(&render_client_))))
            return false;

        UINT32 buffer_frames = 0;
        if (FAILED(audio_client_->GetBufferSize(&buffer_frames)))
            return false;
        buffer_frames_ = buffer_frames;

        if (FAILED(audio_client_->Start()))
            return false;
        started_ = true;

        // Spawn render thread. Per-thread COM is initialized inside it.
        thread_ = std::thread { [this] { render_loop_(); } };
        return true;
    }

    // ---- IAudioBackend ------------------------------------------------

    [[nodiscard]] cd::core::Result<ClipHandle> create_clip(const ClipDesc& desc) override
    {
        if (desc.channels == 0 || desc.sample_rate == 0)
            return std::unexpected(audio_errors::make(
                audio_errors::Code::kInvalidArgument, "channels / rate must be > 0"));
        ClipRec rec;
        rec.channels = desc.channels;
        rec.sample_rate = desc.sample_rate;
        rec.samples.assign(desc.samples.begin(), desc.samples.end());
        std::scoped_lock guard { state_mu_ };
        const auto id = next_clip_id_++;
        clips_.emplace(id, std::move(rec));
        return ClipHandle { static_cast<std::uint32_t>(id), 1u };
    }

    void destroy_clip(ClipHandle h) override
    {
        std::scoped_lock guard { state_mu_ };
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
        std::scoped_lock guard { state_mu_ };
        return clips_.size();
    }

    [[nodiscard]] cd::core::Result<VoiceHandle>
    do_play(ClipHandle clip, float volume, bool looping) override
    {
        std::scoped_lock guard { state_mu_ };
        if (!clips_.contains(clip.index()))
            return std::unexpected(audio_errors::make(audio_errors::Code::kUnknownClip, "clip"));
        VoiceRec v;
        v.clip = clip;
        v.volume = volume;
        v.looping = looping;
        const auto id = next_voice_id_++;
        voices_.emplace(id, v);
        return VoiceHandle { static_cast<std::uint32_t>(id), 1u };
    }

    void stop(VoiceHandle voice) override
    {
        std::scoped_lock guard { state_mu_ };
        voices_.erase(voice.index());
    }

    void set_volume(VoiceHandle voice, float volume) override
    {
        std::scoped_lock guard { state_mu_ };
        auto it = voices_.find(voice.index());
        if (it != voices_.end())
            it->second.volume = volume;
    }

    [[nodiscard]] bool is_playing(VoiceHandle voice) const noexcept override
    {
        std::scoped_lock guard { state_mu_ };
        const auto it = voices_.find(voice.index());
        return it != voices_.end() && it->second.playing;
    }

    [[nodiscard]] std::size_t voice_count() const noexcept override
    {
        std::scoped_lock guard { state_mu_ };
        return voices_.size();
    }

    void set_master_volume(float v) noexcept override { master_.store(v, std::memory_order_release); }
    [[nodiscard]] float master_volume() const noexcept override
    {
        return master_.load(std::memory_order_acquire);
    }

    // ---- Phase 157 — push-stream API ----------------------------------

    [[nodiscard]] cd::core::Result<StreamHandle>
    do_create_stream(std::uint32_t channels, std::uint32_t sample_rate, float volume) override
    {
        if (channels == 0 || sample_rate == 0)
        {
            return std::unexpected(audio_errors::make(
                audio_errors::Code::kInvalidArgument,
                "create_stream: channels / sample_rate must be > 0"));
        }
        std::scoped_lock guard { state_mu_ };
        const auto id = static_cast<std::uint32_t>(next_stream_id_++);
        StreamRec rec;
        rec.channels = channels;
        rec.sample_rate = sample_rate;
        rec.volume = volume;
        streams_.emplace(id, std::move(rec));
        return StreamHandle { id, 1u };
    }

    [[nodiscard]] cd::core::Result<void>
    push_stream_samples(StreamHandle stream, std::span<const float> samples) override
    {
        std::scoped_lock guard { state_mu_ };
        auto it = streams_.find(stream.index());
        if (it == streams_.end())
        {
            return std::unexpected(audio_errors::make(
                audio_errors::Code::kInvalidArgument,
                "push_stream_samples: unknown stream"));
        }
        it->second.queue.insert(it->second.queue.end(), samples.begin(), samples.end());
        return {};
    }

    void destroy_stream(StreamHandle stream) override
    {
        std::scoped_lock guard { state_mu_ };
        streams_.erase(stream.index());
    }

    [[nodiscard]] std::size_t stream_pending_frames(StreamHandle stream) const noexcept override
    {
        std::scoped_lock guard { state_mu_ };
        auto it = streams_.find(stream.index());
        if (it == streams_.end() || it->second.channels == 0) return 0;
        const auto remaining = it->second.queue.size() > it->second.read_offset
            ? it->second.queue.size() - it->second.read_offset : 0;
        return remaining / it->second.channels;
    }

    [[nodiscard]] std::size_t stream_count() const noexcept override
    {
        std::scoped_lock guard { state_mu_ };
        return streams_.size();
    }

private:
    void render_loop_() noexcept
    {
        // Per-thread COM init.
        const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool com_ok = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;

        while (!stop_.load(std::memory_order_acquire))
        {
            // Wait for the OS to signal "buffer almost empty".
            if (WaitForSingleObject(event_, 50) == WAIT_TIMEOUT)
                continue;
            if (stop_.load(std::memory_order_acquire))
                break;

            UINT32 padding = 0;
            if (FAILED(audio_client_->GetCurrentPadding(&padding)))
                continue;
            const UINT32 frames_available = buffer_frames_ - padding;
            if (frames_available == 0)
                continue;

            BYTE* data = nullptr;
            if (FAILED(render_client_->GetBuffer(frames_available, &data)))
                continue;
            if (data == nullptr)
            {
                render_client_->ReleaseBuffer(frames_available, 0);
                continue;
            }

            // Mix into a scratch float buffer, then convert to device
            // format if it isn't already float32.
            scratch_.assign(static_cast<std::size_t>(frames_available) * device_channels_, 0.0F);
            mix_frames_(scratch_, frames_available);

            if (device_is_float_)
            {
                std::memcpy(data, scratch_.data(), scratch_.size() * sizeof(float));
            }
            else if (device_bits_per_sample_ == 16)
            {
                auto* out = reinterpret_cast<std::int16_t*>(data);
                for (std::size_t i = 0; i < scratch_.size(); ++i)
                {
                    const float clamped = std::clamp(scratch_[i], -1.0F, 1.0F);
                    out[i] = static_cast<std::int16_t>(clamped * 32767.0F);
                }
            }
            else
            {
                // Unsupported format → silent buffer.
                std::memset(data, 0, scratch_.size() * (device_bits_per_sample_ / 8));
            }
            render_client_->ReleaseBuffer(frames_available, 0);
        }

        if (com_ok)
            CoUninitialize();
    }

    void mix_frames_(std::vector<float>& out, std::uint32_t frames) noexcept
    {
        std::scoped_lock guard { state_mu_ };
        const float master = master_.load(std::memory_order_acquire);
        for (std::uint32_t i = 0; i < frames; ++i)
        {
            for (std::uint32_t ch = 0; ch < device_channels_; ++ch)
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
                    const std::uint64_t total_frames =
                        static_cast<std::uint64_t>(c.samples.size()) / c.channels;
                    if (total_frames == 0 || v.cursor >= total_frames)
                        continue;
                    const std::uint32_t use_ch = ch < c.channels ? ch : 0u;
                    const std::size_t idx =
                        static_cast<std::size_t>(v.cursor) * c.channels + use_ch;
                    acc += c.samples[idx] * v.volume;
                }
                // Phase 157 — mix in push-streams. Each stream contributes
                // one frame from its queue; broadcast / drop channels as
                // needed to match the device layout. Empty queue → silence
                // (no underrun pop).
                for (auto& [_, s] : streams_)
                {
                    const auto avail = s.queue.size() > s.read_offset
                        ? s.queue.size() - s.read_offset : 0;
                    if (avail < s.channels) continue;
                    const std::uint32_t use_ch = ch < s.channels ? ch : 0u;
                    acc += s.queue[s.read_offset + use_ch] * s.volume;
                }
                out[i * device_channels_ + ch] = std::clamp(acc * master, -1.0F, 1.0F);
            }
            // After writing this device frame, advance every stream by
            // one source frame.
            for (auto& [_, s] : streams_)
            {
                const auto avail = s.queue.size() > s.read_offset
                    ? s.queue.size() - s.read_offset : 0;
                if (avail >= s.channels) s.read_offset += s.channels;
            }
            // Compact stream queues when the read offset gets large to
            // keep memory bounded — drop the consumed prefix.
            for (auto& [_, s] : streams_)
            {
                if (s.read_offset > 8192 && s.read_offset < s.queue.size())
                {
                    s.queue.erase(s.queue.begin(), s.queue.begin() + static_cast<std::ptrdiff_t>(s.read_offset));
                    s.read_offset = 0;
                }
                else if (s.read_offset >= s.queue.size() && !s.queue.empty())
                {
                    s.queue.clear();
                    s.read_offset = 0;
                }
            }
            // Advance every active voice's cursor by 1 frame at the
            // CLIP's sample rate. WASAPI's AUTOCONVERTPCM rescales the
            // overall stream rate, so engine-side cursor walks at the
            // device rate (we ignore the clip rate intentionally — clips
            // designed at 48 kHz play at correct pitch on a 48 kHz
            // device. Mismatched-rate clips will pitch-shift; v2 will
            // add a per-voice resampler when sample_rate != device_rate).
            for (auto& [_, v] : voices_)
            {
                if (!v.playing)
                    continue;
                auto cit = clips_.find(v.clip.index());
                if (cit == clips_.end())
                    continue;
                const auto& c = cit->second;
                const std::uint64_t total_frames =
                    static_cast<std::uint64_t>(c.samples.size()) / c.channels;
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
    }

    bool com_initialized_main_ { false };
    bool started_ { false };
    std::atomic<bool> stop_ { false };
    std::atomic<float> master_ { 1.0F };
    HANDLE event_ { nullptr };
    std::thread thread_;

    ComPtr<IAudioClient> audio_client_;
    ComPtr<IAudioRenderClient> render_client_;
    UINT32 buffer_frames_ { 0 };
    UINT32 device_channels_ { 2 };
    UINT32 device_sample_rate_ { 48000 };
    UINT16 device_bits_per_sample_ { 32 };
    bool device_is_float_ { true };

    mutable std::mutex state_mu_;
    std::uint64_t next_clip_id_ { 1 };
    std::uint64_t next_voice_id_ { 1 };
    std::unordered_map<std::uint64_t, ClipRec> clips_;
    std::unordered_map<std::uint64_t, VoiceRec> voices_;
    std::vector<float> scratch_;

    // Phase 157 — push-stream storage.
    std::uint32_t next_stream_id_ { 1 };
    std::unordered_map<std::uint32_t, StreamRec> streams_;
};

}  // namespace

std::unique_ptr<IAudioBackend> make_wasapi_audio_backend()
{
    auto backend = std::make_unique<WasapiBackend>();
    if (!backend->init())
        return {};  // returns nullptr — caller falls back to file sink / null
    return backend;
}

}  // namespace cd::audio

    #if defined(__clang__)
        #pragma clang diagnostic pop
    #endif

#endif  // _WIN32
