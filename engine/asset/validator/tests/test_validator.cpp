// =============================================================================
// CHROMODYNAMIC — engine/asset/validator/tests/test_validator.cpp
// Phase 611 — cd::asset::validator unit tests (Sprint-1)
//
// Tests:
//   T1  empty blob => kError for all three validate_*_blob() entrypoints
//   T2  oversized texture blob (conservative pixel estimate) => kWarning
//   T3  missing / wrong magic header => kError for texture / gltf / audio
//   T4  valid blobs (PNG / GLB / WAV) => no issues
//   T5  configure() applies new policy (tighter pixel limit triggers warning)
//   T6  valid JSON glTF blob => no issues
//   T7  valid OGG audio blob => no issues
//   T8  GLB version != 2 => kWarning
//   T9  RIFF without WAVE tag => kError
//   T10 configure() replaces previous policy (looser limit clears warning)
//   T11 truncated GLB header (glTF magic but < 12 bytes) => kError, not a crash
//   T12 truncated WAV header (RIFF magic but < 12 bytes, no room for WAVE tag)
//       => kError; and a 1-byte unknown-magic blob => kError (no out-of-bounds
//       read on a sub-magic-length blob).
//
// Depth-70 additions (T13–T27):
//   T13 JPEG magic (FF D8) correctly detected  => no issues
//   T14 KTX2 magic (AB 4B 54 58) correctly detected => no issues
//   T15 DDS magic ("DDS ") correctly detected  => no issues
//   T16 HDR magic ("#?") correctly detected    => no issues
//   T17 Truncated magic: blob shorter than required magic bytes (1-byte PNG) => kError
//   T18 JSON glTF with leading whitespace (tab/space/LF/CR) before '{' => no issues
//   T19 JSON glTF: all-whitespace blob => kError (no '{' found)
//   T20 GLB version = 0 => kWarning
//   T21 Texture: conservative_pixels == limit exactly (not >) => no warning
//   T22 Texture: conservative_pixels == limit + 1 => kWarning
//   T23 OGG magic with extra trailing bytes => no issues (prefix match)
//   T24 WAV: 11-byte blob (RIFF present but size < 12) => kError
//   T25 policy() accessor returns default values on fresh Validator
//   T26 policy() accessor reflects values after configure()
//   T27 SEAL: deep schema parse is out-of-scope; magic-sniff is the full contract
//
// Depth-100 additions (T28–T50):
//   T28 required_color_spaces all-3-slots set => kInfo emitted
//   T29 required_color_spaces partial (1 slot) => no kInfo
//   T30 required_color_spaces two-of-three => no kInfo
//   T31 kInfo message contains all three slot names
//   T32 asset_path propagated verbatim (texture / gltf / audio — 3 TEST cases)
//   T33 multi-issue kError + kWarning in one call (wrong magic + oversized)
//   T34 multi-issue kInfo + kError in one call (color-spaces + bad magic)
//   T35 multi-issue kInfo + kWarning in one call (color-spaces + oversized, valid magic)
//   T36 ValidationIssue::message is non-empty (empty-blob error / bad-audio-magic — 2 cases)
//   T37 empty asset_path stored verbatim, no crash, no substitution
//   T38 Severity enum: kInfo, kWarning, kError are all distinct values
//   T39 GLB version = UINT32_MAX => kWarning (extreme boundary)
//   T40 WAV WAVE tag read from exactly offset 8 (not 0+N) => accepted
//   T41 OGG exactly-4-byte blob ("OggS") => no issues
//   T42 OGG 3-byte blob ('O','g','g') => kError (too short for 4-byte magic)
//   T43 max_texture_pixels = 0 => kWarning on any blob with >= 4 bytes
//   T44 required_color_spaces cleared by second configure() => kInfo stops
//   T45 HDR exactly-2-byte blob ("#?") => no issues (minimum valid)
//   T46 HDR 1-byte '#' blob => kError (magic needs 2 bytes)
//   T47 SEAL: max_mesh_vertices round-trips policy; no validate_mesh_blob() in Sprint-1
//   T48 JSON glTF single-brace '{' blob => no issues
//   T49 JSON glTF array-root '[' blob => kError
//   T50 gltf/audio validators never emit kInfo for required_color_spaces
// =============================================================================

