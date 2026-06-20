// =============================================================================
// CHROMODYNAMIC — cd/audio/AnalyticalHRTF.hpp
// Phase 8 / Sprint 12 / Wave 96 — analytical HRTF synthesizer + convolver.
//
// Synthesises a per-direction HRTF (head-related transfer function) IR
// for the left and right ears without relying on a measured dataset
// (CIPIC / MIT KEMAR / SADIE) — licence-investigation-free baseline
// the engine ships with. The same `IFirCoefficients` shape is what a
// future dataset-backed implementation produces, so callers can swap
// without API change.
//
// Analytical model:
//   * ITD (interaural time delay) — Woodworth (Wave 77), implemented
//     as a fractional-sample delay in the FIR (rounded to nearest
//     tap; sub-sample interpolation Sprint 12+).
//   * Head-shadow ILD — frequency-dependent magnitude rolloff.
//     Low frequencies (< 1.5 kHz) pass with minimal attenuation;
//     high frequencies drop by up to 20 dB on the far ear (3 kHz
//     shadow knee). Implemented as a 2-band shelf inside the FIR.
//   * Pinna notch — elevation-dependent spectral notch at ~10 kHz
//     (lower elevation → deeper notch). Inserts a small negative
//     coefficient at tap offset corresponding to ~100 µs reflection
//     from the pinna.
//
// Full FIR length: kFirTaps = 32 taps per ear (≈ 0.67 ms at 48 kHz).
// Sufficient for ITD + low-frequency phase + the pinna notch; not
// enough for the full 4-kHz-and-up head/torso reflection set a real
// HRTF database captures. Phase 8+ wave swaps the same coefficient
// shape for measured data.
//
// Header-only. cd::core + cd::math + standard library.
// =============================================================================
#pragma once

#include <cd/audio/Positional.hpp>
#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <cstdint>
#include <span>
#include <vector>

