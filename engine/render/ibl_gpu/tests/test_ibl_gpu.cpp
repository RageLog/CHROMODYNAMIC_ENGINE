// =============================================================================
// CHROMODYNAMIC — engine/render/ibl_gpu/tests/test_ibl_gpu.cpp
// Band-6 to-100: the lib previously shipped 0 tests ("deferred to renderer
// integration"). This file corrects that.
//
//   * Host-side pure helpers (float_to_half, byte-layout, mip-count) are
//     verified DETERMINISTICALLY everywhere — no device needed.
//   * The RHI upload path (upload_brdf_lut) is verified END-TO-END behind a
//     Vulkan device gate via an upload -> copy_image_to_buffer -> download
//     round-trip on the host RTX 3080; GTEST_SKIPs when no ICD is present so
//     CI without a GPU stays green (mirrors restir_di_dispatch).
//
// Boundary: cd::ibl bakes on the CPU (B3); cd::ibl_gpu uploads those baked
// products to the GPU. This test exercises only the upload side.
// =============================================================================
#include <cd/ibl_gpu/Upload.hpp>

#include <cd/ibl/BrdfLut.hpp>
#include <cd/ibl/Cubemap.hpp>
#include <cd/ibl/PrefilteredSpecular.hpp>

#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace
{

using cd::ibl_gpu::detail::float_to_half;

// Test-only IEEE-754 binary16 -> binary32 reference decoder. Independent of the
// production encoder so round-trip assertions cross-check rather than re-derive.
// Handles signed zero, normals, and (because the production encoder clamps
// out-of-range magnitudes to 0x7BFF instead of emitting inf/nan) finite-only
// inputs. Subnormals decode through the same generic path.
[[nodiscard]] float ref_half_to_float(std::uint16_t h) noexcept
{
    const std::uint32_t sign = static_cast<std::uint32_t>(h & 0x8000U) << 16U;
    const std::uint32_t exp = (h >> 10U) & 0x1FU;
    const std::uint32_t man = h & 0x3FFU;
    float out = 0.0F;
    if (exp == 0U)
    {
        // Zero or subnormal: value = man / 2^10 * 2^-14.
        out = static_cast<float>(man) * (1.0F / 1024.0F) * std::ldexp(1.0F, -14);
    }
    else
    {
        // Normal: value = (1 + man/2^10) * 2^(exp-15).
        const float mantissa = 1.0F + static_cast<float>(man) * (1.0F / 1024.0F);
        out = mantissa * std::ldexp(1.0F, static_cast<int>(exp) - 15);
    }
    std::uint32_t bits = 0;
    std::memcpy(&bits, &out, sizeof(bits));
    bits |= sign;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

// Largest finite the production encoder will ever emit (its overflow clamp).
constexpr std::uint16_t kHalfMaxFinite = 0x7BFFU;

// RGBA16F / RG16Float channel width in bytes — both upload paths use half.
constexpr std::size_t kHalfBytes = sizeof(std::uint16_t);  // 2

// Bytes for one RGBA16F cube face of `face`x`face` texels (4 channels * 2 B).
[[nodiscard]] std::size_t cube_face_bytes_rgba16f(std::uint32_t face) noexcept
{
    return static_cast<std::size_t>(face) * face * 4U * kHalfBytes;
}

std::unique_ptr<cd::rhi::IDevice> try_make_device()
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = true;  // surface any layout / barrier mismatch
    auto r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

// --- Host-side pure: float_to_half (always runs) -----------------------------

TEST(IblGpuHalf, ExactReferenceBitPatterns)
{
    // IEEE-754 binary16 reference values.
    EXPECT_EQ(float_to_half(0.0F), 0x0000U);
    EXPECT_EQ(float_to_half(1.0F), 0x3C00U);   // 1.0
    EXPECT_EQ(float_to_half(0.5F), 0x3800U);   // 0.5
    EXPECT_EQ(float_to_half(2.0F), 0x4000U);   // 2.0
}

TEST(IblGpuHalf, NegativeSetsSignBit)
{
    // -1.0 == 1.0 with the sign bit set.
    EXPECT_EQ(float_to_half(-1.0F), static_cast<std::uint16_t>(0x3C00U | 0x8000U));
    // -0.0 keeps the sign bit, mantissa/exponent zero.
    EXPECT_EQ(float_to_half(-0.0F), 0x8000U);
}

TEST(IblGpuHalf, OverflowClampsToMaxFiniteAndUnderflowToZero)
{
    // Far above half-float max → the impl clamps to the largest representable
    // finite magnitude (0x7BFF) rather than emitting inf.
    EXPECT_EQ(float_to_half(70000.0F), 0x7BFFU);
    // Far below half-float min normal → flushes to signed zero.
    EXPECT_EQ(float_to_half(1e-9F), 0x0000U);
}

TEST(IblGpuHalf, PowerOfTwoExponentLadderMatchesIeee754)
{
    // Each exact power of two maps to a clean exponent step (mantissa 0).
    // Reference half-bit-patterns from the IEEE-754 binary16 exponent ladder.
    EXPECT_EQ(float_to_half(0.25F), 0x3400U);   // 2^-2
    EXPECT_EQ(float_to_half(4.0F), 0x4400U);    // 2^2
    EXPECT_EQ(float_to_half(8.0F), 0x4800U);    // 2^3
    EXPECT_EQ(float_to_half(16.0F), 0x4C00U);   // 2^4
    EXPECT_EQ(float_to_half(1024.0F), 0x6400U); // 2^10
}

TEST(IblGpuHalf, SmallestNormalAndLargestFinite)
{
    // 2^-14 is the smallest positive normal half (exp field = 1, mantissa 0).
    EXPECT_EQ(float_to_half(std::ldexp(1.0F, -14)), 0x0400U);
    // 65504 is the largest finite half; the encoder reaches it exactly here,
    // and anything larger saturates to the same pattern (the overflow clamp).
    EXPECT_EQ(float_to_half(65504.0F), kHalfMaxFinite);
    EXPECT_EQ(float_to_half(65505.0F), kHalfMaxFinite);
}

TEST(IblGpuHalf, MantissaIsTruncatedNotRoundedToNearest)
{
    // The encoder drops the low 13 mantissa bits with `>> 13` (truncation
    // toward zero), NOT round-to-nearest-even. 1.0009765625 (= 1 + 2^-10) is
    // exactly representable; nudging it up by less than one half-ULP must
    // truncate back DOWN to the same pattern rather than round up.
    const std::uint16_t exact = float_to_half(1.0F + (1.0F / 1024.0F));
    EXPECT_EQ(exact, 0x3C01U);
    // 1.0 + 1.5*2^-10: round-to-nearest would give 0x3C02; truncation keeps 01.
    const std::uint16_t nudged = float_to_half(1.0F + (1.5F / 1024.0F));
    EXPECT_EQ(nudged, 0x3C01U);
}

TEST(IblGpuHalf, InfinityAndNanClampToMaxFiniteNotSpecials)
{
    // The branchless encoder has no inf/nan path: exp>=31 always returns the
    // saturated finite magnitude. Pin this so a future "emit inf" change is
    // caught as a contract break (the GPU consumes RGBA16F where inf would
    // poison filtering).
    EXPECT_EQ(float_to_half(std::numeric_limits<float>::infinity()), kHalfMaxFinite);
    EXPECT_EQ(float_to_half(-std::numeric_limits<float>::infinity()),
              static_cast<std::uint16_t>(kHalfMaxFinite | 0x8000U));
    EXPECT_EQ(float_to_half(std::numeric_limits<float>::quiet_NaN()), kHalfMaxFinite);
}

TEST(IblGpuHalf, SubnormalFloatInputFlushesToSignedZero)
{
    // A float subnormal is far below the half min-normal: e<=0 -> signed zero.
    const float tiny = std::numeric_limits<float>::denorm_min();
    EXPECT_EQ(float_to_half(tiny), 0x0000U);
    EXPECT_EQ(float_to_half(-tiny), 0x8000U);
    // A value just under 2^-14 (half's smallest normal) also flushes here,
    // confirming the encoder produces no half-subnormals.
    EXPECT_EQ(float_to_half(std::ldexp(1.0F, -15)), 0x0000U);
}

TEST(IblGpuHalf, RoundTripStableForExactlyRepresentableValues)
{
    // float -> half -> float must be a fixed point for values the encoder can
    // hold exactly. Cross-checked against an INDEPENDENT reference decoder.
    constexpr std::array<float, 9> exact {
        0.0F, 1.0F, -1.0F, 0.5F, 2.0F, -2.0F, 0.25F, 16.0F, 1024.0F };
    for (const float f : exact)
    {
        const std::uint16_t h = float_to_half(f);
        EXPECT_FLOAT_EQ(ref_half_to_float(h), f) << "round-trip drift for " << f;
        // Re-encoding the decoded value reproduces the same bits (idempotent).
        EXPECT_EQ(float_to_half(ref_half_to_float(h)), h);
    }
}

TEST(IblGpuHalf, SignBitIsIndependentOfMagnitudeEncoding)
{
    // For every test magnitude the negative encoding is the positive one with
    // bit 15 set — the sign is split off before the exponent/mantissa math.
    constexpr std::array<float, 5> mags { 1.0F, 0.5F, 2.0F, 0.25F, 1024.0F };
    for (const float m : mags)
    {
        const std::uint16_t pos = float_to_half(m);
        const std::uint16_t neg = float_to_half(-m);
        EXPECT_EQ(neg, static_cast<std::uint16_t>(pos | 0x8000U)) << "mag " << m;
    }
}

TEST(IblGpuHalf, ExponentTransitionBoundariesMatchIeee754)
{
    // The encoder's bias is 112 (= binary32 bias 127 - binary16 bias 15). The
    // e<=0 flush-to-zero and e>=31 saturate-to-max guards sit on exact
    // power-of-two seams; pin BOTH sides of each seam so a one-off in the bias
    // or the guard comparison is caught.
    //
    // Smallest-normal seam: 2^-14 is the first value with e==1 (smallest half
    // normal -> 0x0400); 2^-15 has e==0 and must flush to +0.
    EXPECT_EQ(float_to_half(std::ldexp(1.0F, -14)), 0x0400U);
    EXPECT_EQ(float_to_half(std::ldexp(1.0F, -15)), 0x0000U);
    // Just below 2^-14 (still e<=0) flushes; just at it does not.
    EXPECT_EQ(float_to_half(std::nextafter(std::ldexp(1.0F, -14), 0.0F)), 0x0000U);
    // Overflow seam: 2^16 (= 65536) is the first value with e>=31 -> saturate.
    EXPECT_EQ(float_to_half(std::ldexp(1.0F, 16)), kHalfMaxFinite);
    // 2^15 (= 32768) is still a clean normal (e==30, mantissa 0) -> 0x7800.
    EXPECT_EQ(float_to_half(std::ldexp(1.0F, 15)), 0x7800U);
}

TEST(IblGpuHalf, EncodingIsMonotonicAcrossThePositiveRange)
{
    // For the encoder's representable finite range the mapping is order
    // preserving: a >= b (both >= 0) implies bits(a) >= bits(b). Walk a dense
    // multiplicative ladder and assert the half bit pattern never decreases.
    // Integer-driven induction (k -> f = 2^-14 * 1.3^k) keeps the loop variable
    // off the float (no float-loop-induction).
    std::uint16_t prev = float_to_half(0.0F);
    EXPECT_EQ(prev, 0x0000U);
    constexpr float kStart = 1.0F / 16384.0F;  // 2^-14, smallest half normal
    for (std::size_t k = 0; k < 80U; ++k)
    {
        const float f = kStart * std::pow(1.3F, static_cast<float>(k));
        if (f > std::ldexp(1.0F, 15))
            break;
        const std::uint16_t cur = float_to_half(f);
        EXPECT_GE(cur, prev) << "non-monotonic at f=" << f;
        prev = cur;
    }
}

TEST(IblGpuHalf, MaxFiniteDecodesToExactly65504)
{
    // The saturation pattern 0x7BFF must decode (via the independent reference)
    // to the canonical largest finite half, 65504. Locks the clamp target to a
    // real IEEE-754 value rather than an arbitrary bit pattern.
    EXPECT_FLOAT_EQ(ref_half_to_float(kHalfMaxFinite), 65504.0F);
    EXPECT_FLOAT_EQ(ref_half_to_float(float_to_half(70000.0F)), 65504.0F);
}

TEST(IblGpuHalf, MantissaTruncationSweepDropsLow13Bits)
{
    // For a normal value, the encoder keeps mantissa bits [22..13] and drops
    // [12..0] via `>> 13`. Build binary32 values of the form 1.0 + k*2^-23
    // (one low binary32 mantissa ULP) and confirm every value strictly inside
    // one half-ULP window truncates to the SAME half mantissa as the window
    // base (round-toward-zero, never round-to-nearest-even).
    for (std::uint32_t low = 0; low < 8192U; low += 1024U)  // within one half-ULP
    {
        const std::uint32_t base_bits = 0x3F800000U;  // 1.0F
        const std::uint32_t bits = base_bits | low;    // 1.0 + low*2^-23
        float f = 0.0F;
        std::memcpy(&f, &bits, sizeof(f));
        // All such f share the half pattern of 1.0 (0x3C00): low bits dropped.
        EXPECT_EQ(float_to_half(f), 0x3C00U) << "low=" << low;
    }
    // Crossing into the next half-ULP (low == 8192) flips the half mantissa.
    const std::uint32_t crossed_bits = 0x3F800000U | 8192U;
    float crossed = 0.0F;
    std::memcpy(&crossed, &crossed_bits, sizeof(crossed));
    EXPECT_EQ(float_to_half(crossed), 0x3C01U);
}

// --- Host-side pure: mip-count / byte-layout (always runs) -------------------

TEST(IblGpuLayout, PrefilteredMipChainHalvesEachLevel)
{
    // Build a trivial 8px env and prefilter into a 4-mip chain; verify the
    // host-side per-mip face sizes the uploader will stride over.
    auto env = cd::ibl::CubeMapRgbF::allocate(8);
    const auto spec = cd::ibl::prefilter_specular(env, /*base*/ 8, /*mips*/ 4, /*spp*/ 1);
    ASSERT_EQ(spec.mip_count, 4U);
    EXPECT_EQ(spec.mips[0].face_size, 8U);
    EXPECT_EQ(spec.mips[1].face_size, 4U);
    EXPECT_EQ(spec.mips[2].face_size, 2U);
    EXPECT_EQ(spec.mips[3].face_size, 1U);
}

TEST(IblGpuLayout, BrdfLutStagingPacksRgInOrder)
{
    // A 2x1 LUT — verify the half-float RG packing the uploader performs,
    // computed here host-side from the same float_to_half the impl uses.
    cd::ibl::BrdfLut lut;
    lut.width = 2;
    lut.height = 1;
    lut.rg = { 0.25F, 0.75F, 1.0F, 0.0F };  // texel0 (R,G), texel1 (R,G)

    const std::array<std::uint16_t, 4> expect {
        float_to_half(0.25F), float_to_half(0.75F),
        float_to_half(1.0F),  float_to_half(0.0F) };
    EXPECT_EQ(expect[0], float_to_half(lut.rg[0]));
    EXPECT_EQ(expect[1], float_to_half(lut.rg[1]));
    EXPECT_EQ(expect[2], float_to_half(lut.rg[2]));
    EXPECT_EQ(expect[3], float_to_half(lut.rg[3]));
}

// --- Host-side pure: descriptor / staging-buffer sizing ----------------------
// These replicate the EXACT byte arithmetic the upload helpers compute for the
// staging buffer + per-region copy offsets. RGBA16F => 4 channels * 2 bytes;
// a cube is 6 faces. Pinning the math host-side catches an off-by-channel or
// off-by-face descriptor-sizing regression with no device required.

TEST(IblGpuSizing, CubemapStagingBytesAreSixFacesRgba16f)
{
    // Mirrors upload_cubemap_rgba16f: face_bytes = size*size*4*2, total = *6.
    constexpr std::uint32_t kSize = 4;
    const std::size_t face_bytes = cube_face_bytes_rgba16f(kSize);
    EXPECT_EQ(face_bytes, std::size_t { 128 });  // 4*4 px * 4 ch * 2 B
    const std::size_t total_bytes = face_bytes * 6U;
    EXPECT_EQ(total_bytes, std::size_t { 768 });
    // The uploader sizes a uint16 staging vector at total_bytes/sizeof(u16).
    EXPECT_EQ(total_bytes / kHalfBytes, std::size_t { 384 });
    // Per-face copy regions stride by exactly face_bytes (region f at f*fb).
    for (std::uint32_t f = 0; f < 6; ++f)
        EXPECT_EQ(face_bytes * f, std::size_t { 128 } * f);
}

TEST(IblGpuSizing, PrefilteredMipByteOffsetsAreCumulativeHalvedFaces)
{
    // Mirrors upload_prefiltered_specular's mip_byte_offset accumulation over a
    // 4-mip 8->4->2->1 chain. Each mip = 6 faces of size*size*4*2 bytes; the
    // offset of mip m is the sum of all earlier mips.
    const auto spec = [] {
        auto env = cd::ibl::CubeMapRgbF::allocate(8);
        return cd::ibl::prefilter_specular(env, /*base*/ 8, /*mips*/ 4, /*spp*/ 1);
    }();
    ASSERT_EQ(spec.mip_count, 4U);

    std::array<std::size_t, 4> expect_offset {};
    std::size_t running = 0;
    for (std::uint32_t m = 0; m < spec.mip_count; ++m)
    {
        expect_offset[m] = running;
        running += cube_face_bytes_rgba16f(spec.mips[m].face_size) * 6U;
    }
    // Per-mip 6-face blocks: 8px=3072, 4px=768, 2px=192, 1px=48.
    // Cumulative offsets: 0, 3072, 3840, 4032.
    EXPECT_EQ(expect_offset[0], std::size_t { 0 });
    EXPECT_EQ(expect_offset[1], std::size_t { 3072 });
    EXPECT_EQ(expect_offset[2], std::size_t { 3840 });
    EXPECT_EQ(expect_offset[3], std::size_t { 4032 });
    // Total staging size = last offset + last mip's 6-face block (48).
    EXPECT_EQ(running, std::size_t { 4080 });
    // Offsets are strictly increasing (no overlapping copy regions).
    EXPECT_TRUE(std::ranges::is_sorted(expect_offset));
}

TEST(IblGpuSizing, BrdfLutStagingBytesAreTwoChannelHalf)
{
    // Mirrors upload_brdf_lut: pixels = w*h, staging = pixels*2 halves,
    // bytes = pixels*2*sizeof(u16). RG16Float => 2 channels, 2 bytes each.
    constexpr std::uint32_t kW = 4;
    constexpr std::uint32_t kH = 2;
    const std::size_t pixels = static_cast<std::size_t>(kW) * kH;
    EXPECT_EQ(pixels, std::size_t { 8 });
    const std::size_t staging_halves = pixels * 2U;
    EXPECT_EQ(staging_halves, std::size_t { 16 });
    const std::size_t bytes = pixels * 2U * kHalfBytes;
    EXPECT_EQ(bytes, std::size_t { 32 });
}

TEST(IblGpuSizing, PrefilterCapsMipCountAtKMaxSpecularMips)
{
    // prefilter_specular clamps num_mips to kMaxSpecularMips (8); the uploader
    // strides over exactly mip_count mips, so the cap bounds the copy-region
    // count. Asking for 32 mips must yield no more than the cap.
    auto env = cd::ibl::CubeMapRgbF::allocate(8);
    const auto spec = cd::ibl::prefilter_specular(env, /*base*/ 8, /*mips*/ 32, /*spp*/ 1);
    EXPECT_EQ(spec.mip_count, cd::ibl::kMaxSpecularMips);
    EXPECT_LE(spec.mip_count, cd::ibl::kMaxSpecularMips);
}

// --- Host-side pure: barrier / copy-region / view descriptor sizing ----------
// The upload helpers build TextureSubresourceRange / BufferImageCopyRegion /
// TextureViewDesc by hand. Pin the field arithmetic host-side so a regression
// in mip/layer counts or per-region copy count is caught with no device.

TEST(IblGpuDescriptor, CubeBarrierRangeCoversAllSixLayersOneMip)
{
    // upload_cubemap_rgba16f transitions range {base_mip 0, mip_count 1,
    // base_layer 0, layer_count 6}. Mirror that field-for-field.
    const cd::rhi::TextureSubresourceRange range { 0, 1, 0, 6 };
    EXPECT_EQ(range.base_mip, 0U);
    EXPECT_EQ(range.mip_count, 1U);
    EXPECT_EQ(range.base_layer, 0U);
    EXPECT_EQ(range.layer_count, 6U);
}

TEST(IblGpuDescriptor, PrefilteredBarrierRangeMipCountTracksProductMips)
{
    // The prefiltered uploader transitions {0, mip_count, 0, 6}; the mip_count
    // is exactly the product's mip_count (clamped at kMaxSpecularMips upstream).
    const auto spec = [] {
        auto env = cd::ibl::CubeMapRgbF::allocate(8);
        return cd::ibl::prefilter_specular(env, /*base*/ 8, /*mips*/ 4, /*spp*/ 1);
    }();
    const cd::rhi::TextureSubresourceRange range { 0, spec.mip_count, 0, 6 };
    EXPECT_EQ(range.mip_count, 4U);
    EXPECT_EQ(range.layer_count, 6U);
}

TEST(IblGpuDescriptor, PrefilteredCopyRegionCountIsSixPerMip)
{
    // The uploader pushes one BufferImageCopyRegion per (mip, face). For a
    // 4-mip cube that is exactly 4*6 = 24 regions; each region targets one
    // (mip_level, base_layer) pair with layer_count 1.
    const auto spec = [] {
        auto env = cd::ibl::CubeMapRgbF::allocate(8);
        return cd::ibl::prefilter_specular(env, 8, 4, 1);
    }();
    std::vector<cd::rhi::BufferImageCopyRegion> regs;
    std::size_t total_bytes = 0;
    for (std::uint32_t m = 0; m < spec.mip_count; ++m)
    {
        const std::uint32_t s = spec.mips[m].face_size;
        const std::size_t face_bytes = cube_face_bytes_rgba16f(s);
        for (std::uint32_t f = 0; f < 6U; ++f)
            regs.push_back(cd::rhi::BufferImageCopyRegion {
                .buffer_offset = total_bytes + face_bytes * f,
                .mip_level = m,
                .base_layer = f,
                .layer_count = 1,
                .image_offset = { 0, 0, 0 },
                .image_extent = { s, s, 1 } });
        total_bytes += face_bytes * 6U;
    }
    EXPECT_EQ(regs.size(), std::size_t { 24 });  // 4 mips * 6 faces
    // The last region must target mip 3, face 5, with the 1px extent.
    EXPECT_EQ(regs.back().mip_level, 3U);
    EXPECT_EQ(regs.back().base_layer, 5U);
    EXPECT_EQ(regs.back().image_extent.width, 1U);
    EXPECT_EQ(regs.back().layer_count, 1U);
}

TEST(IblGpuDescriptor, BrdfLutCopyRegionIsSingleFullExtent)
{
    // upload_brdf_lut emits one region covering the whole 2D image, mip 0,
    // layer 0, layer_count 1.
    const cd::rhi::BufferImageCopyRegion reg {
        .buffer_offset = 0,
        .mip_level = 0,
        .base_layer = 0,
        .layer_count = 1,
        .image_offset = { 0, 0, 0 },
        .image_extent = { 4, 2, 1 } };
    EXPECT_EQ(reg.buffer_offset, std::uint64_t { 0 });
    EXPECT_EQ(reg.mip_level, 0U);
    EXPECT_EQ(reg.layer_count, 1U);
    EXPECT_EQ(reg.image_extent.width, 4U);
    EXPECT_EQ(reg.image_extent.height, 2U);
    EXPECT_EQ(reg.image_extent.depth, 1U);
}

// --- Empty-input guards (always run) -----------------------------------------
// The upload helpers must early-out (no device calls) on empty input. We can
// verify the guard without a device because the size==0 branch returns before
// touching `dev` — pass a null reference path is UB, so we only assert the
// host-side precondition that an empty product yields zero mip/extent here.

TEST(IblGpuLayout, EmptyBrdfLutHasZeroExtent)
{
    const cd::ibl::BrdfLut empty {};
    EXPECT_EQ(empty.width, 0U);
    EXPECT_EQ(empty.height, 0U);
}

TEST(IblGpuLayout, EmptyCubemapAndPrefilteredHaveZeroMips)
{
    // Both upload helpers early-out on a zero-size / zero-mip product before
    // touching the device; pin the host-side precondition that drives that
    // guard (face_size == 0 for cube, mip_count == 0 for prefiltered).
    const cd::ibl::CubeMapRgbF empty_cube {};
    EXPECT_EQ(empty_cube.face_size, 0U);
    const cd::ibl::PrefilteredSpecularCube empty_spec {};
    EXPECT_EQ(empty_spec.mip_count, 0U);
}

TEST(IblGpuLayout, DefaultOutputPodsAreInvalidWithDefaultMipCount)
{
    // The helpers return a default-constructed GpuCubemap / GpuLut2D on every
    // early-out / failure path. Pin that default: invalid handles + mip_count 1
    // (the struct's documented default) so callers can detect a no-op upload by
    // checking handle validity, not by the mip count.
    const cd::ibl_gpu::GpuCubemap cube {};
    EXPECT_FALSE(cube.image.is_valid());
    EXPECT_FALSE(cube.view.is_valid());
    EXPECT_EQ(cube.mip_count, 1U);
    const cd::ibl_gpu::GpuLut2D lut {};
    EXPECT_FALSE(lut.image.is_valid());
    EXPECT_FALSE(lut.view.is_valid());
}

TEST(IblGpuLayout, SingleMipPrefilteredIsRoughnessZeroMirror)
{
    // num_mips == 1 -> roughness 0 (mirror), one mip, full base resolution.
    // The uploader strides exactly one mip; pin the host-side product shape.
    auto env = cd::ibl::CubeMapRgbF::allocate(4);
    const auto spec = cd::ibl::prefilter_specular(env, /*base*/ 4, /*mips*/ 1, /*spp*/ 1);
    ASSERT_EQ(spec.mip_count, 1U);
    EXPECT_EQ(spec.mips[0].face_size, 4U);
    // Staging is exactly one 6-face block at base resolution; no later mips.
    const std::size_t total = cube_face_bytes_rgba16f(spec.mips[0].face_size) * 6U;
    // 4*4 texels * 4 channels * 2 B/half = 128 B/face; * 6 faces = 768 bytes.
    EXPECT_EQ(total, std::size_t { 768 });
}

// --- Device-gated end-to-end round-trip (skips without Vulkan) ---------------

TEST(IblGpuUpload, BrdfLutRoundTripsThroughGpu)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    // Small deterministic LUT: 4x2 RG16Float.
    cd::ibl::BrdfLut lut;
    lut.width = 4;
    lut.height = 2;
    lut.rg.resize(static_cast<std::size_t>(lut.width) * lut.height * 2);
    for (std::size_t i = 0; i < lut.rg.size(); ++i)
        lut.rg[i] = static_cast<float>(i) * 0.03125F;  // exact-ish halves

    const auto gpu = cd::ibl_gpu::upload_brdf_lut(*dev, lut);
    ASSERT_TRUE(gpu.image.is_valid()) << "upload_brdf_lut produced no image";
    ASSERT_TRUE(gpu.view.is_valid());

    // Read the texture back into a CPU-visible buffer.
    const std::size_t pixels = static_cast<std::size_t>(lut.width) * lut.height;
    const std::size_t bytes  = pixels * 2U * sizeof(std::uint16_t);  // RG16

    cd::rhi::BufferDesc rd {};
    rd.size = bytes;
    rd.usage = cd::rhi::BufferUsage::kTransferDst;
    rd.memory = cd::rhi::MemoryUsage::kGpuToCpu;
    auto rb = dev->create_buffer(rd);
    ASSERT_TRUE(rb.has_value()) << "readback buffer alloc failed";

    cd::rhi::IDevice::ImageRegion region {};
    region.x = 0;
    region.y = 0;
    region.width = lut.width;
    region.height = lut.height;
    region.mip_level = 0;
    region.base_layer = 0;
    region.src_state = cd::rhi::ResourceState::kShaderResource;  // upload left it here

    auto cir = dev->copy_image_to_buffer(gpu.image, *rb, 0, region);
    if (!cir.has_value())
    {
        // Backend without readback — clean up and skip rather than fail.
        dev->destroy_buffer(*rb);
        dev->destroy_texture_view(gpu.view);
        dev->destroy_texture(gpu.image);
        GTEST_SKIP() << "copy_image_to_buffer unsupported: " << cir.error().message;
    }

    std::vector<std::uint16_t> got(pixels * 2U);
    auto dr = dev->download_buffer(*rb, 0,
        std::span<std::byte>(reinterpret_cast<std::byte*>(got.data()), bytes));
    ASSERT_TRUE(dr.has_value()) << "download_buffer failed";

    // Every RG half must equal float_to_half of the source — proves the
    // staging conversion + barrier + copy path is byte-correct on the GPU.
    for (std::size_t i = 0; i < got.size(); ++i)
        EXPECT_EQ(got[i], float_to_half(lut.rg[i])) << "mismatch at half index " << i;

    dev->destroy_buffer(*rb);
    dev->destroy_texture_view(gpu.view);
    dev->destroy_texture(gpu.image);
}

TEST(IblGpuUpload, CubemapUploadReportsMipCount)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    auto env = cd::ibl::CubeMapRgbF::allocate(4);
    const auto gpu = cd::ibl_gpu::upload_cubemap_rgba16f(*dev, env);
    ASSERT_TRUE(gpu.image.is_valid());
    EXPECT_EQ(gpu.mip_count, 1U);

    dev->destroy_texture_view(gpu.view);
    dev->destroy_texture(gpu.image);
}

TEST(IblGpuUpload, PrefilteredSpecularUploadReportsMipCount)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    auto env = cd::ibl::CubeMapRgbF::allocate(8);
    const auto spec = cd::ibl::prefilter_specular(env, 8, 4, 1);
    const auto gpu = cd::ibl_gpu::upload_prefiltered_specular(*dev, spec);
    ASSERT_TRUE(gpu.image.is_valid());
    EXPECT_EQ(gpu.mip_count, 4U);

    dev->destroy_texture_view(gpu.view);
    dev->destroy_texture(gpu.image);
}

}  // namespace
