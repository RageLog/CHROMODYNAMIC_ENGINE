// =============================================================================
// CHROMODYNAMIC — cd/spirv_cross_glue/Translate.hpp
// B-infra1 — SPIRV-Cross integration for cross-backend shader translation.
//
// Translates a SPIR-V binary module to a target shading language using the
// KhronosGroup/SPIRV-Cross C++ API. The primary consumer is the M4 D3D12
// parity sprint: GLSL shaders authored once, compiled to SPIR-V by
// cd::shader::GlslangCompiler, then cross-compiled to HLSL here and fed
// to DXC for DXIL emission.
//
// Thread-safety: `translate()` is stateless and re-entrant. Each call
// constructs a fresh SPIRV-Cross compiler object internally.
//
// Error handling: no exceptions escape this API. SPIRV-Cross may throw
// internally (CompilerError inherits std::runtime_error); all exceptions
// are caught and surfaced as a populated TranslateResult::error string.
// The caller checks error.empty() to distinguish success from failure.
// =============================================================================
#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace cd::spirv_cross_glue
{

/// Target shading language for the translation.
enum class Target : std::uint8_t
{
    kGlsl,  ///< OpenGL Shading Language (GLSL). Useful for round-trip validation.
    kHlsl,  ///< High-Level Shading Language (HLSL). Target for D3D12 / DXC.
    kMsl,   ///< Metal Shading Language (MSL). Target for Apple Metal backend.
};

/// Result of a `translate()` call.
struct TranslateResult
{
    /// The translated source text. Non-empty on success, empty on failure.
    std::string source;
    /// Human-readable error description. Empty on success.
    std::string error;

    /// Returns true when the translation succeeded (error is empty).
    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

/// Translate a SPIR-V binary module to the requested target language.
///
/// @param spirv   Span of 32-bit SPIR-V words. Must be non-empty and valid.
/// @param target  Destination shading language.
/// @param version Target language version hint:
///                - 0 = auto-pick: HLSL Shader Model 6.0, GLSL 450, MSL 2.2
///                - GLSL: numeric GLSL version (e.g. 450 for #version 450)
///                - HLSL: shader model × 10 (e.g. 60 = SM 6.0, 51 = SM 5.1)
///                - MSL:  packed version (major*10000 + minor*100; e.g.
///                         20200 = MSL 2.2, 30000 = MSL 3.0)
///
/// @returns TranslateResult with populated `source` on success, or
///          populated `error` on failure (TranslateResult::ok() == false).
[[nodiscard]] TranslateResult translate(
    std::span<const std::uint32_t> spirv,
    Target                         target,
    std::uint32_t                  version = 0
);

}  // namespace cd::spirv_cross_glue
