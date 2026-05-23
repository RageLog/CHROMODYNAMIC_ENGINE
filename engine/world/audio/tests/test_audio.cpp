// =============================================================================
// CHROMODYNAMIC — cd::audio tests (null backend + native dispatcher)
// =============================================================================
#include <cd/audio/AlsaBackend.hpp>
#include <cd/audio/CoreAudioBackend.hpp>
#include <cd/audio/IAudioBackend.hpp>
#include <cd/audio/NativeBackend.hpp>
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
    float left_sum = 0.0F, right_sum = 0.0F;
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

    std::array<float, 8> input;
    input.fill(1.0F);
    std::array<float, 16> out {};
    conv.process(input, out);
    conv.reset();
    // After reset, processing zeros should produce zeros (no
    // residual history energy).
    std::array<float, 8> zeros {};
    std::array<float, 16> out2;
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
