// =============================================================================
// CHROMODYNAMIC — engine/asset/vfx_authoring/tests/test_vfx_authoring.cpp
// Phase 694 — cd::asset::vfx_authoring unit tests
//
// Tests:
//   T1  Presets: all 4 presets pass validate_authored() with no [ERROR].
//   T2  JSON round-trip: save_to_json + load_from_json -> identical fields.
//   T3  validate_authored: empty id -> [ERROR] returned, returns false.
//   T4  validate_authored: emit_rate_per_sec <= 0 -> [ERROR].
//   T5  validate_authored: life_seconds <= 0 -> [ERROR].
//   T6  validate_authored: color channel out of [0,1] -> [ERROR].
//   T7  validate_authored: velocity_min > velocity_max -> [ERROR] per axis.
//   T8  load_from_json: missing 'id' -> nullopt.
//   T9  load_from_json: missing 'emitter_kind' -> nullopt.
//   T10 load_from_json: missing optional fields -> falls back to AuthoredVfx
//       defaults (emit_rate_per_sec=30, life_seconds=1, etc.).
//   T11 load_from_json: malformed JSON (unterminated object) -> nullopt
//       (parse error, no exception escapes).
//   T12 load_from_json: unknown / extra keys are tolerated — known fields still
//       load and the surplus keys are ignored without error.
//   T13 JSON round-trip: all 4 presets individually (not just jump_dust).
//   T14 validate_authored: empty emitter_kind -> [ERROR].
//   T15 validate_authored: NaN in emit_rate_per_sec -> [ERROR].
//   T16 validate_authored: NaN in life_seconds -> [ERROR].
//   T17 validate_authored: size_start < 0 -> [ERROR].
//   T18 validate_authored: size_end < 0 -> [ERROR].
//   T19 validate_authored: velocity inversion on all 3 axes independently.
//   T20 validate_authored: multiple simultaneous errors all accumulate.
//   T21 load_from_json: file not found -> nullopt (no exception escapes).
//   T22 load_from_json: JSON root is array (not object) -> nullopt.
//   T23 load_from_json: empty string id present -> nullopt (empty id invalid).
//   T24 load_from_json: color array with fewer than 4 elements is tolerated
//       (partial arrays leave remaining channels at AuthoredVfx defaults).
// =============================================================================

#include <cd/asset/vfx_authoring/VfxAuthoring.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using cd::asset::vfx_authoring::AuthoredVfx;
using cd::asset::vfx_authoring::VfxPresets;
using cd::asset::vfx_authoring::load_from_json;
using cd::asset::vfx_authoring::save_to_json;
using cd::asset::vfx_authoring::validate_authored;

// ---- helpers ----------------------------------------------------------------

bool has_error(const std::vector<std::string>& issues)
{
    return std::ranges::any_of(issues,
                       [](const std::string& s)
                       { return s.starts_with("[ERROR]"); });
}

// ---- T1: Presets pass validation --------------------------------------------

TEST(VfxAuthoring, T1_PresetsPassValidation)
{
    const std::array presets {
        VfxPresets::jump_dust(),
        VfxPresets::muzzle_flash(),
        VfxPresets::fire_smoke(),
        VfxPresets::water_splash(),
    };

    for (const auto& vfx : presets)
    {
        std::vector<std::string> issues;
        const bool ok = validate_authored(vfx, issues);
        EXPECT_TRUE(ok) << "Preset '" << vfx.id << "' failed validation.";
        EXPECT_FALSE(has_error(issues))
            << "Preset '" << vfx.id << "' produced [ERROR]: "
            << (issues.empty() ? "(none)" : issues.front());
    }
}

// ---- T2: JSON round-trip ----------------------------------------------------

