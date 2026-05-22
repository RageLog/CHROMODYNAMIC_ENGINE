// =============================================================================
// CHROMODYNAMIC — cd::audio tests (null backend + native dispatcher)
// =============================================================================
#include <cd/audio/AlsaBackend.hpp>
#include <cd/audio/CoreAudioBackend.hpp>
#include <cd/audio/IAudioBackend.hpp>
#include <cd/audio/NativeBackend.hpp>
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

TEST(NativeBackend, CoreAudioStubReturnsNullEverywhere)
{
    // The CoreAudio backend is a documented stub — the symbol must
    // exist and return nullptr until the AudioUnit implementation lands.
    auto core = cd::audio::make_coreaudio_backend();
    EXPECT_EQ(core, nullptr);
}

TEST(NativeBackend, AlsaStubReturnsNullEverywhere)
{
    auto alsa = cd::audio::make_alsa_backend();
    EXPECT_EQ(alsa, nullptr);
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

}  // namespace
