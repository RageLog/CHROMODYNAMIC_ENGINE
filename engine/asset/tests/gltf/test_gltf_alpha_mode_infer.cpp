// =============================================================================
// CHROMODYNAMIC — cd::asset::gltf::infer_alpha_mode unit tests (T1.14, phase658)
//
// Validates the alpha-channel histogram heuristic that the glTF loader uses to
// auto-correct alphaMode="OPAQUE" when the linked base-color texture's alpha
// data clearly contradicts that authoring choice (Sponza-curtain case).
//
// The helper `infer_alpha_mode(span<const uint8_t> rgba_pixels)` is pure CPU
// state with no tinygltf / file-system dependency — tested in isolation here.
//
// Four cases (T1.14 spec):
//   1. All pixels at alpha=255               → kOpaque
//   2. >= 95 % at 255, at least one < 255   → kMask (cutout edges)
//   3. Mixed mid-alpha values (< 95 % at 255) → kBlend (semi-transparent)
//   4. All pixels at alpha=0 (degenerate)   → kOpaque (no evidence of intent)
// =============================================================================
#include <cd/asset/gltf/GltfLoader.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace
{

/// Build a flat RGBA8 pixel buffer where every pixel has the given alpha value
/// and RGB=(0,0,0). Pixel count = `count`.
[[nodiscard]] std::vector<std::uint8_t> make_uniform_alpha(std::size_t count, std::uint8_t alpha)
{
    std::vector<std::uint8_t> buf(count * 4U, std::uint8_t { 0 });
    for (std::size_t i = 0U; i < count; ++i)
        buf[i * 4U + 3U] = alpha;
    return buf;
}

/// Build a pixel buffer of `count` pixels where `fully_opaque_count` have
/// alpha=255 and the remaining have alpha=`edge_alpha`.
[[nodiscard]] std::vector<std::uint8_t>
make_mostly_opaque_with_edges(std::size_t count, std::size_t fully_opaque_count, std::uint8_t edge_alpha)
{
    std::vector<std::uint8_t> buf(count * 4U, std::uint8_t { 0 });
    for (std::size_t i = 0U; i < count; ++i)
    {
        const std::uint8_t a = (i < fully_opaque_count) ? std::uint8_t { 0xFF } : edge_alpha;
        buf[i * 4U + 3U] = a;
    }
    return buf;
}

}  // namespace

// -----------------------------------------------------------------------------
// Case 1: all pixels at alpha=255 → kOpaque
// -----------------------------------------------------------------------------

TEST(GltfInferAlphaMode, PureOpaqueHistogramReturnsKOpaque)
{
    const auto buf = make_uniform_alpha(/*count=*/256U, /*alpha=*/0xFFU);
    const auto result = cd::asset::gltf::infer_alpha_mode(
        std::span<const std::uint8_t>(buf)
    );
    EXPECT_EQ(result, cd::asset::gltf::GltfAlphaMode::kOpaque);
}

// -----------------------------------------------------------------------------
// Case 2: >= 95% at 255 + at least one < 255 → kMask (cutout edges)
//
// Use exactly 97 out of 100 pixels at 255 and 3 at alpha=0 (cutout).
// 97/100 = 97 % >= 95 % → kMask expected.
// -----------------------------------------------------------------------------

TEST(GltfInferAlphaMode, MostlyOpaqueWithCutoutEdgesReturnsMask)
{
    // 100 pixels: 97 fully opaque, 3 at alpha=0.
    const auto buf = make_mostly_opaque_with_edges(/*count=*/100U, /*fully_opaque_count=*/97U, /*edge_alpha=*/0U);
    const auto result = cd::asset::gltf::infer_alpha_mode(
        std::span<const std::uint8_t>(buf)
    );
    EXPECT_EQ(result, cd::asset::gltf::GltfAlphaMode::kMask);
}

// -----------------------------------------------------------------------------
// Case 3: mixed mid-alpha values (< 95 % at 255) → kBlend
//
// Use 50 pixels at 255 and 50 at alpha=128 (50 % opaque → well below 95 %).
// -----------------------------------------------------------------------------

TEST(GltfInferAlphaMode, MixedMidAlphaReturnsBlend)
{
    const auto buf = make_mostly_opaque_with_edges(/*count=*/100U, /*fully_opaque_count=*/50U, /*edge_alpha=*/128U);
    const auto result = cd::asset::gltf::infer_alpha_mode(
        std::span<const std::uint8_t>(buf)
    );
    EXPECT_EQ(result, cd::asset::gltf::GltfAlphaMode::kBlend);
}

// -----------------------------------------------------------------------------
// Case 4: all pixels at alpha=0 (degenerate — fully transparent) → kOpaque
//
// A texture where every pixel has alpha=0 is likely a placeholder or an
// incorrectly authored asset. Because there is no evidence of deliberate
// semi-transparency (no pixel at any intermediate alpha value), the heuristic
// treats this as the safe kOpaque fallback — "we have no information, don't
// route the mesh through the alpha-blend pass unexpectedly".
// -----------------------------------------------------------------------------

TEST(GltfInferAlphaMode, AllZeroAlphaDegenerate_ReturnsKOpaque)
{
    const auto buf = make_uniform_alpha(/*count=*/64U, /*alpha=*/0U);
    const auto result = cd::asset::gltf::infer_alpha_mode(
        std::span<const std::uint8_t>(buf)
    );
    EXPECT_EQ(result, cd::asset::gltf::GltfAlphaMode::kOpaque);
}
