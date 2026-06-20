// =============================================================================
// CHROMODYNAMIC — test_asset_wav.cpp
// In-memory WAV decode tests. We synthesize the file bytes here so the
// suite needs no on-disk fixture.
// =============================================================================
#include <cd/asset/wav/Wav.hpp>
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
std::vector<std::byte>
make_minimal_wav(std::uint16_t channels, std::uint32_t sample_rate, std::uint16_t bits, std::uint32_t frames)
{
    const std::uint32_t data_size = frames * channels * (bits / 8u);
    const std::uint32_t fmt_size = 16;
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
    push_u32_le(v, sample_rate * channels * (bits / 8u));                // byte rate
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
    auto r = cd::asset::wav::decode(bytes.data(), bytes.size());
    ASSERT_TRUE(r.has_value()) << "decode failed: " << r.error().message;
    EXPECT_EQ(r->channels, 1u);
    EXPECT_EQ(r->sample_rate, 44100u);
    EXPECT_EQ(r->bits_per_sample, 16u);
    EXPECT_EQ(r->format, cd::asset::wav::SampleFormat::kPcmInt);
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
    auto r = cd::asset::wav::decode(bytes.data(), bytes.size());
    ASSERT_TRUE(r.has_value()) << "decode failed: " << r.error().message;
    EXPECT_EQ(r->channels, 2u);
    EXPECT_EQ(r->sample_rate, 48000u);
    EXPECT_EQ(r->bits_per_sample, 32u);
    EXPECT_EQ(r->format, cd::asset::wav::SampleFormat::kIeeeFloat);
    EXPECT_EQ(r->frame_count(), 256u);
}

TEST(AssetWavTest, BadMagicReturnsMagicMismatch)
{
    auto bytes = make_minimal_wav(1, 44100, 16, 8);
    bytes[0] = std::byte { 'X' };  // corrupt RIFF
    auto r = cd::asset::wav::decode(bytes.data(), bytes.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::wav::wav_errors::Code::kMagicMismatch));
}

TEST(AssetWavTest, TooSmallReturnsCorrupt)
{
    std::vector<std::byte> tiny(20, std::byte { 0 });
    auto r = cd::asset::wav::decode(tiny.data(), tiny.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::wav::wav_errors::Code::kCorrupt));
}

TEST(AssetWavTest, UnsupportedFormatCodeRejected)
{
    auto bytes = make_minimal_wav(1, 44100, 16, 8);
    // Set fmt code to 0xFFFE (WAVE_FORMAT_EXTENSIBLE) — out of v1 support.
    bytes[20] = std::byte { 0xFE };
    bytes[21] = std::byte { 0xFF };
    auto r = cd::asset::wav::decode(bytes.data(), bytes.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::wav::wav_errors::Code::kUnsupportedFormat));
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

    auto r = cd::asset::wav::decode(v.data(), v.size());
    ASSERT_TRUE(r.has_value()) << "decode failed: " << r.error().message;
    EXPECT_EQ(r->frame_count(), 2u);
}

TEST(AssetWavTest, MissingFileReturnsFileNotFound)
{
    auto r = cd::asset::wav::load("nonexistent_does_not_exist_123.wav");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::wav::wav_errors::Code::kFileNotFound));
}

#include <cd/asset/wav/AssetLoader.hpp>

TEST(WavAssetLoader, AdapterDecodesValidWav)
{
    auto bytes = make_minimal_wav(2, 22050, 16, 64);
    cd::asset::wav::WavAssetLoader loader;
    EXPECT_EQ(loader.tag(), "wav");
    auto r = loader.decode(std::span<const std::byte> { bytes.data(), bytes.size() }, "test.wav");
    ASSERT_TRUE(r.has_value()) << r.error().message;
    auto* wav_asset = dynamic_cast<cd::asset::wav::WavAsset*>(r->get());
    ASSERT_NE(wav_asset, nullptr);
    EXPECT_EQ(wav_asset->wav().channels, 2u);
    EXPECT_EQ(wav_asset->wav().sample_rate, 22050u);
    EXPECT_EQ(wav_asset->wav().frame_count(), 64u);
    EXPECT_EQ(wav_asset->tag(), "wav");
}

TEST(WavAssetLoader, AdapterPropagatesDecodeErrors)
{
    std::vector<std::byte> tiny(20, std::byte { 0 });
    cd::asset::wav::WavAssetLoader loader;
    auto r = loader.decode(std::span<const std::byte> { tiny.data(), tiny.size() }, "bad.wav");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::wav::wav_errors::Code::kCorrupt));
}

// =============================================================================
// Robustness / edge / negative coverage (≥80→100 marathon, ADD-ONLY).
// Defensive deserialization: corrupt input must yield a typed error, never UB.
// =============================================================================

namespace
{
using Code = cd::asset::wav::wav_errors::Code;

void patch_u32_le(std::vector<std::byte>& v, std::size_t off, std::uint32_t x)
{
    v[off + 0] = std::byte { static_cast<unsigned char>(x & 0xFFu) };
    v[off + 1] = std::byte { static_cast<unsigned char>((x >> 8u) & 0xFFu) };
    v[off + 2] = std::byte { static_cast<unsigned char>((x >> 16u) & 0xFFu) };
    v[off + 3] = std::byte { static_cast<unsigned char>((x >> 24u) & 0xFFu) };
}
}  // namespace

