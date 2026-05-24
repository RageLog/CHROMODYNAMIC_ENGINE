// =============================================================================
// CHROMODYNAMIC — samples/hello_random
//
// Distribution-visualization smoke test for cd::math::Random (PCG32).
// Draws three distributions to stdout as ASCII histograms:
//   1. Uniform [0,1) — bin into 50 buckets, count 100k samples
//   2. Box-Muller normal (μ=0, σ=1) — bin into 60 buckets over ±3σ
//   3. Two seeds compared — show that PCG32 streams are decorrelated
//
// Also reports basic summary statistics (mean, variance, min, max)
// per stream so anyone running the demo can sanity-check that the
// PRNG output looks plausible without firing up matplotlib.
//
// Headless, deterministic. No file output — just stdout.
// =============================================================================
#include <cd/math/Random.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{

constexpr std::size_t kSampleCount = 100'000;

struct Stats
{
    double mean { 0.0 };
    double variance { 0.0 };
    double min { 0.0 };
    double max { 0.0 };
    std::size_t count { 0 };
};

[[nodiscard]] Stats compute_stats(const std::vector<float>& samples)
{
    Stats s {};
    if (samples.empty()) return s;
    s.count = samples.size();
    s.min = static_cast<double>(samples[0]);
    s.max = s.min;
    double sum = 0.0;
    for (const float x : samples)
    {
        const double v = static_cast<double>(x);
        sum += v;
        if (v < s.min) s.min = v;
        if (v > s.max) s.max = v;
    }
    s.mean = sum / static_cast<double>(s.count);
    double m2 = 0.0;
    for (const float x : samples)
    {
        const double d = static_cast<double>(x) - s.mean;
        m2 += d * d;
    }
    s.variance = m2 / static_cast<double>(s.count);
    return s;
}

void print_histogram(const char* title, const std::vector<float>& samples,
                     float lo, float hi, int bins, int max_bar_width = 60)
{
    std::printf("\n%s\n", title);
    std::printf("range  [%.3f, %.3f)   bins = %d   samples = %zu\n",
                static_cast<double>(lo), static_cast<double>(hi), bins, samples.size());

    std::vector<std::size_t> counts(static_cast<std::size_t>(bins), 0);
    const float inv_w = static_cast<float>(bins) / (hi - lo);
    for (const float x : samples)
    {
        if (x < lo || x >= hi) continue;
        const auto idx = static_cast<int>((x - lo) * inv_w);
        if (idx >= 0 && idx < bins)
            ++counts[static_cast<std::size_t>(idx)];
    }
    std::size_t peak = 0;
    for (const auto c : counts) if (c > peak) peak = c;
    if (peak == 0) { std::printf("  (no samples in range)\n"); return; }

    for (int i = 0; i < bins; ++i)
    {
        const float bin_lo = lo + static_cast<float>(i) * (hi - lo) / static_cast<float>(bins);
        const auto c = counts[static_cast<std::size_t>(i)];
        const int bar = static_cast<int>((static_cast<double>(c) / static_cast<double>(peak))
                                         * static_cast<double>(max_bar_width));
        std::printf("  %+6.2f | ", static_cast<double>(bin_lo));
        for (int k = 0; k < bar; ++k) std::printf("#");
        std::printf("  %zu\n", c);
    }

    const auto s = compute_stats(samples);
    std::printf("  mean=%.4f  var=%.4f  std=%.4f  min=%.4f  max=%.4f\n",
                s.mean, s.variance, std::sqrt(s.variance), s.min, s.max);
}

// Box-Muller: convert two uniform samples to one standard-normal sample.
// Cached the second so consecutive calls amortize the trig pair.
class BoxMullerNormal
{
public:
    explicit BoxMullerNormal(cd::math::Random& rng) noexcept : rng_(rng) {}

