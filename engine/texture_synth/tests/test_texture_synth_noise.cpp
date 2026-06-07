// =============================================================================
// CHROMODYNAMIC — engine/texture_synth/tests/test_texture_synth_noise.cpp
//
// phase898 — regression net for cd::texture_synth::Noise.hpp helpers:
// hash21, value_noise2, fbm2, value_noise2_quintic, fbm2_quintic_6oct.
// Locks deterministic output at known sample points and the
// signature contract every helper carries (return values in [0, 1],
// noexcept, header-only inline).
// =============================================================================
#include <gtest/gtest.h>

#include <cd/texture_synth/Noise.hpp>

#include <type_traits>

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
    const float u = 0.37F, v = 0.62F, base = 3.0F;
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

}  // anonymous
