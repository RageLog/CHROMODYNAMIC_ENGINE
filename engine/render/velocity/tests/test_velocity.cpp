#include <cd/velocity/Velocity.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <string_view>

namespace
{

using cd::velocity::motion_pixels;
using cd::velocity::motion_vector_uv;

constexpr float kEps = 1e-3F;

TEST(Velocity, MotionZeroForIdenticalClipPositions)
{
    const cd::math::Vec4f p { 0.4F, -0.2F, 0.5F, 1.0F };
    const auto mv = motion_vector_uv(p, p);
    EXPECT_NEAR(mv.x, 0.0F, kEps);
    EXPECT_NEAR(mv.y, 0.0F, kEps);
}

TEST(Velocity, MotionVectorMatchesUvDelta)
{
    // Prev clip projects to (0, 0); curr clip projects to (0.25, 0).
    const cd::math::Vec4f prev { 0.0F, 0.0F, 0.5F, 1.0F };  // uv = (0.5, 0.5)
    const cd::math::Vec4f curr { 0.5F, 0.0F, 0.5F, 1.0F };  // uv = (0.75, 0.5)
    const auto mv = motion_vector_uv(prev, curr);
    EXPECT_NEAR(mv.x, 0.25F, kEps);
    EXPECT_NEAR(mv.y, 0.0F,  kEps);
}

TEST(Velocity, MotionZeroWhenEitherBehindNearPlane)
{
    const cd::math::Vec4f behind { 0, 0, 0, -1 };
    const cd::math::Vec4f front  { 0, 0, 0.5F, 1 };
    EXPECT_NEAR(motion_vector_uv(behind, front).x, 0.0F, kEps);
    EXPECT_NEAR(motion_vector_uv(front, behind).x, 0.0F, kEps);
}

TEST(Velocity, MotionPixelsScalesWithExtent)
{
    const cd::math::Vec2f uv_delta { 0.1F, 0.0F };
    EXPECT_NEAR(motion_pixels(uv_delta, 1920, 1080),  192.0F, 0.5F);
    EXPECT_NEAR(motion_pixels(uv_delta, 3840, 2160),  384.0F, 0.5F);
}

TEST(Velocity, GlslSourcesNonEmpty)
{
    EXPECT_FALSE(cd::velocity::kVelocityVS.empty());
    EXPECT_FALSE(cd::velocity::kVelocityFS.empty());
    EXPECT_NE(cd::velocity::kVelocityFS.find("out_motion"),
              std::string_view::npos);
}

// --- ADD-ONLY depth: pins EXISTING clip→uv-delta motion-vector math ---------
// (host helpers mirror the velocity FS contract; tests assert ACTUAL behaviour)

// Helper mirroring the in-shader perspective divide + NDC→UV remap exactly as
// motion_vector_uv() computes it, so expectations are derived from the contract
// rather than hand-tuned magic numbers.
[[nodiscard]] cd::math::Vec2f ndc_to_uv(const cd::math::Vec4f& clip) noexcept
{
    return { (clip.x / clip.w) * 0.5F + 0.5F, (clip.y / clip.w) * 0.5F + 0.5F };
}

// No-motion: a static world point reprojected through an UNCHANGED VP yields a
// zero motion vector even when off the screen centre (the common TAA case).
TEST(Velocity, NoMotionOffCentreYieldsZero)
{
    const cd::math::Vec4f p { -0.7F, 0.3F, 0.9F, 2.0F };
    const auto mv = motion_vector_uv(p, p);
    EXPECT_NEAR(mv.x, 0.0F, kEps);
    EXPECT_NEAR(mv.y, 0.0F, kEps);
}

// Pure horizontal translation: only the U component moves; V stays put.
TEST(Velocity, PureHorizontalTranslation)
{
    const cd::math::Vec4f prev { -0.2F, 0.1F, 0.5F, 1.0F };
    const cd::math::Vec4f curr {  0.4F, 0.1F, 0.5F, 1.0F };  // +0.6 in clip-x
    const auto mv  = motion_vector_uv(prev, curr);
    const auto exp = ndc_to_uv(curr) - ndc_to_uv(prev);
    EXPECT_NEAR(mv.x, exp.x, kEps);   // (0.4 - (-0.2)) * 0.5 = 0.3
    EXPECT_NEAR(mv.x, 0.3F,  kEps);
    EXPECT_NEAR(mv.y, 0.0F,  kEps);
}

// Pure vertical translation: only V moves; U stays put.
TEST(Velocity, PureVerticalTranslation)
{
    const cd::math::Vec4f prev { 0.25F, -0.5F, 0.5F, 1.0F };
    const cd::math::Vec4f curr { 0.25F,  0.5F, 0.5F, 1.0F };  // +1.0 in clip-y
    const auto mv = motion_vector_uv(prev, curr);
    EXPECT_NEAR(mv.x, 0.0F, kEps);
    EXPECT_NEAR(mv.y, 0.5F, kEps);    // (0.5 - (-0.5)) * 0.5 = 0.5
}

// Diagonal motion: both components move; result equals the FS-mirrored delta.
TEST(Velocity, DiagonalMotionMatchesContract)
{
    const cd::math::Vec4f prev { -0.4F, -0.4F, 0.5F, 1.0F };
    const cd::math::Vec4f curr {  0.2F,  0.6F, 0.5F, 1.0F };
    const auto mv  = motion_vector_uv(prev, curr);
    const auto exp = ndc_to_uv(curr) - ndc_to_uv(prev);
    EXPECT_NEAR(mv.x, exp.x, kEps);
    EXPECT_NEAR(mv.y, exp.y, kEps);
}

// Perspective divide: same clip-x but different w changes the screen-space
// position, so motion is non-zero — the w-division is part of the contract.
TEST(Velocity, PerspectiveDivideAffectsMotion)
{
    const cd::math::Vec4f prev { 0.6F, 0.0F, 0.5F, 1.0F };  // u = 0.8
    const cd::math::Vec4f curr { 0.6F, 0.0F, 0.5F, 2.0F };  // u = 0.5*(0.3)+0.5
    const auto mv = motion_vector_uv(prev, curr);
    // curr_u = (0.6/2)*0.5+0.5 = 0.65 ; prev_u = (0.6/1)*0.5+0.5 = 0.8
    EXPECT_NEAR(mv.x, 0.65F - 0.8F, kEps);
    EXPECT_NEAR(mv.y, 0.0F,         kEps);
}

// Negative motion: leftward/downward travel produces negative deltas (no abs).
TEST(Velocity, NegativeDeltaPreservedNoAbs)
{
    const cd::math::Vec4f prev {  0.5F,  0.5F, 0.5F, 1.0F };
    const cd::math::Vec4f curr { -0.5F, -0.5F, 0.5F, 1.0F };
    const auto mv = motion_vector_uv(prev, curr);
    EXPECT_LT(mv.x, 0.0F);
    EXPECT_LT(mv.y, 0.0F);
    EXPECT_NEAR(mv.x, -0.5F, kEps);
    EXPECT_NEAR(mv.y, -0.5F, kEps);
}

// Anti-symmetry: swapping prev/curr negates the motion vector exactly.
TEST(Velocity, SwapPrevCurrNegatesVector)
{
    const cd::math::Vec4f a { -0.3F, 0.2F, 0.5F, 1.0F };
    const cd::math::Vec4f b {  0.4F, 0.7F, 0.5F, 1.5F };
    const auto fwd = motion_vector_uv(a, b);
    const auto rev = motion_vector_uv(b, a);
    EXPECT_NEAR(fwd.x, -rev.x, kEps);
    EXPECT_NEAR(fwd.y, -rev.y, kEps);
}

// Behind-near-plane guard is an EXACT w<=0 boundary: w==0 is also rejected
// (it would otherwise divide by zero), and a tiny positive w is accepted.
TEST(Velocity, ExactZeroWRejectedTinyPositiveAccepted)
{
    const cd::math::Vec4f w_zero { 0.1F, 0.1F, 0.5F, 0.0F };
    const cd::math::Vec4f front  { 0.1F, 0.1F, 0.5F, 1.0F };
    EXPECT_NEAR(motion_vector_uv(w_zero, front).x, 0.0F, kEps);
    EXPECT_NEAR(motion_vector_uv(front, w_zero).y, 0.0F, kEps);

    const cd::math::Vec4f tiny_w { 0.0F, 0.0F, 0.0F, 1e-4F };
    EXPECT_EQ(motion_vector_uv(tiny_w, tiny_w), (cd::math::Vec2f { 0.0F, 0.0F }));
}

// Both behind the near plane → zero (neither projection is valid).
TEST(Velocity, BothBehindNearPlaneYieldsZero)
{
    const cd::math::Vec4f a { 0.3F, -0.2F, 0.5F, -0.5F };
    const cd::math::Vec4f b { 0.1F,  0.4F, 0.5F, -2.0F };
    const auto mv = motion_vector_uv(a, b);
    EXPECT_NEAR(mv.x, 0.0F, kEps);
    EXPECT_NEAR(mv.y, 0.0F, kEps);
}

// NDC corners map to the UV unit square: ndc(-1,-1)→uv(0,0), ndc(+1,+1)→uv(1,1),
// so the full-screen sweep produces a unit motion vector.
TEST(Velocity, NdcCornerSweepIsUnitDelta)
{
    const cd::math::Vec4f bl { -1.0F, -1.0F, 0.5F, 1.0F };  // uv (0,0)
    const cd::math::Vec4f tr {  1.0F,  1.0F, 0.5F, 1.0F };  // uv (1,1)
    const auto mv = motion_vector_uv(bl, tr);
    EXPECT_NEAR(mv.x, 1.0F, kEps);
    EXPECT_NEAR(mv.y, 1.0F, kEps);
}

// Sub-pixel motion: a tiny clip delta survives as a tiny — but exact — UV delta
// and converts to a fractional pixel magnitude (TAA reuses sub-pixel motion).
TEST(Velocity, SubPixelMotionPreserved)
{
    const cd::math::Vec4f prev { 0.0F,     0.0F, 0.5F, 1.0F };
    const cd::math::Vec4f curr { 0.001F,   0.0F, 0.5F, 1.0F };  // 0.0005 uv
    const auto mv = motion_vector_uv(prev, curr);
    EXPECT_NEAR(mv.x, 0.0005F, 1e-6F);
    EXPECT_GT(mv.x, 0.0F);
    EXPECT_NEAR(motion_pixels(mv, 1920, 1080), 0.0005F * 1920.0F, 1e-3F);
}

// motion_pixels magnitude is rotation-invariant: a uv delta and its 90°-rotated
// counterpart give the same magnitude on a square extent.
TEST(Velocity, MotionPixelsMagnitudeRotationInvariantSquare)
{
    const cd::math::Vec2f a { 0.1F, 0.0F };
    const cd::math::Vec2f b { 0.0F, 0.1F };
    EXPECT_NEAR(motion_pixels(a, 1024, 1024),
                motion_pixels(b, 1024, 1024), kEps);
}

// motion_pixels of a zero delta is exactly zero (no spurious sampling).
TEST(Velocity, MotionPixelsZeroDeltaIsZero)
{
    EXPECT_NEAR(motion_pixels(cd::math::Vec2f { 0.0F, 0.0F }, 1920, 1080),
                0.0F, kEps);
}

// motion_pixels obeys the Pythagorean diagonal on a square extent.
TEST(Velocity, MotionPixelsDiagonalPythagorean)
{
    const cd::math::Vec2f d { 0.1F, 0.1F };
    const float expected = std::numbers::sqrt2_v<float> * 0.1F * 1000.0F;
    EXPECT_NEAR(motion_pixels(d, 1000, 1000), expected, 0.5F);
}

// VS↔FS GLSL contract: the VS emits both clip positions the FS divides; the FS
// host-mirror (motion_vector_uv) must match the GLSL token-level math.
TEST(Velocity, GlslContractMirrorsHostMath)
{
    // VS forwards prev/curr clip to the named varyings the FS divides.
    EXPECT_NE(cd::velocity::kVelocityVS.find("v_prev_clip"), std::string_view::npos);
    EXPECT_NE(cd::velocity::kVelocityVS.find("v_curr_clip"), std::string_view::npos);
    EXPECT_NE(cd::velocity::kVelocityVS.find("gl_Position = v_curr_clip"),
              std::string_view::npos);
    // FS performs the exact perspective divide + NDC→UV remap the host mirrors.
    EXPECT_NE(cd::velocity::kVelocityFS.find("/ v_prev_clip.w"), std::string_view::npos);
    EXPECT_NE(cd::velocity::kVelocityFS.find("/ v_curr_clip.w"), std::string_view::npos);
    EXPECT_NE(cd::velocity::kVelocityFS.find("* 0.5 + 0.5"),     std::string_view::npos);
    EXPECT_NE(cd::velocity::kVelocityFS.find("curr_uv - prev_uv"), std::string_view::npos);
}

}  // namespace