TEST(AssetWavEdge, MissingWaveFourccReturnsMagicMismatch)
{
    auto bytes = make_minimal_wav(1, 44100, 16, 8);
    // Corrupt the "WAVE" fourcc at offset 8.
    bytes[8] = std::byte { 'X' };
    auto r = cd::asset::wav::decode(bytes.data(), bytes.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kMagicMismatch));
}

TEST(AssetWavEdge, RiffSizeExceedingBufferReturnsCorrupt)
{
    auto bytes = make_minimal_wav(1, 44100, 16, 8);
    // riff_size lives at offset 4. Inflate it well past the buffer length.
    patch_u32_le(bytes, 4, 0xFFFFFF00u);
    auto r = cd::asset::wav::decode(bytes.data(), bytes.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(AssetWavEdge, FmtChunkSizeUnderSixteenReturnsCorrupt)
{
    auto bytes = make_minimal_wav(1, 44100, 16, 8);
    // "fmt " header is at offset 12; its size field at offset 16. Setting it
    // below 16 means the parser would read out of bounds — it must reject.
    // Shrink fmt size to 8; the chunk-bounds check fires first (payload of 8
    // < 16), so this exercises the chunk-extends OR fmt<16 path.
    patch_u32_le(bytes, 16, 8u);
    auto r = cd::asset::wav::decode(bytes.data(), bytes.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(AssetWavEdge, ZeroChannelsReturnsCorrupt)
{
    auto bytes = make_minimal_wav(1, 44100, 16, 8);
    // channels field is the 2nd u16 of fmt payload: offset 12(hdr)+8(payload start)
    // +2 = 22.
    bytes[22] = std::byte { 0 };
    bytes[23] = std::byte { 0 };
    auto r = cd::asset::wav::decode(bytes.data(), bytes.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(AssetWavEdge, ZeroSampleRateReturnsCorrupt)
{
    auto bytes = make_minimal_wav(1, 44100, 16, 8);
    // sample_rate is at fmt payload +4 = offset 24.
    patch_u32_le(bytes, 24, 0u);
    auto r = cd::asset::wav::decode(bytes.data(), bytes.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(AssetWavEdge, NonMultipleOfEightBitsReturnsCorrupt)
{
    auto bytes = make_minimal_wav(1, 44100, 16, 8);
    // bits_per_sample is fmt payload +14 = offset 34. 12 bits is not a
    // multiple of 8 → malformed fmt fields.
    bytes[34] = std::byte { 12 };
    bytes[35] = std::byte { 0 };
    auto r = cd::asset::wav::decode(bytes.data(), bytes.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(AssetWavEdge, DataChunkSizeExceedingBufferReturnsCorrupt)
{
    auto bytes = make_minimal_wav(1, 44100, 16, 8);
    // data chunk header is the last 8-byte header before payload. Its size
    // field sits 4 bytes into that header. Locate "data" then inflate.
    // Layout: 12 (RIFF/WAVE) + 8 (fmt hdr) + 16 (fmt payload) = 36 is the
    // data header start; size field at 36+4 = 40.
    patch_u32_le(bytes, 40, 0xFFFFFFFEu);
    auto r = cd::asset::wav::decode(bytes.data(), bytes.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(AssetWavEdge, NoDataChunkReturnsCorrupt)
{
    // fmt-only WAV (no data chunk). Decode must report kCorrupt "no data".
    std::vector<std::byte> v;
    push_tag(v, "RIFF");
    push_u32_le(v, 0);  // backpatched below
    push_tag(v, "WAVE");
    push_tag(v, "fmt ");
    push_u32_le(v, 16);
    push_u16_le(v, 1);
    push_u16_le(v, 1);
    push_u32_le(v, 44100);
    push_u32_le(v, 44100 * 2);
    push_u16_le(v, 2);
    push_u16_le(v, 16);
    const auto total = static_cast<std::uint32_t>(v.size());
    patch_u32_le(v, 4, total - 8u);
    // Pad to 44 bytes so the size>=44 guard is satisfied and the chunk walk
    // is what actually trips (otherwise kCorrupt comes from the size guard).
    while (v.size() < 44)
        v.push_back(std::byte { 0 });
    patch_u32_le(v, 4, static_cast<std::uint32_t>(v.size()) - 8u);
    auto r = cd::asset::wav::decode(v.data(), v.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(AssetWavEdge, NullDataPointerReturnsCorrupt)
{
    auto r = cd::asset::wav::decode(nullptr, 128);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(AssetWavEdge, EightChannelPcmDecodesFrameCount)
{
    // Multi-channel (7.1) PCM is explicitly accepted per the header contract.
    auto bytes = make_minimal_wav(8, 48000, 16, 100);
    auto r = cd::asset::wav::decode(bytes.data(), bytes.size());
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->channels, 8u);
    EXPECT_EQ(r->frame_count(), 100u);
}
