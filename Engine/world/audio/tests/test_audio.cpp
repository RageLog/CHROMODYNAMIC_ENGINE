// =============================================================================
// CHROMODYNAMIC — cd::audio tests (null backend)
// =============================================================================
#include <cd/audio/IAudioBackend.hpp>
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

}  // namespace
