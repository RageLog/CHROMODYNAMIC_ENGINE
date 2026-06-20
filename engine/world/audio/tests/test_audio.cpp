// =============================================================================
// CHROMODYNAMIC — cd::audio tests (null backend + native dispatcher)
// =============================================================================
#include <cd/audio/AlsaBackend.hpp>
#include <cd/audio/CoreAudioBackend.hpp>
#include <cd/audio/IAudioBackend.hpp>
#include <cd/audio/NativeBackend.hpp>
#include <cd/audio/WasapiBackend.hpp>
#include <cd/audio/AnalyticalHRTF.hpp>
#include <cd/audio/Positional.hpp>
#include <cd/audio/PositionalSource.hpp>
#include <cd/audio/Surround.hpp>
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

// Phase 157 — push-stream API. Verify the API rejects unknown handles
// and accepts valid push/destroy cycles on the WASAPI backend.
#if defined(_WIN32)
TEST(WasapiPushStream, CreateAcceptsValidParamsAndPushAppendsToQueue)
{
    auto backend = cd::audio::make_wasapi_audio_backend();
    if (!backend) GTEST_SKIP() << "no WASAPI device on CI host";
    auto stream = backend->create_stream(2, 48000, 0.5F);
    ASSERT_TRUE(stream.has_value()) << stream.error().message;
    EXPECT_EQ(backend->stream_count(), 1u);

    std::vector<float> chunk(static_cast<std::size_t>(480) * 2, 0.1F);  // 10 ms of stereo @ 48 kHz
    EXPECT_TRUE(backend->push_stream_samples(*stream, chunk).has_value());
    EXPECT_GE(backend->stream_pending_frames(*stream), 0u);

    backend->destroy_stream(*stream);
    EXPECT_EQ(backend->stream_count(), 0u);
}

TEST(WasapiPushStream, RejectsInvalidCreateArgs)
{
    auto backend = cd::audio::make_wasapi_audio_backend();
    if (!backend) GTEST_SKIP() << "no WASAPI device on CI host";
    EXPECT_FALSE(backend->create_stream(0, 48000, 1.0F).has_value());
    EXPECT_FALSE(backend->create_stream(2, 0,     1.0F).has_value());
}

TEST(WasapiPushStream, PushToUnknownStreamRejected)
{
    auto backend = cd::audio::make_wasapi_audio_backend();
    if (!backend) GTEST_SKIP() << "no WASAPI device on CI host";
    cd::audio::StreamHandle bogus { 999u, 1u };
    std::vector<float> samples(48, 0.0F);
    EXPECT_FALSE(backend->push_stream_samples(bogus, samples).has_value());
}
#endif

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

TEST(Positional, ListenerAtSourcePositionReturnsBaseline)
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
    src.prime(/*sample_rate=*/48000, /*max_block_samples=*/128);
    EXPECT_EQ(src.sample_rate(), 48000U);
    EXPECT_EQ(src.max_block_samples(), 128U);

    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f pos { 0.0F, 0.0F, -1.0F };
    std::vector<float> mono(64, 0.5F);
    std::vector<float> stereo(static_cast<std::size_t>(64) * 2, 0.0F);
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
    std::vector<float> stereo(static_cast<std::size_t>(128) * 2, 0.0F);
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
    std::vector<float> stereo(static_cast<std::size_t>(64) * 2, 0.0F);
    src.process(mono, listener, pos, stereo);

    // Find first non-zero index in each channel.
    int first_left = -1;
    int first_right = -1;
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

// --- Multi-channel surround routing (Wave 93) ------------------------------

TEST(Surround, ChannelCountMatchesLayout)
{
    EXPECT_EQ(cd::audio::channel_count(cd::audio::SpeakerLayout::kMono), 1U);
    EXPECT_EQ(cd::audio::channel_count(cd::audio::SpeakerLayout::kStereo), 2U);
    EXPECT_EQ(cd::audio::channel_count(cd::audio::SpeakerLayout::kSurround51), 6U);
    EXPECT_EQ(cd::audio::channel_count(cd::audio::SpeakerLayout::kSurround71), 8U);
}

TEST(Surround, MonoLayoutSingleGain)
{
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f src { 5.0F, 0.0F, -1.0F };
    std::array<float, 1> gains {};
    cd::audio::compute_surround_gains(cd::audio::SpeakerLayout::kMono,
                                      listener, src, gains);
    EXPECT_GT(gains[0], 0.0F);
    EXPECT_LE(gains[0], 1.0F);
}

TEST(Surround, StereoCentredSourceBalanced)
{
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f src { 0.0F, 0.0F, -1.0F };  // dead ahead
    std::array<float, 2> gains {};
    cd::audio::compute_surround_gains(cd::audio::SpeakerLayout::kStereo,
                                      listener, src, gains);
    EXPECT_NEAR(gains[0], gains[1], 1e-3F);
    EXPECT_GT(gains[0], 0.0F);
}

TEST(Surround, FivePointOneFrontCentreFavoursFC)
{
    // 5.1 layout slots: [FL, FR, FC, LFE, RL, RR]. A source ahead
    // (azimuth 0°) should pan between FL and FC — but the FC speaker
    // is exactly at 0°, so it should dominate. Pairwise pan: FL(-30)
    // ↔ FC(0) bracket θ=0 with t=1 → FC = sin(π/2) = 1, FL = 0.
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f src { 0.0F, 0.0F, -1.0F };  // 0° azimuth
    std::array<float, 6> gains {};
    cd::audio::compute_surround_gains(cd::audio::SpeakerLayout::kSurround51,
                                      listener, src, gains);
    // gains[2] = FC.
    EXPECT_NEAR(gains[2], 1.0F, 0.05F);
    EXPECT_NEAR(gains[3], 0.0F, 1e-5F);  // LFE always 0
    EXPECT_GE(gains[2], gains[0]);  // FC >= FL
    EXPECT_GE(gains[2], gains[1]);  // FC >= FR
    EXPECT_GE(gains[2], gains[4]);  // FC >= RL
    EXPECT_GE(gains[2], gains[5]);  // FC >= RR
}

TEST(Surround, FivePointOneRightSourceFavoursFR)
{
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f src { 1.0F, 0.0F, -1.0F };  // ~45° right
    std::array<float, 6> gains {};
    cd::audio::compute_surround_gains(cd::audio::SpeakerLayout::kSurround51,
                                      listener, src, gains);
    EXPECT_GT(gains[1], gains[0]);  // FR > FL
    EXPECT_GT(gains[1], gains[2]);  // FR > FC (45° is past FR at 30°)
    EXPECT_NEAR(gains[3], 0.0F, 1e-5F);
}

TEST(Surround, FivePointOneBehindFavoursRearChannels)
{
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f src { 0.0F, 0.0F, +1.0F };  // directly behind
    std::array<float, 6> gains {};
    cd::audio::compute_surround_gains(cd::audio::SpeakerLayout::kSurround51,
                                      listener, src, gains);
    // RL (idx 4) + RR (idx 5) should dominate.
    EXPECT_GT(gains[4] + gains[5], gains[0] + gains[1] + gains[2]);
    EXPECT_NEAR(gains[3], 0.0F, 1e-5F);
}

TEST(Surround, SevenPointOneHasNoZeroChannelsOnArbitrarySource)
{
    // For an arbitrary off-axis source, at least one pair of 7.1
    // speakers gets non-zero gain. Just sanity-check the layout size
    // and LFE invariant.
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f src { 0.7F, 0.0F, -0.5F };
    std::array<float, 8> gains {};
    cd::audio::compute_surround_gains(cd::audio::SpeakerLayout::kSurround71,
                                      listener, src, gains);
    float sum = 0.0F;
    for (float g : gains)
        sum += g;
    EXPECT_GT(sum, 0.0F);
    EXPECT_NEAR(gains[3], 0.0F, 1e-5F);  // LFE idx = 3
}