TEST(VfxAuthoring, T2_JsonRoundTrip)
{
    const AuthoredVfx original = VfxPresets::jump_dust();

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_authoring_roundtrip_test.json";

    ASSERT_TRUE(save_to_json(original, tmp))
        << "save_to_json failed for path: " << tmp;

    const auto loaded = load_from_json(tmp);
    ASSERT_TRUE(loaded.has_value()) << "load_from_json returned nullopt.";

    EXPECT_EQ(loaded->id,                original.id);
    EXPECT_EQ(loaded->emitter_kind,      original.emitter_kind);
    EXPECT_NEAR(loaded->emit_rate_per_sec, original.emit_rate_per_sec, 1e-4F);
    EXPECT_NEAR(loaded->life_seconds,      original.life_seconds,      1e-4F);
    EXPECT_NEAR(loaded->size_start,        original.size_start,        1e-4F);
    EXPECT_NEAR(loaded->size_end,          original.size_end,          1e-4F);
    EXPECT_EQ(loaded->texture_path, original.texture_path);

    for (std::size_t i = 0; i < 4; ++i)
    {
        EXPECT_NEAR(loaded->color_start[i], original.color_start[i], 1e-4F)
            << "color_start[" << i << "] mismatch";
        EXPECT_NEAR(loaded->color_end[i],   original.color_end[i],   1e-4F)
            << "color_end[" << i << "] mismatch";
    }
    for (std::size_t i = 0; i < 3; ++i)
    {
        EXPECT_NEAR(loaded->velocity_min[i], original.velocity_min[i], 1e-4F)
            << "velocity_min[" << i << "] mismatch";
        EXPECT_NEAR(loaded->velocity_max[i], original.velocity_max[i], 1e-4F)
            << "velocity_max[" << i << "] mismatch";
    }

    std::filesystem::remove(tmp);
}

// ---- T3: validate — empty id ------------------------------------------------

TEST(VfxAuthoring, T3_ValidateEmptyId)
{
    AuthoredVfx vfx = VfxPresets::jump_dust();
    vfx.id = "";

    std::vector<std::string> issues;
    const bool ok = validate_authored(vfx, issues);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(has_error(issues));
}

// ---- T4: validate — emit_rate_per_sec <= 0 ----------------------------------

TEST(VfxAuthoring, T4_ValidateNonPositiveEmitRate)
{
    AuthoredVfx vfx = VfxPresets::muzzle_flash();
    vfx.emit_rate_per_sec = 0.0F;

    std::vector<std::string> issues;
    EXPECT_FALSE(validate_authored(vfx, issues));
    EXPECT_TRUE(has_error(issues));

    issues.clear();
    vfx.emit_rate_per_sec = -5.0F;
    EXPECT_FALSE(validate_authored(vfx, issues));
    EXPECT_TRUE(has_error(issues));
}

// ---- T5: validate — life_seconds <= 0 --------------------------------------

TEST(VfxAuthoring, T5_ValidateNonPositiveLifeSeconds)
{
    AuthoredVfx vfx = VfxPresets::fire_smoke();
    vfx.life_seconds = 0.0F;

    std::vector<std::string> issues;
    EXPECT_FALSE(validate_authored(vfx, issues));
    EXPECT_TRUE(has_error(issues));
}

// ---- T6: validate — color channel out of [0,1] -----------------------------

TEST(VfxAuthoring, T6_ValidateColorOutOfRange)
{
    AuthoredVfx vfx = VfxPresets::water_splash();
    vfx.color_start[0] = 1.5F;  // red > 1

    std::vector<std::string> issues;
    EXPECT_FALSE(validate_authored(vfx, issues));
    EXPECT_TRUE(has_error(issues));

    issues.clear();
    vfx.color_start[0] = 0.78F;  // restore
    vfx.color_end[3]   = -0.1F;  // alpha < 0
    EXPECT_FALSE(validate_authored(vfx, issues));
    EXPECT_TRUE(has_error(issues));
}

// ---- T7: validate — velocity_min > velocity_max ----------------------------

TEST(VfxAuthoring, T7_ValidateVelocityMinExceedsMax)
{
    AuthoredVfx vfx = VfxPresets::jump_dust();
    vfx.velocity_min[1] = 5.0F;  // Y min > max (2.5)

    std::vector<std::string> issues;
    EXPECT_FALSE(validate_authored(vfx, issues));
    EXPECT_TRUE(has_error(issues));
    // Confirm the axis is mentioned
    EXPECT_TRUE(std::ranges::any_of(issues,
        [](const std::string& s) { return s.find('Y') != std::string::npos; }));
}

// ---- T8: load — missing id -> nullopt ---------------------------------------

TEST(VfxAuthoring, T8_LoadMissingIdReturnsNullopt)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_no_id.json";

    // Write JSON without 'id' field
    {
        std::ofstream ofs { tmp };
        ofs << R"({"emitter_kind":"burst","emit_rate_per_sec":10.0,"life_seconds":1.0})";
    }

    const auto result = load_from_json(tmp);
    EXPECT_FALSE(result.has_value());

    std::filesystem::remove(tmp);
}

// ---- T9: load — missing emitter_kind -> nullopt ----------------------------

TEST(VfxAuthoring, T9_LoadMissingEmitterKindReturnsNullopt)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_no_emitter.json";

    {
        std::ofstream ofs { tmp };
        ofs << R"({"id":"test_vfx","emit_rate_per_sec":10.0,"life_seconds":1.0})";
    }

    const auto result = load_from_json(tmp);
    EXPECT_FALSE(result.has_value());

    std::filesystem::remove(tmp);
}

