// =============================================================================
// CHROMODYNAMIC — cd/math/Random.hpp
// Phase 24.A / Wave 190 — header-only PCG32 PRNG.
//
// PCG32 by Melissa O'Neill: small state (16 bytes), fast, statistically
// solid for game / sim use cases. Seedable for deterministic test runs.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>

namespace cd::math
{

class Random
{
public:
    Random() noexcept : Random(0x853c49e6748fea9bULL) {}
    explicit Random(std::uint64_t seed) noexcept
    {
        state_ = 0u;
        inc_ = (seed << 1u) | 1u;
        next_u32();
        state_ += seed;
        next_u32();
    }

    std::uint32_t next_u32() noexcept
    {
        const std::uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        const std::uint32_t xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
        const std::uint32_t rot = static_cast<std::uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }

    /// Uniform float in [0, 1).
    float next_float() noexcept
    {
        return static_cast<float>(next_u32()) / 4294967296.0F;
    }

    /// Uniform float in [lo, hi).
    float range(float lo, float hi) noexcept
    {
        return lo + (hi - lo) * next_float();
    }

    /// Uniform integer in [lo, hi). `hi` must be > `lo`.
    std::int32_t range_i(std::int32_t lo, std::int32_t hi) noexcept
    {
        const std::uint32_t span = static_cast<std::uint32_t>(hi - lo);
        return lo + static_cast<std::int32_t>(next_u32() % span);
    }

private:
    std::uint64_t state_ { 0 };
    std::uint64_t inc_ { 0xda3e39cb94b95bdbULL };
};

}  // namespace cd::math
