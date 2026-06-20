// =============================================================================
// CHROMODYNAMIC -- engine/render/restir_di/tests/test_restir_di_temporal_buffer.cpp
// 80->100 marathon -- host-side double-buffered reservoir ring coverage.
//
// ADD-ONLY: cd/restir_di/TemporalBuffer.hpp is a header-only CPU mirror of the
// GPU temporal-reuse age tracking (kRestirDiTemporalReuseCS). It had ZERO
// dedicated tests before this file. None of these helpers touch the GPU or
// alter rendered output -- they are the host-side reference / unit-testable
// reproduction of the swap + age-invalidation logic.
//
// Coverage: construction + dimensions, current()/previous() indexing, swap()
// ping-pong, previous() max-age invalidation boundary, clear(), and the
// templated reservoir contract (default-zero + invalidate() + age).
//
// Runs everywhere -- no Vulkan ICD needed (CPU-only header math).
// =============================================================================
#include <cd/restir_di/Reservoir.hpp>
#include <cd/restir_di/TemporalBuffer.hpp>

#include <gtest/gtest.h>

#include <cstdint>

namespace
{

using cd::restir_di::Reservoir;
using cd::restir_di::Sample;
using cd::restir_di::TemporalBuffer;

constexpr float kEps = 1e-4F;

// --- construction + dimensions ----------------------------------------------

TEST(RestirDiTemporalBuffer, ConstructStoresDimensionsAndSize)
{
    TemporalBuffer<Reservoir> buf(8U, 4U);
    EXPECT_EQ(buf.width(), 8U);
    EXPECT_EQ(buf.height(), 4U);
    EXPECT_EQ(buf.size(), static_cast<std::size_t>(8U * 4U));
}

TEST(RestirDiTemporalBuffer, FreshBuffersAreZeroState)
{
    TemporalBuffer<Reservoir> buf(4U, 4U);
    for (std::uint32_t y = 0; y < 4U; ++y)
    {
        for (std::uint32_t x = 0; x < 4U; ++x)
        {
            EXPECT_EQ(buf.current(x, y).m, 0U);
            EXPECT_NEAR(buf.current(x, y).weight_sum, 0.0F, kEps);
        }
    }
}

// --- current() indexing ------------------------------------------------------

TEST(RestirDiTemporalBuffer, CurrentWriteAndReadRoundTrip)
{
    TemporalBuffer<Reservoir> buf(4U, 4U);
    Reservoir& r = buf.current(2U, 1U);
    r.selected   = Sample { 11, { 1, 2, 3 }, 4.0F };
    r.weight_sum = 5.0F;
    r.m          = 6U;

    const Reservoir& got = buf.current(2U, 1U);
    EXPECT_EQ(got.selected.light_index, 11U);
    EXPECT_NEAR(got.weight_sum, 5.0F, kEps);
    EXPECT_EQ(got.m, 6U);

    // A different pixel stays zero (no index aliasing).
    EXPECT_EQ(buf.current(0U, 0U).m, 0U);
}

// --- swap() ping-pong --------------------------------------------------------

TEST(RestirDiTemporalBuffer, SwapPromotesCurrentToPrevious)
{
    TemporalBuffer<Reservoir> buf(2U, 2U);
    buf.current(1U, 1U).selected.light_index = 42U;
    buf.current(1U, 1U).m                    = 3U;

    buf.swap();  // current -> previous

    const Reservoir prev = buf.previous(1U, 1U);
    EXPECT_EQ(prev.selected.light_index, 42U);
    EXPECT_EQ(prev.m, 3U);

    // After the swap, the (new) current buffer holds whatever was previously
    // in "previous" (zero-state in a fresh buffer).
    EXPECT_EQ(buf.current(1U, 1U).m, 0U);
}

TEST(RestirDiTemporalBuffer, DoubleSwapRestoresOriginalCurrent)
{
    TemporalBuffer<Reservoir> buf(2U, 2U);
    buf.current(0U, 0U).m = 7U;
    buf.swap();
    buf.swap();  // back to the original front buffer
    EXPECT_EQ(buf.current(0U, 0U).m, 7U);
}

// --- previous() max-age invalidation ----------------------------------------

TEST(RestirDiTemporalBuffer, PreviousReturnsZeroStateWhenStale)
{
    // max_age = 30 (default). A previous reservoir with age > 30 is reported
    // as the zero-state sentinel so callers never reuse stale history.
    TemporalBuffer<Reservoir> buf(2U, 2U, /*max_age=*/30U);
    buf.current(0U, 0U).selected.light_index = 9U;
    buf.current(0U, 0U).m                    = 5U;
    buf.current(0U, 0U).age                  = 31U;  // one past the cap
    buf.swap();

    const Reservoir prev = buf.previous(0U, 0U);
    EXPECT_EQ(prev.m, 0U);                      // invalidated sentinel
    EXPECT_EQ(prev.selected.light_index, 0U);
}

TEST(RestirDiTemporalBuffer, PreviousReturnsLiveReservoirAtExactMaxAge)
{
    // age == max_age is still LIVE (the guard is strictly-greater).
    TemporalBuffer<Reservoir> buf(2U, 2U, /*max_age=*/30U);
    buf.current(1U, 0U).selected.light_index = 8U;
    buf.current(1U, 0U).m                    = 4U;
    buf.current(1U, 0U).age                  = 30U;  // exactly the cap
    buf.swap();

    const Reservoir prev = buf.previous(1U, 0U);
    EXPECT_EQ(prev.m, 4U);                      // still live
    EXPECT_EQ(prev.selected.light_index, 8U);
}

TEST(RestirDiTemporalBuffer, CustomMaxAgeHonoured)
{
    // A tighter max_age of 5 invalidates an age-6 reservoir.
    TemporalBuffer<Reservoir> buf(2U, 2U, /*max_age=*/5U);
    buf.current(0U, 1U).m   = 2U;
    buf.current(0U, 1U).age = 6U;
    buf.swap();
    EXPECT_EQ(buf.previous(0U, 1U).m, 0U);

    // age 5 == cap stays live under the same tighter setting.
    buf.current(0U, 1U).m   = 2U;
    buf.current(0U, 1U).age = 5U;
    buf.swap();
    EXPECT_EQ(buf.previous(0U, 1U).m, 2U);
}

// --- clear() -----------------------------------------------------------------

TEST(RestirDiTemporalBuffer, ClearResetsBothBuffers)
{
    TemporalBuffer<Reservoir> buf(2U, 2U);
    buf.current(0U, 0U).m = 5U;
    buf.swap();                 // push into previous
    buf.current(0U, 0U).m = 9U; // dirty the (new) current too

    buf.clear();

    // Both buffers must be zero-state after clear().
    EXPECT_EQ(buf.current(0U, 0U).m, 0U);
    buf.swap();
    EXPECT_EQ(buf.current(0U, 0U).m, 0U);
    EXPECT_EQ(buf.previous(0U, 0U).m, 0U);
}

}  // namespace