// ---- T10: load — missing optional fields -> defaults -----------------------

TEST(VfxAuthoring, T10_LoadMissingOptionalFieldsFallsBackToDefaults)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_minimal.json";

    {
        std::ofstream ofs { tmp };
        ofs << R"({"id":"minimal_vfx","emitter_kind":"continuous"})";
    }

    const auto result = load_from_json(tmp);
    ASSERT_TRUE(result.has_value());

    const AuthoredVfx defaults;
    EXPECT_EQ(result->id,           "minimal_vfx");
    EXPECT_EQ(result->emitter_kind, "continuous");
    EXPECT_NEAR(result->emit_rate_per_sec, defaults.emit_rate_per_sec, 1e-4F);
    EXPECT_NEAR(result->life_seconds,      defaults.life_seconds,      1e-4F);
    EXPECT_NEAR(result->size_start,        defaults.size_start,        1e-4F);
    EXPECT_NEAR(result->size_end,          defaults.size_end,          1e-4F);
    EXPECT_EQ(result->texture_path, "");

    std::filesystem::remove(tmp);
}

// ---- T11: load — malformed JSON -> nullopt ----------------------------------

TEST(VfxAuthoring, T11_LoadMalformedJsonReturnsNullopt)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_malformed.json";

    {
        std::ofstream ofs { tmp };
        // Unterminated object + dangling key — not parseable JSON.
        ofs << R"({"id":"broken","emitter_kind":)";
    }

    const auto result = load_from_json(tmp);
    EXPECT_FALSE(result.has_value())
        << "malformed JSON must yield nullopt, not a partial AuthoredVfx";

    std::filesystem::remove(tmp);
}

// ---- T12: load — unknown / extra keys are tolerated -------------------------

TEST(VfxAuthoring, T12_LoadUnknownKeysAreIgnored)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_unknown_keys.json";

    {
        std::ofstream ofs { tmp };
        // Required + known optional fields plus several keys the loader does not
        // recognise (future schema additions / authoring-tool metadata).
        ofs << R"({)"
               R"("id":"surplus_vfx",)"
               R"("emitter_kind":"burst",)"
               R"("life_seconds":2.0,)"
               R"("author":"designer_a",)"
               R"("editor_revision":7,)"
               R"("tags":["smoke","warm"],)"
               R"("nested":{"k":"v"})"
               R"(})";
    }

    const auto result = load_from_json(tmp);
    ASSERT_TRUE(result.has_value())
        << "unknown keys must not cause load to fail";

    EXPECT_EQ(result->id,           "surplus_vfx");
    EXPECT_EQ(result->emitter_kind, "burst");
    EXPECT_NEAR(result->life_seconds, 2.0F, 1e-4F);

    // The surplus keys must not have disturbed defaulted fields.
    const AuthoredVfx defaults;
    EXPECT_NEAR(result->emit_rate_per_sec, defaults.emit_rate_per_sec, 1e-4F);

    std::filesystem::remove(tmp);
}

// ---- T13: JSON round-trip for all 4 presets ---------------------------------

TEST(VfxAuthoring, T13_AllPresetsRoundTrip)
{
    const std::array presets {
        VfxPresets::jump_dust(),
        VfxPresets::muzzle_flash(),
        VfxPresets::fire_smoke(),
        VfxPresets::water_splash(),
    };

    for (const auto& original : presets)
    {
        const std::filesystem::path tmp =
            std::filesystem::temp_directory_path()
            / ("cd_vfx_rt_" + original.id + ".json");

        ASSERT_TRUE(save_to_json(original, tmp))
            << "save_to_json failed for preset: " << original.id;

        const auto loaded = load_from_json(tmp);
        ASSERT_TRUE(loaded.has_value()) << "load_from_json failed for preset: " << original.id;

        EXPECT_EQ(loaded->id,           original.id)           << original.id;
        EXPECT_EQ(loaded->emitter_kind, original.emitter_kind) << original.id;
        EXPECT_NEAR(loaded->emit_rate_per_sec, original.emit_rate_per_sec, 1e-4F) << original.id;
        EXPECT_NEAR(loaded->life_seconds,      original.life_seconds,      1e-4F) << original.id;
        EXPECT_NEAR(loaded->size_start,        original.size_start,        1e-4F) << original.id;
        EXPECT_NEAR(loaded->size_end,          original.size_end,          1e-4F) << original.id;
        EXPECT_EQ(loaded->texture_path, original.texture_path) << original.id;

        for (std::size_t i = 0; i < 4; ++i)
        {
            EXPECT_NEAR(loaded->color_start[i], original.color_start[i], 1e-4F)
                << original.id << " color_start[" << i << "]";
            EXPECT_NEAR(loaded->color_end[i],   original.color_end[i],   1e-4F)
                << original.id << " color_end[" << i << "]";
        }
        for (std::size_t i = 0; i < 3; ++i)
        {
            EXPECT_NEAR(loaded->velocity_min[i], original.velocity_min[i], 1e-4F)
                << original.id << " velocity_min[" << i << "]";
            EXPECT_NEAR(loaded->velocity_max[i], original.velocity_max[i], 1e-4F)
                << original.id << " velocity_max[" << i << "]";
        }

        std::filesystem::remove(tmp);
    }
}