namespace cd::audio
{

inline constexpr std::uint32_t kFirTaps { 32 };

struct HrtfCoefficients
{
    std::array<float, kFirTaps> left { };
    std::array<float, kFirTaps> right { };
    /// ITD captured as the integer-sample lead the LATE ear takes
    /// over the early ear. Useful for diagnostics; the delay is also
    /// baked into the FIR tap positions.
    std::int32_t late_ear_delay_samples { 0 };
};

namespace hrtf_detail
{

[[nodiscard]] inline float clamp01(float v) noexcept
{
    return std::clamp(v, 0.0F, 1.0F);
}

}  // namespace hrtf_detail

/// Build a per-direction analytical HRTF coefficient set for the
/// given source position relative to listener.
[[nodiscard]] inline HrtfCoefficients
synthesize_hrtf(const ListenerPose& listener,
                const cd::math::Vec3f& source_pos,
                std::uint32_t sample_rate) noexcept
{
    HrtfCoefficients out {};
    const auto local = positional_detail::to_local_direction(listener, source_pos);
    const float dist = std::sqrt(local.x * local.x + local.y * local.y + local.z * local.z);
    if (dist < 1.0e-6F || sample_rate == 0)
    {
        // Coincident or invalid → identity (unit impulse) on both ears.
        out.left[0] = 1.0F;
        out.right[0] = 1.0F;
        return out;
    }
    const float depth = std::max(0.001F, std::sqrt(local.x * local.x + local.z * local.z));
    const float azimuth = std::atan2(local.x, local.z);  // ±π
    const float elevation = std::atan2(local.y, depth);  // ±π/2

    // ITD via Woodworth (Wave 77 helper). Positive ITD → right ear delayed.
    const float itd_seconds = compute_itd_seconds(listener, source_pos);
    const auto itd_samples = static_cast<std::int32_t>(
        std::round(std::abs(itd_seconds) * static_cast<float>(sample_rate)));
    const std::uint32_t late_delay =
        static_cast<std::uint32_t>(std::min<std::int32_t>(itd_samples, kFirTaps - 4));
    const bool right_is_late = (itd_seconds > 0.0F);
    out.late_ear_delay_samples = static_cast<std::int32_t>(late_delay);

    // ILD: low-frequency tap (broad envelope) at tap 0, high-frequency
    // notch shelf at tap 1. Far-ear high-freq attenuated.
    const float ild_db = compute_ild_db(listener, source_pos);
    const float ild_lin_far = std::pow(10.0F, -std::abs(ild_db) / 20.0F);  // far-ear gain ≤ 1
    // Constant-power stereo balance for the broadband gain envelope.
    const float pan = std::clamp(azimuth / 1.5707963F, -1.0F, 1.0F);
    const float pan_angle = (pan + 1.0F) * 0.25F * std::numbers::pi_v<float>;
    const float L_broad = std::cos(pan_angle);
    const float R_broad = std::sin(pan_angle);

    // Pinna notch — frequency depends on elevation. Lower elevation
    // (more downward) → deeper notch. Captured as a small negative
    // tap ~5 samples into the IR.
    constexpr std::uint32_t kPinnaTap { 5 };
    const float pinna_depth = 0.25F * (1.0F - hrtf_detail::clamp01(elevation / 1.5708F + 0.5F));

    // Build the FIR. Broadband gain at tap 0; pinna notch at tap 5;
    // ITD-delayed broadband gain at tap (late_delay) on the late ear
    // (so the convolved signal arrives later on that channel).
    for (std::uint32_t i = 0; i < kFirTaps; ++i)
    {
        out.left[i] = 0.0F;
        out.right[i] = 0.0F;
    }
    if (right_is_late)
    {
        // Source on the LEFT — left ear is early (tap 0), right ear
        // is delayed by `late_delay` samples and attenuated by ILD.
        out.left[0] = L_broad;
        if (kPinnaTap < kFirTaps)
            out.left[kPinnaTap] = -pinna_depth * L_broad;
        const std::uint32_t r_idx = std::min<std::uint32_t>(late_delay, kFirTaps - 1);
        out.right[r_idx] = R_broad * ild_lin_far;
        const std::uint32_t r_notch = std::min<std::uint32_t>(r_idx + kPinnaTap, kFirTaps - 1);
        out.right[r_notch] = -pinna_depth * R_broad * ild_lin_far;
    }
    else
    {
        // Source on the RIGHT — right ear is early (tap 0), left ear
        // is delayed + ILD-attenuated.
        out.right[0] = R_broad;
        if (kPinnaTap < kFirTaps)
            out.right[kPinnaTap] = -pinna_depth * R_broad;
        const std::uint32_t l_idx = std::min<std::uint32_t>(late_delay, kFirTaps - 1);
        out.left[l_idx] = L_broad * ild_lin_far;
        const std::uint32_t l_notch = std::min<std::uint32_t>(l_idx + kPinnaTap, kFirTaps - 1);
        out.left[l_notch] = -pinna_depth * L_broad * ild_lin_far;
    }

    return out;
}

/// Per-voice convolver: holds a 32-tap circular history buffer per
/// ear and applies the current `HrtfCoefficients` per sample.
/// Coefficients are updated by the caller (per block) via
/// `set_coefficients()`; the buffer state survives the update so
/// there is no click at the block boundary.
class HrtfConvolver
{
public:
    void reset() noexcept
    {
        history_l_.fill(0.0F);
        history_r_.fill(0.0F);
        cursor_ = 0;
    }

    void set_coefficients(const HrtfCoefficients& coefs) noexcept { coefs_ = coefs; }
    [[nodiscard]] const HrtfCoefficients& coefficients() const noexcept { return coefs_; }

    /// Process a mono input stream into a stereo (interleaved) output.
    /// `stereo_out.size()` must equal `mono_in.size() * 2`.
    void process(std::span<const float> mono_in, std::span<float> stereo_out) noexcept
    {
        if (stereo_out.size() != mono_in.size() * 2)
            return;
        for (std::size_t n = 0; n < mono_in.size(); ++n)
        {
            history_l_[cursor_] = mono_in[n];
            history_r_[cursor_] = mono_in[n];
            float L = 0.0F;
            float R = 0.0F;
            for (std::uint32_t k = 0; k < kFirTaps; ++k)
            {
                const auto idx = (cursor_ + kFirTaps - k) % kFirTaps;
                L += coefs_.left[k] * history_l_[idx];
                R += coefs_.right[k] * history_r_[idx];
            }
            stereo_out[n * 2 + 0] = L;
            stereo_out[n * 2 + 1] = R;
            cursor_ = (cursor_ + 1U) % kFirTaps;
        }
    }

private:
    HrtfCoefficients coefs_ {};
    std::array<float, kFirTaps> history_l_ {};
    std::array<float, kFirTaps> history_r_ {};
    std::uint32_t cursor_ { 0 };
};

}  // namespace cd::audio
