// =============================================================================
// CHROMODYNAMIC — test_asset_wav.cpp
// In-memory WAV decode tests. We synthesize the file bytes here so the
// suite needs no on-disk fixture.
// =============================================================================
#include <cd/asset_wav/Wav.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace
{

void push_u16_le(std::vector<std::byte>& v, std::uint16_t x)
{
    v.push_back(std::byte { static_cast<unsigned char>(x & 0xFFu) });
    v.push_back(std::byte { static_cast<unsigned char>((x >> 8u) & 0xFFu) });
}
void push_u32_le(std::vector<std::byte>& v, std::uint32_t x)
{
    v.push_back(std::byte { static_cast<unsigned char>(x & 0xFFu) });
    v.push_back(std::byte { static_cast<unsigned char>((x >> 8u) & 0xFFu) });
    v.push_back(std::byte { static_cast<unsigned char>((x >> 16u) & 0xFFu) });
    v.push_back(std::byte { static_cast<unsigned char>((x >> 24u) & 0xFFu) });
}
void push_tag(std::vector<std::byte>& v, const char (&tag)[5])
{
    for (int i = 0; i < 4; ++i)
        v.push_back(static_cast<std::byte>(tag[i]));
}

/// Build a minimal valid mono s16 PCM WAV with `frames` frames of zeros.
std::vector<std::byte> make_minimal_wav(
    std::uint16_t channels, std::uint32_t sample_rate, std::uint16_t bits, std::uint32_t frames)
{
    const std::uint32_t data_size = frames * channels * (bits / 8u);
    const std::uint32_t fmt_size  = 16;
    const std::uint32_t riff_size = 4 /*WAVE*/ + 8 /*fmt hdr*/ + fmt_size + 8 /*data hdr*/ + data_size;

    std::vector<std::byte> v;
    v.reserve(8 + riff_size);
    push_tag(v, "RIFF");
    push_u32_le(v, riff_size);
    push_tag(v, "WAVE");

    push_tag(v, "fmt ");
    push_u32_le(v, fmt_size);
    push_u16_le(v, 1 /* PCM */);
    push_u16_le(v, channels);
    push_u32_le(v, sample_rate);
    push_u32_le(v, sample_rate * channels * (bits / 8u));  // byte rate
    push_u16_le(v, static_cast<std::uint16_t>(channels * (bits / 8u)));  // block align
    push_u16_le(v, bits);

    push_tag(v, "data");
    push_u32_le(v, data_size);
    for (std::uint32_t i = 0; i < data_size; ++i)
        v.push_back(std::byte { 0 });
    return v;
}

}  // namespace

TEST(AssetWavTest, MonoS16At44100Decodes)
{
    auto bytes = make_minimal_wav(1, 44100, 16, 1000);
    auto r = cd::asset_wav::decode(bytes.data(), bytes.size());
    ASSERT_TRUE(r.has_value()) << "decode failed: " << r.error().message;
    EXPECT_EQ(r->channels, 1u);
    EXPECT_EQ(r->sample_rate, 44100u);
    EXPECT_EQ(r->bits_per_sample, 16u);
    EXPECT_EQ(r->format, cd::asset_wav::SampleFormat::kPcmInt);
    EXPECT_EQ(r->samples.size(), 1000u * 1u * 2u);
    EXPECT_EQ(r->frame_count(), 1000u);
    EXPECT_NEAR(r->duration_seconds(), 1000.0 / 44100.0, 1e-9);
}

TEST(AssetWavTest, StereoF32At48000Decodes)
{
    auto bytes = make_minimal_wav(2, 48000, 32, 256);
    // Patch fmt code to 3 (IEEE float). The synth helper emits PCM=1.
    bytes[20] = std::byte { 3 };
    bytes[21] = std::byte { 0 };
    auto r = cd::asset_wav::decode(bytes.data(), bytes.size());
    ASSERT_TRUE(r.has_value()) << "decode failed: " << r.error().message;
    EXPECT_EQ(r->channels, 2u);
    EXPECT_EQ(r->sample_rate, 48000u);
    EXPECT_EQ(r->bits_per_sample, 32u);
    EXPECT_EQ(r->format, cd::asset_wav::SampleFormat::kIeeeFloat);
    EXPECT_EQ(r->frame_count(), 256u);
}

