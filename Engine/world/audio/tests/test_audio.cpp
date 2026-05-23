// =============================================================================
// CHROMODYNAMIC — cd::audio tests (null backend + native dispatcher)
// =============================================================================
#include <cd/audio/AlsaBackend.hpp>
#include <cd/audio/CoreAudioBackend.hpp>
#include <cd/audio/IAudioBackend.hpp>
#include <cd/audio/NativeBackend.hpp>
#include <cd/audio/Positional.hpp>
#include <cd/audio/PositionalSource.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace
{

constexpr std::array<float, 4> kSamples { 0.0F, 0.5F, -0.5F, 0.0F };

TEST(Audio, FactoryBuilds)
{
    auto b = cd::audio::make_null_audio_backend();
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->clip_count(), 0U);
    EXPECT_EQ(b->voice_count(), 0U);
}

TEST(Audio, CreateClipRejectsZeroChannels)
{
    auto b = cd::audio::make_null_audio_backend();
    cd::audio::ClipDesc d {};
    d.samples = kSamples;
    d.channels = 0;
    auto r = b->create_clip(d);
    ASSERT_FALSE(r.has_value());
}

TEST(Audio, CreateAndDestroyClip)
{
    auto b = cd::audio::make_null_audio_backend();
    cd::audio::ClipDesc d {};
    d.samples = kSamples;
    auto r = b->create_clip(d);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(b->clip_count(), 1U);
    b->destroy_clip(*r);
    EXPECT_EQ(b->clip_count(), 0U);
}

TEST(Audio, PlayCreatesVoice)
{
    auto b = cd::audio::make_null_audio_backend();
    cd::audio::ClipDesc d {};
    d.samples = kSamples;
    auto c = b->create_clip(d);
    ASSERT_TRUE(c.has_value());
    auto v = b->play(*c, 0.7F, false);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(b->voice_count(), 1U);
    EXPECT_TRUE(b->is_playing(*v));
}

TEST(Audio, PlayUnknownClipRejected)
{
    auto b = cd::audio::make_null_audio_backend();
    auto v = b->play(cd::audio::ClipHandle {}, 1.0F, false);
    ASSERT_FALSE(v.has_value());
    EXPECT_EQ(v.error().code, static_cast<std::uint32_t>(cd::audio::audio_errors::Code::kUnknownClip));
}

TEST(Audio, StopRemovesVoice)
{
    auto b = cd::audio::make_null_audio_backend();
    cd::audio::ClipDesc d {};
    d.samples = kSamples;
    auto c = b->create_clip(d);
    ASSERT_TRUE(c.has_value());
    auto v = b->play(*c);
    ASSERT_TRUE(v.has_value());
    b->stop(*v);
    EXPECT_FALSE(b->is_playing(*v));
}

TEST(Audio, DestroyClipReapsVoices)
{
    auto b = cd::audio::make_null_audio_backend();
    cd::audio::ClipDesc d {};
    d.samples = kSamples;
    auto c = b->create_clip(d);
    ASSERT_TRUE(c.has_value());
    [[maybe_unused]] auto v1 = b->play(*c);
    [[maybe_unused]] auto v2 = b->play(*c);
    EXPECT_EQ(b->voice_count(), 2U);
    b->destroy_clip(*c);
    EXPECT_EQ(b->voice_count(), 0U);
}

TEST(Audio, MasterVolumeClampedToUnit)
{
    auto b = cd::audio::make_null_audio_backend();
    b->set_master_volume(2.0F);
    EXPECT_FLOAT_EQ(b->master_volume(), 1.0F);
    b->set_master_volume(-1.0F);
    EXPECT_FLOAT_EQ(b->master_volume(), 0.0F);
}

// -----------------------------------------------------------------------------
// Native dispatcher — Wave 34. The factory must always return a usable
// backend (real native on Windows / future macOS / future Linux, or the
// null backend as a guaranteed fallback). Stubs for CoreAudio and ALSA
// always return nullptr right now, so the test only checks that the
// dispatcher honours the fallback contract.
// -----------------------------------------------------------------------------

TEST(NativeBackend, AlwaysReturnsUsableBackend)
{
    auto result = cd::audio::make_native_audio_backend();
    ASSERT_NE(result.backend, nullptr);
    // Sanity: the backend must service the same API regardless of kind.
    EXPECT_EQ(result.backend->clip_count(), 0U);
    EXPECT_EQ(result.backend->voice_count(), 0U);
}