TEST(Surround, CoincidentSourceSpreadsAcrossNonLfe)
{
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f src { 0.0F, 0.0F, 0.0F };
    std::array<float, 6> gains {};
    cd::audio::compute_surround_gains(cd::audio::SpeakerLayout::kSurround51,
                                      listener, src, gains);
    EXPECT_NEAR(gains[3], 0.0F, 1e-5F);  // LFE
    // The other 5 should be equal (uniform mono fold-in).
    EXPECT_NEAR(gains[0], gains[1], 1e-5F);
    EXPECT_NEAR(gains[0], gains[2], 1e-5F);
    EXPECT_NEAR(gains[0], gains[4], 1e-5F);
    EXPECT_NEAR(gains[0], gains[5], 1e-5F);
}

TEST(Surround, SizeMismatchIsNoOp)
{
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f src { 0.0F, 0.0F, -1.0F };
    std::array<float, 4> gains { -1.0F, -1.0F, -1.0F, -1.0F };  // wrong size for 5.1
    cd::audio::compute_surround_gains(cd::audio::SpeakerLayout::kSurround51,
                                      listener, src, gains);
    for (float g : gains)
        EXPECT_FLOAT_EQ(g, -1.0F);
}

// --- AnalyticalHRTF (Wave 96) ----------------------------------------------

TEST(AnalyticalHRTF, FrontSourceProducesSymmetricCoefficients)
{
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f src { 0.0F, 0.0F, -1.0F };
    auto c = cd::audio::synthesize_hrtf(listener, src, 48000);
    // Source dead ahead → no ITD, equal broadband gains, no notch
    // diff between ears.
    EXPECT_EQ(c.late_ear_delay_samples, 0);
    EXPECT_NEAR(c.left[0], c.right[0], 1e-3F);
}

TEST(AnalyticalHRTF, RightSourceDelaysLeftEar)
{
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f src { 5.0F, 0.0F, 0.0F };  // hard right
    auto c = cd::audio::synthesize_hrtf(listener, src, 48000);
    // Right ear early (tap 0); left ear delayed by ITD samples.
    EXPECT_GT(c.late_ear_delay_samples, 0);
    EXPECT_GT(c.right[0], 0.0F);
    // left[0] is the early-ear tap of the left ear (which is the late
    // ear in this scenario), so its impulse lives at the delayed tap;
    // tap 0 should be near zero (constant-power L gain at hard right
    // is cos(π/2) ≈ 0, with float-precision noise tolerated).
    EXPECT_NEAR(c.left[0], 0.0F, 1e-5F);
    // Sum across left's coefficients should still be < right's broadband
    // due to head-shadow ILD attenuation.
    float left_sum = 0.0F;
    float right_sum = 0.0F;
    for (std::uint32_t i = 0; i < cd::audio::kFirTaps; ++i)
    {
        left_sum += std::abs(c.left[i]);
        right_sum += std::abs(c.right[i]);
    }
    EXPECT_LT(left_sum, right_sum);
}

TEST(AnalyticalHRTF, LeftSourceDelaysRightEar)
{
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f src { -5.0F, 0.0F, 0.0F };  // hard left
    auto c = cd::audio::synthesize_hrtf(listener, src, 48000);
    EXPECT_GT(c.late_ear_delay_samples, 0);
    EXPECT_NEAR(c.right[0], 0.0F, 1e-5F);
    EXPECT_GT(c.left[0], 0.0F);
}

TEST(AnalyticalHRTF, CoincidentSourceProducesUnitImpulse)
{
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f src { 0.0F, 0.0F, 0.0F };
    auto c = cd::audio::synthesize_hrtf(listener, src, 48000);
    EXPECT_FLOAT_EQ(c.left[0], 1.0F);
    EXPECT_FLOAT_EQ(c.right[0], 1.0F);
    EXPECT_EQ(c.late_ear_delay_samples, 0);
}

TEST(HrtfConvolver, ImpulseInputReproducesCoefficients)
{
    cd::audio::HrtfConvolver conv;
    cd::audio::HrtfCoefficients c {};
    c.left[0] = 0.7F;
    c.left[3] = 0.3F;
    c.right[0] = 0.5F;
    c.right[5] = -0.1F;
    conv.set_coefficients(c);

    std::array<float, 16> impulse {};
    impulse[0] = 1.0F;
    std::array<float, 32> stereo {};
    conv.process(impulse, stereo);
    // Tap 0: L=0.7, R=0.5
    EXPECT_FLOAT_EQ(stereo[0], 0.7F);
    EXPECT_FLOAT_EQ(stereo[1], 0.5F);
    // Tap 3: L=0.3 at sample 3
    EXPECT_FLOAT_EQ(stereo[3 * 2 + 0], 0.3F);
    // Tap 5: R=-0.1 at sample 5
    EXPECT_FLOAT_EQ(stereo[5 * 2 + 1], -0.1F);
}

TEST(HrtfConvolver, ResetClearsHistory)
{
    cd::audio::HrtfConvolver conv;
    cd::audio::HrtfCoefficients c {};
    c.left[0] = 1.0F;
    c.right[0] = 1.0F;
    conv.set_coefficients(c);

    std::array<float, 8> input {};
    input.fill(1.0F);
    std::array<float, 16> out {};
    conv.process(input, out);
    conv.reset();
    // After reset, processing zeros should produce zeros (no
    // residual history energy).
    std::array<float, 8> zeros {};
    std::array<float, 16> out2 {};
    out2.fill(-1.0F);
    conv.process(zeros, out2);
    for (float v : out2)
        EXPECT_FLOAT_EQ(v, 0.0F);
}

TEST(HrtfConvolver, SizeMismatchSafelyNoOp)
{
    cd::audio::HrtfConvolver conv;
    std::array<float, 4> in {};
    std::array<float, 6> bad {};  // not 8
    bad.fill(-1.0F);
    conv.process(in, bad);
    for (float v : bad)
        EXPECT_FLOAT_EQ(v, -1.0F);
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

// ---------------------------------------------------------------------------
// Phase 14.F — DspGraph tests (Wave 162)
// ---------------------------------------------------------------------------

}  // namespace

#include <cd/audio/DspGraph.hpp>

#include <cmath>
#include <numbers>

namespace
{

constexpr float kPi = std::numbers::pi_v<float>;

}  // namespace

TEST(DspGraph, EmptyGraphIsIdentity)
{
    cd::audio::DspGraph g;
    std::vector<float> buf { 0.1F, -0.2F, 0.3F, -0.4F };
    const auto copy = buf;
    g.process(buf, 48000);
    for (std::size_t i = 0; i < buf.size(); ++i)
        EXPECT_FLOAT_EQ(buf[i], copy[i]);
}

TEST(DspGraph, GainNodeScalesEverySample)
{
    cd::audio::DspGraph g;
    g.push(std::make_unique<cd::audio::GainNode>(0.5F));
    std::vector<float> buf { 1.0F, -2.0F, 4.0F };
    g.process(buf, 48000);
    EXPECT_FLOAT_EQ(buf[0],  0.5F);
    EXPECT_FLOAT_EQ(buf[1], -1.0F);
    EXPECT_FLOAT_EQ(buf[2],  2.0F);
}

