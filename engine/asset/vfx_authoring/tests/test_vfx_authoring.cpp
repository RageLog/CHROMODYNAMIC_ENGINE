// =============================================================================
// CHROMODYNAMIC — engine/asset/vfx_authoring/tests/test_vfx_authoring.cpp
// Phase 694 (original) + Phase 1268 (complete pass)
//
// Tests:
// --- Original 24 (preserved) ---
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
// --- FloatCurve (T25-T33) ---
//   T25 FloatCurve::eval on empty curve returns 0.
//   T26 FloatCurve single keyframe returns that value for any t.
//   T27 FloatCurve two keyframes: eval at t=0, 0.5, 1.0.
//   T28 FloatCurve t clamped below 0 -> first keyframe value.
//   T29 FloatCurve t clamped above 1 -> last keyframe value.
//   T30 FloatCurve add_keyframe keeps sorted order regardless of insert order.
//   T31 FloatCurve round-trip: save_to_json + load_from_json preserves curve.
//   T32 FloatCurve validate: keyframe time out of [0,1] -> [ERROR].
//   T33 FloatCurve validate: keyframe value is NaN -> [ERROR].
// --- ColorGradient (T34-T42) ---
//   T34 ColorGradient::eval on empty gradient returns {0,0,0,0}.
//   T35 ColorGradient single keyframe returns that RGBA for any t.
//   T36 ColorGradient two keyframes: eval midpoint is correct lerp.
//   T37 ColorGradient t clamped below 0 -> first keyframe colour.
//   T38 ColorGradient t clamped above 1 -> last keyframe colour.
//   T39 ColorGradient add_keyframe keeps sorted order.
//   T40 ColorGradient round-trip: save_to_json + load_from_json preserves gradient.
//   T41 ColorGradient validate: keyframe time out of [0,1] -> [ERROR].
//   T42 ColorGradient validate: RGBA channel out of [0,1] -> [ERROR].
// --- Schema versioning (T43-T47) ---
//   T43 save_to_json writes "schema_version" field equal to kSchemaVersion.
//   T44 load_from_json: v0 file (no schema_version key) loads with schema_version==0.
//   T45 load_from_json: schema_version == kSchemaVersion -> loads successfully.
//   T46 load_from_json: schema_version > kSchemaVersion -> returns nullopt.
//   T47 validate_authored: schema_version > kSchemaVersion -> [WARNING].
// --- clone_with_id (T48-T51) ---
//   T48 clone_with_id: cloned id is the new id.
//   T49 clone_with_id: all other fields are identical to source.
//   T50 clone_with_id: mutation of clone does not affect original.
//   T51 clone_with_id: curves and gradients are deep-copied.
// --- VfxLibrary (T52-T67) ---
//   T52 VfxLibrary: initially empty.
//   T53 VfxLibrary::add: effect is findable after add.
//   T54 VfxLibrary::add: duplicate id overwrites previous entry.
//   T55 VfxLibrary::add: empty-id effect is rejected, returns false.
//   T56 VfxLibrary::remove: returns true and size decreases.
//   T57 VfxLibrary::remove: returns false for unknown id.
//   T58 VfxLibrary::find: returns nullptr for unknown id.
//   T59 VfxLibrary::size and empty are consistent.
//   T60 VfxLibrary::effects() iterates all added effects.
//   T61 VfxLibrary save + load round-trip: all effects present after reload.
//   T62 VfxLibrary save + load: field values survive the round-trip exactly.
//   T63 VfxLibrary load: missing 'effects' key -> returns false with [ERROR].
//   T64 VfxLibrary load: future-version effect entries are skipped with [WARNING].
//   T65 VfxLibrary load: file not found -> returns false with [ERROR].
//   T66 VfxLibrary load: malformed JSON -> returns false with [ERROR].
//   T67 VfxLibrary load: root is not object -> returns false with [ERROR].
// --- Validation warnings (T68-T71) ---
//   T68 validate_authored: emit_rate_per_sec > 10000 -> [WARNING], returns true.
//   T69 validate_authored: life_seconds > 60 -> [WARNING], returns true.
//   T70 validate_authored: unknown emitter_kind -> [WARNING], returns true.
//   T71 validate_authored: known emitter_kinds do not produce [WARNING].
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
using cd::asset::vfx_authoring::ColorGradient;
using cd::asset::vfx_authoring::FloatCurve;
using cd::asset::vfx_authoring::VfxLibrary;
using cd::asset::vfx_authoring::VfxPresets;
using cd::asset::vfx_authoring::clone_with_id;
using cd::asset::vfx_authoring::kSchemaVersion;
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

bool has_warning(const std::vector<std::string>& issues)
{
    return std::ranges::any_of(issues,
                       [](const std::string& s)
                       { return s.starts_with("[WARNING]"); });
}

bool contains_substr(const std::vector<std::string>& issues, std::string_view sub)
{
    return std::ranges::any_of(issues,
                       [sub](const std::string& s)
                       { return s.find(sub) != std::string::npos; });
}

// ============================================================
// T1 — T24: original tests (preserved, behaviour-identical)
// ============================================================

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

TEST(VfxAuthoring, T3_ValidateEmptyId)
{
    AuthoredVfx vfx = VfxPresets::jump_dust();
    vfx.id = "";

    std::vector<std::string> issues;
    const bool ok = validate_authored(vfx, issues);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(has_error(issues));
}

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

