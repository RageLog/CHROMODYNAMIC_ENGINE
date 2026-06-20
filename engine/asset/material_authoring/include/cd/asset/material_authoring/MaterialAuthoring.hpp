// =============================================================================
// CHROMODYNAMIC — cd/asset/material_authoring/MaterialAuthoring.hpp
// Phase 644 — cd::asset::material_authoring (M10 W1B)
//
// Asset-tier material authoring helper. NOT a v2 of cd::material — this sits
// at the asset pipeline level: designers hand-edit .material.json files, the
// engine loads and validates them before they enter the render pipeline.
//
// Key moment: a designer edits a .material.json, validate_authored() catches
// a typo ("roughness": 1.5 or a missing albedo path), and the hot-reload
// pipeline surfaces the diagnostic before the asset ever reaches the GPU.
//
// API surface:
//   AuthoredMaterial         — POD aggregate: all scalar + path fields.
//   save_to_json()           — serialize AuthoredMaterial -> .material.json
//   load_from_json()         — deserialize .material.json -> AuthoredMaterial
//   validate_authored()      — catch out-of-range scalars, empty IDs, missing
//                              paths, unknown fields, and version skew before
//                              the asset enters the pipeline.
//   AuthoringDefaults        — dielectric / metal / cloth factory presets.
//
// Wire:
//   alpha_mode and alpha_cutoff map directly to cd::material::AlphaMode and
//   cd::material::AlphaParams (one-to-one, safe static_cast).
//
// Schema versioning:
//   Every saved file contains "schema_version": 1 (kCurrentSchemaVersion).
//   load_from_json() accepts files where "schema_version" is absent (v0
//   legacy) or equals kCurrentSchemaVersion.  A future-version file (value >
//   kCurrentSchemaVersion) is rejected at load time (returns nullopt) so that
//   old engine builds never silently load a newer format they don't understand.
//   validate_authored() flags version-skew [WARNING] and unknown keys [WARNING]
//   so that the hot-reload diagnostic surface sees them without a hard fail.
//
// Namespace: cd::asset::material_authoring
// Depends on: cd::material (for AlphaMode), cd::asset_json (for JSON I/O),
//             cd::core
// =============================================================================
#pragma once