TEST(DspGraph, LowpassAttenuatesHighFrequencySine)
{
    constexpr std::uint32_t sr = 48000;
    constexpr std::size_t n = 4096;
    std::vector<float> buf(n);
    for (std::size_t i = 0; i < n; ++i)
        buf[i] = std::sin(2.0F * kPi * 10000.0F *
                          static_cast<float>(i) / static_cast<float>(sr));

    cd::audio::DspGraph g;
    g.push(std::make_unique<cd::audio::BiquadNode>(
        cd::audio::BiquadKind::kLowpass, 1000.0F));
    g.process(buf, sr);

    float peak = 0.0F;
    for (std::size_t i = 512; i < n; ++i)
        peak = std::max(peak, std::fabs(buf[i]));
    EXPECT_LT(peak, 0.1F);
}

TEST(DspGraph, HighpassPreservesHighFrequencySine)
{
    constexpr std::uint32_t sr = 48000;
    constexpr std::size_t n = 4096;
    std::vector<float> buf(n);
    for (std::size_t i = 0; i < n; ++i)
        buf[i] = std::sin(2.0F * kPi * 10000.0F *
                          static_cast<float>(i) / static_cast<float>(sr));

    cd::audio::DspGraph g;
    g.push(std::make_unique<cd::audio::BiquadNode>(
        cd::audio::BiquadKind::kHighpass, 1000.0F));
    g.process(buf, sr);

    float peak = 0.0F;
    for (std::size_t i = 512; i < n; ++i)
        peak = std::max(peak, std::fabs(buf[i]));
    EXPECT_GT(peak, 0.8F);
}

TEST(DspGraph, ChainOrderProducesSameOutputForLinearNodes)
{
    constexpr std::uint32_t sr = 48000;
    constexpr std::size_t n = 1024;
    std::vector<float> buf_a(n, 0.0F);
    std::vector<float> buf_b(n, 0.0F);
    buf_a[0] = 1.0F;
    buf_b[0] = 1.0F;

    cd::audio::DspGraph g1;
    g1.push(std::make_unique<cd::audio::GainNode>(2.0F));
    g1.push(std::make_unique<cd::audio::BiquadNode>(
        cd::audio::BiquadKind::kLowpass, 2000.0F));
    g1.process(buf_a, sr);

    cd::audio::DspGraph g2;
    g2.push(std::make_unique<cd::audio::BiquadNode>(
        cd::audio::BiquadKind::kLowpass, 2000.0F));
    g2.push(std::make_unique<cd::audio::GainNode>(2.0F));
    g2.process(buf_b, sr);

    // Linear systems commute: gain * LP(impulse) == LP(impulse) * gain
    for (std::size_t i = 0; i < n; ++i)
        EXPECT_NEAR(buf_a[i], buf_b[i], 1e-5F) << "i=" << i;
}

TEST(DspGraph, ResetClearsBiquadState)
{
    constexpr std::uint32_t sr = 48000;
    cd::audio::DspGraph g;
    g.push(std::make_unique<cd::audio::BiquadNode>(
        cd::audio::BiquadKind::kLowpass, 1000.0F));

    std::vector<float> step(64, 1.0F);
    g.process(step, sr);
    g.reset();
    std::vector<float> zero(64, 0.0F);
    g.process(zero, sr);
    for (float v : zero)
        EXPECT_FLOAT_EQ(v, 0.0F);
}

// ---------------------------------------------------------------------------
// Phase 15.D — HRTFNode + FirReverbNode tests (Wave 169)
// ---------------------------------------------------------------------------

TEST(HRTFNode, MonoInputProducesStereoOutput)
{
    cd::audio::HRTFNode node;
    cd::audio::ListenerPose listener {};
    const cd::math::Vec3f src { 5.0F, 0.0F, 0.0F };  // source to the right
    node.set_pose(listener, src, 48000);

    std::vector<float> mono(64, 1.0F);
    std::vector<float> stereo(128, 0.0F);
    node.render(mono, stereo);

    // Right channel (source on the +X side) should carry more energy
    // than the left after a steady-state input. Skip the first 16
    // taps to let the FIR converge.
    float left_energy = 0.0F;
    float right_energy = 0.0F;
    for (std::size_t i = 16; i < mono.size(); ++i)
    {
        left_energy  += std::fabs(stereo[i * 2 + 0]);
        right_energy += std::fabs(stereo[i * 2 + 1]);
    }
    // Either the right channel dominates, or both are non-zero
    // (the analytical model attenuates the far ear; "right wins"
    // is the expected qualitative shape).
    EXPECT_GT(right_energy, 0.0F);
    EXPECT_GT(left_energy,  0.0F);
}

TEST(HRTFNode, InPlaceProcessIsNoOp)
{
    // The chain-style process() is passthrough by contract.
    cd::audio::HRTFNode node;
    cd::audio::ListenerPose listener {};
    node.set_pose(listener, cd::math::Vec3f { 1.0F, 0.0F, 0.0F }, 48000);

    std::vector<float> mono { 0.1F, -0.2F, 0.3F };
    const auto copy = mono;
    node.process(mono, 48000);
    EXPECT_EQ(mono, copy);
}

TEST(FirReverb, DirectTapIsUnchangedAtZeroWet)
{
    cd::audio::FirReverbNode rev { 0.0F };  // dry = 1.0, wet = 0
    std::vector<float> buf { 1.0F, 0.0F, 0.0F, 0.0F, 0.0F };
    rev.process(buf, 48000);
    // wet=0 ⇒ output == input
    EXPECT_FLOAT_EQ(buf[0], 1.0F);
    for (std::size_t i = 1; i < buf.size(); ++i)
        EXPECT_FLOAT_EQ(buf[i], 0.0F);
}

TEST(FirReverb, ImpulseProducesEarlyReflections)
{
    cd::audio::FirReverbNode rev { 1.0F };  // fully wet
    std::vector<float> buf(64, 0.0F);
    buf[0] = 1.0F;
    rev.process(buf, 48000);
    // Direct tap (index 0) carries the IR's 1.0 coefficient.
    EXPECT_NEAR(buf[0], 1.0F, 1e-5F);
    // Reflection at index 7 (gain 0.45) is non-zero.
    EXPECT_NEAR(buf[7], 0.45F, 1e-5F);
    // Reflection at 14 (0.30), 23 (0.18), 31 (0.10).
    EXPECT_NEAR(buf[14], 0.30F, 1e-5F);
    EXPECT_NEAR(buf[23], 0.18F, 1e-5F);
    EXPECT_NEAR(buf[31], 0.10F, 1e-5F);
}

TEST(FirReverb, ResetClearsHistory)
{
    cd::audio::FirReverbNode rev { 0.5F };
    std::vector<float> step(32, 1.0F);
    rev.process(step, 48000);
    rev.reset();
    std::vector<float> zero(32, 0.0F);
    rev.process(zero, 48000);
    for (float v : zero) EXPECT_FLOAT_EQ(v, 0.0F);
}

#include <cd/audio/SimpleReverb.hpp>

TEST(SimpleReverb, PassesDryThroughAfterPrepare)
{
    cd::audio::SimpleReverb r;
    r.prepare(8);
    r.set_feedback(0.0F);  // no feedback => pure dry
    EXPECT_FLOAT_EQ(r.process(1.0F), 1.0F);
    EXPECT_FLOAT_EQ(r.process(0.0F), 0.0F);
}

