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
//
// BAND-3 edge tests (phase1241 — seal-the-v1, lock-the-untested-branches):
//   T9  load_from_json: malformed JSON (parse error) -> nullopt (the json::load
//       failure path was never exercised — every prior load test fed valid JSON).
//   T10 load_from_json: 'id' present but wrong type (number, not string) -> nullopt
//       (the `!(*id_r)->is_string()` half of the id guard; T7 only covered absent).
//   T11 save_to_json: un-openable path -> returns false (the ofstream-not-open
//       branch; T2 only ever exercised the success path).
//   T12 validate_authored: NaN scalar -> [ERROR] (the std::isnan limb of each
//       range check; prior tests used finite out-of-range values only).
//   T13 validate_authored: out-of-range base_color channel -> [ERROR]
//       (check #2 was never driven out of [0,1] by any prior test).
//
// SCHEMA VERSIONING + VALIDATION DEPTH (depth-100 gap closure):
//   T14  save_to_json writes schema_version field; load_from_json reads it back
//        as kCurrentSchemaVersion (round-trip correctness for new field).
//   T15  load_from_json: JSON with future schema_version (kCurrentSchemaVersion+1)
//        -> nullopt (hard version gate).
//   T16  load_from_json: JSON absent schema_version -> loads ok, schema_version=0
//        (backward compatibility: legacy files still load).
//   T17  validate_authored: schema_version==0 -> [INFO] (legacy note, not error).
//   T18  validate_authored: schema_version > kCurrentSchemaVersion (in-process
//        constructed bogus value) -> [WARNING], not error.
//   T19  load_from_json: JSON with unknown field -> loads ok, unknown_fields
//        populated; validate_authored -> [WARNING] per unknown key.
//   T20  validate_authored: multiple unknown fields -> one [WARNING] each.
//   T21  load_from_json + validate_authored: all-defaults material (only "id"
//        + "schema_version") round-trips and validates clean.
//   T22  load_from_json: truncated JSON body (valid prefix but incomplete) ->
//        nullopt (parse error path via the engine JSON parser).
//   T23  validate_authored: alpha_cutoff out-of-range loaded from JSON -> [ERROR]
//        (load does NOT clamp; validate catches the bad value).
//   T24  load_from_json + validate_authored: empty material ("id":"","schema_version":1)
//        -> loads but id is empty -> nullopt (id-empty guard fires at load time).
//   T25  validate_authored: all-defaults in-process material -> clean (no issues).
//   T26  JSON round-trip: BLEND alpha_mode survives save + load correctly.
//   T27  JSON round-trip: schema_version NOT written for materials whose
//        schema_version field is loaded as 0 but re-saved writes kCurrentSchemaVersion
//        (save always emits canonical version).
// =============================================================================

#include <cd/asset/material_authoring/MaterialAuthoring.hpp>
#include <cd/material/AlphaMode.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
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

// ============================================================================
// BAND-3 edge tests — lock untested real branches
// ============================================================================

// ---- T9 — malformed JSON -> nullopt -----------------------------------------
// Exercises the `if (!result) return nullopt` parse-failure path in
// load_from_json(); every prior load test fed syntactically valid JSON.

TEST(MaterialAuthoring, MalformedJsonReturnsNullopt)
{
    const auto tmp = std::filesystem::temp_directory_path() / "cd_mat_malformed.material.json";
    {
        std::ofstream ofs { tmp, std::ios::binary };
        ASSERT_TRUE(ofs.is_open());
        ofs << R"({"id": "broken", "metallic": )";  // truncated — unterminated object
    }

    const auto result = load_from_json(tmp);
    EXPECT_FALSE(result.has_value()) << "Expected nullopt for malformed JSON.";

    std::filesystem::remove(tmp);
}

// ---- T10 — 'id' present but wrong type -> nullopt ----------------------------
// Exercises the `!(*id_r)->is_string()` half of the required-id guard. T7 only
// covered an ABSENT id; here the key exists but holds a number.

TEST(MaterialAuthoring, IdWrongTypeReturnsNullopt)
{
    const auto tmp = std::filesystem::temp_directory_path() / "cd_mat_id_number.material.json";
    {
        std::ofstream ofs { tmp, std::ios::binary };
        ASSERT_TRUE(ofs.is_open());
        ofs << R"({"id": 42, "roughness": 0.5})";  // id is a number, not a string
    }

    const auto result = load_from_json(tmp);
    EXPECT_FALSE(result.has_value()) << "Expected nullopt when 'id' is not a string.";

    std::filesystem::remove(tmp);
}

