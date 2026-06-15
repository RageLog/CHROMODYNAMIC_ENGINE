// =============================================================================
// CHROMODYNAMIC — cd/rhi/metal/MetalShaderToolchain.hpp
// phase M3 (Metal backend, ADR-20260530-metal-backend +
// ADR-20260614-d3d12-binding-model §4): the single-source shader chain for the
// Metal backend —
//
//   GLSL (Vulkan-style, the engine corpus)
//     → SPIR-V (cd::shader::ICompiler — glslang; pass X5's CachedCompiler and
//               the SPIR-V half caches automatically)
//     → MSL    (cd::spirv_cross_glue::translate_msl — SPIRV-Cross CompilerMSL)
//
// This TU is PURE HOST-SIDE C++: the output is MSL *text* (a std::string), so
// it needs NO MTLDevice / Metal framework and compiles + tests on Windows. The
// .mm `MetalDevice` consumes the text to build an `MTLLibrary` at runtime on
// Apple; that step is the only Metal-gated half.
//
// Binding model (the contract the .mm argument-encoder must honour): each
// Vulkan descriptor SET N maps to a Metal argument buffer at [[buffer(N)]];
// the push_constant block is remapped to a dedicated [[buffer(n)]] slot. See
// `MslBindingModel` and `cd::spirv_cross_glue::MslBindingConfig`.
//
// Error chaining invariant (mirrors the D3D12 toolchain): whichever stage
// fails, the returned ErrorCode message is prefixed with the stage name
// ("glslang: ..." / "spirv-cross: ...") so debugging across the two-tool chain
// is not blind.
// =============================================================================
#pragma once

