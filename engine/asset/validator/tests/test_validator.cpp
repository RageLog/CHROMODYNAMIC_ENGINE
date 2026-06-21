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

}  // namespace
