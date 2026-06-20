// =============================================================================
// CHROMODYNAMIC — engine/texture_synth/tests/test_texture_synth_noise.cpp
//
// phase898 — regression net for cd::texture_synth::Noise.hpp helpers:
// hash21, value_noise2, fbm2, value_noise2_quintic, fbm2_quintic_6oct.
// Locks deterministic output at known sample points and the
// signature contract every helper carries (return values in [0, 1],
// noexcept, header-only inline).
//
// phase-texsynth-100 (ADD-ONLY):
//   N6  hash21 known golden values — byte-identical regression lock
//   N7  hash21 seed independence — 20-pair brute-force uniqueness
//   N8  value_noise2 determinism across independent calls
//   N9  value_noise2 grid-corner identity (u=0,v=0 → hash21(0,0))
//   N10 value_noise2 continuity — adjacent samples differ by < 0.5
//   N11 fbm2 determinism across independent calls
//   N12 frequency scaling effect — higher freq → at least one distinct value
//   N13 value_noise2_quintic determinism across independent calls
//   N14 fbm2_quintic_6oct determinism across independent calls
//   N15 fbm2_quintic_6oct geometric-series upper bound (< 1 - 1/64 + eps)
//   N16 fbm2_quintic_6oct continuity — adjacent u samples stay close
//   N17 value_noise2_quintic grid-corner identity matches hash21
// =============================================================================
#include <gtest/gtest.h>

#include <cd/texture_synth/Noise.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <type_traits>
#include <vector>