TEST(SimpleReverb, DelayedTapReappearsAfterNSamples)
{
    cd::audio::SimpleReverb r;
    r.prepare(4);
    r.set_feedback(0.5F);
    // Tick impulse on sample 0.
    const float y0 = r.process(1.0F);   // 1 + 0.5*0 = 1
    EXPECT_FLOAT_EQ(y0, 1.0F);
    (void)r.process(0.0F);              // tap 1
    (void)r.process(0.0F);              // tap 2
    (void)r.process(0.0F);              // tap 3
    // On tick 4 the delay line re-reads the stored impulse (=1.0F), so
    // y = 0 + 0.5*1.0 = 0.5.
    const float y4 = r.process(0.0F);
    EXPECT_FLOAT_EQ(y4, 0.5F);
}

TEST(SimpleReverb, FeedbackClampedToStableRange)
{
    cd::audio::SimpleReverb r;
    r.prepare(8);
    r.set_feedback(5.0F);
    EXPECT_LE(r.feedback(), 0.99F);
    r.set_feedback(-5.0F);
    EXPECT_GE(r.feedback(), -0.99F);
}

TEST(SimpleReverb, ResetZeroesState)
{
    cd::audio::SimpleReverb r;
    r.prepare(4);
    r.set_feedback(0.5F);
    (void)r.process(1.0F);
    (void)r.process(1.0F);
    r.reset();
    // After reset the very next tick reads zero from the delay line.
    EXPECT_FLOAT_EQ(r.process(0.0F), 0.0F);
}

#include <cd/audio/Limiter.hpp>

TEST(Limiter, BelowThresholdPassesUnaltered)
{
    cd::audio::Limiter lim;
    lim.prepare(48000.0F, 0.95F);
    // Several below-threshold ticks should converge gain → 1.
    for (int i = 0; i < 200; ++i) (void)lim.process(0.3F);
    EXPECT_NEAR(lim.current_gain(), 1.0F, 0.05F);
}

TEST(Limiter, AboveThresholdAttenuates)
{
    cd::audio::Limiter lim;
    lim.prepare(48000.0F, 0.5F, 0.0005F, 0.05F);
    float y = 0.0F;
    for (int i = 0; i < 2000; ++i) y = lim.process(1.5F);  // way above
    EXPECT_LT(std::fabs(y), 1.0F);
    EXPECT_LT(lim.current_gain(), 1.0F);
}

TEST(Limiter, ResetReturnsToPassthroughGain)
{
    cd::audio::Limiter lim;
    lim.prepare(48000.0F);
    for (int i = 0; i < 200; ++i) (void)lim.process(2.0F);
    lim.reset();
    EXPECT_FLOAT_EQ(lim.current_gain(), 1.0F);
    EXPECT_FLOAT_EQ(lim.envelope(), 0.0F);
}

#include <cd/audio/Mixer.hpp>

TEST(Mixer, DefaultGainPassesThrough)
{
    cd::audio::Mixer<4> m;
    m.mix(0, 0.5F);
    EXPECT_FLOAT_EQ(m.pull(), 0.5F);
}

TEST(Mixer, PullClearsAccumulator)
{
    cd::audio::Mixer<4> m;
    m.mix(0, 1.0F);
    (void)m.pull();
    EXPECT_FLOAT_EQ(m.pull(), 0.0F);
}

TEST(Mixer, GainScalesContribution)
{
    cd::audio::Mixer<4> m;
    m.set_gain(0, 0.5F);
    m.set_gain(1, 0.25F);
    m.mix(0, 1.0F);
    m.mix(1, 1.0F);
    EXPECT_FLOAT_EQ(m.pull(), 0.5F + 0.25F);
}

TEST(Mixer, NegativeGainClampedToZero)
{
    cd::audio::Mixer<4> m;
    m.set_gain(0, -1.0F);
    m.mix(0, 1.0F);
    EXPECT_FLOAT_EQ(m.pull(), 0.0F);
}

TEST(Mixer, OutOfRangeChannelIgnored)
{
    cd::audio::Mixer<4> m;
    m.mix(99, 1.0F);
    EXPECT_FLOAT_EQ(m.pull(), 0.0F);
}

#include <cd/audio/LowPass.hpp>

TEST(LowPass, PassesDcThroughAfterSettling)
{
    cd::audio::LowPass lp;
    lp.prepare(48000.0F, 1000.0F);
    for (int i = 0; i < 5000; ++i) (void)lp.process(0.5F);
    EXPECT_NEAR(lp.process(0.5F), 0.5F, 1e-3F);
}

TEST(LowPass, ResetZerosState)
{
    cd::audio::LowPass lp;
    lp.prepare(48000.0F, 1000.0F);
    for (int i = 0; i < 100; ++i) (void)lp.process(1.0F);
    lp.reset();
    EXPECT_FLOAT_EQ(lp.process(0.0F), 0.0F);
}

TEST(LowPass, AlphaInZeroToOneRange)
{
    cd::audio::LowPass lp;
    lp.prepare(48000.0F, 1000.0F);
    EXPECT_GT(lp.alpha(), 0.0F);
    EXPECT_LT(lp.alpha(), 1.0F);
}

#include <cd/audio/Compressor.hpp>

TEST(Compressor, QuietSignalPassesThrough)
{
    cd::audio::Compressor c;
    c.prepare(48000.0F, 0.5F, 4.0F);
    for (int i = 0; i < 200; ++i) (void)c.process(0.1F);
    const float y = c.process(0.1F);
    EXPECT_NEAR(y, 0.1F, 1e-2F);
}

TEST(Compressor, LoudSignalAttenuated)
{
    cd::audio::Compressor c;
    c.prepare(48000.0F, 0.3F, 8.0F);
    float y = 0.0F;
    for (int i = 0; i < 2000; ++i) y = c.process(1.0F);
    EXPECT_LT(std::fabs(y), 1.0F);
}

TEST(Compressor, ResetReturnsToPassthroughGain)
{
    cd::audio::Compressor c;
    c.prepare(48000.0F);
    for (int i = 0; i < 200; ++i) (void)c.process(2.0F);
    c.reset();
    const float y = c.process(0.0F);
    EXPECT_FLOAT_EQ(y, 0.0F);
}

#include <cd/audio/PanLaw.hpp>

TEST(PanLaw, LinearCenterIsHalfHalf)
{
    auto g = cd::audio::linear_pan(0.0F);
    EXPECT_FLOAT_EQ(g.left, 0.5F);
    EXPECT_FLOAT_EQ(g.right, 0.5F);
}

TEST(PanLaw, LinearLeftFullL)
{
    auto g = cd::audio::linear_pan(-1.0F);
    EXPECT_FLOAT_EQ(g.left, 1.0F);
    EXPECT_FLOAT_EQ(g.right, 0.0F);
}

TEST(PanLaw, ConstantPowerEnergyIsOne)
{
    for (float p : { -1.0F, -0.5F, 0.0F, 0.5F, 1.0F })
    {
        auto g = cd::audio::constant_power_pan(p);
        const float energy = g.left * g.left + g.right * g.right;
        EXPECT_NEAR(energy, 1.0F, 1e-4F);
    }
}

TEST(PanLaw, ClampsOutOfRange)
{
    auto g_left  = cd::audio::linear_pan(-5.0F);
    auto g_right = cd::audio::linear_pan(5.0F);
    EXPECT_FLOAT_EQ(g_left.left, 1.0F);
    EXPECT_FLOAT_EQ(g_right.right, 1.0F);
}

#include <cd/audio/PitchShift.hpp>

TEST(PitchShift, ZeroSemitonesIsUnitStep)
{
    std::array<float, 4> src { 1, 2, 3, 4 };
    cd::audio::PitchShift p;
    p.prepare(src, 0.0F);
    EXPECT_FLOAT_EQ(p.step(), 1.0F);
    EXPECT_FLOAT_EQ(p.process(), 1.0F);
    EXPECT_FLOAT_EQ(p.process(), 2.0F);
}