#include <cd/asset/validator/Validator.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <ranges>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace
{

using cd::asset::validator::Severity;
using cd::asset::validator::ValidationIssue;
using cd::asset::validator::Validator;
using cd::asset::validator::ValidatorPolicy;

// ---- helpers ----------------------------------------------------------------

bool has_severity(const std::vector<ValidationIssue>& issues,
                  Severity                             sev)
{
    return std::ranges::any_of(issues,
                       [sev](const ValidationIssue& i) { return i.severity == sev; });
}

bool has_error(const std::vector<ValidationIssue>& issues)
{
    return has_severity(issues, Severity::kError);
}

bool has_warning(const std::vector<ValidationIssue>& issues)
{
    return has_severity(issues, Severity::kWarning);
}

// Minimal valid PNG header (8-byte signature).
constexpr std::array<std::uint8_t, 8> kPngSig{
    0x89U, 'P', 'N', 'G', '\r', '\n', 0x1AU, '\n'
};

// Minimal valid JPEG header (declared for completeness; not exercised yet).
[[maybe_unused]] constexpr std::array<std::uint8_t, 4> kJpgSig{ 0xFFU, 0xD8U, 0xFFU, 0xE0U };

// Minimal valid GLB header (12 bytes): magic + version(2) + length(12).
constexpr std::array<std::uint8_t, 12> kGlbHeader{
    0x67U, 0x6CU, 0x54U, 0x46U,  // "glTF"
    0x02U, 0x00U, 0x00U, 0x00U,  // version = 2 (LE)
    0x0CU, 0x00U, 0x00U, 0x00U   // length = 12 (LE)
};

// Minimal valid WAV header: "RIFF" + 4-byte size + "WAVE" (12 bytes).
constexpr std::array<std::uint8_t, 12> kWavHeader{
    'R', 'I', 'F', 'F',
    0x04U, 0x00U, 0x00U, 0x00U,  // chunk size (dummy)
    'W', 'A', 'V', 'E'
};

// Minimal valid OGG page header (4-byte capture pattern).
constexpr std::array<std::uint8_t, 4> kOggSig{ 'O', 'g', 'g', 'S' };

// ---- T1: empty blob => kError for all three entrypoints ---------------------

TEST(Validator, EmptyTextureBlobIsError)
{
    const Validator v;
    const auto      issues = v.validate_texture_blob({}, "empty.png");
    EXPECT_TRUE(has_error(issues)) << "Expected kError for empty texture blob";
}

TEST(Validator, EmptyGltfBlobIsError)
{
    const Validator v;
    const auto      issues = v.validate_gltf_blob({}, "empty.gltf");
    EXPECT_TRUE(has_error(issues)) << "Expected kError for empty glTF blob";
}

TEST(Validator, EmptyAudioBlobIsError)
{
    const Validator v;
    const auto      issues = v.validate_audio_blob({}, "empty.wav");
    EXPECT_TRUE(has_error(issues)) << "Expected kError for empty audio blob";
}

// ---- T2: oversized texture blob (conservative pixel estimate) => kWarning ---

TEST(Validator, OversizedTextureBlobWarning)
{
    // policy: max 4 pixels (tiny limit for test)
    ValidatorPolicy policy;
    policy.max_texture_pixels = 4ULL;

    Validator v;
    v.configure(policy);

    // 4-byte PNG-sig + 100 extra bytes = 104 bytes total.
    // conservative_pixels = 104 / 4 = 26 > 4 => warning.
    std::vector<std::uint8_t> oversized_blob(
        kPngSig.begin(), kPngSig.end());
    oversized_blob.resize(oversized_blob.size() + 100U, 0xFFU);

    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ oversized_blob }, "big.png");

    EXPECT_TRUE(has_warning(issues))
        << "Expected kWarning for oversized texture blob";
    // Must NOT produce an error (format is valid PNG).
    EXPECT_FALSE(has_error(issues))
        << "Must not produce kError for a valid PNG magic";
}

// ---- T3: missing / wrong magic header => kError for texture / gltf / audio -

TEST(Validator, WrongTextureMagicIsError)
{
    const Validator v;
    // Valid-size blob but starts with garbage magic.
    const std::array<std::uint8_t, 8> garbage{
        0x00U, 0x01U, 0x02U, 0x03U,
        0x04U, 0x05U, 0x06U, 0x07U
    };
    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ garbage }, "bad.bin");
    EXPECT_TRUE(has_error(issues))
        << "Expected kError for unrecognised texture magic";
}

TEST(Validator, WrongGltfMagicIsError)
{
    const Validator v;
    // Blob that starts with 'X' — not '{' and not "glTF".
    const std::array<std::uint8_t, 8> garbage{
        'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X'
    };
    const auto issues = v.validate_gltf_blob(
        std::span<const std::uint8_t>{ garbage }, "bad.gltf");
    EXPECT_TRUE(has_error(issues))
        << "Expected kError for unrecognised glTF magic";
}

TEST(Validator, WrongAudioMagicIsError)
{
    const Validator v;
    const std::array<std::uint8_t, 8> garbage{
        0x00U, 0x01U, 0x02U, 0x03U,
        0x04U, 0x05U, 0x06U, 0x07U
    };
    const auto issues = v.validate_audio_blob(
        std::span<const std::uint8_t>{ garbage }, "bad.bin");
    EXPECT_TRUE(has_error(issues))
        << "Expected kError for unrecognised audio magic";
}

// ---- T4: valid blobs (PNG / GLB / WAV) => no issues -------------------------

TEST(Validator, ValidPngBlobNoIssues)
{
    const Validator v;
    const auto      issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ kPngSig }, "albedo.png");
    EXPECT_TRUE(issues.empty())
        << "Expected no issues for minimal valid PNG blob";
}

TEST(Validator, ValidGlbBlobNoIssues)
{
    const Validator v;
    const auto      issues = v.validate_gltf_blob(
        std::span<const std::uint8_t>{ kGlbHeader }, "scene.glb");
    EXPECT_TRUE(issues.empty())
        << "Expected no issues for minimal valid GLB blob";
}

TEST(Validator, ValidWavBlobNoIssues)
{
    const Validator v;
    const auto      issues = v.validate_audio_blob(
        std::span<const std::uint8_t>{ kWavHeader }, "click.wav");
    EXPECT_TRUE(issues.empty())
        << "Expected no issues for minimal valid WAV blob";
}

// ---- T5: configure() applies new policy (tighter pixel limit) ---------------

TEST(Validator, ConfigureAppliesNewPolicy)
{
    Validator v;

    // Default policy: max 16 Mi pixels — PNG sig of 8 bytes is fine.
    {
        const auto issues = v.validate_texture_blob(
            std::span<const std::uint8_t>{ kPngSig }, "test.png");
        EXPECT_FALSE(has_warning(issues))
            << "Default policy must not warn on tiny blob";
    }

    // Tighten to 1 pixel max.
    ValidatorPolicy tight;
    tight.max_texture_pixels = 1ULL;
    v.configure(tight);

    // Now even the 8-byte PNG sig blob (conservative_pixels = 8/4 = 2 > 1)
    // must produce a warning.
    {
        const auto issues = v.validate_texture_blob(
            std::span<const std::uint8_t>{ kPngSig }, "test.png");
        EXPECT_TRUE(has_warning(issues))
            << "Tightened policy must warn on blob with > 1 conservative pixel";
    }
}

// ---- T6: valid JSON glTF blob => no issues ----------------------------------

TEST(Validator, ValidJsonGltfBlobNoIssues)
{
    const Validator v;
    // Minimal JSON glTF: starts with '{'.
    const std::array<std::uint8_t, 15> json_gltf{
        '{', '"', 'a', 's', 's', 'e', 't', '"',
        ':', '{', '}', '}', ' ', ' ', ' '
    };
    const auto issues = v.validate_gltf_blob(
        std::span<const std::uint8_t>{ json_gltf }, "scene.gltf");
    EXPECT_TRUE(issues.empty())
        << "Expected no issues for minimal valid JSON glTF blob";
}