#include <cd/core/Result.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/shader/Compiler.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace cd::rhi::metal
{

/// MSL binding convention emitted by the toolchain (the .mm device contract).
///
/// `set N -> [[buffer(N)]]` argument buffer; `push_constant ->
/// [[buffer(push_constant_buffer_index)]]`. The Metal analog of the D3D12
/// space-per-set decision (ADR-20260614-d3d12-binding-model §4): the descriptor
/// set index survives 1:1 into the argument-buffer bind point, and the
/// push range gets one fixed slot regardless of how many sets a shader declares.
/// Canonical push-constant buffer slot. The engine's prim pipeline uses sets
/// 0 (per-prim) and 1 (bindless); a push slot ABOVE that range avoids colliding
/// with any argument-buffer slot. This constant is the canonical engine choice
/// the .mm side binds against.
///
/// Full per-stage [[buffer(N)]] map (see MetalInternal.hpp for the canonical
/// table + rationale): argument-buffer sets [0..7], push_constant [8],
/// vertex-input [9..15], headroom [16..19], SPIRV-Cross aux buffers [20..30].
/// The push slot at [8] sits above the set range and below the vertex range;
/// it MUST equal kMetalPushConstantBufferIndex in MetalInternal.hpp.
inline constexpr std::uint32_t kPushConstantBufferIndex = 8U;

/// Base [[buffer(N)]] index for vertex-input (stage_in) buffers. Mirror of
/// kVertexBufferBaseIndex (MetalInternal.hpp) re-exposed on the public toolchain
/// header so the host-side toolchain test can assert the emitted MSL never
/// places any class in the reserved vertex range [9..15]. Vertex binding B binds
/// at kVertexBufferBaseIndex + B; the range is STRICTLY BELOW the SPIRV-Cross
/// aux floor (kSpirvCrossAuxBaseIndex) so no aux buffer can alias a vertex stream.
inline constexpr std::uint32_t kVertexBufferBaseIndex = 9U;

/// Number of vertex-input buffer slots available [9..15].
inline constexpr std::uint32_t kMaxVertexBufferSlots = 7U;

/// Base [[buffer(N)]] index SPIRV-Cross CompilerMSL's auxiliary buffers are
/// pinned to (Translate.cpp translate_msl_impl). Range [20..30]. Pinning makes
/// the aux indices deterministic + provably disjoint from the vertex range.
inline constexpr std::uint32_t kSpirvCrossAuxBaseIndex = 20U;

struct MslBindingModel
{
    /// Map each descriptor set to its own argument buffer at [[buffer(set)]].
    /// When false, classic flat MSL 1.x binding is emitted (single-set / compute).
    bool argument_buffers { true };
    /// Dedicated [[buffer(n)]] slot the push_constant block is remapped to.
    /// Sits above the engine's set range so it never collides with an
    /// argument-buffer slot.
    std::uint32_t push_constant_buffer_index { kPushConstantBufferIndex };
};

/// GLSL → SPIR-V → MSL end-to-end chain description.
struct GlslToMslDesc
{
    std::string_view glsl_source;  ///< Vulkan-style GLSL. Must be non-empty.
    cd::rhi::ShaderStage stage { cd::rhi::ShaderStage::kVertex };
    std::string_view source_name { "<inline>" };
    /// Packed MSL version (major*10000 + minor*100; e.g. 20200 = MSL 2.2).
    /// 0 = auto-pick (MSL 2.2; raised to 2.0 floor when argument buffers on).
    std::uint32_t msl_version { 0 };
    bool generate_debug_info { false };
    /// Binding convention; defaults to set-per-argument-buffer.
    MslBindingModel binding { .argument_buffers = true,
                              .push_constant_buffer_index = kPushConstantBufferIndex };
    /// Optional include resolver forwarded to the GLSL half — shader-library
    /// modules (`#include <cd/gluon/*.glsl>`) resolve on the Metal path too.
    /// Null bridges to the embedded gluon catalogue inside the .cpp
    /// (ADR-20260614-gluon-consumer-resolver-pattern §2.3).
    cd::shader::IIncludeResolver* include_resolver { nullptr };
};

/// Reflected compute local workgroup size (M6 — ADR-20260615). The GLSL
/// `layout(local_size_x/y/z)` declaration, read from SPIRV-Cross reflection
/// after the SPIR-V -> MSL compile. For non-compute stages every component is
/// 1. This is the threads-per-threadgroup the Metal `.mm` dispatch path needs:
/// Metal's `dispatchThreadgroups:threadsPerThreadgroup:` requires the threads-
/// per-group, and `ComputePipelineDesc` carries no workgroup field, so the
/// shader's reflected local size is the only source of truth.
struct MslWorkgroupSize
{
    std::uint32_t x { 1 };
    std::uint32_t y { 1 };
    std::uint32_t z { 1 };
};

/// MSL output of the chain: the source text + the entry-point name SPIRV-Cross
/// emits. SPIRV-Cross renames the MSL entry point per stage (e.g. a fragment
/// "main" becomes "main0" to dodge MSL's reserved `main`), so the device must
/// look the function up by `entry_point`, not assume "main".
struct MslArtifact
{
    std::string source;       ///< Full MSL text, ready for newLibraryWithSource:.
    std::string entry_point;  ///< MSL function name to look up on the MTLLibrary.
    /// M6: reflected compute local workgroup size. The .mm
    /// create_compute_pipeline path captures this onto the
    /// MetalComputePipelineObj so dispatch() can divide-and-conquer the global
    /// size. 1x1x1 for a non-compute module.
    MslWorkgroupSize workgroup {};
};

/// Run the chain. `spirv_compiler` is caller-owned (X5's CachedCompiler may be
/// passed — the SPIR-V half is then cached automatically). On success the
/// returned `MslArtifact::source` feeds straight into
/// `[device newLibraryWithSource:options:error:]` on the .mm side.
[[nodiscard]] cd::core::Result<MslArtifact>
compose_glsl_to_msl(cd::shader::ICompiler& spirv_compiler, const GlslToMslDesc& desc);

/// SPIR-V → MSL tail of the chain — the same Stage 2 as `compose_glsl_to_msl`,
/// but starting from a pre-built SPIR-V module instead of GLSL source. Used by
/// the Metal `create_shader_module` path when a caller hands the device SPIR-V
/// words. Stage-prefixed error chaining is preserved.
[[nodiscard]] cd::core::Result<MslArtifact>
compose_spirv_to_msl(std::span<const std::uint32_t> spirv, const GlslToMslDesc& desc);

}  // namespace cd::rhi::metal
