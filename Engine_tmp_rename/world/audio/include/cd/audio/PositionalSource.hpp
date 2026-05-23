// =============================================================================
// CHROMODYNAMIC — cd/audio/PositionalSource.hpp
// Phase 7 / Wave 78 — HRTF-lite per-voice positional mixer.
//
// Stateful object that turns a mono input stream into a stereo output
// stream applying the Wave 77 cues:
//
//   * ITD  → sample-delay on whichever ear is farther from the source
//   * ILD  → per-ear gain (compute_stereo_gains output)
//
// Each `PositionalSource` owns two ring-buffer delay lines (one per ear)
// so the ITD applied at the block boundary doesn't introduce a click —
// state survives across `process()` calls. Caller is expected to keep
// the source alive for as long as the voice is playing.
//
// This is "HRTF-LITE" — the impulse response is just a 1-tap delay +
// gain per ear, not a full FIR convolution of measured/synthesised HRTF
// data. Full HRTF (CIPIC / MIT KEMAR datasets + 64-tap minimum-phase
// FIR per ear) stays as a Phase 7+ wave; the API and call site here are
// the same so swapping the inner filter doesn't break consumers.
//
// Audio render-thread safety: process() is lock-free and allocation-
// free after construction. Configure the max delay buffer once via
// `prime(sample_rate, max_block_samples)`; subsequent process() calls
// stay in fixed-size ring buffers.
// =============================================================================
#pragma once

#include <cd/audio/Positional.hpp>
#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace cd::audio
{

class PositionalSource
{
public:
    /// Pre-allocate delay lines for the worst-case ITD + the max
    /// per-process() block size. Defaults cover any single-ear ITD
    /// up to ~1 ms (well above the Woodworth max of ~640 µs).
    void prime(std::uint32_t sample_rate, std::uint32_t max_block_samples,
               float max_itd_seconds = 0.001F)
    {
        sample_rate_ = sample_rate;
        const auto max_itd_samples = static_cast<std::uint32_t>(
            max_itd_seconds * static_cast<float>(sample_rate)) + 1U;
        const auto cap = max_block_samples + max_itd_samples + 1U;
        delay_l_.assign(cap, 0.0F);
        delay_r_.assign(cap, 0.0F);
        head_l_ = head_r_ = 0;
        max_block_ = max_block_samples;
    }

    /// Process `mono_in` into `stereo_out` (interleaved L,R,L,R,...).
    /// Requires `stereo_out.size() == mono_in.size() * 2` and
    /// `mono_in.size() <= max_block_samples` from the last prime().
    /// Silently no-ops on size mismatch (defensive against caller bugs
    /// on the real-time thread).
    void process(std::span<const float> mono_in,
                 const ListenerPose& listener,
                 const cd::math::Vec3f& source_pos,
                 std::span<float> stereo_out)
    {
        if (delay_l_.empty() || delay_r_.empty())
            return;
        if (stereo_out.size() != mono_in.size() * 2)
            return;
        if (mono_in.size() > max_block_)
            return;

        // Compute cues once per block — for sub-block accuracy a caller
        // can call process() per smaller chunk.
        const auto gains = compute_stereo_gains(listener, source_pos);
        const auto itd = compute_itd_seconds(listener, source_pos);
        // Positive ITD → right ear delayed; negative → left ear delayed.
        const int delay_l_samples = (itd < 0.0F)
            ? static_cast<int>(-itd * static_cast<float>(sample_rate_) + 0.5F)
            : 0;
        const int delay_r_samples = (itd > 0.0F)
            ? static_cast<int>(itd * static_cast<float>(sample_rate_) + 0.5F)
            : 0;
        const auto cap_l = delay_l_.size();
        const auto cap_r = delay_r_.size();

        for (std::size_t i = 0; i < mono_in.size(); ++i)
        {
            const float in = mono_in[i];
            // Write current sample to head, read at head - delay.
            delay_l_[head_l_] = in;
            delay_r_[head_r_] = in;
            const std::size_t read_l = (head_l_ + cap_l
                                        - static_cast<std::size_t>(delay_l_samples))
                                       % cap_l;
            const std::size_t read_r = (head_r_ + cap_r
                                        - static_cast<std::size_t>(delay_r_samples))
                                       % cap_r;
            stereo_out[i * 2 + 0] = delay_l_[read_l] * gains.left;
            stereo_out[i * 2 + 1] = delay_r_[read_r] * gains.right;
            head_l_ = (head_l_ + 1) % cap_l;
            head_r_ = (head_r_ + 1) % cap_r;
        }
    }

    /// Reset internal delay-line state (zero the ring buffers + heads).
    /// Call between consecutive playbacks of independent voices on the
    /// same Source object.
    void reset() noexcept
    {
        std::fill(delay_l_.begin(), delay_l_.end(), 0.0F);
        std::fill(delay_r_.begin(), delay_r_.end(), 0.0F);
        head_l_ = head_r_ = 0;
    }

    [[nodiscard]] std::uint32_t sample_rate() const noexcept { return sample_rate_; }
    [[nodiscard]] std::uint32_t max_block_samples() const noexcept { return max_block_; }

private:
    std::vector<float> delay_l_ {};
    std::vector<float> delay_r_ {};
    std::size_t head_l_ { 0 };
    std::size_t head_r_ { 0 };
    std::uint32_t sample_rate_ { 0 };
    std::uint32_t max_block_ { 0 };
};

}  // namespace cd::audio