// ---- T11 — save to an un-openable path -> false -----------------------------
// Exercises the `if (!ofs.is_open()) return false` branch. A path whose parent
// directory does not exist cannot be opened for writing.

TEST(MaterialAuthoring, SaveToUnopenablePathReturnsFalse)
{
    const auto bad = std::filesystem::temp_directory_path() /
                     "cd_mat_no_such_dir_zzz" / "deeper" / "out.material.json";
    // Ensure the parent directory really is absent.
    std::filesystem::remove_all(std::filesystem::temp_directory_path() /
                                "cd_mat_no_such_dir_zzz");

    const auto mat = AuthoringDefaults::dielectric();
    EXPECT_FALSE(save_to_json(mat, bad))
        << "Expected save_to_json to return false for an un-openable path.";
}

// ---- T12 — NaN scalar -> [ERROR] --------------------------------------------
// Exercises the std::isnan limb of the metallic/roughness range checks, which
// no prior test reached (T4 used finite 1.5 / -0.1 only).

TEST(MaterialAuthoring, NanScalarIsError)
{
    AuthoredMaterial mat;
    mat.id        = "mat_nan";
    mat.metallic  = std::numeric_limits<float>::quiet_NaN();
    mat.roughness = 0.5F;

    std::vector<std::string> issues;
    const bool ok = validate_authored(mat, issues);

    EXPECT_FALSE(ok) << "NaN metallic must be reported as an error.";
    EXPECT_TRUE(has_error(issues));
}

// ---- T13 — out-of-range base_color channel -> [ERROR] -----------------------
// Exercises validation check #2 (base_color in [0,1]); no prior test pushed a
// colour channel outside the unit range.

TEST(MaterialAuthoring, OutOfRangeBaseColorIsError)
{
    AuthoredMaterial mat;
    mat.id         = "mat_bad_color";
    mat.base_color = { 1.4F, 0.5F, -0.2F };  // R too high, B too low
    mat.metallic   = 0.0F;
    mat.roughness  = 0.5F;

    std::vector<std::string> issues;
    const bool ok = validate_authored(mat, issues);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(has_error(issues));

    const auto error_count = std::ranges::count_if(
        issues,
        [](const std::string& s) { return s.starts_with("[ERROR]"); });
    EXPECT_GE(error_count, 2);  // one per offending channel
}

// ============================================================================
// SCHEMA VERSIONING + VALIDATION DEPTH — depth-100 gap closure
// ============================================================================

// ---- T14 — round-trip preserves schema_version field -----------------------
// save_to_json writes "schema_version": kCurrentSchemaVersion.
// load_from_json reads it back and sets AuthoredMaterial::schema_version.

TEST(MaterialAuthoring, SchemaVersionRoundTrip)
{
    using cd::asset::material_authoring::kCurrentSchemaVersion;

    const auto tmp = std::filesystem::temp_directory_path() /
                     "cd_mat_schema_ver_rt.material.json";
    const auto mat = AuthoringDefaults::metal();
    ASSERT_TRUE(save_to_json(mat, tmp));

    const auto loaded_opt = load_from_json(tmp);
    ASSERT_TRUE(loaded_opt.has_value());
    EXPECT_EQ(loaded_opt->schema_version, kCurrentSchemaVersion);

    std::filesystem::remove(tmp);
}

// ---- T15 — future schema_version -> nullopt (hard version gate) ------------
// A file with schema_version > kCurrentSchemaVersion must be rejected so that
// an old engine build never silently misinterprets a newer format.

TEST(MaterialAuthoring, FutureSchemaVersionRejectsLoad)
{
    using cd::asset::material_authoring::kCurrentSchemaVersion;

    const auto tmp = std::filesystem::temp_directory_path() /
                     "cd_mat_future_ver.material.json";
    {
        std::ofstream ofs { tmp, std::ios::binary };
        ASSERT_TRUE(ofs.is_open());
        // Write a schema_version one step ahead of the current engine version.
        const auto future_ver = static_cast<std::uint32_t>(kCurrentSchemaVersion + 1U);
        ofs << R"({"schema_version":)" << future_ver
            << R"(,"id":"mat_future"})";
    }

    const auto result = load_from_json(tmp);
    EXPECT_FALSE(result.has_value())
        << "Expected nullopt when schema_version > kCurrentSchemaVersion.";

    std::filesystem::remove(tmp);
}

// ---- T16 — absent schema_version -> loads ok, schema_version set to 0 ------
// Legacy files (pre-v1) have no schema_version key.  They must still load to
// preserve backward compatibility.  The field is set to 0 by the loader to
// signal "legacy" for downstream diagnostic surfaces.