TEST(AssetWavTest, BadMagicReturnsMagicMismatch)
{
    auto bytes = make_minimal_wav(1, 44100, 16, 8);
    bytes[0] = std::byte { 'X' };  // corrupt RIFF
    auto r = cd::asset_wav::decode(bytes.data(), bytes.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::asset_wav::wav_errors::Code::kMagicMismatch));
}

TEST(AssetWavTest, TooSmallReturnsCorrupt)
{
    std::vector<std::byte> tiny(20, std::byte { 0 });
    auto r = cd::asset_wav::decode(tiny.data(), tiny.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::asset_wav::wav_errors::Code::kCorrupt));
}

TEST(AssetWavTest, UnsupportedFormatCodeRejected)
{
    auto bytes = make_minimal_wav(1, 44100, 16, 8);
    // Set fmt code to 0xFFFE (WAVE_FORMAT_EXTENSIBLE) — out of v1 support.
    bytes[20] = std::byte { 0xFE };
    bytes[21] = std::byte { 0xFF };
    auto r = cd::asset_wav::decode(bytes.data(), bytes.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::asset_wav::wav_errors::Code::kUnsupportedFormat));
}

TEST(AssetWavTest, SkipsUnknownChunks)
{
    // Build a WAV with a "JUNK" chunk inserted between fmt and data.
    std::vector<std::byte> v;
    push_tag(v, "RIFF");
    // riff_size placeholder; patch after.
    push_u32_le(v, 0);
    push_tag(v, "WAVE");
    push_tag(v, "fmt ");
    push_u32_le(v, 16);
    push_u16_le(v, 1);
    push_u16_le(v, 1);
    push_u32_le(v, 44100);
    push_u32_le(v, 44100 * 2);
    push_u16_le(v, 2);
    push_u16_le(v, 16);
    // JUNK chunk with 6 bytes of payload.
    push_tag(v, "JUNK");
    push_u32_le(v, 6);
    for (int i = 0; i < 6; ++i)
        v.push_back(std::byte { 0xAA });
    // data chunk
    push_tag(v, "data");
    push_u32_le(v, 4);
    for (int i = 0; i < 4; ++i)
        v.push_back(std::byte { 0 });
    // Now backpatch riff_size = total - 8.
    const auto total = static_cast<std::uint32_t>(v.size());
    const std::uint32_t riff_size = total - 8u;
    v[4] = std::byte { static_cast<unsigned char>(riff_size & 0xFFu) };
    v[5] = std::byte { static_cast<unsigned char>((riff_size >> 8u) & 0xFFu) };
    v[6] = std::byte { static_cast<unsigned char>((riff_size >> 16u) & 0xFFu) };
    v[7] = std::byte { static_cast<unsigned char>((riff_size >> 24u) & 0xFFu) };

    auto r = cd::asset_wav::decode(v.data(), v.size());
    ASSERT_TRUE(r.has_value()) << "decode failed: " << r.error().message;
    EXPECT_EQ(r->frame_count(), 2u);
}

TEST(AssetWavTest, MissingFileReturnsFileNotFound)
{
    auto r = cd::asset_wav::load("nonexistent_does_not_exist_123.wav");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::asset_wav::wav_errors::Code::kFileNotFound));
}

#include <cd/asset_wav/AssetLoader.hpp>

TEST(WavAssetLoader, AdapterDecodesValidWav)
{
    auto bytes = make_minimal_wav(2, 22050, 16, 64);
    cd::asset_wav::WavAssetLoader loader;
    EXPECT_EQ(loader.tag(), "wav");
    auto r = loader.decode(std::span<const std::byte> { bytes.data(), bytes.size() }, "test.wav");
    ASSERT_TRUE(r.has_value()) << r.error().message;
    auto* wav_asset = dynamic_cast<cd::asset_wav::WavAsset*>(r->get());
    ASSERT_NE(wav_asset, nullptr);
    EXPECT_EQ(wav_asset->wav().channels, 2u);
    EXPECT_EQ(wav_asset->wav().sample_rate, 22050u);
    EXPECT_EQ(wav_asset->wav().frame_count(), 64u);
    EXPECT_EQ(wav_asset->tag(), "wav");
}

TEST(WavAssetLoader, AdapterPropagatesDecodeErrors)
{
    std::vector<std::byte> tiny(20, std::byte { 0 });
    cd::asset_wav::WavAssetLoader loader;
    auto r = loader.decode(std::span<const std::byte> { tiny.data(), tiny.size() }, "bad.wav");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::asset_wav::wav_errors::Code::kCorrupt));
}