// ---- T7: valid OGG audio blob => no issues ----------------------------------

TEST(Validator, ValidOggBlobNoIssues)
{
    const Validator v;
    const auto      issues = v.validate_audio_blob(
        std::span<const std::uint8_t>{ kOggSig }, "music.ogg");
    EXPECT_TRUE(issues.empty())
        << "Expected no issues for minimal valid OGG blob";
}

// ---- T8: GLB version != 2 => kWarning --------------------------------------

TEST(Validator, GlbVersionNot2IsWarning)
{
    const Validator v;
    // GLB with version = 1 (not 2).
    constexpr std::array<std::uint8_t, 12> kGlbV1{
        0x67U, 0x6CU, 0x54U, 0x46U,  // "glTF"
        0x01U, 0x00U, 0x00U, 0x00U,  // version = 1 (LE)
        0x0CU, 0x00U, 0x00U, 0x00U
    };
    const auto issues = v.validate_gltf_blob(
        std::span<const std::uint8_t>{ kGlbV1 }, "old.glb");
    EXPECT_TRUE(has_warning(issues))
        << "Expected kWarning for GLB version != 2";
    EXPECT_FALSE(has_error(issues))
        << "Must not produce kError for a GLB with wrong version";
}

// ---- T9: RIFF without WAVE tag => kError -----------------------------------

TEST(Validator, RiffWithoutWaveTagIsError)
{
    const Validator v;
    // "RIFF" at offset 0 but "XXXX" at offset 8 (not "WAVE").
    constexpr std::array<std::uint8_t, 12> kRiffNoWave{
        'R', 'I', 'F', 'F',
        0x04U, 0x00U, 0x00U, 0x00U,
        'X', 'X', 'X', 'X'
    };
    const auto issues = v.validate_audio_blob(
        std::span<const std::uint8_t>{ kRiffNoWave }, "bad.wav");
    EXPECT_TRUE(has_error(issues))
        << "Expected kError for RIFF blob without WAVE fourCC";
}

// ---- T10: configure() replaces previous policy (looser limit) ---------------

TEST(Validator, ConfigureReplacesPolicy)
{
    Validator v;

    // Tighten first.
    ValidatorPolicy tight;
    tight.max_texture_pixels = 1ULL;
    v.configure(tight);
    EXPECT_EQ(v.policy().max_texture_pixels, 1ULL);

    // Loosen: back to default-like large limit.
    ValidatorPolicy loose;
    loose.max_texture_pixels = 1024ULL * 1024ULL * 16ULL;
    v.configure(loose);
    EXPECT_EQ(v.policy().max_texture_pixels, 1024ULL * 1024ULL * 16ULL);

    // PNG sig blob must not warn under loose policy.
    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ kPngSig }, "test.png");
    EXPECT_FALSE(has_warning(issues))
        << "Loose policy must not warn on tiny PNG blob";
}

// ---- T11: truncated GLB header (glTF magic but < 12 bytes) => kError ---------

TEST(Validator, TruncatedGlbHeaderIsError)
{
    const Validator v;
    // "glTF" magic present but only 8 bytes total — header is < 12, so the
    // version field at offset 4 is the last word and the declared 12-byte
    // minimum is not met.
    constexpr std::array<std::uint8_t, 8> kGlbTrunc{
        0x67U, 0x6CU, 0x54U, 0x46U,  // "glTF"
        0x02U, 0x00U, 0x00U, 0x00U   // version = 2, then EOF (no length word)
    };
    const auto issues = v.validate_gltf_blob(
        std::span<const std::uint8_t>{ kGlbTrunc }, "trunc.glb");
    EXPECT_TRUE(has_error(issues))
        << "Expected kError for GLB blob with glTF magic but < 12-byte header";
}

// ---- T12: truncated WAV header + 1-byte unknown blob => kError --------------

TEST(Validator, TruncatedWavAndTinyBlobAreErrors)
{
    const Validator v;

    // (a) "RIFF" magic but only 4 bytes — no room for the WAVE tag at offset 8.
    constexpr std::array<std::uint8_t, 4> kRiffOnly{ 'R', 'I', 'F', 'F' };
    const auto wav_issues = v.validate_audio_blob(
        std::span<const std::uint8_t>{ kRiffOnly }, "trunc.wav");
    EXPECT_TRUE(has_error(wav_issues))
        << "Expected kError for RIFF blob too short to carry a WAVE tag";

    // (b) 1-byte blob with no recognised magic — must not read out of bounds and
    //     must still report an unrecognised-format error.
    constexpr std::array<std::uint8_t, 1> kTiny{ 0x00U };
    const auto tiny_issues = v.validate_audio_blob(
        std::span<const std::uint8_t>{ kTiny }, "tiny.bin");
    EXPECT_TRUE(has_error(tiny_issues))
        << "Expected kError for a 1-byte unknown-magic audio blob";

    // (c) Same 1-byte blob through the texture path — unknown magic => kError.
    const auto tex_issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ kTiny }, "tiny.bin");
    EXPECT_TRUE(has_error(tex_issues))
        << "Expected kError for a 1-byte unknown-magic texture blob";
}

// ---- T13: JPEG magic (FF D8) correctly detected => no issues ----------------

TEST(Validator, ValidJpegBlobNoIssues)
{
    const Validator v;
    // Minimal JPEG blob: SOI marker FF D8 + two padding bytes.
    constexpr std::array<std::uint8_t, 4> kJpegBlob{ 0xFFU, 0xD8U, 0xFFU, 0xE0U };
    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ kJpegBlob }, "photo.jpg");
    EXPECT_TRUE(issues.empty())
        << "Expected no issues for a JPEG magic blob";
}

// ---- T14: KTX2 magic (AB KT X + 4th byte 'X') correctly detected -----------
// The KTX2 spec identifier is \xAB KTX 20\xBB\r\n\x1A\n (12 bytes).
// The validator checks only the first 4 bytes: {0xAB, 'K', 'T', 'X'}.

