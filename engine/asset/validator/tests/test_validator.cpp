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
// =============================================================================

#include <cd/asset/validator/Validator.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
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
    return std::any_of(issues.begin(), issues.end(),
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

}  // namespace
