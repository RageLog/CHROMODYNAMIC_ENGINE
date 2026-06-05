// =============================================================================
// CHROMODYNAMIC — cd::audio::spatial::HrtfMixer tests
// Phase 751 — Sprint-2: real HRTF convolution construct round-trip.
//
// Test strategy:
//   OpenAL Soft requires a real audio device (or a null/headless driver).
//   On CI nodes without audio hardware the context creation will fail.
//   We follow the Vulkan-style GTEST_SKIP pattern: if init() returns false
//   we skip the test rather than failing it.  Device-present machines (dev
//   workstations, machines with virtual audio) will exercise the full path.
//
// Tests:
//  1. HrtfMixer default-constructs without crashing.
//  2. HrtfMixer::init() returns bool; if false → GTEST_SKIP.
//  3. If init() succeeds:
//       a. is_initialized() == true.
//       b. source_count() == 0 initially.
//       c. add_hrtf_source() returns non-zero handle.
//       d. source_count() increments.
//       e. set_source_pose() on valid handle — no crash.
//       f. update_listener() — no crash.
//       g. remove_hrtf_source() decrements count.
//       h. remove_hrtf_source() on unknown id — no crash (no-op).
//       i. source_count() after remove == 0.
//       j. Construct → shutdown() → re-init() round-trip.
//  4. CD_AUDIO_SPATIAL_HAS_OPENAL compile-time flag accessible.
//  5. Sprint-1 SpatialMixer tests still pass (regression guard: ITD/ILD tests
//     remain in test_audio_spatial.cpp; this file only covers HrtfMixer).
// =============================================================================

#include <cd/audio/spatial/HrtfMixer.hpp>

#include <gtest/gtest.h>

namespace
{

// ---------------------------------------------------------------------------
// TEST 1 — HrtfMixer default-constructs without crash
// ---------------------------------------------------------------------------
TEST(HrtfMixer_Lifecycle, DefaultConstruct)
{
    cd::audio::spatial::HrtfMixer mixer;
    EXPECT_FALSE(mixer.is_initialized());
    EXPECT_FALSE(mixer.is_hrtf_active());
    EXPECT_EQ(mixer.source_count(), 0u);
    EXPECT_TRUE(mixer.device_name().empty());
}

// ---------------------------------------------------------------------------
// TEST 2 — init() returns bool; skip if no device
// ---------------------------------------------------------------------------
TEST(HrtfMixer_Lifecycle, InitOrSkip)
{
    cd::audio::spatial::HrtfMixer mixer;
    const bool ok = mixer.init();

    if (!ok)
    {
        GTEST_SKIP() << "No audio device available — HrtfMixer::init() returned false. "
                        "This is expected on headless CI. "
                        "CD_AUDIO_SPATIAL_HAS_OPENAL=" << CD_AUDIO_SPATIAL_HAS_OPENAL;
    }

    EXPECT_TRUE(mixer.is_initialized());
    // HRTF may or may not be active depending on driver support.
    // We do NOT assert is_hrtf_active() == true here — a plain OpenAL
    // context without HRTF extension is still valid for the no-op fallback.
    SUCCEED() << "init() succeeded. HRTF active: " << mixer.is_hrtf_active()
              << " device: '" << mixer.device_name() << "'";
}

// ---------------------------------------------------------------------------
// Helper fixture — skips entire test body if init() fails.
// ---------------------------------------------------------------------------
class HrtfMixerDevice : public ::testing::Test
{
protected:
    cd::audio::spatial::HrtfMixer mixer_;

