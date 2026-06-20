// =============================================================================
// CHROMODYNAMIC — cd/audio/Surround.hpp
// Phase 8 / Sprint 11 / Wave 93 — multi-channel speaker routing.
//
// Computes per-speaker gains for a 3D source position given a
// `SpeakerLayout`. Uses pairwise constant-power panning across the
// two speakers whose azimuths bracket the source direction (a
// 1D-VBAP variant — sufficient for horizontal panning, the common
// case for game audio). LFE on 5.1 / 7.1 layouts is left at zero
// gain; LFE crossover / low-pass routing is a separate concern.
//
// Speaker positions (ITU-R BS.775):
//   Mono       : [C]
//   Stereo     : [FL -30°, FR +30°]
//   5.1        : [FL -30°, FR +30°, FC 0°, LFE, RL -110°, RR +110°]
//   7.1        : [FL -30°, FR +30°, FC 0°, LFE, SL -90°, SR +90°,
//                 RL -150°, RR +150°]
//
// Listener-relative azimuth: 0° = directly in front (-Z), positive
// to the right (+X), wraps at ±180°.
//
// Header-only; cd::core + cd::math + cd::audio Positional.
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

enum class SpeakerLayout : std::uint8_t
{
    kMono = 0,
    kStereo,
    kSurround51,
    kSurround71,
};

/// Number of speakers (channels) in `layout`. LFE counts as one
/// channel for the purposes of buffer sizing; surround gains for
/// LFE are produced as 0.0 (caller routes a low-pass'd mix into it).
[[nodiscard]] inline std::uint32_t channel_count(SpeakerLayout layout) noexcept
{
    switch (layout)
    {
        case SpeakerLayout::kMono: return 1U;
        case SpeakerLayout::kStereo: return 2U;
        case SpeakerLayout::kSurround51: return 6U;
        case SpeakerLayout::kSurround71: return 8U;
    }
    return 0U;
}

namespace surround_detail
{

/// Per-layout speaker azimuth tables in radians (negative = left,
/// positive = right). LFE entries are flagged with `is_lfe = true`
/// so they are excluded from azimuth panning.
struct SpeakerSlot
{
    float azimuth_rad;
    bool is_lfe;
};

[[nodiscard]] inline std::span<const SpeakerSlot>
layout_slots(SpeakerLayout layout) noexcept
{
    constexpr float kDeg = std::numbers::pi_v<float> / 180.0F;
    static const SpeakerSlot kMono[] = {
        { 0.0F, false }
    };
    static const SpeakerSlot kStereo[] = {
        { -30.0F * kDeg, false },
        { +30.0F * kDeg, false },
    };
    static const SpeakerSlot k51[] = {
        { -30.0F * kDeg, false },   // FL
        { +30.0F * kDeg, false },   // FR
        { 0.0F, false },            // FC
        { 0.0F, true },             // LFE
        { -110.0F * kDeg, false },  // RL
        { +110.0F * kDeg, false },  // RR
    };
    static const SpeakerSlot k71[] = {
        { -30.0F * kDeg, false },   // FL
        { +30.0F * kDeg, false },   // FR
        { 0.0F, false },            // FC
        { 0.0F, true },             // LFE
        { -90.0F * kDeg, false },   // SL
        { +90.0F * kDeg, false },   // SR
        { -150.0F * kDeg, false },  // RL
        { +150.0F * kDeg, false },  // RR
    };
    switch (layout)
    {
        case SpeakerLayout::kMono: return { kMono, 1 };
        case SpeakerLayout::kStereo: return { kStereo, 2 };
        case SpeakerLayout::kSurround51: return { k51, 6 };
        case SpeakerLayout::kSurround71: return { k71, 8 };
    }
    return { kMono, 1 };
}

}  // namespace surround_detail