TEST(NativeBackend, CoreAudioReturnsNullOnNonApple)
{
    // Post Wave-79: real AudioUnit impl on __APPLE__, nullptr factory
    // on every other platform. We test on Windows here so the factory
    // returns nullptr; on macOS CI the factory MAY return non-null if
    // the runner has a default audio device, or nullptr in headless mode.
    auto core = cd::audio::make_coreaudio_backend();
#if !defined(__APPLE__)
    EXPECT_EQ(core, nullptr);
#endif
}

TEST(NativeBackend, AlsaReturnsNullOnNonLinux)
{
    // Post Wave-80: real snd_pcm impl on __linux__, nullptr factory
    // on every other host. On Linux CI the factory MAY return non-
    // null if a default audio device is available, or nullptr in
    // headless mode without PipeWire / a /dev/snd device.
    auto alsa = cd::audio::make_alsa_backend();
#if !defined(__linux__)
    EXPECT_EQ(alsa, nullptr);
#endif
}

TEST(NativeBackend, KindMatchesHostPlatform)
{
    // The dispatcher should pick the WASAPI path on Windows. On every
    // other host (or when WASAPI itself fails to initialise — e.g. a
    // headless build agent with no audio service running) we fall back
    // to the null backend. CoreAudio/ALSA always fall through to the
    // null backend right now because those backends are stubs.
    auto result = cd::audio::make_native_audio_backend();
#if defined(_WIN32)
    EXPECT_TRUE(result.kind == cd::audio::NativeBackendKind::kWasapi
                || result.kind == cd::audio::NativeBackendKind::kNullFallback);
#else
    EXPECT_EQ(result.kind, cd::audio::NativeBackendKind::kNullFallback);
#endif
}

// -----------------------------------------------------------------------------
// Positional audio math — Wave 77
// -----------------------------------------------------------------------------

TEST(Positional, SourceInFrontProducesZeroItdAndZeroIld)
{
    cd::audio::ListenerPose listener {};  // at origin, looking -Z, up +Y
    const cd::math::Vec3f src { 0.0F, 0.0F, -5.0F };  // directly in front
    EXPECT_NEAR(cd::audio::compute_itd_seconds(listener, src), 0.0F, 1.0e-5F);
    EXPECT_NEAR(cd::audio::compute_ild_db(listener, src), 0.0F, 1.0e-3F);
}

TEST(Positional, SourceOnRightDelaysLeftEar)
{
    // Listener at origin, source 5 m to its right. Right ear hears
    // first → ITD should be NEGATIVE (right ear delayed = positive
    // by our convention; right ear EARLIER = negative).
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f src { 5.0F, 0.0F, 0.0F };
    const float itd = cd::audio::compute_itd_seconds(listener, src);
    EXPECT_LT(itd, 0.0F);   // right ear arrives first → ITD negative
    EXPECT_GT(itd, -0.001F);  // within ±1 ms (Woodworth max ~640 µs)
}

TEST(Positional, SourceOnRightIldFavoursRight)
{
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f src { 5.0F, 0.0F, 0.0F };  // right
    EXPECT_GT(cd::audio::compute_ild_db(listener, src), 0.0F);
}

TEST(Positional, SourceOnLeftIldFavoursLeft)
{
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f src { -5.0F, 0.0F, 0.0F };  // left
    EXPECT_LT(cd::audio::compute_ild_db(listener, src), 0.0F);
}

TEST(Positional, StereoGainsSumToConstantPower)
{
    cd::audio::ListenerPose listener {};
    // Source directly ahead at the reference distance → constant-power
    // pan gives equal gains of ~sqrt(0.5).
    const cd::math::Vec3f src { 0.0F, 0.0F, -1.0F };
    auto g = cd::audio::compute_stereo_gains(listener, src, 1.0F, 1.0F);
    EXPECT_NEAR(g.left, 0.707F, 0.05F);
    EXPECT_NEAR(g.right, 0.707F, 0.05F);
}

TEST(Positional, StereoGainsAttenuateWithDistance)
{
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f near_src { 0.0F, 0.0F, -1.0F };
    const cd::math::Vec3f far_src { 0.0F, 0.0F, -10.0F };
    auto g_near = cd::audio::compute_stereo_gains(listener, near_src);
    auto g_far = cd::audio::compute_stereo_gains(listener, far_src);
    EXPECT_GT(g_near.left, g_far.left);
    EXPECT_GT(g_near.right, g_far.right);
}

