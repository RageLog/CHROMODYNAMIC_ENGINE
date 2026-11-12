// =============================================================================
// CHROMODYNAMIC — tests/e2e/harness/MockAudioSink.hpp
//
// Mock audio sink wrapping cd::audio::make_null_audio_backend() for headless
// audio cue verification and dispatch accounting.
// =============================================================================
#pragma once

#include <cd/audio/IAudioBackend.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace cd::e2e
{

class MockAudioSink final
{
public:
    MockAudioSink()
        : backend_(cd::audio::make_null_audio_backend())
    {
    }

    [[nodiscard]] cd::audio::IAudioBackend* backend() const noexcept
    {
        return backend_.get();
    }

    [[nodiscard]] bool is_valid() const noexcept
    {
        return backend_ != nullptr;
    }

    void record_cue_dispatched(std::uint32_t preset_id, float volume, float pan)
    {
        ++total_cues_;
        cue_history_.push_back(CueRecord {
            .preset_id = preset_id,
            .volume = volume,
            .pan = pan,
        });
    }

    [[nodiscard]] std::uint32_t total_cues() const noexcept { return total_cues_; }

    struct CueRecord
    {
        std::uint32_t preset_id { 0U };
        float volume { 1.0F };
        float pan { 0.5F };
    };

    [[nodiscard]] const std::vector<CueRecord>& history() const noexcept { return cue_history_; }

    void reset()
    {
        total_cues_ = 0U;
        cue_history_.clear();
    }

private:
    std::unique_ptr<cd::audio::IAudioBackend> backend_;
    std::uint32_t total_cues_ { 0U };
    std::vector<CueRecord> cue_history_;
};

}  // namespace cd::e2e