/// Compute per-channel gains for `source_pos` relative to `listener`.
/// `gains_out` must be sized exactly `channel_count(layout)`; the
/// helper silently no-ops on size mismatch (defensive against caller
/// bugs on the render thread).
///
/// Distance attenuation is the same inverse-distance law as the
/// stereo `compute_stereo_gains` — `ref_distance_m` is the unit
/// distance, `rolloff` scales the falloff.
inline void compute_surround_gains(SpeakerLayout layout,
                                   const ListenerPose& listener,
                                   const cd::math::Vec3f& source_pos,
                                   std::span<float> gains_out,
                                   float ref_distance_m = 1.0F,
                                   float rolloff = 1.0F) noexcept
{
    const auto slots = surround_detail::layout_slots(layout);
    if (gains_out.size() != slots.size())
        return;
    for (auto& g : gains_out)
        g = 0.0F;

    // Source azimuth in listener-local space.
    const auto local = positional_detail::to_local_direction(listener, source_pos);
    const float dist = std::sqrt(local.x * local.x + local.y * local.y + local.z * local.z);
    if (dist < 1.0e-6F)
    {
        // Coincident → uniform spread across non-LFE speakers (mono fold-in).
        std::uint32_t non_lfe_count = 0;
        for (const auto& s : slots)
            if (!s.is_lfe)
                ++non_lfe_count;
        if (non_lfe_count == 0)
            return;
        const float uniform = 1.0F / std::sqrt(static_cast<float>(non_lfe_count));
        for (std::size_t i = 0; i < slots.size(); ++i)
            gains_out[i] = slots[i].is_lfe ? 0.0F : uniform;
        return;
    }

    const float attenuation = std::min(
        1.0F, ref_distance_m / std::max(dist * rolloff, 1.0e-6F));

    // Source azimuth in [-π, +π]. Listener-local: x = right, z = forward.
    // Surround needs the full ±π range (no front-clamp), unlike the
    // Positional stereo path which only cares about the front half.
    const float theta = std::atan2(local.x, local.z);

    // For mono layout, just hand the source to the single speaker.
    if (slots.size() == 1)
    {
        gains_out[0] = attenuation;
        return;
    }

    // Find the two non-LFE speakers whose azimuths bracket `theta`.
    // Build a sorted list of (azimuth, slot_index) for non-LFE entries.
    std::vector<std::pair<float, std::size_t>> sorted;
    sorted.reserve(slots.size());
    for (std::size_t i = 0; i < slots.size(); ++i)
        if (!slots[i].is_lfe)
            sorted.emplace_back(slots[i].azimuth_rad, i);
    std::ranges::sort(sorted,
                      [](const auto& a, const auto& b) { return a.first < b.first; });

    // Wrap so the source angle has bracketing speakers in the circular
    // sense: copy the first entry at the end with +2π, last at start
    // with -2π. Lets us scan once linearly.
    constexpr float kTwoPi = 6.2831853F;
    sorted.insert(sorted.begin(),
                  { sorted.back().first - kTwoPi, sorted.back().second });
    // NOLINTNEXTLINE(modernize-use-emplace): braced std::pair init can't be deduced by emplace_back.
    sorted.push_back({ sorted[1].first + kTwoPi, sorted[1].second });

    // Find the pair (a, b) with a.az <= theta <= b.az.
    for (std::size_t k = 0; k + 1 < sorted.size(); ++k)
    {
        if (theta >= sorted[k].first && theta <= sorted[k + 1].first)
        {
            const float span = sorted[k + 1].first - sorted[k].first;
            const float t = span > 1.0e-6F ? (theta - sorted[k].first) / span : 0.5F;
            // Constant-power pan: angle ∈ [0, π/2].
            const float angle = t * 1.5707963F;
            gains_out[sorted[k].second] += std::cos(angle) * attenuation;
            gains_out[sorted[k + 1].second] += std::sin(angle) * attenuation;
            return;
        }
    }
}

}  // namespace cd::audio