TEST(Positional, StereoGainsHardRightZerosLeft)
{
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f src { 100.0F, 0.0F, 0.0F };  // ~90° right
    auto g = cd::audio::compute_stereo_gains(listener, src);
    EXPECT_NEAR(g.left, 0.0F, 0.05F);
    EXPECT_LT(g.right, 1.0F);  // also attenuated by distance
}

TEST(Positional, ListenerAtSourcePositionReturnsUnity)
{
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f src { 0.0F, 0.0F, 0.0F };  // coincident
    auto g = cd::audio::compute_stereo_gains(listener, src);
    EXPECT_NEAR(g.left, 1.0F, 1.0e-5F);
    EXPECT_NEAR(g.right, 1.0F, 1.0e-5F);
}

// -----------------------------------------------------------------------------
// PositionalSource (HRTF-lite mixer) — Wave 78
// -----------------------------------------------------------------------------

TEST(PositionalSource, PrimeAndProcessSizeContract)
{
    cd::audio::PositionalSource src;
    src.prime(/*sample_rate=*/48000, /*max_block=*/128);
    EXPECT_EQ(src.sample_rate(), 48000U);
    EXPECT_EQ(src.max_block_samples(), 128U);

    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f pos { 0.0F, 0.0F, -1.0F };
    std::vector<float> mono(64, 0.5F);
    std::vector<float> stereo(64 * 2, 0.0F);
    src.process(mono, listener, pos, stereo);
    // Front source → near-equal L/R gains.
    EXPECT_NEAR(stereo[0], stereo[1], 0.1F);
}

TEST(PositionalSource, SizeMismatchIsSafelyNoOp)
{
    cd::audio::PositionalSource src;
    src.prime(48000, 64);
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f pos { 0.0F, 0.0F, -1.0F };
    std::vector<float> mono(32, 0.5F);
    std::vector<float> stereo(40, -1.0F);  // wrong size (should be 64)
    src.process(mono, listener, pos, stereo);
    // Output not touched — sentinel value remains.
    EXPECT_FLOAT_EQ(stereo[0], -1.0F);
    EXPECT_FLOAT_EQ(stereo[39], -1.0F);
}

TEST(PositionalSource, RightSourceFavoursRightChannel)
{
    cd::audio::PositionalSource src;
    src.prime(48000, 128);
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f pos { 5.0F, 0.0F, 0.0F };  // hard right
    std::vector<float> mono(128, 1.0F);
    std::vector<float> stereo(128 * 2, 0.0F);
    src.process(mono, listener, pos, stereo);
    // Sample a late index after the delay-line warmup.
    EXPECT_GT(stereo[200 + 1], stereo[200 + 0]);  // right > left
}

TEST(PositionalSource, ItdDelaysFartherEar)
{
    cd::audio::PositionalSource src;
    src.prime(48000, 64);
    cd::audio::ListenerPose listener {};
    // Source 45° to the right: left ear (farther) receives the signal
    // a few samples later but with nonzero gain so we can measure it.
    // Hard 90° would zero the left channel via the constant-power pan.
    const cd::math::Vec3f pos { 1.0F, 0.0F, -1.0F };
    std::vector<float> mono(64, 0.0F);
    mono[0] = 1.0F;  // impulse at t=0
    std::vector<float> stereo(64 * 2, 0.0F);
    src.process(mono, listener, pos, stereo);

    // Find first non-zero index in each channel.
    int first_left = -1, first_right = -1;
    for (std::size_t i = 0; i < 64; ++i)
    {
        if (first_right < 0 && stereo[i * 2 + 1] != 0.0F)
            first_right = static_cast<int>(i);
        if (first_left < 0 && stereo[i * 2 + 0] != 0.0F)
            first_left = static_cast<int>(i);
    }
    ASSERT_GE(first_right, 0);
    ASSERT_GE(first_left, 0);
    EXPECT_GE(first_left, first_right);  // left is delayed (or equal at 0 azimuth)
}

TEST(PositionalSource, ResetClearsDelayLines)
{
    cd::audio::PositionalSource src;
    src.prime(48000, 32);
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f pos { 5.0F, 0.0F, 0.0F };
    std::vector<float> mono(32, 1.0F);
    std::vector<float> stereo(64, 0.0F);
    src.process(mono, listener, pos, stereo);
    src.reset();
    // After reset, process zeros into stereo should give all-zero
    // output (no residual from earlier block).
    std::vector<float> mono_zero(32, 0.0F);
    std::vector<float> stereo_out(64, -1.0F);
    src.process(mono_zero, listener, pos, stereo_out);
    for (float v : stereo_out)
        EXPECT_FLOAT_EQ(v, 0.0F);
}

}  // namespace