TEST(Validator, ValidKtx2BlobNoIssues)
{
    const Validator v;
    // Provide the full 12-byte KTX2 identifier so the blob is unambiguously KTX2.
    constexpr std::array<std::uint8_t, 12> kKtx2Blob{
        0xABU, 'K',   'T',   'X',
        ' ',   '2',   '0',   0xBBU,
        '\r',  '\n',  0x1AU, '\n'
    };
    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ kKtx2Blob }, "texture.ktx2");
    EXPECT_TRUE(issues.empty())
        << "Expected no issues for a KTX2 magic blob";
}

// ---- T15: DDS magic ("DDS ") correctly detected => no issues ----------------

TEST(Validator, ValidDdsBlobNoIssues)
{
    const Validator v;
    // DDS files start with the four-byte magic "DDS " (0x44 44 53 20).
    constexpr std::array<std::uint8_t, 4> kDdsBlob{ 'D', 'D', 'S', ' ' };
    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ kDdsBlob }, "cubemap.dds");
    EXPECT_TRUE(issues.empty())
        << "Expected no issues for a DDS magic blob";
}

// ---- T16: HDR magic ("#?") correctly detected => no issues ------------------
// Radiance RGBE files begin with "#?RADIANCE\n"; the validator checks
// only the first two bytes "#?" as a lightweight identifier.

TEST(Validator, ValidHdrBlobNoIssues)
{
    const Validator v;
    // Provide the two-byte prefix plus a few extra bytes to keep it realistic.
    constexpr std::array<std::uint8_t, 12> kHdrBlob{
        '#', '?', 'R', 'A', 'D', 'I', 'A', 'N', 'C', 'E', '\n', '\0'
    };
    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ kHdrBlob }, "sky.hdr");
    EXPECT_TRUE(issues.empty())
        << "Expected no issues for an HDR magic blob";
}

// ---- T17: Truncated magic — blob shorter than the required magic bytes -------
// A 1-byte blob whose sole byte happens to be the first byte of PNG magic
// must NOT be accepted as PNG; starts_with_magic returns false when
// blob.size() < magic.size(). The blob must still generate kError.

TEST(Validator, TruncatedPngMagicBlobIsError)
{
    const Validator v;
    // Only the first byte of the PNG 4-byte magic identifier.
    constexpr std::array<std::uint8_t, 1> kOneByte{ 0x89U };
    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ kOneByte }, "partial.png");
    EXPECT_TRUE(has_error(issues))
        << "1-byte blob with PNG first-byte must not match PNG magic — kError expected";
}

// ---- T18: JSON glTF with leading whitespace before '{' => no issues ---------

TEST(Validator, JsonGltfWithLeadingWhitespaceNoIssues)
{
    const Validator v;
    // Leading whitespace: space, tab, CR, LF — then the required '{'.
    constexpr std::array<std::uint8_t, 8> kWsGltf{
        ' ', '\t', '\r', '\n', '{', '"', '}', '}'
    };
    const auto issues = v.validate_gltf_blob(
        std::span<const std::uint8_t>{ kWsGltf }, "spaced.gltf");
    EXPECT_TRUE(issues.empty())
        << "JSON glTF with leading whitespace before '{' must produce no issues";
}

// ---- T19: JSON glTF: all-whitespace blob => kError (no '{' found) -----------

TEST(Validator, AllWhitespaceGltfBlobIsError)
{
    const Validator v;
    constexpr std::array<std::uint8_t, 4> kWsOnly{ ' ', ' ', '\t', '\n' };
    const auto issues = v.validate_gltf_blob(
        std::span<const std::uint8_t>{ kWsOnly }, "ws.gltf");
    EXPECT_TRUE(has_error(issues))
        << "All-whitespace blob without '{' must produce kError";
}

// ---- T20: GLB version = 0 => kWarning (not 2) ------------------------------

TEST(Validator, GlbVersionZeroIsWarning)
{
    const Validator v;
    constexpr std::array<std::uint8_t, 12> kGlbV0{
        0x67U, 0x6CU, 0x54U, 0x46U,  // "glTF"
        0x00U, 0x00U, 0x00U, 0x00U,  // version = 0 (LE)
        0x0CU, 0x00U, 0x00U, 0x00U   // length = 12
    };
    const auto issues = v.validate_gltf_blob(
        std::span<const std::uint8_t>{ kGlbV0 }, "v0.glb");
    EXPECT_TRUE(has_warning(issues))
        << "GLB version=0 must produce kWarning";
    EXPECT_FALSE(has_error(issues))
        << "GLB version=0 must not produce kError (version mismatch is only a warning)";
}

// ---- T21: conservative_pixels == limit exactly (not >) => no warning --------
// conservative_pixels = blob.size() / 4.  policy limit = N.
// The condition is > limit, so equality must NOT trigger a warning.

TEST(Validator, TextureAtExactPixelLimitNoWarning)
{
    Validator v;
    ValidatorPolicy policy;
    policy.max_texture_pixels = 4ULL;  // limit = 4 pixels
    v.configure(policy);

    // Build a blob whose conservative_pixels == 4 exactly: size = 4 * 4 = 16 bytes.
    // Must start with a valid PNG magic to avoid an unrelated kError.
    std::vector<std::uint8_t> blob(kPngSig.begin(), kPngSig.end());
    // Total size = 16 bytes => conservative_pixels = 16/4 = 4 == limit, NOT >.
    blob.resize(16U, 0x00U);

    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ blob }, "exact.png");
    EXPECT_FALSE(has_warning(issues))
        << "conservative_pixels == limit must NOT trigger a warning (condition is >)";
    EXPECT_FALSE(has_error(issues))
        << "Valid PNG magic at exact pixel limit must not produce kError";
}

// ---- T22: conservative_pixels == limit + 1 => kWarning ---------------------
// blob.size() = (limit + 1) * 4 => conservative_pixels = limit + 1 > limit.

TEST(Validator, TextureOneOverPixelLimitIsWarning)
{
    Validator v;
    ValidatorPolicy policy;
    policy.max_texture_pixels = 4ULL;
    v.configure(policy);

    // size = (4+1)*4 = 20 bytes => conservative_pixels = 20/4 = 5 > 4 => warning.
    std::vector<std::uint8_t> blob(kPngSig.begin(), kPngSig.end());
    blob.resize(20U, 0x00U);

    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ blob }, "over.png");
    EXPECT_TRUE(has_warning(issues))
        << "conservative_pixels == limit+1 must trigger kWarning";
    EXPECT_FALSE(has_error(issues))
        << "Valid PNG magic must not produce kError regardless of size";
}

