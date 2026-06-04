// =============================================================================
// CHROMODYNAMIC — cd/asset/vfx_authoring/VfxAuthoring.hpp
// Phase 694 — cd::asset::vfx_authoring (M14 W5C)
//
// Asset-tier VFX authoring helper. Designers hand-edit .vfx.json files; the
// engine validates and loads them into cd::particle::system on the hot-reload
// path — no recompile required.
//
// Key moment: a VFX artist authors a 'jump_dust' effect as JSON, hot-reload
// picks it up, and the engine spawns the new particles immediately on the next
// jump — zero engineer round-trips.
//
// API surface:
//   AuthoredVfx         — POD aggregate: emitter + life + colour + size +
//                         velocity + texture fields.
//   save_to_json()      — serialize AuthoredVfx -> .vfx.json
//   load_from_json()    — deserialize .vfx.json -> AuthoredVfx
//   validate_authored() — catch out-of-range scalars, empty IDs, etc.
//   VfxPresets          — jump_dust / muzzle_flash / fire_smoke / water_splash
//                         factory presets (Source 2 / HL2 / Alyx archetypes).
//
// Namespace: cd::asset::vfx_authoring
// Depends on: cd::asset_json (JSON I/O), cd::core
// Does NOT depend on cd::rhi — stays in the asset tier.
// =============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cd::asset::vfx_authoring
{

// ---- Error domain -----------------------------------------------------------

namespace vfx_authoring_errors
{
inline constexpr std::uint32_t kDomain = 0x0020;

enum class Code : std::uint32_t
{
    kOk              = 0,
    kFileNotFound    = 1,
    kIoError         = 2,
    kParseError      = 3,
    kMissingField    = 4,
    kValidationError = 5,
};
}  // namespace vfx_authoring_errors

// ---- AuthoredVfx ------------------------------------------------------------

/// All fields a VFX designer can specify in a .vfx.json file.
///
/// Rules:
///   * `id`              must be non-empty (pipeline primary key).
///   * `emitter_kind`    must be non-empty ("burst", "continuous", etc.).
///   * `emit_rate_per_sec` must be > 0.
///   * `life_seconds`    must be > 0.
///   * `color_start` / `color_end` channels are RGBA in [0,1].
///   * `size_start` / `size_end` must be >= 0.
///   * `velocity_min[i]` must be <= `velocity_max[i]` for each axis.
///   * `texture_path`    may be empty (solid-colour particle).
///
/// JSON schema (pretty-printed example):
/// {
///   "id": "jump_dust",
///   "emitter_kind": "burst",
///   "emit_rate_per_sec": 40.0,
///   "life_seconds": 0.45,
///   "color_start": [0.85, 0.80, 0.72, 1.0],
///   "color_end":   [0.85, 0.80, 0.72, 0.0],
///   "size_start": 0.08,
///   "size_end":   0.22,
///   "velocity_min": [-1.5, 0.5, -1.5],
///   "velocity_max": [ 1.5, 2.5,  1.5],
///   "texture_path": "particles/smoke_soft.png"
/// }
struct AuthoredVfx
{
    std::string           id {};
    std::string           emitter_kind {};
    float                 emit_rate_per_sec { 30.0F };
    float                 life_seconds { 1.0F };
    std::array<float, 4>  color_start { 1.0F, 1.0F, 1.0F, 1.0F };
    std::array<float, 4>  color_end   { 1.0F, 1.0F, 1.0F, 0.0F };
    float                 size_start { 0.1F };
    float                 size_end   { 0.2F };
    std::array<float, 3>  velocity_min { -1.0F, 0.0F, -1.0F };
    std::array<float, 3>  velocity_max {  1.0F, 2.0F,  1.0F };
    std::string           texture_path {};
};

// ---- I/O --------------------------------------------------------------------

/// Serialize `vfx` to a pretty-printed JSON file at `path`.
/// Creates or overwrites the file. Returns false on I/O error.
[[nodiscard]] bool save_to_json(const AuthoredVfx&           vfx,
                                const std::filesystem::path& path);

/// Deserialize a .vfx.json file from `path`.
/// Returns std::nullopt on file-not-found, parse error, or missing required
/// fields (id and emitter_kind must be present and non-empty).
/// All optional fields fall back to AuthoredVfx defaults when absent.
[[nodiscard]] std::optional<AuthoredVfx>
load_from_json(const std::filesystem::path& path);

// ---- Validation -------------------------------------------------------------

/// Validate `vfx` for pipeline-readiness.
///
/// Checks:
///   1. id is non-empty.
///   2. emitter_kind is non-empty.
///   3. emit_rate_per_sec > 0.
///   4. life_seconds > 0.
///   5. color_start / color_end RGBA channels in [0,1].
///   6. size_start >= 0 and size_end >= 0.
///   7. For each axis i: velocity_min[i] <= velocity_max[i].
///
/// `out_issues` is appended (not cleared). Returns true when no [ERROR]-level
/// issue was added by this call ([WARNING] / [INFO] are non-fatal).
[[nodiscard]] bool validate_authored(const AuthoredVfx&       vfx,
                                     std::vector<std::string>& out_issues);

// ---- Presets ----------------------------------------------------------------

/// Source 2 / HL2 / Alyx archetype presets — starting points for VFX artists.
struct VfxPresets
{
    /// Foot-scuff dust burst on character jump — low velocity, warm tan colour,
    /// quick fade-out.
    [[nodiscard]] static AuthoredVfx jump_dust();

    /// High-rate spark + smoke burst at firearm muzzle — fast forward velocity,
    /// short life, bright orange-white fade.
    [[nodiscard]] static AuthoredVfx muzzle_flash();

    /// Rolling smoke column rising from fire — upward drift, dark grey, long
    /// life, large end size.
    [[nodiscard]] static AuthoredVfx fire_smoke();

    /// Water droplet burst on impact — radial outward spread, blue-white,
    /// quick gravity fall.
    [[nodiscard]] static AuthoredVfx water_splash();
};

}  // namespace cd::asset::vfx_authoring
