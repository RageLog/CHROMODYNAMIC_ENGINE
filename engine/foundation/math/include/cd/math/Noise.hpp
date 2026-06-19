// =============================================================================
// CHROMODYNAMIC — cd/math/Noise.hpp
// Phase 48.B / Wave 216 — 1D/2D/3D value noise (lattice-interpolated).
//
// Value noise = hash(integer lattice point) → [0, 1] random float;
// fractional position smoothly interpolated (smoothstep) between
// neighboring lattice samples.
//
// Cheaper than Perlin/simplex (no gradients, no curl), good enough for
// terrain heightmap previews, particle randomness, and procedural
// texture stand-ins. Caller-supplied uint32 seed makes results
// deterministic; same seed + same coordinates always yield same value.
//
// API:
//   * `noise1d(x, seed)` — float ∈ [0, 1]
//   * `noise2d(x, y, seed)` — float ∈ [0, 1]
//   * `noise3d(x, y, z, seed)` — float ∈ [0, 1]
//   * `fbm2d(x, y, seed, octaves)` — fractal sum (lacunarity 2,
//     persistence 0.5 by default)
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cmath>
#include <cstdint>

namespace cd::math
{

namespace detail
{

[[nodiscard]] constexpr std::uint32_t mix32(std::uint32_t x) noexcept
{
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

[[nodiscard]] constexpr float hash_to_unit(std::uint32_t h) noexcept
{
    return static_cast<float>(h) / 4294967295.0F;
}

[[nodiscard]] constexpr std::uint32_t hash_lattice(std::int32_t ix, std::int32_t iy, std::int32_t iz, std::uint32_t seed) noexcept
{
    auto h = seed;
    h = mix32(h ^ static_cast<std::uint32_t>(ix));
    h = mix32(h ^ static_cast<std::uint32_t>(iy));
    h = mix32(h ^ static_cast<std::uint32_t>(iz));
    return h;
}

[[nodiscard]] constexpr float smoothstep(float t) noexcept
{
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] constexpr std::int32_t floor_i(float v) noexcept
{
    const auto i = static_cast<std::int32_t>(v);
    return (v < static_cast<float>(i)) ? (i - 1) : i;
}

}  // namespace detail

[[nodiscard]] inline float noise1d(float x, std::uint32_t seed) noexcept
{
    const auto i = detail::floor_i(x);
    const float f = x - static_cast<float>(i);
    const float t = detail::smoothstep(f);
    const float a = detail::hash_to_unit(detail::hash_lattice(i,     0, 0, seed));
    const float b = detail::hash_to_unit(detail::hash_lattice(i + 1, 0, 0, seed));
    return a + (b - a) * t;
}

[[nodiscard]] inline float noise2d(float x, float y, std::uint32_t seed) noexcept
{
    const auto ix = detail::floor_i(x);
    const auto iy = detail::floor_i(y);
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);
    const float tx = detail::smoothstep(fx);
    const float ty = detail::smoothstep(fy);

    const float a = detail::hash_to_unit(detail::hash_lattice(ix,     iy,     0, seed));
    const float b = detail::hash_to_unit(detail::hash_lattice(ix + 1, iy,     0, seed));
    const float c = detail::hash_to_unit(detail::hash_lattice(ix,     iy + 1, 0, seed));
    const float d = detail::hash_to_unit(detail::hash_lattice(ix + 1, iy + 1, 0, seed));
    const float ab = a + (b - a) * tx;
    const float cd = c + (d - c) * tx;
    return ab + (cd - ab) * ty;
}

[[nodiscard]] inline float noise3d(float x, float y, float z, std::uint32_t seed) noexcept
{
    const auto ix = detail::floor_i(x);
    const auto iy = detail::floor_i(y);
    const auto iz = detail::floor_i(z);
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);
    const float fz = z - static_cast<float>(iz);
    const float tx = detail::smoothstep(fx);
    const float ty = detail::smoothstep(fy);
    const float tz = detail::smoothstep(fz);

    // Trilinear blend of the eight cube corners (z-slice 0 then z-slice 1).
    const float c000 = detail::hash_to_unit(detail::hash_lattice(ix,     iy,     iz,     seed));
    const float c100 = detail::hash_to_unit(detail::hash_lattice(ix + 1, iy,     iz,     seed));
    const float c010 = detail::hash_to_unit(detail::hash_lattice(ix,     iy + 1, iz,     seed));
    const float c110 = detail::hash_to_unit(detail::hash_lattice(ix + 1, iy + 1, iz,     seed));
    const float c001 = detail::hash_to_unit(detail::hash_lattice(ix,     iy,     iz + 1, seed));
    const float c101 = detail::hash_to_unit(detail::hash_lattice(ix + 1, iy,     iz + 1, seed));
    const float c011 = detail::hash_to_unit(detail::hash_lattice(ix,     iy + 1, iz + 1, seed));
    const float c111 = detail::hash_to_unit(detail::hash_lattice(ix + 1, iy + 1, iz + 1, seed));

    const float x00 = c000 + (c100 - c000) * tx;
    const float x10 = c010 + (c110 - c010) * tx;
    const float x01 = c001 + (c101 - c001) * tx;
    const float x11 = c011 + (c111 - c011) * tx;
    const float y0  = x00 + (x10 - x00) * ty;
    const float y1  = x01 + (x11 - x01) * ty;
    return y0 + (y1 - y0) * tz;
}

[[nodiscard]] inline float fbm2d(float x, float y, std::uint32_t seed,
                                 int octaves = 4,
                                 float lacunarity = 2.0F,
                                 float persistence = 0.5F) noexcept
{
    float total = 0.0F;
    float frequency = 1.0F;
    float amplitude = 1.0F;
    float max_amp = 0.0F;
    for (int i = 0; i < octaves; ++i)
    {
        total += noise2d(x * frequency, y * frequency, seed + static_cast<std::uint32_t>(i)) * amplitude;
        max_amp += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return (max_amp > 0.0F) ? (total / max_amp) : 0.0F;
}

}  // namespace cd::math