TEST(VfxAuthoring, T5_ValidateNonPositiveLifeSeconds)
{
    AuthoredVfx vfx = VfxPresets::fire_smoke();
    vfx.life_seconds = 0.0F;

    std::vector<std::string> issues;
    EXPECT_FALSE(validate_authored(vfx, issues));
    EXPECT_TRUE(has_error(issues));
}

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

TEST(VfxAuthoring, T7_ValidateVelocityMinExceedsMax)
{
    AuthoredVfx vfx = VfxPresets::jump_dust();
    vfx.velocity_min[1] = 5.0F;  // Y min > max (2.5)

    std::vector<std::string> issues;
    EXPECT_FALSE(validate_authored(vfx, issues));
    EXPECT_TRUE(has_error(issues));
    EXPECT_TRUE(contains_substr(issues, "Y"));
}

TEST(VfxAuthoring, T8_LoadMissingIdReturnsNullopt)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_no_id.json";

    {
        std::ofstream ofs { tmp };
        ofs << R"({"emitter_kind":"burst","emit_rate_per_sec":10.0,"life_seconds":1.0})";
    }

    const auto result = load_from_json(tmp);
    EXPECT_FALSE(result.has_value());

    std::filesystem::remove(tmp);
}

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

TEST(VfxAuthoring, T11_LoadMalformedJsonReturnsNullopt)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_malformed.json";

    {
        std::ofstream ofs { tmp };
        ofs << R"({"id":"broken","emitter_kind":)";
    }

    const auto result = load_from_json(tmp);
    EXPECT_FALSE(result.has_value())
        << "malformed JSON must yield nullopt, not a partial AuthoredVfx";

    std::filesystem::remove(tmp);
}

TEST(VfxAuthoring, T12_LoadUnknownKeysAreIgnored)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_unknown_keys.json";

    {
        std::ofstream ofs { tmp };
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

    const AuthoredVfx defaults;
    EXPECT_NEAR(result->emit_rate_per_sec, defaults.emit_rate_per_sec, 1e-4F);

    std::filesystem::remove(tmp);
}

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

TEST(VfxAuthoring, T14_ValidateEmptyEmitterKind)
{
    AuthoredVfx vfx = VfxPresets::fire_smoke();
    vfx.emitter_kind = "";

    std::vector<std::string> issues;
    const bool ok = validate_authored(vfx, issues);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(has_error(issues));
    EXPECT_TRUE(contains_substr(issues, "emitter_kind"));
}

TEST(VfxAuthoring, T15_ValidateNanEmitRate)
{
    AuthoredVfx vfx = VfxPresets::jump_dust();
    vfx.emit_rate_per_sec = std::numeric_limits<float>::quiet_NaN();

    std::vector<std::string> issues;
    const bool ok = validate_authored(vfx, issues);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(has_error(issues));
}

TEST(VfxAuthoring, T16_ValidateNanLifeSeconds)
{
    AuthoredVfx vfx = VfxPresets::muzzle_flash();
    vfx.life_seconds = std::numeric_limits<float>::quiet_NaN();

    std::vector<std::string> issues;
    const bool ok = validate_authored(vfx, issues);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(has_error(issues));
}

TEST(VfxAuthoring, T17_ValidateNegativeSizeStart)
{
    AuthoredVfx vfx = VfxPresets::water_splash();
    vfx.size_start = -0.01F;

    std::vector<std::string> issues;
    const bool ok = validate_authored(vfx, issues);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(has_error(issues));
    EXPECT_TRUE(contains_substr(issues, "size_start"));
}

TEST(VfxAuthoring, T18_ValidateNegativeSizeEnd)
{
    AuthoredVfx vfx = VfxPresets::fire_smoke();
    vfx.size_end = -1.0F;

    std::vector<std::string> issues;
    const bool ok = validate_authored(vfx, issues);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(has_error(issues));
    EXPECT_TRUE(contains_substr(issues, "size_end"));
}

TEST(VfxAuthoring, T19_ValidateVelocityInversionPerAxis)
{
    // X axis inversion
    {
        AuthoredVfx vfx = VfxPresets::jump_dust();
        vfx.velocity_min[0] = 9.0F;
        std::vector<std::string> issues;
        EXPECT_FALSE(validate_authored(vfx, issues));
        EXPECT_TRUE(contains_substr(issues, "X"));
    }
    // Y axis inversion
    {
        AuthoredVfx vfx = VfxPresets::jump_dust();
        vfx.velocity_min[1] = 5.0F;
        std::vector<std::string> issues;
        EXPECT_FALSE(validate_authored(vfx, issues));
        EXPECT_TRUE(contains_substr(issues, "Y"));
    }
    // Z axis inversion
    {
        AuthoredVfx vfx = VfxPresets::jump_dust();
        vfx.velocity_min[2] = 9.0F;
        std::vector<std::string> issues;
        EXPECT_FALSE(validate_authored(vfx, issues));
        EXPECT_TRUE(contains_substr(issues, "Z"));
    }
}

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
    const auto error_count =
        std::ranges::count_if(issues,
            [](const std::string& s) { return s.starts_with("[ERROR]"); });
    EXPECT_GE(error_count, 4);
}

TEST(VfxAuthoring, T21_LoadFileNotFoundReturnsNullopt)
{
    const std::filesystem::path nonexistent =
        std::filesystem::temp_directory_path() / "cd_vfx_does_not_exist_12345.json";

    std::filesystem::remove(nonexistent);

    const auto result = load_from_json(nonexistent);
    EXPECT_FALSE(result.has_value())
        << "loading a non-existent file must return nullopt";
}

