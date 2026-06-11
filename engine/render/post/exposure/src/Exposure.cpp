// =============================================================================
// CHROMODYNAMIC — cd/post/exposure/Exposure.cpp
//
// Pure CPU implementation. The GPU reduction kernel (mip-downsample
// chain producing one float) feeds compute_target_ev / apply_smoothing
// / compute_exposure_multiplier. The composite pass multiplies the HDR
// sample by `compute_exposure_multiplier(ev, settings)` BEFORE the
// tonemap operator, replacing the hard-coded `fx.exposure` slider that
// phase 450 dropped to 1.0.
// =============================================================================
#include <cd/post/exposure/Exposure.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace cd::post::exposure
{

namespace
{

constexpr float kLog2Inv         = std::numbers::log2e_v<float>;   // 1 / ln(2)
constexpr float kMinValidLuma    = 1.0E-6F;               // skip pixels at exactly 0 luma

[[nodiscard]] float luma_rec709(float r, float g, float b) noexcept
{
    return 0.2126F * r + 0.7152F * g + 0.0722F * b;
}

}  // namespace

float log_avg_luminance(const float* rgba_pixels, std::uint32_t pixel_count) noexcept
{
    if (rgba_pixels == nullptr || pixel_count == 0U)
    {
        return Settings::kSkipThreshold;
    }

    double accum = 0.0;
    std::uint32_t valid = 0U;
    for (std::uint32_t i = 0U; i < pixel_count; ++i)
    {
        const float r = rgba_pixels[i * 4U + 0U];
        const float g = rgba_pixels[i * 4U + 1U];
        const float b = rgba_pixels[i * 4U + 2U];
        const float l = luma_rec709(r, g, b);
        if (l < kMinValidLuma) continue;
        accum += static_cast<double>(std::log(l));
        ++valid;
    }
    if (valid == 0U)
    {
        return Settings::kSkipThreshold;
    }
    return static_cast<float>(accum / static_cast<double>(valid)) * kLog2Inv;
}

float compute_target_ev(float log_avg_luminance_val, float prev_ev, const Settings& s) noexcept
{
    if (log_avg_luminance_val < Settings::kSkipThreshold + 1.0F)
    {
        return prev_ev;
    }
    // Reinhard 2002 key: scene's avg luminance should map to s.key
    // (0.18 grey). exposure_scalar = key / avg_lum.
    // In EV (stops): EV = log2(key) - log2(avg_lum)
    //                   = log2(key) - log_avg_luminance_val
    const float key_log2 = std::log2(std::max(s.key, 1.0E-4F));
    return key_log2 - log_avg_luminance_val;
}

float apply_smoothing(float prev_ev, float target_ev, float dt_seconds, const Settings& s) noexcept
{
    const float diff = target_ev - prev_ev;
    const bool brightening = diff < 0.0F;  // target is BRIGHTER (lower EV needed)
    const float speed = brightening ? s.adapt_speed_up : s.adapt_speed_down;
    // EMA: blend = 1 - exp(-speed * dt). When dt*speed is small, blend is
    // small (slow adaptation). When large, blend approaches 1 (snap).
    const float dt = std::max(dt_seconds, 0.0F);
    const float blend = 1.0F - std::exp(-std::max(speed, 0.0F) * dt);
    const float smoothed = prev_ev + diff * blend;
    return std::clamp(smoothed, s.min_ev, s.max_ev);
}

float compute_exposure_multiplier(float ev, const Settings& s) noexcept
{
    // exposure = 2^EV / key. The /key normalises so the linear scalar
    // brings an "average-grey" scene to ~1.0 pre-tonemap, then the
    // composite tonemap maps 1.0 -> 0.18 (the SDR mid-grey).
    return std::exp2(ev) / std::max(s.key, 1.0E-4F);
}

float update_ev(float log_avg_luminance_val, float prev_ev, float dt_seconds,
                const Settings& s) noexcept
{
    const float target = compute_target_ev(log_avg_luminance_val, prev_ev, s);
    return apply_smoothing(prev_ev, target, dt_seconds, s);
}

}  // namespace cd::post::exposure
