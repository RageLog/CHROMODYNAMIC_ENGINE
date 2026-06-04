// =============================================================================
// CHROMODYNAMIC — engine/asset/material_authoring/tests/test_material_authoring.cpp
// Phase 644 — cd::asset::material_authoring unit tests
//
// Tests:
//   T1  Presets: dielectric / metal / cloth have expected PBR values and pass
//       validate_authored() with no issues.
//   T2  JSON round-trip: save_to_json + load_from_json -> identical fields.
//   T3  validate_authored: empty id -> error reported, returns false.
//   T4  validate_authored: out-of-range metallic/roughness -> error reported.
//   T5  validate_authored: MASK mode + alpha_cutoff == 0 -> warning (not error).
//   T6  validate_authored: BLEND mode -> info note about alpha_cutoff, no error.
//   T7  load_from_json: missing 'id' field -> returns nullopt.
//   T8  load_from_json: missing optional fields -> falls back to defaults
//       (metallic=0, roughness=0.5, alpha_mode=OPAQUE).
// =============================================================================

#include <cd/asset/material_authoring/MaterialAuthoring.hpp>
#include <cd/material/AlphaMode.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>
#include <vector>

namespace
{

using cd::asset::material_authoring::AuthoredMaterial;
using cd::asset::material_authoring::AuthoringDefaults;
using cd::asset::material_authoring::load_from_json;
using cd::asset::material_authoring::save_to_json;
using cd::asset::material_authoring::validate_authored;

// ---- helpers ----------------------------------------------------------------

bool has_prefix(const std::vector<std::string>& issues, std::string_view prefix)
{
    return std::ranges::any_of(issues,
                       [prefix](const std::string& s)
                       { return s.starts_with(prefix); });
}

bool has_error(const std::vector<std::string>& issues)
{
    return has_prefix(issues, "[ERROR]");
}

bool has_warning(const std::vector<std::string>& issues)
{
    return has_prefix(issues, "[WARNING]");
}

bool has_info(const std::vector<std::string>& issues)
{
    return has_prefix(issues, "[INFO]");
}

// ---- T1 — Presets -----------------------------------------------------------

TEST(MaterialAuthoring, PresetsPassValidation)
{
    const auto dielectric = AuthoringDefaults::dielectric();
    const auto metal      = AuthoringDefaults::metal();
    const auto cloth      = AuthoringDefaults::cloth();

    // Dielectric: non-metallic, mid roughness.
    EXPECT_EQ(dielectric.metallic, 0.0F);
    EXPECT_GT(dielectric.roughness, 0.3F);
    EXPECT_LT(dielectric.roughness, 0.9F);
    EXPECT_FALSE(dielectric.id.empty());

    // Metal: full metallic, low roughness.
    EXPECT_EQ(metal.metallic, 1.0F);
    EXPECT_LT(metal.roughness, 0.3F);

    // Cloth: non-metallic, very high roughness.
    EXPECT_EQ(cloth.metallic, 0.0F);
    EXPECT_GT(cloth.roughness, 0.8F);

    // All presets validate clean.
    for (const auto* preset : { &dielectric, &metal, &cloth })
    {
        std::vector<std::string> issues;
        const bool ok = validate_authored(*preset, issues);
        EXPECT_TRUE(ok)
            << "Preset '" << preset->id << "' failed validation.";
        EXPECT_FALSE(has_error(issues))
            << "Preset '" << preset->id << "' has unexpected error(s).";
    }
}

// ---- T2 — JSON round-trip ---------------------------------------------------

TEST(MaterialAuthoring, JsonRoundTrip)
{
    AuthoredMaterial original;
    original.id                  = "mat_roundtrip_test";
    original.base_color          = { 0.2F, 0.4F, 0.8F };
    original.metallic            = 0.75F;
    original.roughness           = 0.3F;
    original.albedo_texture_path = "textures/albedo.png";
    original.normal_texture_path = "textures/normal.png";
    original.mr_texture_path     = "textures/mr.png";
    original.alpha_mode          = cd::material::AlphaMode::kMask;
    original.alpha_cutoff        = 0.4F;

    // Write to a temp file.
    const auto tmp = std::filesystem::temp_directory_path() / "cd_mat_rt_test.material.json";
    ASSERT_TRUE(save_to_json(original, tmp))
        << "save_to_json failed for path: " << tmp;

    // Read back.
    const auto loaded_opt = load_from_json(tmp);
    ASSERT_TRUE(loaded_opt.has_value()) << "load_from_json returned nullopt.";
    const AuthoredMaterial& loaded = *loaded_opt;

    EXPECT_EQ(loaded.id,                  original.id);
    EXPECT_NEAR(loaded.base_color[0],     original.base_color[0], 1e-5F);
    EXPECT_NEAR(loaded.base_color[1],     original.base_color[1], 1e-5F);
    EXPECT_NEAR(loaded.base_color[2],     original.base_color[2], 1e-5F);
    EXPECT_NEAR(loaded.metallic,          original.metallic,       1e-5F);
    EXPECT_NEAR(loaded.roughness,         original.roughness,      1e-5F);
    EXPECT_EQ(loaded.albedo_texture_path, original.albedo_texture_path);
    EXPECT_EQ(loaded.normal_texture_path, original.normal_texture_path);
    EXPECT_EQ(loaded.mr_texture_path,     original.mr_texture_path);
    EXPECT_EQ(loaded.alpha_mode,          original.alpha_mode);
    EXPECT_NEAR(loaded.alpha_cutoff,      original.alpha_cutoff, 1e-5F);

    // Cleanup.
    std::filesystem::remove(tmp);
}

// ---- T3 — Empty id ----------------------------------------------------------

TEST(MaterialAuthoring, EmptyIdIsError)
{
    AuthoredMaterial mat;
    mat.id       = "";  // intentionally invalid
    mat.metallic = 0.0F;
    mat.roughness = 0.5F;

    std::vector<std::string> issues;
    const bool ok = validate_authored(mat, issues);

    EXPECT_FALSE(ok) << "Expected validate_authored to return false for empty id.";
    EXPECT_TRUE(has_error(issues)) << "Expected at least one [ERROR] for empty id.";
}

// ---- T4 — Out-of-range metallic/roughness -----------------------------------

TEST(MaterialAuthoring, OutOfRangeScalarsAreErrors)
{
    AuthoredMaterial mat;
    mat.id        = "mat_bad_scalars";
    mat.metallic  = 1.5F;   // too high
    mat.roughness = -0.1F;  // too low

    std::vector<std::string> issues;
    const bool ok = validate_authored(mat, issues);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(has_error(issues));

    // Should have at least two [ERROR] entries (one per bad field).
    const auto error_count = std::ranges::count_if(
        issues,
        [](const std::string& s) { return s.starts_with("[ERROR]"); });
    EXPECT_GE(error_count, 2);
}

// ---- T5 — MASK + alpha_cutoff == 0 is warning, not error -------------------

TEST(MaterialAuthoring, MaskCutoffZeroIsWarningNotError)
{
    AuthoredMaterial mat = AuthoringDefaults::dielectric();
    mat.id          = "mat_mask_zero_cutoff";
    mat.alpha_mode  = cd::material::AlphaMode::kMask;
    mat.alpha_cutoff = 0.0F;  // suspicious but not a hard error

    std::vector<std::string> issues;
    const bool ok = validate_authored(mat, issues);

    // alpha_cutoff=0 means all fragments discarded — warn, don't error.
    EXPECT_TRUE(ok) << "MASK alpha_cutoff=0 should be a warning, not an error.";
    EXPECT_FALSE(has_error(issues));
    EXPECT_TRUE(has_warning(issues));
}

// ---- T6 — BLEND with non-default alpha_cutoff is info ----------------------

TEST(MaterialAuthoring, BlendModeAlphaCutoffInfo)
{
    AuthoredMaterial mat = AuthoringDefaults::cloth();
    mat.id          = "mat_blend_info";
    mat.alpha_mode  = cd::material::AlphaMode::kBlend;
    mat.alpha_cutoff = 0.3F;  // has no effect in BLEND mode

    std::vector<std::string> issues;
    const bool ok = validate_authored(mat, issues);

    EXPECT_TRUE(ok);
    EXPECT_FALSE(has_error(issues));
    EXPECT_TRUE(has_info(issues));
}

// ---- T7 — load missing id -> nullopt ----------------------------------------

TEST(MaterialAuthoring, MissingIdReturnsNullopt)
{
    // Craft a JSON file with no "id" field.
    const auto tmp = std::filesystem::temp_directory_path() / "cd_mat_no_id.material.json";
    {
        std::ofstream ofs { tmp, std::ios::binary };
        ASSERT_TRUE(ofs.is_open());
        ofs << R"({"metallic":0.0,"roughness":0.5})";
    }

    const auto result = load_from_json(tmp);
    EXPECT_FALSE(result.has_value()) << "Expected nullopt when 'id' is absent.";

    std::filesystem::remove(tmp);
}

// ---- T8 — missing optional fields fall back to defaults ---------------------

TEST(MaterialAuthoring, MissingOptionalFieldsUseDefaults)
{
    // Minimal valid JSON: only the required 'id' field.
    const auto tmp = std::filesystem::temp_directory_path() / "cd_mat_minimal.material.json";
    {
        std::ofstream ofs { tmp, std::ios::binary };
        ASSERT_TRUE(ofs.is_open());
        ofs << R"({"id":"mat_minimal"})";
    }

    const auto result = load_from_json(tmp);
    ASSERT_TRUE(result.has_value()) << "Expected successful load for minimal JSON.";

    const AuthoredMaterial& mat = *result;
    EXPECT_EQ(mat.id, "mat_minimal");
    EXPECT_NEAR(mat.metallic,     0.0F, 1e-6F);
    EXPECT_NEAR(mat.roughness,    0.5F, 1e-6F);
    EXPECT_EQ(mat.alpha_mode,     cd::material::AlphaMode::kOpaque);
    EXPECT_NEAR(mat.alpha_cutoff, 0.5F, 1e-6F);
    EXPECT_TRUE(mat.albedo_texture_path.empty());
    EXPECT_TRUE(mat.normal_texture_path.empty());
    EXPECT_TRUE(mat.mr_texture_path.empty());

    std::filesystem::remove(tmp);
}

}  // namespace