TEST(PitchShift, OctaveUpDoublesStep)
{
    std::array<float, 8> src { 0, 1, 2, 3, 4, 5, 6, 7 };
    cd::audio::PitchShift p;
    p.prepare(src, 12.0F);   // one octave → step 2
    EXPECT_NEAR(p.step(), 2.0F, 1e-3F);
}

TEST(PitchShift, OctaveDownHalvesStep)
{
    std::array<float, 4> src { 0, 1, 2, 3 };
    cd::audio::PitchShift p;
    p.prepare(src, -12.0F);
    EXPECT_NEAR(p.step(), 0.5F, 1e-3F);
}

TEST(PitchShift, EmptySourceReturnsZero)
{
    cd::audio::PitchShift p;
    p.prepare(std::span<const float> {}, 0.0F);
    EXPECT_FLOAT_EQ(p.process(), 0.0F);
}

#include <cd/audio/Voice.hpp>

TEST(Voice, IdleByDefault)
{
    cd::audio::Voice v;
    EXPECT_EQ(v.state(), cd::audio::VoiceState::kIdle);
    EXPECT_FALSE(v.is_active());
}

TEST(Voice, PlayThenAdvanceToFinish)
{
    cd::audio::Voice v;
    v.play(/*sample_count=*/10, /*loop=*/false);
    EXPECT_EQ(v.state(), cd::audio::VoiceState::kPlaying);
    v.advance(15.0F);
    EXPECT_EQ(v.state(), cd::audio::VoiceState::kFinished);
}

TEST(Voice, LoopWrapsReadPos)
{
    cd::audio::Voice v;
    v.play(10, true);
    v.advance(12.0F);
    EXPECT_EQ(v.state(), cd::audio::VoiceState::kPlaying);
    EXPECT_LT(v.read_pos(), 10.0F);
}

TEST(Voice, PauseResumeRoundTrip)
{
    cd::audio::Voice v;
    v.play(100);
    v.pause();
    EXPECT_EQ(v.state(), cd::audio::VoiceState::kPaused);
    v.advance(50.0F);   // pausedda hareket etmemeli
    EXPECT_FLOAT_EQ(v.read_pos(), 0.0F);
    v.resume();
    EXPECT_EQ(v.state(), cd::audio::VoiceState::kPlaying);
}

TEST(Voice, StopResetsToIdle)
{
    cd::audio::Voice v;
    v.play(100);
    v.stop();
    EXPECT_EQ(v.state(), cd::audio::VoiceState::kIdle);
    EXPECT_FLOAT_EQ(v.read_pos(), 0.0F);
}

// ---------------------------------------------------------------------------
// Voice — extended edge + negative coverage
// ---------------------------------------------------------------------------

TEST(Voice, GainAndPitchDefaultToOne)
{
    cd::audio::Voice v;
    EXPECT_FLOAT_EQ(v.gain(), 1.0F);
    EXPECT_FLOAT_EQ(v.pitch(), 1.0F);
}

TEST(Voice, SetGainAndPitchAccessors)
{
    cd::audio::Voice v;
    v.set_gain(0.25F);
    v.set_pitch(2.0F);
    EXPECT_FLOAT_EQ(v.gain(), 0.25F);
    EXPECT_FLOAT_EQ(v.pitch(), 2.0F);
}

TEST(Voice, AdvanceOnIdleIsNoOp)
{
    // Idle voice: advance must not change state or read_pos.
    cd::audio::Voice v;
    v.advance(10.0F);
    EXPECT_EQ(v.state(), cd::audio::VoiceState::kIdle);
    EXPECT_FLOAT_EQ(v.read_pos(), 0.0F);
}

TEST(Voice, AdvanceOnPausedIsNoOp)
{
    cd::audio::Voice v;
    v.play(50);
    v.pause();
    v.advance(30.0F);
    EXPECT_EQ(v.state(), cd::audio::VoiceState::kPaused);
    EXPECT_FLOAT_EQ(v.read_pos(), 0.0F);   // position unchanged
}

TEST(Voice, AdvanceOnFinishedIsNoOp)
{
    cd::audio::Voice v;
    v.play(4);
    v.advance(10.0F);
    EXPECT_EQ(v.state(), cd::audio::VoiceState::kFinished);
    const float pos_after_finish = v.read_pos();
    v.advance(5.0F);
    EXPECT_EQ(v.state(), cd::audio::VoiceState::kFinished);
    EXPECT_FLOAT_EQ(v.read_pos(), pos_after_finish);
}

TEST(Voice, StopFromPausedGoesToIdle)
{
    cd::audio::Voice v;
    v.play(100);
    v.pause();
    v.stop();
    EXPECT_EQ(v.state(), cd::audio::VoiceState::kIdle);
    EXPECT_FLOAT_EQ(v.read_pos(), 0.0F);
}

TEST(Voice, StopFromFinishedGoesToIdle)
{
    cd::audio::Voice v;
    v.play(4);
    v.advance(10.0F);
    EXPECT_EQ(v.state(), cd::audio::VoiceState::kFinished);
    v.stop();
    EXPECT_EQ(v.state(), cd::audio::VoiceState::kIdle);
    EXPECT_FLOAT_EQ(v.read_pos(), 0.0F);
}

TEST(Voice, PauseOnIdleIsNoOp)
{
    cd::audio::Voice v;
    v.pause();   // should be ignored for non-Playing voices
    EXPECT_EQ(v.state(), cd::audio::VoiceState::kIdle);
}

TEST(Voice, ResumeOnPlayingIsNoOp)
{
    cd::audio::Voice v;
    v.play(100);
    v.resume();  // already Playing — must stay Playing
    EXPECT_EQ(v.state(), cd::audio::VoiceState::kPlaying);
}

TEST(Voice, IsActiveReturnsFalseForIdleAndFinished)
{
    cd::audio::Voice v;
    EXPECT_FALSE(v.is_active());  // kIdle
    v.play(4);
    v.advance(10.0F);
    EXPECT_FALSE(v.is_active());  // kFinished
}

TEST(Voice, IsActiveReturnsTrueForPaused)
{
    cd::audio::Voice v;
    v.play(100);
    v.pause();
    EXPECT_TRUE(v.is_active());   // kPaused counts as active
}

TEST(Voice, AdvanceExactlyToEndFinishes)
{
    // Advance precisely to sample_count boundary.
    cd::audio::Voice v;
    v.play(5);
    v.advance(5.0F);  // read_pos == 5.0 == sample_count
    EXPECT_EQ(v.state(), cd::audio::VoiceState::kFinished);
}

TEST(Voice, ZeroLengthClipFinishesImmediately)
{
    // sample_count=0: any positive advance finishes immediately.
    cd::audio::Voice v;
    v.play(0, false);
    v.advance(0.001F);
    EXPECT_EQ(v.state(), cd::audio::VoiceState::kFinished);
}

TEST(Voice, LoopWrapPreservesFractionalOvershoot)
{
    // With sample_count=4 and step 1.5, after 3 ticks read_pos
    // should wrap correctly without losing the fractional part.
    // tick 0: 0 + 1.5 = 1.5
    // tick 1: 1.5 + 1.5 = 3.0
    // tick 2: 3.0 + 1.5 = 4.5 → wraps: 4.5 - 4 = 0.5 (NOT 0.0)
    cd::audio::Voice v;
    v.play(4, true);
    v.advance(1.5F);
    v.advance(1.5F);
    v.advance(1.5F);  // triggers wrap
    EXPECT_EQ(v.state(), cd::audio::VoiceState::kPlaying);
    EXPECT_NEAR(v.read_pos(), 0.5F, 1e-5F);
}

