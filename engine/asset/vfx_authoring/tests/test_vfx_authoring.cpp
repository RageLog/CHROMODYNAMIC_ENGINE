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
// =============================================================================

#include <cd/asset/vfx_authoring/VfxAuthoring.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
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
    return std::any_of(issues.begin(), issues.end(),
                       [](const std::string& s)
                       { return s.rfind("[ERROR]", 0) == 0; });
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
    EXPECT_TRUE(std::any_of(issues.begin(), issues.end(),
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

}  // namespace
