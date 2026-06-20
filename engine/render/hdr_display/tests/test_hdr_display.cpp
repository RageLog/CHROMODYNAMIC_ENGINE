#include <cd/hdr_display/HdrDisplay.hpp>

#include <gtest/gtest.h>

#include <cmath>

namespace
{

using cd::hdr_display::linear_srgb_to_rec2020;
using cd::hdr_display::pq_decode;
using cd::hdr_display::pq_encode;
using cd::hdr_display::rec2020_to_xyz;
using cd::hdr_display::scrgb_pack;
using cd::hdr_display::scrgb_unpack;
using cd::hdr_display::xyz_to_rec2020;

constexpr float kEps = 0.01F;

// ---------------------------------------------------------------------------
// Existing tests (unchanged)
// ---------------------------------------------------------------------------

TEST(HdrDisplay, PqRoundTripUnit)
{
    for (float nits : { 100.0F, 200.0F, 1000.0F, 4000.0F, 10000.0F })
    {
        const float c = pq_encode(nits);
        const float r = pq_decode(c);
        EXPECT_NEAR(r, nits, nits * 0.02F + 0.5F);
    }
}

TEST(HdrDisplay, PqMonotonicallyIncreases)
{
    float prev = -1.0F;
    for (int step = 0; step <= 100; ++step)
    {
        const float nits = static_cast<float>(step) * 100.0F;
        const float c = pq_encode(nits);
        EXPECT_GE(c, prev - kEps);
        prev = c;
    }
}

TEST(HdrDisplay, PqClampsAtMax)
{
    EXPECT_LE(pq_encode(20000.0F), 1.0F + kEps);
    EXPECT_GE(pq_encode(0.0F), 0.0F - kEps);
}

TEST(HdrDisplay, Rec2020MatrixPreservesWhite)
{
    const auto w = linear_srgb_to_rec2020({ 1, 1, 1 });
    EXPECT_NEAR(w.x, 1.0F, kEps);
    EXPECT_NEAR(w.y, 1.0F, kEps);
    EXPECT_NEAR(w.z, 1.0F, kEps);
}

TEST(HdrDisplay, ScrgbPackScalesByMaxNits)
{
    const auto p = scrgb_pack({ 1, 1, 1 }, /*display_max_nits=*/400.0F);
    EXPECT_NEAR(p.x, 400.0F / 80.0F, kEps);
}

TEST(HdrDisplay, GlslNonEmpty)
{
    EXPECT_FALSE(cd::hdr_display::kHdrGlsl.empty());
}

// ---------------------------------------------------------------------------
// NEW: PQ round-trip across full [0, 10000] nit range incl. boundary values
// ST-2084: encode must be strictly invertible over [0, 10000].
// ---------------------------------------------------------------------------

TEST(HdrDisplay, PqRoundTripFullRange)
{
    // Reference nit levels covering the full HDR10 gamut, including
    // 0 (black), 100 (SDR ref-white area), 1000 (bright HDR), 10000 (peak).
    // Tolerance: ±1% relative + 0.01 nit absolute (float precision).
    constexpr float kRelTol = 0.01F;
    constexpr float kAbsTol = 0.01F;
    for (float nits : { 0.0F, 0.001F, 1.0F, 100.0F, 1000.0F, 10000.0F })
    {
        const float code  = pq_encode(nits);
        const float recon = pq_decode(code);
        EXPECT_NEAR(recon, nits, nits * kRelTol + kAbsTol)
            << "Round-trip failed for " << nits << " nits";
    }
}

// ---------------------------------------------------------------------------
// NEW: PQ strict monotonicity (encode must be non-decreasing, no tolerance)
// Every step must yield a code value >= previous (monotone EOTF^-1).
// ---------------------------------------------------------------------------

TEST(HdrDisplay, PqStrictMonotonicity)
{
    float prev_code = pq_encode(0.0F);
    // Step through 1-nit increments to catch any non-monotone inflection.
    for (int step = 1; step <= 10000; ++step)
    {
        const auto nits = static_cast<float>(step);
        const float code = pq_encode(nits);
        EXPECT_GE(code, prev_code)
            << "PQ encode not monotone at " << nits << " nits";
        prev_code = code;
    }
}

// ---------------------------------------------------------------------------
// NEW: PQ known reference values from SMPTE ST 2084 / ITU-R BT.2100 Table 4.
// At 100 cd/m² the PQ code value is ≈ 0.5081 (BT.2100-2 Table 4).
// At 10000 cd/m² the code must equal 1.0 (peak white, spec-mandated).
// At 0 cd/m² the code must equal 0.0 (absolute black, spec-mandated).
// ---------------------------------------------------------------------------

TEST(HdrDisplay, PqReferenceValues)
{
    // BT.2100-2 (2018) Table 4 reference: 100 cd/m² ≈ 0.5081 (12-bit 2081/4095)
    // Tolerance ±0.002 to accommodate float32 vs spec's 12-bit integer table.
    EXPECT_NEAR(pq_encode(100.0F), 0.5081F, 0.002F)
        << "PQ code at 100 cd/m2 deviates from BT.2100 Table 4 reference";

    // ST-2084 §5.2: at Y=1 (10000 nit) the transfer function returns 1.
    EXPECT_NEAR(pq_encode(10000.0F), 1.0F, 1e-4F)
        << "PQ encode at 10000 nits must be 1.0 per ST-2084";

    // ST-2084 §5.2: at Y=0 the transfer function returns 0.
    EXPECT_NEAR(pq_encode(0.0F), 0.0F, 1e-6F)
        << "PQ encode at 0 nits must be 0.0 per ST-2084";
}

// ---------------------------------------------------------------------------
// NEW: PQ clamp behaviour for negative + overflow inputs.
// Negative nits must clamp to 0.0; values >10000 must clamp to 1.0.
// ---------------------------------------------------------------------------

TEST(HdrDisplay, PqClampNegativeInput)
{
    // Negative nits are physically meaningless; must clamp to black.
    EXPECT_NEAR(pq_encode(-1.0F),     pq_encode(0.0F), 1e-6F);
    EXPECT_NEAR(pq_encode(-10000.0F), pq_encode(0.0F), 1e-6F);
}

TEST(HdrDisplay, PqClampOverflowInput)
{
    // Values beyond 10000 nits must saturate at code 1.0.
    EXPECT_NEAR(pq_encode(10001.0F),  1.0F, 1e-4F);
    EXPECT_NEAR(pq_encode(100000.0F), 1.0F, 1e-4F);
}

TEST(HdrDisplay, PqDecodeClampNegativeCode)
{
    // Negative code values clamp to 0 nits (same as code 0).
    EXPECT_NEAR(pq_decode(-1.0F), pq_decode(0.0F), 1e-4F);
}

TEST(HdrDisplay, PqDecodeClampOverflowCode)
{
    // Code values >1.0 clamp to 10000 nits peak.
    EXPECT_NEAR(pq_decode(2.0F), pq_decode(1.0F), 0.1F);
}

// ---------------------------------------------------------------------------
// NEW: Rec2020 ↔ XYZ matrix round-trip (inverse correctness).
// Apply rec2020_to_xyz then xyz_to_rec2020: must recover input.
// ---------------------------------------------------------------------------

TEST(HdrDisplay, Rec2020XyzRoundTrip)
{
    constexpr float kRoundTripEps = 0.002F;
    const cd::math::Vec3f kInputs[] = {
        { 1.0F, 0.0F, 0.0F },  // Rec2020 red primary
        { 0.0F, 1.0F, 0.0F },  // Rec2020 green primary
        { 0.0F, 0.0F, 1.0F },  // Rec2020 blue primary
        { 1.0F, 1.0F, 1.0F },  // D65 white
        { 0.5F, 0.3F, 0.8F },  // arbitrary HDR colour
    };

    for (const auto& c : kInputs)
    {
        const auto xyz  = rec2020_to_xyz(c);
        const auto back = xyz_to_rec2020(xyz);
        EXPECT_NEAR(back.x, c.x, kRoundTripEps) << "R round-trip";
        EXPECT_NEAR(back.y, c.y, kRoundTripEps) << "G round-trip";
        EXPECT_NEAR(back.z, c.z, kRoundTripEps) << "B round-trip";
    }
}

// ---------------------------------------------------------------------------
// NEW: Rec2020→XYZ luminance channel correctness.
// BT.2020 luminance coefficients: Y = 0.2627 R + 0.6780 G + 0.0593 B.
// The Y component of rec2020_to_xyz must match these standard coefficients.
// ---------------------------------------------------------------------------

TEST(HdrDisplay, Rec2020ToXyzLuminance)
{
    // Pure primaries: Y channel must match BT.2020 luma coefficients.
    EXPECT_NEAR(rec2020_to_xyz({ 1.0F, 0.0F, 0.0F }).y, 0.2627F, 0.001F);
    EXPECT_NEAR(rec2020_to_xyz({ 0.0F, 1.0F, 0.0F }).y, 0.6780F, 0.001F);
    EXPECT_NEAR(rec2020_to_xyz({ 0.0F, 0.0F, 1.0F }).y, 0.0593F, 0.001F);
}

// ---------------------------------------------------------------------------
// NEW: scRGB pack/unpack negative and >1 values (wide-gamut and HDR colours).
// scRGB explicitly allows negative values (out-of-gamut) and values >1 (HDR).
// The pack/unpack cycle must preserve sign and magnitude faithfully.
// ---------------------------------------------------------------------------

TEST(HdrDisplay, ScrgbPackNegativeValues)
{
    // Out-of-gamut scRGB values: sign must be preserved.
    const auto p = scrgb_pack({ -0.5F, 0.0F, 1.5F }, 400.0F);
    EXPECT_LT(p.x, 0.0F) << "Negative channel must remain negative after pack";
    EXPECT_NEAR(p.x, -0.5F * (400.0F / 80.0F), kEps);
    EXPECT_NEAR(p.z,  1.5F * (400.0F / 80.0F), kEps);
}

TEST(HdrDisplay, ScrgbUnpackRoundTrip)
{
    // Pack then unpack: must recover original linear values for wide-gamut HDR.
    constexpr float kMaxNits   = 1000.0F;
    constexpr float kRtEps     = 1e-4F;

    const cd::math::Vec3f kInputs[] = {
        {  1.0F,  0.5F,  0.0F },
        { -0.2F,  0.0F,  1.8F },   // negative + super-white
        {  0.0F,  0.0F,  0.0F },   // black
        {  2.5F,  2.5F,  2.5F },   // above SDR
    };

    for (const auto& lin : kInputs)
    {
        const auto packed   = scrgb_pack(lin, kMaxNits);
        const auto unpacked = scrgb_unpack(packed, kMaxNits);
        EXPECT_NEAR(unpacked.x, lin.x, kRtEps);
        EXPECT_NEAR(unpacked.y, lin.y, kRtEps);
        EXPECT_NEAR(unpacked.z, lin.z, kRtEps);
    }
}

TEST(HdrDisplay, ScrgbPackSdrReference)
{
    // At display_max_nits = 80 (SDR), pack must be identity (1.0 stays 1.0).
    const auto p = scrgb_pack({ 1.0F, 1.0F, 1.0F }, 80.0F);
    EXPECT_NEAR(p.x, 1.0F, kEps);
    EXPECT_NEAR(p.y, 1.0F, kEps);
    EXPECT_NEAR(p.z, 1.0F, kEps);
}

}  // namespace