TEST(VfxAuthoring, T22_LoadRootArrayReturnsNullopt)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_root_array.json";

    {
        std::ofstream ofs { tmp };
        ofs << R"([{"id":"x"},{"id":"y"}])";
    }

    const auto result = load_from_json(tmp);
    EXPECT_FALSE(result.has_value())
        << "a JSON root that is not an object must return nullopt";

    std::filesystem::remove(tmp);
}

TEST(VfxAuthoring, T23_LoadEmptyStringIdReturnsNullopt)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_empty_id.json";

    {
        std::ofstream ofs { tmp };
        ofs << R"({"id":"","emitter_kind":"burst","emit_rate_per_sec":10.0,"life_seconds":1.0})";
    }

    const auto result = load_from_json(tmp);
    EXPECT_FALSE(result.has_value())
        << "an empty string id must cause load_from_json to return nullopt";

    std::filesystem::remove(tmp);
}

TEST(VfxAuthoring, T24_LoadPartialColorArrayUsesDefaults)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_partial_color.json";

    {
        std::ofstream ofs { tmp };
        ofs << R"({)"
               R"("id":"partial_color","emitter_kind":"burst",)"
               R"("color_start":[0.5,0.6])"
               R"(})";
    }

    const auto result = load_from_json(tmp);
    ASSERT_TRUE(result.has_value())
        << "partial color array must not cause nullopt";

    EXPECT_NEAR(result->color_start[0], 0.5F, 1e-4F);
    EXPECT_NEAR(result->color_start[1], 0.6F, 1e-4F);
    const AuthoredVfx defaults;
    EXPECT_NEAR(result->color_start[2], defaults.color_start[2], 1e-4F);
    EXPECT_NEAR(result->color_start[3], defaults.color_start[3], 1e-4F);
    for (std::size_t i = 0; i < 4; ++i)
    {
        EXPECT_NEAR(result->color_end[i], defaults.color_end[i], 1e-4F)
            << "color_end[" << i << "] should equal AuthoredVfx default";
    }

    std::filesystem::remove(tmp);
}

// ============================================================
// T25 — T33: FloatCurve
// ============================================================

TEST(VfxAuthoring, T25_FloatCurveEvalEmptyReturnsZero)
{
    const FloatCurve curve;
    EXPECT_FLOAT_EQ(curve.eval(0.0F), 0.0F);
    EXPECT_FLOAT_EQ(curve.eval(0.5F), 0.0F);
    EXPECT_FLOAT_EQ(curve.eval(1.0F), 0.0F);
}

TEST(VfxAuthoring, T26_FloatCurveSingleKeyframe)
{
    FloatCurve curve;
    curve.add_keyframe(0.5F, 3.7F);

    // Any t must return the single value.
    EXPECT_FLOAT_EQ(curve.eval(0.0F), 3.7F);
    EXPECT_FLOAT_EQ(curve.eval(0.5F), 3.7F);
    EXPECT_FLOAT_EQ(curve.eval(1.0F), 3.7F);
}

TEST(VfxAuthoring, T27_FloatCurveTwoKeyframesLinearInterp)
{
    FloatCurve curve;
    curve.add_keyframe(0.0F, 0.0F);
    curve.add_keyframe(1.0F, 2.0F);

    EXPECT_NEAR(curve.eval(0.0F), 0.0F, 1e-5F);
    EXPECT_NEAR(curve.eval(0.5F), 1.0F, 1e-5F);
    EXPECT_NEAR(curve.eval(1.0F), 2.0F, 1e-5F);
    EXPECT_NEAR(curve.eval(0.25F), 0.5F, 1e-5F);
}

TEST(VfxAuthoring, T28_FloatCurveClampBelow)
{
    FloatCurve curve;
    curve.add_keyframe(0.2F, 5.0F);
    curve.add_keyframe(0.8F, 10.0F);

    // t < 0 must clamp to first keyframe.
    EXPECT_NEAR(curve.eval(-1.0F), 5.0F, 1e-5F);
    EXPECT_NEAR(curve.eval(0.0F),  5.0F, 1e-5F);
}

TEST(VfxAuthoring, T29_FloatCurveClampAbove)
{
    FloatCurve curve;
    curve.add_keyframe(0.2F, 5.0F);
    curve.add_keyframe(0.8F, 10.0F);

    // t > 1 must clamp to last keyframe.
    EXPECT_NEAR(curve.eval(2.0F), 10.0F, 1e-5F);
    EXPECT_NEAR(curve.eval(1.0F), 10.0F, 1e-5F);
}

TEST(VfxAuthoring, T30_FloatCurveAddKeyframeMaintainsSortedOrder)
{
    FloatCurve curve;
    // Insert in reverse order.
    curve.add_keyframe(1.0F, 3.0F);
    curve.add_keyframe(0.0F, 1.0F);
    curve.add_keyframe(0.5F, 2.0F);

    // After all inserts keyframes must be sorted by time.
    ASSERT_EQ(curve.keyframes.size(), 3U);
    EXPECT_NEAR(curve.keyframes[0].time, 0.0F, 1e-6F);
    EXPECT_NEAR(curve.keyframes[1].time, 0.5F, 1e-6F);
    EXPECT_NEAR(curve.keyframes[2].time, 1.0F, 1e-6F);

    // Interpolation must be correct with the sorted representation.
    EXPECT_NEAR(curve.eval(0.25F), 1.5F, 1e-5F);
    EXPECT_NEAR(curve.eval(0.75F), 2.5F, 1e-5F);
}