// ---- T23: OGG with extra trailing bytes => no issues (prefix match only) ----

TEST(Validator, OggWithTrailingBytesNoIssues)
{
    const Validator v;
    // The validator only checks the 4-byte "OggS" prefix; trailing bytes are ignored.
    constexpr std::array<std::uint8_t, 10> kOggBlob{
        'O', 'g', 'g', 'S',
        0x00U, 0x02U, 0xFFU, 0x00U, 0x00U, 0x00U
    };
    const auto issues = v.validate_audio_blob(
        std::span<const std::uint8_t>{ kOggBlob }, "stream.ogg");
    EXPECT_TRUE(issues.empty())
        << "OGG blob with extra bytes after magic must produce no issues";
}

// ---- T24: WAV: 11-byte blob (RIFF present but too short for WAVE at +8) -----
// The WAV check requires blob.size() >= 12 to read the WAVE fourCC at offset 8.
// An 11-byte blob fulfils the RIFF prefix but fails the size guard.

TEST(Validator, WavElevenBytesIsError)
{
    const Validator v;
    constexpr std::array<std::uint8_t, 11> kRiff11{
        'R', 'I', 'F', 'F',
        0x03U, 0x00U, 0x00U, 0x00U,  // chunk size (dummy)
        'W', 'A', 'V'                // only 3 bytes of "WAVE" — truncated
    };
    const auto issues = v.validate_audio_blob(
        std::span<const std::uint8_t>{ kRiff11 }, "trunc11.wav");
    EXPECT_TRUE(has_error(issues))
        << "11-byte RIFF blob (no room for WAVE at offset 8) must produce kError";
}

// ---- T25: policy() accessor returns default values on a fresh Validator ------

TEST(Validator, PolicyAccessorDefaultValues)
{
    const Validator v;
    const auto& pol = v.policy();
    EXPECT_EQ(pol.max_texture_pixels, 1024ULL * 1024ULL * 16ULL)
        << "Default max_texture_pixels must be 16 Mi";
    EXPECT_EQ(pol.max_mesh_vertices, 1024ULL * 1024ULL)
        << "Default max_mesh_vertices must be 1 Mi";
}

// ---- T26: policy() accessor reflects values after configure() ---------------

TEST(Validator, PolicyAccessorAfterConfigure)
{
    Validator v;
    ValidatorPolicy custom;
    custom.max_texture_pixels = 512ULL;
    custom.max_mesh_vertices  = 256ULL;
    v.configure(custom);

    EXPECT_EQ(v.policy().max_texture_pixels, 512ULL)
        << "max_texture_pixels must reflect configured value";
    EXPECT_EQ(v.policy().max_mesh_vertices, 256ULL)
        << "max_mesh_vertices must reflect configured value";
}

// ---- T27: SEAL — deep schema parse is out-of-scope -------------------------
// The validator contract is magic-header sniffing only.  Deep semantic
// validation (mip chains, mesh topology, audio sample-rate, schema version,
// extension-vs-magic cross-check) is Sprint-2 and is documented out-of-scope.
// This test documents the sealed contract: a syntactically wrong PNG body
// (garbage after a valid magic prefix) still passes the validator cleanly.

TEST(Validator, DeepSchemaParseOutOfScope_MagicSniffIsContract)
{
    const Validator v;
    // Valid PNG 4-byte magic followed by deliberate garbage — a real PNG decoder
    // would reject this, but the Sprint-1 validator must not.
    std::vector<std::uint8_t> blob(kPngSig.begin(), kPngSig.end());
    // Append 32 bytes of garbage that would fail any real PNG chunk parser.
    blob.insert(blob.end(), 32U, 0xDEU);

    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ blob }, "broken_body.png");
    // No kError: magic is valid, body depth is sealed out-of-scope.
    EXPECT_FALSE(has_error(issues))
        << "SEAL: deep body parse is out-of-scope; magic-sniff is the full contract";
}

// ---- T28: required_color_spaces — all three slots set => kInfo emitted ------
// When all three required_color_spaces slots are non-empty the validator
// emits a single kInfo noting the active color-space requirements.

TEST(Validator, RequiredColorSpacesAllSetEmitsInfo)
{
    Validator v;
    ValidatorPolicy policy;
    policy.required_color_spaces[0] = "sRGB";
    policy.required_color_spaces[1] = "linear";
    policy.required_color_spaces[2] = "ACEScg";
    v.configure(policy);

    // Use a blob large enough that conservative_pixels does not exceed the
    // default pixel limit, so only the kInfo fires (no kWarning interference).
    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ kPngSig }, "albedo.png");

    EXPECT_TRUE(has_severity(issues, Severity::kInfo))
        << "All three required_color_spaces set must emit kInfo";
    EXPECT_FALSE(has_error(issues))
        << "kInfo for color-spaces must not produce kError";
    EXPECT_FALSE(has_warning(issues))
        << "kInfo for color-spaces must not produce kWarning when blob is valid and in-limit";
}

// ---- T29: required_color_spaces — partial (only 1 or 2 slots set) => no kInfo
// kInfo is only emitted when ALL THREE slots are non-empty; partial fill is silent.

TEST(Validator, RequiredColorSpacesPartialFillNoInfo)
{
    Validator v;
    ValidatorPolicy policy;
    policy.required_color_spaces[0] = "sRGB";
    // slots 1 and 2 remain empty
    v.configure(policy);

    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ kPngSig }, "albedo.png");

    EXPECT_FALSE(has_severity(issues, Severity::kInfo))
        << "Partial required_color_spaces fill must NOT emit kInfo";
}

// ---- T30: required_color_spaces — two slots set, one empty => no kInfo ------

TEST(Validator, RequiredColorSpacesTwoOfThreeNoInfo)
{
    Validator v;
    ValidatorPolicy policy;
    policy.required_color_spaces[0] = "sRGB";
    policy.required_color_spaces[1] = "linear";
    // slot 2 empty
    v.configure(policy);

    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ kPngSig }, "albedo.png");

    EXPECT_FALSE(has_severity(issues, Severity::kInfo))
        << "Two-of-three required_color_spaces must NOT emit kInfo";
}