    [[nodiscard]] float next() noexcept
    {
        if (have_cached_)
        {
            have_cached_ = false;
            return cached_;
        }
        // Avoid log(0) by clamping u1 away from zero.
        float u1 = rng_.next_float();
        if (u1 < 1e-7F) u1 = 1e-7F;
        const float u2 = rng_.next_float();
        const float r  = std::sqrt(-2.0F * std::log(u1));
        const float t  = 6.28318530717958F * u2;
        cached_       = r * std::sin(t);
        have_cached_  = true;
        return r * std::cos(t);
    }

private:
    cd::math::Random& rng_;
    float             cached_      { 0.0F };
    bool              have_cached_ { false };
};

}  // namespace

int main()
{
    std::printf("=== hello_random — cd::math::Random (PCG32) distribution visualizer ===\n");

    // ---- 1. Uniform ---------------------------------------------------------
    {
        cd::math::Random rng { 0xA1B2C3D4u };
        std::vector<float> samples;
        samples.reserve(kSampleCount);
        for (std::size_t i = 0; i < kSampleCount; ++i)
            samples.push_back(rng.next_float());
        print_histogram("[Uniform 0..1)   (expected mean=0.5, var=1/12=0.0833)",
                        samples, 0.0F, 1.0F, /*bins=*/50);
    }

    // ---- 2. Standard normal via Box-Muller ----------------------------------
    {
        cd::math::Random rng { 0xDEADBEEFu };
        BoxMullerNormal normal { rng };
        std::vector<float> samples;
        samples.reserve(kSampleCount);
        for (std::size_t i = 0; i < kSampleCount; ++i)
            samples.push_back(normal.next());
        print_histogram("[Standard normal N(0,1) via Box-Muller]  (expected mean=0, var=1)",
                        samples, -3.0F, 3.0F, /*bins=*/60);
    }

    // ---- 3. Two seeds — stream decorrelation -------------------------------
    // Compute Pearson correlation between two independently-seeded streams
    // over 50k samples. Should be ~0 for a decent PRNG.
    {
        cd::math::Random rng_a { 0xFEEDFACEu };
        cd::math::Random rng_b { 0xCAFEBABEu };
        constexpr std::size_t n = 50'000;
        double sum_a = 0.0, sum_b = 0.0, sum_ab = 0.0;
        double sum_a2 = 0.0, sum_b2 = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double a = static_cast<double>(rng_a.next_float());
            const double b = static_cast<double>(rng_b.next_float());
            sum_a += a; sum_b += b; sum_ab += a * b;
            sum_a2 += a * a; sum_b2 += b * b;
        }
        const double nd = static_cast<double>(n);
        const double mean_a = sum_a / nd, mean_b = sum_b / nd;
        const double cov = sum_ab / nd - mean_a * mean_b;
        const double var_a = sum_a2 / nd - mean_a * mean_a;
        const double var_b = sum_b2 / nd - mean_b * mean_b;
        const double corr = (var_a > 0.0 && var_b > 0.0)
            ? cov / std::sqrt(var_a * var_b) : 0.0;
        std::printf("\n[Stream decorrelation: 0xFEEDFACE vs 0xCAFEBABE]\n");
        std::printf("  Pearson correlation (n=%zu): %.6f   (expected ≈ 0)\n", n, corr);
    }

    // ---- 4. Determinism check ----------------------------------------------
    // Two identically-seeded PCG32 instances must yield the same byte
    // stream — caller relies on this for replayable tests.
    {
        cd::math::Random a { 0x12345678u };
        cd::math::Random b { 0x12345678u };
        bool match = true;
        for (int i = 0; i < 1024; ++i)
        {
            if (a.next_u32() != b.next_u32()) { match = false; break; }
        }
        std::printf("\n[Determinism]  same seed -> same 1024-u32 stream: %s\n",
                    match ? "YES" : "NO");
        if (!match) return 1;
    }

    std::printf("\n[hello_random] OK\n");
    return 0;
}