    void SetUp() override
    {
        if (!mixer_.init())
        {
            GTEST_SKIP() << "No audio device — skipping HrtfMixer device tests. "
                            "CD_AUDIO_SPATIAL_HAS_OPENAL=" << CD_AUDIO_SPATIAL_HAS_OPENAL;
        }
    }
};

// ---------------------------------------------------------------------------
// TEST 3a — is_initialized() == true after successful init
// ---------------------------------------------------------------------------
TEST_F(HrtfMixerDevice, IsInitializedAfterInit)
{
    EXPECT_TRUE(mixer_.is_initialized());
}

// ---------------------------------------------------------------------------
// TEST 3b — source_count() == 0 initially
// ---------------------------------------------------------------------------
TEST_F(HrtfMixerDevice, SourceCountZeroInitially)
{
    EXPECT_EQ(mixer_.source_count(), 0u);
}

// ---------------------------------------------------------------------------
// TEST 3c — add_hrtf_source() returns non-zero handle
// ---------------------------------------------------------------------------
TEST_F(HrtfMixerDevice, AddSourceReturnsNonZeroHandle)
{
    const cd::audio::spatial::HrtfSourceId id =
        mixer_.add_hrtf_source(42u, { 5.0F, 0.0F, 0.0F });

    EXPECT_NE(id, 0u)
        << "add_hrtf_source must return a non-zero handle when device is available";
}

// ---------------------------------------------------------------------------
// TEST 3d — source_count() increments after add
// ---------------------------------------------------------------------------
TEST_F(HrtfMixerDevice, AddSourceIncrementsCount)
{
    const auto h1 = mixer_.add_hrtf_source(1u, { 1.0F, 0.0F, 0.0F });
    const auto h2 = mixer_.add_hrtf_source(2u, { 2.0F, 0.0F, 0.0F });

    EXPECT_NE(h1, 0u);
    EXPECT_NE(h2, 0u);
    EXPECT_EQ(mixer_.source_count(), 2u);
}

// ---------------------------------------------------------------------------
// TEST 3e — set_source_pose() on valid handle does not crash
// ---------------------------------------------------------------------------
TEST_F(HrtfMixerDevice, SetSourcePoseNoCrash)
{
    const auto handle = mixer_.add_hrtf_source(10u, { 3.0F, 0.0F, 0.0F });
    ASSERT_NE(handle, 0u);

    // Move the source — must not crash or assert.
    mixer_.set_source_pose(handle,
        { -1.0F, 2.0F, 0.5F },
        { 0.0F,  0.0F, 5.0F });
    SUCCEED();
}

// ---------------------------------------------------------------------------
// TEST 3f — update_listener() does not crash
// ---------------------------------------------------------------------------
TEST_F(HrtfMixerDevice, UpdateListenerNoCrash)
{
    // Identity quaternion (facing -Z, up +Y)
    mixer_.update_listener(
        { 0.0F, 0.0F, 0.0F },          // position
        { 0.0F, 0.0F, 0.0F, 1.0F },    // orientation (identity)
        { 0.0F, 0.0F, 0.0F }           // velocity
    );
    SUCCEED();
}

// ---------------------------------------------------------------------------
// TEST 3g — remove_hrtf_source() decrements count
// ---------------------------------------------------------------------------
TEST_F(HrtfMixerDevice, RemoveSourceDecrementsCount)
{
    const auto h1 = mixer_.add_hrtf_source(20u, { 1.0F, 0.0F, 0.0F });
    const auto h2 = mixer_.add_hrtf_source(21u, { 2.0F, 0.0F, 0.0F });
    ASSERT_NE(h1, 0u);
    ASSERT_NE(h2, 0u);
    EXPECT_EQ(mixer_.source_count(), 2u);

    mixer_.remove_hrtf_source(h1);
    EXPECT_EQ(mixer_.source_count(), 1u);
}

// ---------------------------------------------------------------------------
// TEST 3h — remove_hrtf_source() on unknown id is a safe no-op
// ---------------------------------------------------------------------------
TEST_F(HrtfMixerDevice, RemoveUnknownSourceIsNoOp)
{
    mixer_.remove_hrtf_source(99999u);  // must not crash or assert
    SUCCEED();
}

// ---------------------------------------------------------------------------
// TEST 3i — source_count() == 0 after removing all sources
// ---------------------------------------------------------------------------
TEST_F(HrtfMixerDevice, RemoveAllSourcesCountIsZero)
{
    const auto h1 = mixer_.add_hrtf_source(30u, { 0.0F, 0.0F, -5.0F });
    ASSERT_NE(h1, 0u);

    mixer_.remove_hrtf_source(h1);
    EXPECT_EQ(mixer_.source_count(), 0u);
}

// ---------------------------------------------------------------------------
// TEST 3j — construct → shutdown() → re-init() round-trip
// ---------------------------------------------------------------------------
TEST_F(HrtfMixerDevice, ShutdownAndReinitRoundTrip)
{
    EXPECT_TRUE(mixer_.is_initialized());

    mixer_.shutdown();
    EXPECT_FALSE(mixer_.is_initialized());
    EXPECT_FALSE(mixer_.is_hrtf_active());
    EXPECT_EQ(mixer_.source_count(), 0u);

    // Re-initialise on the same object.
    const bool ok = mixer_.init();
    if (!ok)
    {
        GTEST_SKIP() << "Re-init() failed (device released after shutdown) — "
                        "acceptable on some drivers";
    }
    EXPECT_TRUE(mixer_.is_initialized());
}

// ---------------------------------------------------------------------------
// TEST 4 — CD_AUDIO_SPATIAL_HAS_OPENAL compile-time flag is 0 or 1
// ---------------------------------------------------------------------------
TEST(HrtfMixer_CompileFlags, HasOpenALFlagIsBooleanInt)
{
    // The flag must be defined as an integer constant (0 or 1).
    // This test validates that the CMake wiring propagated the definition.
    const int flag = CD_AUDIO_SPATIAL_HAS_OPENAL;
    EXPECT_TRUE(flag == 0 || flag == 1)
        << "CD_AUDIO_SPATIAL_HAS_OPENAL must be 0 or 1, got " << flag;
}

}  // anonymous namespace
