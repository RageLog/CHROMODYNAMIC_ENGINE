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

/// Reflected compute-shader local workgroup size (the GLSL
/// `layout(local_size_x/y/z)` declaration). Populated by `translate_msl` from
/// SPIRV-Cross `CompilerMSL::get_entry_point().workgroup_size`. For non-compute
/// stages every component stays 1 (the SPIR-V execution mode is absent, which
/// SPIRV-Cross reports as a 1x1x1 size). This is the M6 source of truth: Metal
/// `dispatchThreadgroups:threadsPerThreadgroup:` needs the threads-per-group
/// from the shader because `ComputePipelineDesc` carries no workgroup field.
struct WorkgroupSize
{
    std::uint32_t x { 1 };
    std::uint32_t y { 1 };
    std::uint32_t z { 1 };
};

/// Result of a `translate()` call.
struct TranslateResult
{
    /// The translated source text. Non-empty on success, empty on failure.
    std::string source {};
    /// Human-readable error description. Empty on success.
    std::string error {};
    /// Cleansed entry-point name in the emitted source. Populated by
    /// `translate_msl` (SPIRV-Cross renames the MSL entry point per stage, e.g.
    /// a fragment "main" -> "main0"); empty for the HLSL/GLSL `translate` paths.
    std::string entry_point {};
    /// Reflected compute local workgroup size (M6). Populated by `translate_msl`
    /// only; the HLSL/GLSL `translate` paths leave the 1x1x1 default. For a
    /// non-compute module it stays 1x1x1.
    WorkgroupSize workgroup {};

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

// ---- MSL binding model (phase M3) -------------------------------------------
//
// Metal analog of the D3D12 space-per-set decision
// (ADR-20260614-d3d12-binding-model §4). The plain `translate(..., kMsl, ...)`
// above emits MSL with SPIRV-Cross's classic per-resource [[buffer]]/[[texture]]/
// [[sampler]] auto-allocation (every descriptor set is flattened into one
// linear index space). That is fine for a single-set shader but loses the
// "set N == its own bind point" contract the Vulkan + D3D12 paths preserve.
//
// `translate_msl(spirv, cfg)` configures SPIRV-Cross's `CompilerMSL` so that
// the engine's descriptor sets survive into MSL with stable, predictable bind
// points — the contract the future .mm `MetalDevice` argument-encoder side must
// honour. See `MslBindingConfig` for the exact convention.

/// Binding-model configuration for the SPIR-V -> MSL translation.
///
/// Convention (the .mm device contract):
///   * `argument_buffers == true`  : each Vulkan descriptor SET N becomes a
///     Metal *argument buffer* bound at `[[buffer(N)]]`; resources inside the
///     set are emitted as `[[id(binding)]]` members of that argument-buffer
///     struct. This is the direct Metal analog of D3D12 "space N == set N":
///     argument-buffer slot N == descriptor set N. The .mm side creates one
///     `MTLArgumentEncoder` per set and binds it at buffer index N.
///   * `argument_buffers == false` : classic MSL 1.x flat binding — SPIRV-Cross
///     auto-allocates [[buffer(n)]]/[[texture(n)]]/[[sampler(n)]] across all
///     sets in declaration order. Kept for single-set / compute shaders and for
///     parity with the existing `translate(..., kMsl)` behaviour.
///   * The `push_constant` block is always remapped to a *dedicated* buffer slot
///     `push_constant_buffer_index` (default 1, outside the set range used by
///     the engine's set 0 / set 1), so the device can `setVertexBytes` /
///     `setFragmentBytes` the push range at one fixed index regardless of how
///     many descriptor sets the shader declares. This mirrors the D3D12
///     "push_constant -> its own b0/space1 root-constants slot" decision.
struct MslBindingConfig
{
    /// Packed MSL version (major*10000 + minor*100; e.g. 20200 = MSL 2.2).
    /// 0 = auto-pick (MSL 2.2). Argument buffers require MSL >= 2.0.
    std::uint32_t version { 0 };
    /// Map each descriptor set to its own argument buffer at [[buffer(set)]].
    bool argument_buffers { true };
    /// Dedicated [[buffer(n)]] slot the push_constant block is remapped to.
    /// Chosen outside the engine's set range (sets 0..1) so it never collides
    /// with an argument-buffer slot.
    std::uint32_t push_constant_buffer_index { 1 };
};

/// Translate SPIR-V to MSL under an explicit binding model (see
/// `MslBindingConfig`). Equivalent to `translate(spirv, Target::kMsl,
/// cfg.version)` for the source text, but additionally pins the descriptor-set
/// -> argument-buffer and push_constant -> dedicated-buffer mapping so the
/// emitted MSL's resource indices are the contract the Metal device binds
/// against.
///
/// No exceptions escape (same policy as `translate`): SPIRV-Cross errors are
/// surfaced through `TranslateResult::error`.
[[nodiscard]] TranslateResult translate_msl(
    std::span<const std::uint32_t> spirv,
    const MslBindingConfig&        cfg
);

}  // namespace cd::spirv_cross_glue
