#include <cd/hdr_display/HdrDisplay.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::hdr_display::linear_srgb_to_rec2020;
using cd::hdr_display::pq_decode;
using cd::hdr_display::pq_encode;
using cd::hdr_display::scrgb_pack;

constexpr float kEps = 0.01F;

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
    for (float nits = 0.0F; nits <= 10000.0F; nits += 100.0F)
    {
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

}  // namespace