TEST(VfxAuthoring, T31_FloatCurveRoundTrip)
{
    AuthoredVfx vfx = VfxPresets::jump_dust();
    vfx.size_curve.add_keyframe(0.0F, 0.08F);
    vfx.size_curve.add_keyframe(0.5F, 0.20F);
    vfx.size_curve.add_keyframe(1.0F, 0.02F);

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_curve_rt.json";

    ASSERT_TRUE(save_to_json(vfx, tmp));
    const auto loaded = load_from_json(tmp);
    ASSERT_TRUE(loaded.has_value());

    ASSERT_EQ(loaded->size_curve.keyframes.size(), 3U);
    EXPECT_NEAR(loaded->size_curve.keyframes[0].time,  0.0F,  1e-4F);
    EXPECT_NEAR(loaded->size_curve.keyframes[0].value, 0.08F, 1e-4F);
    EXPECT_NEAR(loaded->size_curve.keyframes[1].time,  0.5F,  1e-4F);
    EXPECT_NEAR(loaded->size_curve.keyframes[1].value, 0.20F, 1e-4F);
    EXPECT_NEAR(loaded->size_curve.keyframes[2].time,  1.0F,  1e-4F);
    EXPECT_NEAR(loaded->size_curve.keyframes[2].value, 0.02F, 1e-4F);

    std::filesystem::remove(tmp);
}

TEST(VfxAuthoring, T32_FloatCurveValidateKeyframeTimeOutOfRange)
{
    AuthoredVfx vfx = VfxPresets::jump_dust();
    vfx.size_curve.add_keyframe(-0.1F, 1.0F);  // time < 0 — invalid

    std::vector<std::string> issues;
    const bool ok = validate_authored(vfx, issues);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(has_error(issues));
    EXPECT_TRUE(contains_substr(issues, "size_curve"));
}

TEST(VfxAuthoring, T33_FloatCurveValidateNanValue)
{
    AuthoredVfx vfx = VfxPresets::jump_dust();
    vfx.size_curve.add_keyframe(0.5F, std::numeric_limits<float>::quiet_NaN());

    std::vector<std::string> issues;
    const bool ok = validate_authored(vfx, issues);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(has_error(issues));
    EXPECT_TRUE(contains_substr(issues, "size_curve"));
}

// ============================================================
// T34 — T42: ColorGradient
// ============================================================

TEST(VfxAuthoring, T34_ColorGradientEvalEmptyReturnsZero)
{
    const ColorGradient grad;
    const auto result = grad.eval(0.5F);
    for (float ch : result)
    {
        EXPECT_FLOAT_EQ(ch, 0.0F);
    }
}

TEST(VfxAuthoring, T35_ColorGradientSingleKeyframe)
{
    ColorGradient grad;
    grad.add_keyframe(0.5F, { 0.1F, 0.2F, 0.3F, 0.4F });

    // Any t must return the single keyframe colour.
    const auto at0  = grad.eval(0.0F);
    const auto at05 = grad.eval(0.5F);
    const auto at1  = grad.eval(1.0F);

    const std::array<float, 4> expected { 0.1F, 0.2F, 0.3F, 0.4F };
    for (std::size_t i = 0; i < 4; ++i)
    {
        EXPECT_NEAR(at0[i], expected[i], 1e-5F);
        EXPECT_NEAR(at05[i], expected[i], 1e-5F);
        EXPECT_NEAR(at1[i], expected[i], 1e-5F);
    }
}

TEST(VfxAuthoring, T36_ColorGradientMidpointLerp)
{
    ColorGradient grad;
    grad.add_keyframe(0.0F, { 0.0F, 0.0F, 0.0F, 0.0F });
    grad.add_keyframe(1.0F, { 1.0F, 1.0F, 1.0F, 1.0F });

    const auto mid = grad.eval(0.5F);
    for (float ch : mid)
    {
        EXPECT_NEAR(ch, 0.5F, 1e-5F);
    }
}

TEST(VfxAuthoring, T37_ColorGradientClampBelow)
{
    ColorGradient grad;
    grad.add_keyframe(0.3F, { 1.0F, 0.0F, 0.0F, 1.0F });
    grad.add_keyframe(0.7F, { 0.0F, 1.0F, 0.0F, 1.0F });

    const auto result = grad.eval(-0.5F);
    EXPECT_NEAR(result[0], 1.0F, 1e-5F);
    EXPECT_NEAR(result[1], 0.0F, 1e-5F);
}

TEST(VfxAuthoring, T38_ColorGradientClampAbove)
{
    ColorGradient grad;
    grad.add_keyframe(0.3F, { 1.0F, 0.0F, 0.0F, 1.0F });
    grad.add_keyframe(0.7F, { 0.0F, 1.0F, 0.0F, 1.0F });

    const auto result = grad.eval(2.0F);
    EXPECT_NEAR(result[0], 0.0F, 1e-5F);
    EXPECT_NEAR(result[1], 1.0F, 1e-5F);
}