TEST(MaterialAuthoring, AbsentSchemaVersionLoadsAsLegacy)
{
    const auto tmp = std::filesystem::temp_directory_path() /
                     "cd_mat_no_schema_ver.material.json";
    {
        std::ofstream ofs { tmp, std::ios::binary };
        ASSERT_TRUE(ofs.is_open());
        ofs << R"({"id":"mat_legacy","metallic":0.0,"roughness":0.5})";
    }

    const auto result = load_from_json(tmp);
    ASSERT_TRUE(result.has_value()) << "Legacy file (no schema_version) must load.";
    EXPECT_EQ(result->schema_version, 0U) << "Absent schema_version should read as 0.";
    EXPECT_EQ(result->id, "mat_legacy");

    std::filesystem::remove(tmp);
}

// ---- T17 — validate_authored: schema_version==0 -> [INFO] ------------------
// A legacy mat (schema_version=0) should produce an informational note — not
// an error — so that the hot-reload path can surface it without hard-failing.

TEST(MaterialAuthoring, LegacySchemaVersionEmitsInfo)
{
    AuthoredMaterial mat = AuthoringDefaults::dielectric();
    mat.schema_version   = 0U;  // simulate a legacy in-process material

    std::vector<std::string> issues;
    const bool ok = validate_authored(mat, issues);

    EXPECT_TRUE(ok) << "schema_version=0 must not be an error.";
    EXPECT_FALSE(has_error(issues));
    EXPECT_TRUE(has_info(issues))
        << "Expected an [INFO] note for legacy schema_version=0.";
}

// ---- T18 — validate_authored: schema_version > kCurrentSchemaVersion --------
// If an AuthoredMaterial is constructed in-process with schema_version beyond
// the current engine version (e.g. a unit test or migration tool), validate
// should warn but not error — the hard gate is at load_from_json time.

TEST(MaterialAuthoring, FutureSchemaVersionInProcessEmitsWarning)
{
    using cd::asset::material_authoring::kCurrentSchemaVersion;

    AuthoredMaterial mat = AuthoringDefaults::cloth();
    mat.schema_version   = kCurrentSchemaVersion + 1U;

    std::vector<std::string> issues;
    const bool ok = validate_authored(mat, issues);

    EXPECT_TRUE(ok) << "Future schema_version in-process must not be an error.";
    EXPECT_FALSE(has_error(issues));
    EXPECT_TRUE(has_warning(issues))
        << "Expected a [WARNING] for schema_version > kCurrentSchemaVersion.";
}

// ---- T19 — load_from_json with one unknown field -> loads + warning ---------
// The engine tolerates unknown JSON keys so that files authored by a newer
// design tool still load.  Unknown keys should be collected in unknown_fields,
// and validate_authored should emit one [WARNING] per unknown key.

TEST(MaterialAuthoring, UnknownFieldToleratedAndWarned)
{
    const auto tmp = std::filesystem::temp_directory_path() /
                     "cd_mat_unknown_field.material.json";
    {
        std::ofstream ofs { tmp, std::ios::binary };
        ASSERT_TRUE(ofs.is_open());
        // "emissive_scale" is not a known field in v1.
        ofs << R"({"schema_version":1,"id":"mat_unk","emissive_scale":2.0})";
    }

    const auto result = load_from_json(tmp);
    ASSERT_TRUE(result.has_value()) << "Unknown field must not prevent load.";
    EXPECT_EQ(result->id, "mat_unk");
    ASSERT_EQ(result->unknown_fields.size(), 1U);
    EXPECT_EQ(result->unknown_fields[0], "emissive_scale");

    // validate_authored must surface the unknown field as a [WARNING].
    std::vector<std::string> issues;
    const bool ok = validate_authored(*result, issues);
    EXPECT_TRUE(ok) << "Unknown field must not be a hard error.";
    EXPECT_FALSE(has_error(issues));
    EXPECT_TRUE(has_warning(issues))
        << "Expected a [WARNING] for the unknown 'emissive_scale' field.";

    std::filesystem::remove(tmp);
}

// ---- T20 — multiple unknown fields -> one [WARNING] each -------------------
// Two unknown keys produce two independent [WARNING] messages so that
// the designer sees all offenders in a single validation pass.