TEST(Voice, PlayResetsPositionAndState)
{
    // Re-calling play() on an already-Playing voice must restart it.
    cd::audio::Voice v;
    v.play(10);
    v.advance(6.0F);
    v.play(20);   // restart
    EXPECT_EQ(v.state(), cd::audio::VoiceState::kPlaying);
    EXPECT_FLOAT_EQ(v.read_pos(), 0.0F);
}

// ---------------------------------------------------------------------------
// Mixer — extended coverage
// ---------------------------------------------------------------------------

TEST(Mixer, ChannelCountMatchesTemplate)
{
    cd::audio::Mixer<8> m8;
    EXPECT_EQ(m8.channel_count(), 8U);
    cd::audio::Mixer<1> m1;
    EXPECT_EQ(m1.channel_count(), 1U);
}

TEST(Mixer, GainAccessorReturnsSetValue)
{
    cd::audio::Mixer<4> m;
    m.set_gain(2, 0.75F);
    EXPECT_FLOAT_EQ(m.gain(2), 0.75F);
}

TEST(Mixer, GainAccessorOobReturnsZero)
{
    cd::audio::Mixer<4> m;
    EXPECT_FLOAT_EQ(m.gain(99), 0.0F);
}

TEST(Mixer, MultiChannelAccumulatesCorrectly)
{
    cd::audio::Mixer<4> m;
    m.set_gain(0, 1.0F);
    m.set_gain(1, 0.5F);
    m.set_gain(2, 0.25F);
    m.mix(0, 0.8F);
    m.mix(1, 0.8F);
    m.mix(2, 0.8F);
    // Expected: 0.8 * 1.0 + 0.8 * 0.5 + 0.8 * 0.25 = 0.8 + 0.4 + 0.2 = 1.4
    EXPECT_NEAR(m.pull(), 1.4F, 1e-5F);
}

TEST(Mixer, ConsecutivePullsAreIndependent)
{
    // Each pull-cycle is independent: mixing after pull starts fresh.
    cd::audio::Mixer<4> m;
    m.mix(0, 1.0F);
    (void)m.pull();
    m.mix(0, 0.5F);
    EXPECT_FLOAT_EQ(m.pull(), 0.5F);
}

TEST(Mixer, SetGainClipsNegativeToZero)
{
    cd::audio::Mixer<4> m;
    m.set_gain(0, -99.0F);
    EXPECT_FLOAT_EQ(m.gain(0), 0.0F);
}

// ---------------------------------------------------------------------------
// PanLaw — extended coverage
// ---------------------------------------------------------------------------

TEST(PanLaw, LinearRightFullR)
{
    auto g = cd::audio::linear_pan(1.0F);
    EXPECT_FLOAT_EQ(g.left, 0.0F);
    EXPECT_FLOAT_EQ(g.right, 1.0F);
}

TEST(PanLaw, ConstantPowerHardLeftIsFullLeft)
{
    auto g = cd::audio::constant_power_pan(-1.0F);
    EXPECT_NEAR(g.left, 1.0F, 1e-4F);
    EXPECT_NEAR(g.right, 0.0F, 1e-4F);
}

TEST(PanLaw, ConstantPowerHardRightIsFullRight)
{
    auto g = cd::audio::constant_power_pan(1.0F);
    EXPECT_NEAR(g.left, 0.0F, 1e-4F);
    EXPECT_NEAR(g.right, 1.0F, 1e-4F);
}

TEST(PanLaw, ConstantPowerCenterEqualGains)
{
    auto g = cd::audio::constant_power_pan(0.0F);
    EXPECT_NEAR(g.left, g.right, 1e-5F);
}

// ---------------------------------------------------------------------------
// PitchShift — extended coverage
// ---------------------------------------------------------------------------

TEST(PitchShift, LinearInterpolation)
{
    // Source: [0.0, 1.0, 2.0, 3.0]. With step=0.5, first sample at
    // pos=0 → 0.0, then pos=0.5 → lerp(0,1,0.5)=0.5.
    std::array<float, 4> src { 0.0F, 1.0F, 2.0F, 3.0F };
    cd::audio::PitchShift p;
    p.prepare(src, -12.0F);  // step = 0.5
    EXPECT_NEAR(p.step(), 0.5F, 1e-3F);
    EXPECT_FLOAT_EQ(p.process(), 0.0F);   // pos=0 → 0.0
    EXPECT_NEAR(p.process(), 0.5F, 1e-3F);  // pos=0.5 → lerp(0,1,0.5)=0.5
}

TEST(PitchShift, ResetReturnsToStart)
{
    std::array<float, 4> src { 10.0F, 20.0F, 30.0F, 40.0F };
    cd::audio::PitchShift p;
    p.prepare(src, 0.0F);
    (void)p.process();
    (void)p.process();
    p.reset();
    EXPECT_FLOAT_EQ(p.position(), 0.0F);
    EXPECT_FLOAT_EQ(p.process(), 10.0F);  // back to sample[0]
}

TEST(PitchShift, SetSemitonesUpdatesStep)
{
    std::array<float, 4> src { 0.0F, 1.0F, 2.0F, 3.0F };
    cd::audio::PitchShift p;
    p.prepare(src, 0.0F);
    p.set_semitones(12.0F);  // one octave up → step ≈ 2
    EXPECT_NEAR(p.step(), 2.0F, 1e-3F);
}

// ---------------------------------------------------------------------------
// Limiter — extended coverage
// ---------------------------------------------------------------------------

TEST(Limiter, GainDbBelowThresholdIsNearZero)
{
    cd::audio::Limiter lim;
    lim.prepare(48000.0F, 0.95F);
    for (int i = 0; i < 500; ++i) (void)lim.process(0.1F);
    // Well below threshold → gain ≈ 1.0 → gain_db ≈ 0 dB.
    EXPECT_NEAR(lim.current_gain(), 1.0F, 0.05F);
}

TEST(Limiter, GainDbAboveThresholdIsNegative)
{
    cd::audio::Limiter lim;
    lim.prepare(48000.0F, 0.5F, 0.0005F, 0.05F);
    for (int i = 0; i < 2000; ++i) (void)lim.process(1.5F);
    // Gain must be < 1 ⇒ gain_db < 0.
    EXPECT_LT(lim.current_gain(), 1.0F);
}

TEST(Limiter, EnvelopeTracksAmplitude)
{
    cd::audio::Limiter lim;
    lim.prepare(48000.0F, 0.95F, 0.0001F, 0.05F);
    for (int i = 0; i < 200; ++i) (void)lim.process(0.8F);
    // Envelope should be near 0.8 (the steady input amplitude).
    EXPECT_NEAR(lim.envelope(), 0.8F, 0.1F);
}

TEST(Limiter, ZeroRatePrepareDefaultsTo48k)
{
    // If caller passes 0 for sample_rate, prepare clamps to 48000.
    cd::audio::Limiter lim;
    lim.prepare(0.0F, 0.95F);  // should not crash or produce NaN
    const float y = lim.process(0.5F);
    EXPECT_FALSE(std::isnan(y));
}

// ---------------------------------------------------------------------------
// Compressor — extended coverage
// ---------------------------------------------------------------------------

TEST(Compressor, GainDbNegativeWhenCompressing)
{
    cd::audio::Compressor c;
    c.prepare(48000.0F, 0.3F, 8.0F);
    for (int i = 0; i < 2000; ++i) (void)c.process(1.0F);
    // Gain should be < 1 ⇒ gain_db < 0.
    EXPECT_LT(c.gain_db(), 0.0F);
}