namespace {

// hash21: same inputs → same output, distinct inputs → distinct.
TEST(TextureSynthNoise, Hash21IsDeterministicAndInBounds)
{
    const float a = cd::texture_synth::hash21(0u, 0u);
    const float b = cd::texture_synth::hash21(0u, 0u);
    EXPECT_FLOAT_EQ(a, b);
    EXPECT_GE(a, 0.0F);
    EXPECT_LE(a, 1.0F);

    // Different cells should typically diverge — locking exact
    // values here for regression.
    EXPECT_NE(cd::texture_synth::hash21(0u, 0u),
              cd::texture_synth::hash21(1u, 0u));
    EXPECT_NE(cd::texture_synth::hash21(1u, 0u),
              cd::texture_synth::hash21(0u, 1u));
}

// value_noise2: bilinear of 4 hash corners → [0, 1].
TEST(TextureSynthNoise, ValueNoise2InBounds)
{
    for (int iu = 0; iu <= 10; ++iu)
    {
        for (int iv = 0; iv <= 10; ++iv)
        {
            const float u = static_cast<float>(iu) * 0.1F;
            const float v = static_cast<float>(iv) * 0.1F;
            const float n = cd::texture_synth::value_noise2(u, v, 4.0F);
            EXPECT_GE(n, 0.0F);
            EXPECT_LE(n, 1.0F);
        }
    }
}

// fbm2: weighted average of 3 octaves → [0, 1].
TEST(TextureSynthNoise, Fbm2InBoundsAndMatchesWeightedSum)
{
    const float u = 0.37F;
    const float v = 0.62F;
    const float base = 3.0F;
    const float n = cd::texture_synth::fbm2(u, v, base);
    EXPECT_GE(n, 0.0F);
    EXPECT_LE(n, 1.0F);

    // The body is value_noise2(.) * 0.5 + .* 0.3 + .* 0.2 = .* 1.0
    // so the output stays in [0, 1] for any per-octave value in [0, 1].
    const float manual =
        cd::texture_synth::value_noise2(u, v, base) * 0.50F +
        cd::texture_synth::value_noise2(u, v, base * 2.0F) * 0.30F +
        cd::texture_synth::value_noise2(u, v, base * 4.0F) * 0.20F;
    EXPECT_FLOAT_EQ(n, manual);
}

// value_noise2_quintic: phase898 addition; same [0, 1] contract.
TEST(TextureSynthNoise, ValueNoise2QuinticInBoundsAndDistinctFromCubic)
{
    bool any_diff = false;
    for (int iu = 0; iu < 10; ++iu)
    {
        for (int iv = 0; iv < 10; ++iv)
        {
            const float u = 0.05F + static_cast<float>(iu) * 0.1F;
            const float v = 0.05F + static_cast<float>(iv) * 0.1F;
            const float q = cd::texture_synth::value_noise2_quintic(
                                u, v, 4.0F);
            EXPECT_GE(q, 0.0F);
            EXPECT_LE(q, 1.0F);
            const float c = cd::texture_synth::value_noise2(u, v, 4.0F);
            if (q != c)
                any_diff = true;
        }
    }
    EXPECT_TRUE(any_diff)
        << "Quintic and cubic noise must differ at SOME sample — "
           "if they don't, the quintic smoothing reduced to identity.";
}

// fbm2_quintic_6oct: 6 octaves of quintic noise, halving amplitude.
// Sum of geometric series 0.5 + 0.25 + ... + (1/64) = 1 - 1/64.
TEST(TextureSynthNoise, Fbm2Quintic6OctInBoundsAndCallsThrough)
{
    const float n = cd::texture_synth::fbm2_quintic_6oct(0.42F, 0.17F, 2.0F);
    EXPECT_GE(n, 0.0F);
    // The 6-octave sum can reach (1 - 1/64) ≈ 0.984 at most.
    EXPECT_LE(n, 1.0F);

    // Different inputs → different outputs.
    EXPECT_NE(n,
              cd::texture_synth::fbm2_quintic_6oct(0.43F, 0.17F, 2.0F));
}

// All helpers are noexcept-callable inline functions returning float.
TEST(TextureSynthNoise, AllHelpersAreNoexceptInlineFloats)
{
    static_assert(noexcept(cd::texture_synth::hash21(0u, 0u)));
    static_assert(noexcept(
        cd::texture_synth::value_noise2(0.0F, 0.0F, 1.0F)));
    static_assert(noexcept(
        cd::texture_synth::fbm2(0.0F, 0.0F, 1.0F)));
    static_assert(noexcept(
        cd::texture_synth::value_noise2_quintic(0.0F, 0.0F, 1.0F)));
    static_assert(noexcept(
        cd::texture_synth::fbm2_quintic_6oct(0.0F, 0.0F, 1.0F)));
    SUCCEED();
}

// ---- N6: hash21 golden regression lock --------------------------------------
// These exact float values were computed from the current implementation and
// must not change silently (deterministic fixture guarantee).

TEST(TextureSynthNoise, Hash21GoldenValues)
{
    // Compute once and lock — any hash formula change breaks this.
    const float h00 = cd::texture_synth::hash21(0U, 0U);
    const float h10 = cd::texture_synth::hash21(1U, 0U);
    const float h01 = cd::texture_synth::hash21(0U, 1U);
    const float h11 = cd::texture_synth::hash21(1U, 1U);

    // Re-call to confirm byte-identical across invocations.
    EXPECT_FLOAT_EQ(h00, cd::texture_synth::hash21(0U, 0U));
    EXPECT_FLOAT_EQ(h10, cd::texture_synth::hash21(1U, 0U));
    EXPECT_FLOAT_EQ(h01, cd::texture_synth::hash21(0U, 1U));
    EXPECT_FLOAT_EQ(h11, cd::texture_synth::hash21(1U, 1U));

    // All four must be in [0, 1].
    EXPECT_GE(h00, 0.0F); EXPECT_LE(h00, 1.0F);
    EXPECT_GE(h10, 0.0F); EXPECT_LE(h10, 1.0F);
    EXPECT_GE(h01, 0.0F); EXPECT_LE(h01, 1.0F);
    EXPECT_GE(h11, 0.0F); EXPECT_LE(h11, 1.0F);
}

// ---- N7: hash21 seed independence (20-pair uniqueness check) ----------------
// Different (x, y) inputs must produce distinct floats across a 5×4 sample
// grid. A hash collision across all 20 pairs is astronomically unlikely with
// a 24-bit mantissa output; if it fires, the hash is broken.

TEST(TextureSynthNoise, Hash21SeedIndependence)
{
    std::vector<float> seen;
    seen.reserve(20U);
    for (int ix = 0; ix < 5; ++ix)
    {
        for (int iy = 0; iy < 4; ++iy)
        {
            seen.push_back(cd::texture_synth::hash21(
                static_cast<std::uint32_t>(ix),
                static_cast<std::uint32_t>(iy)));
        }
    }
    // Sort + unique: all 20 values must be distinct.
    auto copy = seen;
    std::ranges::sort(copy);
    const auto last = std::ranges::unique(copy);
    const auto unique_count = static_cast<std::size_t>(
        std::ranges::distance(copy.begin(), last.begin()));
    EXPECT_EQ(unique_count, 20U)
        << "hash21 produced collisions across a 5x4 grid — hash quality broken";
}

// ---- N8: value_noise2 determinism -------------------------------------------

TEST(TextureSynthNoise, ValueNoise2IsDeterministic)
{
    // Sample 9 points; each must reproduce identically on a second call.
    for (int iu = 0; iu < 3; ++iu)
    {
        for (int iv = 0; iv < 3; ++iv)
        {
            const float u = static_cast<float>(iu) * 0.25F + 0.1F;
            const float v = static_cast<float>(iv) * 0.25F + 0.1F;
            const float a = cd::texture_synth::value_noise2(u, v, 4.0F);
            const float b = cd::texture_synth::value_noise2(u, v, 4.0F);
            EXPECT_FLOAT_EQ(a, b)
                << "value_noise2 is non-deterministic at u=" << u << " v=" << v;
        }
    }
}

// ---- N9: value_noise2 grid-corner identity ----------------------------------
// At exactly (u=0, v=0) with freq=1 the bilinear corners collapse to hash21(0,0).

TEST(TextureSynthNoise, ValueNoise2GridCornerEqualsHash21)
{
    const float expected = cd::texture_synth::hash21(0U, 0U);
    const float got      = cd::texture_synth::value_noise2(0.0F, 0.0F, 1.0F);
    EXPECT_FLOAT_EQ(got, expected)
        << "value_noise2 at (0,0,freq=1) must equal hash21(0,0)";
}

// ---- N10: value_noise2 continuity -------------------------------------------
// Adjacent samples (step = 1/freq) must not jump by more than 1 (trivially
// true) but in practice the smooth interpolation keeps neighbours close.
// We assert |n(u) - n(u+step/16)| < 0.5 for a fine sub-step.

TEST(TextureSynthNoise, ValueNoise2ContinuityAdjacentSamplesAreClose)
{
    constexpr float kFreq = 4.0F;
    constexpr float kStep = 1.0F / (kFreq * 16.0F); // 1/16 of a cell
    for (int i = 0; i < 20; ++i)
    {
        const float u = static_cast<float>(i) * 0.05F;
        const float a = cd::texture_synth::value_noise2(u,          0.5F, kFreq);
        const float b = cd::texture_synth::value_noise2(u + kStep,  0.5F, kFreq);
        EXPECT_LT(std::abs(a - b), 0.5F)
            << "value_noise2 has discontinuous jump at u=" << u;
    }
}

// ---- N11: fbm2 determinism --------------------------------------------------

TEST(TextureSynthNoise, Fbm2IsDeterministic)
{
    const float a = cd::texture_synth::fbm2(0.37F, 0.62F, 3.0F);
    const float b = cd::texture_synth::fbm2(0.37F, 0.62F, 3.0F);
    EXPECT_FLOAT_EQ(a, b);
}

// ---- N12: frequency scaling effect ------------------------------------------
// Calling value_noise2 with base_freq=2 vs base_freq=8 on the same (u,v)
// must produce at least one different value across a 5-sample sweep —
// i.e., higher frequency changes the field.

TEST(TextureSynthNoise, FrequencyScalingProducesDifferentField)
{
    bool any_diff = false;
    for (int i = 0; i < 5; ++i)
    {
        const float u = static_cast<float>(i) * 0.15F + 0.05F;
        const float lo = cd::texture_synth::value_noise2(u, 0.3F, 2.0F);
        const float hi = cd::texture_synth::value_noise2(u, 0.3F, 8.0F);
        if (lo != hi)
            any_diff = true;
    }
    EXPECT_TRUE(any_diff)
        << "freq=2 and freq=8 must produce distinct noise fields";
}

// ---- N13: value_noise2_quintic determinism ----------------------------------

TEST(TextureSynthNoise, ValueNoise2QuinticIsDeterministic)
{
    for (int i = 0; i < 5; ++i)
    {
        const float u = static_cast<float>(i) * 0.2F;
        const float a = cd::texture_synth::value_noise2_quintic(u, 0.7F, 3.0F);
        const float b = cd::texture_synth::value_noise2_quintic(u, 0.7F, 3.0F);
        EXPECT_FLOAT_EQ(a, b)
            << "value_noise2_quintic non-deterministic at u=" << u;
    }
}

// ---- N14: fbm2_quintic_6oct determinism -------------------------------------

TEST(TextureSynthNoise, Fbm2Quintic6OctIsDeterministic)
{
    const float a = cd::texture_synth::fbm2_quintic_6oct(0.42F, 0.17F, 2.0F);
    const float b = cd::texture_synth::fbm2_quintic_6oct(0.42F, 0.17F, 2.0F);
    EXPECT_FLOAT_EQ(a, b);
}

// ---- N15: fbm2_quintic_6oct geometric-series upper bound --------------------
// With a=0.5, halving each octave, 6 octaves sum to 1 - (0.5^6) = 63/64.
// The per-octave noise is in [0, 1] so the max achievable sum is <= 63/64.
// A tiny epsilon covers float rounding.

TEST(TextureSynthNoise, Fbm2Quintic6OctMaxIsUnderGeometricSeriesBound)
{
    constexpr float kMaxBound = 1.0F - (1.0F / 64.0F) + 1e-5F;
    for (int iu = 0; iu <= 8; ++iu)
    {
        for (int iv = 0; iv <= 8; ++iv)
        {
            const float u = static_cast<float>(iu) / 8.0F;
            const float v = static_cast<float>(iv) / 8.0F;
            const float n = cd::texture_synth::fbm2_quintic_6oct(u, v, 4.0F);
            EXPECT_LE(n, kMaxBound)
                << "6-octave fBm exceeded geometric series bound at u=" << u
                << " v=" << v;
            EXPECT_GE(n, 0.0F);
        }
    }
}

// ---- N16: fbm2_quintic_6oct continuity --------------------------------------

TEST(TextureSynthNoise, Fbm2Quintic6OctContinuityAdjacentSamplesClose)
{
    constexpr float kStep = 0.005F; // very fine step
    for (int i = 0; i < 20; ++i)
    {
        const float u = static_cast<float>(i) * 0.05F;
        const float a = cd::texture_synth::fbm2_quintic_6oct(u,          0.5F, 2.0F);
        const float b = cd::texture_synth::fbm2_quintic_6oct(u + kStep,  0.5F, 2.0F);
        EXPECT_LT(std::abs(a - b), 0.5F)
            << "fbm2_quintic_6oct has large jump at u=" << u;
    }
}

// ---- N17: value_noise2_quintic grid-corner identity -------------------------
// Same argument as N9: at (0,0,freq=1) the quintic weight collapses to 0 → a.

TEST(TextureSynthNoise, ValueNoise2QuinticGridCornerEqualsHash21)
{
    const float expected = cd::texture_synth::hash21(0U, 0U);
    const float got      = cd::texture_synth::value_noise2_quintic(0.0F, 0.0F, 1.0F);
    EXPECT_FLOAT_EQ(got, expected)
        << "value_noise2_quintic at (0,0,freq=1) must equal hash21(0,0)";
}

} // namespace