TEST(MaterialAuthoring, MultipleUnknownFieldsEachWarn)
{
    const auto tmp = std::filesystem::temp_directory_path() /
                     "cd_mat_multi_unk.material.json";
    {
        std::ofstream ofs { tmp, std::ios::binary };
        ASSERT_TRUE(ofs.is_open());
        ofs << R"({"schema_version":1,"id":"mat_multi_unk",)"
               R"("future_field_a":1,"future_field_b":"hello"})";
    }

    const auto result = load_from_json(tmp);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->unknown_fields.size(), 2U);

    std::vector<std::string> issues;
    const bool ok = validate_authored(*result, issues);
    EXPECT_TRUE(ok);

    const auto warn_count = std::ranges::count_if(
        issues,
        [](const std::string& s) { return s.starts_with("[WARNING]"); });
    EXPECT_GE(warn_count, 2)
        << "Expected at least one [WARNING] per unknown field.";

    std::filesystem::remove(tmp);
}

// ---- T21 — all-defaults round-trip validates clean --------------------------
// A minimal JSON file (id + schema_version only) should load, all fields
// fall back to defaults, and validate_authored should return true with no errors.

TEST(MaterialAuthoring, AllDefaultsRoundTripValidatesClean)
{
    using cd::asset::material_authoring::kCurrentSchemaVersion;

    const auto tmp = std::filesystem::temp_directory_path() /
                     "cd_mat_all_defaults.material.json";
    {
        std::ofstream ofs { tmp, std::ios::binary };
        ASSERT_TRUE(ofs.is_open());
        ofs << R"({"schema_version":1,"id":"mat_defaults"})";
    }

    const auto result = load_from_json(tmp);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->schema_version, kCurrentSchemaVersion);
    EXPECT_EQ(result->id, "mat_defaults");
    // All other fields must be at their AuthoredMaterial default values.
    EXPECT_NEAR(result->base_color[0], 1.0F, 1e-6F);
    EXPECT_NEAR(result->base_color[1], 1.0F, 1e-6F);
    EXPECT_NEAR(result->base_color[2], 1.0F, 1e-6F);
    EXPECT_NEAR(result->metallic,      0.0F, 1e-6F);
    EXPECT_NEAR(result->roughness,     0.5F, 1e-6F);
    EXPECT_EQ(result->alpha_mode,      cd::material::AlphaMode::kOpaque);
    EXPECT_NEAR(result->alpha_cutoff,  0.5F, 1e-6F);
    EXPECT_TRUE(result->unknown_fields.empty());

    std::vector<std::string> issues;
    const bool ok = validate_authored(*result, issues);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(has_error(issues));

    std::filesystem::remove(tmp);
}

// ---- T22 — truncated JSON body -> nullopt -----------------------------------
// A file whose JSON is syntactically incomplete (e.g. a write interrupted
// mid-key) must be rejected by the parser and produce nullopt.

TEST(MaterialAuthoring, TruncatedJsonBodyReturnsNullopt)
{
    const auto tmp = std::filesystem::temp_directory_path() /
                     "cd_mat_truncated_body.material.json";
    {
        std::ofstream ofs { tmp, std::ios::binary };
        ASSERT_TRUE(ofs.is_open());
        // Valid prefix followed by an abruptly-terminated value — no closing '}'.
        ofs << R"({"schema_version":1,"id":"mat_trunc","roughness":)";
    }

    const auto result = load_from_json(tmp);
    EXPECT_FALSE(result.has_value())
        << "Expected nullopt for a truncated JSON body.";

    std::filesystem::remove(tmp);
}

// ---- T23 — out-of-range scalar loaded from JSON -> validate catches it ------
// load_from_json does NOT clamp out-of-range values; it stores whatever the
// JSON contains. validate_authored must then catch and report the bad value.

TEST(MaterialAuthoring, OutOfRangeScalarFromJsonCaughtByValidate)
{
    const auto tmp = std::filesystem::temp_directory_path() /
                     "cd_mat_oob_scalar.material.json";
    {
        std::ofstream ofs { tmp, std::ios::binary };
        ASSERT_TRUE(ofs.is_open());
        // roughness = 2.5 is out of [0,1]; load must NOT silently clamp it.
        ofs << R"({"schema_version":1,"id":"mat_oob","roughness":2.5})";
    }

    const auto result = load_from_json(tmp);
    ASSERT_TRUE(result.has_value()) << "Out-of-range scalar must not block load.";
    // Confirm the raw value is preserved (not clamped).
    EXPECT_NEAR(result->roughness, 2.5F, 1e-4F)
        << "load_from_json must not clamp roughness — validate_authored does.";

    std::vector<std::string> issues;
    const bool ok = validate_authored(*result, issues);
    EXPECT_FALSE(ok) << "validate_authored must reject roughness=2.5.";
    EXPECT_TRUE(has_error(issues));

    std::filesystem::remove(tmp);
}