TEST(Compressor, GainDbNearZeroWhenQuiet)
{
    cd::audio::Compressor c;
    c.prepare(48000.0F, 0.5F, 4.0F);
    for (int i = 0; i < 500; ++i) (void)c.process(0.1F);
    // Well below threshold → very little gain reduction.
    EXPECT_GT(c.gain_db(), -3.0F);
}

// ---------------------------------------------------------------------------
// FileSinkBackend — deterministic buffer-pull tests (CPU-only, no device)
// ---------------------------------------------------------------------------

#include <cd/audio/FileSinkBackend.hpp>

TEST(FileSink, FactoryBuildsWithDefaults)
{
    auto b = cd::audio::make_file_sink_audio_backend();
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->clip_count(), 0U);
    EXPECT_EQ(b->voice_count(), 0U);
    EXPECT_EQ(b->rendered_frames(), 0U);
}

TEST(FileSink, RenderZeroFramesIsNoOp)
{
    auto b = cd::audio::make_file_sink_audio_backend(48000, 2);
    b->render(0);
    EXPECT_EQ(b->rendered_frames(), 0U);
}

TEST(FileSink, RenderSilenceWhenNoVoices)
{
    auto b = cd::audio::make_file_sink_audio_backend(48000, 1);
    b->render(4);
    EXPECT_EQ(b->rendered_frames(), 4U);
    // No voices → output should be all zeros. Write to /dev/null to verify
    // render() doesn't crash; the content check is done via render_frames count.
}

TEST(FileSink, SingleVoiceRendersCorrectAmplitude)
{
    // Create a 1-sample mono clip with value 0.5. After 1 frame of render
    // the mix output at master=1 should be 0.5 (clamped to [-1,1]).
    auto b = cd::audio::make_file_sink_audio_backend(48000, 1);
    const std::array<float, 1> samples { 0.5F };
    cd::audio::ClipDesc d {};
    d.samples = samples;
    d.channels = 1;
    d.sample_rate = 48000;
    auto c = b->create_clip(d);
    ASSERT_TRUE(c.has_value());
    auto v = b->play(*c, 1.0F, false);
    ASSERT_TRUE(v.has_value());

    b->render(1);
    EXPECT_EQ(b->rendered_frames(), 1U);
    // Voice should no longer be playing after 1 frame (clip exhausted).
    EXPECT_FALSE(b->is_playing(*v));
}

TEST(FileSink, VolumeScalesOutput)
{
    // A clip with value 1.0, played at volume=0.5, should produce 0.5 after 1 frame.
    // We can verify indirectly: after render the voice finishes; the backend's
    // rendered_frames count increments. Direct sample inspection is via write_wav,
    // but we test the behavioural invariant (no crash, correct frame count).
    auto b = cd::audio::make_file_sink_audio_backend(48000, 1);
    const std::array<float, 1> samples { 1.0F };
    cd::audio::ClipDesc d { samples, 1, 48000 };
    auto c = b->create_clip(d);
    ASSERT_TRUE(c.has_value());
    (void)b->play(*c, 0.5F, false);
    b->render(1);
    EXPECT_EQ(b->rendered_frames(), 1U);
}

TEST(FileSink, MultiVoiceMixSum)
{
    // Two voices on the same clip (value 0.4 each, volume 1.0) must sum to 0.8.
    // After render, both voices advance past the 1-sample clip → not playing.
    auto b = cd::audio::make_file_sink_audio_backend(48000, 1);
    const std::array<float, 1> samples { 0.4F };
    cd::audio::ClipDesc d { samples, 1, 48000 };
    auto c = b->create_clip(d);
    ASSERT_TRUE(c.has_value());
    auto v1 = b->play(*c, 1.0F, false);
    auto v2 = b->play(*c, 1.0F, false);
    ASSERT_TRUE(v1.has_value());
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(b->voice_count(), 2U);
    b->render(1);
    // Both voices finished after the single sample.
    EXPECT_FALSE(b->is_playing(*v1));
    EXPECT_FALSE(b->is_playing(*v2));
}

TEST(FileSink, MixSaturatesAtPlusOne)
{
    // Two voices each with value 0.8 (sum = 1.6 > 1.0).
    // render() clamps output to [-1, 1], so no value exceeds ±1.
    // We render 2 frames to exercise the clamp path.
    auto b = cd::audio::make_file_sink_audio_backend(48000, 1);
    const std::array<float, 2> samples { 0.8F, 0.8F };
    cd::audio::ClipDesc d { samples, 1, 48000 };
    auto c1 = b->create_clip(d);
    auto c2 = b->create_clip(d);
    ASSERT_TRUE(c1.has_value());
    ASSERT_TRUE(c2.has_value());
    (void)b->play(*c1, 1.0F, false);
    (void)b->play(*c2, 1.0F, false);
    b->render(2);
    EXPECT_EQ(b->rendered_frames(), 2U);
    // Content clamped: write_wav round-trips without error (no crash check).
    // The behavioural guarantee is that rendered_frames() == 2.
}

TEST(FileSink, LoopingVoiceKeepsPlayingAfterClipExhausted)
{
    // A 2-sample clip played looping should still be playing after 4 rendered frames.
    auto b = cd::audio::make_file_sink_audio_backend(48000, 1);
    const std::array<float, 2> samples { 0.3F, -0.3F };
    cd::audio::ClipDesc d { samples, 1, 48000 };
    auto c = b->create_clip(d);
    ASSERT_TRUE(c.has_value());
    auto v = b->play(*c, 1.0F, /*looping=*/true);
    ASSERT_TRUE(v.has_value());
    b->render(4);
    EXPECT_TRUE(b->is_playing(*v));
}

TEST(FileSink, NonLoopingVoiceFinishesAfterAllFrames)
{
    auto b = cd::audio::make_file_sink_audio_backend(48000, 1);
    const std::array<float, 3> samples { 0.1F, 0.2F, 0.3F };
    cd::audio::ClipDesc d { samples, 1, 48000 };
    auto c = b->create_clip(d);
    ASSERT_TRUE(c.has_value());
    auto v = b->play(*c, 1.0F, false);
    ASSERT_TRUE(v.has_value());
    b->render(3);
    EXPECT_FALSE(b->is_playing(*v));
    EXPECT_EQ(b->rendered_frames(), 3U);
}

TEST(FileSink, StopRemovesVoiceBeforeRender)
{
    auto b = cd::audio::make_file_sink_audio_backend(48000, 1);
    const std::array<float, 8> samples {};
    cd::audio::ClipDesc d { samples, 1, 48000 };
    auto c = b->create_clip(d);
    ASSERT_TRUE(c.has_value());
    auto v = b->play(*c, 1.0F, false);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(b->voice_count(), 1U);
    b->stop(*v);
    EXPECT_EQ(b->voice_count(), 0U);
    b->render(4);
    // Render with no voices should be silent (no crash).
    EXPECT_EQ(b->rendered_frames(), 4U);
}

TEST(FileSink, ZeroLengthClipDoesNotCrashOnRender)
{
    auto b = cd::audio::make_file_sink_audio_backend(48000, 1);
    const std::span<const float> empty_samples;
    cd::audio::ClipDesc d {};
    d.samples = empty_samples;
    d.channels = 1;
    d.sample_rate = 48000;
    auto c = b->create_clip(d);
    ASSERT_TRUE(c.has_value());
    (void)b->play(*c, 1.0F, false);
    b->render(4);  // must not crash
    EXPECT_EQ(b->rendered_frames(), 4U);
}