// ---- T31: required_color_spaces kInfo message contains all three slot names -

TEST(Validator, RequiredColorSpacesInfoMessageContainsSlotNames)
{
    Validator v;
    ValidatorPolicy policy;
    policy.required_color_spaces[0] = "sRGB";
    policy.required_color_spaces[1] = "linear";
    policy.required_color_spaces[2] = "ACEScg";
    v.configure(policy);

    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ kPngSig }, "albedo.png");

    const auto info_it = std::ranges::find_if(
        issues,
        [](const ValidationIssue& i)
        {
            return i.severity == Severity::kInfo;
        });
    ASSERT_NE(info_it, issues.end())
        << "Expected at least one kInfo issue for color-spaces";

    EXPECT_NE(info_it->message.find("sRGB"),   std::string::npos)
        << "kInfo message must contain slot-0 name 'sRGB'";
    EXPECT_NE(info_it->message.find("linear"), std::string::npos)
        << "kInfo message must contain slot-1 name 'linear'";
    EXPECT_NE(info_it->message.find("ACEScg"), std::string::npos)
        << "kInfo message must contain slot-2 name 'ACEScg'";
}

// ---- T32: asset_path is propagated into ValidationIssue::asset_path ---------
// Each validate_*_blob() must copy the caller-supplied path into every issue.

TEST(Validator, AssetPathPropagatedIntoIssue_Texture)
{
    const Validator v;
    const std::string_view kPath{ "textures/missing_magic.bin" };
    // Garbage blob => kError with unrecognised-format message.
    constexpr std::array<std::uint8_t, 4> kGarbage{ 0x00U, 0x00U, 0x00U, 0x00U };
    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ kGarbage }, kPath);

    ASSERT_FALSE(issues.empty());
    EXPECT_EQ(issues.front().asset_path, kPath)
        << "asset_path must be copied verbatim into the issue struct (texture)";
}

TEST(Validator, AssetPathPropagatedIntoIssue_Gltf)
{
    const Validator v;
    const std::string_view kPath{ "scenes/corrupt.gltf" };
    // Blob that doesn't start with '{' or "glTF".
    constexpr std::array<std::uint8_t, 4> kBad{ 'X', 'X', 'X', 'X' };
    const auto issues = v.validate_gltf_blob(
        std::span<const std::uint8_t>{ kBad }, kPath);

    ASSERT_FALSE(issues.empty());
    EXPECT_EQ(issues.front().asset_path, kPath)
        << "asset_path must be copied verbatim into the issue struct (gltf)";
}

TEST(Validator, AssetPathPropagatedIntoIssue_Audio)
{
    const Validator v;
    const std::string_view kPath{ "sfx/bad_format.bin" };
    constexpr std::array<std::uint8_t, 4> kGarbage{ 0x00U, 0x00U, 0x00U, 0x00U };
    const auto issues = v.validate_audio_blob(
        std::span<const std::uint8_t>{ kGarbage }, kPath);

    ASSERT_FALSE(issues.empty());
    EXPECT_EQ(issues.front().asset_path, kPath)
        << "asset_path must be copied verbatim into the issue struct (audio)";
}

// ---- T33: multi-issue aggregation — wrong magic AND oversized in one call ---
// A blob with unrecognised magic AND a size that exceeds the conservative pixel
// limit must accumulate BOTH kError (bad magic) AND kWarning (size) in one
// validate_texture_blob() call — neither short-circuits the other.

TEST(Validator, MultiIssueAggregation_ErrorAndWarningInSameCall)
{
    Validator v;
    ValidatorPolicy policy;
    policy.max_texture_pixels = 1ULL;  // 1-pixel limit: conservative_pixels=blob/4 > 1
    v.configure(policy);

    // 8-byte garbage blob: unknown magic + size = 8 bytes => conservative_pixels=2>1.
    constexpr std::array<std::uint8_t, 8> kGarbage{
        0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U
    };
    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ kGarbage }, "multi.bin");

    EXPECT_TRUE(has_error(issues))
        << "Unrecognised magic must produce kError even when size is also over limit";
    EXPECT_TRUE(has_warning(issues))
        << "Oversized blob must produce kWarning even when magic is also invalid";
    EXPECT_GE(issues.size(), std::size_t{ 2 })
        << "Both kError and kWarning must be present in the same issue list";
}

// ---- T34: kInfo + kError aggregation — color-space note alongside bad magic -
// When the policy has all three required_color_spaces set AND the blob has an
// unrecognised magic, the result must contain BOTH kError and kInfo.

TEST(Validator, MultiIssueAggregation_InfoAndErrorInSameCall)
{
    Validator v;
    ValidatorPolicy policy;
    policy.required_color_spaces[0] = "sRGB";
    policy.required_color_spaces[1] = "linear";
    policy.required_color_spaces[2] = "ACEScg";
    v.configure(policy);

    constexpr std::array<std::uint8_t, 4> kGarbage{ 0xAAU, 0xBBU, 0xCCU, 0xDDU };
    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ kGarbage }, "dual.bin");

    EXPECT_TRUE(has_error(issues))
        << "Unrecognised magic must still produce kError";
    EXPECT_TRUE(has_severity(issues, Severity::kInfo))
        << "All-slots color-space note must still produce kInfo";
}

// ---- T35: kInfo + kWarning aggregation — color-space + oversized, valid magic
// Valid PNG magic + all color-spaces set + oversized => kInfo + kWarning, no kError.

TEST(Validator, MultiIssueAggregation_InfoAndWarning_ValidMagic)
{
    Validator v;
    ValidatorPolicy policy;
    policy.max_texture_pixels      = 1ULL;
    policy.required_color_spaces[0] = "sRGB";
    policy.required_color_spaces[1] = "linear";
    policy.required_color_spaces[2] = "ACEScg";
    v.configure(policy);

    // 8-byte PNG sig => conservative_pixels = 8/4 = 2 > 1 => kWarning.
    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ kPngSig }, "tagged.png");

    EXPECT_FALSE(has_error(issues))
        << "Valid PNG magic must not produce kError";
    EXPECT_TRUE(has_warning(issues))
        << "Oversized blob must produce kWarning";
    EXPECT_TRUE(has_severity(issues, Severity::kInfo))
        << "All-slots color-space policy must produce kInfo";
    EXPECT_GE(issues.size(), std::size_t{ 2 })
        << "kWarning + kInfo must both be present";
}