#include <cd/material/AlphaMode.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cd::asset::material_authoring
{

// ---- Schema versioning -------------------------------------------------------

/// Current on-disk schema version.  Increment this when adding new required
/// fields to AuthoredMaterial that break backward-compatibility.  Optional
/// additive fields (new defaults) do NOT require a version bump.
inline constexpr std::uint32_t kCurrentSchemaVersion = 1U;

// ---- Error domain -----------------------------------------------------------

namespace authoring_errors
{
inline constexpr std::uint32_t kDomain = 0x001F;

enum class Code : std::uint32_t
{
    kOk              = 0,
    kFileNotFound    = 1,
    kIoError         = 2,
    kParseError      = 3,
    kMissingField    = 4,
    kValidationError = 5,
    kVersionSkew     = 6,
};
}  // namespace authoring_errors

// ---- AuthoredMaterial -------------------------------------------------------

/// All fields a designer can specify in a .material.json file.
///
/// Rules:
///   * `id`    must be non-empty (pipeline primary key).
///   * `base_color` channels in [0,1].
///   * `metallic` in [0,1], `roughness` in [0,1].
///   * `alpha_cutoff` in [0,1] (meaningful only when alpha_mode == kMask).
///   * Texture paths may be empty strings (no texture bound).
///   * `schema_version` is populated by load_from_json(); 0 = legacy file
///     (absent field); kCurrentSchemaVersion = current engine.
///   * `unknown_fields` is populated by load_from_json() with any JSON keys
///     not recognised by this version of the loader.  validate_authored()
///     emits [WARNING] for each unknown field.  The engine tolerates unknown
///     fields so that files authored by a newer tool still load.
///
/// JSON schema (pretty-printed example):
/// {
///   "schema_version": 1,
///   "id": "mat_concrete",
///   "base_color": [0.6, 0.58, 0.55],
///   "metallic": 0.0,
///   "roughness": 0.85,
///   "albedo_texture_path": "textures/concrete_albedo.png",
///   "normal_texture_path": "textures/concrete_normal.png",
///   "mr_texture_path": "textures/concrete_mr.png",
///   "alpha_mode": "OPAQUE",
///   "alpha_cutoff": 0.5
/// }
struct AuthoredMaterial
{
    std::string             id {};
    std::array<float, 3>    base_color { 1.0F, 1.0F, 1.0F };
    float                   metallic { 0.0F };
    float                   roughness { 0.5F };
    std::string             albedo_texture_path {};
    std::string             normal_texture_path {};
    std::string             mr_texture_path {};
    cd::material::AlphaMode alpha_mode { cd::material::AlphaMode::kOpaque };
    float                   alpha_cutoff { 0.5F };
    /// Schema version read from file; 0 = legacy (no "schema_version" key).
    std::uint32_t           schema_version { kCurrentSchemaVersion };
    /// JSON keys present in the file but not recognised by this loader version.
    std::vector<std::string> unknown_fields {};
};

// ---- I/O --------------------------------------------------------------------

/// Serialize `mat` to a pretty-printed JSON file at `path`.
/// Creates or overwrites the file. Returns false on I/O error.
[[nodiscard]] bool save_to_json(const AuthoredMaterial& mat,
                                const std::filesystem::path& path);

/// Deserialize a .material.json file from `path`.
/// Returns std::nullopt on file-not-found, parse error, or missing required
/// fields (id must be present). All optional fields fall back to AuthoredMaterial
/// defaults when absent from the JSON.
[[nodiscard]] std::optional<AuthoredMaterial>
load_from_json(const std::filesystem::path& path);

// ---- Validation -------------------------------------------------------------

/// Validate `mat` for pipeline-readiness.
///
/// Checks:
///   1. id is non-empty.
///   2. base_color channels are in [0,1] (NaN is an error).
///   3. metallic is in [0,1] (NaN is an error).
///   4. roughness is in [0,1] (NaN is an error).
///   5. alpha_cutoff is in [0,1] (NaN is an error).
///   6. When alpha_mode == kMask: alpha_cutoff is in (0,1) exclusive
///      (0 discards everything, 1 keeps nothing — both are almost certainly
///      typos).
///   7. When alpha_mode == kBlend: alpha_cutoff has no effect (kInfo note).
///   8. schema_version: 0 = legacy tolerated ([INFO]); > kCurrentSchemaVersion
///      = future-format ([WARNING], not an error — the file was already loaded
///      by load_from_json's hard version gate).
///   9. unknown_fields: each unknown key emits a [WARNING].
///
/// `out_issues` is appended (not cleared). Returns true when no kError-level
/// issue was added by this call (kInfo / kWarning are non-fatal).
[[nodiscard]] bool validate_authored(const AuthoredMaterial&  mat,
                                     std::vector<std::string>& out_issues);

// ---- Presets ----------------------------------------------------------------

/// Factory presets — starting points a designer duplicates and tweaks.
struct AuthoringDefaults
{
    /// Plastic / stone / wood — non-conductive dielectric.
    ///   base_color = mid-grey, metallic = 0, roughness = 0.6
    [[nodiscard]] static AuthoredMaterial dielectric();

    /// Aluminium-ish pure metal.
    ///   base_color = light silver, metallic = 1, roughness = 0.15
    [[nodiscard]] static AuthoredMaterial metal();

    /// Microfiber cloth — rough, non-metallic, slight colour saturation.
    ///   base_color = warm beige, metallic = 0, roughness = 0.95
    [[nodiscard]] static AuthoredMaterial cloth();
};

}  // namespace cd::asset::material_authoring