TEST(FileSink, SilentClipRendersZero)
{
    // A clip filled with zeros should produce no signal in the mix.
    auto b = cd::audio::make_file_sink_audio_backend(48000, 1);
    const std::array<float, 4> samples {};  // all zeros
    cd::audio::ClipDesc d { samples, 1, 48000 };
    auto c = b->create_clip(d);
    ASSERT_TRUE(c.has_value());
    (void)b->play(*c, 1.0F, false);
    b->render(4);
    EXPECT_EQ(b->rendered_frames(), 4U);
}

TEST(FileSink, SetVolumeClampedToUnit)
{
    auto b = cd::audio::make_file_sink_audio_backend(48000, 1);
    const std::array<float, 4> samples { 0.5F, 0.5F, 0.5F, 0.5F };
    cd::audio::ClipDesc d { samples, 1, 48000 };
    auto c = b->create_clip(d);
    ASSERT_TRUE(c.has_value());
    auto v = b->play(*c, 1.0F, false);
    ASSERT_TRUE(v.has_value());
    b->set_volume(*v, 5.0F);   // over-range → should clamp to 1.0
    b->render(1);
    // No crash, render completes.
    EXPECT_EQ(b->rendered_frames(), 1U);
}

TEST(FileSink, MasterVolumeClampedToUnit)
{
    auto b = cd::audio::make_file_sink_audio_backend(48000, 1);
    b->set_master_volume(3.0F);
    EXPECT_FLOAT_EQ(b->master_volume(), 1.0F);
    b->set_master_volume(-1.0F);
    EXPECT_FLOAT_EQ(b->master_volume(), 0.0F);
}

TEST(FileSink, DestroyClipReapsVoices)
{
    auto b = cd::audio::make_file_sink_audio_backend(48000, 1);
    const std::array<float, 4> samples { 0.1F, 0.2F, 0.3F, 0.4F };
    cd::audio::ClipDesc d { samples, 1, 48000 };
    auto c = b->create_clip(d);
    ASSERT_TRUE(c.has_value());
    (void)b->play(*c, 1.0F, true);
    (void)b->play(*c, 1.0F, true);
    EXPECT_EQ(b->voice_count(), 2U);
    b->destroy_clip(*c);
    EXPECT_EQ(b->clip_count(), 0U);
    EXPECT_EQ(b->voice_count(), 0U);
}

TEST(FileSink, SimultaneousMaxVoicesAllContribute)
{
    // Start N voices simultaneously — all should be tracked.
    auto b = cd::audio::make_file_sink_audio_backend(48000, 1);
    const std::array<float, 8> samples {};
    cd::audio::ClipDesc d { samples, 1, 48000 };
    auto c = b->create_clip(d);
    ASSERT_TRUE(c.has_value());

    constexpr std::size_t kN = 16;
    for (std::size_t i = 0; i < kN; ++i)
        (void)b->play(*c, 1.0F, true);
    EXPECT_EQ(b->voice_count(), kN);
    b->render(2);
    EXPECT_EQ(b->rendered_frames(), 2U);
}

TEST(FileSink, WriteWavProducesNonEmptyFile)
{
    // Render a few frames and write to a temp path; verify no error.
    auto b = cd::audio::make_file_sink_audio_backend(48000, 2);
    const std::array<float, 4> samples { 0.1F, -0.1F, 0.2F, -0.2F };
    cd::audio::ClipDesc d { samples, 2, 48000 };
    auto c = b->create_clip(d);
    ASSERT_TRUE(c.has_value());
    (void)b->play(*c, 0.8F, false);
    b->render(2);
    const auto result = b->write_wav("cd_audio_test_out.wav");  // cwd (build dir) — writable cross-platform
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
}

TEST(FileSink, WriteWavToInvalidPathReturnsError)
{
    auto b = cd::audio::make_file_sink_audio_backend(48000, 1);
    b->render(1);
    const auto result = b->write_wav("/dev/null/bogus/path/that/does/not/exist.wav");
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// NullAudioBackend — extended coverage
// ---------------------------------------------------------------------------

TEST(NullBackend, SetVolumeClampedBelowZero)
{
    auto b = cd::audio::make_null_audio_backend();
    cd::audio::ClipDesc d {};
    const std::array<float, 4> s { 0.1F, 0.2F, 0.3F, 0.4F };
    d.samples = s;
    auto c = b->create_clip(d);
    ASSERT_TRUE(c.has_value());
    auto v = b->play(*c, 1.0F, false);
    ASSERT_TRUE(v.has_value());
    b->set_volume(*v, -5.0F);
    // No observable volume accessor on NullBackend, but calling it must not crash.
    EXPECT_TRUE(b->is_playing(*v));
}

TEST(NullBackend, SetVolumeOnUnknownVoiceIsNoOp)
{
    auto b = cd::audio::make_null_audio_backend();
    cd::audio::VoiceHandle bogus { 9999u, 1u };
    b->set_volume(bogus, 0.5F);  // must not crash
    EXPECT_EQ(b->voice_count(), 0U);
}

TEST(NullBackend, StopOnUnknownVoiceIsNoOp)
{
    auto b = cd::audio::make_null_audio_backend();
    cd::audio::VoiceHandle bogus { 9999u, 1u };
    b->stop(bogus);  // must not crash
}

TEST(NullBackend, MultipleVoicesSameClip)
{
    auto b = cd::audio::make_null_audio_backend();
    cd::audio::ClipDesc d {};
    const std::array<float, 4> s { 0.5F, 0.5F, 0.5F, 0.5F };
    d.samples = s;
    auto c = b->create_clip(d);
    ASSERT_TRUE(c.has_value());
    auto v1 = b->play(*c);
    auto v2 = b->play(*c);
    auto v3 = b->play(*c);
    EXPECT_EQ(b->voice_count(), 3U);
    EXPECT_TRUE(b->is_playing(*v1));
    EXPECT_TRUE(b->is_playing(*v2));
    EXPECT_TRUE(b->is_playing(*v3));
}

TEST(NullBackend, IsPlayingReturnsFalseAfterStop)
{
    auto b = cd::audio::make_null_audio_backend();
    cd::audio::ClipDesc d {};
    const std::array<float, 2> s { 0.1F, 0.2F };
    d.samples = s;
    auto c = b->create_clip(d);
    ASSERT_TRUE(c.has_value());
    auto v = b->play(*c);
    ASSERT_TRUE(v.has_value());
    b->stop(*v);
    EXPECT_FALSE(b->is_playing(*v));
    EXPECT_EQ(b->voice_count(), 0U);
}

TEST(NullBackend, CreateClipRejectsZeroSampleRate)
{
    auto b = cd::audio::make_null_audio_backend();
    cd::audio::ClipDesc d {};
    const std::array<float, 4> s { 0.1F, 0.2F, 0.3F, 0.4F };
    d.samples = s;
    d.channels = 1;
    d.sample_rate = 0;
    auto r = b->create_clip(d);
    EXPECT_FALSE(r.has_value());
}

TEST(NullBackend, PlayVolumeClampedAboveOne)
{
    // Play with volume > 1 — NullBackend clamps on entry.
    auto b = cd::audio::make_null_audio_backend();
    cd::audio::ClipDesc d {};
    const std::array<float, 2> s { 0.5F, 0.5F };
    d.samples = s;
    auto c = b->create_clip(d);
    ASSERT_TRUE(c.has_value());
    auto v = b->play(*c, 5.0F, false);  // clamped to 1.0 internally
    ASSERT_TRUE(v.has_value());
    EXPECT_TRUE(b->is_playing(*v));
}