// ---- T36: issue message is non-empty for every error/warning path -----------

TEST(Validator, IssueMessageNonEmpty_EmptyTexture)
{
    const Validator v;
    const auto issues = v.validate_texture_blob({}, "empty.png");
    ASSERT_FALSE(issues.empty());
    EXPECT_FALSE(issues.front().message.empty())
        << "ValidationIssue::message must not be empty for empty-blob kError";
}

TEST(Validator, IssueMessageNonEmpty_BadAudioMagic)
{
    const Validator v;
    constexpr std::array<std::uint8_t, 4> kBad{ 0x00U, 0x00U, 0x00U, 0x00U };
    const auto issues = v.validate_audio_blob(
        std::span<const std::uint8_t>{ kBad }, "bad.bin");
    ASSERT_FALSE(issues.empty());
    EXPECT_FALSE(issues.front().message.empty())
        << "ValidationIssue::message must not be empty for bad-magic audio kError";
}

// ---- T37: empty asset_path string is stored as-is (no crash, no default) ----
// Callers may legitimately pass an empty path (e.g. in-memory blob with no
// associated file).  The validator must store it unchanged.

TEST(Validator, EmptyAssetPathStoredVerbatim)
{
    const Validator v;
    const auto issues = v.validate_texture_blob({}, "");
    ASSERT_FALSE(issues.empty()) << "Empty blob must produce kError";
    EXPECT_TRUE(issues.front().asset_path.empty())
        << "Empty path string must be stored verbatim, not replaced";
}

// ---- T38: severity enum — kInfo < kWarning < kError ordering ----------------
// Ensure all three severity enumerators are distinct and ordered as documented.

TEST(Validator, SeverityEnumValuesAreDistinct)
{
    EXPECT_NE(Severity::kInfo,    Severity::kWarning);
    EXPECT_NE(Severity::kWarning, Severity::kError);
    EXPECT_NE(Severity::kInfo,    Severity::kError);
}

// ---- T39: GLB version = UINT32_MAX => kWarning (extreme boundary) -----------

TEST(Validator, GlbVersionMaxUint32IsWarning)
{
    const Validator v;
    constexpr std::array<std::uint8_t, 12> kGlbMaxVer{
        0x67U, 0x6CU, 0x54U, 0x46U,  // "glTF"
        0xFFU, 0xFFU, 0xFFU, 0xFFU,  // version = UINT32_MAX (LE)
        0x0CU, 0x00U, 0x00U, 0x00U   // length = 12
    };
    const auto issues = v.validate_gltf_blob(
        std::span<const std::uint8_t>{ kGlbMaxVer }, "future.glb");
    EXPECT_TRUE(has_warning(issues))
        << "GLB version=UINT32_MAX must produce kWarning (not version 2)";
    EXPECT_FALSE(has_error(issues))
        << "GLB version=UINT32_MAX must not produce kError";
}

// ---- T40: WAV with RIFF tag and correct WAVE tag at exactly offset 8 --------
// Verify the sub-span indexing is correct: the WAVE fourCC must be read from
// exactly offset 8 (bytes 8–11), not offset 0+N.

TEST(Validator, WavWaveTagAtExactOffset8Accepted)
{
    const Validator v;
    // "RIFF" + 4-byte size + "WAVE" — the canonical 12-byte minimal WAV.
    // Altering byte 7 (just before WAVE) must still work if bytes 8–11 == "WAVE".
    constexpr std::array<std::uint8_t, 12> kWavExact{
        'R', 'I', 'F', 'F',
        0x01U, 0xF4U, 0x00U, 0x00U,  // chunk size = 500 (dummy)
        'W', 'A', 'V', 'E'
    };
    const auto issues = v.validate_audio_blob(
        std::span<const std::uint8_t>{ kWavExact }, "sample.wav");
    EXPECT_TRUE(issues.empty())
        << "Valid 12-byte RIFF/WAVE blob must produce no issues";
}

// ---- T41: OGG: exactly 4-byte blob with correct magic => no issues ----------
// Minimum possible OGG blob (just the capture pattern).

TEST(Validator, OggExactly4BytesNoIssues)
{
    const Validator v;
    constexpr std::array<std::uint8_t, 4> kOgg4{ 'O', 'g', 'g', 'S' };
    const auto issues = v.validate_audio_blob(
        std::span<const std::uint8_t>{ kOgg4 }, "min.ogg");
    EXPECT_TRUE(issues.empty())
        << "Exactly-4-byte OggS blob must produce no issues";
}

// ---- T42: OGG: 3-byte blob starting 'O','g','g' => kError (too short) ------
// OGG magic requires exactly 4 bytes "OggS"; 3 bytes must fail the magic check.

TEST(Validator, OggThreeBytesBlobIsError)
{
    const Validator v;
    constexpr std::array<std::uint8_t, 3> kOgg3{ 'O', 'g', 'g' };
    const auto issues = v.validate_audio_blob(
        std::span<const std::uint8_t>{ kOgg3 }, "trunc.ogg");
    EXPECT_TRUE(has_error(issues))
        << "3-byte blob 'Ogg' (missing final 'S') must produce kError";
}

// ---- T43: texture blob: max_texture_pixels = 0 => every non-empty blob warns -
// A zero pixel limit means any non-zero size exceeds it
// (conservative_pixels = blob.size()/4 > 0 only when blob.size() >= 4,
// but with limit=0: blob.size()/4 > 0 whenever size >= 4).
// Specifically, a valid PNG sig (8 bytes) => conservative_pixels=2 > 0 => kWarning.