// ---- T14: validate — empty emitter_kind -> [ERROR] --------------------------

TEST(VfxAuthoring, T14_ValidateEmptyEmitterKind)
{
    AuthoredVfx vfx = VfxPresets::fire_smoke();
    vfx.emitter_kind = "";

    std::vector<std::string> issues;
    const bool ok = validate_authored(vfx, issues);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(has_error(issues));
    // The error message must mention emitter_kind.
    EXPECT_TRUE(std::ranges::any_of(issues,
        [](const std::string& s)
        { return s.find("emitter_kind") != std::string::npos; }));
}

// ---- T15: validate — NaN in emit_rate_per_sec -> [ERROR] -------------------

TEST(VfxAuthoring, T15_ValidateNanEmitRate)
{
    AuthoredVfx vfx = VfxPresets::jump_dust();
    vfx.emit_rate_per_sec = std::numeric_limits<float>::quiet_NaN();

    std::vector<std::string> issues;
    const bool ok = validate_authored(vfx, issues);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(has_error(issues));
}

// ---- T16: validate — NaN in life_seconds -> [ERROR] ------------------------

TEST(VfxAuthoring, T16_ValidateNanLifeSeconds)
{
    AuthoredVfx vfx = VfxPresets::muzzle_flash();
    vfx.life_seconds = std::numeric_limits<float>::quiet_NaN();

    std::vector<std::string> issues;
    const bool ok = validate_authored(vfx, issues);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(has_error(issues));
}

// ---- T17: validate — size_start < 0 -> [ERROR] -----------------------------

TEST(VfxAuthoring, T17_ValidateNegativeSizeStart)
{
    AuthoredVfx vfx = VfxPresets::water_splash();
    vfx.size_start = -0.01F;

    std::vector<std::string> issues;
    const bool ok = validate_authored(vfx, issues);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(has_error(issues));
    EXPECT_TRUE(std::ranges::any_of(issues,
        [](const std::string& s)
        { return s.find("size_start") != std::string::npos; }));
}

// ---- T18: validate — size_end < 0 -> [ERROR] --------------------------------

TEST(VfxAuthoring, T18_ValidateNegativeSizeEnd)
{
    AuthoredVfx vfx = VfxPresets::fire_smoke();
    vfx.size_end = -1.0F;

    std::vector<std::string> issues;
    const bool ok = validate_authored(vfx, issues);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(has_error(issues));
    EXPECT_TRUE(std::ranges::any_of(issues,
        [](const std::string& s)
        { return s.find("size_end") != std::string::npos; }));
}

// ---- T19: validate — velocity inversion on all 3 axes independently --------

TEST(VfxAuthoring, T19_ValidateVelocityInversionPerAxis)
{
    // X axis inversion
    {
        AuthoredVfx vfx = VfxPresets::jump_dust();
        vfx.velocity_min[0] = 9.0F;  // X min > X max (1.5)
        std::vector<std::string> issues;
        EXPECT_FALSE(validate_authored(vfx, issues));
        EXPECT_TRUE(std::ranges::any_of(issues,
            [](const std::string& s) { return s.find('X') != std::string::npos; }));
    }
    // Y axis inversion (already covered by T7; keep for completeness)
    {
        AuthoredVfx vfx = VfxPresets::jump_dust();
        vfx.velocity_min[1] = 5.0F;
        std::vector<std::string> issues;
        EXPECT_FALSE(validate_authored(vfx, issues));
        EXPECT_TRUE(std::ranges::any_of(issues,
            [](const std::string& s) { return s.find('Y') != std::string::npos; }));
    }
    // Z axis inversion
    {
        AuthoredVfx vfx = VfxPresets::jump_dust();
        vfx.velocity_min[2] = 9.0F;  // Z min > Z max (1.5)
        std::vector<std::string> issues;
        EXPECT_FALSE(validate_authored(vfx, issues));
        EXPECT_TRUE(std::ranges::any_of(issues,
            [](const std::string& s) { return s.find('Z') != std::string::npos; }));
    }
}

