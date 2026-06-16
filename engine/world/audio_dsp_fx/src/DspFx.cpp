// =============================================================================
// CHROMODYNAMIC — cd/audio/dsp_fx/DspFx.cpp
// Phase 562 — cd::audio::dsp_fx implementation TU.
//
// The biquad filters + DelayLine are header-only (coefficient maths + span
// loops). This TU holds the Reverb FDN implementation that should not live in
// the header: the Schroeder/Freeverb-style network — RT60-derived feedback
// gains, mutually-prime comb/allpass delay tuning, and the per-sample comb +
// allpass tick math. SIMD-accelerated filter banks remain a perf
// promote-on-need (ADR-20260616-band3-world-scope §2.3).
// =============================================================================
#include <cd/audio/dsp_fx/DspFx.hpp>

namespace
{

bool is_prime(std::size_t n) noexcept
{
    if (n <= 1) return false;
    if (n <= 3) return true;
    if (n % 2 == 0 || n % 3 == 0) return false;
    for (std::size_t i = 5; i * i <= n; i += 6)
    {
        if (n % i == 0 || n % (i + 2) == 0)
            return false;
    }
    return true;
}

std::size_t next_prime(std::size_t n) noexcept
{
    if (n <= 2) return 2;
    if (n % 2 == 0) n++;
    while (!is_prime(n))
    {
        n += 2;
    }
    return n;
}

} // namespace

namespace cd::audio::dsp_fx
{

// --- CombFilter Implementation ---

void Reverb::CombFilter::configure(std::size_t delay_samples, float g, float d) noexcept
{
    delay_line.configure(delay_samples);
    feedback = g;
    damping = d;
    s_prev = 0.0F;
}

float Reverb::CombFilter::tick(float x) noexcept
{
    const float v = delay_line.read(delay_line.max_samples());
    const float s = v * (1.0F - damping) + s_prev * damping;
    s_prev = s;
    const float v_new = x + s * feedback;
    delay_line.write(v_new);
    return v;
}

void Reverb::CombFilter::reset() noexcept
{
    delay_line.reset();
    s_prev = 0.0F;
}

// --- AllpassFilter Implementation ---

void Reverb::AllpassFilter::configure(std::size_t delay_samples, float g) noexcept
{
    delay_line.configure(delay_samples);
    feedback = g;
}

float Reverb::AllpassFilter::tick(float x) noexcept
{
    const float w_del = delay_line.read(delay_line.max_samples());
    const float w = x + feedback * w_del;
    delay_line.write(w);
    return -feedback * w + w_del;
}

void Reverb::AllpassFilter::reset() noexcept
{
    delay_line.reset();
}

// --- Reverb Implementation ---

void Reverb::configure(const Config& cfg) noexcept
{
    config_ = cfg;

    const float sr = (cfg.sample_rate > 0.0F) ? cfg.sample_rate : 48000.0F;

    // Base mutually prime delay lengths at 48 kHz
    constexpr std::array<float, 4> kBaseCombDelays { 1117.0F, 1217.0F, 1291.0F, 1373.0F };
    constexpr std::array<float, 2> kBaseAllpassDelays { 241.0F, 281.0F };

    // room_size maps to delay scale [0.5, 1.5]
    const float delay_scale = 0.5F + cfg.room_size;

    // room_size maps to RT60 decay time [0.1, 2.1] seconds
    const float rt60 = 0.1F + 2.0F * cfg.room_size;

    // damping maps to [0.0, 0.5] stable range
    const float comb_damping = std::clamp(cfg.damping * 0.5F, 0.0F, 0.95F);

    // Setup comb filters
    for (std::size_t i = 0; i < combs_.size(); ++i)
    {
        const float delay_float = kBaseCombDelays[i] * (sr / 48000.0F) * delay_scale;
        const std::size_t target_delay = next_prime(static_cast<std::size_t>(std::round(delay_float)));

        // calculate feedback gain: g = 10^(-3 * delay_samples / (rt60 * sample_rate))
        const float exponent = -3.0F * static_cast<float>(target_delay) / (rt60 * sr);
        const float feedback_gain = std::clamp(std::pow(10.0F, exponent), 0.0F, 0.95F);

        combs_[i].configure(target_delay, feedback_gain, comb_damping);
    }

    // Setup allpass filters
    for (std::size_t i = 0; i < allpasses_.size(); ++i)
    {
        const float delay_float = kBaseAllpassDelays[i] * (sr / 48000.0F) * delay_scale;
        const std::size_t target_delay = next_prime(static_cast<std::size_t>(std::round(delay_float)));

        allpasses_[i].configure(target_delay, 0.5F);
    }
}

void Reverb::process(std::span<const float> input, std::span<float> output) noexcept
{
    const std::size_t n = std::min(input.size(), output.size());
    for (std::size_t i = 0; i < n; ++i)
    {
        const float x = input[i];

        // 1. Process parallel comb filters
        float comb_sum = 0.0F;
        for (auto& comb : combs_)
        {
            comb_sum += comb.tick(x);
        }

        // 2. Scale the parallel sum to keep gain stable
        const float ap_input = comb_sum * 0.25F;

        // 3. Process series allpass filters
        const float ap1 = allpasses_[0].tick(ap_input);
        const float ap2 = allpasses_[1].tick(ap1);

        // 4. Linear wet/dry blend
        output[i] = x * (1.0F - config_.wet_dry_mix) + ap2 * config_.wet_dry_mix;
    }
}

void Reverb::reset() noexcept
{
    for (auto& comb : combs_)
    {
        comb.reset();
    }
    for (auto& ap : allpasses_)
    {
        ap.reset();
    }
}

}  // namespace cd::audio::dsp_fx