TEST(Validator, ZeroPixelLimitWarnsOnAnyNonTinyBlob)
{
    Validator v;
    ValidatorPolicy policy;
    policy.max_texture_pixels = 0ULL;
    v.configure(policy);

    // 8-byte PNG sig: conservative_pixels = 8/4 = 2 > 0 => kWarning.
    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ kPngSig }, "any.png");
    EXPECT_TRUE(has_warning(issues))
        << "Zero pixel limit must warn on a blob with >= 4 bytes";
    EXPECT_FALSE(has_error(issues))
        << "Valid PNG magic must not produce kError";
}

// ---- T44: required_color_spaces: configure clears previous all-set state ----
// After configuring all-three slots set (kInfo), a second configure() that
// clears them must stop emitting kInfo.

TEST(Validator, RequiredColorSpacesClearedByReconfigure)
{
    Validator v;
    ValidatorPolicy full;
    full.required_color_spaces[0] = "sRGB";
    full.required_color_spaces[1] = "linear";
    full.required_color_spaces[2] = "ACEScg";
    v.configure(full);

    {
        const auto issues = v.validate_texture_blob(
            std::span<const std::uint8_t>{ kPngSig }, "a.png");
        EXPECT_TRUE(has_severity(issues, Severity::kInfo))
            << "Before clear: must emit kInfo";
    }

    // Reconfigure with empty slots.
    ValidatorPolicy cleared;
    v.configure(cleared);

    {
        const auto issues = v.validate_texture_blob(
            std::span<const std::uint8_t>{ kPngSig }, "a.png");
        EXPECT_FALSE(has_severity(issues, Severity::kInfo))
            << "After clear: must not emit kInfo";
    }
}

// ---- T45: validate_texture_blob returns empty vector for valid HDR minimal ---
// HDR magic "#?" is only 2 bytes; blobs of exactly 2 bytes must pass the
// magic check without OOB reads.

TEST(Validator, ValidHdrExactly2BytesNoIssues)
{
    const Validator v;
    constexpr std::array<std::uint8_t, 2> kHdr2{ '#', '?' };
    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ kHdr2 }, "sky.hdr");
    EXPECT_TRUE(issues.empty())
        << "Exactly-2-byte '#?' blob must satisfy the HDR magic check with no issues";
}

// ---- T46: validate_texture_blob: 1-byte '#' blob => kError (HDR needs 2 bytes)

TEST(Validator, HdrOneByteBlobIsError)
{
    const Validator v;
    constexpr std::array<std::uint8_t, 1> kHash{ '#' };
    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ kHash }, "partial.hdr");
    EXPECT_TRUE(has_error(issues))
        << "1-byte '#' blob must not match HDR magic (needs 2 bytes) — kError expected";
}

// ---- T47: SEAL — max_mesh_vertices field exists in policy but no mesh -------
// validate_mesh_blob() is not part of the Sprint-1 surface.  The policy field
// max_mesh_vertices is reserved for Sprint-2 and is round-tripped through
// configure()/policy() only — it does not influence any current validate_*()
// call.  This test pins the sealed contract and guards against an accidental
// silent wiring.

TEST(Validator, MeshVerticesFieldRoundTrips_NoMeshValidationSurface)
{
    Validator v;
    ValidatorPolicy policy;
    policy.max_mesh_vertices = 42ULL;
    v.configure(policy);

    EXPECT_EQ(v.policy().max_mesh_vertices, 42ULL)
        << "max_mesh_vertices must round-trip through configure()/policy()";

    // There is no validate_mesh_blob() in Sprint-1: the field is reserved.
    // Calling any existing validate_*() with a valid blob must NOT be
    // influenced by max_mesh_vertices — the texture validator must still
    // accept a valid PNG regardless of this field.
    const auto issues = v.validate_texture_blob(
        std::span<const std::uint8_t>{ kPngSig }, "mesh_unrelated.png");
    EXPECT_TRUE(issues.empty())
        << "max_mesh_vertices must not influence validate_texture_blob() result "
           "(no mesh validation surface in Sprint-1 — SEALED)";
}

// ---- T48: glTF JSON: first-non-whitespace == '{', one character blob --------
// Edge case: a single '{' byte must be accepted as a minimal JSON glTF blob.

TEST(Validator, SingleBraceJsonGltfNoIssues)
{
    const Validator v;
    constexpr std::array<std::uint8_t, 1> kBrace{ '{' };
    const auto issues = v.validate_gltf_blob(
        std::span<const std::uint8_t>{ kBrace }, "minimal.gltf");
    EXPECT_TRUE(issues.empty())
        << "Single-byte '{' blob must pass the JSON glTF validation";
}

// ---- T49: glTF JSON: first-non-whitespace == '[' => kError ------------------
// A JSON array root is not valid glTF JSON (must start with an object '{').

TEST(Validator, JsonArrayRootGltfIsError)
{
    const Validator v;
    constexpr std::array<std::uint8_t, 4> kArray{ '[', ']', ' ', ' ' };
    const auto issues = v.validate_gltf_blob(
        std::span<const std::uint8_t>{ kArray }, "array_root.gltf");
    EXPECT_TRUE(has_error(issues))
        << "JSON glTF blob starting with '[' (array root) must produce kError";
}

// ---- T50: kInfo is never emitted by gltf or audio validators ----------------
// required_color_spaces is a texture-only policy field.  The glTF and audio
// validate_*() functions must not produce any kInfo issue regardless of the
// policy's required_color_spaces configuration.

TEST(Validator, GltfAndAudioValidatorsNeverEmitKInfo)
{
    Validator v;
    ValidatorPolicy policy;
    policy.required_color_spaces[0] = "sRGB";
    policy.required_color_spaces[1] = "linear";
    policy.required_color_spaces[2] = "ACEScg";
    v.configure(policy);

    const auto gltf_issues = v.validate_gltf_blob(
        std::span<const std::uint8_t>{ kGlbHeader }, "scene.glb");
    EXPECT_FALSE(has_severity(gltf_issues, Severity::kInfo))
        << "gltf validator must never emit kInfo for required_color_spaces";

    const auto audio_issues = v.validate_audio_blob(
        std::span<const std::uint8_t>{ kWavHeader }, "click.wav");
    EXPECT_FALSE(has_severity(audio_issues, Severity::kInfo))
        << "audio validator must never emit kInfo for required_color_spaces";
}

}  // namespace