// ---- T20: validate — multiple simultaneous errors all accumulate ------------

TEST(VfxAuthoring, T20_ValidateMultipleErrorsAccumulate)
{
    AuthoredVfx vfx;
    // Leave id and emitter_kind empty (2 errors).
    // Set emit_rate and life to zero (2 more errors).
    vfx.emit_rate_per_sec = 0.0F;
    vfx.life_seconds      = 0.0F;
    // Also invert velocity on X (1 more error).
    vfx.velocity_min[0]   = 10.0F;
    vfx.velocity_max[0]   = 0.0F;

    std::vector<std::string> issues;
    const bool ok = validate_authored(vfx, issues);

    EXPECT_FALSE(ok);
    // At least 4 distinct [ERROR] entries must have been appended.
    const auto error_count =
        std::ranges::count_if(issues,
            [](const std::string& s) { return s.starts_with("[ERROR]"); });
    EXPECT_GE(error_count, 4);
}

// ---- T21: load — file not found -> nullopt ----------------------------------

TEST(VfxAuthoring, T21_LoadFileNotFoundReturnsNullopt)
{
    const std::filesystem::path nonexistent =
        std::filesystem::temp_directory_path() / "cd_vfx_does_not_exist_12345.json";

    // Guarantee the file genuinely does not exist.
    std::filesystem::remove(nonexistent);

    const auto result = load_from_json(nonexistent);
    EXPECT_FALSE(result.has_value())
        << "loading a non-existent file must return nullopt";
}

// ---- T22: load — JSON root is array (not object) -> nullopt ----------------

TEST(VfxAuthoring, T22_LoadRootArrayReturnsNullopt)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_root_array.json";

    {
        std::ofstream ofs { tmp };
        // A valid JSON array — but the loader expects a top-level object.
        ofs << R"([{"id":"x"},{"id":"y"}])";
    }

    const auto result = load_from_json(tmp);
    EXPECT_FALSE(result.has_value())
        << "a JSON root that is not an object must return nullopt";

    std::filesystem::remove(tmp);
}

// ---- T23: load — empty string id present -> nullopt ------------------------

TEST(VfxAuthoring, T23_LoadEmptyStringIdReturnsNullopt)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_empty_id.json";

    {
        std::ofstream ofs { tmp };
        // id key is present but value is the empty string — must be treated as
        // missing because read_required_string checks !out.empty().
        ofs << R"({"id":"","emitter_kind":"burst","emit_rate_per_sec":10.0,"life_seconds":1.0})";
    }

    const auto result = load_from_json(tmp);
    EXPECT_FALSE(result.has_value())
        << "an empty string id must cause load_from_json to return nullopt";

    std::filesystem::remove(tmp);
}

// ---- T24: load — partial color array (< 4 elements) is tolerated -----------

TEST(VfxAuthoring, T24_LoadPartialColorArrayUsesDefaults)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_partial_color.json";

    {
        std::ofstream ofs { tmp };
        // color_start has only 2 elements; color_end is absent entirely.
        // The loader must fill remaining channels from AuthoredVfx defaults.
        ofs << R"({)"
               R"("id":"partial_color","emitter_kind":"burst",)"
               R"("color_start":[0.5,0.6])"
               R"(})";
    }

    const auto result = load_from_json(tmp);
    ASSERT_TRUE(result.has_value())
        << "partial color array must not cause nullopt";

    // Elements 0 and 1 were provided.
    EXPECT_NEAR(result->color_start[0], 0.5F, 1e-4F);
    EXPECT_NEAR(result->color_start[1], 0.6F, 1e-4F);
    // Elements 2 and 3 must remain at AuthoredVfx defaults (1.0, 1.0).
    const AuthoredVfx defaults;
    EXPECT_NEAR(result->color_start[2], defaults.color_start[2], 1e-4F);
    EXPECT_NEAR(result->color_start[3], defaults.color_start[3], 1e-4F);
    // color_end was absent; must be the full default array.
    for (std::size_t i = 0; i < 4; ++i)
    {
        EXPECT_NEAR(result->color_end[i], defaults.color_end[i], 1e-4F)
            << "color_end[" << i << "] should equal AuthoredVfx default";
    }

    std::filesystem::remove(tmp);
}

}  // namespace
