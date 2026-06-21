// =============================================================================
// CHROMODYNAMIC — cd/asset/vfx_authoring/VfxAuthoring.hpp
// Phase 694 — cd::asset::vfx_authoring (M14 W5C) — Phase 1268 complete pass
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
//   FloatCurve          — keyframe curve (time in [0,1] -> float value); eval,
//                         serialize, deserialize.
//   ColorGradient       — keyframe gradient (time in [0,1] -> RGBA); eval,
//                         serialize, deserialize.
//   AuthoredVfx         — POD aggregate: emitter + life + colour + size +
//                         velocity + texture + optional curve/gradient overrides.
//                         Includes schema_version for forward-compat.
//   save_to_json()      — serialize AuthoredVfx -> .vfx.json
//   load_from_json()    — deserialize .vfx.json -> AuthoredVfx
//   validate_authored() — catch out-of-range scalars, empty IDs; emits
//                         [ERROR] for pipeline-blocking issues and [WARNING]
//                         for editorial quality issues.
//   clone_with_id()     — deep-copy an effect under a new id.
//   VfxPresets          — jump_dust / muzzle_flash / fire_smoke / water_splash
//                         factory presets (Source 2 / HL2 / Alyx archetypes).
//   VfxLibrary          — named collection of AuthoredVfx; save/load as a
//                         single .vfxlib.json; lookup by id.
//
// Schema versioning:
//   kSchemaVersion = 1.  Written as "schema_version" to every saved file.
//   Files missing "schema_version" are treated as v0 (pre-versioning) and
//   loaded with a [WARNING].  Files with version > kSchemaVersion are
//   rejected (returns nullopt for single-effect load; skipped for library).
//
// Known emitter kinds (validated as [WARNING] if unrecognised):
//   "burst", "continuous", "one_shot"
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

// ---- Schema version ---------------------------------------------------------

/// Current JSON schema version written by save_to_json() and save_library().
/// Files missing "schema_version" are v0 (pre-versioning) — loaded with a
/// [WARNING].  Files with version > kSchemaVersion are rejected.
inline constexpr std::uint32_t kSchemaVersion = 1U;

// ---- FloatCurve -------------------------------------------------------------

/// A piecewise-linear curve mapping time ∈ [0,1] to a float value.
///
/// Keyframes are stored sorted by time.  eval() performs linear interpolation
/// between adjacent keyframes.  If the curve is empty, eval() returns 0.
///
/// JSON representation (array of [time, value] pairs):
///   "size_curve": [[0.0, 0.1], [0.5, 0.3], [1.0, 0.0]]
struct FloatCurve
{
    struct Keyframe
    {
        float time  {};   ///< in [0, 1]
        float value {};
    };

    std::vector<Keyframe> keyframes;

    /// Evaluate the curve at normalised time `t` (clamped to [0,1]).
    /// Returns 0 when the curve is empty.
    [[nodiscard]] float eval(float t) const noexcept;

    /// Add a keyframe, keeping the keyframes sorted by time.
    void add_keyframe(float time, float value);

    /// True when keyframes is empty.
    [[nodiscard]] bool empty() const noexcept { return keyframes.empty(); }
};

// ---- ColorGradient ----------------------------------------------------------

/// A piecewise-linear colour gradient mapping time ∈ [0,1] to RGBA.
///
/// Keyframes are stored sorted by time.  eval() performs per-channel linear
/// interpolation between adjacent keyframes.  Returns {0,0,0,0} when empty.
///
/// JSON representation (array of [time, r, g, b, a] arrays):
///   "color_gradient": [[0.0, 1.0, 0.8, 0.7, 1.0], [1.0, 1.0, 0.8, 0.7, 0.0]]
struct ColorGradient
{
    struct Keyframe
    {
        float time {};                        ///< in [0, 1]
        std::array<float, 4> rgba { 1.0F, 1.0F, 1.0F, 1.0F };
    };

    std::vector<Keyframe> keyframes;

    /// Evaluate the gradient at normalised time `t` (clamped to [0,1]).
    /// Returns {0,0,0,0} when the gradient is empty.
    [[nodiscard]] std::array<float, 4> eval(float t) const noexcept;

    /// Add a keyframe, keeping the keyframes sorted by time.
    void add_keyframe(float time, std::array<float, 4> rgba);

    /// True when keyframes is empty.
    [[nodiscard]] bool empty() const noexcept { return keyframes.empty(); }
};

// ---- AuthoredVfx ------------------------------------------------------------

