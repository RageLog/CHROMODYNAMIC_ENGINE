// =============================================================================
// CHROMODYNAMIC — cd/asset/validator/Validator.hpp
// Phase 611 — cd::asset::validator (Sprint-1: pre-load issue catcher)
//
// Pre-load asset validation: catch malformed, oversized, or missing assets
// BEFORE they enter the render pipeline. Sprint-1 covers magic-header checks,
// non-empty blob, and size-limit policies for texture, glTF, and audio blobs.
// Deep semantic validation (mip chains, mesh topology, etc.) is future Sprint.
//
// Namespace: cd::asset::validator
// =============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cd::asset::validator
{

// ---- Severity ---------------------------------------------------------------

/// Issue severity level reported by Validator.
enum class Severity
{
    kInfo,
    kWarning,
    kError,
};

// ---- ValidationIssue --------------------------------------------------------

/// A single diagnostic produced by a validate_*_blob() call.
struct ValidationIssue
{
    Severity    severity;
    std::string asset_path;
    std::string message;
};

// ---- ValidatorPolicy --------------------------------------------------------

/// Configurable limits and requirements applied by Validator.
///
/// Defaults:
///   max_texture_pixels   — 16 Mi pixels  (16 384 x 16 384 or equivalent)
///   max_mesh_vertices    — 1 Mi vertices
///   required_color_spaces — three slots, all empty by default (no requirement)
struct ValidatorPolicy
{
    std::uint64_t max_texture_pixels { 1024ULL * 1024ULL * 16ULL };
    std::uint64_t max_mesh_vertices  { 1024ULL * 1024ULL };

    /// Texture color-space tokens that MUST be present (Sprint-2 semantic
    /// check). In Sprint-1 this field is stored but not actively enforced
    /// beyond a kInfo note when all three slots are non-empty.
    std::array<std::string, 3> required_color_spaces {};
};

// ---- Validator --------------------------------------------------------------

/// Pre-load asset validator (Sprint-1: synchronous, header-only checks).
///
/// Usage:
///   cd::asset::validator::Validator v;
///   v.configure({ .max_texture_pixels = 1024ULL * 1024ULL * 4ULL });
///   auto issues = v.validate_texture_blob(blob, "textures/albedo.png");
///   for (auto& issue : issues) { ... }
///
/// Thread safety: not thread-safe — configure() and validate_*() must be
/// called from the same thread, or with external synchronisation.
class Validator
{
public:
    Validator()  = default;
    ~Validator() = default;

    Validator(const Validator&)            = default;
    Validator& operator=(const Validator&) = default;
    Validator(Validator&&)                 = default;
    Validator& operator=(Validator&&)      = default;

    // ---- Configuration ------------------------------------------------------

    /// Replace the active policy. Takes effect on the next validate_*() call.
    void configure(const ValidatorPolicy& policy);

    // ---- Validation ---------------------------------------------------------

    /// Validate a raw texture blob (PNG / JPG / KTX2 / DDS / HDR accepted).
    ///
    /// Sprint-1 checks:
    ///   - Non-empty blob (kError if empty).
    ///   - Known magic header: PNG (\x89PNG), JPEG (\xFF\xD8), KTX2 (12-byte
    ///     KTX2 identifier), DDS ("DDS "), HDR ("#?RADIANCE") (kError if none
    ///     match).
    ///   - Blob byte count as a conservative upper bound on pixel count:
    ///     blob_bytes / 4 (minimum bytes per pixel) > max_texture_pixels
    ///     => kWarning "may exceed pixel limit".
    ///
    /// Returns empty vector when no issues are found.
    [[nodiscard]] std::vector<ValidationIssue>
    validate_texture_blob(std::span<const std::uint8_t> blob,
                          std::string_view              path) const;

    /// Validate a raw glTF 2.0 blob (JSON .gltf or binary .glb accepted).
    ///
    /// Sprint-1 checks:
    ///   - Non-empty blob (kError if empty).
    ///   - JSON .gltf: first non-whitespace byte must be '{' (kError otherwise).
    ///   - Binary .glb: magic bytes 0x67 0x6C 0x54 0x46 ("glTF") (kError if
    ///     absent).
    ///   - glTF version field: GLB header uint32 at offset 4 must be 2
    ///     (kWarning if != 2 — may be unsupported version).
    ///
    /// Returns empty vector when no issues are found.
    [[nodiscard]] std::vector<ValidationIssue>
    validate_gltf_blob(std::span<const std::uint8_t> blob,
                       std::string_view              path) const;

    /// Validate a raw audio blob (WAV / OGG accepted).
    ///
    /// Sprint-1 checks:
    ///   - Non-empty blob (kError if empty).
    ///   - WAV: magic "RIFF" at offset 0 + "WAVE" at offset 8 (kError if
    ///     absent or blob too small).
    ///   - OGG: magic "OggS" at offset 0 (kError if absent or blob too small).
    ///   - If neither WAV nor OGG magic matches, kError "unrecognised audio
    ///     format".
    ///
    /// Returns empty vector when no issues are found.
    [[nodiscard]] std::vector<ValidationIssue>
    validate_audio_blob(std::span<const std::uint8_t> blob,
                        std::string_view              path) const;

    // ---- Accessors ----------------------------------------------------------

    /// Read the currently active policy.
    [[nodiscard]] const ValidatorPolicy& policy() const noexcept;

private:
    ValidatorPolicy policy_ {};
};

}  // namespace cd::asset::validator