TEST(VfxAuthoring, T39_ColorGradientAddKeyframeMaintainsSortedOrder)
{
    ColorGradient grad;
    // Insert in reverse order.
    grad.add_keyframe(1.0F, { 1.0F, 1.0F, 1.0F, 1.0F });
    grad.add_keyframe(0.0F, { 0.0F, 0.0F, 0.0F, 0.0F });
    grad.add_keyframe(0.5F, { 0.5F, 0.5F, 0.5F, 0.5F });

    ASSERT_EQ(grad.keyframes.size(), 3U);
    EXPECT_NEAR(grad.keyframes[0].time, 0.0F, 1e-6F);
    EXPECT_NEAR(grad.keyframes[1].time, 0.5F, 1e-6F);
    EXPECT_NEAR(grad.keyframes[2].time, 1.0F, 1e-6F);
}

TEST(VfxAuthoring, T40_ColorGradientRoundTrip)
{
    AuthoredVfx vfx = VfxPresets::fire_smoke();
    vfx.color_gradient.add_keyframe(0.0F, { 0.22F, 0.20F, 0.18F, 0.8F });
    vfx.color_gradient.add_keyframe(1.0F, { 0.10F, 0.10F, 0.10F, 0.0F });

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_gradient_rt.json";

    ASSERT_TRUE(save_to_json(vfx, tmp));
    const auto loaded = load_from_json(tmp);
    ASSERT_TRUE(loaded.has_value());

    ASSERT_EQ(loaded->color_gradient.keyframes.size(), 2U);
    EXPECT_NEAR(loaded->color_gradient.keyframes[0].time,     0.0F,  1e-4F);
    EXPECT_NEAR(loaded->color_gradient.keyframes[0].rgba[0],  0.22F, 1e-4F);
    EXPECT_NEAR(loaded->color_gradient.keyframes[0].rgba[3],  0.8F,  1e-4F);
    EXPECT_NEAR(loaded->color_gradient.keyframes[1].time,     1.0F,  1e-4F);
    EXPECT_NEAR(loaded->color_gradient.keyframes[1].rgba[3],  0.0F,  1e-4F);

    std::filesystem::remove(tmp);
}

TEST(VfxAuthoring, T41_ColorGradientValidateKeyframeTimeOutOfRange)
{
    AuthoredVfx vfx = VfxPresets::jump_dust();
    // time = 1.5 is invalid.
    vfx.color_gradient.add_keyframe(1.5F, { 1.0F, 1.0F, 1.0F, 1.0F });

    std::vector<std::string> issues;
    const bool ok = validate_authored(vfx, issues);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(has_error(issues));
    EXPECT_TRUE(contains_substr(issues, "color_gradient"));
}

TEST(VfxAuthoring, T42_ColorGradientValidateRgbaOutOfRange)
{
    AuthoredVfx vfx = VfxPresets::jump_dust();
    // alpha channel 2.0 is invalid.
    vfx.color_gradient.add_keyframe(0.5F, { 0.5F, 0.5F, 0.5F, 2.0F });

    std::vector<std::string> issues;
    const bool ok = validate_authored(vfx, issues);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(has_error(issues));
    EXPECT_TRUE(contains_substr(issues, "color_gradient"));
}

// ============================================================
// T43 — T47: Schema versioning
// ============================================================

TEST(VfxAuthoring, T43_SaveWritesSchemaVersion)
{
    const AuthoredVfx vfx = VfxPresets::jump_dust();

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_sv_check.json";

    ASSERT_TRUE(save_to_json(vfx, tmp));

    // Read raw file and verify "schema_version" key is present. The stream is
    // scoped so its handle is closed before remove() (Windows locks open files).
    std::string contents;
    {
        std::ifstream ifs { tmp };
        contents.assign(std::istreambuf_iterator<char>{ ifs },
                        std::istreambuf_iterator<char>{});
    }
    EXPECT_NE(contents.find("schema_version"), std::string::npos)
        << "Saved JSON must contain 'schema_version'";

    std::filesystem::remove(tmp);
}

TEST(VfxAuthoring, T44_LoadV0FileNoSchemaVersion)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_v0.json";

    {
        std::ofstream ofs { tmp };
        // No "schema_version" key — v0 legacy format.
        ofs << R"({"id":"legacy_effect","emitter_kind":"burst"})";
    }

    const auto result = load_from_json(tmp);
    ASSERT_TRUE(result.has_value()) << "v0 file (no schema_version) must load successfully";
    EXPECT_EQ(result->schema_version, 0U) << "Missing schema_version key must be stored as 0";
    EXPECT_EQ(result->id, "legacy_effect");

    std::filesystem::remove(tmp);
}

TEST(VfxAuthoring, T45_LoadCurrentSchemaVersionSucceeds)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_current_sv.json";

    {
        std::ofstream ofs { tmp };
        ofs << R"({"schema_version":1,"id":"current_sv","emitter_kind":"burst"})";
    }

    const auto result = load_from_json(tmp);
    ASSERT_TRUE(result.has_value()) << "schema_version == kSchemaVersion must load";
    EXPECT_EQ(result->schema_version, kSchemaVersion);

    std::filesystem::remove(tmp);
}

TEST(VfxAuthoring, T46_LoadFutureSchemaVersionReturnsNullopt)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_future_sv.json";

    {
        std::ofstream ofs { tmp };
        // schema_version = 999 is far in the future.
        ofs << R"({"schema_version":999,"id":"future_effect","emitter_kind":"burst"})";
    }

    const auto result = load_from_json(tmp);
    EXPECT_FALSE(result.has_value())
        << "schema_version > kSchemaVersion must return nullopt";

    std::filesystem::remove(tmp);
}