/// All fields a VFX designer can specify in a .vfx.json file.
///
/// Rules:
///   * `id`              must be non-empty (pipeline primary key).
///   * `emitter_kind`    must be non-empty ("burst", "continuous", "one_shot").
///   * `emit_rate_per_sec` must be > 0.
///   * `life_seconds`    must be > 0.
///   * `color_start` / `color_end` channels are RGBA in [0,1].
///   * `size_start` / `size_end` must be >= 0.
///   * `velocity_min[i]` must be <= `velocity_max[i]` for each axis.
///   * `texture_path`    may be empty (solid-colour particle).
///   * `size_curve`      optional override; when non-empty, replaces the
///                       linear size_start → size_end interpolation.
///   * `color_gradient`  optional override; when non-empty, replaces the
///                       linear color_start → color_end interpolation.
///
/// Schema:
///   "schema_version": 1    — written by save_to_json(); absent in v0 files.
///
/// JSON schema (pretty-printed example):
/// {
///   "schema_version": 1,
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
    std::uint32_t         schema_version    { kSchemaVersion };
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

    /// Optional curve overrides.  When empty, the runtime falls back to the
    /// linear start/end interpolation above.
    FloatCurve   size_curve;
    ColorGradient color_gradient;
};

// ---- I/O --------------------------------------------------------------------

/// Serialize `vfx` to a pretty-printed JSON file at `path`.
/// Creates or overwrites the file.  schema_version is always written.
/// Returns false on I/O error.
[[nodiscard]] bool save_to_json(const AuthoredVfx&           vfx,
                                const std::filesystem::path& path);

/// Deserialize a .vfx.json file from `path`.
/// Returns std::nullopt on:
///   - file-not-found
///   - parse error
///   - missing required fields (id, emitter_kind — must be non-empty)
///   - schema_version > kSchemaVersion (future format)
/// All optional fields fall back to AuthoredVfx defaults when absent.
/// Files without "schema_version" (v0) are loaded successfully; the caller
/// can detect this via vfx.schema_version == 0.
[[nodiscard]] std::optional<AuthoredVfx>
load_from_json(const std::filesystem::path& path);

// ---- Authoring helpers ------------------------------------------------------

/// Return a deep copy of `src` with `id` replaced by `new_id`.
/// The new effect is otherwise identical to `src`, including curves and
/// gradients.  Useful for deriving a variant from a preset.
[[nodiscard]] AuthoredVfx clone_with_id(const AuthoredVfx& src,
                                        const std::string& new_id);

// ---- Validation -------------------------------------------------------------

/// Validate `vfx` for pipeline-readiness.
///
/// [ERROR] checks (return false if any fire):
///   1. id is non-empty.
///   2. emitter_kind is non-empty.
///   3. emit_rate_per_sec > 0 and not NaN.
///   4. life_seconds > 0 and not NaN.
///   5. color_start / color_end RGBA channels in [0,1].
///   6. size_start >= 0 and size_end >= 0.
///   7. For each axis i: velocity_min[i] <= velocity_max[i].
///   8. FloatCurve keyframe times are in [0,1] and values are finite.
///   9. ColorGradient keyframe times are in [0,1] and RGBA channels in [0,1].
///
/// [WARNING] checks (non-fatal, do not affect return value):
///   W1. emit_rate_per_sec > 10000 — likely performance hazard.
///   W2. life_seconds > 60 — unusually long effect lifetime.
///   W3. emitter_kind is not one of the known values ("burst", "continuous",
///       "one_shot") — may be a typo.
///   W4. schema_version > kSchemaVersion — file was authored by a newer tool.
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

// ---- VfxLibrary -------------------------------------------------------------

/// A named, keyed collection of AuthoredVfx effects stored as a single
/// .vfxlib.json file.
///
/// JSON layout:
/// {
///   "schema_version": 1,
///   "effects": [ { ...AuthoredVfx... }, ... ]
/// }
///
/// Invariant: effect ids within a library are unique.  Adding an effect
/// whose id already exists overwrites the previous entry.
class VfxLibrary
{
public:
    VfxLibrary() = default;

    /// Add or overwrite an effect.  The effect's id must be non-empty.
    /// Returns false and leaves the library unchanged if id is empty.
    bool add(AuthoredVfx effect);

    /// Remove the effect with the given id.  Returns true if it existed.
    bool remove(const std::string& id);

    /// Look up an effect by id.  Returns nullptr when not found.
    [[nodiscard]] const AuthoredVfx* find(const std::string& id) const noexcept;

    /// Number of effects currently in the library.
    [[nodiscard]] std::size_t size() const noexcept;

    /// True when the library contains no effects.
    [[nodiscard]] bool empty() const noexcept;

    /// Iterate over all effects (insertion / alphabetical order is by id since
    /// the underlying storage is std::vector sorted on add).
    [[nodiscard]] const std::vector<AuthoredVfx>& effects() const noexcept;

    /// Serialize the entire library to a pretty-printed .vfxlib.json file.
    /// Returns false on I/O error.
    [[nodiscard]] bool save(const std::filesystem::path& path) const;

    /// Deserialize a .vfxlib.json file.  Effects whose schema_version exceeds
    /// kSchemaVersion are skipped (with a message appended to `out_issues`).
    /// Returns false when the file cannot be opened or parsed.
    /// `out_issues` is appended (not cleared).
    [[nodiscard]] bool load(const std::filesystem::path& path,
                            std::vector<std::string>&    out_issues);

private:
    std::vector<AuthoredVfx> effects_;
};

}  // namespace cd::asset::vfx_authoring