// ---- T24 — empty id string in JSON -> nullopt at load time -----------------
// A JSON file with "id":"" must be rejected by load_from_json (the loader
// treats an empty id as a missing required field).

TEST(MaterialAuthoring, EmptyIdInJsonReturnsNullopt)
{
    const auto tmp = std::filesystem::temp_directory_path() /
                     "cd_mat_empty_id.material.json";
    {
        std::ofstream ofs { tmp, std::ios::binary };
        ASSERT_TRUE(ofs.is_open());
        ofs << R"({"schema_version":1,"id":""})";
    }

    const auto result = load_from_json(tmp);
    EXPECT_FALSE(result.has_value())
        << "Expected nullopt when 'id' is an empty string.";

    std::filesystem::remove(tmp);
}

// ---- T25 — all-defaults in-process material -> clean validation -------------
// An AuthoredMaterial default-constructed (schema_version=kCurrentSchemaVersion,
// no unknown_fields) with a non-empty id must validate clean.

TEST(MaterialAuthoring, AllDefaultsInProcessValidatesClean)
{
    AuthoredMaterial mat;
    mat.id = "mat_in_process_defaults";
    // All other fields are AuthoredMaterial struct defaults.

    std::vector<std::string> issues;
    const bool ok = validate_authored(mat, issues);

    EXPECT_TRUE(ok);
    EXPECT_FALSE(has_error(issues));
    EXPECT_FALSE(has_warning(issues));
    // schema_version == kCurrentSchemaVersion => no INFO note for legacy.
    EXPECT_FALSE(has_info(issues));
}

// ---- T26 — BLEND alpha_mode survives round-trip correctly -------------------
// Verifies that "BLEND" alpha_mode is correctly serialised and re-parsed,
// ensuring alpha_mode_from_string and alpha_mode_to_string are inverses.

TEST(MaterialAuthoring, BlendAlphaModeRoundTrip)
{
    const auto tmp = std::filesystem::temp_directory_path() /
                     "cd_mat_blend_rt.material.json";

    AuthoredMaterial original = AuthoringDefaults::cloth();
    original.id         = "mat_blend_rt";
    original.alpha_mode = cd::material::AlphaMode::kBlend;
    original.alpha_cutoff = 0.5F;

    ASSERT_TRUE(save_to_json(original, tmp));

    const auto loaded_opt = load_from_json(tmp);
    ASSERT_TRUE(loaded_opt.has_value());
    EXPECT_EQ(loaded_opt->alpha_mode, cd::material::AlphaMode::kBlend);

    std::filesystem::remove(tmp);
}

// ---- T27 — re-saving a legacy file always writes kCurrentSchemaVersion ------
// A file loaded without a schema_version (schema_version=0) that is then
// re-saved by save_to_json must emit the current engine version, not 0.
// This ensures the save path is always canonical regardless of the source.

TEST(MaterialAuthoring, ResaveLegacyFileWritesCurrentSchemaVersion)
{
    using cd::asset::material_authoring::kCurrentSchemaVersion;

    // Step 1: create a legacy file (no schema_version).
    const auto legacy_tmp = std::filesystem::temp_directory_path() /
                            "cd_mat_legacy_resave_src.material.json";
    {
        std::ofstream ofs { legacy_tmp, std::ios::binary };
        ASSERT_TRUE(ofs.is_open());
        ofs << R"({"id":"mat_legacy_resave","metallic":0.2,"roughness":0.7})";
    }

    const auto loaded_opt = load_from_json(legacy_tmp);
    ASSERT_TRUE(loaded_opt.has_value());
    EXPECT_EQ(loaded_opt->schema_version, 0U) << "Legacy file should read as version 0.";

    // Step 2: re-save via save_to_json.
    const auto resaved_tmp = std::filesystem::temp_directory_path() /
                             "cd_mat_legacy_resaved.material.json";
    ASSERT_TRUE(save_to_json(*loaded_opt, resaved_tmp));

    // Step 3: load the re-saved file — should have kCurrentSchemaVersion.
    const auto reloaded_opt = load_from_json(resaved_tmp);
    ASSERT_TRUE(reloaded_opt.has_value());
    EXPECT_EQ(reloaded_opt->schema_version, kCurrentSchemaVersion)
        << "Re-saved file must always carry kCurrentSchemaVersion.";

    std::filesystem::remove(legacy_tmp);
    std::filesystem::remove(resaved_tmp);
}

}  // namespace