TEST(VfxAuthoring, T47_ValidateSchemaVersionAheadOfCurrentIsWarning)
{
    AuthoredVfx vfx = VfxPresets::jump_dust();
    vfx.schema_version = kSchemaVersion + 1U;

    std::vector<std::string> issues;
    // Should still return true (WARNING is non-fatal).
    const bool ok = validate_authored(vfx, issues);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(has_warning(issues));
    EXPECT_TRUE(contains_substr(issues, "schema_version"));
}

// ============================================================
// T48 — T51: clone_with_id
// ============================================================

TEST(VfxAuthoring, T48_CloneWithIdSetsNewId)
{
    const AuthoredVfx src = VfxPresets::jump_dust();
    const AuthoredVfx cloned = clone_with_id(src, "jump_dust_variant");
    EXPECT_EQ(cloned.id, "jump_dust_variant");
}

TEST(VfxAuthoring, T49_CloneWithIdPreservesAllOtherFields)
{
    const AuthoredVfx src = VfxPresets::fire_smoke();
    const AuthoredVfx cloned = clone_with_id(src, "fire_smoke_2");

    EXPECT_EQ(cloned.emitter_kind,      src.emitter_kind);
    EXPECT_NEAR(cloned.emit_rate_per_sec, src.emit_rate_per_sec, 1e-6F);
    EXPECT_NEAR(cloned.life_seconds,      src.life_seconds,      1e-6F);
    EXPECT_NEAR(cloned.size_start,        src.size_start,        1e-6F);
    EXPECT_NEAR(cloned.size_end,          src.size_end,          1e-6F);
    EXPECT_EQ(cloned.texture_path, src.texture_path);
    for (std::size_t i = 0; i < 4; ++i)
    {
        EXPECT_NEAR(cloned.color_start[i], src.color_start[i], 1e-6F);
        EXPECT_NEAR(cloned.color_end[i],   src.color_end[i],   1e-6F);
    }
    for (std::size_t i = 0; i < 3; ++i)
    {
        EXPECT_NEAR(cloned.velocity_min[i], src.velocity_min[i], 1e-6F);
        EXPECT_NEAR(cloned.velocity_max[i], src.velocity_max[i], 1e-6F);
    }
}

TEST(VfxAuthoring, T50_CloneWithIdMutationDoesNotAffectOriginal)
{
    const AuthoredVfx src = VfxPresets::muzzle_flash();
    AuthoredVfx cloned = clone_with_id(src, "muzzle_flash_low");
    cloned.emit_rate_per_sec = 1.0F;

    // Original must be unchanged.
    EXPECT_NEAR(src.emit_rate_per_sec, 120.0F, 1e-4F);
}

TEST(VfxAuthoring, T51_CloneWithIdDeepCopiesCurveAndGradient)
{
    AuthoredVfx src = VfxPresets::jump_dust();
    src.size_curve.add_keyframe(0.0F, 0.1F);
    src.size_curve.add_keyframe(1.0F, 0.5F);
    src.color_gradient.add_keyframe(0.0F, { 1.0F, 0.0F, 0.0F, 1.0F });
    src.color_gradient.add_keyframe(1.0F, { 0.0F, 0.0F, 1.0F, 0.0F });

    AuthoredVfx cloned = clone_with_id(src, "jump_dust_blue");

    // Mutate clone's curve — original must be unaffected.
    cloned.size_curve.keyframes[0].value = 99.0F;
    EXPECT_NEAR(src.size_curve.keyframes[0].value, 0.1F, 1e-5F);

    // Gradient deep-copy check.
    EXPECT_EQ(cloned.color_gradient.keyframes.size(), 2U);
    EXPECT_NEAR(cloned.color_gradient.keyframes[0].rgba[0], 1.0F, 1e-5F);
}

// ============================================================
// T52 — T67: VfxLibrary
// ============================================================

TEST(VfxAuthoring, T52_LibraryInitiallyEmpty)
{
    const VfxLibrary lib;
    EXPECT_TRUE(lib.empty());
    EXPECT_EQ(lib.size(), 0U);
}

TEST(VfxAuthoring, T53_LibraryAddEffectIsRetrievable)
{
    VfxLibrary lib;
    ASSERT_TRUE(lib.add(VfxPresets::jump_dust()));
    EXPECT_EQ(lib.size(), 1U);
    const auto* found = lib.find("jump_dust");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, "jump_dust");
}

TEST(VfxAuthoring, T54_LibraryAddDuplicateIdOverwrites)
{
    VfxLibrary lib;
    ASSERT_TRUE(lib.add(VfxPresets::jump_dust()));

    AuthoredVfx variant = VfxPresets::jump_dust();
    variant.emit_rate_per_sec = 999.0F;
    ASSERT_TRUE(lib.add(std::move(variant)));

    // Size must remain 1 — not 2.
    EXPECT_EQ(lib.size(), 1U);
    const auto* found = lib.find("jump_dust");
    ASSERT_NE(found, nullptr);
    EXPECT_NEAR(found->emit_rate_per_sec, 999.0F, 1e-4F);
}

TEST(VfxAuthoring, T55_LibraryAddEmptyIdRejected)
{
    VfxLibrary lib;
    AuthoredVfx bad;  // id is empty by default
    bad.emitter_kind = "burst";

    const bool ok = lib.add(std::move(bad));
    EXPECT_FALSE(ok);
    EXPECT_TRUE(lib.empty());
}

TEST(VfxAuthoring, T56_LibraryRemoveReturnsTrueAndDecreasesSize)
{
    VfxLibrary lib;
    ASSERT_TRUE(lib.add(VfxPresets::jump_dust()));
    ASSERT_TRUE(lib.add(VfxPresets::muzzle_flash()));
    EXPECT_EQ(lib.size(), 2U);

    EXPECT_TRUE(lib.remove("jump_dust"));
    EXPECT_EQ(lib.size(), 1U);
    EXPECT_EQ(lib.find("jump_dust"), nullptr);
    EXPECT_NE(lib.find("muzzle_flash"), nullptr);
}

TEST(VfxAuthoring, T57_LibraryRemoveUnknownIdReturnsFalse)
{
    VfxLibrary lib;
    ASSERT_TRUE(lib.add(VfxPresets::fire_smoke()));

    EXPECT_FALSE(lib.remove("nonexistent_effect"));
    EXPECT_EQ(lib.size(), 1U);
}

TEST(VfxAuthoring, T58_LibraryFindNullptrForUnknownId)
{
    const VfxLibrary lib;
    EXPECT_EQ(lib.find("nothing"), nullptr);
}

TEST(VfxAuthoring, T59_LibrarySizeAndEmptyConsistent)
{
    VfxLibrary lib;
    EXPECT_TRUE(lib.empty());
    EXPECT_EQ(lib.size(), 0U);

    ASSERT_TRUE(lib.add(VfxPresets::water_splash()));
    EXPECT_FALSE(lib.empty());
    EXPECT_EQ(lib.size(), 1U);

    lib.remove("water_splash");
    EXPECT_TRUE(lib.empty());
    EXPECT_EQ(lib.size(), 0U);
}

TEST(VfxAuthoring, T60_LibraryEffectsIteratesAll)
{
    VfxLibrary lib;
    ASSERT_TRUE(lib.add(VfxPresets::jump_dust()));
    ASSERT_TRUE(lib.add(VfxPresets::muzzle_flash()));
    ASSERT_TRUE(lib.add(VfxPresets::fire_smoke()));

    const auto& all = lib.effects();
    EXPECT_EQ(all.size(), 3U);

    const bool has_dust   = std::ranges::any_of(all, [](const AuthoredVfx& e){ return e.id == "jump_dust"; });
    const bool has_flash  = std::ranges::any_of(all, [](const AuthoredVfx& e){ return e.id == "muzzle_flash"; });
    const bool has_smoke  = std::ranges::any_of(all, [](const AuthoredVfx& e){ return e.id == "fire_smoke"; });
    EXPECT_TRUE(has_dust);
    EXPECT_TRUE(has_flash);
    EXPECT_TRUE(has_smoke);
}

TEST(VfxAuthoring, T61_LibrarySaveLoadRoundTripAllPresent)
{
    VfxLibrary lib;
    ASSERT_TRUE(lib.add(VfxPresets::jump_dust()));
    ASSERT_TRUE(lib.add(VfxPresets::muzzle_flash()));
    ASSERT_TRUE(lib.add(VfxPresets::fire_smoke()));
    ASSERT_TRUE(lib.add(VfxPresets::water_splash()));

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_lib_rt.vfxlib.json";

    ASSERT_TRUE(lib.save(tmp));

    VfxLibrary loaded;
    std::vector<std::string> issues;
    ASSERT_TRUE(loaded.load(tmp, issues));
    EXPECT_FALSE(has_error(issues)) << "No errors expected loading a valid library";

    EXPECT_EQ(loaded.size(), 4U);
    EXPECT_NE(loaded.find("jump_dust"),    nullptr);
    EXPECT_NE(loaded.find("muzzle_flash"), nullptr);
    EXPECT_NE(loaded.find("fire_smoke"),   nullptr);
    EXPECT_NE(loaded.find("water_splash"), nullptr);

    std::filesystem::remove(tmp);
}

TEST(VfxAuthoring, T62_LibrarySaveLoadFieldValuesPreserved)
{
    VfxLibrary lib;
    ASSERT_TRUE(lib.add(VfxPresets::fire_smoke()));

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_lib_field_rt.vfxlib.json";

    ASSERT_TRUE(lib.save(tmp));

    VfxLibrary loaded;
    std::vector<std::string> issues;
    ASSERT_TRUE(loaded.load(tmp, issues));

    const auto* e = loaded.find("fire_smoke");
    ASSERT_NE(e, nullptr);
    const AuthoredVfx expected = VfxPresets::fire_smoke();
    EXPECT_EQ(e->emitter_kind, expected.emitter_kind);
    EXPECT_NEAR(e->emit_rate_per_sec, expected.emit_rate_per_sec, 1e-4F);
    EXPECT_NEAR(e->life_seconds,      expected.life_seconds,      1e-4F);
    EXPECT_EQ(e->texture_path, expected.texture_path);

    std::filesystem::remove(tmp);
}

TEST(VfxAuthoring, T63_LibraryLoadMissingEffectsKeyReturnsFalse)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_lib_no_effects.json";

    {
        std::ofstream ofs { tmp };
        ofs << R"({"schema_version":1})";  // no "effects" array
    }

    VfxLibrary lib;
    std::vector<std::string> issues;
    EXPECT_FALSE(lib.load(tmp, issues));
    EXPECT_TRUE(has_error(issues));

    std::filesystem::remove(tmp);
}

TEST(VfxAuthoring, T64_LibraryLoadFutureVersionEffectIsSkippedWithWarning)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_lib_future_entry.json";

    {
        std::ofstream ofs { tmp };
        // First entry is valid v1; second entry has schema_version 999 (future).
        ofs << R"({)"
               R"("schema_version":1,)"
               R"("effects":[)"
               R"({"schema_version":1,"id":"good_effect","emitter_kind":"burst"},)"
               R"({"schema_version":999,"id":"future_effect","emitter_kind":"burst"})"
               R"(]})";
    }

    VfxLibrary lib;
    std::vector<std::string> issues;
    ASSERT_TRUE(lib.load(tmp, issues));  // load still returns true

    EXPECT_EQ(lib.size(), 1U);          // only the valid entry
    EXPECT_NE(lib.find("good_effect"),   nullptr);
    EXPECT_EQ(lib.find("future_effect"), nullptr);
    EXPECT_TRUE(has_warning(issues));   // warning about the skipped entry

    std::filesystem::remove(tmp);
}

TEST(VfxAuthoring, T65_LibraryLoadFileNotFoundReturnsFalse)
{
    const std::filesystem::path nonexistent =
        std::filesystem::temp_directory_path() / "cd_vfx_lib_no_file_999.vfxlib.json";
    std::filesystem::remove(nonexistent);

    VfxLibrary lib;
    std::vector<std::string> issues;
    EXPECT_FALSE(lib.load(nonexistent, issues));
    EXPECT_TRUE(has_error(issues));
}

TEST(VfxAuthoring, T66_LibraryLoadMalformedJsonReturnsFalse)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_lib_bad.vfxlib.json";

    {
        std::ofstream ofs { tmp };
        ofs << R"({"schema_version":1,"effects":[{)";  // unterminated
    }

    VfxLibrary lib;
    std::vector<std::string> issues;
    EXPECT_FALSE(lib.load(tmp, issues));
    EXPECT_TRUE(has_error(issues));

    std::filesystem::remove(tmp);
}

TEST(VfxAuthoring, T67_LibraryLoadRootNotObjectReturnsFalse)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cd_vfx_lib_root_array.vfxlib.json";

    {
        std::ofstream ofs { tmp };
        ofs << R"([1,2,3])";  // root is array, not object
    }

    VfxLibrary lib;
    std::vector<std::string> issues;
    EXPECT_FALSE(lib.load(tmp, issues));
    EXPECT_TRUE(has_error(issues));

    std::filesystem::remove(tmp);
}

// ============================================================
// T68 — T71: Validation warnings
// ============================================================

TEST(VfxAuthoring, T68_ValidateHighEmitRateWarning)
{
    AuthoredVfx vfx = VfxPresets::jump_dust();
    vfx.emit_rate_per_sec = 15000.0F;  // > 10000 threshold

    std::vector<std::string> issues;
    // Must return true (no error), but emit a warning.
    const bool ok = validate_authored(vfx, issues);

    EXPECT_TRUE(ok);
    EXPECT_FALSE(has_error(issues));
    EXPECT_TRUE(has_warning(issues));
    EXPECT_TRUE(contains_substr(issues, "10000"));
}

TEST(VfxAuthoring, T69_ValidateLongLifeWarning)
{
    AuthoredVfx vfx = VfxPresets::fire_smoke();
    vfx.life_seconds = 120.0F;  // > 60 s threshold

    std::vector<std::string> issues;
    const bool ok = validate_authored(vfx, issues);

    EXPECT_TRUE(ok);
    EXPECT_FALSE(has_error(issues));
    EXPECT_TRUE(has_warning(issues));
    EXPECT_TRUE(contains_substr(issues, "60"));
}

TEST(VfxAuthoring, T70_ValidateUnknownEmitterKindWarning)
{
    AuthoredVfx vfx = VfxPresets::jump_dust();
    vfx.emitter_kind = "ribbon";  // not in known set

    std::vector<std::string> issues;
    const bool ok = validate_authored(vfx, issues);

    EXPECT_TRUE(ok);
    EXPECT_FALSE(has_error(issues));
    EXPECT_TRUE(has_warning(issues));
    EXPECT_TRUE(contains_substr(issues, "ribbon"));
}

TEST(VfxAuthoring, T71_ValidateKnownEmitterKindsNoWarning)
{
    // All three known kinds must be warning-free.
    for (std::string_view kind : std::array<std::string_view, 3>{ "burst", "continuous", "one_shot" })
    {
        AuthoredVfx vfx = VfxPresets::jump_dust();
        vfx.emitter_kind = std::string { kind };

        std::vector<std::string> issues;
        static_cast<void>(validate_authored(vfx, issues));

        // Filter only warnings that mention emitter_kind to avoid false
        // positives from other warning checks.
        const bool emitter_kind_warning = std::ranges::any_of(
            issues,
            [](const std::string& s)
            { return s.starts_with("[WARNING]") && s.find("emitter_kind") != std::string::npos; });

        EXPECT_FALSE(emitter_kind_warning) << "Known kind '" << kind << "' must not produce an emitter_kind [WARNING]";
    }
}

}  // namespace
